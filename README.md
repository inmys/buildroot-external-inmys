# buildroot-external-inmys
Buildroot customizations for Inmys devices

`build in ubuntu 24.04`
##Подготовка
```
sudo apt update
sudo apt install -y cpp-mips64el-linux-gnuabi64
```
##Сборка rootfs
```
mkdir -p container
cd container 
wget https://nextcloud.inmys.online/nextcloud/index.php/s/ZJFE4wx3pMwf9yn/download -O Dockerfile
sudo docker build -t komdiv-sdk .
cd ..
wget https://buildroot.org/downloads/buildroot-2025.02.7.tar.gz
tar -xf buildroot-2025.02.7.tar.gz
git clone https://github.com/inmys/buildroot-external-inmys.git -b nms-q7-k5500vk018

sudo docker run -it -e USER=$USER  -e USERID=$UID -v $(pwd):/BR --cpus=12 -t  komdiv-sdk  bash
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2025.02.7 O=$PWD/output br_defconfig
cd output
export FORCE_UNSAFE_CONFIGURE=1 
make -j12
```
`Ctrl + D для выхода из контейнера`
##Сборка ядра
```
export GIT_SSL_NO_VERIFY=1 #needs only for gitlab.inmys
git clone https://gitlab.inmys.online/srisa/k5500vk018-linux.git
cd k5500vk018-linux
make ARCH=mips CROSS_COMPILE=mips64el-linux-gnuabi64- srisa_k64s_defconfig
make ARCH=mips CROSS_COMPILE=mips64el-linux-gnuabi64- -j20
```
## Результаты сборки
```
k5500vk018-linux/vmlinuz
```
