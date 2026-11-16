// SPDX-License-Identifier: GPL-2.0-only
/* Stateful H.264 V4L2 M2M frontend for the Arm MVE firmware driver.
 * MVE owns hardware scheduling; v4l2-m2m provides the standard VB2 queues.
 * Firmware buffers remain private. Capture exports standard contiguous CMA NV12 storage.
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

#define MVE_W 1920U
#define MVE_H 1088U
#define MVE_INPUT_SIZE (2U * 1024 * 1024)
#define MVE_INPUTS 5
/* H.264 can require 16 DPB pictures plus display/output slack. */
#define MVE_OUTPUTS 18
#define MVE_TOTAL (MVE_INPUTS + MVE_OUTPUTS)
#define MVE_EOS 1U
#define MVE_END_FRAME 0x10U

struct mve_video {
	struct media_device media;
	struct v4l2_device v4l2;
	struct video_device video;
	struct v4l2_m2m_dev *m2m;
	struct device *dev;
	struct mutex lock;
};
struct mve_storage {
	void *mem;
	struct dma_buf *dbuf;
	void *vaddr;
	struct vb2_v4l2_buffer *source;
	struct mve_base_buffer_userspace frame;
	bool busy, ready;
};
struct mve_context {
	struct v4l2_fh fh;
	struct v4l2_ctrl_handler controls;
	struct v4l2_ctrl *display_delay_enable;
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
	u64 submitted, completed;
	bool active, failed, sequence_known, draining, eos_sent, drained;
};
static struct mve_video *mve_video;

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
	return issue(c, slot < MVE_INPUTS ? MVE_BASE_EMPTY_THIS_BUFFER :
		     MVE_BASE_FILL_THIS_BUFFER, &p, sizeof(p));
}
static void buffers_return(struct mve_context *c, bool output, enum vb2_buffer_state state)
{
	struct vb2_v4l2_buffer *b;
	while ((b = output ? v4l2_m2m_src_buf_remove(c->fh.m2m_ctx) :
			    v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx)))
		v4l2_m2m_buf_done(b, state);
}
static void fail(struct mve_context *c, int error)
{
	unsigned i;
	dev_err(c->dev->dev, "V4L2 decode failed: %d\n", error);
	c->failed = true;
	for (i = 0; i < MVE_INPUTS; ++i) {
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
	p.size = i < MVE_INPUTS ? MVE_INPUT_SIZE : MVE_W * MVE_H * 2;
	p.format = i < MVE_INPUTS ? MVE_BASE_OMX_BUFFER_FORMAT_BITSTREAM :
		MVE_BASE_OMX_BUFFER_FORMAT_YUV420_SEMIPLANAR;
	if (i >= MVE_INPUTS) {
		p.width = MVE_W; p.height = MVE_H; p.stride = MVE_W;
	}
	s->mem = vb2_vmalloc_memops.alloc(&c->storage_vb, c->dev->dev, p.size);
	if (IS_ERR(s->mem)) { ret = PTR_ERR(s->mem); s->mem = NULL; return ret; }
	s->vaddr = vb2_vmalloc_memops.vaddr(&c->storage_vb, s->mem);
	s->dbuf = vb2_vmalloc_memops.get_dmabuf(&c->storage_vb, s->mem, O_RDWR);
	if (IS_ERR_OR_NULL(s->dbuf)) {
		s->dbuf = NULL; return -ENOMEM;
	}
	session = mve_session_acquire_by_file(&c->client);
	if (!session) return -EIO;
	ret = mve_session_buffer_register_kernel(session, i < MVE_INPUTS ? 0 : 1, &p, s->dbuf);
	mve_session_release(session);
	return ret ? -EIO : 0;
}
static void session_destroy(struct mve_context *c)
{
	unsigned i;
	if (c->active) {
		issue(c, MVE_BASE_DESTROY_SESSION, NULL, 0);
		c->active = false;
		dev_info(c->dev->dev, "V4L2 session closed: input=%llu output=%llu\n",
			 c->submitted, c->completed);
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
	c->submitted = c->completed = 0;
}
static int session_create(struct mve_context *c)
{
	char role[] = "video_decoder.avc";
	u32 opt[] = { 1010, 6, 0x80 }, running = MVE_BASE_HW_STATE_RUNNING;
	unsigned i;
	int ret;
	ret = issue(c, MVE_BASE_CREATE_SESSION, role, sizeof(role));
	if (ret) return ret;
	c->active = true;
	ret = issue(c, MVE_BASE_ACTIVATE_SESSION, NULL, 0);
	if (ret) goto error;
	ret = issue(c, MVE_BASE_SET_PARAMETER, opt, sizeof(opt));
	if (ret) goto error;
	/* Default: presentation order. Explicit display delay 0 is useful to
	 * clients such as VA-API that already manage picture presentation. */
	opt[1] = 8; opt[2] = !v4l2_ctrl_g_ctrl(c->display_delay_enable);
	ret = issue(c, MVE_BASE_SET_PARAMETER, opt, sizeof(opt));
	if (ret) goto error;
	for (i = 0; i < MVE_TOTAL; ++i) {
		ret = storage_alloc(c, i);
		if (ret) goto error;
	}
	ret = issue(c, MVE_BASE_ENQUEUE_STATE_CHANGE, &running, sizeof(running));
	if (!ret) return 0;
error:
	session_destroy(c);
	return ret;
}
static int frames_deliver(struct mve_context *c)
{
	struct vb2_queue *q = v4l2_m2m_get_dst_vq(c->fh.m2m_ctx);
	while (c->count && vb2_is_streaming(q)) {
		struct vb2_v4l2_buffer *b = v4l2_m2m_dst_buf_remove(c->fh.m2m_ctx);
		unsigned slot = c->fifo[c->head], y, rows;
		struct mve_storage *s = &c->storage[slot];
		struct mve_base_buffer_userspace *f = &s->frame;
		u8 *dst, *src = s->vaddr;
		int ret;
		if (!b) break;
		if (vb2_plane_size(&b->vb2_buf, 0) < c->pitch * c->height * 3 / 2) {
			/* A source-change event lets the client replace old capture storage. */
			v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR);
			continue;
		}
		rows = min_t(unsigned, f->decoded_height, c->height);
		if (f->filled_len) {
			if (f->decoded_width > c->pitch || f->decoded_width > MVE_W ||
			    f->decoded_height > MVE_H) {
				v4l2_m2m_buf_done(b, VB2_BUF_STATE_ERROR); return -EOVERFLOW;
			}
			dma_buf_begin_cpu_access(s->dbuf, DMA_FROM_DEVICE);
			dst = vb2_plane_vaddr(&b->vb2_buf, 0);
			for (y = 0; y < rows; ++y)
				memcpy(dst + y * c->pitch, src + y * MVE_W, f->decoded_width);
			dst = vb2_plane_vaddr(&b->vb2_buf, 0) + c->pitch * c->height;
			for (y = 0; y < DIV_ROUND_UP(rows, 2); ++y)
				memcpy(dst + y * c->pitch, src + (MVE_H + y) * MVE_W, f->decoded_width);
			dma_buf_end_cpu_access(s->dbuf, DMA_FROM_DEVICE);
		}
		/* Publish WC/coherent capture stores before userspace/GPU sees DONE. */
		dma_wmb();
		vb2_set_plane_payload(&b->vb2_buf, 0, f->filled_len ? c->pitch * c->height * 3 / 2 : 0);
		b->vb2_buf.timestamp = f->timestamp;
		b->field = V4L2_FIELD_NONE;
		b->sequence = c->sequence++;
		if (f->flags & 0x20) b->flags |= V4L2_BUF_FLAG_KEYFRAME;
		if (f->flags & MVE_EOS) {
			struct v4l2_event ev = { .type = V4L2_EVENT_EOS };
			b->flags |= V4L2_BUF_FLAG_LAST;
			c->drained = true;
			v4l2_event_queue_fh(&c->fh, &ev);
		}
		v4l2_m2m_buf_done(b, VB2_BUF_STATE_DONE);
		s->ready = false; c->head = (c->head + 1) % MVE_OUTPUTS; --c->count;
		if (!c->drained) {
			ret = enqueue(c, slot, 0, 0, 0);
			if (ret) return ret;
		}
	}
	return 0;
}
static int inputs_feed(struct mve_context *c)
{
	unsigned i;
	for (i = 0; i < MVE_INPUTS; ++i) {
		struct mve_storage *s = &c->storage[i];
		struct vb2_v4l2_buffer *b;
		unsigned size, offset, flags = MVE_END_FRAME;
		int ret;
		if (s->busy || c->eos_sent) continue;
		b = v4l2_m2m_src_buf_remove(c->fh.m2m_ctx);
		if (!b && !c->draining) break;
		offset = b ? b->vb2_buf.planes[0].data_offset : 0;
		size = b ? vb2_get_plane_payload(&b->vb2_buf, 0) - offset : 0;
		if (size > MVE_INPUT_SIZE) {
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
	unsigned i, slot;
	int ret;
	if (r->size < sizeof(*ev) || r->size != sizeof(*ev) + ev->size) return -EIO;
	if (ev->code == MVE_BASE_EVENT_ERROR || ev->code == MVE_BASE_EVENT_SESSION_HUNG) return -EIO;
	if (ev->code == MVE_BASE_EVENT_SEQUENCE_PARAMS) {
		const struct mve_base_response_sequence_parameters *p = (void *)ev->data;
		if (ev->size != sizeof(*p)) return -EIO;
		dev_info(c->dev->dev, "V4L2 firmware requests %u output buffers (pool %u)\n",
			 p->num_buffers_planar, MVE_OUTPUTS);
		if (p->num_buffers_planar > MVE_OUTPUTS) return -ENOSPC;
		return 0;
	}
	if (ev->code == MVE_BASE_EVENT_ALLOC_PARAMS) {
		u32 flush = MVE_BASE_FLUSH_OUTPUT_PORT;
		if (c->output_state) return -EOPNOTSUPP;
		c->output_state = 1;
		return issue(c, MVE_BASE_ENQUEUE_FLUSH_BUFFERS, &flush, sizeof(flush));
	}
	if (ev->code == MVE_BASE_EVENT_OUTPUT_FLUSHED && c->output_state == 1) {
		c->output_state = 2;
		for (i = MVE_INPUTS; i < MVE_TOTAL; ++i) {
			ret = enqueue(c, i, 0, 0, 0); if (ret) return ret;
		}
		return 0;
	}
	if (ev->code != MVE_BASE_EVENT_INPUT && ev->code != MVE_BASE_EVENT_OUTPUT) return 0;
	if (ev->size != sizeof(*f)) return -EIO;
	f = (void *)ev->data;
	if (!f->buffer_id || f->buffer_id > MVE_TOTAL) return -EIO;
	slot = f->buffer_id - 1;
	if (ev->code == MVE_BASE_EVENT_INPUT) {
		struct mve_storage *s = &c->storage[slot];
		if (slot >= MVE_INPUTS) return -EIO;
		if (s->source) v4l2_m2m_buf_done(s->source, VB2_BUF_STATE_DONE);
		s->source = NULL; s->busy = false;
		return 0;
	}
	if (slot < MVE_INPUTS) return -EIO;
	if (!f->filled_len && !(f->flags & MVE_EOS))
		return enqueue(c, slot, 0, 0, 0);
	if (!c->sequence_known && f->filled_len) {
		struct v4l2_event change = { .type = V4L2_EVENT_SOURCE_CHANGE,
			.u.src_change.changes = V4L2_EVENT_SRC_CH_RESOLUTION };
		if (!f->decoded_width || !f->decoded_height || f->decoded_width > MVE_W || f->decoded_height > MVE_H)
			return -EINVAL;
		c->width = ALIGN(f->decoded_width, 16);
		c->height = ALIGN(f->decoded_height, 16);
		c->visible_w = min(c->visible_w, f->decoded_width);
		c->visible_h = min(c->visible_h, f->decoded_height);
		c->pitch = ALIGN(c->width, 64);
		c->sequence_known = true;
		v4l2_event_queue_fh(&c->fh, &change);
		dev_info(c->dev->dev, "V4L2 H264 sequence %ux%u, visible %ux%u\n",
			c->width, c->height, c->visible_w, c->visible_h);
	}
	if (c->count == MVE_OUTPUTS || c->storage[slot].ready) return -EIO;
	c->storage[slot].frame = *f; c->storage[slot].ready = true;
	c->fifo[(c->head + c->count++) % MVE_OUTPUTS] = slot;
	if (f->filled_len) ++c->completed;
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
	unsigned need[] = { V4L2_TYPE_IS_OUTPUT(q->type) ? MVE_INPUT_SIZE : c->pitch * c->height * 3 / 2 }, i;
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
	unsigned need[] = { V4L2_TYPE_IS_OUTPUT(b->type) ? MVE_INPUT_SIZE : c->pitch * c->height * 3 / 2 };
	for (i = 0; i < planes; ++i) {
		if (vb2_plane_size(b, i) < need[i]) return -EINVAL;
		if (!vb2_plane_vaddr(b, i)) return -ENOMEM;
	}
	if (V4L2_TYPE_IS_OUTPUT(b->type) && (vb2_get_plane_payload(b, 0) > MVE_INPUT_SIZE ||
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
	if (!V4L2_TYPE_IS_OUTPUT(q->type)) return 0;
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
static void stop_streaming(struct vb2_queue *q)
{
	struct mve_context *c = vb2_get_drv_priv(q);
	if (V4L2_TYPE_IS_OUTPUT(q->type)) {
		if (c->thread) {
			/* The worker uses the same queue mutex. */
			struct task_struct *thread = c->thread;
			c->thread = NULL;
			mutex_unlock(&c->dev->lock);
			kthread_stop(thread);
			mutex_lock(&c->dev->lock);
		}
		session_destroy(c);
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
		q[i]->drv_priv = c; q[i]->ops = &queue_ops;
		q[i]->buf_struct_size = sizeof(struct v4l2_m2m_buffer);
		q[i]->mem_ops = i ? &vb2_dma_contig_memops : &vb2_vmalloc_memops;
		q[i]->timestamp_flags = V4L2_BUF_FLAG_TIMESTAMP_COPY;
		q[i]->lock = &c->dev->lock; q[i]->dev = c->dev->dev;
		ret = vb2_queue_init(q[i]); if (ret) return ret;
	}
	return 0;
}
static int querycap(struct file *file, void *fh, struct v4l2_capability *cap)
{
	strscpy(cap->driver, "mve-v4l2", sizeof(cap->driver));
	strscpy(cap->card, "MVE H264 decoder", sizeof(cap->card));
	strscpy(cap->bus_info, "platform:mcom03-vpu", sizeof(cap->bus_info));
	return 0;
}
static int enum_fmt(struct file *file, void *fh, struct v4l2_fmtdesc *f)
{
	if (f->index) return -EINVAL;
	f->pixelformat = V4L2_TYPE_IS_OUTPUT(f->type) ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_NV12;
	f->flags = V4L2_TYPE_IS_OUTPUT(f->type) ? V4L2_FMT_FLAG_COMPRESSED : 0;
	return 0;
}
static void format_fill(struct mve_context *c, struct v4l2_format *f)
{
	struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
	bool out = V4L2_TYPE_IS_OUTPUT(f->type);
	memset(p, 0, sizeof(*p));
	p->width = c->width; p->height = c->height; p->field = V4L2_FIELD_NONE;
	p->pixelformat = out ? V4L2_PIX_FMT_H264 : V4L2_PIX_FMT_NV12;
	p->num_planes = 1;
	p->colorspace = c->colorspace;
	p->ycbcr_enc = c->ycbcr_enc; p->quantization = c->quantization;
	p->xfer_func = c->xfer_func;
	p->plane_fmt[0].sizeimage = out ? MVE_INPUT_SIZE : c->pitch * c->height * 3 / 2;
	if (!out) {
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
	if (V4L2_TYPE_IS_OUTPUT(f->type)) {
		struct v4l2_pix_format_mplane requested = f->fmt.pix_mp;
		struct v4l2_pix_format_mplane *p = &f->fmt.pix_mp;
		unsigned w = f->fmt.pix_mp.width, h = f->fmt.pix_mp.height;
		format_fill(c, f);
		f->fmt.pix_mp.width = ALIGN(clamp_t(unsigned, w ? w : c->width, 16, MVE_W), 16);
		f->fmt.pix_mp.height = ALIGN(clamp_t(unsigned, h ? h : c->height, 16, MVE_H), 16);
		p->colorspace = requested.colorspace ?: V4L2_COLORSPACE_REC709;
		p->xfer_func = requested.xfer_func ?: V4L2_MAP_XFER_FUNC_DEFAULT(p->colorspace);
		p->ycbcr_enc = requested.ycbcr_enc ?: V4L2_MAP_YCBCR_ENC_DEFAULT(p->colorspace);
		p->quantization = requested.quantization ?:
			V4L2_MAP_QUANTIZATION_DEFAULT(false, p->colorspace, p->ycbcr_enc);
	} else format_fill(c, f);
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
		c->visible_w = w ? min(w, c->width) : c->width;
		c->visible_h = h ? min(h, c->height) : c->height;
		c->pitch = ALIGN(c->width, 64);
		c->colorspace = f->fmt.pix_mp.colorspace;
		c->xfer_func = f->fmt.pix_mp.xfer_func;
		c->ycbcr_enc = f->fmt.pix_mp.ycbcr_enc;
		c->quantization = f->fmt.pix_mp.quantization;
	}
	format_fill(c, f); return 0;
}
static int get_selection(struct file *file, void *fh, struct v4l2_selection *s)
{
	struct mve_context *c = to_ctx(file->private_data);
	/* The V4L2 core normalizes _MPLANE selection types before this call. */
	if (s->type != V4L2_BUF_TYPE_VIDEO_CAPTURE) return -EINVAL;
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
static int subscribe(struct v4l2_fh *fh, const struct v4l2_event_subscription *s)
{
	if (s->type == V4L2_EVENT_SOURCE_CHANGE) return v4l2_src_change_event_subscribe(fh, s);
	if (s->type == V4L2_EVENT_EOS) return v4l2_event_subscribe(fh, s, 2, NULL);
	return v4l2_ctrl_subscribe_event(fh, s);
}
static const struct v4l2_ioctl_ops ioctl_ops = {
	.vidioc_querycap = querycap,
	.vidioc_enum_fmt_vid_cap = enum_fmt, .vidioc_enum_fmt_vid_out = enum_fmt,
	.vidioc_g_fmt_vid_cap_mplane = get_fmt, .vidioc_g_fmt_vid_out_mplane = get_fmt,
	.vidioc_try_fmt_vid_cap_mplane = try_fmt, .vidioc_try_fmt_vid_out_mplane = try_fmt,
	.vidioc_s_fmt_vid_cap_mplane = set_fmt, .vidioc_s_fmt_vid_out_mplane = set_fmt,
	.vidioc_g_selection = get_selection,
	.vidioc_reqbufs = v4l2_m2m_ioctl_reqbufs, .vidioc_create_bufs = v4l2_m2m_ioctl_create_bufs,
	.vidioc_querybuf = v4l2_m2m_ioctl_querybuf, .vidioc_qbuf = v4l2_m2m_ioctl_qbuf,
	.vidioc_dqbuf = v4l2_m2m_ioctl_dqbuf, .vidioc_expbuf = v4l2_m2m_ioctl_expbuf,
	.vidioc_streamon = v4l2_m2m_ioctl_streamon, .vidioc_streamoff = v4l2_m2m_ioctl_streamoff,
	.vidioc_try_decoder_cmd = v4l2_m2m_ioctl_try_decoder_cmd, .vidioc_decoder_cmd = decoder_cmd,
	.vidioc_subscribe_event = subscribe, .vidioc_unsubscribe_event = v4l2_event_unsubscribe,
};
static int set_control(struct v4l2_ctrl *ctrl)
{
	struct mve_context *c = container_of(ctrl->handler, struct mve_context, controls);
	return READ_ONCE(c->active) ? -EBUSY : 0;
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
	c->storage_queue.dma_dir = DMA_BIDIRECTIONAL;
	c->storage_vb.vb2_queue = &c->storage_queue;
	mutex_lock(&d->lock);
	v4l2_fh_init(&c->fh, &d->video);
	file->private_data = &c->fh;
	v4l2_ctrl_handler_init(&c->controls, 4);
	c->display_delay_enable = v4l2_ctrl_new_std(&c->controls, &control_ops,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY_ENABLE, 0, 1, 1, 0);
	ctrl = v4l2_ctrl_new_std(&c->controls, NULL,
		V4L2_CID_MPEG_VIDEO_DEC_DISPLAY_DELAY, 0, 0, 1, 0);
	if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ctrl = v4l2_ctrl_new_std(&c->controls, NULL, V4L2_CID_MIN_BUFFERS_FOR_CAPTURE, 4, 4, 1, 4);
	if (ctrl) ctrl->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	ctrl = v4l2_ctrl_new_std(&c->controls, NULL, V4L2_CID_MIN_BUFFERS_FOR_OUTPUT, 1, 1, 1, 1);
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
int mve_v4l2_register(struct device *dev)
{
	struct mve_video *d;
	int ret;
	d = kzalloc(sizeof(*d), GFP_KERNEL); if (!d) return -ENOMEM;
	d->dev = &mve_rsrc_data.pdev->dev; mutex_init(&d->lock);
	media_device_init(&d->media);
	d->media.dev = dev;
	strscpy(d->media.model, "MVE H264 decoder", sizeof(d->media.model));
	strscpy(d->media.bus_info, "platform:mcom03-vpu", sizeof(d->media.bus_info));
	d->v4l2.mdev = &d->media;
	ret = v4l2_device_register(dev, &d->v4l2); if (ret) goto free;
	d->m2m = v4l2_m2m_init(&m2m_ops);
	if (IS_ERR(d->m2m)) { ret = PTR_ERR(d->m2m); goto unregister; }
	strscpy(d->video.name, "mve-h264-decoder", sizeof(d->video.name));
	d->video.v4l2_dev = &d->v4l2; d->video.fops = &video_fops;
	d->video.ioctl_ops = &ioctl_ops; d->video.lock = &d->lock;
	d->video.release = video_device_release_empty;
	d->video.vfl_dir = VFL_DIR_M2M;
	d->video.device_caps = V4L2_CAP_VIDEO_M2M_MPLANE | V4L2_CAP_STREAMING;
	video_set_drvdata(&d->video, d);
	ret = video_register_device(&d->video, VFL_TYPE_VIDEO, -1);
	if (ret) goto m2m_release;
	ret = v4l2_m2m_register_media_controller(d->m2m, &d->video,
						MEDIA_ENT_F_PROC_VIDEO_DECODER);
	if (ret) goto video_unregister;
	ret = media_device_register(&d->media);
	if (ret) goto controller_unregister;
	mve_video = d;
	dev_info(dev, "Stateful H264 V4L2 M2M decoder registered as /dev/video%d\n", d->video.num);
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
void mve_v4l2_unregister(void)
{
	struct mve_video *d = mve_video;
	if (!d) return;
	media_device_unregister(&d->media);
	v4l2_m2m_unregister_media_controller(d->m2m);
	video_unregister_device(&d->video);
	v4l2_m2m_release(d->m2m); v4l2_device_unregister(&d->v4l2);
	media_device_cleanup(&d->media);
	kfree(d); mve_video = NULL;
}
