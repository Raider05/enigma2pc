#!/bin/sh
export DISPLAY=:0.0

while [ 1 != 0 ]; do 
 
 if [ $(ps -A | grep -c oscam) -ne 0 ]; then
 
#	if [ $(ps -A | grep -c lircd) -ne 0 ]; then

#		/usr/bin/irxevent /root/.lircrc &
#		/usr/bin/irexec /root/.lircrc &
		/usr/local/e2/bin/e2pc.sh &

		exit 0
#	else
# 		sleep  1
#	fi
 else
    sleep 1
 fi
done

