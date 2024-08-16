################################################################################
#
# QT5COMPAT
#
################################################################################

QT5COMPAT_VERSION = $(QT6_VERSION)
QT5COMPAT_SITE = $(QT6_SITE)
QT5COMPAT_SOURCE = qt5compat-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT5COMPAT_VERSION).tar.xz
QT5COMPAT_INSTALL_STAGING = YES
QT5COMPAT_SUPPORTS_IN_SOURCE_BUILD = NO

QT5COMPAT_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT5COMPAT_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT5COMPAT_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR)


QT5COMPAT_DEPENDENCIES = \
	qt6base \



HOST_QT5COMPAT_DEPENDENCIES = \
        host-qt6base

HOST_QT5COMPAT_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))
