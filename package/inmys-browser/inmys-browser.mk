INMYS_BROWSER_VERSION = 1
INMYS_BROWSER_SOURCE =
INMYS_BROWSER_LICENSE = MIT
INMYS_BROWSER_DEPENDENCIES = elvees-powervr-graphics elvees-vpu-modules \
	weston cog glib-networking shared-mime-info gst1-plugins-good gst1-plugins-bad

# Buildroot disables WebRTC in WPE unconditionally. Use its GStreamer backend
# with the selected media-stream/WebRTC plugins for this browser profile.
ifeq ($(BR2_PACKAGE_INMYS_BROWSER),y)
WPEWEBKIT_CONF_OPTS := $(filter-out -DENABLE_WEB_RTC=OFF,$(WPEWEBKIT_CONF_OPTS)) \
	-DENABLE_WEB_RTC=ON -DUSE_GSTREAMER_WEBRTC=ON
endif

ifeq ($(BR2_PACKAGE_INMYS_BROWSER_CAMERA),y)
INMYS_BROWSER_LICENSE = MIT (launcher), Apache-2.0 (CSI bridge)
INMYS_BROWSER_LICENSE_FILES = COPYING.camera

define INMYS_BROWSER_COPY_CAMERA_LICENSE
	$(INSTALL) -m 0644 $(INMYS_BROWSER_PKGDIR)/../inmys-vision/src/LICENSE $(@D)/COPYING.camera
endef
INMYS_BROWSER_POST_EXTRACT_HOOKS += INMYS_BROWSER_COPY_CAMERA_LICENSE
INMYS_BROWSER_DEPENDENCIES += libgpiod elvees-gst-felix v4l2loopback

define INMYS_BROWSER_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) -std=c++17 -Wall -Wextra -Werror \
		-I$(INMYS_BROWSER_PKGDIR)/../inmys-vision/src \
		`$(PKG_CONFIG_HOST_BINARY) --cflags gstreamer-1.0 libgpiod` \
		$(INMYS_BROWSER_PKGDIR)/camera-bridge.cpp $(INMYS_BROWSER_PKGDIR)/../inmys-vision/src/camera.cpp \
		`$(PKG_CONFIG_HOST_BINARY) --libs gstreamer-1.0 libgpiod` \
		$(TARGET_LDFLAGS) -pthread -o $(@D)/inmys-browser-camera
endef

define INMYS_BROWSER_INSTALL_CAMERA
	$(INSTALL) -D -m 0755 $(@D)/inmys-browser-camera $(TARGET_DIR)/usr/libexec/inmys-browser-camera
endef
endif

# Match the current image and keep /data/browser ownership stable on updates.
define INMYS_BROWSER_USERS
	browser 101 browser 105 * /var/lib/browser /bin/sh video,audio Browser
endef

define INMYS_BROWSER_INSTALL_TARGET_CMDS
	$(INMYS_BROWSER_INSTALL_CAMERA)
	$(INSTALL) -D -m 0755 $(INMYS_BROWSER_PKGDIR)/inmys-browser $(TARGET_DIR)/usr/sbin/inmys-browser
	$(INSTALL) -D -m 0755 $(INMYS_BROWSER_PKGDIR)/inmys-cog $(TARGET_DIR)/usr/libexec/inmys-cog
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/weston.ini $(TARGET_DIR)/etc/inmys-browser/weston.ini
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/rutube-low-effects.css $(TARGET_DIR)/etc/inmys-browser/rutube-low-effects.css
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/70-inmys-browser.rules $(TARGET_DIR)/etc/udev/rules.d/70-inmys-browser.rules
	$(INSTALL) -D -m 0644 $(INMYS_BROWSER_PKGDIR)/test.html $(TARGET_DIR)/usr/share/inmys/browser-test.html
endef

$(eval $(generic-package))
