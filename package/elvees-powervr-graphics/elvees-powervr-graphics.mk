ELVEES_POWERVR_GRAPHICS_VERSION = 2025.09
ELVEES_POWERVR_GRAPHICS_SOURCE = mcom03r-defconfig-src.tar.gz
ELVEES_POWERVR_GRAPHICS_SITE = https://dist.elvees.com/mcom03r/buildroot/2025.09/linux66
ELVEES_POWERVR_GRAPHICS_DL_SUBDIR = elvees-sdk-sources
ELVEES_POWERVR_GRAPHICS_DL_OPTS = --no-check-certificate
ELVEES_POWERVR_GRAPHICS_LICENSE = MIT, Proprietary (PowerVR GLES)
ELVEES_POWERVR_GRAPHICS_LICENSE_FILES = docs/license.html
ELVEES_POWERVR_GRAPHICS_REDISTRIBUTE = NO
ELVEES_POWERVR_GRAPHICS_INSTALL_STAGING = YES
ELVEES_POWERVR_GRAPHICS_PROVIDES = libegl libgles libgbm
ELVEES_POWERVR_GRAPHICS_DEPENDENCIES = host-elvees-python2 host-pkgconf \
	host-bison host-flex elvees-powervr-opencl mesa3d-headers libdrm expat wayland
ELVEES_POWERVR_GRAPHICS_AUTORECONF = YES
ELVEES_POWERVR_GRAPHICS_CONF_ENV = \
	PYTHON2=$(HOST_DIR)/opt/elvees-python2/bin/python2.7 \
	WAYLAND_SCANNER=$(HOST_DIR)/bin/wayland-scanner \
	CPPFLAGS="$(TARGET_CPPFLAGS) -I$(@D)/vendor/usr/include" \
	LDFLAGS="$(TARGET_LDFLAGS) -L$(@D)/vendor/usr/lib" \
	CFLAGS="$(TARGET_CFLAGS) -std=gnu11 -fcommon" CXXFLAGS="$(TARGET_CXXFLAGS) -fcommon"
ELVEES_POWERVR_GRAPHICS_CONF_OPTS = --with-dri-drivers=pvr \
	--with-gallium-drivers= --with-vulkan-drivers= --enable-dri --disable-dri3 \
	--disable-glx --enable-shared-glapi --enable-opengl --disable-gles1 --disable-gles2 \
	--enable-egl --enable-gbm --with-egl-platforms=wayland,drm,surfaceless \
	--disable-gallium-llvm --disable-libunwind --disable-opencl --disable-osmesa \
	--disable-va --disable-vdpau --disable-xvmc --disable-xa --disable-nine
ELVEES_POWERVR_GRAPHICS_VENDOR_ROOT = aarch64-buildroot-linux-gnu_sdk-buildroot/aarch64-buildroot-linux-gnu/sysroot
ELVEES_POWERVR_GRAPHICS_VENDOR_LIBS = libGLESv2.so libpvr_dri_support.so libglslcompiler.so

define ELVEES_POWERVR_GRAPHICS_EXTRACT_CMDS
	$(TAR) -xOf $(ELVEES_POWERVR_GRAPHICS_DL_DIR)/$(ELVEES_POWERVR_GRAPHICS_SOURCE) --occurrence=1 \
		mcom03r-defconfig-src/buildroot/dl/mesa3d/mesa3d-mcom03-17.0.y-br1.tar.gz > $(@D)/mesa.tar.gz
	$(TAR) -xf $(@D)/mesa.tar.gz --strip-components=1 -C $(@D)
	rm $(@D)/mesa.tar.gz
endef

define ELVEES_POWERVR_GRAPHICS_EXTRACT_VENDOR
	mkdir -p $(@D)/vendor
	$(TAR) -xf $(ELVEES_POWERVR_OPENCL_DL_DIR)/$(ELVEES_POWERVR_OPENCL_SOURCE) \
		--strip-components=3 -C $(@D)/vendor \
		$(addprefix $(ELVEES_POWERVR_GRAPHICS_VENDOR_ROOT)/usr/lib/,$(ELVEES_POWERVR_GRAPHICS_VENDOR_LIBS)) \
		$(ELVEES_POWERVR_GRAPHICS_VENDOR_ROOT)/usr/include/powervr
endef

ELVEES_POWERVR_GRAPHICS_PRE_CONFIGURE_HOOKS += ELVEES_POWERVR_GRAPHICS_EXTRACT_VENDOR

# Avoid legacy libtool's install-time relinking against the host /usr/lib.
define ELVEES_POWERVR_GRAPHICS_INSTALL_LIBS
	mkdir -p $(1)/usr/lib/dri
	cp -a $(@D)/lib/libEGL.so* $(@D)/lib/libgbm.so* $(@D)/lib/libglapi.so* $(1)/usr/lib/
	cp -a $(@D)/lib/pvr_dri.so $(1)/usr/lib/dri/
	cp -a $(@D)/vendor/usr/lib/* $(1)/usr/lib/
	ln -sf libGLESv2.so $(1)/usr/lib/libGLESv2.so.2
endef

define ELVEES_POWERVR_GRAPHICS_INSTALL_STAGING_CMDS
	$(call ELVEES_POWERVR_GRAPHICS_INSTALL_LIBS,$(STAGING_DIR))
	cp -dpfr $(MESA3D_HEADERS_DIR)/include/GLES3 $(STAGING_DIR)/usr/include/
	$(INSTALL) -m 0644 $(@D)/src/gbm/main/gbm.h $(STAGING_DIR)/usr/include/gbm.h
	$(INSTALL) -D -m 0644 $(@D)/src/egl/main/egl.pc $(STAGING_DIR)/usr/lib/pkgconfig/egl.pc
	$(INSTALL) -D -m 0644 $(@D)/src/gbm/main/gbm.pc $(STAGING_DIR)/usr/lib/pkgconfig/gbm.pc
	$(INSTALL) -D -m 0644 $(ELVEES_POWERVR_GRAPHICS_PKGDIR)/glesv2.pc $(STAGING_DIR)/usr/lib/pkgconfig/glesv2.pc
endef

define ELVEES_POWERVR_GRAPHICS_INSTALL_TARGET_CMDS
	$(call ELVEES_POWERVR_GRAPHICS_INSTALL_LIBS,$(TARGET_DIR))
endef

$(eval $(autotools-package))
