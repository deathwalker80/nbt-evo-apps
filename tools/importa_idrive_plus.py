#!/usr/bin/env python3
"""
Turns an iDrive Plus USB stick (the folder of the release zip, or idrive-plus/chiavetta with the built libraries)
into packages of the nbt-evo-apps stick: one package per plugin (Meteo, Notizie, Locali, Carburanti, Traffico,
Auto) in usb/nbt-evo-apps/pacchetti/<NAME>/, with the "waiting for the phone" pages of MINI / BMW / Supra Plus in
marche/<brand>/ (chosen by pkg.sh from the phone app: NBT Plus = the common pages).
The version is the one of the stick (vNN in the first line of 00-installa.sh).
Usage: python3 tools/importa_idrive_plus.py <iDrive Plus stick folder>
"""
import os
import re
import shutil
import sys

QUI = os.path.dirname(os.path.abspath(__file__))
PACCHETTI = os.path.join(QUI, "..", "usb", "nbt-evo-apps", "pacchetti")
PLUGIN = [("Meteo", "Meteo"), ("Notizie", "Notizie"), ("Locali", "Notizie locali"), ("Carburanti", "Carburanti"),
          ("Traffico", "Traffico"), ("Auto", "Stato vettura")]
MARCHE = ("mini", "bmw", "supra")


def main(src):
    inst = os.path.join(src, "00-installa.sh")
    if not os.path.isfile(inst):
        sys.exit(f"ERROR: {inst} missing - is it an iDrive Plus stick folder?")
    testa = open(inst, encoding="utf-8", errors="replace").read(600)
    m = re.search(r"USB stick v(\d+)", testa)
    if not m:
        sys.exit("ERROR: version (USB stick vNN) not found in 00-installa.sh")
    versione = "v" + m.group(1)
    for nome, titolo in PLUGIN:
        sorg = os.path.join(src, nome)
        for f in (f"lib{nome}.so", f"{nome}.xml", "attesa.png"):
            if not os.path.isfile(os.path.join(sorg, f)):
                sys.exit(f"ERROR: {sorg}/{f} missing (the libraries are built, not in the repository: use the release zip)")
        dst = os.path.join(PACCHETTI, nome)
        shutil.rmtree(dst, ignore_errors=True)
        os.makedirs(dst)
        for f in sorted(os.listdir(sorg)):
            if os.path.isfile(os.path.join(sorg, f)):
                shutil.copy2(os.path.join(sorg, f), os.path.join(dst, f))
        for marca in MARCHE:
            mv = os.path.join(src, "marche", marca, nome)
            if not os.path.isdir(mv):
                sys.exit(f"ERROR: {mv} missing")
            os.makedirs(os.path.join(dst, "marche", marca))
            for f in sorted(os.listdir(mv)):
                shutil.copy2(os.path.join(mv, f), os.path.join(dst, "marche", marca, f))
        righe = [f"name={nome}", f"title={titolo} (iDrive Plus)", f"version={versione}", "group=iDrive Plus",
                 "brands=all", "dest=plugin", "variants=" + " ".join(MARCHE), "variant_from=phone",
                 f"reset={nome}.avvii", "disable_file=/opt/aa/ponte.disable"]
        if nome == "Meteo":
            righe.insert(6, "replaces=MeteoProva")
        with open(os.path.join(dst, "package.txt"), "w", newline="\n") as fh:
            fh.write("\n".join(righe) + "\n")
        print(f"{nome} {versione}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    main(sys.argv[1])
