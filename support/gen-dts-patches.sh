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

python3 - "$linux_src/$ldir/Makefile" "$work/linux/a/$ldir/Makefile" \
	"$work/linux/b/$ldir/Makefile" "$board/linux" <<'PY'
import glob
import os
import sys

src, dst_a, dst_b, board_dir = sys.argv[1:5]
text = open(src).read()

# The list of device trees is derived from the files in the board
# directory so that adding a carrier never requires editing this script.
names = sorted(os.path.basename(f)[:-4]
               for f in glob.glob(os.path.join(board_dir, "*.dts")))
lines = ["dtb-$(CONFIG_ARCH_MCOM03R) += %s.dtb" % n for n in names] + ["", ""]
marker = "subdir-y += overlays"
assert marker in text, "unexpected Makefile layout"

# Buildroot patches its source tree in place, so the tree this script is
# pointed at may already carry this patch.  Take the block back out before
# using it as the "a" side, otherwise a/ and b/ both contain it and the
# Makefile hunk either vanishes or turns into a duplicate insertion.
block = "\n".join(lines)
text = text.replace(block + marker, marker, 1)

open(dst_a, "w").write(text)
open(dst_b, "w").write(text.replace(marker, block + marker, 1))
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

python3 - "$uboot_src/$udir/Makefile" "$work/uboot/a/$udir/Makefile" \
	"$work/uboot/b/$udir/Makefile" "$board/uboot" <<'PY'
import glob
import os
import sys

src, dst_a, dst_b, board_dir = sys.argv[1:5]
lines = open(src).read().splitlines(keepends=True)

# Derived from the files in the board directory, see the Linux block above.
names = sorted(os.path.basename(f)[:-4]
               for f in glob.glob(os.path.join(board_dir, "*.dts")))

# The anchor is the last upstream entry.  Match it by prefix and drop any
# trailing continuation, so the script also works when it is run against a
# tree that already carries this patch (the build directories do).
anchor = "mcom03r-skifmsb-v2.0-skifmp-v2.0.dtb"


def find_anchor(ls):
    for i, line in enumerate(ls):
        if line.strip().rstrip("\\").strip() == anchor:
            return i
    sys.exit("gen-dts-patches: cannot find %s in %s" % (anchor, src))


# Drop the entries a previous run may have inserted, so that "a" is the
# pristine Makefile, and put the anchor back the way upstream has it.
ours = set("%s.dtb" % n for n in names)
base = [l for l in lines if l.strip().rstrip("\\").strip() not in ours]
idx = find_anchor(base)
base[idx] = "\t%s\n" % anchor

patched = list(base)
patched[idx] = "\t%s \\\n" % anchor
for i, name in enumerate(names):
    suffix = " \\\n" if i < len(names) - 1 else "\n"
    patched.insert(idx + 1 + i, "\t%s.dtb%s" % (name, suffix))

open(dst_a, "w").write("".join(base))
open(dst_b, "w").write("".join(patched))
PY

for f in "$board"/uboot/*.dts "$board"/uboot/*.dtsi; do
	cp "$f" "$work/uboot/b/$udir/"
done

(cd "$work/uboot" && diff -urN a b | strip_ts) > "$here/patches/uboot/0001-arm-dts-mcom03r-add-NMS-SM-K1892VM21YA-module-and-ca.patch" || true

echo "regenerated:"
echo "  $here/patches/linux/0001-arm64-dts-elvees-add-NMS-SM-K1892VM21YA-module-and-c.patch"
echo "  $here/patches/uboot/0001-arm-dts-mcom03r-add-NMS-SM-K1892VM21YA-module-and-ca.patch"
