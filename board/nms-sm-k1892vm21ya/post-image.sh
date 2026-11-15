#!/bin/sh
# Generate factory SD/eMMC images and one whole-rootfs A/B update.
set -eu
BOARD_DIR=$(dirname "$0")

cp "$BINARIES_DIR/sd-boot/nms-sm-k1892vm21ya.sbimg" "$BINARIES_DIR/sdboot.sbimg"

build_image() {
    out=$1
    sig=$2
    filter=$3
    sed -e "s|image sdcard.img {|image $out {|" -e "$filter" \
        "$BOARD_DIR/genimage.cfg" > "$BUILD_DIR/genimage-$out.cfg"
    rm -rf "$BUILD_DIR/genimage.tmp"
    "$HOST_DIR/bin/genimage" --rootpath "$TARGET_DIR" \
        --tmppath "$BUILD_DIR/genimage.tmp" --inputpath "$BINARIES_DIR" \
        --outputpath "$BINARIES_DIR" --config "$BUILD_DIR/genimage-$out.cfg"
    printf "$sig" | dd of="$BINARIES_DIR/$out" bs=1 seek=440 conv=notrunc status=none
    # libparted replaces an all-zero MBR boot-code area, including its disk
    # signature. A two-byte x86 loop keeps PARTUUID stable when p3
    # grows; the ARM ROM starts at LBA1 and never executes these two bytes.
    printf '\353\376' | dd of="$BINARIES_DIR/$out" bs=1 conv=notrunc status=none
}
build_image sdcard.img '\116\115\123\061' ''
build_image emmc.img '\116\115\123\062' '/# SD_BOOT_BEGIN/,/# SD_BOOT_END/d'
rm -rf "$BUILD_DIR/genimage.tmp"

# The same rootfs payload serves both slots and both MMC devices.
# Only the inactive target link exists; the pre-update hook validates it.
SWU="$BUILD_DIR/swu"
rm -rf "$SWU"
mkdir -p "$SWU"
gzip -n -9 -c "$BINARIES_DIR/rootfs.ext4" > "$SWU/rootfs.ext4.gz"
hash=$(sha256sum "$SWU/rootfs.ext4.gz" | cut -d' ' -f1)
{
    echo 'software = {'
    echo '    version = "1.0";'
    echo '    hardware-compatibility = ["nms-sm-k1892vm21ya"];'
    echo '    stable = {'
    for slot in A B; do
        cat <<EOF
        $slot = {
            images = ({
                filename = "rootfs.ext4.gz";
                type = "raw";
                compressed = "zlib";
                installed-directly = true;
                device = "/run/swupdate/rootfs-$slot";
                sha256 = "$hash";
            });
            bootenv = (
                { name = "active_slot"; value = "$slot"; },
                { name = "upgrade_available"; value = "1"; },
                { name = "bootcount"; value = "0"; }
            );
        };
EOF
    done
    echo '    };'
    echo '};'
} > "$SWU/sw-description"
(cd "$SWU" && printf '%s\n' sw-description rootfs.ext4.gz | \
    cpio --quiet -o -H crc > "$BINARIES_DIR/firmware.swu")
echo "post-image: firmware.swu ($(stat -c %s "$BINARIES_DIR/firmware.swu") bytes)"

# ------------------------------------------------------------------
# QSPI boot flash
#
# One module boot chain is shared by all carriers. Linux still selects a
# carrier-specific DTB through fdtfile and fdtdir in its rootfs slot.
TL_IMAGE="$BINARIES_DIR/tl-sbimg/nms-sm-k1892vm21ya-mcom03r-nms-sm-k1892vm21ya.tl-image"
[ -f "$TL_IMAGE" ] || { echo "post-image: missing $TL_IMAGE" >&2; exit 1; }

# The flat programmer image has the same module-only DTB as the TL package.
"$BOARD_DIR/../../support/make-qspi-image.sh" \
    -o "$BINARIES_DIR/qspi-full-nms-sm-k1892vm21ya.bin" "$TL_IMAGE"

echo "post-image: images in $BINARIES_DIR:"
ls -lh "$BINARIES_DIR/sdcard.img" "$BINARIES_DIR/emmc.img" \
    "$BINARIES_DIR/firmware.swu" "$TL_IMAGE" \
    "$BINARIES_DIR/qspi-full-nms-sm-k1892vm21ya.bin"
