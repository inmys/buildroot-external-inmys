################################################################################
#
# elvees-mcom03-tools
#
# Host tools required to build the MCom-03 (К1892ВМ21Я) boot chain:
#   - bootrom-tools (mksbimage, otpcreator, otpdump, sbimgCrack)
#   - MIPS32 bare-metal toolchain (mipsel-elvees-elf32) used by ddrinit
#     and the SBL
#
# Both are shipped inside the official ELVEES "MCom-03R Linux SDK"
# toolchain tarball.  The original download locations
# (callisto.elvees.com and diver.elvees.com) are not reachable from
# outside the ELVEES network, so the SDK tarball from dist.elvees.com
# is used instead.  Only the two directories we need are extracted.
#
################################################################################

ELVEES_MCOM03_TOOLS_VERSION = 2025.09
ELVEES_MCOM03_TOOLS_KERNEL = linux66
ELVEES_MCOM03_TOOLS_SOURCE = aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
ELVEES_MCOM03_TOOLS_SITE = https://dist.elvees.com/mcom03r/buildroot/$(ELVEES_MCOM03_TOOLS_VERSION)/$(ELVEES_MCOM03_TOOLS_KERNEL)/images
# dist.elvees.com uses an expired certificate
ELVEES_MCOM03_TOOLS_DL_OPTS = --no-check-certificate
ELVEES_MCOM03_TOOLS_LICENSE = Proprietary (ELVEES)
ELVEES_MCOM03_TOOLS_REDISTRIBUTE = NO

HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR = $(HOST_DIR)/opt
HOST_ELVEES_MCOM03_TOOLS_MIPS_TOOLCHAIN = \
	$(HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR)/toolchain-mipsel-elvees-elf32
HOST_ELVEES_MCOM03_TOOLS_BOOTROM_TOOLS = \
	$(HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR)/bootrom-tools

# Extract only opt/bootrom-tools and opt/toolchain-mipsel-elvees-elf32
# from the 600 MiB SDK tarball.
define HOST_ELVEES_MCOM03_TOOLS_EXTRACT_CMDS
	mkdir -p $(@D)/opt
	tar -xf $(HOST_ELVEES_MCOM03_TOOLS_DL_DIR)/$(ELVEES_MCOM03_TOOLS_SOURCE) \
		--strip-components=2 -C $(@D)/opt \
		aarch64-buildroot-linux-gnu_sdk-buildroot/opt/bootrom-tools \
		aarch64-buildroot-linux-gnu_sdk-buildroot/opt/toolchain-mipsel-elvees-elf32
endef

define HOST_ELVEES_MCOM03_TOOLS_INSTALL_CMDS
	mkdir -p $(HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR)
	cp -a $(@D)/opt/bootrom-tools $(HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR)/
	cp -a $(@D)/opt/toolchain-mipsel-elvees-elf32 $(HOST_ELVEES_MCOM03_TOOLS_INSTALL_DIR)/
	$(INSTALL) -D -m 0644 $(HOST_ELVEES_MCOM03_TOOLS_PKGDIR)/toolchain.cmake \
		$(HOST_ELVEES_MCOM03_TOOLS_MIPS_TOOLCHAIN)/share/cmake/toolchain.cmake
	mkdir -p $(HOST_DIR)/bin
	for t in mksbimage otpcreator otpcreatorext otpdump sbimgCrack; do \
		ln -sf ../opt/bootrom-tools/$$t/$$t $(HOST_DIR)/bin/$$t; \
	done
endef

$(eval $(host-generic-package))
