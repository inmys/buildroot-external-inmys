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

All carriers share the same build configuration and boot images.
Select the Linux carrier DTB at runtime with U-Boot's fdtfile variable.
See [SDK usage](../README.md#boot-selection-and-updates).

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

`mcom03-sbl` reads the maintained native JSON configuration,
copies the common module U-Boot device tree into `output/images/uboot-dtb/`,
builds the QSPI and SD boot chains from that configuration and installs:

* `output/images/sbl/sbl-xip-*.bin` — the SBL-XIP images
* `output/images/tl-sbimg/nms-sm-k1892vm21ya-mcom03r-nms-sm-k1892vm21ya.tl-image` — common QSPI package
* `output/images/sd-boot/nms-sm-k1892vm21ya.sbimg` — common SD boot capsule

### `board/nms-sm-k1892vm21ya/`

| Path | Description |
|------|-------------|
| `HARDWARE.md` | hardware description extracted from the Altium projects |
| `ddrinit/nms-sm-k1892vm21ya_defconfig` | LPDDR4 configuration |
| `linux/dts/elvees/*.dts{,i}` | Linux device trees (module + 4 carriers) |
| `linux/*_linux.fragment` | Linux configuration fragment |
| `uboot/mcom03r-nms-sm-k1892vm21ya.dts` | one common module U-Boot tree |
| `nms-sm-k1892vm21ya-sbl-config.json` | SBL build configuration |
| `uboot/nms-sm-k1892vm21ya.env` | readable A/B policy and default fdtfile |
| `rootfs-overlay/` | boot/data setup, one updater command and hardware helpers |
| `genimage.cfg`, `post-build.sh`, `post-image.sh` | image generation |

Linux device trees are copied directly through the standard
`BR2_LINUX_KERNEL_CUSTOM_DTS_DIR=.../linux/dts` mechanism. `external.mk`
tracks their file/directory timestamps as prerequisites of the Linux build
stamp. Ordinary `make` therefore rebuilds and installs DTBs after source
changes, including added/removed carrier files; stale output DTBs are
removed before installation. There is no Linux DTS patch to synchronize.

U-Boot uses `BR2_TARGET_UBOOT_CUSTOM_DTS_PATH` to copy the single module DTS
from `board/.../uboot/`. `external.mk` sets CONFIG_OF_LIST from its name
and tracks sources/directories as build-stamp prerequisites.
Ordinary `make` rebuilds the changed U-Boot tree, then SBL and the SD/QSPI
capsules. SBL tracks installed U-Boot/TF-A/ddrinit stamps. Its DTB copies
are taken directly from the current U-Boot build directory and stale
copies are removed first. The native SBL JSON describes the module boot variants;
carrier selection is the persistent fdtfile environment variable.
U-Boot does not initialize carrier PCIe or external QSPI1 and uses a
conservative 3.3 V/25 MHz SD bootstrap. Linux configures these peripherals.
There are no DTS patches or patch-generation commands.

The U-Boot device trees must be named `mcom03r-*`: the SBL selects the
chip variant from the DTB name (`^mcom03-` → MCOM03, otherwise
MCOM03R), and U-Boot builds the `mcom03r-*` DTBs for `TARGET_MCOM03R`.

### `patches/`

The remaining Linux/U-Boot patches fix drivers and boot behaviour.
The former 0001 patches containing DTS sources have been removed.

SBL patches are applied automatically from `package/mcom03-sbl/` by
Buildroot's package infrastructure. No separate patch hook is needed.
The QSPI fix retains the controller/GPO initialization changes without
bring-up register dumps. The fitted W25Q128FW is supported by the original
SBL JEDEC table; unknown devices are no longer assumed to be 16 MiB.
U-Boot has no external-QSPI1 patches because that controller is disabled
in its common module DTS. Linux QSPI1 support is independent.

### `support/`

| Script | Description |
|--------|-------------|
| `make-sdboot.sh` | pack and verify complete SD BootROM/SBL firmware capsules |
| `make-qspi-image.sh` | generate full QSPI images |

These two helpers use shell and existing host tools. The SBL configuration
is maintained directly as native JSON; there is no TOML conversion step.
Python is still required by upstream Buildroot/U-Boot/SBL tools.

### Boot and update layout

Each A/B rootfs includes its own kernel, all four Linux DTBs and one
extlinux entry using fdtdir. p1 is slot A, p2 is reserved for slot B,
and p3 is persistent ext4 data. Factory SD/eMMC images contain only the
boot area and slot A (516 MiB); the first update populates B. On first
boot S15nmsboot grows p3 to the end of the medium and formats it with label
data. An existing data-labelled filesystem is retained and grown; other
old contents in p3 are replaced. One SWU rootfs payload replaces the inactive slot;
SWUpdate's standard U-Boot backend changes the slot only on success.
S15nmsboot mounts data/SSH keys and selects Linux's env configuration from
the bootloader's /chosen marker. S99nmsconfirm confirms trial boots.

The environment is stored with U-Boot: two 64 KiB copies on QSPI0
(0xFE0000/0xFF0000), or SD (0x300000/0x310000). The SD capsule is bounded
below 3 MiB; filesystems begin at 4 MiB. QSPI U-Boot can load Linux from
SD/eMMC without changing its environment backend. Armbian uses the same
U-Boot with normal distro boot and does not need the NMS A/B layout.
