#!/bin/sh
# nbt-evo-apps - check for drawing with the GPU: which graphics libraries the head unit has, and how the
# LayerManager composes the screen (layers, surfaces, render order). READS ONLY: only "get" queries, nothing is set.
# Run it INSIDE the omap shell, once for every screen you want to compare, with a short name for that screen:
#   on -f /net/hu-omap/ /bin/sh
#   sh /mnt/umass*/nbt-evo-apps/gpu-verifica.sh home        (iDrive main menu on screen)
#   sh /mnt/umass*/nbt-evo-apps/gpu-verifica.sh minisport   (a MINI Sport page on screen)
# Report: nbt-evo-apps/gpu-<name>.log on the stick (or /dev/shmem without the stick).

NOME=${1:-prova}
S=""
for d in /mnt/umass*/nbt-evo-apps; do if [ -d "$d" ]; then S=$d; break; fi; done
OUT=${S:-/dev/shmem}/gpu-$NOME.log
L=/opt/sys/bin/LayerManagerControl

{
  echo "== gpu-verifica.sh $NOME $(date)"
  echo ""
  echo "== graphics libraries"
  ls -l /opt/sys/lib /usr/lib /lib 2>&1 | grep -i -e egl -e gles -e screen -e ilm -e kd -e gpu -e pvr -e sgx
  echo ""
  echo "== graphics processes"
  pidin -F "%a %N" 2>&1 | grep -i -e screen -e layer -e hmi -e gpu -e pvr -e sgx -e nbt
  echo ""
  echo "== memory and CPU"
  pidin info 2>&1
  echo ""
  echo "== screen windows (names only)"
  ls -lR /dev/screen 2>&1 | head -200
  echo ""
  echo "== LayerManagerControl"
  ls -l $L
  for q in "get screens" "get layers" "get surfaces" "get scene"; do
    echo ""
    echo "== $q"
    $L $q 2>&1
  done
  echo ""
  echo "== end"
} > "$OUT" 2>&1

echo "Done: the report is $OUT"
