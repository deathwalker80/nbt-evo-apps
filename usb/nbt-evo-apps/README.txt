nbt-evo-apps - in-car apps for NBT EVO (ID5/ID6) - USB folder s1 (MINI Sport s1)
=================================================================================
Apps that run only on the head unit: no phone, no network, no bridge. Car data from the head unit itself (CDS),
pictures installed with the plugin.

MINI Sport - in the menu as "MINI Sport", four pages chosen with the knob:
  Giri e cambiata    rev ring, shift light (10 LEDs from 4200 rpm, blinking from 6200), gear, oil, water,
                     accelerator pedal, torque
  G-meter            dot with trail, g now, maxima (left, right, push, braking), last minute
  Cronometri         0-100 and 0-50 from a stop, 80-120, braking 100-0 (best values until the car is switched off)
  Strumenti motore   oil, water, accelerator pedal, torque dials, battery, steering angle, clutch
Theme from the driving mode: MID / GREEN blue, SPORT red.

INSTALL (omap shell, the whole nbt-evo-apps folder in the root of the stick)
  mount -o remount rw /net/hu-omap/fs/sda0
  on -f /net/hu-omap/ /bin/sh
  sh /mnt/umass*/nbt-evo-apps/install.sh
  then reboot: hold the power button for 30 seconds.
  The test plugin SportTest, if installed, is moved to /opt/aa/plugins_off.

CHECK PARKED, ENGINE OFF
  touch /dev/shmem/MiniSport.demo      demo values: everything moves (within 5 s)
  rm /dev/shmem/MiniSport.demo         back to the car values
  echo 1 > /dev/shmem/MiniSport.tema   force the SPORT theme (0 = MID); rm the file to follow the car again

FILES
  /opt/aa/minisport.disable    exists: MINI Sport stays off (reboot)
  /opt/aa/minisport.sport      driving.mode values that mean SPORT, e.g. "3" (default 3)
  /dev/shmem/MiniSport.log     log, also nbt-evo-apps/MiniSport.log on the stick while it is in
  After 3 starts that do not reach 30 s it stays off on its own (delete /dev/shmem/MiniSport.avvii).

REMOVE
  sh /mnt/umass*/nbt-evo-apps/remove.sh (after the mount and on lines), then reboot.

Use it parked or on a closed track.
