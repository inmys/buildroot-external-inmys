/*
 * Copyright (C) 2011, Hewlett-Packard Development Company, L.P.
 *   Author: Sebastian Dröge <sebastian.droege@collabora.co.uk>, Collabora Ltd.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation
 * version 2.1 of the License.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstomxh264utils.h"

#include "bitstream/cbs_h264.h"
#include "bitstream/bitstream.h"

typedef struct
{
  const gchar *profile;
  OMX_VIDEO_AVCPROFILETYPE e;
} H264ProfileMapping;

static const H264ProfileMapping h264_profiles[] = {
  {"baseline", OMX_VIDEO_AVCProfileBaseline},
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  {"constrained-baseline",
      (OMX_VIDEO_AVCPROFILETYPE) OMX_ALG_VIDEO_AVCProfileConstrainedBaseline},
#else
  {"constrained-baseline", OMX_VIDEO_AVCProfileBaseline},
#endif
  {"main", OMX_VIDEO_AVCProfileMain},
  {"high", OMX_VIDEO_AVCProfileHigh},
  {"high-10", OMX_VIDEO_AVCProfileHigh10},
  {"high-4:2:2", OMX_VIDEO_AVCProfileHigh422},
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  {"progressive-high",
      (OMX_VIDEO_AVCPROFILETYPE) OMX_ALG_VIDEO_AVCProfileProgressiveHigh},
  {"constrained-high",
      (OMX_VIDEO_AVCPROFILETYPE) OMX_ALG_VIDEO_AVCProfileConstrainedHigh},
  {"high-10-intra",
      (OMX_VIDEO_AVCPROFILETYPE) OMX_ALG_VIDEO_AVCProfileHigh10_Intra},
  {"high-4:2:2-intra",
      (OMX_VIDEO_AVCPROFILETYPE) OMX_ALG_VIDEO_AVCProfileHigh422_Intra},
#endif
};

OMX_VIDEO_AVCPROFILETYPE
gst_omx_h264_utils_get_profile_from_str (const gchar * profile)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (h264_profiles); i++) {
    if (g_str_equal (profile, h264_profiles[i].profile))
      return h264_profiles[i].e;
  }

  return OMX_VIDEO_AVCProfileMax;
}

const gchar *
gst_omx_h264_utils_get_profile_from_enum (OMX_VIDEO_AVCPROFILETYPE e)
{
  guint i;

  for (i = 0; i < G_N_ELEMENTS (h264_profiles); i++) {
    if (e == h264_profiles[i].e)
      return h264_profiles[i].profile;
  }

  return NULL;
}

const gchar *
gst_omx_h264_utils_get_level_from_enum (OMX_VIDEO_AVCLEVELTYPE e)
{
  switch (e) {
        case OMX_VIDEO_AVCLevel1:
          return  "1";
        case OMX_VIDEO_AVCLevel1b:
          return  "1b";
        case OMX_VIDEO_AVCLevel11:
          return  "1.1";
        case OMX_VIDEO_AVCLevel12:
          return  "1.2";
        case OMX_VIDEO_AVCLevel13:
          return  "1.3";
        case OMX_VIDEO_AVCLevel2:
          return  "2";
        case OMX_VIDEO_AVCLevel21:
          return  "2.1";
        case OMX_VIDEO_AVCLevel22:
          return  "2.2";
        case OMX_VIDEO_AVCLevel3:
          return  "3";
        case OMX_VIDEO_AVCLevel31:
          return  "3.1";
        case OMX_VIDEO_AVCLevel32:
          return  "3.2";
        case OMX_VIDEO_AVCLevel4:
          return  "4";
        case OMX_VIDEO_AVCLevel41:
          return  "4.1";
        case OMX_VIDEO_AVCLevel42:
          return  "4.2";
        case OMX_VIDEO_AVCLevel5:
          return  "5";
        case OMX_VIDEO_AVCLevel51:
          return  "5.1";
  #ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
        case OMX_ALG_VIDEO_AVCLevel52:
          return  "5.2";
        case OMX_ALG_VIDEO_AVCLevel60:
          return  "6.0";
        case OMX_ALG_VIDEO_AVCLevel61:
          return  "6.1";
        case OMX_ALG_VIDEO_AVCLevel62:
          return  "6.2";
  #endif
        default:
          g_assert_not_reached ();
          return NULL;
      }
}

OMX_VIDEO_AVCLEVELTYPE
gst_omx_h264_utils_get_level_from_str (const gchar * level)
{
  if (g_str_equal (level, "1")) {
    return OMX_VIDEO_AVCLevel1;
  } else if (g_str_equal (level, "1b")) {
    return OMX_VIDEO_AVCLevel1b;
  } else if (g_str_equal (level, "1.1")) {
    return OMX_VIDEO_AVCLevel11;
  } else if (g_str_equal (level, "1.2")) {
    return OMX_VIDEO_AVCLevel12;
  } else if (g_str_equal (level, "1.3")) {
    return OMX_VIDEO_AVCLevel13;
  } else if (g_str_equal (level, "2")) {
    return OMX_VIDEO_AVCLevel2;
  } else if (g_str_equal (level, "2.1")) {
    return OMX_VIDEO_AVCLevel21;
  } else if (g_str_equal (level, "2.2")) {
    return OMX_VIDEO_AVCLevel22;
  } else if (g_str_equal (level, "3")) {
    return OMX_VIDEO_AVCLevel3;
  } else if (g_str_equal (level, "3.1")) {
    return OMX_VIDEO_AVCLevel31;
  } else if (g_str_equal (level, "3.2")) {
    return OMX_VIDEO_AVCLevel32;
  } else if (g_str_equal (level, "4")) {
    return OMX_VIDEO_AVCLevel4;
  } else if (g_str_equal (level, "4.1")) {
    return OMX_VIDEO_AVCLevel41;
  } else if (g_str_equal (level, "4.2")) {
    return OMX_VIDEO_AVCLevel42;
  } else if (g_str_equal (level, "5")) {
    return OMX_VIDEO_AVCLevel5;
  } else if (g_str_equal (level, "5.1")) {
    return OMX_VIDEO_AVCLevel51;
#ifdef USE_OMX_TARGET_ZYNQ_USCALE_PLUS
  } else if (g_str_equal (level, "5.2")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel52;
  } else if (g_str_equal (level, "6.0")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel60;
  } else if (g_str_equal (level, "6.1")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel61;
  } else if (g_str_equal (level, "6.2")) {
    return (OMX_VIDEO_AVCLEVELTYPE) OMX_ALG_VIDEO_AVCLevel62;
#endif
  }

  return OMX_VIDEO_AVCLevelMax;
}

/* MVE uses 32-bit prefix format. */
static uint8_t nal_prefix[4] = { 0, 0, 0, 1 };

