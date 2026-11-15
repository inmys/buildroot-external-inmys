#!/bin/sh
# Program D41 STM32L052C8 through its factory USART1 ROM bootloader.
# Run on the ELVEES SoC. Requires R290 omitted, R291 fitted, and working
# BC_BOOT/BC_RST GPIOs. Hardware reset is used; no application flash code.
# Usage: bc-flash.sh [/dev/ttyS1] firmware.bin
set -eu
case $# in
1) PORT=/dev/ttyS1; IMG=$1 ;;
2) PORT=$1; IMG=$2 ;;
*) echo "usage: $0 [/dev/ttyS1] firmware.bin" >&2; exit 1 ;;
esac
[ -c "$PORT" ] || { echo "no such serial port: $PORT" >&2; exit 1; }
[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 1; }
command -v stm32flash >/dev/null
command -v gpiofind >/dev/null
command -v gpioset >/dev/null
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
# GPIO names are stable; gpiochip numbers may change between kernels.
set -- $(gpiofind BC_UART_SEL); chip=$1; uart_sel=$2
set -- $(gpiofind BC_BOOT); [ "$1" = "$chip" ]; boot=$2
set -- $(gpiofind BC_RST); [ "$1" = "$chip" ]; reset=$2
holder=
cleanup() {
    if [ -n "$holder" ]; then
        kill "$holder" 2>/dev/null || true
        wait "$holder" 2>/dev/null || true
        holder=
    fi
    # D34 retains output registers on release; external pull-downs also
    # keep all three lines low. A failed flash stays in ROM: no reset pulse.
    gpioset "$chip" "$boot=0" "$uart_sel=0" "$reset=0" || true
}
pulse_reset() {
    gpioset -m time -u 50000 "$chip" "$reset=1"
    gpioset "$chip" "$reset=0"
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
# UART_0_1_CROSS# is held high by the DTS debug-console hog.
# Hold the route and BOOT0 throughout the ROM transaction.
gpioset -m signal "$chip" "$uart_sel=1" "$boot=1" &
holder=$!
sleep 0.1
kill -0 "$holder"
pulse_reset
sleep 1
# stm32flash configures 8E1 itself. No erase/program command is sent until
# it has synchronized with, and identified, the ROM bootloader.
stm32flash -b 115200 -m 8e1 -w "$IMG" -v "$PORT"
cleanup
trap - EXIT INT TERM
echo "Verified. Resetting STM32; the new application will restart the SoC."
sync
pulse_reset
