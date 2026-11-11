#!/bin/sh
# nbt-evo-apps - installs the in-car apps on the NBT EVO: MINI Sport (shift light and revs, G-meter, timers,
# engine gauges). They need no phone and no network: car data from the head unit, pictures installed with the plugin.
# SportTest (the test plugin of t1) is not needed any more: if it is installed it is moved to /opt/aa/plugins_off.
# Libraries in use by the HMI can't be overwritten: they are renamed (.old_*) and the new one is copied.
# Run it INSIDE the omap shell:
#   mount -o remount rw /net/hu-omap/fs/sda0
#   on -f /net/hu-omap/ /bin/sh
#   sh /mnt/umass*/nbt-evo-apps/install.sh
# then reboot: hold the power button for 30 seconds.

APPS="MiniSport"
S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
if [ -z "$S" ]; then echo "ERROR: nbt-evo-apps folder not found on a USB stick in /mnt"; exit 1; fi
echo "USB stick folder: $S"

P=/opt/hmi/ID5/data/ro/Plugins
if [ ! -d "$P" ]; then echo "ERROR: $P missing - are you on the omap shell?"; exit 1; fi

for N in $APPS; do
  for f in lib$N.so $N.xml $N.manifest ${N}_ALL_Images.zip ${N}_ALL_Texts.zip; do
    if [ ! -f "$S/$N/$f" ]; then echo "ERROR: $S/$N/$f missing - did you copy the whole nbt-evo-apps folder?"; exit 1; fi
  done
done

for N in $APPS; do
  mkdir -p "$P/$N" || { echo "ERROR: cannot create $P/$N - did you run the mount -o remount rw line?"; exit 1; }
  rm -f "$P/$N"/lib$N.so.old_* 2>/dev/null
  if [ -f "$P/$N/lib$N.so" ]; then
    mv "$P/$N/lib$N.so" "$P/$N/lib$N.so.old_$$" && echo "OK: old lib$N.so renamed (in use until reboot)"
  fi
  cp "$S/$N"/* "$P/$N/" || { echo "ERROR: copying $N failed"; exit 1; }
  rm -f "/dev/shmem/$N.avvii"
  echo "OK: $N"
done

if [ -d "$P/SportTest" ]; then
  mkdir -p /opt/aa/plugins_off
  DEST=/opt/aa/plugins_off/SportTest; if [ -e "$DEST" ]; then DEST="/opt/aa/plugins_off/SportTest_$$"; fi
  if mv "$P/SportTest" "$DEST"; then echo "OK: SportTest (test plugin) moved to $DEST"; else echo "WARNING: cannot move SportTest"; fi
fi
if [ -e /opt/aa/minisport.disable ]; then echo "WARNING: /opt/aa/minisport.disable exists, MINI Sport will stay off"; fi

echo "===== PLUGINS ====="
ls "$P"
echo ""
echo "########## DONE - NOW REBOOT: hold the power button for 30 seconds ##########"
echo "Log: /dev/shmem/MiniSport.log (also written to $S/MiniSport.log while the stick is in)"
