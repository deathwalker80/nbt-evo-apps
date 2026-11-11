#!/bin/sh
# Runs test/host.cpp on the PC and draws the pages it produced (apps/MiniSport/test/out/*.png).
# It needs the plugin folder of the car on the PC: it creates /opt/hmi/ID5/data/ro/Plugins/MiniSport with the
# files of the package for the run and removes it afterwards (root needed). Nothing of /opt/aa is touched.
set -e
cd "$(dirname "$0")/.."
D=/opt/hmi/ID5/data/ro/Plugins/MiniSport
mkdir -p $D && cp ../../usb/nbt-evo-apps/pacchetti/MiniSport/* $D/
mkdir -p /dev/shmem test/out
rm -f /dev/shmem/MiniSport.* test/out/*
clang++ -O1 -fno-exceptions -fno-rtti -w test/host.cpp -o /tmp/minisport_host
/tmp/minisport_host
echo "== log"; cat /dev/shmem/MiniSport.log
rm -rf /opt/hmi /dev/shmem/MiniSport.*
python3 preview.py test/out
