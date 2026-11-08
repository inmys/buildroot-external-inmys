################################################################################
#
# tusb926x-flash-burner
#
################################################################################

TUSB926X_FLASH_BURNER_VERSION = b254bce6788e120cfab0d1df9e84792758606bb7
TUSB926X_FLASH_BURNER_SITE = $(call github,luxonis,tusb926x-flash-burner,$(TUSB926X_FLASH_BURNER_VERSION))
TUSB926X_FLASH_BURNER_LICENSE = MIT
TUSB926X_FLASH_BURNER_LICENSE_FILES = LICENSE
TUSB926X_FLASH_BURNER_DEPENDENCIES = libusb

# The upstream Makefile hard-codes /usr/include paths and asks pkg-config for
# the hidapi flags, neither of which works for a cross build, so the two
# translation units are compiled directly instead.  Local patch 0001 replaces
# hidapi with direct hidraw access, because Buildroot's hidapi package
# hard-depends on libudev and this image has no udev.
define TUSB926X_FLASH_BURNER_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_CPPFLAGS) \
		-I$(@D)/include -std=c++11 \
		$(@D)/src/TUSB9260_FlashBurner.cpp \
		$(@D)/src/Hex2Bin.cpp \
		$(TARGET_LDFLAGS) -lusb-1.0 \
		-o $(@D)/tusb926x-flash-burner
endef

define TUSB926X_FLASH_BURNER_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/tusb926x-flash-burner \
		$(TARGET_DIR)/usr/sbin/tusb926x-flash-burner
	$(INSTALL) -d -m 0755 $(TARGET_DIR)/usr/share/tusb926x
	$(INSTALL) -m 0644 $(@D)/flash-images/*.bin \
		$(TARGET_DIR)/usr/share/tusb926x/
endef

$(eval $(generic-package))
