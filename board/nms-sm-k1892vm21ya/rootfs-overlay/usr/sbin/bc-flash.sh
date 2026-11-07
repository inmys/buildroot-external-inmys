#!/bin/sh
#
# bc-flash.sh - update the board controller firmware over the BC UART.
#
# Runs on the Elvees SoC, where the BC UART is /dev/ttyS1 once the crossbar
# is switched:
#
#     nms-gpio.sh uart01 1
#     nms-gpio.sh bcuart
#     bc-flash.sh /dev/ttyS1 /root/bc.bin 7835ca90
#
# The third argument is the CRC-32 of the padded image, printed by
# "make crc" on the build host.  It is optional but strongly recommended:
# without it a corrupted transfer is only caught by the MCU's own read-back
# check, which happens after the erase.
#
# The STM32 ROM bootloader is not used.  Entering it needs a reset, and a
# reset of the MCU drops DCDC_EN and takes the SoC down (see README).  The
# application instead buffers the image in RAM, reports its CRC, and only
# erases and programs on "flash commit".
#
# When the update finishes the MCU restarts, and its board_early_init()
# pulses CPU_RESET#: the SoC reboots too.  That is expected - it is the last
# step, and the script will not get to print anything after it.
#
# Note on reading the replies: busybox "read" does not work reliably on a
# tty in raw mode, so the port is drained by a background dd into a file and
# the file is polled.  conv=fsync keeps it durable across the final reboot.

set -e

PORT=${1:-/dev/ttyS1}
IMG=${2:-/root/bc.bin}
WANT=${3:-}

[ -f "$IMG" ] || { echo "no such image: $IMG" >&2; exit 1; }
[ -c "$PORT" ] || { echo "no such port: $PORT" >&2; exit 1; }

size=$(wc -c < "$IMG")
pad=$(( (4 - size % 4) % 4 ))
total=$(( size + pad ))

echo "image : $IMG"
echo "size  : $size bytes (+$pad padding = $total)"
echo "port  : $PORT"

# busybox stty returns 0 but leaves ICANON/ECHO set, and the echoed bytes go
# straight back into the MCU as image data.  tty-raw is a 20-line termios
# tool that actually does it (tools/tty-raw.c, installed as /usr/sbin/tty-raw).
if command -v tty-raw >/dev/null 2>&1; then
    tty-raw "$PORT" 115200
else
    echo "warning: tty-raw not found, falling back to stty" >&2
    stty -F "$PORT" 115200 raw -echo -crtscts
fi

# Bring the MCU to a known idle state: an earlier session may have left it
# waiting for image data.  "flash abort" is harmless when nothing is buffered.
printf 'flash abort\r\n' > "$PORT"
# If a previous session died mid-transfer the MCU is still in its receive
# loop and swallows "flash abort" as image data; it only comes back when its
# own per-byte timeout expires.  That timeout is uart_getc_to() in
# src/main.c; older builds used 2000000 (~10 s), current ones 200000 (~1 s).
# Wait long enough for the slowest build we might be recovering from.
sleep 12
timeout 1 cat "$PORT" > /dev/null 2>&1 || true

RESP=$(mktemp /tmp/bc-flash.XXXXXX)
dd if="$PORT" of="$RESP" bs=64 conv=fsync &
CATPID=$!
trap 'kill $CATPID 2>/dev/null; rm -f "$RESP"' EXIT INT TERM

# wait_for <extended-regexp> <seconds>
wait_for() {
    n=0
    while [ "$n" -lt $(( $2 * 10 )) ]; do
        if grep -qE "$1" "$RESP" 2>/dev/null; then
            return 0
        fi
        sleep 0.1
        n=$(( n + 1 ))
    done
    return 1
}

printf 'flash %d\r\n' "$total" > "$PORT"

if ! wait_for 'READY [0-9]+' 10; then
    echo "no READY from the board controller, got:" >&2
    cat "$RESP" >&2
    cp "$RESP" /tmp/bc-resp.log 2>/dev/null || true
    exit 1
fi
echo "board : $(grep -oE 'READY [0-9]+' "$RESP" | tail -1)"

{
    dd if="$IMG" bs=512 2>/dev/null
    if [ "$pad" -gt 0 ]; then
        head -c "$pad" /dev/zero | tr '\000' '\377'
    fi
} > "$PORT"

if ! wait_for 'CRC [0-9a-f]{8}' 20; then
    echo "no CRC from the board controller, got:" >&2
    cat "$RESP" >&2
    exit 1
fi
got=$(grep -oE 'CRC [0-9a-f]{8}' "$RESP" | tail -1 | cut -d' ' -f2)
echo "board : CRC $got"

if [ -n "$WANT" ]; then
    if [ "$got" != "$WANT" ]; then
        echo "CRC mismatch: board got $got, expected $WANT" >&2
        printf 'flash abort\r\n' > "$PORT"
        exit 1
    fi
    echo "crc   : matches"
else
    echo "crc   : not checked - pass it as the third argument"
fi

echo "commit: erasing and writing, the SoC will reboot"
printf 'flash commit\r\n' > "$PORT"

if wait_for 'DONE|ERR' 20; then
    echo "board : $(grep -oE 'DONE.*|ERR.*' "$RESP" | tail -1)"
fi

# give the MCU time to restart and pulse CPU_RESET#
sleep 2
