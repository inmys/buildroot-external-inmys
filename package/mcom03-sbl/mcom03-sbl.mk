################################################################################
#
# mcom03-sbl
#
# Second bootloader (SBL) for the ELVEES MCom-03 (К1892ВМ21Я).
# SBL-XIP is stored in the QSPI0 boot flash and copies ddrinit, TF-A,
# U-Boot and the U-Boot DTB from QSPI to DDR before jumping to TF-A.
#
################################################################################

MCOM03_SBL_VERSION = b381285167a41983a7f9f689cbd559a967bbd162
MCOM03_SBL_SITE = https://github.com/elvees/mcom03-sbl
MCOM03_SBL_SITE_METHOD = git
MCOM03_SBL_LICENSE = MIT
MCOM03_SBL_LICENSE_FILES = LICENSE
MCOM03_SBL_DEPENDENCIES = host-elvees-mcom03-tools arm-trusted-firmware \
	uboot ddrinit

MCOM03_SBL_CONF_OPTS = \
	-DIMAGES_PATH=$(BINARIES_DIR) \
	-DCMAKE_TOOLCHAIN_FILE=$(HOST_DIR)/opt/toolchain-mipsel-elvees-elf32/share/cmake/toolchain.cmake \
	-DCMAKE_INSTALL_PREFIX= \
	-DSBL_CONFIG_FILE=$(MCOM03_SBL_CONFIG_JSON) \
	-DREPO_DIR=$(@D) \
	-DSBIMG_SEC_TYPE=none \
	-DWDT_ENABLE=ON \
	-DUART_ENABLE=ON \
	-DBOOTSTAGE_ENABLE=ON

# Native SBL JSON: no intermediate format or configuration generator.
MCOM03_SBL_CONFIG_JSON = \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/board/nms-sm-k1892vm21ya/nms-sm-k1892vm21ya-sbl-config.json

MCOM03_SBL_MAKE_ENV = \
	PATH=$(HOST_DIR)/opt/bootrom-tools/mksbimage:$(HOST_DIR)/opt/bootrom-tools/otpcreator:$(BR_PATH)

# The SBL embeds the U-Boot device trees, so they have to be present in
# BINARIES_DIR/uboot-dtb/ before the CMake configure step.  Buildroot's
# U-Boot package does not install the DTBs itself (the ELVEES fork of
# Buildroot adds BR2_TARGET_UBOOT_INSTALL_DTB for that), so they are
# copied out of the U-Boot build directory here.
MCOM03_SBL_UBOOT_DTBS = $(basename $(notdir $(wildcard \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/board/nms-sm-k1892vm21ya/uboot/*.dts)))

define MCOM03_SBL_INSTALL_UBOOT_DTBS
	mkdir -p $(BINARIES_DIR)/uboot-dtb
	rm -f $(BINARIES_DIR)/uboot-dtb/mcom03r-nms-*.dtb
	for dtb in $(MCOM03_SBL_UBOOT_DTBS); do \
		src=$(UBOOT_DIR)/arch/arm/dts/$$dtb.dtb; \
		if [ ! -f "$$src" ]; then \
			echo "ERROR: $$dtb.dtb was not built by U-Boot" >&2; \
			exit 1; \
		fi; \
		cp -f "$$src" $(BINARIES_DIR)/uboot-dtb/; \
	done
endef

MCOM03_SBL_PRE_CONFIGURE_HOOKS += MCOM03_SBL_INSTALL_UBOOT_DTBS
MCOM03_SBL_PRE_BUILD_HOOKS += MCOM03_SBL_INSTALL_UBOOT_DTBS

# The module now has one bootloader DTB. Remove earlier per-carrier output
# files before installing, so glob-based image consumers cannot reuse them.
define MCOM03_SBL_CLEAN_OLD_BOOT_IMAGES
	rm -f $(BINARIES_DIR)/sbl/sbl-xip-nms-sm-k1892vm21ya-*.bin
	rm -f $(BINARIES_DIR)/tl-sbimg/nms-sm-k1892vm21ya-*.tl-image
	rm -f $(BINARIES_DIR)/sd-boot/nms-*.sbimg
	rm -f $(BINARIES_DIR)/qspi-full-nms-*.bin $(BINARIES_DIR)/qspi-full-nms-*.bin.sha256 \
		$(BINARIES_DIR)/qspi-full-nms-*.layout.txt
endef
MCOM03_SBL_PRE_INSTALL_TARGET_HOOKS += MCOM03_SBL_CLEAN_OLD_BOOT_IMAGES

define MCOM03_SBL_INSTALL_TARGET_CMDS
	$(MCOM03_SBL_MAKE_ENV) $(MCOM03_SBL_BUILD_ENV) DESTDIR=$(BINARIES_DIR) \
		$(BR2_CMAKE) --install $(MCOM03_SBL_BUILDDIR)
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/support/make-sdboot.sh \
		$(@D) $(BINARIES_DIR) $(HOST_DIR) \
		$(MCOM03_SBL_UBOOT_DTBS) $(DDRINIT_DEFCONFIGS)-bootrom
endef

$(eval $(cmake-package))
