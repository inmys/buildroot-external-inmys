# NMS-SM-K1892VM21YA V1 — hardware notes

Everything in this file was extracted from the Altium projects
(`NMS-SM-K1892VM21YA_V1.ai`, `NMS-SM-EVM_V1.ai`, `NMS_SMARC_TESTER_V1.ai`,
`NMS-ITX-SM-LC-V4.ai`) and from the ELVEES MCom-03R documentation.

> **Designator collisions.** The module Altium project reuses designators
> across sheets (see `REVIEW_FINDINGS.md` in the project root). Always
> resolve a net by *net name*, not by designator. Designators also differ
> between boards: module `D31/D32/D33` are RS2299XTQC16 crossbar
> switches, while on the EVM `D30/D31/D32` are PCA9546/PCA9535/ADT7473.

## 1. Module

| Item | Part | Notes |
|------|------|-------|
| SoC | К1892ВМ21Я (ELVEES MCom-03R) | BGA-1936, 4× Cortex-A53 + RISC0 |
| eMMC | Kioxia THGAMRG7T13BAIL | 16 GB, BGA-153, SDMMC1, 8 bit, 1.8 V |
| QSPI NOR | Winbond W25Q128FWPIG | 16 MiB, QSPI0, 4 bit, 66 MHz |
| LPDDR4 | 2 × Rayson RS512M32LO4D1BDS-46IT | 16 Gbit each, x32, 200-FBGA |
| RTC | NXP PCF8523TK | I2C_INT_1V8, 0x68 |
| GPIO expander | NXP PCAL6416AHF | I2C_INT_1V8, 0x20 |
| I2C mux | NXP PCA9546ABS | I2C3, 0x77 → CAM0/CAM1/LCD |
| I2C mux | NXP PCA9540BGD | I2C2, 0x70 → I2C_INT / I2C_INT_1V8 |
| USB hub | TI TUSB8041IRGCR | USB0 → 4 ports |
| USB-SATA | TI TUSB9261IPVP | USB1 → SATA |
| HDMI TX | ADI ADV7513BSWZ | I2C_INT, 0x39 (and 0x3F for the packet memory) |
| LVDS bridge | TI SN65DSI84ZQER | I2C_INT, 0x2C |
| GbE PHY | 2 × DAP8211RI (D9A/D9B) | RGMII, EMAC0/EMAC1 |
| Board controller | STM32L052C8T6 (D41) | BOOTSEL, power/reset, I2C 0x2a, BC_INT; RTC_INT wired |
| XTAL | 27 MHz (B5), 32.768 kHz (B6) | |
| Crossbar | 3 × RS2299XTQC16 (D31/D32/D33) | SPI/QSPI/UART routing |

### 1.1 Boot mode selection

`BOOTSEL.2/1/0` (X1 pins P125/P124/P123) are read by the board controller
(D41), which drives `BOOT_SEL.2/1/0` (SoC balls BC28/BD28/BD29 = the
`BOOT2/1/0` pins of the SoC). Thus BOOT0/1/2 = 0/1/1 means boot mode 110.
The table below is the one in the module
schematic (`CPU_CFG` sheet); the SMARC column is the value seen on the
`BOOT_SEL2#/1#/0#` pins of the connector, i.e. before the board
controller translation.

| BOOTSEL 2:0 | SMARC 2:0 | Boot device |
|-------------|-----------|-------------|
| 000 | 111 (?) | direct execution from QSPI0 (XIP) |
| 001 | 111 (?) | RISC0 BootROM → QSPI0 |
| 010 | — | RISC0 BootROM → MFBSP0 |
| 011 | 101 | RISC0 BootROM → UART0 (flashing) |
| 100 | 001 | RISC0 BootROM → SDMMC0 |
| 101 | 011 (?) | RISC0 BootROM, fast CPU start from QSPI1 |
| 110 | 011 (?) | RISC0 BootROM → SPI0 (reserved) |
| 111 | — | no boot (RISC0 waits for a debugger) |

The question marks are in the original schematic: the designer was not
sure about the SMARC-side values of those modes.

**ROM verification, 2026-11-10:** the table above transcribes the schematic
and is not an accurate description of the installed ROM's SD modes.
On this K1892VM21YA, `100` selects SDMMC0 initialization at 1.8 V;
`110` implements SDMMC0 initialization at 3.3 V. UART `sdopen` tries
both voltages and successfully reads the installed SD card. The ROM
expects SBIMG at byte offset `0x200`; older `sdcard.img` files put FAT
there. The current SDK reserves 4 MiB for a complete SD boot chain and
builds a RAM-backed SBL-S2 plus volatile U-Boot environment for that path.
STM32 0.14 uses a single boot policy: carrier input 111 -> SoC 001,
all other codes unchanged. Set carrier BOOTSEL=110 for SD at 3.3 V,
111/001 for QSPI0, or 011 for UART0. The uncertain SMARC translation in
the schematic table above is historical and is not the current firmware
policy. Internal AO pull-ups make released inputs read 111. On EVM,
X2 fitted -> 110/SD, removed -> 111/QSPI0; other carriers use all three bits.
See `sdk2/MMC.md` for layout and hardware verification.

`BOOT_SEL.0` and `BOOT_SEL.1` are pulled down (R279/R276 to GND) but
`BOOT_SEL.2` is pulled **up** (R275 to `+1V8_CORE`), so if the board
controller is not running the original assembly gives `100` (MMC at 1.8 V),
not `000`. SD startup requires STM32 to select `110`.
The original PCB pulls `BOOTSEL.2/1/0` to switched `+1V8_CORE`
(R32/R45/R51). On the reworked test module those resistors are removed;
STM32 firmware 0.10 enables internal pull-ups to its VDD, `+1V8_AO`.
An unconnected SMARC connector reads `111` after firmware initialization.

On 2026-11-10 the test module was reworked: R276 removed, R278 fitted
(10K to +1V8_CORE); R275 and R279 retained. With PA5/PA6/PA7 temporarily
high impedance and internal pulls disabled, five GPIO samples read 110.
With the entire STM32 program flash and QSPI0 erased, a four-second cold
power cycle booted Linux from SD. STM32 was then programmed from that
Linux through USART1 factory ROM, using the former fixed-SD firmware.
R290 must remain omitted and R291 fitted to keep DCDC enabled during
STM32 reset. The empty-module cold test is verified on ELVEES-SMARC-CB.

Connectivity evidence: BC.SchDoc R276 `GJFGXZYD:12`, R278 `WOSRJQEA:23`,
net BOOT_SEL.1; independent PCB components R276:43/R278:48, net
BOOT_SEL.1, connect to GND/+1V8_CORE respectively. The rework is a user
assembly change, not an update of the exported schematic's fitted flags.

### 1.2 Serial ports (SMARC SER0..SER3)

| SMARC | X1 pins | SoC UART | Linux | Notes |
|-------|---------|----------|-------|-------|
| SER0 | P129/P130 (+P131/P132 RTS/CTS) | UART3 @0x1660000 | `ttyS3` | RS232 through D23 |
| SER1_DBG | P134/P135 | UART0 @0x1750000 | `ttyS0` | console, see below |
| SER2 | P136/P137 | UART2 @0x1650000 | `ttyS2` | |
| SER3 | P140/P141 | UART1 @0x1640000 | `ttyS1` | |

`SER1_DBG` is connected through the RS2299 crossbar `D33`. Its select
input `UART_0_1_CROSS#` is driven by `GPIO1_PORTA_6` and is pulled high
on the module, which selects **UART0** — the same UART that the RISC0
BootROM uses. This is why the Linux console is `ttyS0` and why the
BootROM console is visible on the same connector.

`SER1` is also wired to the on-board CP210x USB-UART bridge (D11) and,
through R49/R50 and the TXS0108 level shifter D23, to the RS232
transceiver. The EVM additionally routes it to its own USB-UART.

### 1.3 DRAM

Two x32 LPDDR4 devices, one per DDRMC:

```
D27 → DDRMC0 (channels MEMA/MEMB, 16 bit each)
D28 → DDRMC1 (channels MEMC/MEMD, 16 bit each)
```

* Single rank: the Rayson RS512M32LO4D1BDS is a 16 Gbit single-rank die
  (the 32 Gbit RS1G32LO4D2BDS is the dual-rank part). The PCB is wired
  for the dual-rank pinout (`CS1`, `CKE1`, `ZQ1` are routed), but those
  balls are NC on the fitted device.
* Per channel: 8 Gbit, x16, 8 banks, 16 row bits, 10 column bits,
  2 KiB page.
* Total: 2 × 16 Gbit = 32 Gbit = **4 GiB**.
* Speed: 2133 MT/s (the device is rated for 4266 MT/s; the ELVEES
  reference configuration runs at 2133).
* `ZQ0`/`ZQ1` are terminated with 240 Ω (R201/R202, R208/R209),
  `ODT_CA_A/B` with R203/R205.

