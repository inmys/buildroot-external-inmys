#!/bin/sh
#
# Regenerate the device tree patches (patches/linux/*.patch and
# patches/uboot/*.patch) from the board directory.
#
# The device trees are maintained in this br2-external tree and applied
# to the upstream ELVEES kernel/U-Boot trees as patches, so that the
# upstream repositories stay untouched.
#
# Usage: gen-dts-patches.sh <linux-src> <uboot-src>
#
# Copyright (C) 2026 INMYS

set -eu

here=$(cd -- "$(dirname -- "$0")/.." && pwd)
board="$here/board/nms-sm-k1892vm21ya"
linux_src=${1:-}
uboot_src=${2:-}

[ -n "$linux_src" ] && [ -d "$linux_src/arch/arm64/boot/dts/elvees" ] || {
	echo "usage: $0 <linux-src> <uboot-src>" >&2
	exit 1
}
[ -n "$uboot_src" ] && [ -d "$uboot_src/arch/arm/dts" ] || {
	echo "usage: $0 <linux-src> <uboot-src>" >&2
	exit 1
}

work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT

# ------------------------------------------------------------------
# Linux
# ------------------------------------------------------------------
ldir=arch/arm64/boot/dts/elvees
mkdir -p "$work/linux/a/$ldir" "$work/linux/b/$ldir"

cp "$linux_src/$ldir/Makefile" "$work/linux/a/$ldir/Makefile"
python3 - "$linux_src/$ldir/Makefile" "$work/linux/b/$ldir/Makefile" "$board/linux" <<'PY'
import glob
import os
import sys

src, dst, board_dir = sys.argv[1], sys.argv[2], sys.argv[3]
text = open(src).read()

# The list of device trees is derived from the files in the board
# directory so that adding a carrier never requires editing this script.
names = sorted(os.path.basename(f)[:-4]
               for f in glob.glob(os.path.join(board_dir, "*.dts")))
lines = ["dtb-$(CONFIG_ARCH_MCOM03R) += %s.dtb" % n for n in names] + ["", ""]
marker = "subdir-y += overlays"
assert marker in text, "unexpected Makefile layout"
text = text.replace(marker, "\n".join(lines) + marker, 1)
open(dst, "w").write(text)
PY

for f in "$board"/linux/*.dts "$board"/linux/*.dtsi; do
	cp "$f" "$work/linux/b/$ldir/"
done

# Strip the file timestamps from the diff header so that regenerating
# the patch does not produce spurious changes.
strip_ts() { sed -E 's/^(---|\+\+\+) ([^\t]+)\t.*/\1 \2/'; }

(cd "$work/linux" && diff -urN a b | strip_ts) > "$here/patches/linux/0001-arm64-dts-elvees-add-NMS-SM-K1892VM21YA-module-and-c.patch" || true

# ------------------------------------------------------------------
# U-Boot
# ------------------------------------------------------------------
udir=arch/arm/dts
mkdir -p "$work/uboot/a/$udir" "$work/uboot/b/$udir"

cp "$uboot_src/$udir/Makefile" "$work/uboot/a/$udir/Makefile"
python3 - "$uboot_src/$udir/Makefile" "$work/uboot/b/$udir/Makefile" "$board/uboot" <<'PY'
import glob
import os
import sys

src, dst, board_dir = sys.argv[1], sys.argv[2], sys.argv[3]
lines = open(src).read().splitlines(keepends=True)

# Derived from the files in the board directory, see the Linux block above.
names = sorted(os.path.basename(f)[:-4]
               for f in glob.glob(os.path.join(board_dir, "*.dts")))

anchor = "\tmcom03r-skifmsb-v2.0-skifmp-v2.0.dtb\n"
idx = lines.index(anchor)
lines[idx] = "\tmcom03r-skifmsb-v2.0-skifmp-v2.0.dtb \\\n"
for i, name in enumerate(names):
    suffix = " \\\n" if i < len(names) - 1 else "\n"
    lines.insert(idx + 1 + i, "\t%s.dtb%s" % (name, suffix))
open(dst, "w").write("".join(lines))
PY

for f in "$board"/uboot/*.dts "$board"/uboot/*.dtsi; do
	cp "$f" "$work/uboot/b/$udir/"
done

(cd "$work/uboot" && diff -urN a b | strip_ts) > "$here/patches/uboot/0001-arm-dts-mcom03r-add-NMS-SM-K1892VM21YA-module-and-ca.patch" || true

echo "regenerated:"
echo "  $here/patches/linux/0001-arm64-dts-elvees-add-NMS-SM-K1892VM21YA-module-and-c.patch"
echo "  $here/patches/uboot/0001-arm-dts-mcom03r-add-NMS-SM-K1892VM21YA-module-and-ca.patch"
