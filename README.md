# buildroot-external-inmys
Buildroot customizations for Inmys devices

## Подготовка
<code># build in ubuntu 20/22 with rockchip kernel/packages
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-sm-rk3588
export GIT_SSL_NO_VERIFY=1 #needs only for gitlab.inmys
git clone https://gitlab.inmys.online/rk3588/buildroot-2024.git
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b kernel-6.1 kernel
./buildroot-external-inmys/apply-kernel-patches.sh
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b gstreamer-rockchip external/gstreamer-rockchip
git clone https://github.com/airockchip/rknn-toolkit2.git --depth=1 external/rknn-toolkit2
ln -s rknn-toolkit2/rknpu2 external/rknpu2
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b libmali external/libmali
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b mpp-dev external/mpp
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b linux-rga-multi external/linux-rga
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b rkaiq-2024_04_08 external/camera_engine_rkaiq
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2024 O=$PWD/output br-rk_defconfig
</code>


## Сборка
<code>cd output
make
</code>

## Результаты сборки
<code>#result:
output/images/Image
output/images/rk3588-inmys-smarc-evm.dtb
output/images/rootfs.ext2
output/images/u-boot-rockchip.bin
</code>

## LVDS (мост SN65DSI84 на модуле)
Оба канала LVDS модуля (SMARC LVDS0 и LVDS1) выходят из одного моста
SN65DSI84 (U7), вход которого подключён к DSI0 процессора. DSI1 процессора
для LVDS не используется.

Режимы задаются в `dts-rk/rk3588-inmys-smarc-evm.dts` до `#include`:
<code>#define DSI 1
#define DSI_TO_LVDS_BRIDGE 1
//#define LVDS_SWAP_TO_B 1   // одиночный линк на LVDS1 (канал B) вместо LVDS0
//#define LVDS_DUAL_LINK 1   // одна dual-link панель на LVDS0 + LVDS1
</code>
Нужны патчи ядра `patches/linux/0010..0014` (накладываются
`apply-kernel-patches.sh`):
* 0010 — без DSI burst: мост делит DSI-клок фиксированным делителем, частота LVDS должна совпадать с пиксельной;
* 0011 — PLL моста включается в `atomic_enable`, когда DSI-клок уже идёт;
* 0012 — одиночный линк на канале B (swap);
* 0013 — горизонтальное гашение пополам в dual link;
* 0014 — `PHY_SYS_RATIO` контроллера DSI2 RK3588 (без него DSI-линк не передаёт данные).
