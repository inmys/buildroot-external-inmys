#!/bin/sh
# Program D41 STM32L052C8 through its factory USART1 ROM bootloader.
# Run on the ELVEES SoC. Requires R290 omitted, R291 fitted, and working
# BC_BOOT/BC_RST GPIOs. Hardware reset is used; no application flash code.
# Usage: bc-flash.sh [/dev/ttyS1] firmware.bin
set -eu
PORT=${1:-/dev/ttyS1}
IMG=${2:-/root/bc.bin}
[ $# -le 2 ] || { echo "usage: $0 [/dev/ttyS1] firmware.bin" >&2; exit 1; }
[ -c "$PORT" ] || { echo "no such serial port: $PORT" >&2; exit 1; }
[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 1; }
command -v stm32flash >/dev/null
command -v nms-gpio.sh >/dev/null
size=$(wc -c < "$IMG")
[ "$size" -ge 192 ] && [ "$size" -le 65536 ] || {
    echo "invalid STM32L052C8 image size: $size" >&2; exit 1;
}
sp=$(od -An -tu4 -N4 "$IMG" | tr -d ' ')
rv=$(od -An -tu4 -j4 -N4 "$IMG" | tr -d ' ')
[ "$sp" -gt $((0x20000000)) ] && [ "$sp" -le $((0x20002000)) ] &&
[ $((sp & 7)) -eq 0 ] && [ "$rv" -ge $((0x08000001)) ] &&
[ "$rv" -lt $((0x08000000 + size)) ] && [ $((rv & 1)) -eq 1 ] || {
    echo "invalid STM32L052C8 vector table" >&2; exit 1;
}
cleanup() {
    # On failure, leave the MCU in ROM for recovery. Do not jump into a
    # partially programmed application. BOOT0 may safely be deasserted.
    nms-gpio.sh set BC_BOOT 0 >/dev/null 2>&1 || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# Remove consumers before unbinding the virtual GPIO/IRQ controller. This
# does not affect the physical PCAL6416 at 0x20 which routes BC_BOOT/BC_RST.
for dev in /sys/bus/platform/drivers/gpio-keys/*; do
    [ -L "$dev/of_node" ] || continue
    [ "${dev##*/}" = power-keys ] || continue
    echo "${dev##*/}" > /sys/bus/platform/drivers/gpio-keys/unbind
done
if [ -L /sys/bus/i2c/devices/9-0068/driver ]; then
    echo 9-0068 > /sys/bus/i2c/devices/9-0068/driver/unbind
fi
if [ -L /sys/bus/i2c/devices/9-002a/driver ]; then
    echo 9-002a > /sys/bus/i2c/devices/9-002a/driver/unbind
fi
nms-gpio.sh uart01 1
nms-gpio.sh bcuart
nms-gpio.sh set BC_BOOT 1
nms-gpio.sh bcreset 50
sleep 1
# stm32flash configures 8E1 itself. No erase/program command is sent until
# it has synchronized with, and identified, the ROM bootloader.
stm32flash -b 115200 -m 8e1 -w "$IMG" -v "$PORT"
nms-gpio.sh set BC_BOOT 0
trap - EXIT INT TERM
echo "Verified. Resetting STM32; the new application will restart the SoC."
sync
nms-gpio.sh bcreset 50
