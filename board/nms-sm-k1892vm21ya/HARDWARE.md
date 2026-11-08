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
| Board controller | STM32L052C8T6 (D41) | BOOTSEL, reset, RTC_INT, BC_RST |
| XTAL | 27 MHz (B5), 32.768 kHz (B6) | |
| Crossbar | 3 × RS2299XTQC16 (D31/D32/D33) | SPI/QSPI/UART routing |

### 1.1 Boot mode selection

`BOOTSEL.2/1/0` (X1 pins P125/P124/P123) are read by the board controller
(D41), which drives `BOOT_SEL.2/1/0` (SoC balls BD29/BD28/BC28 = the
`BOOT0/1/2` pins of the SoC). The table below is the one in the module
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

`BOOT_SEL.0` and `BOOT_SEL.1` are pulled down (R279/R276 to GND) but
`BOOT_SEL.2` is pulled **up** (R275 to `+1V8_CORE`), so if the board
controller is not running the SoC always sees `100` (RISC0 BootROM from
SDMMC0), not `000`. `BOOTSEL.2/1/0` are pulled up to `+1V8` (R32/R45/R51),
so an unconnected SMARC connector reads `111`.

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
SMARC connector. It is programmed either by the RISC0 BootROM over
UART0 or with an external programmer clipped onto `D21`; for the latter
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
| I2C_INT | 0x39 / 0x3F | ADV7513 HDMI transmitter (D19) |
| I2C_INT | 0x2C | SN65DSI84 LVDS bridge (D10) |

> The netlist labels the PCA9540B upstream as `I2C2.SCL/SDA` and the
> PCA9546A upstream as `I2C1.SCL/SDA`; the DTS follows the SoC
> controllers (`i2c2` and `i2c3`). Verify on hardware with `i2cdetect`.

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
| P1_1 | EXP_P1_1 | spare |
| P1_2 | ENABLE_LVDS | SN65DSI84 enable |
| P1_3 | USB1_ID0_GPIO | USB1 ID / GPIO |
| P1_4 | BC_BOOT | board controller boot |
| P1_5 | BC_RST | board controller reset |
| P1_6/P1_7 | CAM_SEL0/1 | camera selection |
| RESET# (24) | CPU_RESET# | expander reset from the SoC |

`RTC_INT#` from the PCF8523 is connected to the board controller (D41),
not to the SoC, so the RTC cannot wake the SoC.

#### RTC power tree and the state of D8

```
+1V8_AO --[R58]--> RTC_VCC ---------------------> D8.8 Vcc
+3V3_AO --[VD1]--> RTC_VBAT --+--[C25,C26]-----> D8.3 Vbat
                              +--[VT1 S]  (P-FET, G = +3V3_AO, D = +VDD_RTC)
+3V3_AO -----------------------> VT1 G
```

There is **no backup cell** on the module: `Vbat` is fed from the
always-on 3.3 V rail through `VD1`, and `R55` (the optional link to
`+VDD_RTC`) is not fitted.  The RTC therefore keeps time only as long as
`+3V3_AO` is present; there is nothing to buffer a real power cycle.
Note also that `Vbat` (~2.9 V) sits above `Vcc` (1.8 V), so the chip
runs permanently in battery-switchover mode.

Kernel support had to be added explicitly: the ELVEES mcom03r defconfig
enables no RTC at all, so `/sys/class/rtc` was empty.  The fragment now
sets `CONFIG_RTC_CLASS`, `CONFIG_RTC_DRV_PCF8523`, `CONFIG_RTC_HCTOSYS`
and `CONFIG_RTC_SYSTOHC`, and the driver binds (`rtc-pcf8523 9-0068:
registered as rtc0`).

**On the second EVM sample the chip does not answer at all.**  Every
other device on the same 1.8 V mux channel ACKs, and only `0x68` is
missing:

```
$ i2cdetect -y -r 9
20: UU -- -- -- -- -- -- -- -- -- -- -- -- UU -- --
30: -- -- -- -- -- -- -- -- UU UU -- -- UU -- -- UU
50: UU -- -- UU -- -- -- -- -- -- -- -- -- -- -- --
70: UU -- -- -- -- -- -- --
```

`i2cget`/`i2cset` return `Read failed`/`Write failed` even with the
driver unbound, and the probe fails with `-121` (`EREMOTEIO`, i.e. an
I2C NACK):

