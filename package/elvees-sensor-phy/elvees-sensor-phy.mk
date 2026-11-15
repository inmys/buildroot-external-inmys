ELVEES_SENSOR_PHY_VERSION = 202607-g889ee62
ELVEES_SENSOR_PHY_SITE = $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/package/elvees-sensor-phy/src
ELVEES_SENSOR_PHY_SITE_METHOD = local
ELVEES_SENSOR_PHY_LICENSE = GPL-2.0
ELVEES_SENSOR_PHY_INSTALL_STAGING = YES

define ELVEES_SENSOR_PHY_INSTALL_STAGING_CMDS
	$(INSTALL) -D -m 0644 $(@D)/sensor_phy.h $(STAGING_DIR)/usr/include/linux/sensor_phy.h
endef

$(eval $(kernel-module))
$(eval $(generic-package))
