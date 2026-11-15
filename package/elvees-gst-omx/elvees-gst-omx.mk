ELVEES_GST_OMX_VERSION = 1.18.1-60-g6d00dd0
ELVEES_GST_OMX_SITE = $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/package/elvees-gst-omx/src
ELVEES_GST_OMX_SITE_METHOD = local
ELVEES_GST_OMX_LICENSE = LGPL-2.1+
ELVEES_GST_OMX_LICENSE_FILES = COPYING
ELVEES_GST_OMX_DEPENDENCIES = elvees-vpu-libs gst1-plugins-base
ELVEES_GST_OMX_CONF_OPTS = -Dtarget=mcom03 -Dheader_path=$(STAGING_DIR)/usr/include/IL \
	-Dexamples=disabled -Dtests=disabled -Dtools=disabled -Ddoc=disabled

$(eval $(meson-package))
