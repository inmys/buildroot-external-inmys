################################################################################
#
# qt6charts
#
################################################################################

QT6CHARTS_VERSION = $(QT6_VERSION)
QT6CHARTS_SITE = $(QT6_SITE)
QT6CHARTS_SOURCE = qtcharts-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6CHARTS_VERSION).tar.xz
QT6CHARTS_INSTALL_STAGING = YES
QT6CHARTS_SUPPORTS_IN_SOURCE_BUILD = NO

QT6CHARTS_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6CHARTS_LICENSE_FILES = \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt \
	LICENSES/LicenseRef-Qt-Commercial.txt

QT6CHARTS_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR)

QT6CHARTS_DEPENDENCIES = \
	qt6base \
	qt6declarative \
	host-qt6charts

HOST_QT6CHARTS_DEPENDENCIES = \
	host-qt6base \
	host-qt6declarative

HOST_QT6CHARTS_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DQT_FORCE_BUILD_TOOLS=ON

$(eval $(cmake-package))
$(eval $(host-cmake-package))
