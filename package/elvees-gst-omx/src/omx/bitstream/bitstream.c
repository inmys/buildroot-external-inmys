#include <gst/gst.h>
#include "cbs_h264.h"
#include "bitstream.h"

#define av_log2(x) (31 - __builtin_clz((x)|1))
#define av_ceil_log2(x) av_log2(((x) - 1U) << 1);

#define MARK_USED(expr) do { (void)(expr); } while (0)

// The largest unsigned value representable in N bits, suitable for use as
// range_max in the above functions.
#define MAX_UINT_BITS(length) ((UINT64_C(1) << (length)) - 1)

// The largest signed value representable in N bits, suitable for use as
// range_max in the above functions.
#define MAX_INT_BITS(length) ((INT64_C(1) << ((length) - 1)) - 1)

// The smallest signed value representable in N bits, suitable for use as
// range_min in the above functions.
#define MIN_INT_BITS(length) (-(INT64_C(1) << ((length) - 1)))

#define HEADER(name) do { \
    } while (0)

#define CHECK(call) do { \
        err = (call); \
        if (err < 0) \
            return err; \
    } while (0)

#define CHECK_EOF(name, width) \
    MARK_USED(err); \
    if (rw->p >= rw->p_end) { \
        av_log(ctx->log_ctx, AV_LOG_ERROR, "EOF detected for %s", #name); \
        return AVERROR_EOF; \
    }

#define CHECK_RANGE(name, val, range_min, range_max) \
    if ((val) < (range_min) || (val) > (range_max)) { \
        av_log(ctx->log_ctx, AV_LOG_ERROR, "%s is out of range: %d [%d, %d]", \
            #name, (int) val, (int) range_min, (int) range_max); \
        return AVERROR_INVALIDDATA; \
    }

#define av_log(ctx, level, ...) \
    GST_ERROR (__VA_ARGS__)

#define AVERROR_INVALIDDATA -1
#define AVERROR_PATCHWELCOME -2
#define AVERROR_EOF -3

typedef struct {
    void *log_ctx;
} CodedBitstreamContext;

#define FUNC_NAME2(rw, codec, name) cbs_ ## codec ## _ ## rw ## _ ## name
#define FUNC_NAME1(rw, codec, name) FUNC_NAME2(rw, codec, name)
#define FUNC_H264(name) FUNC_NAME1(READWRITE, h264, name)

#define SUBSCRIPTS(subs, ...) (subs > 0 ? ((int[subs + 1]){ subs, __VA_ARGS__ }) : NULL)

#define u(width, name, range_min, range_max) \
        xu(width, name, current->name, range_min, range_max, 0, )
#define ub(width, name) \
        xu(width, name, current->name, 0, MAX_UINT_BITS(width), 0, )
#define flag(name) ub(1, name)
#define ue(name, range_min, range_max) \
        xue(name, current->name, range_min, range_max, 0, )
#define i(width, name, range_min, range_max) \
        xi(width, name, current->name, range_min, range_max, 0, )
#define ib(width, name) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), 0, )
#define se(name, range_min, range_max) \
        xse(name, current->name, range_min, range_max, 0, )

#define us(width, name, range_min, range_max, subs, ...) \
        xu(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ubs(width, name, subs, ...) \
        xu(width, name, current->name, 0, MAX_UINT_BITS(width), subs, __VA_ARGS__)
#define flags(name, subs, ...) \
        xu(1, name, current->name, 0, 1, subs, __VA_ARGS__)
#define ues(name, range_min, range_max, subs, ...) \
        xue(name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define is(width, name, range_min, range_max, subs, ...) \
        xi(width, name, current->name, range_min, range_max, subs, __VA_ARGS__)
#define ibs(width, name, subs, ...) \
        xi(width, name, current->name, MIN_INT_BITS(width), MAX_INT_BITS(width), subs, __VA_ARGS__)
#define ses(name, range_min, range_max, subs, ...) \
        xse(name, current->name, range_min, range_max, subs, __VA_ARGS__)

#define fixed(width, name, value) do { \
        uint32_t fixed_value = value; MARK_USED(fixed_value); \
        xu(width, name, fixed_value, value, value, 0, ); \
    } while (0)

#define READ
#define READWRITE read
#define RWContext bs_t

#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, width); \
        var = bs_read(rw, width); \
        CHECK_RANGE(name, var, range_min, range_max); \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, 1); \
        var = bs_read_ue(rw); \
        CHECK_RANGE(name, var, range_min, range_max); \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, width); \
        var = bs_read(rw, width); \
        CHECK_RANGE(name, var, range_min, range_max); \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, 1); \
        var = bs_read_ue(rw); \
        CHECK_RANGE(name, var, range_min, range_max); \
    } while (0)

