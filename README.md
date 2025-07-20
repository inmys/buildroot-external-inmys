buildroot-external для HH_IMX6

### Сборка
<code>
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-hh-imx6ull
git clone https://gitlab.com/buildroot.org/buildroot.git -b 2025.05.x
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot O=$PWD/output br_defconfig
cd output
make
#result: output/images/u-boot.imx, output/images/imx6ull-nms-hh-evm.dtb, output/images/zImage, output/images/rootfs.ext4, output/images/sdcard.img output/images/update.swu
</code>

### Вспомогательные цели в Makefile (у buildroot)
* `make uboot-menuconfig` - вызвать menuconfig для u-boot
* `make uboot-rebuild` - пересобрать output/images/u-boot.imx (и dtb в нём)
* `make uboot-savedefconfig` - сохранить конфиг u-boot как "конфиг по умолчанию": uboot_defconfig
* `make linux-menuconfig` - вызвать menuconfig для linux kernel
* `make linux-rebuild` - пересобрать output/images/zImage и output/images/imx6ull-nms-hh-evm.dtb
* `make linux-savedefconfig` - сохранить конфиг linux kernel как "конфиг по умолчанию": kernel_defconfig
* `make menuconfig` - вызвать menuconfig для buildroot
* `make savedefconfig` - сохранить конфиг buildroot как "конфиг по умолчанию": buildroot-external-inmys/configs/br_defconfig

### Обновление
- исползуя web: http://<ip>:8080
        нажимаем "Click here, or drag and drop a software update image file to this area." 
        выбираем update.swu
        после обновления прибор перезагрузится
- используя консоль
        загружаем в прибор файл update.swu (в /tmp по scp)
        /etc/init.d/S80swupdate stop
        bootslot=`grep -o root=ubi0:rootfs[AB] /proc/cmdline | sed 's/.*root=ubi0:rootfs//'`
        /usr/bin/swupdate -v -e stable,now-${bootslot} -k /etc/sign_public.pem -K /etc/update.key -i /tmp/update.swu
        после обновления прибор перезагрузится
используется A/B обновление (то есть 2 rootfs на NAND)
