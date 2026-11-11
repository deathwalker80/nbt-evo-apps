#!/bin/sh
# nbt-evo-apps - package manager of the USB stick: installs, updates, removes and restores the apps of the
# packages in nbt-evo-apps/pacchetti/ (nbt-evo-apps apps and iDrive Plus plugins).
# Run it INSIDE the omap shell (after: mount -o remount rw /net/hu-omap/fs/sda0 and on -f /net/hu-omap/ /bin/sh):
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh                     menu with numbers
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh list                the packages, installed and on the stick
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh brand mini          your car: mini, bmw or supra (asked once)
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh phone nbt           phone app of iDrive Plus: nbt (NBT Plus) or brand (MINI/BMW/Supra Plus)
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh install NAME...     (or: install all)
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh remove NAME...      moved to /opt/aa/plugins_off, recoverable
#   sh /mnt/umass*/nbt-evo-apps/pkg.sh restore NAME        back to the version before the last install
# After installing or removing: reboot, hold the power button for 30 seconds.
#
# A package is a folder pacchetti/<NAME>/ with package.txt (name, version, brands...), files.sum (cksum of every
# file), the files of the plugin and optionally marche/<variant>/ with the files that change by brand / phone app.
# What is installed is written in /opt/aa/nbt-evo-apps/installed/<NAME>.txt. Before an update the folder in use is
# moved to /opt/aa/nbt-evo-apps/previous/<NAME>: only ONE previous version is kept.
# Nothing starts by itself: this script runs only when you run it.

P=${NEA_PLUGINS:-/opt/hmi/ID5/data/ro/Plugins}
AA=${NEA_DATA:-/opt/aa/nbt-evo-apps}
OFF=${NEA_OFF:-/opt/aa/plugins_off}
SHM=${NEA_SHMEM:-/dev/shmem}

S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
if [ -n "$NEA_STICK" ]; then S=$NEA_STICK; fi
if [ -z "$S" ]; then echo "ERROR: nbt-evo-apps folder not found on a USB stick in /mnt"; exit 1; fi
K=$S/pacchetti
LOG=$S/pkg.log

CKSUM=0
if cksum /dev/null >/dev/null 2>&1; then CKSUM=1; fi

log() {
  echo "$*"
  echo "$(date '+%Y-%m-%d %H:%M:%S') $*" >> "$LOG" 2>/dev/null
}

# value of a key of a package.txt: campo FILE KEY
campo() {
  v=""
  while IFS= read -r riga || [ -n "$riga" ]; do
    case "$riga" in
      "$2="*) v=${riga#*=} ;;
    esac
  done < "$1"
  echo "$v"
}

marca() { if [ -f "$AA/brand" ]; then cat "$AA/brand"; fi; }
telefono() { if [ -f "$AA/phone" ]; then cat "$AA/phone"; else echo nbt; fi; }

# the variant whose files go over the common ones: the brand for the apps, NBT Plus or brand for iDrive Plus
variante() {
  vv=$(campo "$1" variants)
  if [ -z "$vv" ]; then echo ""; return; fi
  if [ "$(campo "$1" variant_from)" = phone ] && [ "$(telefono)" = nbt ]; then echo ""; return; fi   # NBT Plus: common files only
  marca
}

# yes if the package is for the brand chosen (brands=all or the brand in the list)
adatto() {
  b=$(campo "$1" brands)
  if [ -z "$b" ] || [ "$b" = all ]; then echo yes; return; fi
  m=$(marca)
  if [ -z "$m" ]; then echo nobrand; return; fi
  for x in $b; do if [ "$x" = "$m" ]; then echo yes; return; fi; done
  echo no
}

installata() { if [ -f "$AA/installed/$1.txt" ]; then campo "$AA/installed/$1.txt" version; fi; }

controlla_hmi() {
  if [ ! -d "$P" ]; then echo "ERROR: $P missing - are you on the omap shell?"; exit 1; fi
  mkdir -p "$AA/installed" "$AA/previous" "$OFF" 2>/dev/null
  if [ ! -d "$AA/installed" ]; then echo "ERROR: cannot write in $AA - did you run the mount -o remount rw line?"; exit 1; fi
}

# checks the files of a package with files.sum (cksum); without the cksum command it only checks they exist
verifica() {
  d=$K/$1
  if [ ! -f "$d/files.sum" ]; then log "ERROR: $1: files.sum missing"; return 1; fi
  ok=0
  conta=0
  while IFS= read -r riga || [ -n "$riga" ]; do
    set -- $riga
    crc=$1; dim=$2; f=$3
    conta=$((conta + 1))
    if [ ! -f "$d/$f" ]; then log "ERROR: $d/$f missing - did you copy the whole nbt-evo-apps folder?"; ok=1; continue; fi
    if [ "$CKSUM" = 1 ]; then
      set -- $(cksum "$d/$f")
      if [ "$1" != "$crc" ] || [ "$2" != "$dim" ]; then log "ERROR: $d/$f is damaged (cksum $1 $2, expected $crc $dim)"; ok=1; fi
    fi
  done < "$d/files.sum"
  if [ "$CKSUM" != 1 ]; then log "NOTE: no cksum command here: only checked that the $conta files exist"; fi
  return $ok
}