**DQ mapping is the identity mapping for every byte lane.** The PCB
routes `DDRn_Dx` straight to the device `DQx`, so all eight
`DQ_MAPPING` nibble pairs are `0x76543210`:

| SoC signal | Device ball | Net |
|------------|-------------|-----|
| DDR0_D0..D7 | D27.B2, C2, E2, F2, F4, E4, C4, B4 | MEMA.B0.DQ.0..7 |
| DDR0_D8..D11 | D27.C3, D3, E3, — | MEMA.B0.DM/DQS |
| DDR0_D12..D19 | D27.B11, C11, E11, F11, F9, E9, C9, B9 | MEMA.B1.DQ.0..7 |
| DDR0_D20..D23 | D27.C10, D10, E10, — | MEMA.B1.DM/DQS |
| DDR0_D24..D31 | D27.AA2, Y2, V2, U2, U4, V4, Y4, AA4 | MEMB.B0.DQ.0..7 |
| DDR0_D32..D35 | D27.Y3, W3, V3, — | MEMB.B0.DM/DQS |
| DDR0_D36..D43 | D27.AA11, Y11, V11, U11, U9, V9, Y9, AA9 | MEMB.B1.DQ.0..7 |
| DDR0_D44..D47 | D27.Y10, W10, V10, — | MEMB.B1.DM/DQS |

(The same pattern applies to D28 / DDRMC1.)

### 1.4 QSPI boot flash layout (16 MiB)

| Offset | Size | Content |
|--------|------|---------|
| `0x000000` | 2 MiB | BootROM SBIMG: `sbl-s1` + `ddrinit` + `sbl-s2` |
| `0x200000` | 4 MiB | SBL image slot A: `bl31` + `u-boot` + DTB + `sbl-s3` |
| `0x600000` | 4 MiB | SBL image slot B (update slot) |
| `0xA00000` | 5.875 MiB | free |
| `0xFE0000` | 64 KiB | U-Boot environment (`CONFIG_ENV_OFFSET`) |
| `0xFF0000` | 64 KiB | free |

QSPI0 is wired to the SoC only (`QSPI0.CLK/CS0/IO0-3` connect `D2` to
`D21` and nothing else), so the boot flash cannot be reached through the
SMARC connector. Linux can access its existing partitions through MTD
when silvaco-qspi is loaded (now automatic). It can also be programmed by
the RISC0 BootROM over UART0 or with an external programmer clipped onto
`D21`; for the latter
`make qspi-image` produces a flat 16 MiB image of the whole chip.

### 1.5 I2C topology

```
I2C_PM  (SoC i2c0, 3.3 V)  → SMARC P121/P122
I2C_GP  (SoC i2c1, 3.3 V)  → SMARC S48/S49
SoC i2c2 → PCA9540B @0x70 ─┬─ channel 0: I2C_INT      (3.3 V)
                            └─ channel 1: I2C_INT_1V8  (1.8 V)
SoC i2c3 → PCA9546A @0x77 ─┬─ channel 0: I2C_CAM0
                            ├─ channel 1: I2C_CAM1
                            └─ channel 2: I2C_LCD
```

Devices:

| Bus | Address | Device |
|-----|---------|--------|
| I2C_INT_1V8 | 0x68 | PCF8523 RTC (D8) |
| I2C_INT_1V8 | 0x20 | PCAL6416A GPIO expander (D34) |
| I2C_INT_1V8 | 0x2A | STM32 board controller (D41), firmware 0.7+ |
| I2C_INT | 0x39 / 0x3F | ADV7513 HDMI transmitter (D19) |
| I2C_INT | 0x2C | SN65DSI84 LVDS bridge (D10) |

> The netlist labels the PCA9540B upstream as `I2C2.SCL/SDA` and the
> PCA9546A upstream as `I2C1.SCL/SDA`; the DTS follows the SoC
> controllers (`i2c2` and `i2c3`). Verify on hardware with `i2cdetect`.

### Board controller power/reset policy

D41 firmware 0.9 exposes a PCAL6416-compatible GPIO/IRQ controller at 0x2a.
PCB V1 needs a wire from BC_INT (D41.PB5) to FORCE_RECOV#/GPIO0_PORTA_0.
INT# is open drain and active low, with R293 pull-up to +1V8_CORE; DTS routes
it through gpio0a directly to GIC. The original R179 -> D2.BB29 EXTINT/QLIC
connection is unused by Linux. The next PCB keeps BC_INT on GPIO0_PORTA_0
and moves FORCE_RECOV# to GPIO0_PORTA_1. This is a hardware rework; the V1
netlist still shows the original routing.

Linux uses gpio-pca953x, gpio-keys (KEY_POWER) and standard acpid. RTC INT#
on STM32 PA15 becomes virtual GPIO 4 and an IRQ for rtc-pcf8523. WOL, charger,
battery, test, fan-alert and power-good signals are available as named GPIOs.
SLEEP#/LID# expose PA13/PA14 levels through virtual GPIO 2/3 while preserving
SWD mode. DTS only names them; no Linux input consumers are attached and their
interrupts remain masked until requested. Active SWD traffic cannot be
interpreted as lid/sleep state. Carrier signals on these wires can affect SWD.
The fitted YT8531 PHYs lack an IRQ handler in this kernel; WOL signal exposure
does not enable network wake by itself.

The proprietary Linux nms-bc driver/protocol is removed. Normal reboot uses
PSCI/watchdog. Final shutdown uses standard Linux gpio-poweroff, priority 192,
through a PCB V1 rework: D2.AW19 GPIO1_PORTC_7 / SSI1_SS_IN -> wire -> TP1
(I2C_BC.SCL) -> D41 pin 21 PB10. R100 (CORE pull-up) is removed and R101
(10 kOhm pull-down) is fitted. PB10 is an input with an internal pull-down.
SPI1 no longer muxes SS_IN; its master data and CS0..CS2 remain available.

Firmware 0.11 arms after a stable low and latches a stable high (5 ms each),
then requests OFF after the existing 100 ms command delay. A fresh short
POWER press starts the module. TF-A is unmodified; the former I2C patch and
build hook are removed. Virtual GPIO14/15 requests remain for compatibility.
Five-second POWER hold still forces soft-off. See sdk2/POWEROFF.md for tests.

Evidence: original read-only module export, BC.SchDoc D41
sch-component:BC.SchDoc:WVCQHGYJ:0; CPU_LSP1_MFBSP.SchDoc D2
sch-component:CPU_LSP1_MFBSP.SchDoc:XGSVWFQJ:0. Physical PCB identifiers:
TP1 pcb-component:TP1:173, D41 pcb-component:D41:114, D2 pcb-component:D2:738,
R100 pcb-component:R100:286, R101 pcb-component:R101:277. The original
SSI1_SS_IN and I2C_BC.SCL nets are joined by the user's reported wire;
the export does not include this rework.

The reworked test module was subsequently flashed with firmware 0.12 and
explicit BC_POWER_CUT_DCDC=1. Linux poweroff asserted PB12, PG_3V3 fell,
and the user measured 0 V at L32A.2 (+0V9_CORE), L34.2 (+1V8_CORE),
L31.2 (+3V3_CORE). A real short POWER press restored PG_3V3 and booted Linux
without removing the 12 V input. UART PA9/PA10 and boot PA5..PA7 go analog
with no pulls while CORE is off, and return after power settling.
Firmware 0.13 removed the power-cut option: all presets now disable DCDC
on OFF. Its SD image passed a Linux ROM-loader update and Linux boot;
see sdk2/POWEROFF.md.

Reset groups owned by STM32:

* PB9 -> VT9B -> CPU_RESET#: ELVEES, eMMC, PCAL6416A.
* PB1 -> SMARC P126 RESET_OUT#: carrier peripherals.
* PB13 -> VT7 -> ETH_RESET#: both Ethernet PHYs.
* PA8 -> VT8 -> RESET#: TUSB9261 D16, PCA9546A D29, and through R124
  TUSB8041 D14. PA8 is not spare.

PG_3V3 on PA4 gates release. All carriers wait 500 ms for power/reset,
then 50 ms stable BOOTSEL; there are no fixed-boot build variants.
Peripheral reset is released 20 ms before CPU/carrier reset. RESET_IN#
assertion uses EXTI; release is debounced. Power-good failure leaves resets
asserted instead of cycling the machine indefinitely.

OFF disables DCDC, asserts all reset groups and lowers
CARRIER_PWR_ON/CARRIER_STBY#. STM32 remains powered from AO. The original CORE pull-ups on
BOOTSEL.0/1/2, RESET_IN#, POWER_BTN#, LID#, SLEEP#, CHARGING#,
CHARGER_PRSNT#, BATLOW# and TEST# were removed on the test module.
Firmware 0.10 replaces R32/R45/R51, R33, R34, R18/R19, R21/R22,
R24/R25 with internal STM32 pull-ups supplied by +1V8_AO. PA13/PA14 retain
SWD mode; only their pulls change. These pulls depend on the application
running and do not replace external AO pulls during MCU reset/ROM entry.
Full rail removal still requires powered-off I/O/backfeed validation;
BC_POWER_CUT_DCDC remains 0.
Actual carrier supply switching depends on the carrier implementation.
ELVEES-SMARC-CB routes POWER_BTN# to maintained switch SA10 S5ENA.

