#!/bin/bash

# where install Enigma2 tree
INSTALL_E2DIR="/usr/local/e2"

BACKUP_E2="etc/enigma2 etc/tuxbox/*.xml etc/tuxbox/nim_sockets share/enigma2/xine.conf"

REQPKG="xterm unclutter mingetty libmpcdec-dev mawk libvpx-dev \
	"
for p in $REQPKG; do
        echo -n ">>> Checking \"$p\" : "
        dpkg -s $p >/dev/null
        if [ "$?" -eq "0" ]; then
                echo "package is installed, skip it"
        else
                echo "package NOT present, installing it"
                sudo apt-get -y --force-yes install $p
        fi
done


# ----------------------------------------------------------------------

DO_BACKUP=0
DO_RESTORE=0
DO_XINE=1
DO_CONFIGURE=1
DO_PARALLEL=1
DO_MAKEINSTALL=1

function e2_backup {
        echo "-----------------------------"
        echo "BACKUP E2 CONFIG"
        echo "-----------------------------"

	tar -C $INSTALL_E2DIR -v -c -z -f e2backup.tgz $BACKUP_E2
}

function e2_restore {
        echo "-----------------------------"
        echo "RESTORE OLD E2 CONFIG"
        echo "-----------------------------"

	if [ -f e2backup.tgz ]; then
		sudo tar -C $INSTALL_E2DIR -v -x -z -f e2backup.tgz
	fi
}

function usage {
	echo "Usage:"
	echo " -b : backup E2 conf file before re-compile"
	echo " -r : restore E2 conf file after re-compile"
	echo " -x : don't compile xine-lib (compile only enigma2)"
	echo " -nc: don't start configure/autoconf"
	echo " -py: parallel compile (y threads) e.g. -p2"
	echo " -ni: only execute make and no make install"
	echo " -h : this help"
	echo ""
	echo "common usage:"
	echo "  $0 -b -r : make E2 backup, compile E2, restore E2 conf files"
	echo ""
}

function copy_dvbsoftwareca {
	KERNEL_1="3.7"
	KERNEL=`uname -r | mawk -F. '{ printf("%d.%d\n",$1,$2); }'`
	KERNEL_2=`echo -e "$KERNEL\n$KERNEL_1" | sort -V | head -n1`

	echo "You the kernel - $KERNEL, the result compare with the kernel $KERNEL_1 - $KERNEL_2"
	if [ $KERNEL_2 == "3.7" ]; then
        	sudo cp -fR dvbsoftwareca.ko /lib/modules/`uname -r`/kernel/drivers/media/dvb-frontends/
	else
        	sudo cp -fR dvbsoftwareca.ko /lib/modules/`uname -r`/kernel/drivers/media/dvb/
	fi
}

while [ "$1" != "" ]; do
    case $1 in
        -b ) 	DO_BACKUP=1
              shift
              ;;
        -r ) 	DO_RESTORE=1
		          shift
              ;;
	      -x )	DO_XINE=0
		          shift
		          ;;
	      -nc )	DO_CONFIGURE=0
		          shift
		          ;;
        -ni )	DO_MAKEINSTALL=0
		          shift
		          ;;
        -p* ) if [ "`expr substr "$1" 3 3`" = "" ]
              then
                 echo "Number threads is missing"
                 usage
                 exit
              else
                 DO_PARALLEL=`expr substr "$1" 3 3`
              fi
              shift
		          ;;
	      -h )  usage
	      	    exit
	      	    ;;
	      * )  	echo "Unknown parameter $1"
	      	    usage
	      	    exit
	      	    ;;
    esac
done

if [ "$DO_BACKUP" -eq "1" ]; then
	e2_backup
fi

# ----------------------------------------------------------------------

if [ "$DO_XINE" -eq "1" ]; then

	# Build and install xine-lib:
	PKG="xine-lib"

	cd $PKG
	
  if [ "$DO_CONFIGURE" -eq "1" ]; then	
	  echo "-----------------------------------------"
	  echo "configuring OpenPliPC $PKG"
	  echo "-----------------------------------------"

	  ./autogen.sh --disable-xinerama --prefix=/usr
  fi	

  if [ "$DO_MAKEINSTALL" -eq "0" ]; then
	  echo "-----------------------------------------"
	  echo "build OpenPliPC $PKG, please wait..."
	  echo "-----------------------------------------"

	  make -j"$DO_PARALLEL"
    if [ ! $? -eq 0 ]
    then
      echo ""
      echo "An error occured while building xine-lib"
      exit
    fi
    
  else
	  echo "--------------------------------------"
	  echo "installing OpenPliPC $PKG"
	  echo "--------------------------------------"

	  sudo make -j"$DO_PARALLEL" install
    if [ ! $? -eq 0 ]
    then
      echo ""
      echo "An error occured while building xine-lib"
      exit
    fi
  fi
    
	cd ..

