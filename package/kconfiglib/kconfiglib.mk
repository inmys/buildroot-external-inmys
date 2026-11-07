################################################################################
#
# kconfiglib
#
# kconfiglib provides the Kconfig frontends (genconfig, olddefconfig,
# defconfig, ...) used by the ELVEES ddrinit and SBL build systems.
# The Python modules are installed into HOST_DIR/lib/kconfiglib and the
# frontends are wrapped by small shell scripts in HOST_DIR/bin, so that
# "genconfig", "defconfig", ... work from any package's build commands.
#
################################################################################

KCONFIGLIB_VERSION = 14.1.0
KCONFIGLIB_SOURCE = kconfiglib-$(KCONFIGLIB_VERSION).tar.gz
KCONFIGLIB_SITE = https://files.pythonhosted.org/packages/59/29/d557718c84ef1a8f275faa4caf8e353778121beefbe9fadfa0055ca99aae
KCONFIGLIB_LICENSE = ISC
KCONFIGLIB_LICENSE_FILES = LICENSE.txt

HOST_KCONFIGLIB_DEPENDENCIES = host-python3

KCONFIGLIB_SCRIPTS = \
	alldefconfig allnoconfig allyesconfig defconfig genconfig guiconfig \
	listnewconfig menuconfig oldconfig olddefconfig savedefconfig setconfig

HOST_KCONFIGLIB_INSTALL_DIR = $(HOST_DIR)/lib/kconfiglib

define HOST_KCONFIGLIB_INSTALL_CMDS
	mkdir -p $(HOST_DIR)/bin $(HOST_KCONFIGLIB_INSTALL_DIR)
	cp $(@D)/*.py $(HOST_KCONFIGLIB_INSTALL_DIR)/
	for f in $(KCONFIGLIB_SCRIPTS); do \
		printf '#!/bin/sh\n' > $(HOST_DIR)/bin/$$f; \
		printf 'PYTHONPATH=%s exec %s %s/%s.py "$$@"\n' \
			$(HOST_KCONFIGLIB_INSTALL_DIR) \
			$(HOST_DIR)/bin/python3 \
			$(HOST_KCONFIGLIB_INSTALL_DIR) $$f \
			>> $(HOST_DIR)/bin/$$f; \
		chmod +x $(HOST_DIR)/bin/$$f; \
	done
endef

$(eval $(host-generic-package))
