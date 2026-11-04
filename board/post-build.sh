#!/bin/sh
set -eu
: "${HOST_DIR:?}" "${BASE_DIR:?}"
target_dir=${1:?}
keys_dir=${NMS_KEYS_DIR:-$BASE_DIR/../keys}
openssl=$HOST_DIR/bin/openssl
umask 077
mkdir -p "$keys_dir"
work=$(mktemp -d "$keys_dir/.prepare.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 1' HUP INT TERM

if [ ! -s "$keys_dir/sign_priv.pem" ]; then
    "$openssl" genpkey -algorithm RSA -pkeyopt rsa_keygen_bits:2048 -out "$work/sign_priv.pem"
    mv "$work/sign_priv.pem" "$keys_dir/sign_priv.pem"
fi
if [ ! -s "$keys_dir/update.key" ]; then
    "$openssl" rand -hex 32 > "$work/update.key"
    mv "$work/update.key" "$keys_dir/update.key"
fi
passin=pass:
if [ "${SWU_SIGN_PASSPHRASE+x}" ]; then
    passin=env:SWU_SIGN_PASSPHRASE
elif [ -f "$keys_dir/sign.pass" ]; then
    passin=file:$keys_dir/sign.pass
fi
"$openssl" pkey -in "$keys_dir/sign_priv.pem" -passin "$passin" -pubout -out "$work/sign_public.pem"
mv "$work/sign_public.pem" "$keys_dir/sign_public.pem"
install -D -m 0644 "$keys_dir/sign_public.pem" "$target_dir/etc/sign_public.pem"
install -D -m 0600 "$keys_dir/update.key" "$target_dir/etc/update.key"
