#!/bin/sh
# Runs tests/host_test.cpp on the PC. It needs the plugin folder of the car on the PC: it creates
# /opt/hmi/ID5/data/ro/Plugins/SportTest with the files of usb/ for the run and removes it afterwards (root needed).
set -e
cd "$(dirname "$0")/.."
D=/opt/hmi/ID5/data/ro/Plugins/SportTest
mkdir -p $D && cp usb/nbt-evo-apps/SportTest/* $D/
rm -f /dev/shm/SportTest.* 2>/dev/null || true
mkdir -p /dev/shmem
rm -f /dev/shmem/SportTest.*
clang++ -O1 -fno-exceptions -fno-rtti -w tests/host_test.cpp -o /tmp/sporttest_host
/tmp/sporttest_host
echo "== log"; cat /dev/shmem/SportTest.log
rm -rf /opt/hmi /dev/shmem/SportTest.*
