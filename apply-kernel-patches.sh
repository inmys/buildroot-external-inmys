#!/bin/bash
cd kernel
patch -p1 < ../buildroot-external-inmys/patches/linux/0001-add-imx-477.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0002-imx219-radxa-driver.patch
patch -p1 < ../buildroot-external-inmys/patches/linux/0003-ov5647-radxa-driver.patch
cd ..
