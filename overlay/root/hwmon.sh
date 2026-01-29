#!/bin/sh

for i in `seq 0 100`; do 
	if [ ! -e /sys/class/hwmon/hwmon$i ];then
		break
	fi
	for j in `seq 1 10`; do
		if [ ! -e /sys/class/hwmon/hwmon${i}/temp${j}_input ];then
			break
		fi
		name="`cat /sys/class/hwmon/hwmon${i}/name`"
		if [ -e /sys/class/hwmon/hwmon${i}/temp${j}_label ];then
			name="$name.`cat /sys/class/hwmon/hwmon${i}/temp${j}_label`"
		fi
		printf "%s: %s\n" "$name" "`cat /sys/class/hwmon/hwmon${i}/temp${j}_input`"
	done
done


for i in `seq 0 100`; do
	if [ ! -e /sys/class/thermal/thermal_zone${i}/temp ]; then
		break;
	fi
	printf "%s: %s\n" "`cat /sys/class/thermal/thermal_zone${i}/type`" "`cat /sys/class/thermal/thermal_zone${i}/temp`"
	
done

