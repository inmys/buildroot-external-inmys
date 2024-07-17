# buildroot-external-inmys
Buildroot customizations for Inmys devices

######### build in ubuntu 20/22

wget http://buildroot.org/downloads/buildroot-2023.11.1.tar.gz

tar -xf buildroot-2023.11.1.tar.gz

git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-hh-px30

make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2023.11.1 O=$PWD/output br_defconfig

cd output

make

#make menuconfig

make

# result: output/images/Image,output/images/px30-sh-din_cntrl_v1.dtb,rootfs.cpio.gz,rootfs.ext2.gz

# подготовить toolchain:
make sdk


######### build in docker

wget http://buildroot.org/downloads/buildroot-2023.11.1.tar.gz

tar -xf buildroot-2023.11.1.tar.gz

git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-hh-px30

# build docker image

docker build -t buildroot-2023.11.1 buildroot-2023.11.1/support/docker

# build toolcahin and rootfs in docker

docker run --mount type=bind,source="$(pwd)",target=/workdir --user "$(id -u):$(id -g)" -it buildroot-2023.11.1 make BR2_EXTERNAL=/workdir/buildroot-external-inmys -C /workdir/buildroot-2023.11.1 O=/workdir/output br_defconfig

docker run --mount type=bind,source="$(pwd)",target=/workdir --user "$(id -u):$(id -g)" -it buildroot-2023.11.1 make -C /workdir/output

# result: output/images/Image,output/images/px30-sh-din_cntrl_v1.dtb,rootfs.cpio.gz,rootfs.ext2.gz

docker run --mount type=bind,source="$(pwd)",target=/workdir --user "$(id -u):$(id -g)" -it buildroot-2023.1.1 make -C /workdir/output menuconfig

make linux-rebuild # build Image and dtb
#
