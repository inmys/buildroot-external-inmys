#!/bin/sh
#
# nms-gpio.sh - board multiplexers and switches of the NMS-SM-K1892VM21YA
#
# Every line is resolved by name with gpiofind, so the script does not care
# which gpiochip a line ended up on: some of them are PCAL6416A (D34)
# outputs on I2C_INT_1V8, one is a plain SoC GPIO.
#
#   name            function
#   --------------  -----------------------------------------------------
#   LCD1_BKLT_EN    backlight 1 enable             -> SMARC S107
#   LCD1_VDD_EN     panel 1 power                  -> SMARC S116
#   QSPI_SPI#       1 = QSPI1 (socket XS7), 0 = SPI1
#   BC_UART_SEL     1 = STM32, 0 = SER3 (SMARC P140/P141)
#   PCIE_SEL        drives VT5 -> PCIe root complex select
#   EXP_P1_1        spare
#   USB1_ID0_GPIO   1 = OTG_ID low = host mode (VT3)
#   BC_BOOT         STM32 BOOT0
#
# BC_RST is special: it is never held.  Resetting the STM32 also resets the
# SoC (the board controller drives RESET_OUT# / the carrier's RESET_IN#), so
# "bcreset" only emits a short pulse and lets the SoC come back on its own.
# R272 pulls the line down, so releasing it is enough to leave reset.
#   CAM_SEL0/1      camera clock/FSYNC mux A/B (D1 RS2255)
#   UART_0_1_CROSS# which UART reaches SER3/BC and which reaches SER1_DBG
#
# Not here, because a driver owns them: LCD0_VDD_EN (lvds_panel),
# LCD0_BKLT_EN (pwm-backlight), HDMI_PD (adv7511), ENABLE_LVDS
# (sn65dsi83); HDMI_INT# is an input.
#
# D1 is a 4:1 analog mux (RS2255XN, ENABLE tied to GND) that puts one of the
# SoC camera clocks onto the shared SMARC S6 pin:
#
#   CAM_SEL1 CAM_SEL0   S6 carries
#   -------- --------   ------------
#      0        0        CMOS0_FSYNC
#      0        1        CMOS1_FSYNC
#      1        0        CMOS0_CLK
#      1        1        CMOS1_CLK
#
# UART_0_1_CROSS# (D33 RS2299, 10K series from the SoC, 10K pull-down,
# active low) swaps the two debug UARTs:
#
#   0   UART0 -> SER3/board controller, UART1 -> SER1_DBG (SMARC P134/P135)
#   1   UART1 -> SER3/board controller, UART0 -> SER1_DBG
#
# 1 is the default because UART0 is not reachable from CPU0 once the SoC
# security is enabled, so Linux always talks over UART1.
#
# libgpiod releases a line when the process that requested it exits and the
# pin then falls back to its pull resistor, so the state is held by detached
# "gpioset --mode=wait" processes, one per gpiochip.
#
# Copyright (C) 2026 INMYS

set -eu

STATE=/run/nms-gpio.state
PIDFILE=/run/nms-gpio.pid

# name value -- the order is the order of the "status" output
DEFAULTS="
LCD1_BKLT_EN 0
LCD1_VDD_EN 0
QSPI_SPI# 1
BC_UART_SEL 0
PCIE_SEL 0
EXP_P1_1 0
USB1_ID0_GPIO 0
BC_BOOT 0
CAM_SEL0 0
CAM_SEL1 0
UART_0_1_CROSS# 1
"

usage() {
	cat >&2 <<EOF
usage: $(basename "$0") status
       $(basename "$0") set <NAME> <0|1> ...
       $(basename "$0") qspi1 | spi1
       $(basename "$0") bcuart | ser3
       $(basename "$0") uart01 <0|1>        0 = UART0 on SER3/BC
       $(basename "$0") cam <0|1|2|3>       0 = CMOS0_FSYNC, 1 = CMOS1_FSYNC
                                            2 = CMOS0_CLK,   3 = CMOS1_CLK
       $(basename "$0") lcd <bklt> <power>  LCD1_BKLT_EN, LCD1_VDD_EN
       $(basename "$0") bcreset [ms]      pulse BC_RST (default 50 ms);
                                           the SoC restarts, this shell dies
EOF
	exit 1
}

# state file: "<chip> <line> <value> <name>" per line, resolved on demand
load() {
	[ -r "$STATE" ] || : > "$STATE"
}

get() {
	awk -v n="$1" '$4 == n { print $3; found = 1 } END { if (!found) print "" }' "$STATE"
}

set_line() {
	name=$1
	value=$2

	set -- $(gpiofind "$name" 2>/dev/null) || true
	if [ $# -ne 2 ]; then
		echo "no such gpio line: $name" >&2
		exit 1
	fi

	awk -v n="$name" '$4 != n' "$STATE" > "$STATE.new"
	echo "$1 $2 $value $name" >> "$STATE.new"
	mv "$STATE.new" "$STATE"
}

kill_holders() {
	[ -r "$PIDFILE" ] || return 0
	while read -r pid; do
		kill "$pid" 2>/dev/null || true
	done < "$PIDFILE"
	rm -f "$PIDFILE"
}

apply() {
	kill_holders

	# one holder per gpiochip, otherwise the lines are busy
	for chip in $(awk '{ print $1 }' "$STATE" | sort -u); do
		args=""
		while read -r c l v _; do
			[ "$c" = "$chip" ] && args="$args $l=$v"
		done < "$STATE"
		nohup gpioset -m wait "$chip" $args >/dev/null 2>&1 &
		echo $! >> "$PIDFILE"
	done
	sleep 0.1
}

load

if [ -s "$STATE" ]; then
	:
else
	for n in $(echo "$DEFAULTS" | awk 'NF == 2 { print $1 }'); do
		set_line "$n" "$(echo "$DEFAULTS" | awk -v n="$n" '$1 == n { print $2 }')"
	done
fi

[ $# -gt 0 ] || usage

case "$1" in
qspi1)	set_line "QSPI_SPI#" 1 ;;
spi1)	set_line "QSPI_SPI#" 0 ;;
bcuart)	set_line "BC_UART_SEL" 1 ;;
ser3)	set_line "BC_UART_SEL" 0 ;;
uart01)
	[ $# -eq 2 ] || usage
	set_line "UART_0_1_CROSS#" "$2" ;;
cam)
	[ $# -eq 2 ] || usage
	set_line "CAM_SEL0" "$(( $2 & 1 ))"
	set_line "CAM_SEL1" "$(( ($2 >> 1) & 1 ))" ;;
lcd)
	[ $# -eq 3 ] || usage
	set_line "LCD1_BKLT_EN" "$2"
	set_line "LCD1_VDD_EN" "$3" ;;
bcreset)
	# pulse only, never held: the line is released again after <ms>
	ms=${2:-50}
	set -- $(gpiofind "BC_RST" 2>/dev/null) || true
	[ $# -eq 2 ] || { echo "no such gpio line: BC_RST" >&2; exit 1; }
	gpioset -m time -s 0 -u "$((ms * 1000))" "$1" "$2=1" ;;
status)
	apply
	while read -r _ _ v n; do
		printf '%-15s = %s\n' "$n" "$v"
	done < "$STATE"
	printf '%-15s = %s\n' "BC_RST" "pulse only (bcreset)"
	exit 0 ;;
set)
	shift
	[ $# -ge 2 ] || usage
	while [ $# -gt 0 ]; do
		[ "$(get "$1")" = "" ] && { echo "unknown line: $1" >&2; exit 1; }
		set_line "$1" "$2"
		shift 2
	done ;;
*) usage ;;
esac

apply
