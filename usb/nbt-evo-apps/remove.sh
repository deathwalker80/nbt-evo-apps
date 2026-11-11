#!/bin/sh
# nbt-evo-apps - removes the in-car apps (moved to /opt/aa/plugins_off, recoverable).
# Switch MINI Sport off without removing it:  echo 1 > /opt/aa/minisport.disable   (+ reboot)
# Run it INSIDE the omap shell (after mount -o remount rw /net/hu-omap/fs/sda0 and on -f /net/hu-omap/ /bin/sh):
#   sh /mnt/umass*/nbt-evo-apps/remove.sh

APPS="MiniSport SportTest"
P=/opt/hmi/ID5/data/ro/Plugins
OFF=/opt/aa/plugins_off
mkdir -p "$OFF"
for N in $APPS; do
  if [ -d "$P/$N" ]; then
    DEST="$OFF/$N"; if [ -e "$DEST" ]; then DEST="$OFF/${N}_$$"; fi
    if mv "$P/$N" "$DEST"; then echo "OK: $N removed (moved to $DEST)"; else echo "WARNING: cannot move $N - did you run the mount -o remount rw line?"; fi
  else
    echo "$N not present, nothing to do"
  fi
done
echo "Remaining plugins:"; ls "$P"
echo "NOW REBOOT: hold the power button for 30 seconds."
