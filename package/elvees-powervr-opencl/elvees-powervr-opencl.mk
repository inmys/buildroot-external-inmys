ELVEES_POWERVR_OPENCL_VERSION = 2025.09
ELVEES_POWERVR_OPENCL_SOURCE = aarch64-buildroot-linux-gnu_sdk-buildroot.tar.gz
ELVEES_POWERVR_OPENCL_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66/images
ELVEES_POWERVR_OPENCL_DL_SUBDIR = elvees-mcom03-tools
ELVEES_POWERVR_OPENCL_DL_OPTS = --no-check-certificate
ELVEES_POWERVR_OPENCL_LICENSE = Proprietary (Imagination/ELVEES)
ELVEES_POWERVR_OPENCL_REDISTRIBUTE = NO
ELVEES_POWERVR_OPENCL_PROVIDES = libopencl
ELVEES_POWERVR_OPENCL_INSTALL_STAGING = YES
ELVEES_POWERVR_OPENCL_DEPENDENCIES = elvees-rogue-modules libdrm opencl-headers
ELVEES_POWERVR_OPENCL_SDK_ROOT = aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot
ELVEES_POWERVR_OPENCL_LIBS = libPVROCL.so libusc.so libsrv_um.so libufwriter.so

define ELVEES_POWERVR_OPENCL_EXTRACT_CMDS
	$(TAR) -xf $(ELVEES_POWERVR_OPENCL_DL_DIR)/$(ELVEES_POWERVR_OPENCL_SOURCE) \
		--strip-components=3 -C $(@D) \
		$(addprefix $(ELVEES_POWERVR_OPENCL_SDK_ROOT)/usr/lib/,$(ELVEES_POWERVR_OPENCL_LIBS)) \
		$(ELVEES_POWERVR_OPENCL_SDK_ROOT)/usr/lib/firmware/rgx.fw.22.68.54.30
endef

define ELVEES_POWERVR_OPENCL_INSTALL_STAGING_CMDS
	$(INSTALL) -m 0755 $(addprefix $(@D)/usr/lib/,$(ELVEES_POWERVR_OPENCL_LIBS)) $(STAGING_DIR)/usr/lib/
	ln -sf libPVROCL.so $(STAGING_DIR)/usr/lib/libOpenCL.so
endef

define ELVEES_POWERVR_OPENCL_INSTALL_TARGET_CMDS
	$(INSTALL) -m 0755 $(addprefix $(@D)/usr/lib/,$(ELVEES_POWERVR_OPENCL_LIBS)) $(TARGET_DIR)/usr/lib/
	ln -sf libPVROCL.so $(TARGET_DIR)/usr/lib/libOpenCL.so
	$(INSTALL) -D -m 0644 $(@D)/usr/lib/firmware/rgx.fw.22.68.54.30 $(TARGET_DIR)/lib/firmware/rgx.fw.22.68.54.30
endef



$(eval $(generic-package))
