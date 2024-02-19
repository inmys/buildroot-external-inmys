# buildroot-external-inmys
Buildroot customizations for Inmys devices

Инструкция проверена в Ubuntu 22.04

## Подготовка

<code>
wget https://buildroot.org/downloads/buildroot-2023.11.1.tar.gz
git clone -b nms-sm-rk3568 https://github.com/inmys/buildroot-external-inmys
tar -xf buildroot-2023.11.1.tar.gz
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2023.11.1 O=$PWD/output br_defconfig
</code>

## Сборка 

<code>
cd output
make
</code>

Результаты сборки:

<code>
output/images/Image
output/images/rk3568-inmys-smarc-evm.dtb
output/images/rootfs.cpio.gz
output/images/u-boot-rockchip.bin
</code>

## Прошивка SD-карты

Для прошивки SD-карты используется [скрипт](./burn/burn_sd.sh) и [extlinux.conf](./burn/extlinux/extlinux.conf)
