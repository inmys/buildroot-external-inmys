#!/bin/sh
#
# Root filesystem post-build script for NMS-SM-K1892VM21YA.
#
# Copyright (C) 2026 INMYS

set -eu

TARGET_DIR="$1"

# Dropbear's Buildroot default symlink is for a volatile /run directory.
if [ -L "$TARGET_DIR/etc/dropbear" ]; then rm "$TARGET_DIR/etc/dropbear"; fi
mkdir -p "$TARGET_DIR/etc/dropbear" "$TARGET_DIR/root/.ssh" "$TARGET_DIR/data"

# Keep Buildroot's standard fstab/getty; only debugfs is useful for bring-up.
if ! grep -q '^[^#]*[[:space:]]/sys/kernel/debug[[:space:]]' "$TARGET_DIR/etc/fstab"; then
    printf 'debugfs /sys/kernel/debug debugfs defaults 0 0\n' >> "$TARGET_DIR/etc/fstab"
fi
