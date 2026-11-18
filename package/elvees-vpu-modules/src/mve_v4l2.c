// SPDX-License-Identifier: GPL-2.0-only
/* Stateful V4L2 M2M decode and encode frontend for the Arm MVE firmware driver.
 * MVE owns hardware scheduling; v4l2-m2m provides the standard VB2 queues.
 * Raw frames use the VB2 DMA-BUF storage directly; only compressed data is copied.
 */
#include <linux/dma-buf.h>
#include <linux/kthread.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-event.h>
#include <media/v4l2-ioctl.h>
#include <media/v4l2-mem2mem.h>
#include <media/videobuf2-dma-contig.h>
#include <media/videobuf2-vmalloc.h>
#include "mve_command.h"
#include "mve_session.h"
#include "mve_session_buffer.h"
#include "mve_rsrc_driver.h"
#include "mve_v4l2.h"
#include "host_interface_v2/mve_protocol_def.h"

#define MVE_W 1920U
#define MVE_H 1088U
#define MVE_INPUT_SIZE (2U * 1024 * 1024)
#define MVE_INPUTS 5
/* VB2 requires at least 32 available indices; storage is allocated by userspace. */
#define MVE_OUTPUTS 32
#define MVE_RAW_INPUTS 32U
#define MVE_TOTAL (MVE_RAW_INPUTS + MVE_OUTPUTS)
#define MVE_ENC_OUTPUTS 4U
#define MVE_ENC_BITSTREAM_SIZE (MVE_W * MVE_H * 3 / 2 + 65536)
#define MVE_EOS 1U
#define MVE_END_FRAME 0x10U
#define MVE_READONLY 0x200U

struct mve_video {
	struct media_device media;
	struct v4l2_device v4l2;
	struct video_device video;
	struct v4l2_m2m_dev *m2m;
	struct device *dev;
	struct mutex lock;
	bool encode;
};
struct mve_storage {
	void *mem;
	struct dma_buf *dbuf;
	void *vaddr;
	struct vb2_v4l2_buffer *source;
	struct vb2_buffer *registered_vb;
	struct mve_base_buffer_userspace frame;
	bool busy, ready, release_requested;
};
struct mve_context {
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler controls;
	struct v4l2_ctrl *display_delay_enable, *min_capture;
	struct v4l2_ctrl *bitrate, *bitrate_mode, *gop, *profile, *level, *jpeg_quality, *repeat_header;
	u32 codec, raw_format, fps_num, fps_den;
	bool force_keyframe;
	u8 headers[4096];
	unsigned header_size;
	struct vb2_v4l2_buffer *encoded_capture;
	unsigned encoded_size;
	bool headers_pending;
	struct mve_video *dev;
	/* Private in-kernel MVE client, independent of the V4L2 file's fh. */
	struct file client;
	struct task_struct *thread;
	struct vb2_queue storage_queue;
	struct vb2_buffer storage_vb;
	struct mve_storage storage[MVE_TOTAL];
	unsigned fifo[MVE_OUTPUTS], head, count;
	unsigned width, height, visible_w, visible_h, pitch;
	unsigned sequence, src_sequence, output_state;
	unsigned colorspace, ycbcr_enc, quantization, xfer_func;
	u64 submitted, completed, raw_in_direct, raw_out_direct;
	bool active, failed, sequence_known, draining, eos_sent, drained;
	bool capture_stopping, capture_flushed, firmware_stopped;
};
static struct mve_video *mve_videos[2];

static unsigned input_count(struct mve_context *c)
{
	return c->dev->encode ? MVE_RAW_INPUTS : MVE_INPUTS;
}
static unsigned output_count(struct mve_context *c)
{
	return c->dev->encode ? MVE_ENC_OUTPUTS : MVE_OUTPUTS;
}
static unsigned queue_size(struct mve_context *c, bool output)
{
	if (c->dev->encode)
		return output ? c->pitch * c->height * 3 / 2 : MVE_ENC_BITSTREAM_SIZE;
	return output ? MVE_INPUT_SIZE : c->pitch * c->height * 3 / 2;
}

