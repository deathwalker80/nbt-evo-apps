#!/usr/bin/env python3
"""
Builds the USB-stick package of "Lettura dati" (LetturaDati), a TEST plugin that only reads: LetturaDati.xml,
manifest, texts and the icon. One page with 7 rows of text written by the plugin (models 41-47, the same as
apps/LetturaDati/lettura_dati.cpp kModRiga0 / kRighe); the rows do nothing when pressed.
Usage: python3 apps/LetturaDati/make.py     (writes usb/nbt-evo-apps/pacchetti/LetturaDati/)
"""
import io
import os
import zipfile

from PIL import Image, ImageDraw

QUI = os.path.dirname(os.path.abspath(__file__))
NOME = "LetturaDati"
OUT = os.path.join(QUI, "..", "..", "usb", "nbt-evo-apps", "pacchetti", NOME)
VERSIONE = dict(r.rstrip("\n").split("=", 1) for r in open(os.path.join(QUI, "package.txt"), encoding="utf-8")
                if "=" in r)["version"]
FOCUS, CONNETTORE, ID_ICONA = 32758, 220, 64160   # free: see README (and NBT-AAD: 32752-32753, 210, 55001-55002)
RX, RW, RH, RY0, RPASSO = 60, 1060, 44, 74, 48    # rows: x, width, height, first y, step
RIGHE, MOD0, SEL0, NO_OP = 7, 41, 20, 59


def icona():
    """63 x 54 like the iDrive Plus icons: a magnifying glass over three bars, white"""
    s = 4
    im = Image.new("RGBA", (63 * s, 54 * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    b = (255, 255, 255, 255)
    for i, h in enumerate((14, 24, 34)):
        x = (8 + i * 10) * s
        d.rectangle((x, (48 - h) * s, x + 6 * s, 48 * s), fill=b)
    d.ellipse((30 * s, 6 * s, 50 * s, 26 * s), outline=b, width=3 * s)
    d.line((47 * s, 23 * s, 57 * s, 33 * s), fill=b, width=4 * s)
    im = im.resize((63, 54), Image.LANCZOS)
    o = io.BytesIO()
    im.save(o, "PNG", optimize=True)
    return o.getvalue()


def xml():
    a = []
    p = a.append
    p('<?xml version="1.0" encoding="UTF-8"?>')
    p("<pluginApps>")
    p(f'  <pluginApp applicationType="Custom" applicationWeight="200" entryButtonFocusID="{FOCUS}" insertStateID="50" text="Lettura dati">')
    p(f"    <comment>Lettura dati {VERSIONE} (nbt-evo-apps): TEST, reads the car data and writes them to its log only</comment>")
    p('    <entryButton action="5" id="1" model="3" imageModel="4">')
    p("      <properties/>")
    p("    </entryButton>")
    p("    <actions>")
    p('      <hmiAction id="5" target="100"/>')
    for s in list(range(SEL0, SEL0 + RIGHE)) + [NO_OP]:
        p(f'      <raAction id="{s}"/>')
    p("    </actions>")
    p("    <models>")
    p('      <textIdModel id="3" textId="1">')
    p("        <synonyms/>")
    p("      </textIdModel>")
    p(f'      <imageIdModel id="4" imageId="{ID_ICONA}"/>')
    for r in range(RIGHE):
        p(f'      <raDataModel id="{MOD0 + r}"/>')
        p(f'      <raImageModel id="{MOD0 + r + 100}"/>')
    p("    </models>")
    p("    <events/>")
    p("    <hmiStates>")
    p('      <hmiState id="100" text="Lettura dati" textModel="3">')
    p("        <properties>")
    p('          <property id="24" value="3"/>')
    p("        </properties>")
    p("        <components>")
    for r in range(RIGHE):
        p(f'          <button action="{NO_OP}" selectAction="{SEL0 + r}" id="{200 + r}" model="{MOD0 + r}" imageModel="{MOD0 + r + 100}" text=" ">')
        p("            <properties>")
        for k, v in [(9, RW), (10, RH), (20, RX), (21, RY0 + r * RPASSO)]:
            p(f'              <property id="{k}" value="{v}"/>')
        p("            </properties>")
        p("          </button>")
    p("        </components>")
    p("      </hmiState>")
    p("    </hmiStates>")
    p("  </pluginApp>")
    p("</pluginApps>")
    return "\n".join(a) + "\n"


def manifest():
    return f"""<?xml version="1.0" encoding="utf-8"?>
<plugin>
\t<name>{NOME}</name>
\t<type>App</type>
\t<file>{{pluginDir}}/{NOME}</file>
\t<config>
\t\t<autoStart>yes</autoStart>
\t\t<dependencies>
\t\t\t<pluginAppConnector>
\t\t\t\t<type>Etch</type>
\t\t\t\t<id>{CONNETTORE}</id>
\t\t\t\t<asyncCallbacks>yes</asyncCallbacks>
\t\t\t</pluginAppConnector>
\t\t</dependencies>
\t\t<parameters>
\t\t\t<DescFile>{{pluginDir}}/{NOME}.xml</DescFile>
\t\t\t<TextDBFile>{{pluginDir}}/{NOME}_ALL_Texts.zip</TextDBFile>
\t\t\t<ImageDBFile>{{pluginDir}}/{NOME}_ALL_Images.zip</ImageDBFile>
\t\t</parameters>
\t</config>
</plugin>
"""


def main():
    os.makedirs(OUT, exist_ok=True)
    with open(os.path.join(OUT, f"{NOME}.xml"), "w", encoding="utf-8", newline="\n") as f:
        f.write(xml())
    with open(os.path.join(OUT, f"{NOME}.manifest"), "w", encoding="utf-8", newline="\n") as f:
        f.write(manifest())
    nomi = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Español", "es-US": "Español (US)",
            "fr-CA": "Français (CA)", "fr-FR": "Français", "it": "Italiano", "pt": "Português", "ru": "Русский"}
    with zipfile.ZipFile(os.path.join(OUT, f"{NOME}_ALL_Texts.zip"), "w", zipfile.ZIP_STORED) as z:
        for lingua, nome_lingua in nomi.items():
            z.writestr(f"{lingua}.txt", f"100000={nome_lingua}\n1=Lettura dati\n".encode("utf-8"))
    with zipfile.ZipFile(os.path.join(OUT, f"{NOME}_ALL_Images.zip"), "w", zipfile.ZIP_STORED) as z:
        z.writestr(f"{ID_ICONA}.png", icona())
    print(f"ok: {NOME} {VERSIONE}")


if __name__ == "__main__":
    main()
