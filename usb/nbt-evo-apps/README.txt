nbt-evo-apps - in-car apps for NBT EVO (ID5/ID6) - USB folder
==============================================================
Apps that run only on the head unit: no phone, no network, no bridge. Car data from the head unit itself (CDS),
pictures installed with the plugin. One package per app in pacchetti/, one package manager: pkg.sh.

PACKAGES
  MiniSport   MINI Sport s1 (MINI only) - in the menu as "MINI Sport", four pages chosen with the knob:
              Giri e cambiata (rev ring, shift light, gear, oil, water, pedal, torque), G-meter, Cronometri
              (0-100, 0-50, 80-120, braking 100-0), Strumenti motore. MID/GREEN blue, SPORT red.
  AppUsb      App da USB t0 (all cars) - in the menu as "App da USB". t0 = DIAGNOSIS ONLY, installs nothing:
              shows the packages of the stick, lets you choose the car, checks what the real installer needs.
  SportTest   the test plugin of t1: replaced by MINI Sport (installing MINI Sport moves it to plugins_off).
  Meteo, Notizie, Locali, Carburanti, Traffico, Auto   iDrive Plus plugins, if the stick has them.

INSTALL (omap shell, the whole nbt-evo-apps folder in the root of the stick)
  mount -o remount rw /net/hu-omap/fs/sda0
  on -f /net/hu-omap/ /bin/sh
  sh /mnt/umass*/nbt-evo-apps/pkg.sh                    menu with numbers
  or:
  sh /mnt/umass*/nbt-evo-apps/pkg.sh brand mini         your car: mini, bmw or supra (once)
  sh /mnt/umass*/nbt-evo-apps/pkg.sh install MiniSport AppUsb
  sh /mnt/umass*/nbt-evo-apps/install.sh                 MINI Sport only (as before)
  then reboot: hold the power button for 30 seconds.

  pkg.sh list                 packages on the stick and installed
  pkg.sh phone nbt|brand      iDrive Plus phone app: NBT Plus, or MINI / BMW / Supra Plus
  pkg.sh install all          everything that is for your car
  pkg.sh remove NAME          moved to /opt/aa/plugins_off (recoverable)
  pkg.sh restore NAME         back to the version before the last install (one is kept)
  Log of pkg.sh: nbt-evo-apps/pkg.log on the stick.
  sh /mnt/umass*/nbt-evo-apps/verifica.sh   reads only: can a plugin install by itself? report: nbt-evo-apps/verifica.log

MINI SPORT - CHECK PARKED, ENGINE OFF
  touch /dev/shmem/MiniSport.demo      demo values: everything moves (within 5 s)
  rm /dev/shmem/MiniSport.demo         back to the car values
  echo 1 > /dev/shmem/MiniSport.tema   force the SPORT theme (0 = MID); rm the file to follow the car again
  /opt/aa/minisport.sport              driving.mode values that mean SPORT, e.g. "3" (default 3)

FILES
  /opt/aa/<name>.disable            exists: that app stays off (reboot)
  /dev/shmem/<Name>.log             log of every app, also nbt-evo-apps/<Name>.log on the stick while it is in
  After 3 starts that do not reach 30 s an app stays off on its own (delete /dev/shmem/<Name>.avvii).
  /opt/aa/nbt-evo-apps/             what pkg.sh installed (installed/), the previous versions (previous/),
                                    the car (brand) and the iDrive Plus phone app (phone)

Use the sport apps parked or on a closed track.
