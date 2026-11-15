// SPDX-License-Identifier: GPL-2.0+
/*
 * Copyright 2022 RnD Center "ELVEES", JSC
 */

#ifndef __GST_OMX_MJPEG_ENC_H__
#define __GST_OMX_MJPEG_ENC_H__

#include <gst/gst.h>
#include "gstomxvideoenc.h"

G_BEGIN_DECLS

#define GST_TYPE_OMX_MJPEG_ENC \
  (gst_omx_mjpeg_enc_get_type())
#define GST_OMX_MJPEG_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_OMX_MJPEG_ENC,GstOMXMJPEGEnc))
#define GST_OMX_MJPEG_ENC_CLASS(klass) \
  (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_OMX_MJPEG_ENC,GstOMXMJPEGEncClass))
#define GST_OMX_MJPEG_ENC_GET_CLASS(obj) \
  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_OMX_MJPEG_ENC,GstOMXMJPEGEncClass))
#define GST_IS_OMX_MJPEG_ENC(obj) \
  (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_OMX_MJPEG_ENC))
#define GST_IS_OMX_MJPEG_ENC_CLASS(obj) \
  (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_OMX_MJPEG_ENC))

typedef struct _GstOMXMJPEGEnc GstOMXMJPEGEnc;
typedef struct _GstOMXMJPEGEncClass GstOMXMJPEGEncClass;

struct _GstOMXMJPEGEnc
{
  GstOMXVideoEnc parent;

  guint32 qfactor;
};

struct _GstOMXMJPEGEncClass
{
  GstOMXVideoEncClass parent_class;
};

GType gst_omx_mjpeg_enc_get_type (void);

G_END_DECLS

#endif /* __GST_OMX_MJPEG_ENC_H__ */