static struct mve_context *to_ctx(void *priv)
{
	return container_of(priv, struct mve_context, fh);
}
static void reply_free(struct mve_response *r)
{
	if (!r) return;
	if (r->data) MVE_RSRC_MEM_CACHE_FREE(r->data, r->size);
	MVE_RSRC_MEM_CACHE_FREE(r, sizeof(*r));
}
static struct mve_response *command(struct mve_context *c, unsigned cmd,
				  void *data, unsigned size)
{
	struct mve_base_command_header header = { .cmd = cmd, .size = size };
	return mve_command_execute(&header, data, &c->client);
}
static int issue(struct mve_context *c, unsigned cmd, void *data, unsigned size)
{
	struct mve_response *r = command(c, cmd, data, size);
	int ret = (!r || r->error) ? -EIO : 0;
	if (ret) dev_err(c->dev->dev, "V4L2 command %u failed: %u/%u\n", cmd,
			r ? r->error : ~0U, r ? r->firmware_error : 0);
	reply_free(r);
	return ret;
}
static int enqueue(struct mve_context *c, unsigned slot, unsigned size,
		   u64 timestamp, unsigned flags)
{
	struct mve_base_buffer_details p = {
		.buffer_id = slot + 1, .handle = slot + 1,
		.filled_len = size, .timestamp = timestamp, .flags = flags,
	};
	return issue(c, slot < input_count(c) ? MVE_BASE_EMPTY_THIS_BUFFER :
		     MVE_BASE_FILL_THIS_BUFFER, &p, sizeof(p));
}
static void partial_capture_return(struct mve_context *c, enum vb2_buffer_state state)
{
	if (c->encoded_capture) {
		v4l2_m2m_buf_done(c->encoded_capture, state);
		c->encoded_capture = NULL;
	}
	c->encoded_size = 0;
}
static void buffers_return(struct mve_context *c, bool output, enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *b;
	if (!output) partial_capture_return(c, state);
	while ((b = output ? v4l2_m2m_src_buf_remove(c->fh.m2m_ctx) :
			    v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx)))
		v4l2_m2m_buf_done(b, state);
}
static void fail(struct mve_context *c, int error)
{
	unsigned i;
	dev_err(c->dev->dev, "V4L2 codec failed: %d\n", error);
	c->failed = true;
	for (i = 0; i < MVE_TOTAL; ++i) {
		if (c->storage[i].source) {
			v4l2_m2m_buf_done(c->storage[i].source, VB2_BUF_STATE_ERROR);
			c->storage[i].source = NULL;
		}
	}
	buffers_return(c, true, VB2_BUF_STATE_ERROR);
	buffers_return(c, false, VB2_BUF_STATE_ERROR);
	vb2_queue_error(v4l2_m2m_get_src_vq(c->fh.m2m_ctx));
	vb2_queue_error(v4l2_m2m_get_dst_vq(c->fh.m2m_ctx));
}
static int storage_alloc(struct mve_context *c, unsigned i)
{
	struct mve_storage *s = &c->storage[i];
	struct mve_base_buffer_userspace p = {
		.buffer_id = i + 1, .handle = i + 1,
		.allocator = MVE_BASE_BUFFER_ALLOCATOR_DMABUF,
		.stride_alignment = 1,
	};
	struct mve_session *session;
	int ret;
	p.size = c->dev->encode ? MVE_ENC_BITSTREAM_SIZE : MVE_INPUT_SIZE;
	p.format = MVE_BASE_OMX_BUFFER_FORMAT_BITSTREAM;
	s->mem = vb2_vmalloc_memops.alloc(&c->storage_vb, c->dev->dev, p.size);
	if (IS_ERR(s->mem)) { ret = PTR_ERR(s->mem); s->mem = NULL; return ret; }
	s->vaddr = vb2_vmalloc_memops.vaddr(&c->storage_vb, s->mem);
	s->dbuf = vb2_vmalloc_memops.get_dmabuf(&c->storage_vb, s->mem, O_RDWR);
	if (IS_ERR_OR_NULL(s->dbuf)) {
		s->dbuf = NULL; return -ENOMEM;
	}
	session = mve_session_acquire_by_file(&c->client);
	if (!session) return -EIO;
	ret = mve_session_buffer_register_kernel(session, i < input_count(c) ? 0 : 1, &p, s->dbuf);
	mve_session_release(session);
	return ret ? -EIO : 0;
}
static void session_destroy(struct mve_context *c)
{
	unsigned i;
	partial_capture_return(c, VB2_BUF_STATE_ERROR);
	if (c->active) {
		issue(c, MVE_BASE_DESTROY_SESSION, NULL, 0);
		c->active = false;
		dev_info(c->dev->dev, "V4L2 session closed: input=%llu output=%llu raw-in-direct=%llu raw-out-direct=%llu\n",
			 c->submitted, c->completed, c->raw_in_direct, c->raw_out_direct);
	}
	for (i = 0; i < MVE_TOTAL; ++i) {
		struct mve_storage *s = &c->storage[i];
		if (s->source) v4l2_m2m_buf_done(s->source, VB2_BUF_STATE_ERROR);
		if (s->dbuf) dma_buf_put(s->dbuf);
		if (s->mem) vb2_vmalloc_memops.put(s->mem);
		memset(s, 0, sizeof(*s));
	}
	c->head = c->count = c->output_state = 0;
	c->sequence_known = c->draining = c->eos_sent = c->drained = false;
	c->capture_stopping = c->capture_flushed = c->firmware_stopped = false;
	c->submitted = c->completed = c->raw_in_direct = c->raw_out_direct = 0;
	c->header_size = 0; c->headers_pending = false;
}
static int set_option(struct mve_context *c, u32 index, u32 value)
{
	u32 message[] = { MVE_REQUEST_CODE_SET_OPTION, index, value };
	return issue(c, MVE_BASE_SET_PARAMETER, message, sizeof(message));
}
static int set_buffer_parameter(struct mve_context *c, u32 type, u32 a, u32 b, bool pair)
{
	u32 message[] = { MVE_BUFFER_CODE_PARAM, type, a, b };
	return issue(c, MVE_BASE_SET_CONFIG, message, pair ? sizeof(message) : sizeof(message) - sizeof(u32));
}
static int encoder_rate(struct mve_context *c)
{
	int ret;
	if (c->codec == V4L2_PIX_FMT_JPEG) return 0;
	ret = set_buffer_parameter(c, MVE_BUFFER_PARAM_TYPE_FRAME_RATE,
		(u32)div_u64((u64)c->fps_num << 16, c->fps_den), 0, false);
	if (!ret)
		ret = set_buffer_parameter(c, MVE_BUFFER_PARAM_TYPE_RATE_CONTROL,
			c->bitrate_mode->val == V4L2_MPEG_VIDEO_BITRATE_MODE_CBR ?
			MVE_OPT_RATE_CONTROL_MODE_CONSTANT : MVE_OPT_RATE_CONTROL_MODE_VARIABLE,
			c->bitrate->val, true);
	return ret;
}
static int jpeg_options(struct mve_context *c)
{
	static const u8 tables[2][64] = {
		{ 16,11,10,16,24,40,51,61, 12,12,14,19,26,58,60,55,
		  14,13,16,24,40,57,69,56, 14,17,22,29,51,87,80,62,
		  18,22,37,56,68,109,103,77, 24,35,55,64,81,104,113,92,
		  49,64,78,87,103,121,120,101, 72,92,95,98,112,100,103,99 },
		{ 17,18,24,47,99,99,99,99, 18,21,26,66,99,99,99,99,
		  24,26,56,99,99,99,99,99, 47,66,99,99,99,99,99,99,
		  99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99,
		  99,99,99,99,99,99,99,99, 99,99,99,99,99,99,99,99 }
	};
	struct { u32 code, index, type; u8 matrix[64]; } message = {
		.code = MVE_REQUEST_CODE_SET_OPTION, .index = MVE_SET_OPT_INDEX_QUANT_TABLE,
	};
	unsigned q = c->jpeg_quality->val, scale = q < 50 ? 5000 / q : 200 - 2 * q;
	unsigned i, j;
	int ret;
	for (i = 0; i < 2; ++i) {
		message.type = i ? MVE_OPT_QUANT_TABLE_CHROMA : MVE_OPT_QUANT_TABLE_LUMA;
		for (j = 0; j < 64; ++j)
			message.matrix[j] = clamp_t(unsigned, (tables[i][j] * scale + 50) / 100, 1, 255);
		ret = issue(c, MVE_BASE_SET_PARAMETER, &message, sizeof(message));
		if (ret) return ret;
	}
	return 0;
}
static int encoder_options(struct mve_context *c)
{
	int ret;
	if (c->codec == V4L2_PIX_FMT_JPEG)
		return jpeg_options(c);
	if (c->codec == V4L2_PIX_FMT_VP8)
		return set_option(c, MVE_SET_OPT_INDEX_ENC_P_FRAMES, c->gop->val - 1);
	ret = set_option(c, MVE_SET_OPT_INDEX_NALU_FORMAT, MVE_OPT_NALU_FORMAT_START_CODES);
	if (!ret) ret = set_option(c, MVE_SET_OPT_INDEX_PROFILE_LEVEL,
		(c->profile->val == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH ? MVE_OPT_PROFILE_H264_HIGH :
		 c->profile->val == V4L2_MPEG_VIDEO_H264_PROFILE_MAIN ? MVE_OPT_PROFILE_H264_MAIN :
		 MVE_OPT_PROFILE_H264_BASELINE) | ((c->level->val + 1) << 16));
	if (!ret) ret = set_option(c, MVE_SET_OPT_INDEX_ENC_H264_CABAC,
		c->profile->val == V4L2_MPEG_VIDEO_H264_PROFILE_MAIN ||
		c->profile->val == V4L2_MPEG_VIDEO_H264_PROFILE_HIGH);
	if (!ret) ret = set_option(c, MVE_SET_OPT_INDEX_ENC_P_FRAMES, c->gop->val - 1);
	if (!ret) ret = set_option(c, MVE_SET_OPT_INDEX_ENC_B_FRAMES, 0);
	if (!ret) ret = set_option(c, MVE_SET_OPT_INDEX_GOP_TYPE, MVE_OPT_GOP_TYPE_LOW_DELAY);
	return ret;
}
static int session_create(struct mve_context *c)
{
	const char *role = c->dev->encode ?
		(c->codec == V4L2_PIX_FMT_H264 ? "video_encoder.avc" :
		 c->codec == V4L2_PIX_FMT_VP8 ? "video_encoder.vp8" : "video_encoder.jpeg") :
		(c->codec == V4L2_PIX_FMT_VP8 ? "video_decoder.vp8" : "video_decoder.avc");
	u32 opt[] = { 1010, 6, 0x80 }, running = MVE_BASE_HW_STATE_RUNNING;
	unsigned i;
	int ret;
	ret = issue(c, MVE_BASE_CREATE_SESSION, (void *)role, strlen(role) + 1);
	if (ret) return ret;
	c->active = true;
	ret = issue(c, MVE_BASE_ACTIVATE_SESSION, NULL, 0);
	if (ret) goto error;
	ret = issue(c, MVE_BASE_SET_PARAMETER, opt, sizeof(opt));
	if (ret) goto error;
	if (c->dev->encode) {
		ret = encoder_options(c);
		if (!ret) ret = encoder_rate(c);
	} else if (c->codec == V4L2_PIX_FMT_H264) {
		/* Default presentation order; clients may explicitly request decode order. */
		opt[1] = 8; opt[2] = !v4l2_ctrl_g_ctrl(c->display_delay_enable);
		ret = issue(c, MVE_BASE_SET_PARAMETER, opt, sizeof(opt));
	}
	if (ret) goto error;
	for (i = c->dev->encode ? input_count(c) : 0;
	     i < (c->dev->encode ? input_count(c) + output_count(c) : input_count(c)); ++i) {
		ret = storage_alloc(c, i);
		if (ret) goto error;
	}
	ret = issue(c, MVE_BASE_ENQUEUE_STATE_CHANGE, &running, sizeof(running));
	if (!ret && c->dev->encode)
		for (i = input_count(c); i < input_count(c) + output_count(c); ++i) {
			ret = enqueue(c, i, 0, 0, 0);
			if (ret) break;
		}
	if (!ret) return 0;
error:
	session_destroy(c);
	return ret;
}
static int frames_deliver(struct mve_context *c)
{
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(c->fh.m2m_ctx);
	while (c->dev->encode && c->count && vb2_is_streaming(q)) {
		struct vb2_v4l2_buffer *b = c->encoded_capture ?: v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
		unsigned slot = c->fifo[c->head];
		struct mve_storage *s = &c->storage[slot];
		struct mve_base_buffer_userspace *f = &s->frame;
		u8 *dst, *src = s->vaddr;
		int ret;
		if (!b) break;
		c->encoded_capture = NULL;
		if (vb2_plane_size(&b->vb2_buf, 0) < queue_size(c, false)) {
			/* A source-change event lets the client replace old capture storage. */
			v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR);
			c->encoded_size = 0;
			continue;
		}
		if (c->dev->encode && f->filled_len) {
			unsigned prefix = c->codec == V4L2_PIX_FMT_H264 &&
				((c->repeat_header->val && (f->flags & 0x20)) || !c->sequence) ? c->header_size : 0;
			c->headers_pending = false;
			if (f->offset > MVE_ENC_BITSTREAM_SIZE ||
			    f->filled_len > MVE_ENC_BITSTREAM_SIZE - f->offset ||
			    c->encoded_size > vb2_plane_size(&b->vb2_buf, 0) ||
			    f->filled_len + prefix > vb2_plane_size(&b->vb2_buf, 0) - c->encoded_size) {
				v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR); return -EOVERFLOW;
			}
			dma_buf_begin_cpu_access(s->dbuf, DMA_FROM_DEVICE);
			dst = vb2_plane_vaddr(&b->vb2_buf, 0);
			memcpy(dst + c->encoded_size, c->headers, prefix);
			memcpy(dst + c->encoded_size + prefix, src + f->offset, f->filled_len);
			c->encoded_size += prefix + f->filled_len;
			dma_buf_end_cpu_access(s->dbuf, DMA_FROM_DEVICE);
			vb2_set_plane_payload(&b->vb2_buf, 0, c->encoded_size);
		}
		/* Publish WC/coherent capture stores before userspace/GPU sees DONE. */
		dma_wmb();
		if (!c->dev->encode || !f->filled_len)
			vb2_set_plane_payload(&b->vb2_buf, 0, f->filled_len ? c->pitch * c->height * 3 / 2 : 0);
		b->vb2_buf.timestamp = f->timestamp;
		b->field = V4L2_FIELD_NONE;
		b->sequence = c->sequence;
		if (f->flags & 0x20) b->flags |= V4L2_BUF_FLAG_KEYFRAME;
		if (f->flags & MVE_EOS) {
			struct v4l2_event ev = { .type = V4L2_EVENT_EOS };
			b->flags |= V4L2_BUF_FLAG_LAST;
			c->drained = true;
			v4l2_event_queue_fh(&c->fh, &ev);
		}
		/* VP8 firmware emits the mode and coefficient partitions separately.
		 * Assemble compressed bytes in the client's CAPTURE buffer; publish
		 * only a complete frame. Raw input DMA-BUF storage is unchanged. */
		if (c->codec == V4L2_PIX_FMT_VP8 && f->filled_len && !(f->flags & (MVE_END_FRAME | MVE_EOS)))
			c->encoded_capture = b;
		else {
			c->encoded_size = 0;
			++c->sequence;
			v4l2_m2m_buf_done(b, VB2_BUF_STATE_DONE);
		}
		s->ready = false; c->head = (c->head + 1) % MVE_OUTPUTS; --c->count;
		if (!c->drained) {
			ret = enqueue(c, slot, 0, 0, 0);
			if (ret) return ret;
		}
	}
	return 0;
}
/* Register the V4L2 raw storage itself. The queue owns it until firmware
 * returns INPUT/OUTPUT; mappings outlive STREAMOFF while DPB references remain.
 * Imported DMA-BUFs may change on each QBUF, so compare the actual object too. */
