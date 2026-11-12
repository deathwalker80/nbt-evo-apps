# nbt-evo-apps

Apps that run only on the head unit of BMW / MINI / Toyota Supra cars with **NBT EVO (iDrive 5 / 6)**, as HMI
plugins in the iDrive menu, and the USB stick that installs them. No phone, no network, no bridge: the car data
comes from the head unit itself (CDS) and the pictures are installed with the plugin.

One repository for all the apps: every app is a folder in [`apps/`](apps), the stick is one folder
([`usb/nbt-evo-apps`](usb/nbt-evo-apps)) with one package per app, and one package manager installs them. Apps can
be for every car or for one brand (MINI, BMW, Supra): you choose your car once.

## Apps

| App | Package | For | Status |
|---|---|---|---|
| **MINI Sport**: revs and shift light, G-meter, timers (0-100, 0-50, 80-120, braking 100-0), engine gauges | `MiniSport` | MINI | s1, not yet tried in a car |
| **App da USB**: the packages of the stick from the iDrive menu, with the knob | `AppUsb` | all | t0 = diagnosis only, installs nothing |
| SportTest: test plugin of the car data and of the HMI calls | `SportTest` | all | t1, tried in the car (results below); replaced by MINI Sport |

The stick can also carry the **iDrive Plus** plugins (weather, news, fuel, traffic, vehicle status) as packages:
[`tools/importa_idrive_plus.py`](tools/importa_idrive_plus.py) turns an iDrive Plus stick into packages.

### MINI Sport

| Page | What it shows |
|---|---|
| Giri e cambiata | rev ring, shift light (10 LEDs, blinking at the shift point), gear, oil and water temperature, accelerator pedal, torque |
| G-meter | dot with a trail, g now, maxima (left, right, push, braking), the last minute |
| Cronometri | 0-100 and 0-50 km/h from a stop, 80-120, braking 100-0, best values |
| Strumenti motore | oil, water, accelerator pedal and torque dials, battery, steering angle, clutch |

