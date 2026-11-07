#!/bin/sh
#
# Image post-processing for NMS-SM-K1892VM21YA:
#   - collect the boot files (kernel, carrier DTB, extlinux.conf)
#   - build a bootable SD card image (FAT boot + ext4 rootfs)
#   - create the QSPI flashing helper script
#
# Copyright (C) 2026 INMYS

set -eu

BOARD_DIR="$(dirname "$0")"
BINARIES_DIR="${BINARIES_DIR:-$PWD}"
BUILD_DIR="${BUILD_DIR:-$PWD}"
HOST_DIR="${HOST_DIR:-/usr}"

CARRIER=$(sed -n 's/^BR2_NMS_SM_K1892_CARRIER="\(.*\)"$/\1/p' "${BR2_CONFIG}")
CARRIER="${CARRIER:-nms-sm-evm}"

# ------------------------------------------------------------------
# Boot files for the SD card / extlinux
# ------------------------------------------------------------------
mkdir -p "$BINARIES_DIR/extlinux"

if [ ! -f "$BINARIES_DIR/Image" ]; then
	echo "post-image: WARNING: $BINARIES_DIR/Image is missing" >&2
fi

# Buildroot installs the kernel device trees with the directory name
# preserved (elvees/nms-*.dtb); extlinux and genimage want them flat.
for dtb in "$BINARIES_DIR"/elvees/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	cp "$dtb" "$BINARIES_DIR/"
done

# One label per carrier, so a single image boots on any of them; the
# carrier this image was built for is the default.
write_extlinux() {
	root=$1
	{
		echo "default $CARRIER"
		echo "menu title NMS-SM-K1892VM21YA boot options"
		echo "timeout 10"
		for dtb in "$BINARIES_DIR"/nms-*.dtb; do
			[ -f "$dtb" ] || continue
			name=$(basename "$dtb" .dtb)
			echo
			echo "label $name"
			echo "\tmenu label $name"
			echo "\tlinux /Image"
			echo "\tfdt /$name.dtb"
			echo "\tappend root=$root rootwait rw console=ttyS0,115200 earlycon"
		done
	} > "$BINARIES_DIR/extlinux/extlinux.conf"
}

# genimage does not expand globs, so the DTB list is spliced into the
# template here; a new carrier then needs no change to genimage.cfg.
GENIMAGE_TMP="$BUILD_DIR/genimage.tmp"

DTB_LIST=
for dtb in "$BINARIES_DIR"/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	DTB_LIST="$DTB_LIST\t\t\t\"$(basename "$dtb")\",\n"
done

# build_image <name> <root=> <mbr-signature, 4 bytes as printf escapes>
#
# Two images are produced.  They differ only in the root= argument and in
# the MBR disk signature, so root=PARTUUID resolves to the right device
# even when the SD card and the eMMC are both present: the SD image points
# at "NMS1"-02, the eMMC image at "NMS2"-02.
build_image() {
	out=$1
	root=$2
	sig=$3

	write_extlinux "$root"

	sed -e "s|^\t\t\t\"nms-\*\.dtb\",$|$DTB_LIST|" \
	    -e "s|^image boot\.vfat {|image $out.vfat {|" \
	    -e "s|^image sdcard\.img {|image $out {|" \
	    -e "s|\"boot\.vfat\"|\"$out.vfat\"|g" \
	    "$BOARD_DIR/genimage.cfg" > "$BUILD_DIR/genimage-$out.cfg"

	rm -rf "$GENIMAGE_TMP"
	mkdir -p "$GENIMAGE_TMP"

	"$HOST_DIR/bin/genimage" \
		--rootpath "$BINARIES_DIR" \
		--tmppath "$GENIMAGE_TMP" \
		--inputpath "$BINARIES_DIR" \
		--outputpath "$BINARIES_DIR" \
		--config "$BUILD_DIR/genimage-$out.cfg"

	rm -rf "$GENIMAGE_TMP"

	# Force the MBR disk signature (offset 0x1b8) so that the root
	# PARTUUID is stable no matter where the image is copied to.
	printf "$sig" | dd of="$BINARIES_DIR/$out" \
		bs=1 seek=440 conv=notrunc status=none
}

# The MBR disk signature is a little-endian u32, so the bytes "NMS1" /
# "NMS2" written at offset 0x1b8 come out as 0x31534d4e / 0x32534d4e --
# that is what "mmc part" reports and what root=PARTUUID must match.
# (dash's printf has no \x, hence the octal escapes.)
build_image sdcard.img "PARTUUID=31534d4e-02" '\116\115\123\061'
build_image emmc.img   "PARTUUID=32534d4e-02" '\116\115\123\062'