installa_uno() {
  n=$1
  d=$K/$n
  pt=$d/package.txt
  if [ ! -f "$pt" ]; then log "ERROR: package $n not on the stick"; return 1; fi
  a=$(adatto "$pt")
  if [ "$a" = nobrand ]; then log "ERROR: $n is for $(campo "$pt" brands): choose your car first (pkg.sh brand mini|bmw|supra)"; return 1; fi
  if [ "$a" = no ]; then log "SKIPPED: $n is for $(campo "$pt" brands), your car is $(marca)"; return 0; fi
  verifica "$n" || return 1
  dest=$(campo "$pt" dest)
  case "$dest" in
    plugin) dest=$P/$n ;;
    *) log "ERROR: $n: destination '$dest' not supported"; return 1 ;;
  esac
  var=$(variante "$pt")
  if [ -n "$var" ] && [ ! -d "$d/marche/$var" ]; then log "ERROR: $n has no files for '$var' (variants: $(campo "$pt" variants))"; return 1; fi
  # 1) new folder next to the old one, complete before anything else is touched
  nuovo=$P/.nea_$n
  rm -rf "$nuovo"
  mkdir -p "$nuovo" || { log "ERROR: cannot create $nuovo - did you run the mount -o remount rw line?"; return 1; }
  for f in "$d"/*; do
    b=${f##*/}
    case "$b" in package.txt|files.sum|marche) continue ;; esac
    cp "$f" "$nuovo/" || { log "ERROR: copying $b failed"; rm -rf "$nuovo"; return 1; }
  done
  if [ -n "$var" ]; then
    cp "$d/marche/$var"/* "$nuovo/" || { log "ERROR: copying the $var files of $n failed"; rm -rf "$nuovo"; return 1; }
  fi
  # 2) the folder in use becomes the previous version (only one is kept)
  if [ -d "$dest" ]; then
    rm -rf "$AA/previous/$n" "$AA/previous/$n.txt"
    mv "$dest" "$AA/previous/$n" || { log "ERROR: cannot move the old $n away"; rm -rf "$nuovo"; return 1; }
    if [ -f "$AA/installed/$n.txt" ]; then cp "$AA/installed/$n.txt" "$AA/previous/$n.txt"; fi
  fi
  # 3) the new folder takes its place
  mv "$nuovo" "$dest" || { log "ERROR: cannot put the new $n in place - the old one is in $AA/previous/$n"; return 1; }
  # 4) what it replaces goes to plugins_off; its start counters are reset
  for x in $(campo "$pt" replaces); do
    if [ -d "$P/$x" ]; then
      dx=$OFF/$x; if [ -e "$dx" ]; then dx=${dx}_$$; fi
      if mv "$P/$x" "$dx"; then log "OK: $x moved to $dx (replaced by $n)"; fi
    fi
    rm -f "$AA/installed/$x.txt"
  done
  for x in $(campo "$pt" reset); do rm -f "$SHM/$x"; done
  { cat "$pt"; echo "variant=$var"; echo "installed=$(date '+%Y-%m-%d %H:%M:%S')"; } > "$AA/installed/$n.txt"
  log "OK: $n $(campo "$pt" version) installed${var:+ ($var)}"
  for x in $(campo "$pt" disable_file); do
    if [ -e "$x" ]; then log "WARNING: $x exists, $n will stay off"; fi
  done
  return 0
}

togli_uno() {
  n=$1
  if [ -d "$P/$n" ]; then
    dx=$OFF/$n; if [ -e "$dx" ]; then dx=${dx}_$$; fi
    if mv "$P/$n" "$dx"; then
      log "OK: $n removed (moved to $dx)"
      rm -f "$AA/installed/$n.txt"
    else
      log "ERROR: cannot move $n - did you run the mount -o remount rw line?"; return 1
    fi
  else
    log "$n not installed, nothing to do"
  fi
}

ripristina_uno() {
  n=$1
  if [ ! -d "$AA/previous/$n" ]; then log "ERROR: no previous version of $n"; return 1; fi
  tmp=$AA/previous/.$n.$$
  if [ -d "$P/$n" ]; then mv "$P/$n" "$tmp" || { log "ERROR: cannot move $n"; return 1; }; fi
  mv "$AA/previous/$n" "$P/$n" || { log "ERROR: cannot restore $n"; return 1; }
  if [ -f "$AA/previous/$n.txt" ]; then mv "$AA/previous/$n.txt" "$AA/installed/$n.txt.new"; fi
  if [ -d "$tmp" ]; then
    mv "$tmp" "$AA/previous/$n"
    if [ -f "$AA/installed/$n.txt" ]; then mv "$AA/installed/$n.txt" "$AA/previous/$n.txt"; fi
  fi
  if [ -f "$AA/installed/$n.txt.new" ]; then mv "$AA/installed/$n.txt.new" "$AA/installed/$n.txt"; fi
  log "OK: $n back to $(installata "$n")"
}

pacchetti() { for d in "$K"/*/; do if [ -f "$d/package.txt" ]; then b=${d%/}; echo "${b##*/}"; fi; done; }

