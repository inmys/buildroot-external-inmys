#!/bin/sh
set -eu
"$HOST_DIR/bin/mkimage" -A arm -T script -C none -n 'NMS-SDM-AM335x SD boot' \
    -d "$BR2_EXTERNAL_INMYS_PATH/board/boot.cmd" "$BINARIES_DIR/boot.scr"
"$BR2_EXTERNAL_INMYS_PATH/swu/make-swu.sh"
install -m 0755 "$BR2_EXTERNAL_INMYS_PATH/overlay/usr/sbin/nms-install-nand" "$BINARIES_DIR/install-nand.sh"
(cd "$BINARIES_DIR" && sha256sum MLO u-boot.img rootfs.ubi install-nand.sh > nand-SHA256SUMS)
tar -cf "$BINARIES_DIR/nand-install.tar" -C "$BINARIES_DIR" MLO u-boot.img rootfs.ubi install-nand.sh nand-SHA256SUMS
support/scripts/genimage.sh "$1" -c "$BR2_EXTERNAL_INMYS_PATH/genimage.cfg"
