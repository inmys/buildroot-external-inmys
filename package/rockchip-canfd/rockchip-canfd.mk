#############################################################
#
# rockchip-canfd
#
#############################################################

ROCKCHIP_CANFD_VERSION = 1.0.0
ROCKCHIP_CANFD_SOURCE = rockchip_canfd.tar.gz
ROCKCHIP_CANFD_SITE = file://$(BR2_EXTERNAL_INMYS_PATH)/package/rockchip-canfd

# orig source from rockchip linux kernel 4.19, commit: 850110ad821042077e398e0ed6e54466a3e3a75d

$(eval $(kernel-module))
$(eval $(generic-package))
