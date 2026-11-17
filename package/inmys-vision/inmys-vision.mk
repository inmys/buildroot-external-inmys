INMYS_VISION_VERSION = 1
INMYS_VISION_SITE = $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/package/inmys-vision/src
INMYS_VISION_SITE_METHOD = local
INMYS_VISION_LICENSE = Apache-2.0
INMYS_VISION_LICENSE_FILES = LICENSE NOTICE
INMYS_VISION_DEPENDENCIES = libgpiod elvees-gst-felix elvees-vpu-modules elvees-elcorenn elvees-yolov8n gst1-rtsp-server gst1-plugins-good gst1-plugins-bad gst1-plugins-ugly

define INMYS_VISION_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -Wall -Wextra -Werror \
		`$(PKG_CONFIG_HOST_BINARY) --cflags gstreamer-rtsp-server-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-allocators-1.0 libgpiod` \
		$(@D)/vision.cpp $(@D)/camera.cpp $(@D)/yolo.cpp $(@D)/overlay.cpp $(@D)/rtsp.cpp $(@D)/web.cpp -pthread \
		-L$(STAGING_DIR)/usr/lib/elvees-nn -lelcorenn -Wl,-rpath-link,$(STAGING_DIR)/usr/lib/elvees-nn \
		`$(PKG_CONFIG_HOST_BINARY) --libs gstreamer-rtsp-server-1.0 gstreamer-app-1.0 gstreamer-video-1.0 gstreamer-allocators-1.0 libgpiod` \
		$(TARGET_LDFLAGS) -o $(@D)/inmys-vision
endef

define INMYS_VISION_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/inmys-vision $(TARGET_DIR)/usr/libexec/inmys-vision
	$(INSTALL) -D -m 0644 $(@D)/NOTICE $(TARGET_DIR)/usr/share/inmys/NOTICE
endef

$(eval $(generic-package))
