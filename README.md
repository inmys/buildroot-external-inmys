# Tiny rootfs for nms-sm-el (INTEL-ATOM x6425E,x6413E)

## build in ubuntu 20/22
```sh
git clone https://gitlab.com/buildroot.org/buildroot.git -b 2025.05.x
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-sm-el
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot O=$PWD/output br_defconfig
cd output
make
#make menuconfig
# result: images/bzImage, images/rootfs.cpio.gz, images/usbdisk.img
```

# burn to usb/sata/nvme disk
```sh
dd if=usbdisk.img of=/dev/sdX
```

# другие полезные цели:
```sh
make menuconfig # config buildroot
make savedefconfig # save buildroot defconfig (buildroot-external-inmys/configs/br_defconfig)
make linux-rebuild # rebuild bzImage 
make linux-menuconfig # config linux kernel
make linux-savedefconfig #save linux defconfig
```


