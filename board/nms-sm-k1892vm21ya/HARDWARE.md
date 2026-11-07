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
  `pca9546@0x70` on `i2c1`, the ADV7513 HDMI output and the PCIe0
  reset GPIO (`GPIO0_B4`).

## 3. NMS_SMARC_TESTER V1

Bring-up carrier with a single GbE port, HDMI, USB and a set of
test points. See `linux/nms-smarc-tester.dts`.

## 4. NMS-ITX-SM-LC-V4

mini-ITX carrier: 2 × GbE, HDMI, DP (via a DP-to-VGA bridge), DSI/LVDS,
M.2 SATA, CAN (buffered to UART), I2S audio, fan control, I2C GPIO
expander and a carrier ID EEPROM. See `linux/nms-itx-sm-lc-v4.dts`.
