#!/usr/bin/env python3
"""
Builds the 3 USB-stick packages of the zip test (ProvaZipA, ProvaZipB, ProvaZipC): XML, manifest, texts and the zip
of pictures. In every zip the icon of the list is the LAST picture: it shows only if the HMI loaded the whole zip.
  A: 99 small pictures + icon (about 25 KB)    B: 399 small pictures + icon (about 94 KB)
  C: 49 big pictures + icon (about 2.5 MB)
Ids (free, see README): focus 32757/32756/32755, connector 221/222/223, pictures A 65100-65199, B 65200-65599,
C 65600-65649.
Second round (after A and B showed their icon and C did not, 13/11): D one 50 KB picture (as those of C) + icon;
E, F, G 100 noise pictures of about 3, 7 and 15 KB (zip of about 300 KB, 700 KB, 1.5 MB).
Ids D-G: focus 32754/32750/32749/32748, connector 224-227, pictures 66000-66001, 66100-66199, 66200-66299,
66300-66399 (ids over 65535 load: the icon of B is 65599).
Usage: python3 apps/ProvaZipA/make.py [A B ...]     (writes usb/nbt-evo-apps/pacchetti/ProvaZip<X>/)
"""
import io
import os
import random
import sys
import zipfile

from PIL import Image, ImageDraw, ImageFont

QUI = os.path.dirname(os.path.abspath(__file__))
PACCHETTI = os.path.join(QUI, "..", "..", "usb", "nbt-evo-apps", "pacchetti")
PROVE = {
    "A": dict(focus=32757, conn=221, primo=65100, n=100, lato=40, rumore=False, testo="Prova zip A: 100 immagini piccole"),
    "B": dict(focus=32756, conn=222, primo=65200, n=400, lato=40, rumore=False, testo="Prova zip B: 400 immagini piccole"),
    "C": dict(focus=32755, conn=223, primo=65600, n=50, lato=112, rumore=True, testo="Prova zip C: 50 immagini grandi"),
    # second round (13/11 evening): A and B loaded, C (2.5 MB) did not -> size of one picture or size of the zip?
    "D": dict(focus=32754, conn=224, primo=66000, n=2, lato=112, rumore=True, testo="Prova zip D: 1 immagine da 50 KB"),
    "E": dict(focus=32750, conn=225, primo=66100, n=100, lato=28, rumore=True, testo="Prova zip E: 300 KB in tutto"),
    "F": dict(focus=32749, conn=226, primo=66200, n=100, lato=42, rumore=True, testo="Prova zip F: 700 KB in tutto"),
    "G": dict(focus=32748, conn=227, primo=66300, n=100, lato=61, rumore=True, testo="Prova zip G: 1,5 MB in tutto"),
}


def png(im):
    o = io.BytesIO()
    im.save(o, "PNG")
    return o.getvalue()


def riempitivo(k, lato, rumore, rnd):
    """a picture that only takes space: small = colour gradient (about 1 KB), big = noise (about 50 KB)"""
    if rumore:
        return png(Image.frombytes("RGBA", (lato, lato), bytes(rnd.getrandbits(8) for _ in range(lato * lato * 4))))
    im = Image.new("RGBA", (lato, lato))
    d = ImageDraw.Draw(im)
    for y in range(lato):
        d.line((0, y, lato, y), fill=((k * 37) % 256, (y * 6) % 256, (k * 11 + y) % 256, 255))
    return png(im)


