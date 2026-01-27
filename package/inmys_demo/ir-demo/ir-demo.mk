################################################################################
#
# ir-demo
#
################################################################################

IR_DEMO_VERSION = 1.0

IR_DEMO_SITE = $(patsubst %/,%,$(IR_DEMO_PKGDIR))
IR_DEMO_SITE_METHOD = local
IR_DEMO_SOURCE =

define IR_DEMO_BUILD_CMDS
	$(TARGET_CXX) $(TARGET_CXXFLAGS) $(TARGET_LDFLAGS) \
		-o $(@D)/ir-demo $(@D)/ir-demo.cpp
endef

define IR_DEMO_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/ir-demo $(TARGET_DIR)/usr/bin/ir-demo
endef

$(eval $(generic-package))
