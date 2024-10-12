# buildroot-external-inmys
Buildroot customizations for Inmys devices

######### build in ubuntu 20/22 with rockchip kernel/packages
<code>
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-sm-rk3588
git clone https://github.com/JeffyCN/rockchip_mirrors.git --depth=1 -b buildroot-2024 buildroot-2024 
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b kernel-6.1 kernel
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b gstreamer-rockchip external/gstreamer-rockchip
git clone https://github.com/airockchip/rknn-toolkit2.git --depth=1 external/rknn-toolkit2
ln -s rknn-toolkit2/rknpu2 external/rknpu2
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b libmali external/libmali
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b mpp-dev external/mpp
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b linux-rga-multi external/linux-rga
git clone https://github.com/JeffyCN/mirrors.git --depth=1 -b rkaiq-2024_04_08 external/camera_engine_rkaiq
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2024 O=$PWD/output br-rk_defconfig
cd output
make
#result: output/images/Image, output/images/rk3588-inmys-smarc-evm.dtb, output/images/rootfs.ext2 output/images/u-boot-rockchip.bin
</code>
