#############################################################
#
# libwt
#
#############################################################

LIBWT_VERSION = 4.12.2
LIBWT_SITE = $(call github,emweb,wt,$(LIBWT_VERSION))
LIBWT_SOURCE = $(LIBWT_VERSION).tar.gz
LIBWT_DEPENDENCIES = boost
LIBWT_INSTALL_STAGING = YES

LIBWT_CONF_OPTS += -DBUILD_EXAMPLES=OFF -DENABLE_LIBWTTEST=OFF -DENABLE_LIBWTDBO=OFF
#-DINSTALL_RESOURCES=OFF

$(eval $(cmake-package))
