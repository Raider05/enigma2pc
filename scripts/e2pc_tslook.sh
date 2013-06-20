#!/bin/sh
#
#	The easy script create files 'TS_Files'.meta for Enigma2PC
#	and after can look TS_Files (recordings VDR, downloading from internet)
#

# You directory witch TS-files, default current the directory
# When some directory 
# DIR_TS="DIR1 DIR3 DIR3"

DIR_TS=`pwd`

# You LANGUAGE

AUDIO_LANG="(rus)"

#Temp file

TMP_FILE="/tmp/e2ts.txt"

############################################################
#	Data for 'TS_Files.meta'
############################################################

#REF_SERVICE="1:0:1:84:7:70:168000:0:0:0:"
REF_SERVICE="1:0:0:0:0:0:0:0:0:0:"
F_NAME=""
F_DECRIPTION=""
F_CREATE=""
UNKNOW=""
F_LENGTH=""
F_SIZE=""
SERVICE_DATA="f:0,c:"
PACKET="188"
SCRAMBLED="0"

create_mfile() {

        echo "For ts-file $ts create $ts.meta file"
	F_NAME=`basename $1`
	echo "Filename is $F_NAME"
################################################
#	Give data use FFMPEG
###############################################
	ffmpeg -i $1 2>&1 | grep Stream | tee $TMP_FILE

################################################
#	Video PID Detect
################################################
	VPID=`grep Video $TMP_FILE | cut -d'x' -f2 | cut -d']' -f1`
	count_vpid=$((4-`echo -n $VPID | sed s/*//g | wc -c`))

	while [ $count_vpid != 0 ]
        do
                VPID="0"$VPID
                count_vpid=$(($count_vpid-1))
        done
	echo "VPID: $VPID"
	VPID="00"$VPID

#################################################
#	Audio PID Detect
#################################################
	AAC3=""
	if [ $(grep Audio $TMP_FILE | grep $AUDIO_LANG | grep -c ac3  ) -ne 0 ]; then
		APID=`grep Audio $TMP_FILE | grep $AUDIO_LANG | grep  ac3 | head -1 | cut -d'x' -f2 | cut -d']' -f1`
		AAC3=$AAC3"1"
	elif [ $(grep Audio $TMP_FILE | grep $AUDIO_LANG  | grep -c mp2 ) -ne 0 ]; then
		APID=`grep Audio $TMP_FILE | grep $AUDIO_LANG | grep  mp2 | head -1 | cut -d'x' -f2 | cut -d']' -f1`
	elif [ $(grep Audio $TMP_FILE | grep "Stream #0.1" | grep -c ac3  ) -ne 0 ]; then
		APID=`grep Audio $TMP_FILE | grep "Stream #0.1" | cut -d'x' -f2 | cut -d']' -f1`
		AAC3=$AAC3"1"
	else
		APID=`grep Audio $TMP_FILE | grep "Stream #0.1" | cut -d'x' -f2 | cut -d']' -f1`
	fi

	count_apid=$((4-`echo -n $APID | sed s/*//g | wc -c`))
	while [ $count_apid != 0 ]
	do
		APID="0"$APID
		count_apid=$(($count_apid-1))
	done
	echo "APID: $APID"
        if [ -z $AAC3 ]; then
                APID=",c:01"$APID
        else
                APID=",c:04"$APID
        fi
##################################################
#	Video type detect
##################################################
	if [ $(grep Video $TMP_FILE |grep -c h264) -ne 0 ]; then
		VTYPE=",c:050001"
	else
		VTYPE=""
	fi
###################################################
#	Create string m_service_data
###################################################

	SERVICE_DATA=$SERVICE_DATA$VPID$APID$VTYPE


####################################################
#	Create new meta file
####################################################
	echo $REF_SERVICE > $1".meta"
	echo $F_NAME >> $1".meta"
	echo $F_DECRIPTION >> $1".meta"
	echo $F_CREATE >> $1".meta"
	echo $UNKNOW >> $1".meta"
	echo $F_LENGTH >> $1".meta"
	echo $F_SIZE >> $1".meta"
	echo $SERVICE_DATA >> $1".meta"
	echo $PACKET >> $1".meta"
	echo $SCRAMBLED >> $1".meta"

####################################################
#	Free vars
####################################################
	APID=""
	VPID=""
	VTYPE=""
	AAC3=""
	SERVICE_DATA="f:0,c:"
}

meta() {
	for ts in $(find $DIR_TS -iname '*.ts')
	do
        	if [ -f $ts".meta" ]; then
			if [ $(grep -c $SERVICE_DATA $ts".meta") -ne 0 ]; then
				echo "Meta file $ts is exist";
			else
			        create_mfile "$ts"
			fi
		else
			create_mfile "$ts"
		fi
	done
}

rm_file() {
	if [ -f $TMP_FILE ]; then
		rm $TMP_FILE
	fi
}

meta_clear() {
	find $DIR_TS -iname "*.ts.meta" -exec rm -fR {} \;
}

case "$1" in
  create_meta)
        meta
	rm_file
        ;;
  start)
        meta
        rm_file
        ;;
  clear)
        meta_clear
        ;;
  stop)
        meta_clear
        ;;
  *)
	meta
	rm_file
	;;
esac