# leave the SD variant in place as the reference extlinux.conf
write_extlinux "PARTUUID=31534d4e-02"

# ------------------------------------------------------------------
# QSPI boot flash
#
# The SBL build produces one *.tl-image per carrier.  It is written to
# the QSPI0 flash with the ELVEES mcom03-flash utility over the BootROM
# UART (SER1_DBG) or from a running U-Boot/Linux system.
# ------------------------------------------------------------------
TL_IMAGES=$(ls "$BINARIES_DIR"/tl-sbimg/*.tl-image 2>/dev/null || true)

if [ -n "$TL_IMAGES" ]; then
	cat > "$BINARIES_DIR/flash-qspi.sh" <<EOF
#!/bin/sh
#
# Flash the QSPI0 boot flash of the NMS-SM-K1892VM21YA module.
#
#   1. Set BOOTSEL.2/1/0 to 0/1/1 (BootROM RISC0 / UART0)
#   2. Connect the debug UART (SER1_DBG) to /dev/ttyUSB0
#   3. Press RESET
#   4. Run this script
#
# Every carrier has its own *.tl-image (they only differ in the U-Boot
# device tree embedded into the SBL).  The image for the configured
# carrier is used by default; pass another carrier name or a path as the
# second argument:
#
#   flash-qspi.sh /dev/ttyUSB0 nms-smarc-tester
#   flash-qspi.sh /dev/ttyUSB0 path/to/other.tl-image
#
set -e
if ! command -v mcom03-flash >/dev/null 2>&1; then
	echo "flash-qspi.sh: mcom03-flash is not installed" >&2
	echo "  pip install --break-system-packages git+https://github.com/elvees/mcom03-flash-tools" >&2
	exit 1
fi
PORT=\${1:-/dev/ttyUSB0}
IMAGE=\${2:-}
dir=\$(dirname "\$0")
case "\$IMAGE" in
''|*/*) ;;
*) IMAGE="\$dir/tl-sbimg/nms-sm-k1892vm21ya-mcom03r-\$IMAGE.tl-image" ;;
esac
if [ -z "\$IMAGE" ]; then
	# Look next to this script and in the tl-sbimg/ subdirectory, so the
	# script works both in output/images and in /usr/lib/mcom03-boot.
	for candidate in "\$dir"/*$CARRIER.tl-image "\$dir"/tl-sbimg/*$CARRIER.tl-image; do
		[ -f "\$candidate" ] && IMAGE=\$candidate && break
	done
fi
if [ ! -f "\$IMAGE" ]; then
	echo "flash-qspi.sh: no such image: \$IMAGE" >&2
	echo "available:" >&2
	ls "\$dir"/tl-sbimg/*.tl-image 2>/dev/null | sed 's|.*/nms-sm-k1892vm21ya-mcom03r-|  |; s|\.tl-image\$||' >&2
	exit 1
fi
exec mcom03-flash --port "\$PORT" flash-tl-image qspi0 "\$IMAGE"
EOF
	chmod +x "$BINARIES_DIR/flash-qspi.sh"
	echo "post-image: QSPI images:"
	for img in $TL_IMAGES; do
		echo "  $img"
	done
else
	echo "post-image: WARNING: no SBL *.tl-image found" >&2
fi

# ------------------------------------------------------------------
# Flat QSPI0 image for an external programmer
#
# The QSPI0 boot flash is wired to the SoC only (it is not routed to the
# SMARC connector), so it can be written either by mcom03-flash over the
# BootROM UART or with an external programmer clipped onto the flash.
# The latter needs a plain binary of the whole 16 MiB chip; build one for
# every carrier, so that a single build produces the complete set of
# release artifacts (the SBL package builds all carriers anyway, and the
# carriers only differ in the U-Boot device tree embedded into the SBL).
# ------------------------------------------------------------------
if [ -n "$TL_IMAGES" ]; then
	for tl in $TL_IMAGES; do
		name=${tl##*/nms-sm-k1892vm21ya-mcom03r-}
		name=${name%.tl-image}
		"$BOARD_DIR/../../support/make-qspi-image.sh" \
			-e "$BINARIES_DIR/uboot-env.bin" \
			-o "$BINARIES_DIR/qspi-full-$name.bin" \
			"$tl"
	done
else
	echo "post-image: WARNING: no tl-image, no flat QSPI image" >&2
fi

echo "post-image: images in $BINARIES_DIR:"
ls -l "$BINARIES_DIR"/*.img "$BINARIES_DIR"/*.tl-image \
	"$BINARIES_DIR"/qspi-full-*.bin 2>/dev/null || true
