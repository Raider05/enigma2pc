#!/bin/sh

#Start  enigma2
if [ $(ps -A | grep -c enigma2) -eq 0 ]; then
	xterm -bg black -geometry 1x1 -e /usr/local/e2/bin/enigma2.sh &
	xset -dpms
	xset s off
	if [ $(ps -A | grep -c unclutter) -eq 0]; then
		/usr/bin/unclutter -idle 1 -root  &
	fi
fi

exit
