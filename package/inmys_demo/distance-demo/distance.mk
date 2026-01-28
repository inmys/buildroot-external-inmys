################################################################################
#
# ir-demo
#
################################################################################

DISTANCE_DEMO_VERSION = 1.0

DISTANCE_DEMO_SITE = $(patsubst %/,%,$(DISTANCE_DEMO_PKGDIR))
DISTANCE_DEMO_SITE_METHOD = local
DISTANCE_DEMO_SOURCE =

define DISTANCE_DEMO_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/distance-demo $(@D)/distance-demo.cpp
endef

define DISTANCE_DEMO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/distance-demo $(TARGET_DIR)/usr/bin/distance-demo
endef

$(eval $(generic-package))
