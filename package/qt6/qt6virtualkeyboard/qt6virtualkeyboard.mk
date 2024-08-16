################################################################################
#
# QT6VIRTUALKEYBOARD
#
################################################################################

QT6VIRTUALKEYBOARD_VERSION = $(QT6_VERSION)
QT6VIRTUALKEYBOARD_SITE = $(QT6_SITE)
QT6VIRTUALKEYBOARD_SOURCE = qttranslations-$(QT6_SOURCE_TARBALL_PREFIX)-$(QT6VIRTUALKEYBOARD_VERSION).tar.xz
QT6VIRTUALKEYBOARD_INSTALL_STAGING = YES
QT6VIRTUALKEYBOARD_SUPPORTS_IN_SOURCE_BUILD = NO

QT6VIRTUALKEYBOARD_LICENSE = \
	GPL-2.0+ or LGPL-3.0, \
	GPL-3.0 with exception (tools), \
	GFDL-1.3 (docs), \
	BSD-3-Clause

QT6VIRTUALKEYBOARD_LICENSE_FILES = \
	LICENSES/BSD-3-Clause.txt \
	LICENSES/GFDL-1.3-no-invariants-only.txt \
	LICENSES/GPL-2.0-only.txt \
	LICENSES/GPL-3.0-only.txt \
	LICENSES/LGPL-3.0-only.txt \
	LICENSES/Qt-GPL-exception-1.0.txt

QT6VIRTUALKEYBOARD_CONF_OPTS = \
	-DQT_HOST_PATH=$(HOST_DIR) \
	-DFEATURE_linguist=OFF \
	-DFEATURE_designer=OFF \
	-DFEATURE_pixeltool=OFF \
	-DFEATURE_assistant=OFF \
	-DFEATURE_distancefieldgenerator=OFF \
	-DFEATURE_qtattributionsscanner=OFF \
	-DFEATURE_commandlineparser=OFF \
	-DFEATURE_qdbus=OFF \
	-DFEATURE_qtdiag=OFF

QT6VIRTUALKEYBOARD_DEPENDENCIES = \
	host-pkgconf \
	host-ninja \
	qt6base

$(eval $(cmake-package))
