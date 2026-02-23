#!/bin/bash

# external envs set by buildroot:
#BINARIES_DIR=<...>/output/images
#HOST_DIR=<...>/output/host
#SCRIPT_DIR=<...>/buildroot-external-inmys
SCRIPT_DIR="$(dirname $0)"

set -e

# syslinux builded by buildroot can be (will be) broken - use system syslinux
if ! which syslinux; then
	echo "WARNING: syslinux tool not found, skip gen usbdisk.img (apt install syslinux?)"
	exit 0
fi

# copy syslinux.cfg to images/
cp ${SCRIPT_DIR}/boot/syslinux.cfg ${BINARIES_DIR}/syslinux.cfg
cp -r ${SCRIPT_DIR}/boot/EFI ${BINARIES_DIR}/
cp -r ${SCRIPT_DIR}/boot/loader ${BINARIES_DIR}/
# create ${BINARIES_DIR}/usbdisk.img
support/scripts/genimage.sh -c "${SCRIPT_DIR}/boot/genimage.cfg"
# install syslinux/mbr.bin to usbdisk.img
dd if=${BINARIES_DIR}/syslinux/mbr.bin of=${BINARIES_DIR}/usbdisk.img conv=notrunc
# install syslinux (512 is offset of first partition in genimage.cfg) in usbdisk.img
syslinux --offset 512 ${BINARIES_DIR}/usbdisk.img