The look follows the MINI ID6 screens; blue accents in MID / GREEN, red in SPORT (from the car's driving mode).

What SportTest (t1) showed in the car, and how the apps use it:

| | In the car | Used for |
|---|---|---|
| Pictures of the plugin's zip switched by id (`ImageID`) | works | every LED, segment, digit, bar and label |
| Moving a picture (`setProperty` x / y) | works | G-meter dot, centring of numbers |
| Showing / hiding a picture, changing its width (`setProperty` 3 / 9) | nothing happened | not used |
| An action on the press of a button (`raAction`) | never arrived | not used: a press opens a state, the focus in it starts the work |
| Car data (CDS) | revs ~9/s, accelerations ~13/s, pedals ~14/s, torque ~6/s | |

### App da USB

t0 is a diagnosis: it shows the packages of the stick and what is installed, lets you choose the car and the iDrive
Plus phone app, and checks in the car what a real installer needs (can a plugin change the system, does the press
reach the plugin). The full answers go in its log. The installing version comes after.

## Install (USB stick)

The `usb/nbt-evo-apps` folder goes in the root of a FAT USB stick (next to anything else already there).
The built files (libraries, the pictures of MINI Sport) are not in the repository: build them first (see Build).
On the head unit shell (SSH root needed):

```sh
mount -o remount rw /net/hu-omap/fs/sda0
on -f /net/hu-omap/ /bin/sh
sh /mnt/umass*/nbt-evo-apps/pkg.sh            # menu: choose the car, install / remove / previous version
# reboot: hold the power button for 30 s
```

Or without the menu: `pkg.sh brand mini`, `pkg.sh install MiniSport AppUsb`, `pkg.sh install all`,
`pkg.sh remove NAME`, `pkg.sh restore NAME`, `pkg.sh list`. `install.sh` (MINI Sport) and `remove.sh` are still there.

What the package manager does:

- checks every file of the package (`files.sum`, POSIX cksum) before touching anything;
- builds the new folder next to the one in use and swaps them only when it is complete;
- keeps **one** previous version in `/opt/aa/nbt-evo-apps/previous/` (`restore` puts it back), and writes what is
  installed in `/opt/aa/nbt-evo-apps/installed/`;
- installs the apps that are for your car only; files that change by brand or by iDrive Plus phone app come from
  `marche/<variant>/` of the package;
- moves removed apps to `/opt/aa/plugins_off` (recoverable); nothing starts by itself.

## Safety

- Read only: the apps read the car data, they send nothing to the car.
- Every app has its off switch (`/opt/aa/<name>.disable`) and stays off on its own after 3 starts that do not
  reach 30 s.
- Use the sport apps parked or on a closed track.

## Repository

```
apps/<Name>/            one app: C++ plugin, generator of its stick files (make.py), package.txt, test/
framework/              declarations of the HMI plugin framework of the head unit
build/build.sh          builds every app (ARM, QNX) and copies lib<Name>.so into its package
tools/pacchetti.py      copies package.txt into the packages and writes files.sum
tools/importa_idrive_plus.py   iDrive Plus stick -> packages
usb/nbt-evo-apps/       the stick folder: pkg.sh, install.sh, remove.sh, README.txt, pacchetti/<Name>/
```

## Build

```sh
python3 apps/MiniSport/make.py    # pictures, zips, XML of MINI Sport + apps/MiniSport/layout.h (a few minutes)
python3 apps/AppUsb/make.py       # XML, zips of App da USB
python3 apps/SportTest/make.py    # XML, pictures of SportTest
sh build/build.sh                 # lib<Name>.so of every app (clang + ld.lld, no QNX SDK)
python3 tools/importa_idrive_plus.py <iDrive Plus stick folder>     # optional
python3 tools/pacchetti.py        # package.txt and files.sum of every package
sh apps/MiniSport/test/host.sh    # MINI Sport on the PC with a fake framework, pages drawn into apps/MiniSport/test/out/
sh apps/AppUsb/test/host.sh       # App da USB on the PC (root: /opt/aa, /mnt, /dev/shmem)
```

The version of an app is written only in its `package.txt`.

MINI Sport's menu icons come from `apps/MiniSport/icone/`; put your own in `apps/MiniSport/icone_mie/` (same
names: `app.png`, `giri.png`, `gmeter.png`, `crono.png`, `motore.png`) and the generator uses those instead. That
folder is never committed.

`build/build.sh` needs `libAppFramework.A.so` and `libCoreFramework.A.so` from **your** head unit in `build/link/`
(`find / -name "lib*Framework.A.so"` on the head unit): they belong to the head unit and are not in this repository.
The framework declarations are in [framework/bmw_plugin.h](framework/bmw_plugin.h).

## A new app

1. `apps/<Name>/<name>.cpp` (start from an existing app), `apps/<Name>/make.py` writing its XML, manifest and zips
   into `usb/nbt-evo-apps/pacchetti/<Name>/`, and `apps/<Name>/package.txt`:
   ```
   name=<Name>
   title=<shown in the menu of App da USB>
   version=<version>
   group=nbt-evo-apps
   brands=all            (or: mini / bmw / supra, several separated by spaces)
   dest=plugin
   reset=<Name>.avvii
   disable_file=/opt/aa/<name>.disable
   ```
2. A new focus id, connector id and image ids (used: focus 32759-32767, connector 212-219; images SportTest
   64001-64110, AppUsb 64150, MiniSport 64200-65000, iDrive Plus 57001-63001). Image ids are shared by all the
   plugins: never give the same id to two plugins.
3. `build/build.sh` and `tools/pacchetti.py` pick it up by themselves.

## Not included on purpose

No BMW files (libraries, pictures, texts of the head unit).

## Related

- [idrive-plus](https://github.com/deathwalker80/idrive-plus): weather, news, fuel and traffic in the iDrive, from a phone app.
- [nbt-aa-tile](https://github.com/deathwalker80/nbt-aa-tile): Android Auto tile in the main menu.
- [NBT-AAD](https://github.com/Sq00ky/Supra-iDrive6-Android-Auto): Android Auto on NBT EVO; the plugin start-up follows its plugin code.

## Licence

PolyForm Noncommercial 1.0.0, see [LICENSE.md](LICENSE.md). Not affiliated with or endorsed by BMW, MINI or Toyota.
