#!/bin/sh

# Build and install plugins for enigma2pc
INSTALL_E2DIR="/usr/local/e2"

echo "-----------------------------------------"
echo "*** INSTALL REQUIRED PACKAGES ***"
echo "-----------------------------------------"
REQPKG="python-cheetah python-twisted-web ntpdate dvb-apps \
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

cd plugins/plugins-enigma2
autoreconf -i
PKG_CONFIG_PATH=$INSTALL_E2DIR/lib/pkgconfig ./configure --prefix=$INSTALL_E2DIR
make
sudo make install

# Create symlink for Skin HD_LINE_TVPRO
sudo ln -s $INSTALL_E2DIR/lib/enigma2/python/Plugins/Extensions/BitrateViewer/bitratecalc.so $INSTALL_E2DIR/lib/enigma2/python/Components/Converter/bitratecalc.so

#ln -s $INSTALL_E2DIR/bin/bitrate /usr/local/bin/bitrate

echo "-----------------------------"
echo "Copy  plugins E2PC"
echo "-----------------------------"

cd ..
sudo cp -fR Extensions/* $INSTALL_E2DIR/lib/enigma2/python/Plugins/Extensions/
sudo cp -fR SystemPlugins/* $INSTALL_E2DIR/lib/enigma2/python/Plugins/SystemPlugins/
sudo cp -fR PLi/* $INSTALL_E2DIR/lib/enigma2/python/Plugins/PLi/

echo "-----------------Installed plugins:----------------------"
echo " BouquetHotkeys, EPGSearch, OpenWebIf, OscamStatus, TMBD,"
echo " SimpleUmount, TMBD, xModem, SystemTime,  BitrateViewer, "
echo " Filebrowser, PermanentClock, VirtualZap"

cd ../skins
sudo cp -fR * $INSTALL_E2DIR/share/enigma2/

echo "-----------------Installed skins:----------------------" 
echo "          HD_LINE_TVPRO, classplus_hd-Domica           "
echo "*************************<END>*************************"
