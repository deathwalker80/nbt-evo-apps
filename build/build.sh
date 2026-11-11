#!/bin/sh
# Builds the apps (ARM, QNX) into out/: out/lib<Name>.so for every apps/<Name>/ with C++ code, copied into its
# package usb/nbt-evo-apps/pacchetti/<Name>/. Needs clang and ld.lld (tested with LLVM 18).
#
# The two HMI libraries, libAppFramework.A.so and libCoreFramework.A.so, are NOT in the repository (they belong
# to the head unit and can't be redistributed): copy them from YOUR head unit into build/link/
#   (on the head unit:  find / -name "lib*Framework.A.so")
# libc.so.3 is built here from an empty stub: it is only for linking, in the car the loader uses the real one.
set -e
cd "$(dirname "$0")/.."
L=build/link
for f in libAppFramework.A.so libCoreFramework.A.so; do
  [ -f "$L/$f" ] || { echo "ERROR: $L/$f missing - copy it from your head unit (see above)"; exit 1; }
done
mkdir -p out
ARM="--target=armv7a-unknown-linux-gnueabi -mfloat-abi=soft -marm -O2 -fPIC -ffreestanding -fno-builtin"
clang $ARM -nostdlib -shared -fuse-ld=lld -Wl,-soname,libc.so.3 -o out/libc.so.3 $L/libc_stub.c
for A in apps/*/; do
  N=$(basename "$A")
  S=$(ls apps/$N/*.cpp 2>/dev/null) || continue
  clang++ $ARM -fno-exceptions -fno-rtti -fno-threadsafe-statics -fno-stack-protector -fvisibility=hidden \
    -Wall -Wno-unused-function -c $S -o out/$N.o
  ld.lld -shared --hash-style=sysv -z norelro -z nognustack --no-rosegment -z max-page-size=0x1000 --no-undefined \
    -soname lib$N.so -o out/lib$N.so out/$N.o $L/libAppFramework.A.so $L/libCoreFramework.A.so out/libc.so.3
  rm -f out/$N.o
  mkdir -p usb/nbt-evo-apps/pacchetti/$N
  cp out/lib$N.so usb/nbt-evo-apps/pacchetti/$N/
  echo "OK: out/lib$N.so (copied to usb/nbt-evo-apps/pacchetti/$N/)"
done
