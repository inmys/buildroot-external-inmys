################################################################################
#
# qt6wayland
#
################################################################################

QT6WAYLAND_VERSION = $(QT6_VERSION)
QT6WAYLAND_SITE = $(QT6_SITE)
QT6WAYLAND_SOURCE = qtwayland-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6WAYLAND_VERSION).tar.xz
QT6WAYLAND_INSTALL_STAGING = YES
QT6WAYLAND_SUPPORTS_IN_SOURCE_BUILD = NO

QT6WAYLAND_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6WAYLAND_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6WAYLAND_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DFEATURE_wayland_server=OFF

QT6WAYLAND_DEPENDENCIES = \
	host-pkgconf \
	qt6base \
	host-qt6wayland


HOST_QT6WAYLAND_DEPENDENCIES = \
        host-qt6base

HOST_QT6WAYLAND_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))

