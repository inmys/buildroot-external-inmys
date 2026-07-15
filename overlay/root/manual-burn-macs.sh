cat > /opt/set-macs <<'EOF'
#!/bin/sh
set -e

ENV_MTD=/dev/mtd2
ENV_DIR=/tmp/barebox-env
BAREBOXENV=bareboxenv

echo -n "Введите MAC для eth0: "
read ETH0_MAC

echo -n "Введите MAC для eth1: "
read ETH1_MAC

case "$ETH0_MAC" in
    [0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F])
        ;;
    *)
        echo "Некорректный MAC eth0"
        exit 1
        ;;
esac

case "$ETH1_MAC" in
    [0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F]:[0-9a-fA-F][0-9a-fA-F])
        ;;
    *)
        echo "Некорректный MAC eth1"
        exit 1
        ;;
esac

rm -rf "$ENV_DIR"
mkdir -p "$ENV_DIR"

if ! "$BAREBOXENV" -l "$ENV_DIR" "$ENV_MTD"; then
    echo "Barebox environment пустой. Будет создан новый."
    rm -rf "$ENV_DIR"
    mkdir -p "$ENV_DIR"
fi

mkdir -p "$ENV_DIR/nv"

echo "$ETH0_MAC" > "$ENV_DIR/nv/dev.board.eth0_mac"
echo "$ETH1_MAC" > "$ENV_DIR/nv/dev.board.eth1_mac"

flash_erase "$ENV_MTD" 0 0
"$BAREBOXENV" -s "$ENV_DIR" "$ENV_MTD"
sync

echo "Записано:"
echo "board.eth0_mac=$ETH0_MAC"
echo "board.eth1_mac=$ETH1_MAC"
EOF

chmod +x /opt/set-macs
/opt/set-macs
