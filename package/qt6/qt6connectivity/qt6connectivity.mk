################################################################################
#
# QT6CONNECTIVITY
#
################################################################################

QT6CONNECTIVITY_VERSION = $(QT6_VERSION)
QT6CONNECTIVITY_SITE = $(QT6_SITE)
QT6CONNECTIVITY_SOURCE = qtconnectivity-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6CONNECTIVITY_VERSION).tar.xz
QT6CONNECTIVITY_INSTALL_STAGING = YES
QT6CONNECTIVITY_SUPPORTS_IN_SOURCE_BUILD = NO

QT6CONNECTIVITY_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6CONNECTIVITY_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6CONNECTIVITY_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR)


QT6CONNECTIVITY_DEPENDENCIES = \
	qt6base \



HOST_QT6CONNECTIVITY_DEPENDENCIES = \
        host-qt6base

HOST_QT6CONNECTIVITY_CONF_OPTS = \
        -DQT_HOST_PATH=$(HOST_DIR) \
        -DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))