Evidence: module BC.SchDoc D41, VT7/VT8/VT9; independent physical PCB nets
42 NetD41_29, 607 RESET#, 67 ETH_RESET#, 95 BC_INT, 696 POWER_BTN#;
The original PCB has R34.1=+1V8_CORE, R257.1=+1V8_AO; the resistor removal
above is the user's assembly rework and is not in that export.
Rail sequencing uses PG_CVDD through
R237/R238/R239, PG_1V8 through R240/R246, and final PG_3V3. These PCB
connections establish routing, not installed resistor variants or measured
power timing. Firmware protocol/build/testing are documented in bc-stm32/README.md.

### 1.6 GPIO expander (D34, PCAL6416A, 1.8 V)

| Pin | Net | Function |
|-----|-----|----------|
| P0_0/P0_1 | LCD0_BKLT_EN / LCD0_VDD_EN | LCD0 backlight / power |
| P0_2/P0_3 | LCD1_BKLT_EN / LCD1_VDD_EN | LCD1 backlight / power |
| P0_4 | QSPI_SPI# | QSPI/SPI routing (crossbar D31-D33) |
| P0_5 | BC_UART_SEL | board controller UART select |
| P0_6 | PCIE_SEL | PCIe configuration |
| P0_7 | HDMI_INT# | ADV7513 interrupt |
| P1_0 | HDMI_PD | ADV7513 power down |
| P1_1 | RTC_32K | input from RTC_INT_CLK via R230 |
| P1_2 | ENABLE_LVDS | SN65DSI84 enable |
| P1_3 | USB1_ID0_GPIO | USB1 ID / GPIO |
| P1_4 | BC_BOOT | board controller boot |
| P1_5 | BC_RST | board controller reset |
| P1_6/P1_7 | CAM_SEL0/1 | camera selection |
| RESET# (24) | CPU_RESET# | expander reset from the SoC |

`RTC_INT#` from the PCF8523 is connected to the board controller (D41),
not to the SoC, so the RTC cannot wake the SoC.

#### RTC power tree, clock output and I2C failure

Re-audited 2026-11-10 against both compiled schematic connectivity and
PCB pad/net assignments. D8 is PCF8523TK/1,118, RTC.SchDoc / AWLADZOR.

```
+1V8_AO -- R58 (10K) --> RTC_VCC --> D8.8 VDD
+3V3_AO -- VD1 --> RTC_VBAT --> D8.3 VBAT
SMARC S147 +VDD_RTC -- VT1 --> RTC_VBAT
                         gate = +3V3_AO
```

There is no cell on the module itself, but VT1 provides a path from the
carrier's VDD_RTC backup supply. R55 is an optional bypass, not the only
backup path; its DNP state does not establish the absence of carrier backup.
Actual supply voltages have not been measured on this sample.

R58 is a 10K series resistor on VDD, a suspect source of voltage sag during
I2C activity. Separately, nominal VDD=1.8 V and VBAT near 3 V satisfy the
PCF8523 standard-mode battery switch condition (VDD<VBAT and VDD<2.5 V
typical). In battery operation its I2C interface is disabled. These are
power-tree concerns, not proof that D8 is dead or absent. PM defaults to
111 (switching disabled); Linux can enable it during probe, but the current
boot fails before that step. Actual PM bits are unreadable, so battery mode
remains an unconfirmed explanation, not an established fault diagnosis.

The user observed 32 kHz on R163 before Linux startup. The clock path is:
D8.7 RTC_INT# -> VT2 gate; VT2 drain/R59 pull-up -> RTC_INT_CLK -> R163
-> D2.BD30 XTI32K_XIN. R57 is DNP in the schematic assembly data. R230
(I2C_GPIO_EXPANDER.SchDoc / KHNGBHWB) also connects RTC_INT_CLK to D34.11,
P1_1. PCB pad nets independently agree with these R230/R163 connections.

The old nms-gpio.sh incorrectly called P1_1 EXP_P1_1 and drove it low.
This clamps the R163 clock node. The script now excludes it from output
state, removes old runtime state and explicitly selects GPIO input mode;
the DTS line name is RTC_32K. Runtime direction input was verified, but
the restored physical waveform still needs an oscilloscope confirmation.

Before the requested cold power cycle, D8 NACKed even a direct read
address on Linux bus 9. Releasing
the clock input did not fix I2C. The earlier categorical diagnosis of an
unfitted/dead chip was unsupported and is withdrawn. Measure D8.8 VDD
(after R58), D8.3 VBAT, and VDD during I2C before selecting a board rework.

RTC is built in. The driver reports -121 while loading capacitance because
an I2C operation fails. It has no SoC IRQ in this DT and does not take its
IRQ-only branch that disables CLKOUT. Changing quartz-load-femtofarads
will not make an unresponsive I2C address acknowledge. The fitted crystal's
load capacitance is not specified by the semantic BOM, so no value was guessed.

See sdk2/RTC.md and the NXP PCF8523 Rev.7 datasheet, sections 8.5 and 12.

### 1.7 Board multiplexers and switches

Switches are named by gpio-line-names in nms-sm-k1892vm21ya.dtsi and
are accessible with gpiofind/gpioset. Board pulls set QSPI_SPI# high
(R224), BC_UART_SEL low (R225), PCIE_SEL low (R199), BC_BOOT/BC_RST low
(R281/R272), and LCD1 enable lines low (R231/R232). The generic nms-gpio
initializer has been removed. Camera startup configures weak PCAL6416
pull-downs on CAM_SEL0/1, which have no external resistors.

| Line | Mux / switch | Values |
|------|--------------|--------|
| P0_4 | `QSPI_SPI#` -> D31/D32 RS2299 | 1 = QSPI1 (socket XS7), 0 = SPI1 |
| P0_5 | `BC_UART_SEL` -> D32 RS2299 | 1 = STM32, 0 = SER3 (SMARC P140/P141) |
| P0_6 | `PCIE_SEL` -> VT5 | PCIe root complex select |
| P1_3 | `USB1_ID0_GPIO` -> VT3 | 1 = `OTG_ID` low = host mode |
| P1_6/P1_7 | `CAM_SEL0/1` -> D1 RS2255XN (SP4T, ENABLE tied to GND) | see below |
| GPIO1_PORTA_6 | `UART_0_1_CROSS#` -> D33 RS2299 | see below |

D1 puts one of the SoC camera clocks onto the shared SMARC S6 pin
(`CAM_CLK_FSYNC`):

| CAM_SEL1 | CAM_SEL0 | S6 carries |
|----------|----------|------------|
| 0 | 0 | `CMOS0_FSYNC` |
| 0 | 1 | `CMOS1_FSYNC` |
| 1 | 0 | `CMOS0_CLK` |
| 1 | 1 | `CMOS1_CLK` |

`UART_0_1_CROSS#` (10K series from the SoC, 10K pull-down R226, active low)
swaps the two debug UARTs. The module annotation gives the table directly:

| UART_0_1_CROSS# | SER3 / board controller | SER1_DBG (SMARC P134/P135) |
|-----------------|-------------------------|----------------------------|
| 0 | UART0 | UART1 |
| 1 | UART1 | UART0 |

A DTS GPIO hog holds this line at `1`, as required for the console: with `0` the console on
`ttyS0` moves to SER3 and the serial port goes silent.

Driven by a driver, not by the script: `LCD0_BKLT_EN` (pwm-backlight),
`HDMI_PD` (adv7511), `ENABLE_LVDS` (sn65dsi83); `HDMI_INT#` is an input.

### 1.8 Board controller reset and DCDC_EN

D41.25 is **PB12**, driving VT7.2 (gate). VT7.6 is the drain on DCDC_EN,
which reaches SY8825 ENABLEA through R236. A high gate pulls DCDC_EN low
and disables the supply; a low gate lets R294 pull DCDC_EN to +3V3_AO.
D41.26/PB13 drives the other half of VT7 and ETH_RESET#, not DCDC_EN.

| Part | Value | Actual connection |
| --- | --- | --- |
| R294 | 10K | DCDC_EN -> +3V3_AO |
| R290 | 10K | VT7.2 gate / D41.25 -> +1V8_AO |
| R291 | 10K | VT7.2 gate / D41.25 -> GND |

R290 and R291 are alternative **gate** pulls. R290 is not connected to
DCDC_EN. With R290 fitted and R291 omitted, an MCU reset can disable the
SoC supply. With R290 omitted and R291 fitted, the pull holds DCDC enabled
while the MCU pins are high impedance. The user confirmed the latter
assembly on 2026-11-09; the previous description of this board was stale.

