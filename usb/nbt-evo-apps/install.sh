#!/bin/sh
# nbt-evo-apps - installs MINI Sport (the same command as before; everything else: pkg.sh).
# SportTest (the test plugin of t1), if installed, is moved to /opt/aa/plugins_off.
# Run it INSIDE the omap shell:
#   mount -o remount rw /net/hu-omap/fs/sda0
#   on -f /net/hu-omap/ /bin/sh
#   sh /mnt/umass*/nbt-evo-apps/install.sh
# then reboot: hold the power button for 30 seconds.

S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
if [ -z "$S" ]; then echo "ERROR: nbt-evo-apps folder not found on a USB stick in /mnt"; exit 1; fi
if [ -z "$(sh "$S/pkg.sh" brand)" ]; then
  echo "Car not chosen yet: MINI Sport is for MINI, so the car is set to mini (change it with: sh $S/pkg.sh brand bmw)"
  sh "$S/pkg.sh" brand mini || exit 1
fi
sh "$S/pkg.sh" install MiniSport
