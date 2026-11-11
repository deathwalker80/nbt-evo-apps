#!/bin/sh
# Runs test/host.cpp on the PC with the packages of the stick folder: it makes /mnt/umassTEST point to usb/ and a
# test /opt/aa/nbt-evo-apps, and removes both afterwards (root needed).
set -e
cd "$(dirname "$0")/.."
R=$(cd ../.. && pwd)
if [ -e /opt/aa ] && [ ! -e /opt/aa/.nea_test ]; then echo "ERROR: /opt/aa exists on this PC: not touching it"; exit 1; fi
mkdir -p /opt/aa/nbt-evo-apps/installed /dev/shmem /mnt && touch /opt/aa/.nea_test
printf 'name=MiniSport\nversion=s0\n' > /opt/aa/nbt-evo-apps/installed/MiniSport.txt
printf 'name=Meteo\nversion=v27\n' > /opt/aa/nbt-evo-apps/installed/Meteo.txt
ln -sfn "$R/usb" /mnt/umassTEST
rm -f /dev/shmem/AppUsb.*
clang++ -O1 -fno-exceptions -fno-rtti -w test/host.cpp -o /tmp/appusb_host
/tmp/appusb_host
echo "== saved files: brand=$(cat /opt/aa/nbt-evo-apps/brand) phone=$(cat /opt/aa/nbt-evo-apps/phone)"
echo "== log"; cat /dev/shmem/AppUsb.log
rm -rf /opt/aa /mnt/umassTEST /dev/shmem/AppUsb.* "$R/usb/nbt-evo-apps/AppUsb.log"