Source: BC.SchDoc, D41 `WVCQHGYJ:0`, VT7 `DKQHHGKH:25`, R290
`IRNIQQZB:16`, R291 `WOUVYOMV:15`. Independent PCB nets 43
`NetD41_25`, 78 `DCDC_EN`, 99 `+1V8_AO` verify these connections.

Factory ROM programming should use BC_BOOT and BC_RST, followed by
stm32flash over USART1. The generic software jump into System Memory is
unsupported for STM32L0 according to ST AN2606. Check FLASH_OPTR.nBOOT1,
not the nonexistent nBOOT_SEL option used in the old comments. ROM entry and a complete 4876-byte write/read-back verification were
confirmed on this module with R291 fitted/R290 omitted. Linux remained
running until the final application restart. The ROM also configures SPI
pins sharing carrier/power-control signals.
See `bc-stm32/README.md` and its updated helper tools.

### 1.9 Audio

Two independent paths, both described in the device trees and both
verified on hardware.

#### HDMI audio (on the module)

```
SoC I2S0  SCLK_OUT  ball BB18  GPIO1_PORTB_0  ->  D19.9  SCLK
          WS_OUT    ball BA18  GPIO1_PORTB_1  ->  D19.10 LRCLK
          SDO0      ball BD18  GPIO1_PORTB_2  ->  D19.5  I2S0
```

The SoC is the clock master and the ADV7513 is an I2S slave.  Its MCLK
(D19.4), SPDIF (D19.3) and I2S1..3 (D19.6/7/8) inputs are tied to ground
through R191/R190/R187..189, so only the I2S0 path is usable and no
master clock is needed.

`hdmi_sound` is declared in the module dtsi (`simple-audio-card`,
"hdmi-audio", CPU = `i2s0`) and enabled by the carrier.

Note that `sound-dai = <&adv7513>` looks wrong but is correct: since 6.13
the ADV7511 driver no longer registers an ASoC codec, it only exposes DRM
bridge HDMI-audio ops, and `drm_bridge_connector` creates the `hdmi-codec`
platform device as a child of the bridge's i2c device.  ASoC's
`soc_component_to_node()` falls back to `dev->parent->of_node`, so the
phandle of the i2c node resolves to that component.

#### Analog audio (on the carrier)

The ELV-SMARC-CB carries an Everest **ES8327** (DD1):

| Signal | ES8327 pin | Via | SMARC | On the module |
|--------|-----------|-----|-------|---------------|
| CCLK   | 32 | R14, R102 | I2C_GP | `i2c1@1710000` (bus 1) |
| CDATA  | 31 | R15, R103 | I2C_GP | `i2c1@1710000` (bus 1) |
| CE     | 30 | R13 -> AGND | | address **0x18** |
| SCLK   | 5  | R17 | S42 I2S0_CK | `D2.BC23` MFBSP0_LCLK |
| LRCK   | 7  | R19 | S39 I2S0_LRCK | `D2.BA24` MFBSP0_LDAT1 |
| SDIN   | 6  | R18 | S40 I2S0_SDOUT | `D2.AY24` MFBSP0_LDAT3 |
| SDOUT  | 8  | R20 | S41 I2S0_SDIN | `D2.BC24` MFBSP0_LDAT2 |
| MCLK   | 1  | R16 <- S38, R12 <- G1 | AUDIO_MCK | **S38 not connected** |
| INTOUT | 10 | DD29 | P118 | `D2.BD33` GPIO0_PORTD_1 |

SMARC `I2S0*` lands on **MFBSP0**, not on the designware I2S0 that feeds
HDMI - the two audio paths use different controllers.  MFBSP0 is the
bitclock/frame master, exactly as in the ELVEES reference device tree.

The optional carrier `G1` is an `SX2M12.000M20F30TNN` 12.000 MHz
oscillator, but its series link R12 is marked DNP. S38 is not driven by
this module. Describing a fixed 12 MHz MCLK therefore left the actual
ES8326 (chip ID 0x8326, revision 0) silent. Using BCLK as the codec main
clock restored audible playback on hardware. The carrier DTS now selects
that mode with two 32-bit I2S slots and a 64-fs BCLK; see
`sdk2/ANALOG_AUDIO.md` for the evidence, divider limits and validation.

Two things had to be fixed beyond describing the codec:

* `qlic0` is the interrupt controller of the service subsystem, where
  MFBSP0/1 live.  `mcom03r.dtsi` leaves it `disabled`, so `mfbsp-i2s`
  never got its DMA interrupts, never probed, and the sound card failed
  to bind with `asoc-simple-card: parse error`.  The carrier DTS enables
  it, as the ELVEES reference does.
* Jack detection is **not** wired up.  `gpio0d` is not declared as an
  interrupt controller in `mcom03r.dtsi` (only bank A of each GPIO
  controller is), and the GIC SPI numbers of bank D are not documented in
  the SDK, so guessing them could claim an SPI that belongs to another
  peripheral.  The `es8326` driver tolerates a missing IRQ - it warns and
  continues and the sound card registers. Headset detection is unavailable;
  the missing-MCLK issue above required a separate playback fix.

The whole sound stack is built in (`CONFIG_SND*` = `y`) rather than left
as modules: the ELVEES defconfig makes it all `=m`, and although the
modules are installed in the rootfs nothing loads them, so `/proc/asound`
never appeared.  `alsa-utils` (aplay, arecord, amixer, alsactl,
speaker-test) is installed for testing.

### 1.10 Display: LVDS and the LCD0 controls

The module carries the whole display chain, so the carriers only route
SMARC signals to their receptacles.

| SMARC | Signal | Module side |
|-------|--------|-------------|
| S125..S138 | LVDS0 (A.CLK, A.D0..D3) | D10 SN65DSI84 channel A |
| S108..S121 | LVDS1 (B.CLK, B.D0..D3) | D10 SN65DSI84 channel B |
| S127 | LCD0_BKLT_EN | D34 PCAL6416A P0_0 (+ R228) |
| S133 | LCD0_VDD_EN | D34 PCAL6416A P0_1 (+ R229) |
| S141 | LCD0_BKLT_PWM | D2.AW17 = GPIO1_PORTD_3 |
| S107 | LCD1_BKLT_EN | D34 PCAL6416A P0_2 (+ R231) |
| S116 | LCD1_VDD_EN | D34 PCAL6416A P0_3 (+ R232) |
| S122 | LCD1_BKLT_PWM | D2.AV19 = GPIO1_PORTD_4 |

`LCD0_VDD_EN` and `LCD0_BKLT_EN` are modelled as `regulator-fixed`
nodes in the module device tree (`lcd0_vdd_en`, `lcd0_bklt_en`), not
left to GPIO tools: the panel takes the first as its `power-supply`
and the backlight the second, so the power-up order is the driver's
business and the expander lines are not poked behind its back.  Neither
is `regulator-always-on`, so on a carrier without a panel the rails stay
off. GPIO tools must not claim LCD0_VDD_EN or LCD0_BKLT_EN; the former `lcd`
subcommand now only drives the LCD1 pair.

#### The backlight PWM is *not* on the designware timers

This is the one place where the module differs from the ELVEES
reference device trees, and getting it wrong costs the camera reset:

| Pin | Ball | Function 1 |
|-----|------|------------|
| GPIO1_PORTD_0 | BC16 | TIMERS_TOGGLE_0 |
| GPIO1_PORTD_1 | AW16 | TIMERS_TOGGLE_1 |
| GPIO1_PORTD_2 | BC15 | PWM_OENA0 |
| GPIO1_PORTD_3 | AW17 | PWM_OENB0 |
| GPIO1_PORTD_4 | AV19 | PWM_OENA1 |
| GPIO1_PORTD_5 | BC14 | PWM_OENB1 |

The backlight lines land on `PWM_OEN*`, which belongs to the
`elvees,elvees-pwm` block at `0x1770000`, not to `timers_pwm`
(`snps,dw-apb-timers-pwm2` at `0x1790000`).  And `GPIO1_PORTD_1`, the
pin `timer1_toggle_mux` takes over, is `GPIO3_CAM1_RST#` on SMARC P111 -
so the ELVEES recipe (`pwms = <&timers_pwm 1 ...>` plus
`pinctrl-0 = <&timer1_toggle_mux>`) both fails to reach S141 and quietly
takes the camera reset away.

The carriers therefore use

```
&pwm {
        status = "okay";
        elvees,output-channel = <1 0 0 0>;   /* port 0 -> output B */
        elvees,state-on-disable = <1 0 0 0>;
        pinctrl-names = "default";
        pinctrl-0 = <&pwm_oenb0_mux>;
};

backlight { pwms = <&pwm 0 2000000>; ... };  /* 500 Hz */
```

`CONFIG_PWM_ELVEES=y` is forced in the kernel fragment; the ELVEES
defconfig leaves it `=m` and nothing loads modules.  `timers_pwm` stays
disabled.

#### Panel timing

