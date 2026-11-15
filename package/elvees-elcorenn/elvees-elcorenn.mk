ELVEES_ELCORENN_VERSION = 2025.09
ELVEES_ELCORENN_SOURCE = aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
ELVEES_ELCORENN_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66/images
ELVEES_ELCORENN_DL_SUBDIR = elvees-mcom03-tools
ELVEES_ELCORENN_DL_OPTS = --no-check-certificate
ELVEES_ELCORENN_LICENSE = Proprietary (ELVEES), BSL-1.0 (Boost)
ELVEES_ELCORENN_REDISTRIBUTE = NO
ELVEES_ELCORENN_INSTALL_STAGING = YES
ELVEES_ELCORENN_DEPENDENCIES = elvees-elcore50
ELVEES_ELCORENN_SDK_ROOT = aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot
ELVEES_ELCORENN_LIBS = libelcorenn.so libelcorecl.so libelcore.so \
	libboost_atomic.so.1.82.0 libboost_chrono.so.1.82.0 \
	libboost_filesystem.so.1.82.0 libboost_log.so.1.82.0 \
	libboost_regex.so.1.82.0 libboost_thread.so.1.82.0

define ELVEES_ELCORENN_EXTRACT_CMDS
	tar -xf $(ELVEES_ELCORENN_DL_DIR)/$(ELVEES_ELCORENN_SOURCE) \
		--strip-components=3 -C $(@D) \
		$(ELVEES_ELCORENN_SDK_ROOT)/usr/include/elcorenn \
		$(addprefix $(ELVEES_ELCORENN_SDK_ROOT)/usr/lib/,$(ELVEES_ELCORENN_LIBS)) \
		$(ELVEES_ELCORENN_SDK_ROOT)/usr/share/elcorenn/elcorenn_dsp_backend_512mb
endef

define ELVEES_ELCORENN_INSTALL_STAGING_CMDS
	mkdir -p $(STAGING_DIR)/usr/lib/elvees-nn $(STAGING_DIR)/usr/include
	cp -a $(@D)/usr/include/elcorenn $(STAGING_DIR)/usr/include/
	$(INSTALL) -m 0755 $(addprefix $(@D)/usr/lib/,$(ELVEES_ELCORENN_LIBS)) $(STAGING_DIR)/usr/lib/elvees-nn/
endef

define ELVEES_ELCORENN_INSTALL_TARGET_CMDS
	mkdir -p $(TARGET_DIR)/usr/lib/elvees-nn
	$(INSTALL) -m 0755 $(addprefix $(@D)/usr/lib/,$(ELVEES_ELCORENN_LIBS)) $(TARGET_DIR)/usr/lib/elvees-nn/
	$(INSTALL) -D -m 0755 $(@D)/usr/share/elcorenn/elcorenn_dsp_backend_512mb \
		$(TARGET_DIR)/usr/share/elcorenn/elcorenn_dsp_backend_512mb
endef

$(eval $(generic-package))
