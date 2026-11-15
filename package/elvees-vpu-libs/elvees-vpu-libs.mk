ELVEES_VPU_LIBS_VERSION = d00e74d-20261108
ELVEES_VPU_LIBS_SOURCE = mcom03-vpu-libs-$(ELVEES_VPU_LIBS_VERSION).tar.gz
ELVEES_VPU_LIBS_SITE = https://dist.elvees.com/mcom03r/packages/mcom03-vpu-libs
ELVEES_VPU_LIBS_DL_OPTS = --no-check-certificate
ELVEES_VPU_LIBS_STRIP_COMPONENTS = 0
ELVEES_VPU_LIBS_LICENSE = Proprietary (ELVEES/Arm runtime), MIT (OpenMAX headers)
ELVEES_VPU_LIBS_INSTALL_STAGING = YES
ELVEES_VPU_LIBS_DEPENDENCIES = elvees-vpu-modules

define ELVEES_VPU_LIBS_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/include
	cp -a $(@D)/staging/usr/include/IL $(STAGING_DIR)/usr/include/
endef

define ELVEES_VPU_LIBS_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/target/usr/lib/libmveomx.so.0.1 $(TARGET_DIR)/usr/lib/libmveomx.so.0.1
	ln -sf libmveomx.so.0.1 $(TARGET_DIR)/usr/lib/libOMX_Core.so
endef

$(eval $(generic-package))