`lvds_panel` is declared in the module dtsi (disabled) so that the
SN65DSI84 output endpoint resolves for every carrier, and enabled by the
carriers that have a receptacle.  The timing is the 7" 800x480 panel
INMYS uses on the RK3588 carriers: 33 MHz pixel clock, JEIDA-24,
231x139 mm, which is also the mode the SMARC tester feeds its LVDS
receivers (7 x 33 = 231 Mbit/s per lane pair).  Only LVDS channel A is
described; a dual-link panel needs a `port@3` on the bridge and a second
port on the panel node.

#### The LVDS clock comes out 1% fast, and that is expected

The bridge does not have a fine divider for its LVDS clock: it derives it
from the DSI clock, and the register that tells it what the DSI clock is,
`CHA_DSI_CLK_RANGE` (0x12), is quantised to **5 MHz** steps.  For this
panel the driver computes

```
DSI_CLK = 33000 kHz * 24 bpp / 4 lanes / 2 (the bus is DDR) = 99 MHz
0x12    = DIV_ROUND_UP(99 MHz, 5 MHz) = 20  ->  the bridge believes 100 MHz
0x0b    = 24 / 4 / 2 - 1 = 2                ->  LVDS divider is 3
LVDS    = 100 MHz / 3 = 33.333 MHz          ->  1.01% above the 33.000 asked
```

99 MHz is not on the 5 MHz grid and the nearest value below it, 95 MHz,
would give 31.667 MHz (-4%), so 33.333 is the best the part can do.

What this does **not** do is change the picture's timing.  The bridge
keeps the line rate tied to the DSI video packets, so the extra 1% shows
up as nine extra clocks of horizontal blanking per line rather than as a
shorter line:

| | mode asks | measured |
|---|---|---|
| visible area | 800 x 480 | 800 x 480 |
| htotal in clocks | 928 | 937 (= 928 x 100/99) |
| line time | 28.12 us | 28.14 us |
| frame time | 14.764 ms | 14.765 ms |
| refresh | 67.73 Hz | 67.7 Hz |

The bridge's own timing registers are programmed with the correct values
(`0x20/0x21` = 800, `0x2c/0x2d` = 48, `0x34` = 40, `0x38` = 40), so the
928 is what it intends; the ninth extra clock is the clock error, not a
timing one.  A panel only sees a pixel clock 1% high, which is well
inside its tolerance.

The SMARC tester's FPGA monitor reports this directly, and its geometry
test fails on the htotal check for this reason alone - everything else
in it passes.  Run it with the module's own bus numbers:

```
LVDS_BUS=1 BRIDGE_BUS=9 BRIDGE_ADDR=0x2d CONN=LVDS-1 sh test_lvds_geom.sh
LVDS_BUS=1 BRIDGE_BUS=9 BRIDGE_ADDR=0x2d CONN=LVDS-1 sh test_lvds_lanes.sh
```

The monitor is at `0x26` on `i2c-1` (`I2C_GP`), not on `i2c-2` as on the
RK3588 module, and the bridge is behind the PCA9540B on `i2c-9`.  The
lane test passes: clock lane 33299 kHz and all four pairs active on
S125/S126, S128/S129, S131/S132 and S137/S138, with channel B silent as
befits a single link.  The bridge's test pattern register (0x3c) takes
effect without a soft reset on this part.

#### Dual link on the tester: the wiring is proven, the picture is not

The module wires **both** SN65DSI84 channels to the connector - A to SMARC
LVDS0 (S125..S138), B to LVDS1 (S108..S121) - and the tester has a FIN1032
receiver on every pair of both, feeding the FPGA:

| | channel A | channel B |
|---|---|---|
| CLK | S134/S135 -> D32 RIN9 | S108/S109 -> D31 RIN6 |
| Y0 | S125/S126 -> D30 RIN1 | S120/S121 -> D30 RIN2 |
| Y1 | S128/S129 -> D30 RIN3 | S111/S112 -> D31 RIN5 |
| Y2 | S131/S132 -> D32 RIN10 | S117/S118 -> D31 RIN7 |
| Y3 | S137/S138 -> D32 RIN11 | S114/S115 -> D31 RIN8 |

So the tester enables dual link in its device tree: the bridge gets a
`port@3`, the panel a `port@1`, and the two ports carry the
`dual-lvds-odd-pixels` / `dual-lvds-even-pixels` markers that
`drm_of_lvds_get_dual_link_pixel_order()` reads.  Channel A carries the odd
pixels, as in the freescale dual-link panels.  This lives in the carrier
and not in the module dtsi because the 7" panel the other carriers drive
is a single-link part.

**What is verified** (`test_lvds_lanes.sh` and the FPGA counters):

* both clock lanes run: `A_CLK = B_CLK = 33300 kHz`;
* all eight data lanes are active with the bridge's test pattern;
* both deserialisers lock, and each channel measures `512` of `1024`
  pixels per line and a DE run of `512` independently;
* the frame is `806` lines at `60.9 Hz`, and the two channels' clocks agree.

The lane test passes.  That is the answer to "does channel B reach the
far end" - it does.

**What does not work** is the picture:

* with `CHA_ACTIVE_LINE_LENGTH` (0x20/0x21) set to the mode's `1024`, the
  split is correct - `512` per channel, `htotal 678`, `806` lines - but DE
  is high for exactly **one line per frame**, so the monitor reads
  `vact = 1` and `de_count = 512`;
* with `512` programmed the vertical comes right (`vact = 768`) but each
  channel carries only `256` pixels, i.e. half the picture is dropped;
* the bridge latches `CHA_SYNCH_ERR` (0xe5 bit 7) throughout and the driver
  resets the pipe every few seconds.

Neither value gives both the right width and the right height, so the
bridge is not assembling dual-link frames from this DSI stream.  The
burst-mode question was checked and is not it: dropping
`MIPI_DSI_MODE_VIDEO_BURST` and the `NO_HFP/HBP/HSA` flags changes nothing.

To fall back to a working picture, take the dual-link block out of
`nms-smarc-tester.dts` - the `port@3` on the bridge, the `port@1` and the
two markers on the panel - and drop the `panel-timing` override so the
module's 800x480 at 33 MHz comes back.  Single link measures correctly:
`800x480`, `DE 384000`, no errors at all.

#### The LVDS clock must stay above 25 MHz

`LVDS_CLK_RANGE` (0x0a bits 3:1) has no setting below 25 MHz - `000` is
25..37.5 MHz - and `sn65dsi83_get_lvds_range()` computes it from
`mode_clock / 2` in dual link.  An 800x480 mode at 33 MHz therefore asks
for **16.5 MHz per channel**, below what the part can generate: the PLL
never settles, `PLL_UNLOCK` latches in 0xe5, and the frame comes out as a
single line.  The driver does not check this - `sn65dsi83_mode_valid()`
limits the mode clock to 25..154 MHz but not the halved one.

The tester therefore runs 1024x768 at **66 MHz**, which gives 33.3 MHz per
channel.  The pixel clock is not free to be chosen either: `disp_pxlclk`
is `media_pll1` (594 MHz) over an integer UCG divider, so only 594/N
exists - 148.5, 118.8, 99, 74.25, 66, 59.4, 54, 49.5, 33 - and
`mcom03_dsi_mode_valid()` demands an exact match.  65 MHz, the obvious
choice, is simply not producible: the mode is rejected and `crtc-0` is
left with an empty mode.

#### The sn65dsi83 reset loop

`REG_IRQ_STAT` (0xe5) is RW1C - the bits stay set until a one is written
back - and `sn65dsi83_handle_errors()` acted on the first read without
clearing it.  The bridge latches `CHA_SYNCH_ERR` while the DSI link starts
up, and resetting the pipe latches another, so one start-up glitch became
an endless loop: `sn65dsi83 9-002d: reset the pipe` every 1.1 s, forever.

Patch `0002-drm-bridge-sn65dsi83-clear-the-latched-error-status-b.patch`
clears what was latched and only resets if it comes back, which is what
the datasheet's init sequence prescribes (write 0xff to 0xe5, wait 1 ms,
re-initialise if it is still not zero).

### 1.11 USB: the hub (D14) and the USB-SATA bridge (D16)

#### Topology

The SoC has two USB 3.0 controllers and they are wired very differently:

| SoC | Balls | Goes to |
|-----|-------|---------|
| USB0 | BC12/BD12 (D+/D-), BC13/BD13/BA13/BB13 (SS) | upstream of the TUSB8041 hub (D14) |
| USB1 | BC10/BD10, BC11/BD11/BA11/BB11 | SMARC OTG (X1.S68/S69, S65/S66, P74) |

The module net names are misleading: what the netlist calls `USB2_UP` and
`USB3_UP` is the hub *upstream* pair and lands on SoC **USB0**, while the
nets called `USB0_*`/`USB1_*` are the hub *downstream* ports on the SMARC
connector.

The hub's four downstream ports:

| Port | Module nets | Goes to |
|------|-------------|---------|
| 1 | `USB2_DP_0`, `USB3_DP_0` (AC-coupled by C288..C291) | TUSB9261 (D16), the USB-SATA bridge |
| 2 | `USB2`, `USB2_SS` | SMARC USB2 (X1.P69/P70, S74/S75) |
| 3 | `USB1` | SMARC USB1 (X1.P65/P66) |
| 4 | `USB0` | SMARC USB0 (X1.P60/P61) |