static int register_raw(struct mve_context *c, unsigned slot, struct vb2_buffer *vb)
{
	struct mve_storage *storage = &c->storage[slot];
	struct dma_buf *dbuf = vb->planes[0].dbuf;
	struct mve_base_buffer_userspace info = {
		.buffer_id = slot + 1, .handle = slot + 1,
		.allocator = MVE_BASE_BUFFER_ALLOCATOR_DMABUF,
		.width = c->width, .height = c->height, .stride = c->pitch,
		.stride_alignment = 1,
		.format = c->raw_format == V4L2_PIX_FMT_NV12 ?
			MVE_BASE_OMX_BUFFER_FORMAT_YUV420_SEMIPLANAR : MVE_BASE_OMX_BUFFER_FORMAT_YUV420_PLANAR,
	};
	struct mve_session *session;
	int ret;
	if (storage->registered_vb == vb && storage->dbuf &&
	    (vb->memory != VB2_MEMORY_DMABUF || dbuf == storage->dbuf)) return 0;
	if (vb->planes[0].data_offset) return -EINVAL;
	if (!c->dev->encode && storage->dbuf && ((storage->frame.flags & MVE_READONLY) || storage->release_requested)) return -EBUSY;
	if (vb->memory == VB2_MEMORY_DMABUF) {
		if (!dbuf) return -EINVAL;
		get_dma_buf(dbuf);
	} else if (vb->memory == VB2_MEMORY_MMAP) {
		dbuf = vb->vb2_queue->mem_ops->get_dmabuf(vb, vb->planes[0].mem_priv, O_RDWR);
		if (IS_ERR_OR_NULL(dbuf)) return dbuf ? PTR_ERR(dbuf) : -ENOMEM;
	} else return -EINVAL;
	if (dbuf->size < queue_size(c, c->dev->encode) || dbuf->size > U32_MAX) {
		dma_buf_put(dbuf); return -EINVAL;
	}
	info.size = dbuf->size;
	session = mve_session_acquire_by_file(&c->client);
	if (!session) { dma_buf_put(dbuf); return -EIO; }
	ret = storage->dbuf ? mve_session_buffer_unregister_internal(session, slot + 1) : 0;
	if (!ret) {
		if (storage->dbuf) dma_buf_put(storage->dbuf);
		storage->dbuf = NULL; storage->registered_vb = NULL;
		ret = mve_session_buffer_register_kernel(session, c->dev->encode ? 0 : 1, &info, dbuf);
	}
	mve_session_release(session);
	if (ret) { dma_buf_put(dbuf); return -EIO; }
	storage->dbuf = dbuf; storage->registered_vb = vb;
	storage->frame.flags = 0; storage->release_requested = false;
	return 0;
}
/* Once userspace returns a displayed CAPTURE buffer, firmware may reuse it.
 * REF_FRAME pictures are read-only while held by userspace; re-enqueueing the
 * same storage is the firmware protocol's normal reference-release path. */
