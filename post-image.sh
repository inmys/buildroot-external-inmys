#!/bin/bash

# external envs set by buildroot:
#BINARIES_DIR=<...>/output/images
#HOST_DIR=<...>/output/host

set -e

#SCRIPT_DIR=<...>/buildroot-external-inmys
SCRIPT_DIR="$(dirname $0)"

# build emmc.img (sdcard.img) 
# offset: 446 -> MBR partition table(64 bytes) (3 partitions: rootfsA(1GB),rootfsB(1GB),/opt(12GB)), (only part1 in .img)
# offset: 32K  -> u-boot (~10MB)
# offset: 16MB -> rootfs (~80MB)
dd if=/dev/zero bs=512 count=32768 of=${BINARIES_DIR}/16MB.dat
${HOST_DIR}/bin/genpart -t 0x83 -b 32768 -s 2097152 -c | dd of=${BINARIES_DIR}/16MB.dat bs=1 seek=$((446+0)) conv=notrunc
${HOST_DIR}/bin/genpart -t 0x83 -b $((32768+2097152)) -s 2097152 | dd of=${BINARIES_DIR}/16MB.dat bs=1 seek=$((446+16)) conv=notrunc
${HOST_DIR}/bin/genpart -t 0x83 -b $((32768+2097152+2097152)) -s 25165824 | dd of=${BINARIES_DIR}/16MB.dat bs=1 seek=$((446+32)) conv=notrunc
/bin/echo -n -e '\x55\xaa'  | dd of=${BINARIES_DIR}/16MB.dat bs=1 seek=510 conv=notrunc
cat ${BINARIES_DIR}/u-boot-rockchip.bin | dd of=${BINARIES_DIR}/16MB.dat bs=512 seek=64 conv=notrunc
cat ${BINARIES_DIR}/16MB.dat ${BINARIES_DIR}/rootfs.ext2 > ${BINARIES_DIR}/emmc.img
rm ${BINARIES_DIR}/16MB.dat

