#!/bin/sh
#
# Root filesystem post-build script for NMS-SM-K1892VM21YA.
#
# Copyright (C) 2026 INMYS

set -eu

TARGET_DIR="$1"
BOARD_DIR="$(dirname "$0")"
CARRIER=$(sed -n 's/^BR2_NMS_SM_K1892_CARRIER="\(.*\)"$/\1/p' "${BR2_CONFIG}")
CARRIER="${CARRIER:-nms-sm-evm}"

# Carrier specific rootfs overlay (optional)
if [ -d "$BOARD_DIR/rootfs-overlay-$CARRIER" ]; then
	cp -a "$BOARD_DIR/rootfs-overlay-$CARRIER/." "$TARGET_DIR/"
fi

# Write the extlinux configuration used by the SD card image and by
# "bootm" from the U-Boot prompt.
mkdir -p "$TARGET_DIR/boot/extlinux"
cat > "$TARGET_DIR/boot/extlinux/extlinux.conf" <<EOF
label linux
	linux /boot/Image
	fdt /boot/$CARRIER.dtb
	append root=/dev/mmcblk0p2 rootwait rw console=ttyS0,115200 earlycon
EOF

# The kernel device trees are installed by Buildroot with the directory
# name preserved (elvees/nms-sm-evm.dtb); extlinux wants them flat.
#
# All of them are copied, not just the configured carrier, so that /boot
# matches the FAT partition of the SD image and "bootm" works for any
# carrier.  The stale copies are removed first: output/target survives
# between builds, so a carrier that was dropped from the list would
# otherwise linger in /boot forever.
rm -f "$TARGET_DIR"/boot/nms-*.dtb
for dtb in "$TARGET_DIR"/boot/elvees/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	cp "$dtb" "$TARGET_DIR/boot/"
done

# fstab
#
# Buildroot's default fstab does not mount sysfs and this image has no
# S10mdev (the system uses devtmpfs), so /sys stayed unmounted and
# everything that reads it -- /sys/class/net, /sys/firmware/devicetree,
# most of "ip" -- silently reported nothing.
cat > "$TARGET_DIR/etc/fstab" <<EOF
# device	mount point	type		options					dump	pass
/dev/root	/		ext4		defaults				0	1
LABEL=boot	/boot		vfat	defaults				0	0
proc		/proc		proc		defaults				0	0
sysfs		/sys		sysfs		defaults				0	0
devtmpfs	/dev		devtmpfs	defaults				0	0
devpts		/dev/pts	devpts		defaults,gid=5,mode=620,ptmxmode=0666	0	0
tmpfs		/dev/shm	tmpfs		mode=0777				0	0
tmpfs		/run		tmpfs		mode=0755				0	0
tmpfs		/tmp		tmpfs		mode=1777				0	0
EOF

# Allow root login over the serial console
if [ -f "$TARGET_DIR/etc/inittab" ] && ! grep -q '^ttyS0' "$TARGET_DIR/etc/inittab"; then
	echo 'ttyS0::respawn:/sbin/getty -L ttyS0 115200 vt100' >> "$TARGET_DIR/etc/inittab"
fi
