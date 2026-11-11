# nbt-evo-apps

Apps that run only on the head unit of BMW / MINI / Toyota Supra cars with **NBT EVO (iDrive 5 / 6)**, as
HMI plugins in the iDrive menu. No phone, no network, no bridge: the car data comes from the head unit itself
(CDS) and the pictures are installed with the plugin, loaded once.

Planned, in this order: **shift light + rev counter**, **G-meter**, **0-100 / braking / ¼ mile timers**, **engine
gauges** (temperatures, warm-up, torque, pedals).

## Status

Test stage. `SportTest` is a test plugin, not an app: one session in the car answers what the real apps need.

| Question | How SportTest checks it |
|---|---|
| How often does the car data arrive? | CDS events per second for 16 properties (revs, speed, accelerations, gear, torque, pedals, steering, temperatures...), raw values in the log |
| How fast can small pictures be swapped? | rev digits, pictures kept in memory and sent with `updateModel` |
| Can parts be shown, hidden, resized and moved without new pictures? | `setProperty`: shift LEDs (visibility), rev bar (width), G-meter dot (position) |
| Can the plugin's own zip pictures be switched by id? | `updateModel` with an `ImageID` from `SportTest_ALL_Images.zip` |
| Does the HMI show 256-colour PNG with transparency? | the two samples, side by side |

Not tested in a car yet.

## Install

The `usb/nbt-evo-apps` folder goes in the root of a FAT USB stick (next to anything else already there).
On the head unit shell (SSH root needed):

```sh
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
sh /mnt/umass*/nbt-evo-apps/install.sh
# reboot: hold the power button for 30 s
```

What to do in the car: [usb/nbt-evo-apps/README.txt](usb/nbt-evo-apps/README.txt).
Remove: `sh /mnt/umass*/nbt-evo-apps/remove.sh` (after the `mount` line), then reboot.

## Safety

- Read only: the apps read the car data, they send nothing to the car.
- `/opt/aa/sporttest.disable` switches SportTest off; after 3 starts that do not reach 30 s it stays off on its own.
- Every new kind of HMI call is behind a stage marker in `/dev/shmem`: if the HMI restarts during a stage, that kind
  of call stays off until the head unit is switched off.
- Use the apps parked or on a closed track.

## Build

```sh
python3 tools/make_sporttest.py   # XML, manifest, zips and pictures into usb/nbt-evo-apps/SportTest/
sh build/build.sh                 # libSportTest.so (clang + ld.lld, no QNX SDK)
sh tests/host_test.sh             # the plugin logic on the PC with a fake framework (root: it uses /opt/hmi and /dev/shmem)
```

`build/build.sh` needs `libAppFramework.A.so` and `libCoreFramework.A.so` from **your** head unit in `build/link/`
(`find / -name "lib*Framework.A.so"` on the head unit): they belong to the head unit and are not in this repository.
The framework declarations are in [framework/bmw_plugin.h](framework/bmw_plugin.h).

## Not included on purpose

No BMW files (libraries, pictures, texts of the head unit).

## Related

- [idrive-plus](https://github.com/deathwalker80/idrive-plus): weather, news, fuel and traffic in the iDrive, from a phone app.
- [nbt-aa-tile](https://github.com/deathwalker80/nbt-aa-tile): Android Auto tile in the main menu.
- [NBT-AAD](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto): Android Auto on NBT EVO; the plugin start-up follows its plugin code.

## Licence

PolyForm Noncommercial 1.0.0, see [LICENSE.md](LICENSE.md). Not affiliated with or endorsed by BMW, MINI or Toyota.
