/*****************************************************************************
 * bitstream.h: bitstream writing
 *****************************************************************************
 * Copyright (C) 2003-2022 x264 project
 *
 * Authors: Loren Merritt <lorenm@u.washington.edu>
 *          Fiona Glaser <fiona@x264.com>
 *          Laurent Aimar <fenrir@via.ecp.fr>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02111, USA.
 *
 * This program is also available under a commercial proprietary license.
 * For more information, contact us at licensing@x264.com.
 *****************************************************************************/

#ifndef X264_BS_H
#define X264_BS_H

#include <stdint.h>

#if defined(__GNUC__) && (__GNUC__ > 3 || __GNUC__ == 3 && __GNUC_MINOR__ > 0)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#define MAY_ALIAS __attribute__((may_alias))
#else
#define ALWAYS_INLINE __forceinline
#define MAY_ALIAS
#endif

/*
 * Recommendation ITU-T H.264 | International Standard ISO/IEC 14496-10
 *
 * 7.4.1 NAL unit semantics
 *    ...
 * emulation_prevention_three_byte is a byte equal to 0x03. When an
 * emulation_prevention_three_byte is present in the NAL unit, it shall
 * be discarded by the decoding process. The last byte of the NAL unit
 * shall not be equal to 0x00. Within the NAL unit, the following three-byte
 * sequences shall not occur at any byte-aligned position:
 * - 0x000000
 * - 0x000001
 * - 0x000002
 *
 * Within the NAL unit, any four-byte sequence that starts with 0x000003
 * other than the following sequences shall not occur at any byte-aligned position:
 * - 0x00000300
 * - 0x00000301
 * - 0x00000302
 * - 0x00000303
 *    ...
 */
#define EP3B 0x03

typedef struct bs_s
{
    uint8_t *p_start;
    uint8_t *p;
    uint8_t *p_end;

    uint8_t cur_bits;
    int     i_left;    /* i_count number of available bits */
    int     i_read_bit;
    int     b_ep3b;
} bs_t;

static inline void bs_init( bs_t *s, void *p_data, int i_data )
{
    s->p       = s->p_start = (uint8_t*)p_data;
    s->p_end   = (uint8_t*)p_data + i_data;
    s->i_left  = 8;
    s->i_read_bit = 7;
    s->cur_bits = 0;
    s->b_ep3b = 1;
}
static inline int bs_pos( bs_t *s )
{
    return( 8 * (s->p - s->p_start) + 8 - s->i_left );
}

/* Write the rest of cur_bits to the bitstream; results in a bitstream no longer 32-bit aligned. */
static inline void bs_flush( bs_t *s )
{
    if (s->i_left < 8) {
        if (s->b_ep3b && s->cur_bits <= 3 &&
            s->p_start + 2 <= s->p && s->p[-1] == 0 && s->p[-2] == 0) {
            *(s->p++) = EP3B;
        }
        *(s->p++) = s->cur_bits;
        s->i_left = 8;
    }
}

static inline void bs_write( bs_t *s, int i_count, uint32_t i_bits )
{
    while( i_count > 0 ) {
        if( i_count < s->i_left )
        {
            s->cur_bits = (s->cur_bits << i_count) | (uint8_t) i_bits;
            s->i_left -= i_count;
            return;
        }
        else
        {
            i_count -= s->i_left;
            s->cur_bits = (s->cur_bits << s->i_left) | (i_bits >> i_count);
            s->i_left = 0;
            bs_flush(s);
        }
    }
}

/* Special case to eliminate branch in normal bs_write. */
/* Golomb never writes an even-size code, so this is only used in slice headers. */
static inline void bs_write32( bs_t *s, uint32_t i_bits )
{
    bs_write( s, 16, i_bits >> 16 );
    bs_write( s, 16, i_bits );
}

static inline void bs_write1( bs_t *s, uint32_t i_bit )
{
    s->cur_bits <<= 1;
    s->cur_bits |= i_bit;
    --s->i_left;
    if( s->i_left == 0 ) {
        bs_flush(s);
    }
}

static inline uint8_t bs_read1( bs_t *s )
{
    uint8_t res = ((*s->p) >> s->i_read_bit) & 0x01;

    if (s->i_read_bit == 0) {
        ++s->p;
        if (s->b_ep3b && s->p[0] == EP3B &&
            s->p_start + 2 <= s->p && s->p[-1] == 0 && s->p[-2] == 0) {
            ++s->p;
        }
        s->i_read_bit = 7;
    } else {
        --s->i_read_bit;
    }

    return res;
}

