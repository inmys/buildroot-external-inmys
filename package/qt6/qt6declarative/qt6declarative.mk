################################################################################
#
# qt6declarative
#
################################################################################

QT6DECLARATIVE_VERSION = $(QT6_VERSION)
QT6DECLARATIVE_SITE = $(QT6_SITE)
QT6DECLARATIVE_SOURCE = qtdeclarative-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6DECLARATIVE_VERSION).tar.xz
QT6DECLARATIVE_INSTALL_STAGING = YES
QT6DECLARATIVE_SUPPORTS_IN_SOURCE_BUILD = NO

QT6DECLARATIVE_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6DECLARATIVE_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6DECLARATIVE_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DQT_FORCE_BUILD_TOOLS=ON

QT6DECLARATIVE_DEPENDENCIES = \
	host-pkgconf \
	qt6base \
	host-qt6declarative

HOST_QT6DECLARATIVE_DEPENDENCIES = \
        host-qt6base \
	host-qt6shadertools

HOST_QT6DECLARATIVE_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))
