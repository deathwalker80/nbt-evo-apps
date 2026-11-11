#!/bin/sh
# nbt-evo-apps - removes MINI Sport and SportTest (moved to /opt/aa/plugins_off, recoverable; everything else: pkg.sh).
# Switch MINI Sport off without removing it:  echo 1 > /opt/aa/minisport.disable   (+ reboot)
# Run it INSIDE the omap shell (after mount -o remount rw /net/hu-omap/fs/sda0 and on -f /net/hu-omap/ /bin/sh):
#   sh /mnt/umass*/nbt-evo-apps/remove.sh

S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
if [ -z "$S" ]; then echo "ERROR: nbt-evo-apps folder not found on a USB stick in /mnt"; exit 1; fi
sh "$S/pkg.sh" remove MiniSport SportTest
