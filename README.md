# buildroot-external-inmys
Buildroot customizations for Inmys devices

Ветка для NMS_SMARC_TESTER: стенд проверки модуля NMS-SM-RK3588.

## Сборка (ubuntu 24)
<code>git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-sm-rk3588-tester
export GIT_SSL_NO_VERIFY=1 #needs only for gitlab.inmys
git clone https://gitlab.inmys.online/rk3588/buildroot-2024.git
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2024 O=$PWD/output br-rk_defconfig
cd output
make
</code>

## Результаты сборки
<code>output/images/Image
output/images/rk3588-inmys-smarc-tester.dtb
output/images/rootfs.ext2
output/images/u-boot-rockchip.bin
output/images/sdcard.img
</code>

## LVDS (мост SN65DSI84 на модуле)
Оба канала LVDS модуля (SMARC LVDS0 и LVDS1) выходят из одного моста
SN65DSI84 (U7), вход которого подключён к DSI0 процессора. DSI1 процессора
для LVDS не используется.

Режимы задаются в DTS платы до `#include` (`dts-rk/rk3588-inmys-smarc-evm.dts`,
`dts-rk/rk3588-inmys-smarc-tester.dts`):
<code>#define DSI 1
#define DSI_TO_LVDS_BRIDGE 1
//#define LVDS_SWAP_TO_B 1   // одиночный линк на LVDS1 (канал B) вместо LVDS0
//#define LVDS_DUAL_LINK 1   // одна dual-link панель на LVDS0 + LVDS1
</code>
Нужны патчи ядра `patches/linux/0010..0014` (в этой ветке их накладывает
buildroot через `BR2_GLOBAL_PATCH_DIR`):
* 0010 — без DSI burst: мост делит DSI-клок фиксированным делителем, частота LVDS должна совпадать с пиксельной;
* 0011 — PLL моста включается в `atomic_enable`, когда DSI-клок уже идёт;
* 0012 — одиночный линк на канале B (swap);
* 0013 — горизонтальное гашение пополам в dual link;
* 0014 — `PHY_SYS_RATIO` контроллера DSI2 RK3588 (без него DSI-линк не передаёт данные).
