#!/usr/bin/env python3
"""
Finishes the packages of the USB stick (usb/nbt-evo-apps/pacchetti/<NAME>/): copies apps/<NAME>/package.txt into
the package of every app and writes files.sum, the POSIX cksum (CRC, size, path) of every file of every package,
which pkg.sh checks before installing. Run it after the generators and build/build.sh.
Usage: python3 tools/pacchetti.py
"""
import os
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
RADICE = os.path.join(QUI, "..")
PACCHETTI = os.path.join(RADICE, "usb", "nbt-evo-apps", "pacchetti")

_TAB = []
for i in range(256):
    c = i << 24
    for _ in range(8):
        c = ((c << 1) ^ 0x04C11DB7) if c & 0x80000000 else (c << 1)
        c &= 0xFFFFFFFF
    _TAB.append(c)


def cksum(dati):
    """the same number as the POSIX cksum command"""
    c = 0
    for b in dati:
        c = ((c << 8) & 0xFFFFFFFF) ^ _TAB[(c >> 24) ^ b]
    n = len(dati)
    while n:
        c = ((c << 8) & 0xFFFFFFFF) ^ _TAB[(c >> 24) ^ (n & 0xFF)]
        n >>= 8
    return (~c) & 0xFFFFFFFF


def campi(path):
    out = {}
    for riga in open(path, encoding="utf-8"):
        riga = riga.rstrip("\n")
        if "=" in riga and not riga.startswith("#"):
            k, v = riga.split("=", 1)
            out[k] = v
    return out


def chiudi(cartella):
    nome = os.path.basename(cartella)
    pt = os.path.join(cartella, "package.txt")
    if not os.path.isfile(pt):
        sys.exit(f"ERROR: {pt} missing")
    c = campi(pt)
    for k in ("name", "title", "version", "dest"):
        if not c.get(k):
            sys.exit(f"ERROR: {pt}: '{k}' missing")
    if c["name"] != nome:
        sys.exit(f"ERROR: {pt}: name '{c['name']}' but the folder is '{nome}'")
    righe = []
    for base, dirs, files in os.walk(cartella):
        dirs.sort()
        for f in sorted(files):
            rel = os.path.relpath(os.path.join(base, f), cartella).replace(os.sep, "/")
            if rel in ("package.txt", "files.sum"):
                continue
            if " " in rel:
                sys.exit(f"ERROR: {nome}/{rel}: no spaces in file names (pkg.sh reads them by words)")
            if os.sep in rel.replace("/", os.sep) and not rel.startswith("marche/"):
                sys.exit(f"ERROR: {nome}/{rel}: only marche/<variant>/ may have subfolders")
            dati = open(os.path.join(base, f), "rb").read()
            righe.append(f"{cksum(dati)} {len(dati)} {rel}")
    with open(os.path.join(cartella, "files.sum"), "w", newline="\n") as fh:
        fh.write("\n".join(righe) + "\n")
    print(f"{nome} {c['version']}: {len(righe)} files")


def main():
    for app in sorted(os.listdir(os.path.join(RADICE, "apps"))):
        src = os.path.join(RADICE, "apps", app, "package.txt")
        dst = os.path.join(PACCHETTI, app)
        if os.path.isfile(src) and os.path.isdir(dst):
            shutil.copyfile(src, os.path.join(dst, "package.txt"))
    for nome in sorted(os.listdir(PACCHETTI)):
        d = os.path.join(PACCHETTI, nome)
        if os.path.isdir(d):
            chiudi(d)


if __name__ == "__main__":
    main()
