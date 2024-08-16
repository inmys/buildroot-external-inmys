################################################################################
#
# QT6LOCATION
#
################################################################################

QT6LOCATION_VERSION = $(QT6_VERSION)
QT6LOCATION_SITE = $(QT6_SITE)
QT6LOCATION_SOURCE = qtlocation-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6LOCATION_VERSION).tar.xz
QT6LOCATION_INSTALL_STAGING = YES
QT6LOCATION_SUPPORTS_IN_SOURCE_BUILD = NO

QT6LOCATION_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6LOCATION_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6LOCATION_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR)


QT6LOCATION_DEPENDENCIES = \
	qt6base \



HOST_QT6LOCATION_DEPENDENCIES = \
        host-qt6base

HOST_QT6LOCATION_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))
