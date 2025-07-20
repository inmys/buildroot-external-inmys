#!/bin/bash

# external envs set by buildroot:
#BINARIES_DIR=<...>/output/images
#HOST_DIR=<...>/output/host

set -e

#SCRIPT_DIR=<...>/buildroot-external-inmys
SCRIPT_DIR="$(dirname $0)"

# offset: 446 -> partition table(64 bytes) (1 partition ext2, size 1Gb)
# offset: 1K  -> u-boot (~200K)
# offset: 1MB -> rootfs (~40MB)
dd if=/dev/zero bs=512 count=2048 of=${BINARIES_DIR}/1MB.dat
${HOST_DIR}/bin/genpart -t 0x83 -c -b 2048 -s 2048000 | dd of=${BINARIES_DIR}/1MB.dat bs=1 seek=446 conv=notrunc
/bin/echo -n -e '\x55\xaa'  | dd of=${BINARIES_DIR}/1MB.dat bs=1 seek=510 conv=notrunc
cat ${BINARIES_DIR}/u-boot.imx | dd of=${BINARIES_DIR}/1MB.dat bs=512 seek=2 conv=notrunc
cat ${BINARIES_DIR}/1MB.dat ${BINARIES_DIR}/rootfs.ext2 > ${BINARIES_DIR}/sdcard.img

# build update.swu
make -C ${SCRIPT_DIR}/swu
exit $?
