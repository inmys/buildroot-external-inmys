ELVEES_VPU_MODULES_VERSION = 2025.09
ELVEES_VPU_MODULES_SOURCE = mcom03r-defconfig-src.tar.gz
ELVEES_VPU_MODULES_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66
ELVEES_VPU_MODULES_DL_SUBDIR = elvees-sdk-sources
ELVEES_VPU_MODULES_DL_OPTS = --no-check-certificate
ELVEES_VPU_MODULES_LICENSE = GPL-2.0, Proprietary (codec firmware)
ELVEES_VPU_MODULES_MODULE_SUBDIRS = drivers/video/arm/v5xx
ELVEES_VPU_MODULES_MODULE_MAKE_OPTS = KDIR=$(LINUX_DIR) ARCH=$(KERNEL_ARCH) \
	CONFIG_MALI_VPU=m CONFIG_MALI_VPU_DEVICE_TREE=y CONFIG_MALI_VPU_MCOM03=y \
	CONFIG_MALI_VPU_ENABLE_DVFS_SIM=y CONFIG_MALI_VPU_V4L2=y

define ELVEES_VPU_MODULES_COPY_V4L2_FRONTEND
	$(INSTALL) -m 0644 $(ELVEES_VPU_MODULES_PKGDIR)/src/mve_v4l2.c \
		$(@D)/drivers/video/arm/v5xx/base/mve_v4l2.c
endef
ELVEES_VPU_MODULES_PRE_BUILD_HOOKS += ELVEES_VPU_MODULES_COPY_V4L2_FRONTEND

define ELVEES_VPU_MODULES_EXTRACT_CMDS
	$(TAR) -xOf $(ELVEES_VPU_MODULES_DL_DIR)/$(ELVEES_VPU_MODULES_SOURCE) --occurrence=1 \
		mcom03r-defconfig-src/buildroot/dl/mcom03-vpu-modules/mcom03-vpu-modules-master-br1.tar.gz \
		> $(@D)/driver-source.tar.gz
	$(TAR) -xf $(@D)/driver-source.tar.gz --strip-components=1 -C $(@D)
	rm $(@D)/driver-source.tar.gz
endef

define ELVEES_VPU_MODULES_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/lib/firmware
	$(INSTALL) -m 0644 $(@D)/drivers/video/arm/v5xx/firmware/*.fwb $(TARGET_DIR)/lib/firmware/
endef



$(eval $(kernel-module))
$(eval $(generic-package))
