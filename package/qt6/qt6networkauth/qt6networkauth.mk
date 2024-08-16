################################################################################
#
# QT6NETWORKAUTH
#
################################################################################

QT6NETWORKAUTH_VERSION = $(QT6_VERSION)
QT6NETWORKAUTH_SITE = $(QT6_SITE)
QT6NETWORKAUTH_SOURCE = qtnetworkauth-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6NETWORKAUTH_VERSION).tar.xz
QT6NETWORKAUTH_INSTALL_STAGING = YES
QT6NETWORKAUTH_SUPPORTS_IN_SOURCE_BUILD = NO

QT6NETWORKAUTH_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6NETWORKAUTH_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6NETWORKAUTH_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DFEATURE_wayland_server=OFF

QT6NETWORKAUTH_DEPENDENCIES = \
	host-pkgconf \
	qt6base \
	host-QT6NETWORKAUTH


HOST_QT6NETWORKAUTH_DEPENDENCIES = \
        host-qt6base

HOST_QT6NETWORKAUTH_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))