def icona(lettera):
    """63 x 54 like the other icons: a white frame with the letter of the test"""
    s = 4
    im = Image.new("RGBA", (63 * s, 54 * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((6 * s, 4 * s, 57 * s, 50 * s), 6 * s, outline=(255, 255, 255, 255), width=3 * s)
    f = ImageFont.truetype("DejaVuSans-Bold.ttf", 34 * s)
    d.text((31.5 * s, 27 * s), lettera, font=f, fill=(255, 255, 255, 255), anchor="mm")
    return png(im.resize((63, 54), Image.LANCZOS))


def xml(nome, p):
    icona_id = p["primo"] + p["n"] - 1
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<pluginApps>
  <pluginApp applicationType="Custom" applicationWeight="200" entryButtonFocusID="{p['focus']}" insertStateID="50" text="{p['testo']}">
    <comment>{nome} t0 (nbt-evo-apps): TEST, {p['n']} pictures in the zip, the icon ({icona_id}) is the last one</comment>
    <entryButton action="5" id="1" model="3" imageModel="4">
      <properties/>
    </entryButton>
    <actions>
      <hmiAction id="5" target="100"/>
      <raAction id="58"/>
      <raAction id="59"/>
    </actions>
    <models>
      <textIdModel id="3" textId="1">
        <synonyms/>
      </textIdModel>
      <imageIdModel id="4" imageId="{icona_id}"/>
      <raDataModel id="41"/>
      <raImageModel id="141"/>
    </models>
    <events/>
    <hmiStates>
      <hmiState id="100" text="{p['testo']}" textModel="3">
        <properties>
          <property id="24" value="3"/>
        </properties>
        <components>
          <button action="59" selectAction="58" id="200" model="41" imageModel="141" text="{p['testo']}">
            <properties>
              <property id="9" value="860"/>
              <property id="10" value="44"/>
              <property id="20" value="150"/>
              <property id="21" value="74"/>
            </properties>
          </button>
        </components>
      </hmiState>
    </hmiStates>
  </pluginApp>
</pluginApps>
"""


def manifest(nome, p):
    return f"""<?xml version="1.0" encoding="utf-8"?>
<plugin>
\t<name>{nome}</name>
\t<type>App</type>
\t<file>{{pluginDir}}/{nome}</file>
\t<config>
\t\t<autoStart>yes</autoStart>
\t\t<dependencies>
\t\t\t<pluginAppConnector>
\t\t\t\t<type>Etch</type>
\t\t\t\t<id>{p['conn']}</id>
\t\t\t\t<asyncCallbacks>yes</asyncCallbacks>
\t\t\t</pluginAppConnector>
\t\t</dependencies>
\t\t<parameters>
\t\t\t<DescFile>{{pluginDir}}/{nome}.xml</DescFile>
\t\t\t<TextDBFile>{{pluginDir}}/{nome}_ALL_Texts.zip</TextDBFile>
\t\t\t<ImageDBFile>{{pluginDir}}/{nome}_ALL_Images.zip</ImageDBFile>
\t\t</parameters>
\t</config>
</plugin>
"""


def main():
    nomi = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Español", "es-US": "Español (US)",
            "fr-CA": "Français (CA)", "fr-FR": "Français", "it": "Italiano", "pt": "Português", "ru": "Русский"}
    scelte = sys.argv[1:] or list(PROVE)
    for lettera in scelte:
        p = PROVE[lettera]
        nome = "ProvaZip" + lettera
        out = os.path.join(PACCHETTI, nome)
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, f"{nome}.xml"), "w", encoding="utf-8", newline="\n") as f:
            f.write(xml(nome, p))
        with open(os.path.join(out, f"{nome}.manifest"), "w", encoding="utf-8", newline="\n") as f:
            f.write(manifest(nome, p))
        with zipfile.ZipFile(os.path.join(out, f"{nome}_ALL_Texts.zip"), "w", zipfile.ZIP_STORED) as z:
            for lingua, nome_lingua in nomi.items():
                z.writestr(f"{lingua}.txt", f"100000={nome_lingua}\n1={p['testo']}\n".encode("utf-8"))
        rnd = random.Random(lettera)
        zp = os.path.join(out, f"{nome}_ALL_Images.zip")
        with zipfile.ZipFile(zp, "w", zipfile.ZIP_STORED) as z:
            for k in range(p["n"] - 1):
                z.writestr(f"{p['primo'] + k}.png", riempitivo(k, p["lato"], p["rumore"], rnd))
            z.writestr(f"{p['primo'] + p['n'] - 1}.png", icona(lettera))
        print(f"ok: {nome}: {p['n']} pictures, zip {os.path.getsize(zp)} bytes, icon {p['primo'] + p['n'] - 1} last")


if __name__ == "__main__":
    main()
