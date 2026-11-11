nbt-evo-apps - in-car apps for NBT EVO (ID5/ID6) - USB folder t1 (SportTest, a TEST)
==================================================================================
Apps that run only on the head unit: no phone, no network, no bridge. Car data from the head unit itself (CDS),
pictures installed with the plugin and loaded once.

SportTest is NOT an app yet: one test session in the car tells what the real apps (shift light + rev counter,
G-meter, 0-100 / braking timers, engine gauges) can use. It shows up in the menu as "Prova sport".

INSTALL (omap shell, the whole nbt-evo-apps folder in the root of the stick)
  mount -o remount rw /net/hu-omap/fs/sda0
  on -f /net/hu-omap/ /bin/sh
  sh /mnt/umass*/nbt-evo-apps/install.sh
  then reboot: hold the power button for 30 seconds.

THE TEST (engine running, parked; a short drive only for the G-meter values)
  Open "Prova sport". Without car data (engine off) everything moves in DEMO mode, so every part can be checked.
  The "Fase" button goes through the stages; watch what moves and how smoothly:
   0  rev digits (top left, pictures swapped) and the data line - always on
   1  + the 10 shift LEDs appear with the revs          (setProperty: visibility)
   2  + the rev bar follows the revs                    (setProperty: width)
   3  + the red dot moves in the G circle               (setProperty: position)
   4  + the blue digits under the buttons follow the revs (pictures of the zip, by id)
  Also compare the two samples at the bottom: "256 colori" must look like "RGBA".
  If the button does not change stage: echo N > /dev/shmem/SportTest.vai   (N = 0..4, read within 5 s)

  If the HMI restarts during a stage, SportTest finds it at the next start and leaves that kind of call off
  until the head unit is switched off (/dev/shmem/SportTest.fase).

AFTERWARDS
  The log is written to the stick (nbt-evo-apps/SportTest.log) while it is in, and to /dev/shmem/SportTest.log.
  Remove: sh /mnt/umass*/nbt-evo-apps/remove.sh (after the mount line), then reboot.
  Switch off without removing: echo 1 > /opt/aa/sporttest.disable, then reboot.
