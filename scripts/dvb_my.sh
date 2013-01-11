#!/bin/sh

if [ $(lsmod | grep -c dvbsoftwareca) -eq 0 ]; then
	sudo modprobe dvbsoftwareca
fi

for i in $(find /dev/dvb -name demux0 | sed 's/.\{6\}$//')
do
        echo "For $i"demux0" create symlink $i"demux1" "
        sudo ln -s $i"demux0" $i"demux1"
        echo "For $i"dvr0" create symlink $i"dvr1" "
        sudo ln -s $i"dvr0" $i"dvr1"
done

sleep 1

if [ $(ps -A | grep -c oscam) -ne 0 ]; then
	sudo /etc/init.d/softcam.oscam restart &
else
	sudo /etc/init.d/softcam.oscam start &
fi

exit 0
