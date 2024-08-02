################################################################################
#
# qt6multimedia
#
################################################################################

QT6MULTIMEDIA_VERSION = $(QT6_VERSION)
QT6MULTIMEDIA_SITE = $(QT6_SITE)
QT6MULTIMEDIA_SOURCE = qtmultimedia-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6MULTIMEDIA_VERSION).tar.xz
QT6MULTIMEDIA_INSTALL_STAGING = YES
QT6MULTIMEDIA_SUPPORTS_IN_SOURCE_BUILD = NO

QT6MULTIMEDIA_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6MULTIMEDIA_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6MULTIMEDIA_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DFEATURE_alsa=ON

QT6MULTIMEDIA_DEPENDENCIES = \
	qt6base \
	qt6declarative\
	qt6shadertools

$(eval $(cmake-package))
