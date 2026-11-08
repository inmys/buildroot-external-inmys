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
#
# A/B: the boot partition is shared, so every slot gets its own copy of
# the kernel and of each device tree.  That keeps an update atomic -- the
# inactive slot is fully written before extlinux.conf is flipped, and the
# running slot's files are never touched.
rm -f "$BINARIES_DIR"/nms-*.dtb
for dtb in "$BINARIES_DIR"/elvees/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	name=$(basename "$dtb" .dtb)
	cp "$dtb" "$BINARIES_DIR/$name.A.dtb"
	cp "$dtb" "$BINARIES_DIR/$name.B.dtb"
done
cp "$BINARIES_DIR/Image" "$BINARIES_DIR/Image.A"
cp "$BINARIES_DIR/Image" "$BINARIES_DIR/Image.B"

# One label per carrier, so a single image boots on any of them; the
# carrier this image was built for is the default.
# One label per carrier and slot, so a single image boots on any carrier;
# the carrier this image was built for, slot A, is the default.
# Must stay in sync with NMS_CMDLINE in rootfs-overlay/usr/sbin/nms-ab, which
# re-applies these to boards that were flashed before they were introduced.
NMS_CMDLINE="pcie_aspm=off pcie_port_pm=off"

write_extlinux() {
	root_a=$1
	root_b=$2
	{
		echo "default ${CARRIER}_A"
		echo "menu title NMS-SM-K1892VM21YA boot options"
		echo "timeout 3"
		for dtb in "$BINARIES_DIR"/elvees/nms-*.dtb; do
			[ -f "$dtb" ] || continue
			name=$(basename "$dtb" .dtb)
			for slot in A B; do
				case $slot in
				A) root=$root_a ;;
				B) root=$root_b ;;
				esac
				echo
				echo "label ${name}_$slot"
				echo "\tmenu label $name ($slot)"
				echo "\tlinux /Image.$slot"
				echo "\tfdt /$name.$slot.dtb"
				echo "\tappend root=$root rootwait rw console=ttyS0,115200 earlycon $NMS_CMDLINE"
			done
		done
	} > "$BINARIES_DIR/extlinux/extlinux.conf"
}

# genimage does not expand globs, so the DTB list is spliced into the
# template here; a new carrier then needs no change to genimage.cfg.
GENIMAGE_TMP="$BUILD_DIR/genimage.tmp"

# genimage is invoked once per output image, and the sed below renames the
# vfat image to "$out.vfat".  An older build may have left a plain
# boot.vfat behind; it is never used again and only confuses inspection.
rm -f "$BINARIES_DIR/boot.vfat"

DTB_LIST=
for dtb in "$BINARIES_DIR"/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	DTB_LIST="$DTB_LIST\t\t\t\"$(basename "$dtb")\",\n"
done

# build_image <name> <root-A> <root-B> <mbr-signature>
#
# Two images are produced.  They differ only in the root= arguments and in
# the MBR disk signature, so root=PARTUUID resolves to the right device
# even when the SD card and the eMMC are both present: the SD image points
# at "NMS1"-02/-03, the eMMC image at "NMS2"-02/-03.
build_image() {
	out=$1
	root_a=$2
	root_b=$3
	sig=$4

	write_extlinux "$root_a" "$root_b"

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
build_image sdcard.img "PARTUUID=31534d4e-02" "PARTUUID=31534d4e-03" '\116\115\123\061'
build_image emmc.img   "PARTUUID=32534d4e-02" "PARTUUID=32534d4e-03" '\116\115\123\062'

# leave the SD variant in place as the reference extlinux.conf
write_extlinux "PARTUUID=31534d4e-02" "PARTUUID=31534d4e-03"

# ------------------------------------------------------------------
# SWUpdate package (A/B)
#
# Carries the root filesystem plus the kernel and the device trees of
# every carrier.  nms-update on the target resolves the inactive slot and
# points /run/swupdate/* at it, so one package works on both the SD card
# and the eMMC, and for either slot.
# ------------------------------------------------------------------
SWU="$BUILD_DIR/swu"
rm -rf "$SWU"
mkdir -p "$SWU"
gzip -n -9 -c "$BINARIES_DIR/rootfs.ext4" > "$SWU/rootfs.ext4.gz"
cp "$BINARIES_DIR/Image" "$SWU/Image"
SWU_FILES="rootfs.ext4.gz Image"
for dtb in "$BINARIES_DIR"/elvees/nms-*.dtb; do
	[ -f "$dtb" ] || continue
	cp "$dtb" "$SWU/"
	SWU_FILES="$SWU_FILES $(basename "$dtb")"
done

# One entry per image, each ending in a comma; the last one is stripped
# afterwards so libconfig never sees a trailing comma.
swu_entries() {
	slot=$1
	printf '\t\t\t\t{ filename = "rootfs.ext4.gz"; type = "raw";\n'
	printf '\t\t\t\t  compressed = "zlib"; installed-directly = true;\n'
	printf '\t\t\t\t  device = "/run/swupdate/rootfs-%s";\n' "$slot"
	printf '\t\t\t\t  sha256 = "%s" },\n' "$(sha256sum "$SWU/rootfs.ext4.gz" | cut -d' ' -f1)"
	printf '\t\t\t\t{ filename = "Image"; type = "raw";\n'
	printf '\t\t\t\t  installed-directly = true;\n'
	printf '\t\t\t\t  device = "/run/swupdate/Image-%s";\n' "$slot"
	printf '\t\t\t\t  sha256 = "%s" },\n' "$(sha256sum "$SWU/Image" | cut -d' ' -f1)"
	for dtb in "$BINARIES_DIR"/elvees/nms-*.dtb; do
		[ -f "$dtb" ] || continue
		name=$(basename "$dtb")
		printf '\t\t\t\t{ filename = "%s"; type = "raw";\n' "$name"
		printf '\t\t\t\t  installed-directly = true;\n'
		printf '\t\t\t\t  device = "/run/swupdate/%s-%s.dtb";\n' "${name%.dtb}" "$slot"
		printf '\t\t\t\t  sha256 = "%s" },\n' "$(sha256sum "$SWU/$name" | cut -d' ' -f1)"
	done
}

swu_images() {
	slot=$1
	echo "\t\t\timages = ("
	swu_entries "$slot" | sed '$ s/,$//'
	echo "\t\t\t);"
}

{
	echo "software = {"
	echo "\tversion = \"${SWU_VERSION:-1.0}\";"
	echo "\tdescription = \"NMS-SM-K1892VM21YA\";"
	echo "\thardware-compatibility = [\"nms-sm-k1892vm21ya\"];"
	echo "\tstable = {"
	echo "\t\tA = {"
	swu_images A ""
	echo "\t\t};"
	echo "\t\tB = {"
	swu_images B ""
	echo "\t\t};"
	echo "\t};"
	echo "};"
} > "$SWU/sw-description"

( cd "$SWU" && printf '%s\n' sw-description $SWU_FILES |
	cpio --quiet -o -H crc > "$BINARIES_DIR/firmware.swu" )
echo "post-image: firmware.swu ($(stat -c %s "$BINARIES_DIR/firmware.swu") bytes)"

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
