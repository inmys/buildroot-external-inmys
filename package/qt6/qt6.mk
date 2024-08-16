################################################################################
#
# qt6
#
################################################################################

QT6_VERSION_MAJOR = 6.7
QT6_VERSION = $(QT6_VERSION_MAJOR).2
QT6_SOURCE_TARBALL_PREFIX = everywhere-src
QT6_SITE = https://download.qt.io/archive/qt/$(QT6_VERSION_MAJOR)/$(QT6_VERSION)/submodules
#QT6_SITE = https://ftp.jaist.ac.jp/pub/qtproject/archive/qt/$(QT6_VERSION_MAJOR)/$(QT6_VERSION)/submodules

include $(sort $(wildcard $(BR2_EXTERNAL_INMYS_PATH)/package/qt6/*/*.mk))
