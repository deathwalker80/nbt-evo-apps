#!/bin/sh
# Runs tests/host_minisport.cpp on the PC and draws the pages it produced (tests/out/*.png).
# It needs the plugin folder of the car on the PC: it creates /opt/hmi/ID5/data/ro/Plugins/MiniSport with the
# files of usb/ for the run and removes it afterwards (root needed). Nothing of /opt/aa is touched.
set -e
cd "$(dirname "$0")/.."
D=/opt/hmi/ID5/data/ro/Plugins/MiniSport
mkdir -p $D && cp usb/nbt-evo-apps/MiniSport/* $D/
mkdir -p /dev/shmem tests/out
rm -f /dev/shmem/MiniSport.* tests/out/*
clang++ -O1 -fno-exceptions -fno-rtti -w tests/host_minisport.cpp -o /tmp/minisport_host
/tmp/minisport_host
echo "== log"; cat /dev/shmem/MiniSport.log
rm -rf /opt/hmi /dev/shmem/MiniSport.*
python3 tools/preview_minisport.py tests/out