The ELV-SMARC-CB puts a VIA VL817 hub (`2109:0817` / `2109:2817`) and its
billboard device on SMARC USB2, so a working TUSB8041 brings up that whole
sub-tree as well.

#### The hub does not enumerate until `cfgActive` is cleared

This one cost a long detour and is worth stating plainly.

`SMBUSz` (D14.39) is pulled to GND by R128, which selects **SMBus slave
mode** instead of the pin-strap defaults.  In that mode the hub sets
`cfgActive` (Device Status and Command Register, offset `F8h`, bit 0) and

> does not connect on the upstream port while this bit is 1 [...] this bit
> must be cleared by the SMBus host in order to exit the configuration mode
> and allow the upstream port to connect.  The bit is cleared by a writing
> 1.  A write of 0 has no effect.

The symptom is an xHCI root hub with an empty port and no hub anywhere in
`lsusb`, even though the hub is alive and answers on I2C.

The SMBus slave address is `1000 1x0`, where `x` is the `GANGED`/`SMBA2`
strap.  It is **not the same on every module**: 0x44 has been measured on
one and **0x47** on another, so nothing may hard-code it.  The hub sits on
`I2C_INT`, that is channel 0 of the PCA9540B (bus 8 in the running system):

```
i2cget -y 8 0x44 0x01   # 0x51  \
i2cget -y 8 0x44 0x02   # 0x04   > TI vendor ID 0x0451
i2cget -y 8 0x44 0x03   # 0x40  \
i2cget -y 8 0x44 0x04   # 0x81   > product ID 0x8140
i2cget -y 8 0x44 0x08   # 0x0f  > all four downstream ports used
i2cget -y 8 0x44 0xf8   # 0x01  > cfgActive set, hub is waiting
i2cset -y 8 0x44 0xf8 0x01
i2cget -y 8 0x44 0xf8   # 0x00  > hub connects, the tree appears
```

Current assembly rework (user-confirmed 2026-11-13): R128 is omitted,
so D14 enumerates directly without the SMBus cfgActive handshake. The SDK
removed nms-usbhub and S04nmsusbhub. The procedure above describes the
earlier SMBus-strapped assembly, not a requirement for the current modules.

Bit 1 of the same register is `smbusRst` - also write-1-to-act, and it
reloads the register defaults - which is why the script masks bit 0 rather
than comparing the whole byte.

Clearing `cfgActive` after the hub has already connected does not
disconnect it: the bit is only consulted on the way up.  Writing
`0xF8 = 0x02` therefore puts the register back to `0x01` without
reproducing the original fault, which makes the fix awkward to re-test
without a power cycle.

#### The TUSB9261 needs firmware, and this module has none

The TUSB9261 is an ARM Cortex-M3 with no application flash of its own.  Its
boot code loads an image from the attached SPI flash into RAM, and

> In the absence of an attached SPI flash memory or a valid image in the
> SPI flash memory, the firmware will idle and wait for a connection from a
> USB host through its HID interface.

That flash is **D15 = Winbond W25X10CLUXIG**, 1 Mbit (128 KiB), USON-8
3x2 mm, wired to nothing but the bridge:

| D15 | Signal | D16 pin |
|-----|--------|---------|
| 1 CS# | `SPI_CS0` | 21 |
| 6 CLK | `SPI_SCLK` | 17 |
| 5 DI | `SPI_DATA_O` | 18 |
| 2 DO | `SPI_DATA_IN` | 20 |
| 3 WP#, 7 HOLD# | pulled to 3V3 (R139, R138) | - |

On this module it is **empty**.  Once the hub is up, the bridge enumerates
as

```
0451:926b  "TUSB9260 Boot Loader", serial TUSB9260BL01
  interface 0: class 03 (HID)   -> usbhid
  interface 1: class ff (vendor)
```

which is precisely the HID boot loader described above.  A programmed part
would appear as `0451:9261` with a mass-storage interface instead.

#### Flashing it

The SPI flash is reachable from **nothing but the TUSB9261**: not from any
SoC SPI controller, not from a GPIO, and there are no test points on those
four nets.  The bridge itself hangs off downstream port 1 of the hub, which
is not brought out to the SMARC connector, so no external host can reach it
either.  The only in-circuit path is the HID boot loader, driven by the SoC
itself.  TI's `TUSB926x Flash Burner` is Windows-only and therefore useless
here; the SDK carries the Linux equivalent:

```
tusb926x-flash-burner -f /usr/share/tusb926x/TUSB926x_FW_v1.06_SATA_NO_POLARITY_SWAP.bin
```

The package is `package/tusb926x-flash-burner`, built from
`luxonis/tusb926x-flash-burner`.  Local patch 0001 replaces hidapi with
direct `/dev/hidraw` access (Buildroot's hidapi hard-depends on libudev and
this image has no udev), fixes the signed comparison of
`hidraw_devinfo.product` (0x926b does not fit in `__s16`), claims interface
1 - the vendor interface that owns the bulk endpoints - instead of
interface 0, and stops detaching `usbhid` from the HID interface, which
used to revoke the hidraw handle before the final reset command.
`CONFIG_HIDRAW=y` is forced in the kernel fragment.

A run looks like this:

```
Issuing USB_HID_ENABLE_REPROGRAM command      ... sent
USB_HID_POISON_FLASH                          ... sent
USB_HID_RESET_FLASH_BURNER_DEVICE             ... sent
USB_HID_SETUP_DOWNLOAD_DATA                   ... sent
Sent 52266 bytes to device
USB_HID_RESET_FLASH_BURNER_DEVICE             ... sent
```

and the bridge comes back as `0451:9261 "TUSB9260 USB 3 to SATA"` with a
`TI-DSG TUSB9261` SCSI disk behind it.  The first attempt on this module
stopped after `Sent 52266 bytes` with `Failed to send
USB_HID_RESET_FLASH_BURNER_DEVICE command` - the image was already written
and correct, only the self-reset was missing, so a power cycle brought the
new firmware up.  The interface/detach fix above addresses that, but it has
not been re-tested, because the bridge is now programmed and cannot be put
back into boot loader mode (see below).

Out of circuit it means desoldering D15 and programming it in a USON-8
adapter; the package is far too small for a clip.

**The HID boot loader only exists while the flash holds no valid image.**
Once the bridge runs its firmware it enumerates with a single mass-storage
interface and there is no HID interface left to talk to, so re-flashing
from Linux is not possible - D15 has to be erased out of circuit first.
The `-e` option of the burner is for recovering a half-written device, not
for replacing a working image.

Use the **no-polarity-swap** image.  The datasheet notes that

> the default firmware and reference design for the TUSB9261 have the SATA
> TXP/TXM swapped for ease of routing,

while this module is straight through - `D16.57 SATA_TXP` -> C19 ->
`X1.P48 SATA_TX._P`, `D16.56 SATA_TXM` -> C22 -> `X1.P49 SATA_TX._N` - and
SATA, unlike PCIe, does not negotiate lane polarity.

#### There is no I2C on the TUSB9261

The bridge has no I2C or SMBus interface at all.  Its serial interfaces are
SPI (`SPI_CS0..2`), a UART on `GPIO8`/`GPIO9`, JTAG and twelve GPIOs, and on
this module every one of those except `SPI_CS0` is unconnected
(`NetD16-3/5/6/8..11/14..16/22/23/25..29`).  All of its configuration -
vendor and product ID, port behaviour, LED mapping - lives in the firmware
image, not on a bus.  So "I2C to the TUSB9261" is neither needed nor
possible.

What the straps do say:

| pin | signal | strap |
|-----|--------|-------|
| 13 | GPIO4 / `SELF_POWERED` | R145 to 3V3 = 1 |
| 30, 31 | `FREQSEL[1:0]` | R140 to 3V3 = `11` = 40 MHz, matches B4 DSX321G-40MHz |
| 50 | `USB_VBUS` | divider R142/R143 |
| 2 | PWM0 | `SATA_ACT` to X1.S54 |
| 4 | `GRSTz` | `RESET#` |

#### The shared `RESET#` line

`RESET#` is a wired-OR that resets three parts at once:

| Node | What |
|------|------|
| D16.4 | TUSB9261 `GRSTz` |
| D29.1 | PCA9546A `RESET#` (the camera/LCD I2C mux) |
| D14.50 | TUSB8041 `GRSTz` output, through R124 (a 0R link) |
| VT8.3 | NTA4153N open drain, gate on D41.29 = board controller PA8 |
| R287 | 10K pull-up to +3V3_CORE |

`R284` holds the VT8 gate down, so leaving PA8 as an input releases the line.
Driving PA8 high resets the USB hub, USB-SATA bridge and camera/LCD I2C mux
together. Firmware 0.7 controls this group during startup, carrier reset,
Linux reboot and soft-off; earlier versions left PA8 untouched.