static uint8_t *
gst_omx_h264_find_nal(uint8_t *begin, uint8_t *end, uint32_t min_size)
{
    uint8_t *nal;

    nal = begin + sizeof(nal_prefix) - 1;

    do {
        nal = memchr(nal, 1, end - nal - min_size);
        if (nal == NULL) {
            return NULL;
        }

        if (memcmp(nal - sizeof(nal_prefix) + 1, nal_prefix, sizeof(nal_prefix)) == 0) {
            break;
        }

        if (nal + sizeof(nal_prefix) + min_size > end) {
            return NULL;
        }

        nal += sizeof(nal_prefix);
    } while (1);

    return nal + 1;
}

/* It's expected that buffer pointed by data contains both (and only)
 * one SPS and one PPS. SPS and PPS must be in Byte stream format according
 * to Annex B of Rec. ITU-T H.264.
 */
void
gst_omx_h264_utils_update_sps_id(guint sps_id, void * data, uint32_t * size, uint32_t max_size)
{
    enum {
        NALU_TYPE_MASK = 0x1F,

        SPS_MIN_SIZE = 4,
        PPS_MIN_SIZE = 2,
    };

    uint8_t *begin = data;
    uint8_t *end = begin + *size;
    uint8_t *sps_nal;
    uint8_t *pps_nal;

    bs_t bs;
    H264RawSPS sps;
    H264RawPPS pps;

    if (*size < 2 * sizeof(nal_prefix) + SPS_MIN_SIZE + PPS_MIN_SIZE) {
        GST_ERROR ("buffer too small");
        return;
    }

    sps_nal = gst_omx_h264_find_nal(begin, end, SPS_MIN_SIZE);
    if (sps_nal == NULL) {
        GST_ERROR ("SPS NAL prefix not found");
        return;
    }

    if ((sps_nal[0] & NALU_TYPE_MASK) != H264_NAL_SPS) {
        GST_ERROR ("SPS NALU type not match %d", sps_nal[0] & NALU_TYPE_MASK);
        return;
    }

    pps_nal = gst_omx_h264_find_nal(sps_nal + SPS_MIN_SIZE, end, PPS_MIN_SIZE);
    if (pps_nal == NULL) {
        GST_ERROR ("PPS NAL prefix not found");
        return;
    }

    if ((pps_nal[0] & NALU_TYPE_MASK) != H264_NAL_PPS) {
        GST_ERROR ("PPS NALU type not match %d", pps_nal[0] & NALU_TYPE_MASK);
        return;
    }

    bs_init(&bs, sps_nal, pps_nal - sps_nal - sizeof(nal_prefix));
    if (bs_h264_read_sps(&bs, &sps) < 0) {
        GST_ERROR ("failed to read SPS at %d", (int) (bs.p - begin));
        return;
    }

    bs_init(&bs, pps_nal, end - pps_nal);
    if (bs_h264_read_pps(&bs, &pps, &sps) < 0) {
        GST_ERROR ("failed to read PPS at %d", (int) (bs.p - begin));
        return;
    }

    if (sps.seq_parameter_set_id != pps.seq_parameter_set_id) {
        GST_ERROR ("SPS and PPS seq_parameter_set_id does not match: %d != %d",
            sps.seq_parameter_set_id,  pps.seq_parameter_set_id);
        return;
    }

    if (sps.seq_parameter_set_id == sps_id) {
        return;
    }

    sps.seq_parameter_set_id = sps_id;
    pps.seq_parameter_set_id = sps_id;

    bs_init(&bs, sps_nal, begin + max_size - sps_nal);

    if (bs_h264_write_sps(&bs, &sps) < 0) {
        GST_ERROR ("failed to write SPS at %d", (int) (bs.p - begin));
        return;
    }

    bs.b_ep3b = 0;
    bs_write(&bs, sizeof(nal_prefix) * 8, 1);
    bs.b_ep3b = 1;

    if (bs_h264_write_pps(&bs, &pps, &sps) < 0) {
        GST_ERROR ("failed to write PPS at %d", (int) (bs.p - begin));
        return;
    }
    bs_flush(&bs);

    *size = bs.p - (uint8_t *) data;
}