static int decoder_outputs_feed(struct mve_context *c)
{
	struct vb2_v4l2_buffer *buffer;
	if (c->dev->encode || c->output_state != 2 || c->drained || c->capture_stopping ||
		!vb2_is_streaming(v4l2_m2m_get_dst_vq(c->fh.m2m_ctx))) return 0;
	while ((buffer = v4l2_m2m_next_dst_buf(c->fh.m2m_ctx))) {
		struct vb2_buffer *vb = &buffer->vb2_buf;
		struct mve_storage *storage;
		unsigned slot, candidate = MVE_TOTAL;
		int ret;
		for (slot = MVE_INPUTS; slot < MVE_INPUTS + MVE_OUTPUTS; ++slot) {
			storage = &c->storage[slot];
			if (!storage->busy && storage->registered_vb == vb &&
				(vb->memory != VB2_MEMORY_DMABUF || storage->dbuf == vb->planes[0].dbuf)) {
				candidate = slot; break;
			}
		}
		if (candidate == MVE_TOTAL)
			for (slot = MVE_INPUTS; slot < MVE_INPUTS + MVE_OUTPUTS; ++slot) {
				storage = &c->storage[slot];
				if (storage->busy) continue;
				if ((storage->frame.flags & MVE_READONLY) || storage->release_requested) {
					if (!storage->release_requested) {
						mve_base_buffer_handle_t id = slot + 1;
						ret = issue(c, MVE_BASE_NOTIFY_REF_FRAME_RELEASE, &id, sizeof(id));
						if (ret) return ret;
						storage->release_requested = true;
					}
				} else if (candidate == MVE_TOTAL) candidate = slot;
			}
		if (candidate == MVE_TOTAL) break;
		slot = candidate; storage = &c->storage[slot];
		buffer = v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
		ret = register_raw(c, slot, vb);
		if (ret) { v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR); return ret; }
		storage->source = buffer; storage->busy = true;
		ret = enqueue(c, slot, 0, 0, 0);
		if (ret) return ret;
	}
	return 0;
}
static int encoder_inputs_feed(struct mve_context *c)
{
	while (!c->eos_sent) {
		struct vb2_v4l2_buffer *buffer = v4l2_m2m_next_src_buf(c->fh.m2m_ctx);
		struct mve_storage *storage;
		unsigned slot, size = 0, flags = MVE_END_FRAME;
		int ret;
		if (buffer) {
			slot = buffer->vb2_buf.index;
			if (slot >= MVE_RAW_INPUTS) return -EINVAL;
			storage = &c->storage[slot];
			if (storage->busy) break;
			buffer = v4l2_m2m_src_buf_remove(c->fh.m2m_ctx);
			size = vb2_get_plane_payload(&buffer->vb2_buf, 0) ? queue_size(c, true) : 0;
			ret = register_raw(c, slot, &buffer->vb2_buf);
			if (ret) { v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR); return ret; }
		} else {
			if (!c->draining) break;
			for (slot = 0; slot < MVE_RAW_INPUTS; ++slot)
				if (c->storage[slot].dbuf && !c->storage[slot].busy) break;
			if (slot == MVE_RAW_INPUTS) break;
			storage = &c->storage[slot];
		}
		if (!size) { flags |= MVE_EOS; c->eos_sent = c->draining = true; }
		if (size && c->force_keyframe && c->codec != V4L2_PIX_FMT_JPEG) {
			u32 reset[] = { MVE_BUFFER_CODE_PARAM, MVE_BUFFER_PARAM_TYPE_GOP_RESET };
			ret = issue(c, MVE_BASE_SET_CONFIG, reset, sizeof(reset));
			if (ret) { if (buffer) v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_ERROR); return ret; }
			c->force_keyframe = false;
		}
		storage->source = buffer; storage->busy = true;
		if (buffer) { buffer->sequence = c->src_sequence++; buffer->field = V4L2_FIELD_NONE; }
		ret = enqueue(c, slot, size, buffer ? buffer->vb2_buf.timestamp : 0, flags);
		if (ret) return ret;
		if (size) { ++c->submitted; ++c->raw_in_direct; }
	}
	return 0;
}
static int inputs_feed(struct mve_context *c)
{
	unsigned i;
	if (c->dev->encode) return encoder_inputs_feed(c);
	for (i = 0; i < input_count(c); ++i) {
		struct mve_storage *s = &c->storage[i];
		struct vb2_v4l2_buffer *b;
		unsigned size, offset, flags = MVE_END_FRAME;
		int ret;
		if (s->busy || c->eos_sent) continue;
		b = v4l2_m2m_src_buf_remove(c->fh.m2m_ctx);
		if (!b && !c->draining) break;
		offset = b ? b->vb2_buf.planes[0].data_offset : 0;
		size = b ? vb2_get_plane_payload(&b->vb2_buf, 0) - offset : 0;
		if (size > queue_size(c, true)) {
			v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR); return -EINVAL;
		}
		if (size) {
			dma_buf_begin_cpu_access(s->dbuf, DMA_TO_DEVICE);
			memcpy(s->vaddr, vb2_plane_vaddr(&b->vb2_buf, 0) + offset, size);
			dma_buf_end_cpu_access(s->dbuf, DMA_TO_DEVICE);
		} else {
			flags |= MVE_EOS; c->eos_sent = true; c->draining = true;
		}

		s->source = b; s->busy = true;
		if (b) { b->sequence = c->src_sequence++; b->field = V4L2_FIELD_NONE; }
		ret = enqueue(c, i, size, b ? b->vb2_buf.timestamp : 0, flags);
		if (ret) return ret;
		if (size) ++c->submitted;
	}
	return 0;
}
static int event_handle(struct mve_context *c, struct mve_response *r)
{
	struct mve_base_event_header *ev = r->data;
	struct mve_base_buffer_userspace *f;
	unsigned slot;
	if (r->size < sizeof(*ev) || r->size != sizeof(*ev) + ev->size) return -EIO;
	if (ev->code == MVE_BASE_EVENT_ERROR || ev->code == MVE_BASE_EVENT_SESSION_HUNG) return -EIO;
	if (ev->code == MVE_BASE_EVENT_STATE_CHANGED) {
		u32 state;
		if (ev->size != sizeof(state)) return -EIO;
		memcpy(&state, ev->data, sizeof(state));
		c->firmware_stopped = state == MVE_BASE_HW_STATE_STOPPED;
		return 0;
	}
	if (ev->code == MVE_BASE_EVENT_SEQUENCE_PARAMS) {
		const struct mve_base_response_sequence_parameters *p = (void *)ev->data;
		if (ev->size != sizeof(*p)) return -EIO;
		dev_info(c->dev->dev, "V4L2 firmware requests %u output buffers (pool %u)\n",
			 p->num_buffers_planar, MVE_OUTPUTS);
		if (p->num_buffers_planar > MVE_OUTPUTS) return -ENOSPC;
		/* Advertise the firmware's working set before userspace allocates its
		 * capture pool (including any additional downstream buffers). */
		if (!c->dev->encode)
			return v4l2_ctrl_s_ctrl(c->min_capture, max(4U, p->num_buffers_planar));
		return 0;
	}
	if (ev->code == MVE_BASE_EVENT_ALLOC_PARAMS && !c->dev->encode) {
		u32 flush = MVE_BASE_FLUSH_OUTPUT_PORT;
		const struct mve_base_fw_frame_alloc_parameters *p = (void *)ev->data;
		if (ev->size != sizeof(*p)) return -EIO;
		dev_info(c->dev->dev, "V4L2 raw allocation %ux%u, capture %ux%u stride %u\n",
			p->planar_alloc_frame_width, p->planar_alloc_frame_height, c->width, c->height, c->pitch);
		if (p->planar_alloc_frame_width > c->pitch || p->planar_alloc_frame_height > c->height) return -ENOSPC;
		if (c->output_state) return -EOPNOTSUPP;
		c->output_state = 1;
		return issue(c, MVE_BASE_ENQUEUE_FLUSH_BUFFERS, &flush, sizeof(flush));
	}
	if (ev->code == MVE_BASE_EVENT_REF_FRAME_RELEASED) {
		mve_base_buffer_handle_t id;
		if (ev->size != sizeof(id)) return -EIO;
		memcpy(&id, ev->data, sizeof(id));
		if (id <= MVE_INPUTS || id > MVE_INPUTS + MVE_OUTPUTS) return -EIO;
		c->storage[id - 1].frame.flags &= ~MVE_READONLY;
		c->storage[id - 1].release_requested = false;
		return 0;
	}
	if (ev->code == MVE_BASE_EVENT_OUTPUT_FLUSHED && c->capture_stopping) {
		c->capture_flushed = true; return 0;
	}
	if (ev->code == MVE_BASE_EVENT_OUTPUT_FLUSHED && c->output_state == 1) {
		struct v4l2_event change = { .type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION };
		c->output_state = 2;

		/* Input dimensions come from the client's parsed SPS. Notify before
		 * decoded pictures, so clients can start CAPTURE without a DPB wait. */
		v4l2_event_queue_fh(&c->fh, &change);
		return 0;
	}
	if (ev->code != MVE_BASE_EVENT_INPUT && ev->code != MVE_BASE_EVENT_OUTPUT) return 0;
	if (ev->size != sizeof(*f)) return -EIO;
	f = (void *)ev->data;
	if (!f->buffer_id || f->buffer_id > MVE_TOTAL) return -EIO;
	slot = f->buffer_id - 1;
	if (ev->code == MVE_BASE_EVENT_INPUT) {
		struct mve_storage *s = &c->storage[slot];
		if (slot >= input_count(c)) return -EIO;
		if (s->source) v4l2_m2m_buf_done(s->source, VB2_BUF_STATE_DONE);
		s->source = NULL; s->busy = false;
		return 0;
	}
	if (slot < input_count(c) || slot >= input_count(c) + output_count(c)) return -EIO;
	if (c->dev->encode && c->codec == V4L2_PIX_FMT_H264 && (f->flags & 0x80)) {
		struct mve_storage *storage = &c->storage[slot];
		if (!c->headers_pending) c->header_size = 0;
		c->headers_pending = true;
		if (f->offset > MVE_ENC_BITSTREAM_SIZE || f->filled_len > sizeof(c->headers) - c->header_size ||
		    f->filled_len > MVE_ENC_BITSTREAM_SIZE - f->offset) return -EOVERFLOW;
		dma_buf_begin_cpu_access(storage->dbuf, DMA_FROM_DEVICE);
		memcpy(c->headers + c->header_size, storage->vaddr + f->offset, f->filled_len);
		dma_buf_end_cpu_access(storage->dbuf, DMA_FROM_DEVICE);
		c->header_size += f->filled_len;
		return enqueue(c, slot, 0, 0, 0);
	}
	if (!c->dev->encode && c->capture_stopping) {
		struct mve_storage *storage = &c->storage[slot];
		storage->frame = *f; storage->busy = false;
		if (storage->source) v4l2_m2m_buf_done(storage->source, VB2_BUF_STATE_ERROR);
		storage->source = NULL;
		return 0;
	}
	if (!f->filled_len && !(f->flags & MVE_EOS))
		return enqueue(c, slot, 0, 0, 0);
	if (!c->dev->encode && !c->sequence_known && f->filled_len) {
		if (!f->decoded_width || !f->decoded_height || f->decoded_width > MVE_W || f->decoded_height > MVE_H)
			return -EINVAL;
		if (ALIGN(f->decoded_width, 16) != c->width ||
		    ALIGN(f->decoded_height, 16) != c->height) return -EOPNOTSUPP;
		c->width = ALIGN(f->decoded_width, 16);
		c->height = ALIGN(f->decoded_height, 16);
		c->visible_w = min(c->visible_w, f->decoded_width);
		c->visible_h = min(c->visible_h, f->decoded_height);
		c->pitch = ALIGN(c->width, 64);
		c->sequence_known = true;
		dev_info(c->dev->dev, "V4L2 decoded sequence %ux%u, visible %ux%u\n",
			c->width, c->height, c->visible_w, c->visible_h);
	}
	if (!c->dev->encode) {
		struct mve_storage *storage = &c->storage[slot];
		struct vb2_v4l2_buffer *buffer = storage->source;
		if (!buffer || !storage->busy) return -EIO;
		storage->frame = *f;
		if (f->filled_len) { ++c->completed; ++c->raw_out_direct; }
		vb2_set_plane_payload(&buffer->vb2_buf, 0, f->filled_len ? queue_size(c, false) : 0);
		buffer->vb2_buf.timestamp = f->timestamp;
		buffer->field = V4L2_FIELD_NONE; buffer->sequence = c->sequence++;
		if (f->flags & MVE_EOS) {
			struct v4l2_event event = { .type = V4L2_EVENT_EOS };
			buffer->flags |= V4L2_BUF_FLAG_LAST; c->drained = true;
			v4l2_event_queue_fh(&c->fh, &event);
		}
		storage->source = NULL; storage->busy = false;
		v4l2_m2m_buf_done(buffer, VB2_BUF_STATE_DONE);
		return 0;
	}
	if (c->count == output_count(c) || c->storage[slot].ready) return -EIO;
	c->storage[slot].frame = *f; c->storage[slot].ready = true;
	c->fifo[(c->head + c->count++) % MVE_OUTPUTS] = slot;
	if (f->filled_len && (c->codec != V4L2_PIX_FMT_VP8 || (f->flags & MVE_END_FRAME))) ++c->completed;
	return 0;
}
static int decode_thread(void *opaque)
{
	struct mve_context *c = opaque;
	int ret;
	while (!kthread_should_stop()) {
		struct mve_response *r;
		u32 timeout = 10;
		mutex_lock(&c->dev->lock);
		ret = frames_deliver(c);
		if (!ret) ret = decoder_outputs_feed(c);
		if (!ret) ret = inputs_feed(c);
		mutex_unlock(&c->dev->lock);
		if (ret) goto error;
		r = command(c, MVE_BASE_GET_EVENT, &timeout, sizeof(timeout));
		mutex_lock(&c->dev->lock);
		ret = !r ? -ENOMEM : r->error == MVE_BASE_ERROR_TIMEOUT ? 0 :
			r->error ? -EIO : event_handle(c, r);
		reply_free(r);
		mutex_unlock(&c->dev->lock);
		if (ret) goto error;
	}
	return 0;
error:
	mutex_lock(&c->dev->lock); fail(c, ret); mutex_unlock(&c->dev->lock);
	while (!kthread_should_stop()) {
		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}
	return 0;
}
static int queue_setup(struct vb2_queue *q, unsigned *nbufs, unsigned *nplanes,
		       unsigned sizes[], struct device *alloc_devs[])
{
	struct mve_context *c = vb2_get_drv_priv(q);
	unsigned planes = 1;
	unsigned need[] = { queue_size(c, V4L2_TYPE_IS_OUTPUT(q->type)) }, i;
	if (*nplanes) {
		if (*nplanes != planes) return -EINVAL;
		for (i = 0; i < planes; ++i) if (sizes[i] < need[i]) return -EINVAL;
	} else {
		*nplanes = planes;
		for (i = 0; i < planes; ++i) sizes[i] = need[i];
	}
	return 0;
}
static int buffer_prepare(struct vb2_buffer *b)
{
	struct mve_context *c = vb2_get_drv_priv(b->vb2_queue);
	unsigned i, planes = 1;
	unsigned need[] = { queue_size(c, V4L2_TYPE_IS_OUTPUT(b->type)) };
	for (i = 0; i < planes; ++i) {
		if (vb2_plane_size(b, i) < need[i]) return -EINVAL;
		if (c->dev->encode != V4L2_TYPE_IS_OUTPUT(b->type) && !vb2_plane_vaddr(b, i)) return -ENOMEM;
	}
	if (!c->dev->encode && !V4L2_TYPE_IS_OUTPUT(b->type) &&
		(b->index >= MVE_OUTPUTS || b->planes[0].data_offset)) return -EINVAL;
	if (c->dev->encode && V4L2_TYPE_IS_OUTPUT(b->type) &&
	    (b->index >= MVE_RAW_INPUTS || b->planes[0].data_offset ||
	     (vb2_get_plane_payload(b, 0) && vb2_get_plane_payload(b, 0) < queue_size(c, true)))) return -EINVAL;
	if (V4L2_TYPE_IS_OUTPUT(b->type) && (vb2_get_plane_payload(b, 0) > vb2_plane_size(b, 0) ||
	    b->planes[0].data_offset > vb2_get_plane_payload(b, 0))) return -EINVAL;
	return 0;
}
static void buffer_queue(struct vb2_buffer *b)
{
	struct mve_context *c = vb2_get_drv_priv(b->vb2_queue);
	v4l2_m2m_buf_queue(c->fh.m2m_ctx, to_vb2_v4l2_buffer(b));
}
static int start_streaming(struct vb2_queue *q, unsigned count)
{
	struct mve_context *c = vb2_get_drv_priv(q);
	int ret;
	if (!V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (!c->dev->encode && c->active && !c->thread) {
			u32 running = MVE_BASE_HW_STATE_RUNNING;
			ret = issue(c, MVE_BASE_ENQUEUE_STATE_CHANGE, &running, sizeof(running));
			if (ret) { buffers_return(c, false, VB2_BUF_STATE_QUEUED); return ret; }
			c->capture_stopping = false; c->firmware_stopped = false;
			c->thread = kthread_run(decode_thread, c, "mve-v4l2");
			if (IS_ERR(c->thread)) {
				ret = PTR_ERR(c->thread); c->thread = NULL;
				buffers_return(c, false, VB2_BUF_STATE_QUEUED);
				return ret;
			}
		}
		return 0;
	}
	c->failed = false; c->sequence = c->src_sequence = 0;
	ret = session_create(c);
	if (ret) goto error;
	c->thread = kthread_run(decode_thread, c, "mve-v4l2");
	if (IS_ERR(c->thread)) {
		ret = PTR_ERR(c->thread); c->thread = NULL;
		session_destroy(c); goto error;
	}
	return 0;
error:
	buffers_return(c, true, VB2_BUF_STATE_QUEUED);
	return ret;
}
static void worker_stop(struct mve_context *c)
{
	if (c->thread) {
		struct task_struct *thread = c->thread;
		c->thread = NULL;
		mutex_unlock(&c->dev->lock);
		kthread_stop(thread);
		mutex_lock(&c->dev->lock);
	}
}
/* Called with the worker stopped and the queue mutex held. */
static int wait_firmware(struct mve_context *c, bool *done)
{
	unsigned long deadline = jiffies + 2 * HZ;
	int ret = 0;
	while (!ret && !*done && time_before(jiffies, deadline)) {
		u32 timeout = 10;
		struct mve_response *response = command(c, MVE_BASE_GET_EVENT, &timeout, sizeof(timeout));
		ret = !response ? -ENOMEM : response->error == MVE_BASE_ERROR_TIMEOUT ? 0 :
			response->error ? -EIO : event_handle(c, response);
		reply_free(response);
	}
	return ret ? ret : (*done ? 0 : -ETIMEDOUT);
}
static void stop_streaming(struct vb2_queue *q)
{
	struct mve_context *c = vb2_get_drv_priv(q);
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		worker_stop(c);
		session_destroy(c);
	} else if (!c->dev->encode && c->active) {
		u32 stopped = MVE_BASE_HW_STATE_STOPPED, flush = MVE_BASE_FLUSH_OUTPUT_PORT;
		unsigned i;
		int ret;
		worker_stop(c);
		c->capture_stopping = true; c->capture_flushed = false;
		/* H.264 firmware permits output flush only while stopped (or during
		 * its initial sequence handshake). Preserve the input/DPB state. */
		ret = issue(c, MVE_BASE_ENQUEUE_STATE_CHANGE, &stopped, sizeof(stopped));
		if (!ret) ret = wait_firmware(c, &c->firmware_stopped);
		if (!ret) ret = issue(c, MVE_BASE_ENQUEUE_FLUSH_BUFFERS, &flush, sizeof(flush));
		if (!ret) ret = wait_firmware(c, &c->capture_flushed);
		for (i = MVE_INPUTS; !ret && i < MVE_INPUTS + MVE_OUTPUTS; ++i) {
			struct mve_storage *storage = &c->storage[i];
			/* Keep mappings for DPB references after STREAMOFF; a new
			 * CAPTURE pool uses other slots while firmware releases them. */
			if (storage->busy || storage->source) { ret = -EIO; break; }
			storage->registered_vb = NULL;
			if ((storage->frame.flags & MVE_READONLY) && !storage->release_requested) {
				mve_base_buffer_handle_t id = i + 1;
				ret = issue(c, MVE_BASE_NOTIFY_REF_FRAME_RELEASE, &id, sizeof(id));
				storage->release_requested = !ret;
			}
		}
		if (ret) { fail(c, ret); session_destroy(c); }
	}
	buffers_return(c, V4L2_TYPE_IS_OUTPUT(q->type), VB2_BUF_STATE_ERROR);
}
static const struct vb2_ops queue_ops = {
	.queue_setup = queue_setup, .buf_prepare = buffer_prepare,
	.buf_queue = buffer_queue, .start_streaming = start_streaming,
	.stop_streaming = stop_streaming,
	.wait_prepare = vb2_ops_wait_prepare, .wait_finish = vb2_ops_wait_finish,
};
static int queues_init(void *priv, struct vb2_queue *src, struct vb2_queue *dst)
{
	struct mve_context *c = priv;
	struct vb2_queue *q[] = { src, dst };
	unsigned i;
	int ret;
	for (i = 0; i < 2; ++i) {
		q[i]->type = i ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		q[i]->io_modes = VB2_MMAP | VB2_DMABUF;
		if (!i && c->dev->encode) q[i]->max_num_buffers = MVE_RAW_INPUTS;
		if (i && !c->dev->encode) q[i]->max_num_buffers = MVE_OUTPUTS;
		q[i]->drv_priv = c; q[i]->ops = &queue_ops;
		q[i]->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
		q[i]->mem_ops = i && !c->dev->encode ? &vb2_dma_contig_memops : &vb2_vmalloc_memops;
		q[i]->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
		q[i]->lock = &c->dev->lock; q[i]->dev = c->dev->dev;
		ret = vb2_queue_init(q[i]); if (ret) return ret;
	}
	return 0;
}
static int querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "mve-v4l2", sizeof(cap->driver));
	struct mve_video *d = video_drvdata(file);
	strscpy(cap->card, d->encode ? "MVE H264/VP8/JPEG encoder" : "MVE H264/VP8 decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:mcom03-vpu", sizeof(cap->bus_info));
	return 0;
}
static int enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	struct mve_video *d = video_drvdata(file);
	if (d->encode) {
		if (f->index > (V4L2_TYPE_IS_OUTPUT(f->type) ? 1 : 2)) return -EINVAL;
		f->pixelformat = V4L2_TYPE_IS_OUTPUT(f->type) ?
			(f->index ? V4L2_PIX_FMT_YUV420 : V4L2_PIX_FMT_NV12) :
			(f->index == 2 ? V4L2_PIX_FMT_VP8 : f->index ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264);
		f->flags = V4L2_TYPE_IS_OUTPUT(f->type) ? 0 : V4L2_FMT_FLAG_COMPRESSED;
	} else {
		if (f->index > (V4L2_TYPE_IS_OUTPUT(f->type) ? 1 : 0)) return -EINVAL;
		f->pixelformat = V4L2_TYPE_IS_OUTPUT(f->type) ?
			(f->index ? V4L2_PIX_FMT_VP8 : V4L2_PIX_FMT_H264) : V4L2_PIX_FMT_NV12;
		/* A changed coded size requires a new session/CAPTURE pool.  Do not
		 * advertise in-session resolution changes: V4L2 clients must drain
		 * and restart when their parser/depayloader announces new caps. */
		f->flags = V4L2_TYPE_IS_OUTPUT(f->type) ? V4L2_FMT_FLAG_COMPRESSED : 0;
	}
	return 0;
}
static int enum_framesizes(struct file *file, void *fh,
			  struct v4l2_frmsizeenum *f)
{
	struct mve_video *d = video_drvdata(file);