### 1.12 PCIe: why ASPM has to stay off

The SoC has two PCIe root complexes, `PCI0` and `PCI1`, and three SMARC
slots A/B/C behind them.  The module fans them out through a clock buffer
(D5) and a set of switches, and it does **not** wire the CLKREQ# side of
the handshake back to the SoC:

| Signal | Where it goes |
|--------|---------------|
| `PCI0_CLKREQ` (D2.G13) | net `NetD2_G13` - **nothing else on it** |
| `PCI1_CLKREQ` (D2.G15) | net `NetD2_G15` - **nothing else on it** |
| `PCIE_A_CLKREQ#` | X1.P78, D5.3, D5.12, R49.1 |
| `PCIE_B_CLKREQ#` | X1.P77, D5.6, D5.16, R39.1 |
| `PCIE_C_CLKREQ` | D6.3 only - does not even reach the connector |

In an ASPM L1 entry the reference clock is switched off and the endpoint
asserts CLKREQ# to ask for it back.  Here the request stops at the clock
buffer, so the SoC never learns that the endpoint wants the clock and the
link cannot be woken from the endpoint side.

This is invisible with a passive device such as an NVMe drive, but an
Intel WiFi card drops CLKREQ# as soon as its firmware is running and
expects the host to bring the clock back - the result is a stream of PCIe
correctable errors and eventually a dead link.

Current assembly rework (user-confirmed 2026-11-13): the clock-buffer
CLKREQ# wiring has been cut/reworked so both PCIe reference clocks run
continuously. The original schematic connectivity above is retained as
design history. Current extlinux.conf no longer contains pcie_aspm=off
or pcie_port_pm=off. This change assumes the free-running-clock rework.


#### The reference clock is gated by the card in slot B

D5 is a Microchip **SY75604B** PCIe clock buffer (16-lead 3x3 VQFN), not a
switch.  It takes `LPHCSL_100M_A` and fans it out to four outputs, each
with its own active-low enable:

| Pin | Function | Net |
|-----|----------|-----|
| 1,2 | IN_p/n | `LPHCSL_100M_A` |
| 4,5 | OUT0 | `PCIE_A_REFCLK` -> slot A |
| 7,8 | OUT1 | `PCIE_B_REFCLK` -> slot B |
| 10,11 | OUT2 | `PCI0_REF_CLK` -> SoC PCI0 |
| 14,15 | OUT3 | `PCI1_REF_CLK` -> SoC PCI1 |
| 3 | OE0b | `PCIE_A_CLKREQ#` |
| 6 | OE1b | `PCIE_B_CLKREQ#` |
| 12 | OE2b | `PCIE_A_CLKREQ#` |
| 16 | OE3b | `PCIE_B_CLKREQ#` |

So the two CLKREQ# lines from the connector gate the outputs in pairs:
`PCIE_A_CLKREQ#` turns on slot A's clock **and PCI0's**, `PCIE_B_CLKREQ#`
turns on slot B's clock **and PCI1's**.  `OExb` is active low with an
internal 100 kOhm pull-down, i.e. an output runs unless something pulls the
pin high - but `PCIE_B_CLKREQ#` also has `R39` to `+3V3_CORE`, so with an
empty slot B the pin sits high and OUT3 is off.

The SoC's own `PCI1_CLKREQ` pad (`D2.G15`) is not connected to anything, so
nothing on the module can turn PCI1's clock on by itself.

That is why `pcie1` used to hang the kernel on ELV-SMARC-CB: no card in
slot B, no reference clock, the PHY never locks, and the config cycle to a
non-existent endpoint never returns.  The last line printed was
`pci 0001:00:00.0: PME# supported from D0 D1 D3hot`.

With a card in slot B the clock is present and PCI1 enumerates normally.
Verified on NMS_SMARC_TESTER with `num-lanes = <2>`:

```
mcom03-pcie 2400000.pcie: PCI host bridge to bus 0001:00
pci 0001:01:00.0: [8086:095a] type 00 class 0x028000 PCIe Endpoint
```

`pcie1` therefore stays `status = "disabled"` in the module dtsi and is
enabled only by carriers that are known to have slot B populated.

D6 is a second SY75604B on `LPHCSL_100M_B`.  Its OUT0 (`PCIE_C_REFCLK`,
slot C) is gated by `PCIE_C_CLKREQ`, a one-pin net that does not reach the
connector - with no pull resistor the internal pull-down wins and slot C's
clock is always on.  Its OUT1 is tied off (`OE1b` = `VCC_3V3_FO2`) and
unused, and OUT2/OUT3 are `USB0_REF_CLK` / `USB1_REF_CLK` with their enables
left floating, so they run unconditionally.

#### Only two of the three cards can ever be enumerated

SMARC PCIe A/B/C/D are the four lanes of one x4 link, not four links, and
the SoC has no bifurcation (datasheet ch. 25.1.1: automatic lane reversal
only; `sdr_pci0_ctl` in table 6.21 has no lane-mode field).  The module
splits them across two controllers:

| Slot | Lanes | Controller |
|------|-------|------------|
| A | `PCI0_TX0/RX0` (C11/B11, C13/B13) | PCI0, direct |
| B | `PCI0_TX1/RX1` (B10/A10, B12/A12) | PCI0, direct |
| C | `PCI0_TX2/RX2` or `PCI1_TX0/RX0` | D25/D26 mux |
| D | `PCI0_TX3/RX3` or `PCI1_TX1/RX1` | D25/D26 mux |

The mux select is `PCIE_SEL` (PCAL6416A P0_6) driving `VT5`, whose drain
pulls `NetD25-6` low; `R200` holds it at `+3V3_CORE` when `VT5` is off, so
the default `PCIE_SEL = 0` selects **PCI1** for slots C and D.  Setting it
to 1 hands C/D back to PCI0 lanes 2/3 and leaves PCI1 with nothing.

A PCIe link is point to point, so a controller that trains to x1 talks to
one card only - the second lane of that controller reaches a card that is
not part of any link and can never be enumerated.  With cards in A, B and
C the result is exactly two devices:

```
-[0000:00]---00.0-[01-ff]----00.0   8086:095a  (slot A)
-[0001:00]---00.0-[01-ff]----00.0   8086:095a  (slot C)
```

The card in slot B is electrically alive - it is the one asserting
`PCIE_B_CLKREQ#` that makes PCI1 work at all - but it shares PCI0's link
with slot A and stays invisible.  Three simultaneous cards need a PCIe
switch on the carrier.

#### The carrier `reset-gpios` that never did anything

All three carrier device trees used to carry

```
&pcie0 {
	reset-gpios = <&gpio0b 4 GPIO_ACTIVE_LOW>;
};
```

The GPIO was wrong: GPIO0_PORTB_4 / BB34 is WDT_TIME_OUT# (SMARC S145).
The earlier explanation about the property spelling was also wrong:
pcie-designware.c uses devm_gpiod_get_optional(dev, "reset", ...), which
accepts the standard reset-gpios property. The singular spelling in the
pcie-mcom03.c warning is stale.

The connector reset outputs are GPIO0_PORTD_7 / BA32 -> D22 -> PCIe A,
and GPIO0_PORTD_6 / AW33 -> D23/D24 -> PCIe B/C. These are now described
as active-low reset-gpios in the module DTSI. Native PCI0_PERSTN / H12
and PCI1_PERSTN / H16 are separate input pads, grounded through R102/R103;
deasserting the internal controller resets does not drive the connector
outputs. Evidence: schematic-compiled PCIe sheet nets PCI0_PERSTN_1V8,
PCI1_PERSTN_1V8 and PCIE_A_RST#, D2/D22/D23/D24/R102/R103 in project.sqlite.

### 1.13 WiFi in the M.2 slot

The M.2 slot is on PCIe0 and has been seen with two different cards:

| Card | PCI ID | Driver | Firmware |
|------|--------|--------|----------|
| Intel AX210 (Typhoon Peak) | `8086:2725` | `iwlmvm` | `iwlwifi-ty-a0-gf-a0-*.ucode` + `.pnvm` |
| Intel Dual Band Wireless-AC 7265 (Stone Peak 2) | `8086:095a` | `iwlmvm` | `iwlwifi-7265D-29.ucode` |

Both blobs are installed (`BR2_PACKAGE_LINUX_FIRMWARE_IWLWIFI_6E` and
`..._IWLWIFI_7265D`) and the driver picks by PCI ID, so the slot can be
swapped without rebuilding.  Note that linux-firmware 20251011 ships only
`iwlwifi-7265D-29.ucode` for the 7265D; the driver probes 29 down to 22 and
29 is the one that works.

#### Why the wireless stack is modular

Every other driver in this image is built in, but `cfg80211`, `mac80211`,
`iwlwifi` and `iwlmvm` are modules.  The reason is purely a boot ordering
problem:

```
[    2.861889] iwlwifi 0000:01:00.0: Detected Intel(R) Dual Band Wireless-AC 7265
[    2.870106] iwlwifi 0000:01:00.0: Direct firmware load for iwlwifi-7265D-29.ucode failed with error -2
[    3.483063] VFS: Mounted root (ext4 filesystem) on device 179:2.
```

A built-in `iwlwifi` probes from its PCI driver initcall, which runs before
the root filesystem is mounted, so the firmware is not reachable yet and no
`wlan0` appears.  `cfg80211` has exactly the same problem with
`regulatory.db`.  There is no initramfs, so the only way to have the
firmware available in time is to load the driver after boot:

```
/etc/modules-load.d/iwlwifi.conf   ->  iwlmvm
```

`S11modules` reads that file.  `iwlmvm` is the module that registers the PCI
driver and `modprobe` pulls in `iwlwifi`, `mac80211` and `cfg80211` through
`modules.dep`.  With this in place the same card logs

```
[    5.289027] iwlwifi 0000:01:00.0: loaded firmware version 29.9ef079ed.0 7265D-29.ucode op_mode iwlmvm
[    5.347688] iwlwifi 0000:01:00.0: base HW address: 4c:34:88:4a:28:83, OTP minor version: 0x0
```

and `iw dev wlan0 scan` returns access points.

Because `CONFIG_IWLWIFI=m` makes `IWLWIFI_OPMODE_MODULAR` default to `y`,
`iwlmvm` is a separate `.ko`; it cannot be folded into `iwlwifi.ko` because
the symbol is not user-selectable.

#### Regulatory domain

`regulatory.db` is installed by `BR2_PACKAGE_WIRELESS_REGDB`, but when
`cfg80211` was built in it never loaded, for the same boot-ordering reason.
Now that `cfg80211` is a module it loads after boot and the database is
picked up.  `CONFIG_CFG80211_REQUIRE_SIGNED_REGDB` stays at its default `y`;
it cannot be switched off from a fragment because it is only user-visible
behind `CFG80211_CERTIFICATION_ONUS`.

## 2. NMS-SM-EVM V1 carrier (default)

| Item | Part | Notes |
|------|------|-------|
| I2C GPIO expander | PCA9535PW (D31) | I2C_GP, 0x20 |
| I2C switch | PCA9546APW (D30) | I2C_GP, 0x70 |
| Fan controller | ADT7473ARQZ (D32) | I2C_GP, 0x2E |
| Level shifter | TXS0108EPWR (D29) | |
| HDMI level shifter | TPD12S016 (D1) | |
| RJ45 | 2 × TRJG16464ADNL (X7/X8) | |
| Connectors | X33 PCIe x4, X19 miniPCIe, X9 M.2, X34 fan | |

* `GPIO5_PWM_OUT` and `GPIO6_TACHIN` are **not** connected to the SoC;
  the fan is controlled by the ADT7473 over I2C.
* The carrier DTS adds: `pca9535@0x20`, `adt7473@0x2e` and
  `pca9546@0x70` on `i2c1` and the ADV7513 HDMI output.  There is no PCIe0
  reset GPIO - see §1.12.

## 3. NMS_SMARC_TESTER V1

Bring-up carrier with a single GbE port, HDMI, USB and a set of
test points. See `linux/dts/elvees/nms-smarc-tester.dts`.

The tester is driven by a separate board (an NMS-SM-EVM with an
NMS-SM-RK3568 module) that provides the module's console and network:

| Item | Value |
|------|-------|
| Module console on the host board | `/dev/ttyS5`, 115200 8N1 |
| Module Ethernet | wired to the host board's `eth1`, not to the lab LAN |
| Host board `eth1` | `172.16.147.1/16`, no DHCP server |
| Module Ethernet | `udhcpc`, so it comes up without an address |

Reach the module with `ssh -J root@<host> root@<module>`; after every reboot
the address has to be typed on the console first, because nothing on the
host's `eth1` answers DHCP.

**The interface name is not stable.**  The module's own port is
`10200000.ethernet` and it is normally `eth0`, but when the Realtek USB
dongle is plugged into the OTG port its `r8152` driver can register first
and take `eth0`, pushing the SoC ports to `eth1`/`eth2`.  Check with

```
for i in /sys/class/net/*; do echo "$i $(readlink -f $i/device 2>/dev/null)"; done
```

before typing the address - `ifconfig eth0 172.16.147.147` on the dongle
leaves the module unreachable.

Measured on this carrier:

* the TUSB8041 hub answers at **0x47**, not at the 0x44 seen on other
  modules; current assemblies omit R128 and need no SMBus release (see §1.11);
* the TUSB9261 boot loader is reachable and the bridge flashes normally;
  with a 128 GB SATA SSD attached it enumerates as `0451:9261` and `sda`
  reports 250069680 sectors;
* PCIe0 enumerates the M.2 card and `iwlwifi` brings up `wlan0` (see §1.13),
  on the earlier assembly with an ASPM workaround; current reworked clocks
  no longer require it (see §1.12);
* LVDS runs as a **dual link** here, which is the only way to exercise
  channel B: both clock lanes measure 33300 kHz and all eight data lanes
  are active, but the bridge does not assemble a correct frame from this
  DSI stream yet - see §1.10 for what works, what does not, and how to fall
  back to single link;
* **PCIe1 works here** because this carrier has a card in SMARC PCIe B, which
  is what gates `PCI1_REF_CLK` (see §1.12).  With `pcie1` enabled and
  `num-lanes = <2>` a second card enumerates as `0001:01:00.0` and `wlan1`
  comes up.  Both links train to x1, so exactly two of the three cards are
  visible - the one in slot B shares PCI0's link with slot A.

## 4. NMS-ITX-SM-LC-V4

mini-ITX carrier: 2 × GbE, HDMI, DP (via a DP-to-VGA bridge), DSI/LVDS,
M.2 SATA, CAN (buffered to UART), I2S audio, fan control, I2C GPIO
expander and a carrier ID EEPROM. See `linux/dts/elvees/nms-itx-sm-lc-v4.dts`.


### NMS-SM-EVM X12 camera routing

X12 (15-pin FPC, csi.SchDoc, X12/TWOFTRWF) carries SoC **CSI1 lanes 0/1**
through SMARC X6.P7/P8/P10/P11 and clock P3/P4. Its control bus is
**I2C_CAM0** through VT3/VT4, so Felix uses **Sensor1_i2c=5**.
PWDN is **carrier D31 P04**, net CAM0_PWDN# through D21 to X12.11;
there is no independent reset. See sdk2/CSI.md for the schematic evidence,
sensor_phy numbering fix and runtime IMX219/OV5647/IMX477 selection.
OV5647 capture on X12 was verified live on 2026-11-10. D31 is on I2C_GP
(Linux bus 1), not I2C_PM: i2c_gpio.SchDoc D31/YFXNEUGD, SCL/SDA through
R135/R136 to I2C_GP_3V3.SCL/SDA. The old carrier DTS put D31 on bus 0,
causing -EREMOTEIO and preventing camera startup. Moving it to &i2c1 fixes
GPIO discovery and automatic capture after reboot. A subsequent IMX219
swap also worked: automatic IMX219_1 mode 2 capture at 15 fps, confirmed
by the user and read-only runtime checks (see sdk2/CSI.md).


EVM LVDS brightness (2026-11-10): the carrier's backlight levels are now
ascending (indices 0..11), with 0 off, 11 maximum and default 6 (50% duty).
Index 5 requests 40% duty and index 9 requests 80%; the old descending
table inverted the sysfs control. PWM stays at 500 Hz.


### Linux SSI exposure

SSI0 has two routed chip selects (X1.P43/P31), exposed as
/dev/spidev2.0 and .1. SSI1 has three through the D31/D32 crossbar,
exposed as /dev/spidev3.0, .1 and .2. SSI1 CS3 is GPIO0_CAM0_PWR# and
is excluded to preserve camera operation. Vendor bus aliases reserve
spi0/spi1 for QSPI0/QSPI1. See sdk2/SPI.md for schematic evidence and tests.


RTC POR follow-up: with the driver disabled, the user removed main and
backup power and restarted. D8 then acknowledged all manual register
reads: Control_3=0xe0 (PM=111), Control_1=0, CLKOUT control=0. Seconds
advanced 47 -> 49 over two seconds with OS still set. Basic RTC hardware
therefore works in the reset-default mode. No RTC configuration was
written in this experiment. See sdk2/RTC.md for interpretation.


RTC confirmed fix (2026-11-10): a controlled raw write of PM=000 was
ACKed, and all subsequent RTC reads immediately NACKed. The module DTSI
now enables D8 with nxp,disable-battery-switchover; the local driver patch
selects PM=010 (VDD-only) and blocks userspace enabling direct/standard
switching. The first boot on kernel 6.18.18 #25 showed Control_3=0x40,
and hwclock initialized/read time successfully. A warm reboot retained
time and hctosys set the Linux clock from RTC. VBAT backup is disabled
by this software workaround. The GPIO RTC_32K remains input. See sdk2/RTC.md.
