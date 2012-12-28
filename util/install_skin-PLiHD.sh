#!/bin/sh

INSTALL_E2DIR=`grep "INSTALL_E2DIR=\"" ../build_openpliPC.sh |awk -F"\"" '{print $2}'`

printf "INSTALL_E2DIR value in build_openpliPC.sh is %s\n" $INSTALL_E2DIR
if [ -d $INSTALL_E2DIR ]; then
	printf "Cloning PLi-HD skin files\n"
	git clone https://github.com/nobody9/skin-PLiHD
	printf "Copying files\n"
	sudo cp -rf ./skin-PLiHD/usr/. $INSTALL_E2DIR 
	printf "Removing temp files\n"
	rm -rf ./skin-PLiHD
	printf "Done skin PLi_HD installed\n"
	printf "Start OpenpliPC and enjoy!\n"
else
	printf "Destination folder doesn\'t exist.\n"
	printf "You first need to build OpenpliPC before launching this script.\n"
	printf "To build OpenpliPC launch the build scripts in the following order:\n"
	printf "../build_libs.sh\n"
	printf "../build_openpliPC.sh\n"
	printf "../build_openpwebif.sh\n"
fi

