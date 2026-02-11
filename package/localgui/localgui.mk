#############################################################
#
# localgui
#
#############################################################

LOCALGUI_VERSION = 1.0
#LOCALGUI_SITE = $(call github,inmys,webgen,$(LOCALGUI_VERSION))
LOCALGUI_SITE = $(BR2_EXTERNAL_INMYS_PATH)/package/localgui/localgui_src
LOCALGUI_SITE_METHOD = local

LOCALGUI_DEPENDENCIES = libwt

#LOCALGUI_CONF_OPTS += -DWITH_HTTPD=OFF

define LOCALGUI_INSTALL_TARGET_CMDS
	$(INSTALL) -D -m 0755 $(@D)/localgui $(TARGET_DIR)/root/localgui
	$(INSTALL) -D -m 0644 $(@D)/css/style.css $(TARGET_DIR)/home/http/css/style.css
	$(INSTALL) -D -m 0644 $(@D)/webgui_ru.xml $(TARGET_DIR)/home/http/webgui_ru.xml
	$(INSTALL) -D -m 0644 $(@D)/webgui_en.xml $(TARGET_DIR)/home/http/webgui_en.xml
endef

$(eval $(cmake-package))
