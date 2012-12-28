#!/bin/sh

if [ $(lsmod | grep -c dvbsoftwareca) -eq 0 ]; then
	modprobe dvbsoftwareca
	ln -s /dev/dvb/adapter0/dvr0 /dev/dvb/adapter0/dvr1
	ln -s /dev/dvb/adapter0/demux0 /dev/dvb/adapter0/demux1
	sleep 1
fi

if [ $(ps -A | grep -c oscam) -ne 0 ]; then
	/etc/init.d/softcam.oscam restart &
else
	/etc/init.d/softcam.oscam start &
fi

exit 0
