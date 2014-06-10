#!/bin/bash
#To build enigma2 on Ubuntu 10.xx and 11.xx (32/64bit) 
#Install these packages:
#
#
echo "-----------------------------------------"
echo "*** INSTALL REQUIRED PACKAGES ***"
echo "-----------------------------------------"
REQPKG="autoconf automake automake1.10 build-essential debhelper gettext subversion mercurial git autopoint \
	libdvdnav-dev libfreetype6-dev libfribidi-dev \
	libgif-dev libjpeg62-dev libpng12-dev \
	libsdl1.2-dev libsigc++-1.2-dev \
	libtool libxml2-dev libxslt1-dev python-dev swig libssl-dev libssl0.9.8 \
	libvdpau-dev vdpau-va-driver \
	libcdio-dev libvcdinfo-dev \
	libavcodec-dev libpostproc-dev libnl2-dev \
	python-setuptools \
	libgstreamer0.10-dev libgstreamer-plugins-base0.10-dev \
	"

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

cd libs

#Build and install libdvbsi++:
PKG="libdvbsi++"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
git clone git://git.opendreambox.org/git/obi/$PKG.git
cd $PKG
dpkg-buildpackage -uc -us
cd ..
sudo dpkg -i $PKG*.deb

#Build and install libxmlccwrap:
PKG="libxmlccwrap"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
git clone git://git.opendreambox.org/git/obi/$PKG.git
cd $PKG
dpkg-buildpackage -uc -us
cd ..
sudo dpkg -i $PKG*.deb

#Build and install libdreamdvd:
PKG="libdreamdvd"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
git clone git://schwerkraft.elitedvb.net/libdreamdvd/$PKG.git
cd $PKG
dpkg-buildpackage -uc -us
cd ..
sudo dpkg -i $PKG*.deb

#Build and install libdvbcsa:
PKG="libdvbcsa"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
svn co svn://svn.videolan.org/$PKG/trunk $PKG
cd $PKG
autoreconf -i
./configure --prefix=/usr --enable-sse2
make
sudo make install
cd ..

#Build and install libbluray:
PKG="libbluray"
LIB_BLURAY_REF="6d88105783fa3a83963178d31f624717334ca9e0"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
git clone git://git.videolan.org/libbluray.git
cd $PKG
git checkout $LIB_BLURAY_REF
autoreconf -vif
./configure --prefix=/usr
make
sudo make install
cd ..

#Build and install pythonwifi:
PKG="pythonwifi"
echo "-----------------------------------------"
echo "Build and install $PKG"
echo "-----------------------------------------"
if [ -d $PKG ]; then
	echo "Erasing older build dir"
	sudo rm -Rf $PKG
	sudo rm -f $PKG*
fi
#git clone git://git.berlios.de/$PKG
git clone git://github.com/pingflood/$PKG
cd $PKG
sudo python setup.py install
cd ..

#Build dvbsoftwareca kernel module:
#cd dvbsoftwareca
#make   # You must have installed dvb-core (for example from s2-liplianin).
#insmod dvbsoftwareca.ko  # It will create ca0 device for adapter0


cd ..
echo "*********************<END>*********************"


