ELVEES_GST_FELIX_VERSION = 4e73f88-20261109
ELVEES_GST_FELIX_SOURCE = gst-felix-$(ELVEES_GST_FELIX_VERSION).tar.gz
ELVEES_GST_FELIX_SITE = https://dist.elvees.com/mcom03r/packages/gst-felix
ELVEES_GST_FELIX_DL_OPTS = --no-check-certificate
ELVEES_GST_FELIX_STRIP_COMPONENTS = 0
ELVEES_GST_FELIX_LICENSE = Proprietary
ELVEES_GST_FELIX_DEPENDENCIES = gstreamer1 elvees-felix

define ELVEES_GST_FELIX_INSTALL_TARGET_CMDS
	cp -a $(@D)/lib/. $(TARGET_DIR)/lib/
endef

$(eval $(generic-package))