#define infer(name, value) do { \
        current->name = value; \
    } while (0)

static inline uint8_t bs_more_rbsp_data(bs_t *bs) {
    if (bs->p_end - bs->p > 1) {
        return 1;
    }
    if (bs->p_end == bs->p) {
        return 0;
    }
    return ((*bs->p) & ((1 << bs->i_read_bit) - 1));
}

#define more_rbsp_data(var) ((var = bs_more_rbsp_data(rw)))

#define bit_position(rw)   (put_bits_count(rw))
#define byte_alignment(rw) (7 - rw->i_read_bit)

#define allocate(name, size) do { \
    } while (0)

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.h"
#undef FUNC

#undef READ
#undef READWRITE
#undef RWContext
#undef xu
#undef xi
#undef xue
#undef xse
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate

#define WRITE
#define READWRITE write
#define RWContext bs_t

#define xu(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, width); \
        CHECK_RANGE(name, var, range_min, range_max); \
        bs_write(rw, width, (uint32_t) var); \
    } while (0)
#define xue(name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, 1); \
        CHECK_RANGE(name, var, range_min, range_max); \
        bs_write_ue_big(rw, (uint32_t) var); \
    } while (0)
#define xi(width, name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, width); \
        CHECK_RANGE(name, var, range_min, range_max); \
        bs_write(rw, width, (uint32_t) var); \
    } while (0)
#define xse(name, var, range_min, range_max, subs, ...) do { \
        CHECK_EOF(name, 1); \
        CHECK_RANGE(name, var, range_min, range_max); \
        bs_write_se(rw, (int32_t) var); \
    } while (0)

#define infer(name, value) do { \
        if (current->name != (value)) { \
            av_log(ctx->log_ctx, AV_LOG_ERROR, \
                   "%s does not match inferred value: " \
                   "%d, but should be %d.\n", \
                   #name, (int)current->name, (int)(value)); \
            return AVERROR_INVALIDDATA; \
        } \
    } while (0)

#define more_rbsp_data(var) (var)

#define bit_position(rw)   (put_bits_count(rw))
#define byte_alignment(rw) (rw->i_left % 8)

#define allocate(name, size) do { \
    } while (0)

#define FUNC(name) FUNC_H264(name)
#include "cbs_h264_syntax_template.h"
#undef FUNC

#undef WRITE
#undef READWRITE
#undef RWContext
#undef xu
#undef xi
#undef xue
#undef xse
#undef u
#undef i
#undef flag
#undef ue
#undef se
#undef infer
#undef more_rbsp_data
#undef bit_position
#undef byte_alignment
#undef allocate

int bs_h264_write_sps(bs_t *rw, H264RawSPS *sps)
{
    return cbs_h264_write_sps(NULL, rw, sps);
}

int bs_h264_write_pps(bs_t *rw, H264RawPPS *pps, const H264RawSPS *sps)
{
    return cbs_h264_write_pps(NULL, rw, pps, sps);
}

int bs_h264_read_sps(bs_t *rw, H264RawSPS *sps)
{
    return cbs_h264_read_sps(NULL, rw, sps);
}

int bs_h264_read_pps(bs_t *rw, H264RawPPS *pps, const H264RawSPS *sps)
{
    return cbs_h264_read_pps(NULL, rw, pps, sps);
}
