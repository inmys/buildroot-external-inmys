#!/bin/sh
set -e

ENV_MTD=/dev/mtd2
ENV_DIR=/tmp/barebox-env
BAREBOXENV=bareboxenv
BUS=1

read_mac()
{
	addr=$1
	eeprom="/sys/bus/i2c/devices/${BUS}-00${addr}/eeprom"
	dd if="$eeprom" bs=1 skip=250 count=6 2>/dev/null |
		hexdump -v -e '5/1 "%02x:" 1/1 "%02x\n"'


}

ETH0_MAC=$(read_mac 52)
ETH1_MAC=$(read_mac 53)

echo "EEPROM 0x52: $ETH0_MAC"
echo "EEPROM 0x53: $ETH1_MAC"

rm -rf "$ENV_DIR"
mkdir -p "$ENV_DIR"

"$BAREBOXENV" -l "$ENV_DIR" "$ENV_MTD"

mkdir -p "$ENV_DIR/nv"
echo "$ETH0_MAC" > "$ENV_DIR/nv/dev.board.eth0_mac"
echo "$ETH1_MAC" > "$ENV_DIR/nv/dev.board.eth1_mac"

flash_erase "$ENV_MTD" 0 0
"$BAREBOXENV" -s "$ENV_DIR" "$ENV_MTD"
sync

echo "MAC-адреса записаны"

