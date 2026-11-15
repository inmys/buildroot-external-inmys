ELVEES_ROGUE_MODULES_VERSION = 2025.09
ELVEES_ROGUE_MODULES_SOURCE = mcom03r-defconfig-src.tar.gz
ELVEES_ROGUE_MODULES_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66
ELVEES_ROGUE_MODULES_DL_SUBDIR = elvees-sdk-sources
ELVEES_ROGUE_MODULES_DL_OPTS = --no-check-certificate
ELVEES_ROGUE_MODULES_LICENSE = MIT or GPL-2.0
ELVEES_ROGUE_MODULES_LICENSE_FILES = MIT-COPYING GPL-COPYING
ELVEES_ROGUE_MODULES_DEPENDENCIES = linux
ELVEES_ROGUE_MODULES_SETTINGS = PVR_BUILD_DIR=mcom03 \
	KERNELDIR=$(LINUX_DIR) ARCH=$(KERNEL_ARCH) CROSS_COMPILE=$(TARGET_CROSS) \
	DISCIMAGE=$(TARGET_DIR) SYSROOT=$(STAGING_DIR) \
	WINDOW_SYSTEM=lws-generic BUILD=release PDUMP=0

# The public source distribution contains the cached Gerrit source archive.
# --occurrence stops after this member; no need to unpack the entire 2.9 GiB SDK.
define ELVEES_ROGUE_MODULES_EXTRACT_CMDS
	$(TAR) -xOf $(ELVEES_ROGUE_MODULES_DL_DIR)/$(ELVEES_ROGUE_MODULES_SOURCE) --occurrence=1 \
		mcom03r-defconfig-src/buildroot/dl/mcom03-rogue-modules/mcom03-rogue-modules-elvees-1.9.y-br1.tar.gz \
		> $(@D)/driver-source.tar.gz
	$(TAR) -xf $(@D)/driver-source.tar.gz --strip-components=1 -C $(@D)
	rm $(@D)/driver-source.tar.gz
endef

define ELVEES_ROGUE_MODULES_BUILD_CMDS
	$(TARGET_MAKE_ENV) $(MAKE) -C $(@D) $(ELVEES_ROGUE_MODULES_SETTINGS)
endef

define ELVEES_ROGUE_MODULES_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0644 $(@D)/binary_mcom03_lws-generic_release/target_aarch64/pvrsrvkm.ko \
		$(TARGET_DIR)/lib/modules/$(LINUX_VERSION_PROBED)/updates/pvrsrvkm.ko
endef

$(eval $(generic-package))
