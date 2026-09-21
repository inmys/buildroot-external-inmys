################################################################################
#
# lvdspat
#
################################################################################

LVDSPAT_SITE = $(BR2_EXTERNAL_INMYS_PATH)/package/lvdspat
LVDSPAT_SITE_METHOD = local
LVDSPAT_LICENSE = Proprietary
LVDSPAT_DEPENDENCIES = libdrm

define LVDSPAT_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Os -o $(@D)/lvdspat $(@D)/lvdspat.c \
		`$(PKG_CONFIG_HOST_BINARY) --cflags --libs libdrm`
endef

define LVDSPAT_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/lvdspat $(TARGET_DIR)/usr/bin/lvdspat
endef

$(eval $(generic-package))
