################################################################################
#
# ms2130-status
#
################################################################################

MS2130_STATUS_SITE = $(BR2_EXTERNAL_INMYS_PATH)/package/ms2130-status
MS2130_STATUS_SITE_METHOD = local
MS2130_STATUS_LICENSE = Proprietary

define MS2130_STATUS_BUILD_CMDS
	$(TARGET_CC) $(TARGET_CFLAGS) -Os -o $(@D)/ms2130-status $(@D)/ms2130-status.c
endef

define MS2130_STATUS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ms2130-status $(TARGET_DIR)/usr/bin/ms2130-status
endef

$(eval $(generic-package))
