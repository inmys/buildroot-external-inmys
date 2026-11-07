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

MCOM03_SBL_CONFIG_TOML = \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/board/nms-sm-k1892vm21ya/nms-sm-k1892vm21ya-sbl-config.toml
# The JSON is generated into the package build directory so that the
# source tree stays clean.
MCOM03_SBL_CONFIG_JSON = $(@D)/$(notdir $(basename $(MCOM03_SBL_CONFIG_TOML))).json

MCOM03_SBL_MAKE_ENV = \
	PATH=$(HOST_DIR)/opt/bootrom-tools/mksbimage:$(HOST_DIR)/opt/bootrom-tools/otpcreator:$(BR_PATH)

# The SBL embeds the U-Boot device trees, so they have to be present in
# BINARIES_DIR/uboot-dtb/ before the CMake configure step.  Buildroot's
# U-Boot package does not install the DTBs itself (the ELVEES fork of
# Buildroot adds BR2_TARGET_UBOOT_INSTALL_DTB for that), so they are
# copied out of the U-Boot build directory here.
MCOM03_SBL_UBOOT_DTBS = \
	mcom03r-nms-sm-evm \
	mcom03r-nms-smarc-tester \
	mcom03r-nms-itx-sm-lc-v4 \
	mcom03r-nms-sm-elvsmarccb

define MCOM03_SBL_INSTALL_UBOOT_DTBS
	mkdir -p $(BINARIES_DIR)/uboot-dtb
	for dtb in $(MCOM03_SBL_UBOOT_DTBS); do \
		src=$$(ls $(BUILD_DIR)/uboot-*/arch/arm/dts/$$dtb.dtb 2>/dev/null | head -n1); \
		if [ -z "$$src" ]; then \
			echo "ERROR: $$dtb.dtb was not built by U-Boot" >&2; \
			exit 1; \
		fi; \
		cp -f "$$src" $(BINARIES_DIR)/uboot-dtb/; \
	done
endef

# The SBL config TOML uses the U-Boot DTB names without the .dtb suffix;
# make sure the file names in BINARIES_DIR/uboot-dtb match the TOML.
define MCOM03_SBL_GENERATE_CONFIG
	$(HOST_DIR)/bin/python3 \
		$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/support/gen_sbl_config.py \
		-i $(MCOM03_SBL_CONFIG_TOML) -o $(MCOM03_SBL_CONFIG_JSON)
endef
MCOM03_SBL_PRE_CONFIGURE_HOOKS += MCOM03_SBL_INSTALL_UBOOT_DTBS \
	MCOM03_SBL_GENERATE_CONFIG

define MCOM03_SBL_INSTALL_TARGET_CMDS
	$(MCOM03_SBL_MAKE_ENV) $(MCOM03_SBL_BUILD_ENV) DESTDIR=$(BINARIES_DIR) \
		$(BR2_CMAKE) --install $(MCOM03_SBL_BUILDDIR)
endef

$(eval $(cmake-package))