```
rtc-pcf8523 9-0068: failed to set xtal load capacitance: -121
rtc-pcf8523 9-0068: probe with driver rtc-pcf8523 failed with error -121
```

So this is a hardware fault (unfitted, dead or unsoldered D8), not a
software one.  The software side is ready for a board where the chip
responds.

### 1.7 Board multiplexers and switches (no kernel driver)

All of these are driven from user space by `/usr/sbin/nms-gpio.sh`, which
resolves every line with `gpiofind` against the `gpio-line-names` in
`nms-sm-k1892vm21ya.dtsi`.

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

`1` is the default and is required for the console: with `0` the console on
`ttyS0` moves to SER3 and the serial port goes silent.

Driven by a driver, not by the script: `LCD0_BKLT_EN` (pwm-backlight),
`HDMI_PD` (adv7511), `ENABLE_LVDS` (sn65dsi83); `HDMI_INT#` is an input.

### 1.8 Board controller reset kills the SoC power (`DCDC_EN`)

**Resetting the STM32 (D41) removes `DCDC_EN` and the ELVEES SoC loses
power.** The board controller is part of the power-enable chain, so it
cannot be reset from the SoC it is powering.

Circuit, from the netlist:

```
DCDC_EN   = R236.1 (0R -> ENABLEA) + R294.1 + VT7.6 (drain)
+3V3_AO   = R294.2 (10K)
NetD41-25 = D41.25 + VT7.2 (gate) + R291.2
GND       = VT7.1, VT7.4 (source), R291.1
```

VT7 is an `NTJD4001NT1G` dual N-channel MOSFET; the second half drives
`ETH_RESET#` from D41.26. **R291 is a 10K pull-down on the gate, not a
series resistor.** When the STM32 is held in reset its D41.25 output goes
high-impedance, VT7 turns off, `DCDC_EN` is released and the rail drops.

Stuffing options on `DCDC_EN`:

| Part | Value | Connection | Stock BOM |
|------|-------|------------|-----------|
| R294 | 10K | `DCDC_EN` -> `+3V3_AO` | fitted |
| R290 | 10K | `DCDC_EN` -> GND, marked "VM21YA encoding" | **DNP** |
| R291 | 10K | VT7 gate -> GND | fitted |

**This module has R291 removed and R290 fitted**, and a reset of the STM32
still takes the SoC down.

Consequence: the STM32 system bootloader cannot be entered from the SoC.
`BC_BOOT` and `BC_RST` exist and `nms-gpio.sh` can drive them, but the
reset that the bootloader needs is the same reset that removes the power.
Use SWD (`bc-stm32/tools/bc-probe.py`, `st-util`) instead.

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

The master clock always comes from the carrier: `G1` is an
`SX2M12.000M20F30TNN` 12.000 MHz oscillator, and it is also routed to
S38 through R16.  `audio_mclk` in the module dtsi is therefore a 12 MHz
`fixed-clock`.

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
  continues - so playback and capture work, only headset detection does
  not.

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
left to `nms-gpio.sh`: the panel takes the first as its `power-supply`
and the backlight the second, so the power-up order is the driver's
business and the expander lines are not poked behind its back.  Neither
is `regulator-always-on`, so on a carrier without a panel the rails stay
off.  `nms-gpio.sh` therefore no longer touches `LCD0_VDD_EN`; the `lcd`
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

`nms-usbhub` does exactly this and `S04nmsusbhub` runs it at boot.  It
derives the adapter number from the PCA9540B device tree node (the kernel
names the child adapter `i2c-<parent>-mux (chan_id 0)`) and then probes
0x44..0x47, confirming the TI vendor and product IDs before writing
anything.  There is deliberately **no** `hub@NN` node in the device tree: a
`reg` property would be wrong on half the boards, and the mux node is a
stable anchor that does not depend on the strap.

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
| VT8.3 | NTA4153N open drain, gate on D41.29 = board controller PB11 |
| R287 | 10K pull-up to +3V3_CORE |

`R284` holds the VT8 gate down, so a board controller that leaves PB11 as
an input keeps the line released.  Driving PB11 high resets the USB hub,
the USB-SATA bridge and the camera/LCD I2C mux together - which is exactly
what `bc-stm32` does *not* do today.

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