# "all": every package except those replaced by another package for this car (e.g. SportTest by MiniSport)
tutti() {
  sost=""
  for n in $(pacchetti); do
    if [ "$(adatto "$K/$n/package.txt")" = yes ]; then sost="$sost $(campo "$K/$n/package.txt" replaces)"; fi
  done
  for n in $(pacchetti); do
    salta=0
    for x in $sost; do if [ "$x" = "$n" ]; then salta=1; fi; done
    if [ "$salta" = 0 ]; then echo "$n"; fi
  done
}

lista() {
  echo "Car: $(marca)   iDrive Plus phone app: $(telefono)"
  i=0
  for n in $(pacchetti); do
    i=$((i + 1))
    pt=$K/$n/package.txt
    inst=$(installata "$n"); if [ -z "$inst" ]; then inst="-"; fi
    a=$(adatto "$pt")
    case "$a" in yes) nota="" ;; nobrand) nota=" (for $(campo "$pt" brands): choose the car)" ;; *) nota=" (not for this car)" ;; esac
    echo "$i) $n - $(campo "$pt" title) - stick $(campo "$pt" version), installed $inst$nota"
  done
}

nome_n() { j=0; for n in $(pacchetti); do j=$((j + 1)); if [ "$j" = "$1" ]; then echo "$n"; return; fi; done; }

imposta_marca() {
  case "$1" in
    mini|bmw|supra) mkdir -p "$AA"; echo "$1" > "$AA/brand" && log "OK: car $1" ;;
    *) echo "ERROR: car: mini, bmw or supra"; return 1 ;;
  esac
}

imposta_telefono() {
  case "$1" in
    nbt|brand) mkdir -p "$AA"; echo "$1" > "$AA/phone" && log "OK: iDrive Plus phone app $1" ;;
    *) echo "ERROR: phone app: nbt (NBT Plus) or brand (MINI / BMW / Supra Plus)"; return 1 ;;
  esac
}

fine() { echo ""; echo "########## NOW REBOOT: hold the power button for 30 seconds ##########"; }

menu() {
  while :; do
    echo ""
    lista
    echo ""
    echo "  i N  install package N (i all = all)     r N  remove N     p N  previous version of N"
    echo "  c    car (mini, bmw, supra)               t    phone app of iDrive Plus      q    quit"
    echo "> "
    read -r cmd arg || return
    case "$cmd" in
      i) if [ "$arg" = all ]; then for n in $(tutti); do installa_uno "$n"; done; else n=$(nome_n "$arg"); [ -n "$n" ] && installa_uno "$n"; fi; CAMBIATO=1 ;;
      r) n=$(nome_n "$arg"); [ -n "$n" ] && togli_uno "$n"; CAMBIATO=1 ;;
      p) n=$(nome_n "$arg"); [ -n "$n" ] && ripristina_uno "$n"; CAMBIATO=1 ;;
      c) echo "car (mini, bmw, supra):"; read -r x; imposta_marca "$x" ;;
      t) echo "phone app (nbt = NBT Plus, brand = MINI/BMW/Supra Plus):"; read -r x; imposta_telefono "$x" ;;
      q) return ;;
      *) echo "?" ;;
    esac
  done
}

CAMBIATO=0
case "$1" in
  list|"") ;;
  *) controlla_hmi ;;
esac
case "$1" in
  "") controlla_hmi; menu; if [ "$CAMBIATO" = 1 ]; then fine; fi ;;
  list) lista ;;
  brand) if [ -n "$2" ]; then imposta_marca "$2"; else marca; fi ;;
  phone) if [ -n "$2" ]; then imposta_telefono "$2"; else telefono; fi ;;
  install)
    shift
    if [ "$1" = all ]; then set -- $(tutti); fi
    err=0
    for n in "$@"; do installa_uno "$n" || err=1; done
    fine
    exit $err ;;
  remove) shift; for n in "$@"; do togli_uno "$n"; done; fine ;;
  restore) shift; for n in "$@"; do ripristina_uno "$n"; done; fine ;;
  *) echo "ERROR: unknown '$1' - see the first lines of this script"; exit 1 ;;
esac
