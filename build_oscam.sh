#!/bin/sh

REQPKG="cmake"

for p in $REQPKG; do
        echo -n ">>> Checking \"$p\" : "
        dpkg -s $p >/dev/null
        if [ "$?" -eq "0" ]; then
                echo "package is installed, skip it"
        else
                echo "package NOT present, installing it"
                sudo apt-get -y install $p
        fi
done
echo "-----------------------------------------"
echo "*** INSTALL LIBUSB 1.06, GO LAST VERSION   ***"
echo "***  http://www.libusb.org/ ***"
echo "-----------------------------------------"

cd oscam/libusb-1.0.6
autoconf -i
./configure --prefix=/usr/local
make
sudo make install
cd ..

echo "-----------------------------------------"
echo "*** INSTALL OSCAM ***"
echo "-----------------------------------------"

mkdir oscam_8249/build
cd oscam_8249/build
cmake --DHAVE_DVBAPI --DHAVE_WEBIF ../
sudo make install
cd ../..

echo "-----------------------------------------"
echo "*** COPY CONFIG FILES in /etc/vdr/oscam ***"
echo "*** EDIT DATA FOR YOU ***"
echo "-----------------------------------------"

sudo mkdir -p /etc/vdr/oscam
sudo cp -fR conf/* /etc/vdr/oscam
sudo cp -fRP softcam.oscam /etc/init.d/
ln -s /etc/init.d/softcam.oscam /etc/init.d/softcam

echo "-----------------------------------------"
echo "*** STARTING OCSCAM ... ***"
echo "*** sudo /etc/init.d/softcam start ***"
echo "-----------------------------------------"

sudo /etc/init.d/softcam restart


