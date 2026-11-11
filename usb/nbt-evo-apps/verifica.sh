#!/bin/sh
# nbt-evo-apps - check: can a plugin (App da USB) install apps by itself?
# READS ONLY: changes nothing, writes only its report verifica.log on the stick (or /dev/shmem without the stick).
# Run it INSIDE the omap shell, after a normal start of the car and WITHOUT the "mount -o remount rw" line
# (so it sees the system as a plugin sees it):
#   on -f /net/hu-omap/ /bin/sh
#   sh /mnt/umass*/nbt-evo-apps/verifica.sh
# Needs a plugin of ours running (SportTest, MiniSport or AppUsb): the process that loads it is the one checked.

P=/opt/hmi/ID5/data/ro/Plugins
S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
OUT=${S:-/dev/shmem}/verifica.log

# pid of the process that runs our plugins: the number in [ ] of the last line of their logs
pid_plugin() {
  for f in /dev/shmem/AppUsb.log /dev/shmem/MiniSport.log /dev/shmem/SportTest.log; do
    if [ -f "$f" ]; then
      ultima=""
      while IFS= read -r r || [ -n "$r" ]; do ultima=$r; done < "$f"
      x=${ultima#*\[}
      x=${x%%\]*}
      case "$x" in
        ''|*[!0-9]*) ;;
        *) echo "$x $f"; return ;;
      esac
    fi
  done
}

{
  echo "== verifica.sh $(date)"
  echo "== this shell"
  pidin -p $$ users
  echo ""
  set -- $(pid_plugin)
  PID=$1
  if [ -z "$PID" ]; then
    echo "== NO plugin log of ours in /dev/shmem: no pid to check"
  else
    echo "== plugin process: pid $PID (from $2)"
    pidin -p "$PID" users
    echo ""
    pidin -p "$PID" arguments
    echo ""
    ls -ld /proc/"$PID"
    echo ""
    echo "== libraries of that process (all)"
    pidin -p "$PID" mem
  fi
  echo ""
  echo "== mount"
  mount
  echo ""
  echo "== folders"
  ls -ld /opt /opt/aa /opt/aa/nbt-evo-apps /opt/hmi /opt/hmi/ID5 /opt/hmi/ID5/data /opt/hmi/ID5/data/ro "$P" /fs /fs/sda0 /mnt /mnt/umass*
  echo ""
  echo "== plugins"
  ls -l "$P"
  echo ""
  echo "== df"
  df -k "$P" /opt/aa /mnt/umass*
  echo ""
  echo "== commands"
  for c in mount umount cp mv rm mkdir cksum shutdown slay ksh sh; do
    echo "$c: $(type "$c" 2>&1)"
  done
  echo ""
  echo "== end"
} > "$OUT" 2>&1

echo "Done: the report is $OUT"
