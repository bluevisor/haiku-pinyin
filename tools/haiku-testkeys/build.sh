#!/bin/sh
# On Haiku: builds and installs the test device and hpkeys (~/bin).
set -e
cd "$(dirname "$0")"
g++ -O2 -shared -fPIC -o TestKeys TestKeysDevice.cpp -lbe /boot/system/servers/input_server
g++ -O2 -o hpkeys hpkeys.cpp -lbe
devices=$(finddir B_USER_NONPACKAGED_ADDONS_DIRECTORY)/input_server/devices
mkdir -p "$devices" ~/bin
cp TestKeys "$devices/../.TestKeys.new" && mv "$devices/../.TestKeys.new" "$devices/TestKeys"
cp hpkeys ~/bin/
g++ -O2 -o imtest imtest.cpp -lbe && cp imtest ~/bin/
g++ -O2 -o hpsettings hpsettings.cpp -lbe && cp hpsettings ~/bin/
