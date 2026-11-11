#!/usr/bin/env python3
"""
Builds the USB-stick package of "App da USB" (AppUsb): AppUsb.xml, manifest, texts and the icon. The screens are
lists of native buttons (text drawn by the HMI itself, like the iDrive Plus news titles), the plugin writes the text
of every row. Layout, models and actions are the same as apps/AppUsb/app_usb.cpp (kStato*, kMod*, kAz*).
Usage: python3 apps/AppUsb/make.py     (writes usb/nbt-evo-apps/pacchetti/AppUsb/)

How a "press" reaches the plugin: an action on the press (raAction) never arrived in SportTest t1, while the
selectAction (the focus arriving on a button) works in iDrive Plus. So pressing a button only opens another
state, and the first button of that state, which gets the focus as the state opens, sends the selectAction
that starts the work (STATE_TRIGGER below).
"""
import io
import os
import zipfile

from PIL import Image, ImageDraw

QUI = os.path.dirname(os.path.abspath(__file__))
NOME = "AppUsb"
OUT = os.path.join(QUI, "..", "..", "usb", "nbt-evo-apps", "pacchetti", NOME)
VERSIONE = dict(r.rstrip("\n").split("=", 1) for r in open(os.path.join(QUI, "package.txt"), encoding="utf-8")
                if "=" in r)["version"]
FOCUS, CONNETTORE, ID_ICONA = 32759, 219, 64300

# rows: native buttons, x, width, height, first y, step (screen 1280x480, safe area x 10-1140, y 6-420)
RX, RW, RH, RY0, RPASSO = 150, 860, 44, 74, 48
NO_OP = 59                                   # raAction of rows that do nothing when pressed

# state id, title, number of rows, model of row 0, selectAction of row 0 (rows: +1 each, or the same if
# "stessa"), action of the press of each row (hmiAction id or NO_OP)
STATI = [
    # main: 0 diagnosis, 1 car and phone app, 2-6 packages / groups
    dict(id=100, titolo="App da USB", righe=7, mod=41, sel=20, stessa=False, premi=[7, 9, 6, 6, 6, 6, 6]),
    # detail of a package or group: 0-5 information, 6 install
    dict(id=110, titolo="Pacchetto", righe=7, mod=51, sel=30, stessa=False, premi=[NO_OP] * 6 + [8]),
    # diagnosis: every row starts it when it gets the focus (the first one gets it as the state opens)
    dict(id=120, titolo="Diagnosi", righe=7, mod=61, sel=40, stessa=True, premi=[NO_OP] * 7),
    # install: the first row starts it (t0: only says what it would do)
    dict(id=130, titolo="Installazione", righe=4, mod=71, sel=60, stessa=False, premi=[NO_OP] * 4),
    # car and phone app: the row with the focus is remembered, the press saves it (state 145)
    dict(id=140, titolo="Auto e telefono", righe=5, mod=81, sel=50, stessa=False, premi=[10] * 5),
    dict(id=145, titolo="Auto e telefono", righe=2, mod=91, sel=66, stessa=False, premi=[NO_OP] * 2),
]
HMI_AZIONI = {5: 100, 6: 110, 7: 120, 8: 130, 9: 140, 10: 145}
TESTI_FISSI = {   # rows whose text never changes: in the XML as button text, written again by the plugin at start
    81: "Auto: MINI", 82: "Auto: BMW", 83: "Auto: Toyota Supra",
    84: "iDrive Plus, app del telefono: NBT Plus", 85: "iDrive Plus, app del telefono: MINI / BMW / Supra Plus",
}


def icona():
    """63 x 54 like the iDrive Plus icons: a USB stick with an arrow into it, white"""
    s = 4
    im = Image.new("RGBA", (63 * s, 54 * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    b = (255, 255, 255, 255)
    d.rounded_rectangle((22 * s, 18 * s, 41 * s, 50 * s), 4 * s, outline=b, width=3 * s)           # body
    d.rectangle((26 * s, 8 * s, 37 * s, 18 * s), outline=b, width=3 * s)                           # plug
    d.rectangle((29 * s, 11 * s, 31 * s, 14 * s), fill=b)
    d.rectangle((33 * s, 11 * s, 35 * s, 14 * s), fill=b)
    d.line((31.5 * s, 25 * s, 31.5 * s, 41 * s), fill=b, width=3 * s)                               # arrow
    d.polygon([(25 * s, 36 * s), (38 * s, 36 * s), (31.5 * s, 44 * s)], fill=b)
    im = im.resize((63, 54), Image.LANCZOS)
    o = io.BytesIO()
    im.save(o, "PNG", optimize=True)
    return o.getvalue()


def xml():
    a = []
    p = a.append
    p('<?xml version="1.0" encoding="UTF-8"?>')
    p("<pluginApps>")
    p(f'  <pluginApp applicationType="Custom" applicationWeight="200" entryButtonFocusID="{FOCUS}" insertStateID="50" text="App da USB">')
    p(f"    <comment>App da USB {VERSIONE} (nbt-evo-apps): packages of the USB stick - t0 = diagnosis only, nothing is installed</comment>")
    p('    <entryButton action="5" id="1" model="3" imageModel="4">')
    p("      <properties/>")
    p("    </entryButton>")
    p("    <actions>")
    for k, v in HMI_AZIONI.items():
        p(f'      <hmiAction id="{k}" target="{v}"/>')
    sel = set()
    for st in STATI:
        for r in range(st["righe"]):
            sel.add(st["sel"] if st["stessa"] else st["sel"] + r)
    for s in sorted(sel | {NO_OP}):
        p(f'      <raAction id="{s}"/>')
    p("    </actions>")
    p("    <models>")
    p('      <textIdModel id="3" textId="1">')
    p("        <synonyms/>")
    p("      </textIdModel>")
    p(f'      <imageIdModel id="4" imageId="{ID_ICONA}"/>')
    for st in STATI:
        for r in range(st["righe"]):
            m = st["mod"] + r
            p(f'      <raDataModel id="{m}"/>')
            p(f'      <raImageModel id="{m + 100}"/>')        # as the iDrive Plus buttons: text + picture model
    p("    </models>")
    p("    <events/>")
    p("    <hmiStates>")
    cid = 200
    for st in STATI:
        padre = "" if st["id"] == 100 else ' parentId="100"'
        p(f'      <hmiState id="{st["id"]}"{padre} text="{st["titolo"]}" textModel="3">')
        p("        <properties>")
        p('          <property id="24" value="3"/>')
        p("        </properties>")
        p("        <components>")
        for r in range(st["righe"]):
            s = st["sel"] if st["stessa"] else st["sel"] + r
            m = st["mod"] + r
            testo = TESTI_FISSI.get(m, " ")
            p(f'          <button action="{st["premi"][r]}" selectAction="{s}" id="{cid}" model="{m}" imageModel="{m + 100}" text="{testo}">')
            p("            <properties>")
            for k, v in [(9, RW), (10, RH), (20, RX), (21, RY0 + r * RPASSO)]:
                p(f'              <property id="{k}" value="{v}"/>')
            p("            </properties>")
            p("          </button>")
            cid += 1
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
            z.writestr(f"{lingua}.txt", f"100000={nome_lingua}\n1=App da USB\n".encode("utf-8"))
    with zipfile.ZipFile(os.path.join(OUT, f"{NOME}_ALL_Images.zip"), "w", zipfile.ZIP_STORED) as z:
        z.writestr(f"{ID_ICONA}.png", icona())
    print(f"ok: {NOME} {VERSIONE}")


if __name__ == "__main__":
    main()
