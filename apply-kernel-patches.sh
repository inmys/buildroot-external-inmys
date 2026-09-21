#!/bin/bash
cd kernel
patch -p1 < ../buildroot-external-inmys/patches/linux/0001-add-imx-477.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0002-imx219-radxa-driver.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0003-ov5647-radxa-driver.patch
# LVDS through the SN65DSI84 bridge (DSI0 -> LVDS0/LVDS1)
patch -p1 < ../buildroot-external-inmys/patches/linux/0010-drm-bridge-ti-sn65dsi83-Disable-video-burst-mode-for.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0011-drm-bridge-ti-sn65dsi83-Fix-premature-PLL-locking.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0012-drm-bridge-ti-sn65dsi83-Support-LVDS-Channel-B-on-SN.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0013-drm-bridge-ti-sn65dsi83-Halve-horizontal-blanking-in.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0014-drm-rockchip-dw-mipi-dsi2-fix-PHY_SYS_RATIO-off-by-a.patch
cd ..
