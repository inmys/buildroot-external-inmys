# NMS-SDM-AM335x / Buildroot

Модуль с 256 MiB DDR и 512 MiB NAND, несущая VAR CustomBoard.
Buildroot 2025.05.3, Linux 5.10.162-cip24.

## Первая сборка

Зависимости Debian/Ubuntu:

```sh
sudo apt-get install build-essential git bc bison flex cpio file \
    libncurses-dev patch rsync unzip wget binutils-arm-none-eabi
```

```sh
mkdir sdk2
cd sdk2
git clone --branch 2025.05.x https://gitlab.com/buildroot.org/buildroot.git
git -C buildroot checkout 85c588cdf9bafa37eb2a2c24e50ffe070e85d2b9
git clone --branch nms-sdm-am335x --single-branch git@github.com:inmys/buildroot-external-inmys.git

make BR2_EXTERNAL="$PWD/buildroot-external-inmys" -C buildroot O="$PWD/output" br_defconfig
make -C output
```

Образ — `output/images/sdcard.img`. Записать на всю SD-карту, вставить её
в VAR CustomBoard, зажать SW3 и подать питание. Консоль: 115200 8N1,
вход `root` / `root`; Ethernet — DHCP.

## Команды Buildroot

Из каталога SDK:

```sh
make -C output                     # повторная сборка
make -C output menuconfig          # настройки Buildroot
make -C output savedefconfig       # сохранить br_defconfig
make -C output linux-menuconfig    # настройки ядра
make -C output linux-savedefconfig # сохранить kernel_defconfig
make -C output uboot-menuconfig    # настройки U-Boot
make -C output uboot-savedefconfig # сохранить uboot_defconfig
```

[Запись NAND](NAND.md) · [Подробности: сборка, обновления, ключи и оборудование](GUIDE.md).