static inline uint32_t bs_read( bs_t *s, int i_count )
{
    uint32_t res = 0;

    for (; i_count > 0; --i_count) {
        res = (res << 1) | bs_read1(s);
    }

    return res;
}

static inline uint32_t bs_read_ue( bs_t *s )
{
    int bits = 0;

    while (bs_read1(s) == 0) { ++bits; }

    return bs_read(s, bits) + (1 << bits) - 1;
}

static inline int32_t bs_read_se( bs_t *s )
{
    int bits = 0;
    int32_t ures;

    while (bs_read1(s) == 0) { ++bits; }

    ures = bs_read(s, bits);

    if (ures & 0x01) {
        return (1 - ures) / 2;
    } else {
        return ures >> 2;
    }
}

static inline void bs_align_0( bs_t *s )
{
    bs_write( s, s->i_left&7, 0 );
    bs_flush( s );
}
static inline void bs_align_1( bs_t *s )
{
    bs_write( s, s->i_left&7, (1 << (s->i_left&7)) - 1 );
    bs_flush( s );
}
static inline void bs_align_10( bs_t *s )
{
    if( s->i_left&7 )
        bs_write( s, s->i_left&7, 1 << ( (s->i_left&7) - 1 ) );
    bs_flush( s );
}

/* golomb functions */

static const uint8_t x264_ue_size_tab[256] =
{
     1, 1, 3, 3, 5, 5, 5, 5, 7, 7, 7, 7, 7, 7, 7, 7,
     9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9, 9,
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
    11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,11,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,13,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
    15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,15,
};

static inline void bs_write_ue_big( bs_t *s, unsigned int val )
{
    int size = 0;
    int tmp = ++val;
    if( tmp >= 0x10000 )
    {
        size = 32;
        tmp >>= 16;
    }
    if( tmp >= 0x100 )
    {
        size += 16;
        tmp >>= 8;
    }
    size += x264_ue_size_tab[tmp];
    bs_write( s, size>>1, 0 );
    bs_write( s, (size>>1)+1, val );
}

/* Only works on values under 255. */
static inline void bs_write_ue( bs_t *s, int val )
{
    bs_write( s, x264_ue_size_tab[val+1], val+1 );
}

static inline void bs_write_se( bs_t *s, int val )
{
    int size = 0;
    /* Faster than (val <= 0 ? -val*2+1 : val*2) */
    /* 4 instructions on x86, 3 on ARM */
    int tmp = 1 - val*2;
    if( tmp < 0 ) tmp = val*2;
    val = tmp;

    if( tmp >= 0x100 )
    {
        size = 16;
        tmp >>= 8;
    }
    size += x264_ue_size_tab[tmp];
    bs_write( s, size, val );
}

static inline void bs_write_te( bs_t *s, int x, int val )
{
    if( x == 1 )
        bs_write1( s, 1^val );
    else //if( x > 1 )
        bs_write_ue( s, val );
}

static inline void bs_rbsp_trailing( bs_t *s )
{
    bs_write1( s, 1 );
    bs_write( s, s->i_left&7, 0  );
}

static ALWAYS_INLINE int bs_size_ue( unsigned int val )
{
    return x264_ue_size_tab[val+1];
}

static ALWAYS_INLINE int bs_size_ue_big( unsigned int val )
{
    if( val < 255 )
        return x264_ue_size_tab[val+1];
    else
        return x264_ue_size_tab[(val+1)>>8] + 16;
}

static ALWAYS_INLINE int bs_size_se( int val )
{
    int tmp = 1 - val*2;
    if( tmp < 0 ) tmp = val*2;
    if( tmp < 256 )
        return x264_ue_size_tab[tmp];
    else
        return x264_ue_size_tab[tmp>>8]+16;
}

static ALWAYS_INLINE int bs_size_te( int x, int val )
{
    if( x == 1 )
        return 1;
    else //if( x > 1 )
        return x264_ue_size_tab[val+1];
}

int bs_h264_write_sps(bs_t *rw, H264RawSPS *sps);
int bs_h264_write_pps(bs_t *rw, H264RawPPS *pps, const H264RawSPS *sps);

int bs_h264_read_sps(bs_t *rw, H264RawSPS *sps);
int bs_h264_read_pps(bs_t *rw, H264RawPPS *pps, const H264RawSPS *sps);

#endif
