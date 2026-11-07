################################################################################
#
# ddrinit
#
# DDR initializer for the ELVEES MCom-03 (К1892ВМ21Я) DDR subsystem.
# It is a bare-metal MIPS32 program that runs on the RISC0 core before
# TF-A/U-Boot and trains the LPDDR4/DDR4 interface.
#
# The package builds one image per entry of DDRINIT_DEFCONFIGS (plus the
# "-bootrom" variant used when ddrinit is started by the BootROM) and
# installs it to BINARIES_DIR/ddrinit-<defconfig>/, which is the layout
# expected by the SBL build (see the SBL config TOML).
#
################################################################################

DDRINIT_VERSION = 12ca43ab6964dece88c464f104af4d861a259a6b
DDRINIT_SITE = https://github.com/elvees/ddrinit
DDRINIT_SITE_METHOD = git
DDRINIT_LICENSE = GPL-2.0-or-later
DDRINIT_LICENSE_FILES = LICENSE

DDRINIT_DEPENDENCIES = host-elvees-mcom03-tools host-kconfiglib

# Defconfigs (files in <ddrinit>/configs/<name>_defconfig) to build.
DDRINIT_DEFCONFIGS = nms-sm-k1892vm21ya

DDRINIT_CROSS_COMPILE = \
	$(HOST_DIR)/opt/toolchain-mipsel-elvees-elf32/bin/mipsel-elvees-elf-

DDRINIT_BOARD_DIR = \
	$(BR2_EXTERNAL_INMYS_NMS_SM_K1892_PATH)/board/nms-sm-k1892vm21ya

# Board specific defconfigs are kept in the external tree.
define DDRINIT_COPY_DEFCONFIGS
	cp $(DDRINIT_BOARD_DIR)/ddrinit/*_defconfig $(@D)/configs/
endef
DDRINIT_POST_EXTRACT_HOOKS += DDRINIT_COPY_DEFCONFIGS
DDRINIT_POST_RSYNC_HOOKS += DDRINIT_COPY_DEFCONFIGS

define DDRINIT_BUILD_CMDS
	export PATH=$(BR_PATH); \
	for cfg in $(DDRINIT_DEFCONFIGS); do \
		$(MAKE) -C $(@D) clean-all || exit 1; \
		$(MAKE) -C $(@D) $${cfg}_defconfig || exit 1; \
		$(MAKE) -C $(@D) CROSS_COMPILE=$(DDRINIT_CROSS_COMPILE) all || exit 1; \
		$(MAKE) -C $(@D) install DESTDIR=$(BINARIES_DIR)/ddrinit-$${cfg} || exit 1; \
		$(MAKE) -C $(@D) clean-all || exit 1; \
		$(MAKE) -C $(@D) FRAGMENTS=bootrom $${cfg}_defconfig || exit 1; \
		$(MAKE) -C $(@D) CROSS_COMPILE=$(DDRINIT_CROSS_COMPILE) all || exit 1; \
		$(MAKE) -C $(@D) install DESTDIR=$(BINARIES_DIR)/ddrinit-$${cfg}-bootrom || exit 1; \
	done
endef

# ddrinit is a bare-metal firmware, nothing is installed into the target
# root filesystem.
define DDRINIT_INSTALL_TARGET_CMDS
	:
endef

$(eval $(generic-package))
