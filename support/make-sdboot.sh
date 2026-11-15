#!/bin/sh
# Pack the module's QSPI-independent RISC0 SD boot chain.
# Usage: make-sdboot.sh BUILD_DIR IMAGES_DIR HOST_DIR UBOOT_DTB DDRINIT_NAME
set -eu
export LC_ALL=C
[ "$#" -eq 5 ] || { echo "usage: $0 BUILD IMAGES HOST UBOOT_DTB DDRINIT_NAME" >&2; exit 1; }
build=$(cd "$1" && pwd)
images=$(cd "$2" && pwd)
host=$(cd "$3" && pwd)
dtb=$4
ddrinit=$5
name=${dtb#mcom03r-}
mkimage="$host/opt/bootrom-tools/mksbimage/mksbimage"

die() { echo "make-sdboot: $*" >&2; exit 1; }

# SBIMG v1: a 96-byte header followed by its payload, repeated per object.
# The header hash covers bytes 0..63 plus 32 zero bytes. Payload hash flag=8.
verify_sbimg() {
    blob=$1
    blob_size=$(stat -c %s "$blob")
    [ "$blob_size" -gt 0 ] || die "$blob: empty SBIMG"
    offset=0
    while [ "$offset" -lt "$blob_size" ]; do
        [ $((blob_size - offset)) -ge 96 ] || die "$blob: truncated header"
        set -- $(od -An -v -tu4 --endian=little -j "$offset" -N 20 "$blob")
        [ "$1" -eq $((0x53424d47)) ] || die "$blob: invalid SBIMG magic"
        length=$2
        flags=$5
        [ $((offset + 96 + length)) -le "$blob_size" ] || die "$blob: truncated payload"
        expected=$(dd if="$blob" iflag=skip_bytes,count_bytes skip=$((offset + 64)) count=32 status=none |
            od -An -v -tx1 | tr -d ' \n')
        actual=$({
            dd if="$blob" iflag=skip_bytes,count_bytes skip="$offset" count=64 status=none
            head -c 32 /dev/zero
        } | sha256sum | cut -d' ' -f1)
        [ "$actual" = "$expected" ] || die "$blob: invalid header SHA-256"
        if [ $((flags & 8)) -ne 0 ]; then
            expected=$(dd if="$blob" iflag=skip_bytes,count_bytes skip=$((offset + 32)) count=32 status=none |
                od -An -v -tx1 | tr -d ' \n')
            actual=$(dd if="$blob" iflag=skip_bytes,count_bytes skip=$((offset + 96)) count="$length" status=none |
                sha256sum | cut -d' ' -f1)
            [ "$actual" = "$expected" ] || die "$blob: invalid payload SHA-256"
        fi
        offset=$((offset + 96 + length))
    done
}

le32() {
    for shift in 0 8 16 24; do
        printf '%b' "\\0$(printf '%03o' "$((($1 >> shift) & 255))")"
    done
}

work=$(mktemp -d "$build/sdboot.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
cp "$build/sbl-s1/sbl-s1.bin" "$work/s1.bin"
cp "$build/sbl-s2/sbl-s2-sd.bin" "$work/s2.bin"
cp "$build/sbl-s3/sbl-s3.bin" "$work/s3.bin"
cp "$images/ddrinit-$ddrinit/ddrinit.bin" "$work/ddr.bin"
cp "$images/uboot-dtb/$dtb.dtb" "$work/dtb.bin"
cp "$images/bl31.bin" "$work/bl31.bin"
cp "$images/u-boot.bin" "$work/uboot.bin"
cd "$work"

# Modify the generated DTB only; SD and QSPI share one maintained source.
"$host/bin/fdtput" -p -t x dtb.bin /chosen inmys,sd-boot
"$host/bin/fdtput" -t s dtb.bin /chosen inmys,boot-medium sd
qspi=$("$host/bin/fdtget" -t s dtb.bin /__symbols__ qspi0)
"$host/bin/fdtput" -t s dtb.bin "$qspi" status disabled

# Include alignment bytes in each object's recorded length and payload hash.
for file in s1.bin s2.bin s3.bin ddr.bin dtb.bin bl31.bin uboot.bin; do
    size=$(stat -c %s "$file")
    head -c "$(((16 - size % 16) % 16))" /dev/zero >> "$file"
done
cat > firmware.cfg <<'EOF'
path_to_bin_part=dtb.bin 0xC0002000 no_exec
path_to_bin_part=bl31.bin 0xC0300000 no_exec
path_to_bin_part=uboot.bin 0xC0080000 no_exec
path_to_bin=s3.bin
load_address=0x40003000
entry_address=0x40003000
check_payload_hash=1
check_encrypted_payload_integrity=0
skip_header_hash=0
EOF
"$mkimage" -i firmware.cfg -o firmware.sbimg
verify_sbimg firmware.sbimg
firmware_size=$(stat -c %s firmware.sbimg)
[ "$firmware_size" -le $((0x400000)) ] || die "RAM firmware exceeds SBL-S2's 4 MiB limit"
{
    printf NMSD
    le32 1
    le32 "$firmware_size"
    le32 0
    cat firmware.sbimg
} > capsule.bin
cat > sd.cfg <<'EOF'
path_to_bin_part=s1.bin 0xa0000000 0xa0000000
path_to_bin_part=ddr.bin 0xa0000000 0xa0000000
path_to_bin_part=capsule.bin 0x4c000000 no_exec
path_to_bin=s2.bin
load_address=0x48000000
entry_address=0x48000000
check_payload_hash=1
check_encrypted_payload_integrity=0
skip_header_hash=0
EOF
"$mkimage" -i sd.cfg -o sd.sbimg
verify_sbimg sd.sbimg
size=$(stat -c %s sd.sbimg)
[ $((size + 512)) -le $((0x300000)) ] || die "SD chain overlaps environment at 3 MiB"
mkdir -p "$images/sd-boot"
cp sd.sbimg "$images/sd-boot/$name.sbimg"
echo "SD boot: $name: $size bytes"