The workaround is a kernel command line option, applied to every extlinux
label:

```
pcie_aspm=off pcie_port_pm=off
```

`pcie_aspm=off` keeps the link at L0 and stops CLKREQ# from ever being
deasserted; `pcie_port_pm=off` keeps the root port out of D3hot for the
same reason.  Both are recorded in `NMS_CMDLINE` in `nms-ab`, and
`nms-postupdate` re-applies them to `extlinux.conf` after every update -
the boot configuration lives on the FAT partition, which an A/B update does
not otherwise touch, so a board flashed before the option existed would
keep booting without it.

Once the CLKREQ# pad is wired to the module (a rework on the SoC side, not
on the carriers) the option can be dropped and ASPM re-tested.

#### The carrier `reset-gpios` that never did anything

All three carrier device trees used to carry

```
&pcie0 {
	reset-gpios = <&gpio0b 4 GPIO_ACTIVE_LOW>;
};
```

which is wrong twice over.  `drivers/pci/controller/dwc/pcie-mcom03.c`
looks for `reset-gpio`, singular, and warns `'reset-gpio' isn't specified
in PCIe node` when it is absent - so the property was silently ignored.
And `GPIO0_PORTB_4` is ball BB34, which is `WDT_TIME_OUT#` on SMARC S145,
not a PCIe reset: had the property name been right, the driver would have
driven the carrier watchdog line.  The nodes are now plain
`status = "okay"` and the controller drives the `PCIx_PERSTN` pad itself.

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
| I2C GPIO expander | PCA9535PW (D31) | I2C_PM, 0x20 |
| I2C switch | PCA9546APW (D30) | I2C_GP, 0x70 |
| Fan controller | ADT7473ARQZ (D32) | I2C_GP, 0x2E |
| Level shifter | TXS0108EPWR (D29) | |
| HDMI level shifter | TPD12S016 (D1) | |
| RJ45 | 2 × TRJG16464ADNL (X7/X8) | |
| Connectors | X33 PCIe x4, X19 miniPCIe, X9 M.2, X34 fan | |

* `GPIO5_PWM_OUT` and `GPIO6_TACHIN` are **not** connected to the SoC;
  the fan is controlled by the ADT7473 over I2C.
* The carrier DTS adds: `pca9535@0x20` on `i2c0`, `adt7473@0x2e` and
  `pca9546@0x70` on `i2c1` and the ADV7513 HDMI output.  There is no PCIe0
  reset GPIO - see §1.12.

## 3. NMS_SMARC_TESTER V1

Bring-up carrier with a single GbE port, HDMI, USB and a set of
test points. See `linux/nms-smarc-tester.dts`.

The tester is driven by a separate board (an NMS-SM-EVM with an
NMS-SM-RK3568 module) that provides the module's console and network:

| Item | Value |
|------|-------|
| Module console on the host board | `/dev/ttyS5`, 115200 8N1 |
| Module Ethernet | wired to the host board's `eth1`, not to the lab LAN |
| Host board `eth1` | `172.16.147.1/16`, no DHCP server |
| Module `eth0` | `udhcpc`, so it comes up without an address |

Reach the module with `ssh -J root@<host> root@<module>`; after every reboot
`ifconfig eth0 172.16.147.147` has to be typed on the console first, because
nothing on `eth1` answers DHCP.

Measured on this carrier:

* the TUSB8041 hub answers at **0x47**, not at the 0x44 seen on other
  modules - the address is strap-dependent and `nms-usbhub` probes the whole
  0x44..0x47 window (see §1.11);
* the TUSB9261 boot loader is reachable and the bridge flashes normally;
  with a 128 GB SATA SSD attached it enumerates as `0451:9261` and `sda`
  reports 250069680 sectors;
* PCIe0 enumerates the M.2 card and `iwlwifi` brings up `wlan0` (see §1.13),
  with `pcie_aspm=off` because CLKREQ# is not wired (see §1.12).

## 4. NMS-ITX-SM-LC-V4

mini-ITX carrier: 2 × GbE, HDMI, DP (via a DP-to-VGA bridge), DSI/LVDS,
M.2 SATA, CAN (buffered to UART), I2S audio, fan control, I2C GPIO
expander and a carrier ID EEPROM. See `linux/nms-itx-sm-lc-v4.dts`.
