// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>

#include "gstomxmjpegenc.h"

GST_DEBUG_CATEGORY_STATIC (gst_omx_mjpeg_enc_debug_category);
#define GST_CAT_DEFAULT gst_omx_mjpeg_enc_debug_category

/* prototypes */
static gboolean gst_omx_mjpeg_enc_set_format (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);
static GstCaps *gst_omx_mjpeg_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state);

static void gst_omx_mjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_omx_mjpeg_enc_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

enum
{
  PROP_0,
  PROP_QFACTOR
};

#define GST_OMX_MJPEG_ENC_QFACTOR_DEFAULT (0)

/* class initialization */

#define DEBUG_INIT \
  GST_DEBUG_CATEGORY_INIT (gst_omx_mjpeg_enc_debug_category, "omxmjpegenc", 0, \
      "debug category for gst-omx video encoder base class");

G_DEFINE_TYPE_WITH_CODE (GstOMXMJPEGEnc, gst_omx_mjpeg_enc,
    GST_TYPE_OMX_VIDEO_ENC, DEBUG_INIT);

static void
gst_omx_mjpeg_enc_class_init (GstOMXMJPEGEncClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstOMXVideoEncClass *videoenc_class = GST_OMX_VIDEO_ENC_CLASS (klass);
  GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

  videoenc_class->set_format = GST_DEBUG_FUNCPTR (gst_omx_mjpeg_enc_set_format);
  videoenc_class->get_caps = GST_DEBUG_FUNCPTR (gst_omx_mjpeg_enc_get_caps);

  gobject_class->set_property = gst_omx_mjpeg_enc_set_property;
  gobject_class->get_property = gst_omx_mjpeg_enc_get_property;

  videoenc_class->cdata.default_src_template_caps = "video/x-jpeg, "
      "width=(int) [1,MAX], height=(int) [1,MAX]";

  gst_element_class_set_static_metadata (element_class,
      "OpenMAX MJPEG Video Encoder",
      "Codec/Encoder/Video/Hardware",
      "Encode MJPEG video streams",
      "RnD Center ELVEES, JSC");

  gst_omx_set_default_role (&videoenc_class->cdata, "video_encoder.jpeg");

  g_object_class_install_property (gobject_class, PROP_QFACTOR,
      g_param_spec_uint ("qfactor", "Compression quality factor",
          "The quality factor for JPEG compression (0=component default)",
          0, 100, GST_OMX_MJPEG_ENC_QFACTOR_DEFAULT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS |
          GST_PARAM_MUTABLE_READY));

}

static void
gst_omx_mjpeg_enc_init (GstOMXMJPEGEnc * self)
{
  self->qfactor = GST_OMX_MJPEG_ENC_QFACTOR_DEFAULT;
}

static gboolean
gst_omx_mjpeg_enc_set_format (GstOMXVideoEnc * enc, GstOMXPort * port,
    GstVideoCodecState * state)
{
  gboolean ret;
  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  GstOMXMJPEGEnc *self = GST_OMX_MJPEG_ENC (enc);

  gst_omx_port_get_port_definition (port, &port_def);
  port_def.format.video.eCompressionFormat = OMX_VIDEO_CodingMJPEG;
  ret = gst_omx_port_update_port_definition (port, &port_def) == OMX_ErrorNone;

  if (ret && self->qfactor != GST_OMX_MJPEG_ENC_QFACTOR_DEFAULT) {
    OMX_IMAGE_PARAM_QFACTORTYPE qfactor_param;
    OMX_ERRORTYPE err;

    GST_OMX_INIT_STRUCT (&qfactor_param);
    qfactor_param.nPortIndex = GST_OMX_VIDEO_ENC (self)->enc_out_port->index;
    qfactor_param.nQFactor = self->qfactor;

    err =
      gst_omx_component_set_parameter (GST_OMX_VIDEO_ENC (self)->enc,
      OMX_IndexParamQFactor, &qfactor_param);

    if (err != OMX_ErrorNone) {
      GST_ERROR_OBJECT (self, "Failed to set qfactor parameter: %s (0x%08x)",
          gst_omx_error_to_string (err), err);
      return FALSE;
    }
  }

  return ret;
}

static GstCaps *gst_omx_mjpeg_enc_get_caps (GstOMXVideoEnc * enc,
    GstOMXPort * port, GstVideoCodecState * state)
{
  return gst_caps_new_empty_simple ("video/x-jpeg");
}

static void
gst_omx_mjpeg_enc_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstOMXMJPEGEnc *self = GST_OMX_MJPEG_ENC (object);

  switch (prop_id) {
    case PROP_QFACTOR:
      self->qfactor = g_value_get_uint (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_omx_mjpeg_enc_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstOMXMJPEGEnc *self = GST_OMX_MJPEG_ENC (object);

  switch (prop_id) {
    case PROP_QFACTOR:
      g_value_set_uint (value, self->qfactor);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

