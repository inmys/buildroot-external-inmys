#!/bin/sh
set -eu
swu_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
: "${BINARIES_DIR:?}" "${HOST_DIR:?}" "${BASE_DIR:?}"
openssl=$HOST_DIR/bin/openssl
keys_dir=${NMS_KEYS_DIR:-$BASE_DIR/../keys}
rootfs=$BINARIES_DIR/rootfs.ubifs
size=$(wc -c < "$rootfs")
[ "$size" -le 50331648 ] || { echo 'rootfs.ubifs exceeds the 48 MiB slot' >&2; exit 1; }
read -r key rest < "$keys_dir/update.key" || [ -n "$key" ]
case "$key" in ''|*[!0-9a-fA-F]*) echo 'Invalid AES key' >&2; exit 1 ;; esac
[ "${#key}" -eq 64 ] || { echo 'Expected AES-256 key' >&2; exit 1; }
work=$(mktemp -d "$BINARIES_DIR/.swu.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM
iv=$("$openssl" rand -hex 16)
"$openssl" enc -aes-256-cbc -in "$rootfs" -out "$work/rootfs.ubifs" -K "$key" -iv "$iv"
hash=$(sha256sum "$work/rootfs.ubifs" | cut -d ' ' -f 1)
sed -e "s/size_orig_rootfs.ubifs/$size/g" \
    -e "s/9F70FCEC206603D2AAD7B318CC2259EA/$iv/g" \
    -e "s/sha256_rootfs.ubifs/$hash/g" \
    "$swu_dir/sw-description" > "$work/sw-description"
passin=pass:
if [ "${SWU_SIGN_PASSPHRASE+x}" ]; then
    passin=env:SWU_SIGN_PASSPHRASE
elif [ -f "$keys_dir/sign.pass" ]; then
    passin=file:$keys_dir/sign.pass
fi
"$openssl" dgst -sha256 -sign "$keys_dir/sign_priv.pem" -passin "$passin" \
    -out "$work/sw-description.sig" "$work/sw-description"
"$openssl" dgst -sha256 -verify "$keys_dir/sign_public.pem" \
    -signature "$work/sw-description.sig" "$work/sw-description"
(cd "$work" && printf '%s\n' sw-description sw-description.sig rootfs.ubifs | \
    cpio --quiet -o -H crc > update.swu)
mv "$work/update.swu" "$BINARIES_DIR/update.swu"