	if (!d->encode || f->index)
		return -EINVAL;
	switch (f->pixel_format) {
	case V4L2_PIX_FMT_H264: case V4L2_PIX_FMT_VP8: case V4L2_PIX_FMT_JPEG:
	case V4L2_PIX_FMT_NV12: case V4L2_PIX_FMT_YUV420:
		break;
	default:
		return -EINVAL;
	}
	/* CAPTURE TRY_FMT follows the current OUTPUT size. Advertise the
	 * supported range separately so clients can negotiate a different size. */
	f->type = V4L2_FRMSIZE_TYPE_STEPWISE;
	f->stepwise = (struct v4l2_frmsize_stepwise) {
		.min_width = 16, .max_width = MVE_W, .step_width = 2,
		.min_height = 16, .max_height = MVE_H, .step_height = 2,
	};
	return 0;
}
static void format_fill(struct mve_context *c, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
	bool out = V4L2_TYPE_IS_OUTPUT(f->type);
	memset(p, 0, sizeof(*p));
	p->width = c->width; p->height = c->height; p->field = V4L2_FIELD_NONE;
	p->pixelformat = c->dev->encode ? (out ? c->raw_format : c->codec) :
		(out ? c->codec : V4L2_PIX_FMT_NV12);
	p->num_planes = 1;
	p->colorspace = c->colorspace;
	p->ycbcr_enc = c->ycbcr_enc; p->quantization = c->quantization;
	p->xfer_func = c->xfer_func;
	p->plane_fmt[0].sizeimage = queue_size(c, out);
	if (out == c->dev->encode) {
		p->plane_fmt[0].bytesperline = c->pitch;
	}
}
static int get_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	format_fill(to_ctx(file->private_data), f); return 0;
}
static int try_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct mve_context *c = to_ctx(file->private_data);
	if (c->dev->encode) {
		struct v4l2_pix_format_mplane requested = f->fmt.pix_mp;
		bool out = V4L2_TYPE_IS_OUTPUT(f->type);
		format_fill(c, f);
		if (out) {
			f->fmt.pix_mp.width = ALIGN(clamp_t(unsigned, requested.width ?: c->width, 16, MVE_W), 2);
			f->fmt.pix_mp.height = ALIGN(clamp_t(unsigned, requested.height ?: c->height, 16, MVE_H), 2);
			f->fmt.pix_mp.pixelformat = requested.pixelformat == V4L2_PIX_FMT_YUV420 ? V4L2_PIX_FMT_YUV420 : V4L2_PIX_FMT_NV12;
			f->fmt.pix_mp.plane_fmt[0].bytesperline = ALIGN(f->fmt.pix_mp.width, 64);
			f->fmt.pix_mp.plane_fmt[0].sizeimage = f->fmt.pix_mp.plane_fmt[0].bytesperline * f->fmt.pix_mp.height * 3 / 2;
		} else f->fmt.pix_mp.pixelformat = requested.pixelformat == V4L2_PIX_FMT_VP8 ? V4L2_PIX_FMT_VP8 :
			requested.pixelformat == V4L2_PIX_FMT_JPEG ? V4L2_PIX_FMT_JPEG : V4L2_PIX_FMT_H264;
		return 0;
	}
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		struct v4l2_pix_format_mplane requested = f->fmt.pix_mp;
		struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
		unsigned w = f->fmt.pix_mp.width, h = f->fmt.pix_mp.height;
		format_fill(c, f);
		f->fmt.pix_mp.width = ALIGN(clamp_t(unsigned, w ? w : c->width, 16, MVE_W), 16);
		f->fmt.pix_mp.height = ALIGN(clamp_t(unsigned, h ? h : c->height, 16, MVE_H), 16);
		p->pixelformat = requested.pixelformat == V4L2_PIX_FMT_VP8 ? V4L2_PIX_FMT_VP8 : V4L2_PIX_FMT_H264;
		p->colorspace = requested.colorspace ?: V4L2_COLORSPACE_REC709;
		p->xfer_func = requested.xfer_func;
		p->ycbcr_enc = requested.ycbcr_enc;
		p->quantization = requested.quantization;
	} else {
		/* The bitstream determines YUV colorimetry. During probing, accept
		 * the requested tags rather than advertising only the current stream. */
		struct v4l2_pix_format_mplane requested = f->fmt.pix_mp;
		struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
		format_fill(c, f);
		if (requested.colorspace) {
			p->colorspace = requested.colorspace;
			p->xfer_func = requested.xfer_func;
			p->ycbcr_enc = requested.ycbcr_enc;
			p->quantization = requested.quantization;
		}
	}
	return 0;
}
static int set_fmt(struct file *file, void *fh, struct v4l2_format *f)
{
	struct mve_context *c = to_ctx(file->private_data);
	unsigned w = f->fmt.pix_mp.width, h = f->fmt.pix_mp.height;
	if (vb2_is_busy(v4l2_m2m_get_vq(c->fh.m2m_ctx, f->type))) return -EBUSY;
	if (V4L2_TYPE_IS_OUTPUT(f->type) &&
	    vb2_is_busy(v4l2_m2m_get_dst_vq(c->fh.m2m_ctx))) return -EBUSY;
	try_fmt(file, fh, f);
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		c->width = f->fmt.pix_mp.width; c->height = f->fmt.pix_mp.height;
		if (c->dev->encode) c->raw_format = f->fmt.pix_mp.pixelformat;
		else c->codec = f->fmt.pix_mp.pixelformat;
		c->visible_w = w ? min(w, c->width) : c->width;
		c->visible_h = h ? min(h, c->height) : c->height;
		c->pitch = ALIGN(c->width, 64);
		c->colorspace = f->fmt.pix_mp.colorspace;
		c->xfer_func = f->fmt.pix_mp.xfer_func;
		c->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		c->quantization = f->fmt.pix_mp.quantization;
	}
	if (c->dev->encode && !V4L2_TYPE_IS_OUTPUT(f->type)) c->codec = f->fmt.pix_mp.pixelformat;
	format_fill(c, f); return 0;
}
static int get_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mve_context *c = to_ctx(file->private_data);
	/* The V4L2 core normalizes _MPLANE selection types before this call. */
	if (s->type != (c->dev->encode ? V4L2_BUF_TYPE_VIDEO_OUTPUT : V4L2_BUF_TYPE_VIDEO_CAPTURE)) return -EINVAL;
	s->r.left = s->r.top = 0;
	switch (s->target) {
	case V4L2_SEL_TGT_COMPOSE: case V4L2_SEL_TGT_COMPOSE_DEFAULT:
	case V4L2_SEL_TGT_CROP: case V4L2_SEL_TGT_CROP_DEFAULT:
		s->r.width = c->visible_w; s->r.height = c->visible_h; break;
	case V4L2_SEL_TGT_COMPOSE_BOUNDS: case V4L2_SEL_TGT_COMPOSE_PADDED:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		s->r.width = c->width; s->r.height = c->height; break;
	default: return -EINVAL;
	}
	return 0;
}
static int decoder_cmd(struct file *file, void *fh, struct v4l2_decoder_cmd *cmd)
{
	struct mve_context *c = to_ctx(file->private_data);
	int ret = v4l2_m2m_ioctl_try_decoder_cmd(file, fh, cmd);
	if (ret) return ret;
	if (cmd->cmd == V4L2_DEC_CMD_STOP) c->draining = true;
	else if (c->eos_sent) {
		struct task_struct *thread;
		if (!c->drained) return -EBUSY;
		thread = c->thread; c->thread = NULL;
		if (thread) {
			mutex_unlock(&c->dev->lock); kthread_stop(thread); mutex_lock(&c->dev->lock);
		}
		session_destroy(c);
		vb2_clear_last_buffer_dequeued(v4l2_m2m_get_dst_vq(c->fh.m2m_ctx));
		ret = session_create(c);
		if (ret) { fail(c, ret); return ret; }
		c->thread = kthread_run(decode_thread, c, "mve-v4l2");
		if (IS_ERR(c->thread)) {
			ret = PTR_ERR(c->thread); c->thread = NULL;
			session_destroy(c); fail(c, ret); return ret;
		}
	}
	return 0;
}
static int encoder_cmd(struct file *file, void *fh, struct v4l2_encoder_cmd *cmd)
{
	struct mve_context *c = to_ctx(file->private_data);
	if (!c->dev->encode || cmd->flags) return -EINVAL;
	if (cmd->cmd == V4L2_ENC_CMD_STOP) { c->draining = true; return 0; }
	if (cmd->cmd == V4L2_ENC_CMD_START && !c->eos_sent) { c->draining = false; return 0; }
	return -EINVAL;
}
static int get_parm(struct file *file, void *fh, struct v4l2_streamparm *p)
{
	struct mve_context *c = to_ctx(file->private_data);
	if (!c->dev->encode || p->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) return -EINVAL;
	memset(&p->parm, 0, sizeof(p->parm));
	p->parm.output.capability = V4L2_CAP_TIMEPERFRAME;
	p->parm.output.timeperframe.numerator = c->fps_den;
	p->parm.output.timeperframe.denominator = c->fps_num;
	return 0;
}
static int set_parm(struct file *file, void *fh, struct v4l2_streamparm *p)
{
	struct mve_context *c = to_ctx(file->private_data);
	unsigned n = p->parm.output.timeperframe.denominator;
	unsigned d = p->parm.output.timeperframe.numerator;
	if (!c->dev->encode || p->type != V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE) return -EINVAL;
	if (!n || !d || (u64)n > (u64)d * 60 || n < d) return -EINVAL;
	c->fps_num = n; c->fps_den = d;
	if (c->active) { int ret = encoder_rate(c); if (ret) return ret; }
	return get_parm(file, fh, p);
}
static int subscribe(struct v4l2_fh *fh, const struct v4l2_event_subscription *s)
{
	if (s->type == V4L2_EVENT_SOURCE_CHANGE) return v4l2_src_change_event_subscribe(fh, s);
	if (s->type == V4L2_EVENT_EOS) return v4l2_event_subscribe(fh, s, 2, NULL);
	return v4l2_ctrl_subscribe_event(fh, s);
}
static const struct v4l2_ioctl_ops ioctl_ops = {
	.vidioc_querycap = querycap,
	.vidioc_enum_fmt_vid_cap = enum_fmt, .vidioc_enum_fmt_vid_out = enum_fmt,
	.vidioc_enum_framesizes = enum_framesizes,
	.vidioc_g_fmt_vid_cap_mplane = get_fmt, .vidioc_g_fmt_vid_out_mplane = get_fmt,
	.vidioc_try_fmt_vid_cap_mplane = try_fmt, .vidioc_try_fmt_vid_out_mplane = try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = set_fmt, .vidioc_s_fmt_vid_out_mplane = set_fmt,
	.vidioc_g_selection = get_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs, .vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf, .vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf, .vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon, .vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd, .vidioc_decoder_cmd = decoder_cmd,
	.vidioc_try_encoder_cmd = v4l2_m2m_ioctl_try_encoder_cmd, .vidioc_encoder_cmd = encoder_cmd,
	.vidioc_g_parm = get_parm, .vidioc_s_parm = set_parm,
	.vidioc_subscribe_event = subscribe, .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};
