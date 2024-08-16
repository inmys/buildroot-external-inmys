################################################################################
#
# QT6POSITIONING
#
################################################################################

QT6POSITIONING_VERSION = $(QT6_VERSION)
QT6POSITIONING_SITE = $(QT6_SITE)
QT6POSITIONING_SOURCE = qtpositioning-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6POSITIONING_VERSION).tar.xz
QT6POSITIONING_INSTALL_STAGING = YES
QT6POSITIONING_SUPPORTS_IN_SOURCE_BUILD = NO

QT6POSITIONING_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6POSITIONING_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6POSITIONING_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DFEATURE_wayland_server=OFF

QT6POSITIONING_DEPENDENCIES = \
	host-pkgconf \
	qt6base \
	host-QT6POSITIONING


HOST_QT6POSITIONING_DEPENDENCIES = \
        host-qt6base

HOST_QT6POSITIONING_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))

