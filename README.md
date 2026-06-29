# buildroot-external-inmys
Buildroot customizations for Inmys devices

`build in ubuntu 24.04`
## Подготовка
```
sudo apt update
sudo apt install -y cpp-mips64el-linux-gnuabi64
```

## Сборка образа системы 

Введите логин и токен к gitlab.inmys.online вместо `XXXXX` и `YYYYY`:

```
LOGIN="XXXXX"; TOKEN="YYYYY"; printf "machine gitlab.inmys.online\nlogin %s\npassword %s\n" "$LOGIN" "$TOKEN" > .gitlab.netrc
```

Выполните следующие команды:
```
mkdir -p container
cd container 
wget https://nextcloud.inmys.online/nextcloud/index.php/s/ZJFE4wx3pMwf9yn/download -O Dockerfile
sudo docker build -t komdiv-sdk .
cd ..
wget https://buildroot.org/downloads/buildroot-2025.02.7.tar.gz
tar -xf buildroot-2025.02.7.tar.gz
git clone https://github.com/inmys/buildroot-external-inmys.git  -b nms-q7-k5500vk018
sudo docker run -it -e USER=$USER -e USERID=$UID -v $(pwd):/BR -v "$(pwd)/.gitlab.netrc:/root/.netrc:ro" --cpus=12 -t komdiv-sdk bash 
```

В Docker-контейнере выполнить:
```
make BR2_EXTERNAL=$PWD/buildroot-external-inmys -C buildroot-2025.02.7 O=$PWD/output br_defconfig
cd output
export FORCE_UNSAFE_CONFIGURE=1 
make -j12
```

После завершения сборки необходимо выйти из контейнера сочетанием клавиш `Ctrl + D`

## Результаты сборки

После успешной сборки ядра основной результат находится по пути:

```
output/build/linux-main/vmlinuz
```

Результаты сборки rootfs находятся в директории:

```
output/
```
