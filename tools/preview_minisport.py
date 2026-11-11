#!/usr/bin/env python3
"""
Draws the screens of the host test (tests/host_minisport.sh) as the HMI would: for every <name>.txt of the test
(state shown, theme, picture sent to each model, picture id of the zip of each model, components moved) the
components of that state of MiniSport.xml are drawn in their order over an approximate MINI background
(MID blue / SPORT black: drawn here, no picture of the head unit).
Usage: python3 tools/preview_minisport.py tests/out     -> tests/out/<name>.png and tests/out/all.png
"""
import glob
import io
import os
import sys
import xml.etree.ElementTree as ET
import zipfile

import numpy as np
from PIL import Image

QUI = os.path.dirname(os.path.abspath(__file__))
PKG = os.path.join(QUI, "..", "usb", "nbt-evo-apps", "MiniSport")
W, H = 1280, 480


def sfondo(tema):
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    if tema == 0:
        t = np.clip(1 - np.abs(xx / W - 0.55) * 1.9, 0, 1)
        band = np.clip(1 - np.abs((xx / W - 0.5) - (yy / H - 0.5) * 0.45) * 2.2, 0, 1)
        k = (0.35 * t + 0.65 * band * t)[..., None]
        a, b = np.array([18, 58, 84], np.float32), np.array([84, 132, 142], np.float32)
        rgb = a + (b - a) * k
    else:
        t = np.clip(1 - np.hypot((xx / W - 0.45) * 1.3, (yy / H - 0.35)) * 1.5, 0, 1)
        v = 6 + 22 * t
        rgb = np.stack([v, v * 1.02, v * 1.08], -1)
    return Image.fromarray(np.concatenate([rgb, np.full((H, W, 1), 255, np.float32)], -1).astype(np.uint8), "RGBA")


def componenti():
    """state id -> list of (component id, model, x, y, w, h) in XML order"""
    root = ET.parse(os.path.join(PKG, "MiniSport.xml")).getroot()
    out = {}
    for st in root.iter("hmiState"):
        lista = []
        for c in st.find("components"):
            if c.tag != "image":
                continue
            pr = {int(p.get("id")): p.get("value") for p in c.find("properties")}
            lista.append((int(c.get("id")), int(c.get("model")), int(pr[20]), int(pr[21]), int(pr[9]), int(pr[10])))
        out[int(st.get("id"))] = lista
    return out


def main(cartella):
    comp = componenti()
    z = zipfile.ZipFile(os.path.join(PKG, "MiniSport_ALL_Images.zip"))
    cache = {}
    usciti = []
    for txt in sorted(glob.glob(os.path.join(cartella, "*.txt"))):
        stato, tema = 100, 0
        img, ids, xs, ys = {}, {}, {}, {}
        for riga in open(txt):
            p = riga.split()
            if p[0] == "state":
                stato = int(p[1])
            elif p[0] == "theme":
                tema = int(p[1])
            elif p[0] == "img":
                img[int(p[1])] = p[2]
            elif p[0] == "id":
                ids[int(p[1])] = int(p[2])
            elif p[0] == "x":
                xs[int(p[1])] = int(p[2])
            elif p[0] == "y":
                ys[int(p[1])] = int(p[2])
        tela = sfondo(tema)
        mancanti = 0
        for cid, mod, x, y, w, h in comp[stato]:
            x, y = xs.get(cid, x), ys.get(cid, y)
            if mod in img:
                pic = Image.open(img[mod]).convert("RGBA")
            elif mod in ids:
                if ids[mod] not in cache:
                    try:
                        cache[ids[mod]] = Image.open(io.BytesIO(z.read(f"{ids[mod]}.png"))).convert("RGBA")
                    except KeyError:
                        cache[ids[mod]] = None
                pic = cache[ids[mod]]
                if pic is None:
                    mancanti += 1
                    continue
            else:
                continue
            if pic.size != (w, h):
                print(f"{os.path.basename(txt)}: component {cid} is {w}x{h}, picture {pic.size}")
            tela.alpha_composite(pic, (max(x, 0), max(y, 0)), (max(-x, 0), max(-y, 0)))
        nome = txt[:-4] + ".png"
        tela.convert("RGB").save(nome)
        usciti.append(nome)
        if mancanti:
            print(f"{os.path.basename(txt)}: {mancanti} picture ids NOT in the zip")
    # sheet with everything
    cols = 2
    righe = (len(usciti) + cols - 1) // cols
    foglio = Image.new("RGB", (W * cols, H * righe), (0, 0, 0))
    for i, n in enumerate(usciti):
        foglio.paste(Image.open(n), ((i % cols) * W, (i // cols) * H))
    foglio.save(os.path.join(cartella, "all.png"))
    print(f"preview: {len(usciti)} screens in {cartella}")


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "tests/out")
