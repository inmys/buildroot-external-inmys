include $(sort $(wildcard $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/package/*/*.mk))

NMS_BOARD_DIR := $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/board/nms-sm-k1892vm21ya

# The normal configure step records these snapshots, and the final install
# consumes them. Refresh missing snapshots when rebuilding without configure.
define NMS_REBUILD_FILE_LISTS
	@if test ! -f $(@D)/.files-list.before; then $(call pkg_size_before,$(TARGET_DIR)); fi
	@if test ! -f $(@D)/.files-list-staging.before; then $(call pkg_size_before,$(STAGING_DIR),-staging); fi
	@if test ! -f $(@D)/.files-list-images.before; then $(call pkg_size_before,$(BINARIES_DIR),-images); fi
	@if test ! -f $(@D)/.files-list-host.before; then $(call pkg_size_before,$(HOST_DIR),-host); fi
endef

# Buildroot copies this directory through BR2_LINUX_KERNEL_CUSTOM_DTS_DIR.
# Its package stamp does not normally depend on external DTS sources.
# Track files and directories so editing, adding or removing a tree also
# rebuilds/installs Linux when invoked by plain `make` in sdk2/output.
ifeq ($(BR2_LINUX_KERNEL),y)
NMS_LINUX_DTS_DIR := $(NMS_BOARD_DIR)/linux/dts
NMS_LINUX_DTS_SOURCES := $(wildcard $(NMS_LINUX_DTS_DIR)/elvees/*.dts \
	$(NMS_LINUX_DTS_DIR)/elvees/*.dtsi $(NMS_LINUX_DTS_DIR)/elvees/*.dtso)
$(LINUX_TARGET_BUILD): $(NMS_LINUX_DTS_SOURCES) $(NMS_LINUX_DTS_DIR) \
	$(NMS_LINUX_DTS_DIR)/elvees $(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/external.mk

LINUX_PRE_BUILD_HOOKS += NMS_REBUILD_FILE_LISTS

# Remove DTBs of deleted carrier sources before installing the current set.
define NMS_LINUX_CLEAN_IMAGE_DTBS
	rm -f $(BINARIES_DIR)/elvees/nms-*.dtb
endef
LINUX_PRE_INSTALL_IMAGES_HOOKS += NMS_LINUX_CLEAN_IMAGE_DTBS

define NMS_LINUX_CLEAN_TARGET_DTBS
	rm -f $(TARGET_DIR)/boot/elvees/nms-*.dtb
endef
LINUX_PRE_INSTALL_TARGET_HOOKS += NMS_LINUX_CLEAN_TARGET_DTBS
endif

# U-Boot copies these files through BR2_TARGET_UBOOT_CUSTOM_DTS_PATH.
# scripts/Makefile.dts builds CONFIG_OF_LIST as well as the default tree,
# so no vendor Makefile patch is needed for the common module DTB.
ifeq ($(BR2_TARGET_UBOOT),y)
NMS_UBOOT_ENV := $(NMS_BOARD_DIR)/uboot/nms-sm-k1892vm21ya.env
define NMS_UBOOT_COPY_ENV
	cp $(NMS_UBOOT_ENV) $(@D)/board/elvees/common/
endef
UBOOT_PRE_BUILD_HOOKS += NMS_UBOOT_COPY_ENV NMS_REBUILD_FILE_LISTS
UBOOT_MAKE_OPTS += CONFIG_OF_LIST="$(basename $(notdir $(UBOOT_CUSTOM_DTS_PATH)))"
$(UBOOT_TARGET_BUILD): $(UBOOT_CUSTOM_DTS_PATH) $(NMS_UBOOT_ENV) $(NMS_BOARD_DIR)/uboot \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/external.mk
endif

# Buildroot normally orders package dependencies without tracking their
# stamps. Repack the SBL and install SD/QSPI capsules after firmware updates.
ifeq ($(BR2_PACKAGE_MCOM03_SBL),y)
MCOM03_SBL_PRE_BUILD_HOOKS += NMS_REBUILD_FILE_LISTS
$(MCOM03_SBL_TARGET_CONFIGURE): $(MCOM03_SBL_CONFIG_JSON)
$(MCOM03_SBL_TARGET_BUILD): $(UBOOT_TARGET_INSTALL) \
	$(ARM_TRUSTED_FIRMWARE_TARGET_INSTALL) $(DDRINIT_TARGET_INSTALL) \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/support/make-sdboot.sh \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/package/mcom03-sbl/mcom03-sbl.mk
endif
