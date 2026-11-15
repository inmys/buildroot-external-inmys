ELVEES_FELIX_VERSION = 6.18-2.9-407-g81107df-20261109
ELVEES_FELIX_SOURCE = mcom03-felix-$(ELVEES_FELIX_VERSION).tar.gz
ELVEES_FELIX_SITE = https://dist.elvees.com/mcom03r/packages/mcom03-felix
ELVEES_FELIX_DL_OPTS = --no-check-certificate
ELVEES_FELIX_STRIP_COMPONENTS = 0
ELVEES_FELIX_LICENSE = GPL-2.0 (kernel module), unknown (utilities)
ELVEES_FELIX_DEPENDENCIES = linux libdrm libgpiod elvees-sensor-phy elvees-dmabuf-exporter

define ELVEES_FELIX_INSTALL_TARGET_CMDS
	test -f $(@D)/lib/modules/$(LINUX_VERSION_PROBED)/extra/Felix.ko || \
		{ echo "Felix binary does not match kernel $(LINUX_VERSION_PROBED)" >&2; exit 1; }
	cp -a $(@D)/usr/. $(TARGET_DIR)/usr/
	cp -a $(@D)/etc/. $(TARGET_DIR)/etc/
	# Generic OV5647 profile: do not apply shading for a specific Arducam lens.
	sed -e 's/^LSH_MATRIX_ENABLE.*/LSH_MATRIX_ENABLE 0/' -e '/^LSH_CTRL_/d' \
		$(@D)/etc/felix/ov5647/ov5647-arducam.cfg > $(TARGET_DIR)/etc/felix/ov5647/ov5647.cfg
	$(INSTALL) -D -m 0644 $(@D)/lib/modules/$(LINUX_VERSION_PROBED)/extra/Felix.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/extra/Felix.ko
endef

$(eval $(generic-package))
