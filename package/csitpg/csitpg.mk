#############################################################
#
# csitpg
#
#############################################################

CSITPG_VERSION = 1.0.0
CSITPG_SOURCE = csitpg.tar.gz
CSITPG_SITE = file://$(BR2_EXTERNAL_INMYS_PATH)/package/csitpg

#$(eval $(kernel-module))
$(eval $(generic-package))