static int set_control(struct v4l2_ctrl *ctrl)
{
	struct mve_context *c = container_of(ctrl->handler, struct mve_context, controls);
	if (!c->dev->encode) return READ_ONCE(c->active) ? -EBUSY : 0;
	if (ctrl->id == V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME) {
		c->force_keyframe = true; return 0;
	}
	if (!c->active) return 0;
	if (ctrl->id == V4L2_CID_MPEG_VIDEO_BITRATE || ctrl->id == V4L2_CID_MPEG_VIDEO_BITRATE_MODE)
		return encoder_rate(c);
	return -EBUSY;
}
static const struct v4l2_ctrl_ops control_ops = { .s_ctrl = set_control };
static int video_open(struct file *file)
{
	struct mve_video *d = video_drvdata(file);
	struct mve_context *c = kzalloc(sizeof(*c), GFP_KERNEL);
	struct v4l2_ctrl *ctrl;
	int ret;
	if (!c) return -ENOMEM;
	c->dev = d; c->width = c->visible_w = 1280; c->height = c->visible_h = 720; c->pitch = 1280;
	c->colorspace = V4L2_COLORSPACE_REC709; c->xfer_func = V4L2_XFER_FUNC_709;
	c->ycbcr_enc = V4L2_YCBCR_ENC_709; c->quantization = V4L2_QUANTIZATION_LIM_RANGE;
	c->codec = V4L2_PIX_FMT_H264; c->raw_format = V4L2_PIX_FMT_NV12;
	c->fps_num = 15; c->fps_den = 1;
	c->storage_queue.dma_dir = DMA_BIDIRECTIONAL;
	c->storage_vb.vb2_queue = &c->storage_queue;
	mutex_lock(&d->lock);
	v4l2_fh_init(&c->fh, &d->video);
	file->private_data = &c->fh;
	v4l2_ctrl_handler_init(&c->controls, 14);
	if (d->encode) {
		c->bitrate = v4l2_ctrl_new_std(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_BITRATE, 100000, 50000000, 1000, 4000000);
		c->bitrate_mode = v4l2_ctrl_new_std_menu(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_BITRATE_MODE,
			V4L2_MPEG_VIDEO_BITRATE_MODE_CBR, 0, V4L2_MPEG_VIDEO_BITRATE_MODE_CBR);
		c->gop = v4l2_ctrl_new_std(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_GOP_SIZE, 1, 600, 1, 15);
		c->profile = v4l2_ctrl_new_std_menu(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_H264_PROFILE,
			V4L2_MPEG_VIDEO_H264_PROFILE_HIGH, BIT(V4L2_MPEG_VIDEO_H264_PROFILE_EXTENDED), V4L2_MPEG_VIDEO_H264_PROFILE_CONSTRAINED_BASELINE);
		c->level = v4l2_ctrl_new_std_menu(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_H264_LEVEL,
			V4L2_MPEG_VIDEO_H264_LEVEL_4_2, 0, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
		v4l2_ctrl_new_std_menu(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_VP8_PROFILE, 0, 0, 0);
		c->jpeg_quality = v4l2_ctrl_new_std(&c->controls, &control_ops, V4L2_CID_JPEG_COMPRESSION_QUALITY, 1, 100, 1, 75);
		c->repeat_header = v4l2_ctrl_new_std(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_REPEAT_SEQ_HEADER, 0, 1, 1, 1);
		v4l2_ctrl_new_std(&c->controls, &control_ops, V4L2_CID_MPEG_VIDEO_FORCE_KEY_FRAME, 0, 0, 0, 0);
		ctrl = v4l2_ctrl_new_std(&c->controls, NULL, V4L2_CID_MPEG_VIDEO_B_FRAMES, 0, 0, 1, 0);
		if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	} else {
	c->display_delay_enable = v4l2_ctrl_new_std(&c->controls, &control_ops,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&c->controls, NULL,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY, 0, 0, 1, 0);
	if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	c->min_capture = v4l2_ctrl_new_std(&c->controls, NULL,
		V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 4, MVE_OUTPUTS, 1, 4);
	if (c->min_capture) c->min_capture->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	}
	ctrl = v4l2_ctrl_new_std(&c->controls, NULL, V4L2_CID_MIN_BUFFERS_FOR_OUTPUT,
		MVE_INPUTS, MVE_INPUTS, 1, MVE_INPUTS);
	if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	c->fh.ctrl_handler = &c->controls;
	ret = c->controls.error;
	if (ret) goto error;
	c->fh.m2m_ctx = v4l2_m2m_ctx_init(d->m2m, c, queues_init);
	if (IS_ERR(c->fh.m2m_ctx)) { ret = PTR_ERR(c->fh.m2m_ctx); goto error; }
	v4l2_fh_add(&c->fh, file);
	mutex_unlock(&d->lock);
	return 0;
error:
	v4l2_ctrl_handler_free(&c->controls); v4l2_fh_exit(&c->fh);
	mutex_unlock(&d->lock); kfree(c); return ret;
}
static int video_release(struct file *file)
{
	struct mve_context *c = to_ctx(file->private_data);
	mutex_lock(&c->dev->lock);
	v4l2_m2m_ctx_release(c->fh.m2m_ctx);
	v4l2_ctrl_handler_free(&c->controls);
	v4l2_fh_del(&c->fh, file); v4l2_fh_exit(&c->fh);
	mutex_unlock(&c->dev->lock);
	kfree(c); return 0;
}
static const struct v4l2_file_operations video_fops = {
	.owner = THIS_MODULE, .open = video_open, .release = video_release,
	.unlocked_ioctl = video_ioctl2, .poll = v4l2_m2m_fop_poll,
	.mmap = v4l2_m2m_fop_mmap,
};
/* Transactions and buffering are managed asynchronously by the MVE scheduler. */
static int job_ready(void *priv) { return 0; }
static void device_run(void *priv) {}
static const struct v4l2_m2m_ops m2m_ops = { .job_ready = job_ready, .device_run = device_run };
static int register_video(struct device *dev, bool encode)
{
	struct mve_video *d;
	int ret;
	d = kzalloc(sizeof(*d), GFP_KERNEL); if (!d) return -ENOMEM;
	d->dev = &mve_rsrc_data.pdev->dev; d->encode = encode; mutex_init(&d->lock);
	media_device_init(&d->media);
	d->media.dev = dev;
	strscpy(d->media.model, encode ? "MVE H264/VP8/JPEG encoder" : "MVE H264/VP8 decoder", sizeof(d->media.model));
	strscpy(d->media.bus_info, "platform:mcom03-vpu", sizeof(d->media.bus_info));
	d->v4l2.mdev = &d->media;
	ret = v4l2_device_register(dev, &d->v4l2); if (ret) goto free;
	d->m2m = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(d->m2m)) { ret = PTR_ERR(d->m2m); goto unregister; }
	strscpy(d->video.name, encode ? "mve-video-encoder" : "mve-h264-decoder", sizeof(d->video.name));
	d->video.v4l2_dev = &d->v4l2; d->video.fops = &video_fops;
	d->video.ioctl_ops = &ioctl_ops; d->video.lock = &d->lock;
	d->video.release = video_device_release_empty;
	d->video.vfl_dir = VFL_DIR_M2M;
	d->video.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(&d->video, d);
	ret = video_register_device(&d->video, VFL_TYPE_VIDEO, -1);
	if (ret) goto m2m_release;
	ret = v4l2_m2m_register_media_controller(d->m2m, &d->video,
						encode ? MEDIA_ENT_F_PROC_VIDEO_ENCODER : MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) goto video_unregister;
	ret = media_device_register(&d->media);
	if (ret) goto controller_unregister;
	mve_videos[encode] = d;
	dev_info(dev, "Stateful V4L2 M2M %s registered as /dev/video%d\n",
		encode ? "H264/JPEG encoder" : "H264 decoder", d->video.num);
	return 0;
controller_unregister:
	v4l2_m2m_unregister_media_controller(d->m2m);
video_unregister:
	video_unregister_device(&d->video);
m2m_release:
	v4l2_m2m_release(d->m2m);
unregister:
	v4l2_device_unregister(&d->v4l2);
free:
	media_device_cleanup(&d->media);
	kfree(d); return ret;
}
static void unregister_video(bool encode)
{
	struct mve_video *d = mve_videos[encode];
	if (!d) return;
	media_device_unregister(&d->media);
	v4l2_m2m_unregister_media_controller(d->m2m);
	video_unregister_device(&d->video);
	v4l2_m2m_release(d->m2m); v4l2_device_unregister(&d->v4l2);
	media_device_cleanup(&d->media);
	kfree(d); mve_videos[encode] = NULL;
}
int mve_v4l2_register(struct device *dev)
{
	int ret = register_video(dev, false);
	if (ret) return ret;
	ret = register_video(dev, true);
	if (ret) unregister_video(false);
	return ret;
}
void mve_v4l2_unregister(void)
{
	unregister_video(true);
	unregister_video(false);
}
