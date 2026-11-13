#!/bin/sh
# Runs test/host.cpp on the PC in three cases, with a fake stick (/mnt/umassTEST -> a temporary folder) and
# /dev/shmem; removes both afterwards. Nothing of /opt is touched (only /opt/aa/letturadati.disable is checked).
#   1. normal start: 162 properties registered, values from another thread, logs and marker "ok";
#   2. the HMI stops while registering group 3: marker "group 3";
#   3. the next start finds "group 3": registers nothing;
#   4. marker deleted: registers again; 5. no stick: waits, registers nothing.
set -e
cd "$(dirname "$0")"
T=$(mktemp -d)
mkdir -p "$T/nbt-evo-apps" /dev/shmem
[ -e /mnt/umassTEST ] && { echo "ERROR: /mnt/umassTEST exists"; exit 1; }
ln -s "$T" /mnt/umassTEST
trap 'rm -f /mnt/umassTEST; rm -rf "$T" /dev/shmem/LetturaDati.*' EXIT
${CXX:-clang++} -O1 -g -fno-exceptions -fno-rtti -w ${SAN:-} host.cpp -o /tmp/letturadati_host -lpthread
fase() {
  echo ""; echo "######## $1"
  rm -f /dev/shmem/LetturaDati.*
  c=0; /tmp/letturadati_host "$2" > "$T/out.txt" 2>&1 || c=$?
  cat "$T/out.txt"
  [ $c = 0 ] || echo "== EXIT CODE $c"
  echo "== marker: $(cat "$T/nbt-evo-apps/LetturaDati.stato" 2>&1)"
}
fase "1 normal" normale
echo "== log lines (stick): $(wc -l < "$T/nbt-evo-apps/LetturaDati.log"), shmem same as stick: $(cmp -s /dev/shmem/LetturaDati.log "$T/nbt-evo-apps/LetturaDati.log" && echo yes || echo NO)"
cp "$T/nbt-evo-apps/LetturaDati.log" /tmp/letturadati_1.log
fase "2 HMI stops in group 3" ferma3
fase "3 next start" normale
grep -c "CDS: registering" "$T/nbt-evo-apps/LetturaDati.log" | sed 's/^/== registering lines in the stick log so far: /'
rm -f "$T/nbt-evo-apps/LetturaDati.stato"
fase "4 marker deleted" normale
rm /mnt/umassTEST
fase "5 no stick" normale