fi

# ----------------------------------------------------------------------

# Build and install enigma2:

PKG="enigma2"

cd $PKG

if [ "$DO_CONFIGURE" -eq "1" ]; then

  echo "--------------------------------------"
  echo "configuring OpenPliPC $PKG with native lirc support"
  echo "please edit file $INSTALL_E2DIR/etc/enigma2/remote.conf"
  echo "--------------------------------------"

#Create symlinks in /usr diectory before compile enigma2
  sudo ln -sd /usr/share/swig2.0 /usr/share/swig1.3

  autoreconf -i
  ./configure --prefix=$INSTALL_E2DIR --with-xlib --with-debug
fi  
 
echo "--------------------------------------"
echo "build OpenPliPC $PKG, please wait..."
echo "--------------------------------------"

if [ "$DO_MAKEINSTALL" -eq "0" ]; then
  make -j"$DO_PARALLEL"
  if [ ! $? -eq 0 ]
  then
    echo ""
    echo "An error occured while building OpenPliPC - section make"
    exit
  fi
  
else  
  echo "--------------------------------------"
  echo "installing OpenPliPC $PKG in $INSTALL_E2DIR"
  echo "--------------------------------------"

  sudo make -j"$DO_PARALLEL" install
  if [ ! $? -eq 0 ]
  then
    echo ""
    echo "An error occured while building OpenPliPC - section make install"
    exit
  fi
  cd dvbsoftwareca
  sudo make -j"$DO_PARALLEL"
  if [ ! $? -eq 0 ]
  then
    echo ""
    echo "An error occured while building OpenPliPC - section dvbsoftwareca"
    exit
  fi
#  sudo cp -fR dvbsoftwareca.ko /lib/modules/`uname -r`/kernel/drivers/media/dvb/
  copy_dvbsoftwareca
  sudo depmod -a

#Insert module dvbsoftwareca and create symlink
  if [ $(lsmod | grep -c dvbsoftwareca) -eq 0 ]; then
        sudo modprobe dvbsoftwareca
        sudo ln -s /dev/dvb/adapter0/dvr0 /dev/dvb/adapter0/dvr1
        sudo ln -s /dev/dvb/adapter0/demux0 /dev/dvb/adapter0/demux1
  fi

fi  
cd ../..

echo "--------------------------------------"
echo "final step: installing E2 conf files"
echo "--------------------------------------"

#Create symlinks in /lib diectory post install enigma2
sudo ln -sf `ls /lib/i386-linux-gnu/libc-2.??.so`  /lib/libc.so.6

#Create symlinks in /usr diectory post install enigma2
sudo ln -sd $INSTALL_E2DIR/lib/enigma2 /usr/lib/enigma2
sudo ln -sd $INSTALL_E2DIR/lib/enigma2 /usr/local/lib/enigma2
sudo ln -sd $INSTALL_E2DIR/share/enigma2 /usr/local/share/enigma2
sudo ln -sd $INSTALL_E2DIR/share/enigma2 /usr/share/enigma2
sudo ln -sd $INSTALL_E2DIR/include/enigma2 /usr/include/enigma2
sudo ln -sd $INSTALL_E2DIR/etc/stb /usr/local/etc/stb

#Create symlinks in /etc diectory post install enigma2
sudo ln -s -d $INSTALL_E2DIR/etc/enigma2 /etc/enigma2
sudo ln -s -d $INSTALL_E2DIR/etc/tuxbox /etc/tuxbox

# strip binary
sudo strip $INSTALL_E2DIR/bin/enigma2

# removing pre-compiled py files
sudo find $INSTALL_E2DIR/lib/enigma2/python/ -name "*.py[oc]" -exec rm {} \;

# copying needed files
sudo mkdir -p $INSTALL_E2DIR/etc/enigma2
sudo mkdir -p $INSTALL_E2DIR/etc/tuxbox
sudo cp share/fonts/* $INSTALL_E2DIR/share/fonts
sudo cp -rf etc/* $INSTALL_E2DIR/etc
sudo cp enigma2/data/black.mvi $INSTALL_E2DIR/etc/tuxbox/logo.mvi
sudo cp -fR scripts/* $INSTALL_E2DIR/bin/

sudo ln -sf $INSTALL_E2DIR/bin/enigma2 ./e2bin
sudo ln -sf $INSTALL_E2DIR/bin/enigma2.sh /usr/local/bin/enigma2.sh

if [ "$DO_RESTORE" -eq "1" ]; then
	e2_restore
fi

echo ""
echo "**********************<END>**********************"
