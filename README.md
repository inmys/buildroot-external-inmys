# br2-external: INMYS_NMS_SM_K1892

Buildroot external tree for the **INMYS NMS-SM-K1892VM21YA V1** SMARC
module based on the ELVEES MCom-03R (К1892ВМ21Я) SoC.

```sh
make -C ../buildroot O=../output BR2_EXTERNAL=$PWD nms_sm_k1892vm21ya_defconfig
```

## Contents

### `configs/nms_sm_k1892vm21ya_defconfig`

AArch64, glibc, 6.18 kernel headers, BusyBox, Linux 6.18 from
`elvees/linux` (`mcom03-6.18.y`), TF-A and U-Boot from the ELVEES
GitHub mirrors, plus the MCom-03 boot chain packages.

Options added by this tree:

| Option | Meaning |
|--------|---------|
| `BR2_NMS_SM_K1892_CARRIER` | default carrier (`nms-sm-evm`) |
| `BR2_NMS_SM_K1892_SBL_CONFIG` | SBL config TOML (ddrinit ↔ DTB pairs) |

### `package/`

| Package | Type | Description |
|---------|------|-------------|
| `elvees-mcom03-tools` | host | `mksbimage`/`otpcreator` and the `mipsel-elvees-elf32` toolchain, extracted from the ELVEES SDK tarball |
| `kconfiglib` | host | kconfiglib frontends (`genconfig`, `defconfig`, ...) used by ddrinit and the SBL |
| `ddrinit` | target | LPDDR4/DDR4 initializer (bare-metal MIPS32, runs on RISC0) |
| `mcom03-sbl` | target | SBL-XIP second bootloader and the `*.tl-image` packages |

`ddrinit` builds two variants of the board configuration: the normal one
(SBL loads it) and the `-bootrom` one (the BootROM loads it). Both are
installed to `output/images/ddrinit-nms-sm-k1892vm21ya{,-bootrom}/`.

`mcom03-sbl` generates the SBL JSON configuration from the TOML file,
copies the U-Boot device trees into `output/images/uboot-dtb/`, builds
the SBL for every (ddrinit, DTB, boot type) combination and installs:

* `output/images/sbl/sbl-xip-*.bin` — the SBL-XIP images
* `output/images/tl-sbimg/*.tl-image` — flashable QSPI packages

### `board/nms-sm-k1892vm21ya/`

| Path | Description |
|------|-------------|
| `HARDWARE.md` | hardware description extracted from the Altium projects |
| `ddrinit/nms-sm-k1892vm21ya_defconfig` | LPDDR4 configuration |
| `linux/*.dts{,i}` | Linux device trees (module + 3 carriers) |
| `uboot/*.dts{,i}` | U-Boot device trees (`mcom03r-nms-*`) |
| `nms-sm-k1892vm21ya-sbl-config.toml` | SBL build configuration |
| `rootfs-overlay/` | `/etc/network/interfaces`, init scripts, motd |
| `genimage.cfg`, `post-build.sh`, `post-image.sh` | image generation |

The device trees are kept here and applied to the upstream ELVEES trees
as patches (`patches/linux`, `patches/uboot`). After editing a DTS,
regenerate the patches:

```sh
support/gen-dts-patches.sh <linux-src> <uboot-src>
```

The U-Boot device trees must be named `mcom03r-*`: the SBL selects the
chip variant from the DTB name (`^mcom03-` → MCOM03, otherwise
MCOM03R), and U-Boot builds the `mcom03r-*` DTBs for `TARGET_MCOM03R`.

### `patches/`

| Patch | Content |
|-------|---------|
| `linux/0001-arm64-dts-elvees-add-NMS-SM-K1892VM21YA-module-and-c.patch` | module DTSI, 3 carrier DTS, `Makefile` entries |
| `uboot/0001-arm-dts-mcom03r-add-NMS-SM-K1892VM21YA-module-and-ca.patch` | U-Boot DTSI, 3 carrier DTS, `Makefile` entries |

### `support/`

| Script | Description |
|--------|-------------|
| `gen_sbl_config.py` | TOML → JSON SBL configuration (stdlib `tomllib`) |
| `gen-dts-patches.sh` | regenerate the DTS patches from `board/` |
