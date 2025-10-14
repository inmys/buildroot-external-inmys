# buildroot-external-inmys
Buildroot customizations for Inmys devices
## Подготовка

<code>wget http://buildroot.org/downloads/buildroot-2023.02.1.tar.gz
tar -xf buildroot-2023.02.1.tar.gz
git clone -b nms-cm3-rk3328 https://github.com/inmys/buildroot-external-inmys
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2023.02.1 O=$PWD/output br_defconfig
</code>


## Сборка 

<code>cd output
make
</code>


## Другие полезные цели:


<code>make linux-rebuild # build Image and dtb
make linux-menuconfig
make uboot-rebuild
make uboot-menuconfig
</code>
