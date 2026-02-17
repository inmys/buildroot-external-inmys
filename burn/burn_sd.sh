#!/bin/sh 

exit_on_error()
{
	if [ $? = 0 ]; then
		return
	fi
	text=$1
	echo "$text, exit"
	exit 1
}

dev=${1}
if [ "$dev" = "" ];then
	echo "uasge: burn_sd.sh /dev/sdX"
	exit 1
fi

script_path=$(readlink -f "$0")
#fw=$(dirname "${script_path}")
fw=../../output/images
#mount point
mp=/tmp/mp
mkdir -p $mp

#umount
for i in 3 2 1; do
	if [ ! -e ${dev}${i} ]; then
		#no partition
		continue
	fi
	if ! grep -q ${dev}${i} /proc/mounts; then
		#not mounted
		continue
	fi
	umount ${dev}${i}
	exit_on_error "cant umount ${dev}${i}"
done

#burn
#echo 'o n p 1 32768 +1G n p 2 2129920 +1G n p 3 4227072 +1500M a 1 w' | tr ' ' '\n' | fdisk -u ${dev}
echo 'o n p 1 32768 +1G n p 2 2129920 +1G n p 3 4227072  a 1 w' | tr ' ' '\n' | fdisk -u ${dev}
exit_on_error "cant write partitions table to ${dev}"

#use release uboot from https://disk.yandex.ru/d/hmKqKdvUE5smig . Doc: https://wiki.inmys.ru/boards:nms-sm-rk3568:u-boot:start
if [ -f ${fw}/u-boot-rockchip.bin ]; then
	dd if=${fw}/u-boot-rockchip.bin of=${dev} seek=64
	exit_on_error "cant write ${fw}/u-boot-rockchip.bin to ${dev}"
else
	echo "use release inmys uboot from https://disk.yandex.ru/d/hmKqKdvUE5smig . Doc: https://wiki.inmys.ru/boards:nms-sm-rk3568:u-boot:start"
fi
mkfs.ext4 -F ${dev}1
mount ${dev}1 ${mp}
exit_on_error "cant mount ${dev}1 to ${mp}"
cp -rL ${fw}/Image extlinux ${fw}/rk3568-inmys-smarc-evm.dtb ${fw}/rootfs.cpio.gz ${mp}
exit_on_error "can't copy files to ${mp}"
umount ${mp}
exit_on_error "can't umount"
mkfs.ext4 -F -L STORE ${dev}3
exit_on_error "cant do mkfs.ext4 on ${dev}3"
sync
echo "Success!"
