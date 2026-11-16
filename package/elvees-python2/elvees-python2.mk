# Legacy Mesa generators require Python 2 and Mako, shipped in the pinned SDK.
HOST_ELVEES_PYTHON2_VERSION = 2025.09
HOST_ELVEES_PYTHON2_SOURCE = aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
HOST_ELVEES_PYTHON2_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66/images
HOST_ELVEES_PYTHON2_DL_SUBDIR = elvees-mcom03-tools
HOST_ELVEES_PYTHON2_DL_OPTS = --no-check-certificate
HOST_ELVEES_PYTHON2_LICENSE = Python-2.0, MIT, OpenSSL
HOST_ELVEES_PYTHON2_REDISTRIBUTE = NO

define HOST_ELVEES_PYTHON2_EXTRACT_CMDS
	$(TAR) -xf $(HOST_ELVEES_PYTHON2_DL_DIR)/$(HOST_ELVEES_PYTHON2_SOURCE) \
		--strip-components=1 -C $(@D) \
		aarch64-buildroot-linux-gnu_sdk-buildroot/bin/python2.7 \
		aarch64-buildroot-linux-gnu_sdk-buildroot/lib/libpython2.7.so.1.0 \
		aarch64-buildroot-linux-gnu_sdk-buildroot/lib/libssl.so.1.1 \
		aarch64-buildroot-linux-gnu_sdk-buildroot/lib/libcrypto.so.1.1 \
		aarch64-buildroot-linux-gnu_sdk-buildroot/lib/python2.7
endef

define HOST_ELVEES_PYTHON2_INSTALL_CMDS
	mkdir -p $(HOST_DIR)/opt/elvees-python2
	cp -a $(@D)/bin $(@D)/lib $(HOST_DIR)/opt/elvees-python2/
endef

$(eval $(host-generic-package))
