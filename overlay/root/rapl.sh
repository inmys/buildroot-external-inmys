#!/bin/sh

last=`cat /sys/class/powercap/intel-rapl\:0/energy_uj`
while sleep 1
do
        x=`cat /sys/class/powercap/intel-rapl\:0/energy_uj`
        # One joule per second is a watt.
	uw=$((x - last))
	mC=`cat /sys/class/thermal/thermal_zone0/temp`
	printf "p: %d mW, t: %d C\n" $((${uw}/1000)) $(($mC/1000))
        last=$x
done
