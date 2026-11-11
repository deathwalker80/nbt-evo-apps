# nbt-evo-apps

Apps that run only on the head unit of BMW / MINI / Toyota Supra cars with **NBT EVO (iDrive 5 / 6)**, as
HMI plugins in the iDrive menu. No phone, no network, no bridge: the car data comes from the head unit itself
(CDS) and the pictures are installed with the plugin, loaded once.

Planned, in this order: **shift light + rev counter**, **G-meter**, **0-100 / braking / ¼ mile timers**, **engine
gauges** (temperatures, warm-up, torque, pedals).

## Status

**MINI Sport s1** (first version, not yet tried in a car): a menu with four pages chosen with the knob.

| Page | What it shows |
|---|---|
| Giri e cambiata | rev ring, shift light (10 LEDs, blinking at the shift point), gear, oil and water temperature, accelerator pedal, torque |
| G-meter | dot with a trail, g now, maxima (left, right, push, braking), the last minute |
| Cronometri | 0-100 and 0-50 km/h from a stop, 80-120, braking 100-0, best values |
| Strumenti motore | oil, water, accelerator pedal and torque dials, battery, steering angle, clutch |

The look follows the MINI ID6 screens; blue accents in MID / GREEN, red in SPORT (from the car's driving mode).

What the test plugin `SportTest` (t1) showed in the car, and how MINI Sport uses it:

| | In the car | Used for |
|---|---|---|
| Pictures of the plugin's zip switched by id (`ImageID`) | works | every LED, segment, digit, bar and label |
| Moving a picture (`setProperty` x / y) | works | G-meter dot, centring of numbers |
| Showing / hiding a picture, changing its width (`setProperty` 3 / 9) | nothing happened | not used |
| Car data (CDS) | revs ~9/s, accelerations ~13/s, pedals ~14/s, torque ~6/s | |

## Install

The `usb/nbt-evo-apps` folder goes in the root of a FAT USB stick (next to anything else already there).
The pictures of MINI Sport and `libMiniSport.so` are not in the repository: build them first (see Build).
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
- `/opt/aa/minisport.disable` switches MINI Sport off; after 3 starts that do not reach 30 s it stays off on its own.
- Only the HMI calls that SportTest already used in the car.
- Use the apps parked or on a closed track.

## Build

```sh
python3 tools/make_minisport.py   # pictures, zips, XML into usb/nbt-evo-apps/MiniSport/ + plugins/MiniSport/layout.h (a few minutes)
sh build/build.sh                 # libSportTest.so, libMiniSport.so (clang + ld.lld, no QNX SDK)
sh tests/host_minisport.sh        # MINI Sport on the PC with a fake framework, pages drawn into tests/out/ (root: /opt/hmi, /dev/shmem)
```

The menu icons come from `tools/icone/`; put your own in `tools/icone_mie/` (same names: `app.png`, `giri.png`,
`gmeter.png`, `crono.png`, `motore.png`) and the generator uses those instead. That folder is never committed.

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
