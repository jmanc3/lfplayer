#!/usr/bin/env bash

#exit when any of the following commands fails
set -e

#make and enter the build directory if it doesn't exist already
mkdir -p installbuild
cd installbuild

#let cmake find dependencies on system
# NEEDS TO BE SUDO SO IT CAN INSTALL RESOURCES
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POLICY_VERSION_MINIMUM=3.5 ../

#actually compile
make -j 16

echo "Trying to install to /usr/local/bin/lfp"

sudo mkdir -p /usr/local/bin
sudo make -j 16 install

cd ../
unzip -o lfp.zip
sudo mkdir -p /usr/share/lfp
sudo cp -R lfp/icons /usr/share/lfp
sudo rm -r lfp

