#!/usr/bin/env python3
"""
Builds what SportTest needs on the USB stick: SportTest.xml and the pictures (background, the two PNG samples,
rev bar, G-meter dot, LED, digits). The geometry is the same as plugins/SportTest/sport_test.cpp.
Usage: python3 tools/make_sporttest.py   (writes usb/nbt-evo-apps/SportTest/)
Needs Pillow and the Inter font (fonts-inter on Ubuntu).
"""
import os
from PIL import Image, ImageDraw, ImageFont

QUI = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(QUI, "..", "usb", "nbt-evo-apps", "SportTest")
FONT = "/usr/share/fonts/opentype/inter/Inter-{}.otf"

W, H = 1280, 480
BAR_X, BAR_Y, BAR_W, BAR_H = 20, 40, 1100, 40
LED_N, LED_W, LED_H, LED_Y, LED_PASSO = 10, 92, 26, 92, 112
CIFRE, CIFRA_W, CIFRA_H, CIFRA_X, CIFRA_Y, CIFRA_PASSO = 4, 80, 110, 20, 140, 86
CERCHIO_X, CERCHIO_Y, RAGGIO, PUNTO = 980, 300, 130, 36
GIRI_MAX = 7000
BOTTONI = [(200, 30, 41, 94, "Fase", (380, 206, 440, 56)), (201, 31, 42, 95, "Dati", (380, 140, 440, 56))]
CAMPIONE = (380, 100)
CAMPIONI = [(72, 302, "sample256.png", (20, 340)), (73, 303, "samplergba.png", (420, 340))]
ZIP_W, ZIP_H, ZIP_X, ZIP_Y, ZIP_PASSO = 50, 68, 380, 268, 54     # second row of digits: pictures of the zip
ID_ICONA, ID_CIFRA0 = 64001, 64100
ACCENTO = (0, 168, 255, 255)


def font(sz, peso="SemiBold"):
    return ImageFont.truetype(FONT.format(peso), sz)


def salva(im, nome, tavolozza=False):
    if tavolozza:
        im = im.quantize(256, method=Image.Quantize.FASTOCTREE)
    im.save(os.path.join(OUT, nome), optimize=True)


def sfondo():
    im = Image.new("RGBA", (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    # rev bar frame and scale
    d.rounded_rectangle((BAR_X - 3, BAR_Y - 3, BAR_X + BAR_W + 3, BAR_Y + BAR_H + 3), 6, outline=(255, 255, 255, 110), width=2)
    for g in range(0, GIRI_MAX + 1, 1000):
        x = BAR_X + g * BAR_W // GIRI_MAX
        d.line((x, BAR_Y + BAR_H + 4, x, BAR_Y + BAR_H + 10), fill=(255, 255, 255, 160), width=2)
        d.text((x, BAR_Y - 6), str(g // 1000), font=font(18, "Medium"), fill=(220, 230, 240, 255), anchor="mb")
    # LED sockets (switched-off LEDs)
    for i in range(LED_N):
        x = BAR_X + i * LED_PASSO
        d.rounded_rectangle((x, LED_Y, x + LED_W, LED_Y + LED_H), 6, fill=(255, 255, 255, 28), outline=(255, 255, 255, 70))
    d.text((CIFRA_X, CIFRA_Y + CIFRA_H + 6), "giri/min", font=font(22, "Medium"), fill=(210, 225, 235, 255))
    # G-meter rings
    for r, a in [(RAGGIO, 140), (RAGGIO // 2, 90)]:
        d.ellipse((CERCHIO_X - r, CERCHIO_Y - r, CERCHIO_X + r, CERCHIO_Y + r), outline=(255, 255, 255, a), width=2)
    d.line((CERCHIO_X - RAGGIO, CERCHIO_Y, CERCHIO_X + RAGGIO, CERCHIO_Y), fill=(255, 255, 255, 60))
    d.line((CERCHIO_X, CERCHIO_Y - RAGGIO, CERCHIO_X, CERCHIO_Y + RAGGIO), fill=(255, 255, 255, 60))
    d.text((CERCHIO_X + RAGGIO + 6, CERCHIO_Y - RAGGIO), "G", font=font(24, "Bold"), fill=(255, 255, 255, 220))
    d.text((CERCHIO_X - RAGGIO, CERCHIO_Y + RAGGIO + 8), "punto: posizione (fase 3)", font=font(18, "Medium"),
           fill=(210, 225, 235, 255))
    d.text((ZIP_X + 4 * ZIP_PASSO + 8, ZIP_Y + 20), "cifre dallo zip (fase 4)", font=font(18, "Medium"), fill=(210, 225, 235, 255))
    d.text((20, 446), "PNG a 256 colori", font=font(20, "Medium"), fill=(210, 225, 235, 255))
    d.text((420, 446), "PNG a colori pieni (RGBA)", font=font(20, "Medium"), fill=(210, 225, 235, 255))
    d.text((1130, 452), "SportTest t1", font=font(18, "Medium"), fill=(210, 225, 235, 200), anchor="ra")
    salva(im, "background.png")


def campione(nome, tavolozza):
    """what the pages are made of: a translucent card, an icon with soft edges, text"""
    w, h = CAMPIONE
    s = 3
    im = Image.new("RGBA", (w * s, h * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, w * s - 1, h * s - 1), 14 * s, fill=(255, 255, 255, 46), outline=(255, 255, 255, 120), width=2 * s)
    d.ellipse((14 * s, 14 * s, 86 * s, 86 * s), fill=(255, 196, 0, 255))
    d.ellipse((40 * s, 30 * s, 110 * s, 80 * s), fill=(235, 240, 245, 210))
    d.text((240 * s, 50 * s), "256 colori" if tavolozza else "RGBA", font=font(32 * s, "Bold"), fill=(255, 255, 255, 255), anchor="mm")
    salva(im.resize((w, h), Image.LANCZOS), nome, tavolozza)


def barra():
    im = Image.new("RGBA", (BAR_W, BAR_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    for x in range(BAR_W):
        t = x / (BAR_W - 1)
        c = (int(40 + 215 * t), int(200 - 150 * max(0, t - 0.5) * 2), int(255 * (1 - t)), 255) if t < 0.85 else (255, 40, 40, 255)
        d.line((x, 0, x, BAR_H), fill=c)
    salva(im, "bar.png")


def punto():
    s = 4
    im = Image.new("RGBA", (PUNTO * s, PUNTO * s), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.ellipse((2 * s, 2 * s, (PUNTO - 2) * s, (PUNTO - 2) * s), fill=(255, 60, 60, 255), outline=(255, 255, 255, 255), width=3 * s)
    salva(im.resize((PUNTO, PUNTO), Image.LANCZOS), "dot.png")


def led():
    im = Image.new("RGBA", (LED_W, LED_H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, LED_W - 1, LED_H - 1), 6, fill=(255, 70, 40, 255), outline=(255, 210, 190, 255), width=2)
    salva(im, "led.png", True)


def cifra(c, w, h, sz, colore=(255, 255, 255, 255)):
    im = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    if c != "_":
        ImageDraw.Draw(im).text((w // 2, h // 2), c, font=font(sz, "Bold"), fill=colore, anchor="mm")
    return im


def cifre():
    for c in list("0123456789") + ["_"]:
        salva(cifra(c, CIFRA_W, CIFRA_H, 104), f"digit{c}.png", True)


def zip_immagini():
    """SportTest_ALL_Images.zip: the menu icon and the digits of the second row (<id>.png, as the other plugins)"""
    import io
    import zipfile
    with zipfile.ZipFile(os.path.join(OUT, "SportTest_ALL_Images.zip"), "w", zipfile.ZIP_STORED) as z:
        icona = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
        d = ImageDraw.Draw(icona)
        d.arc((4, 8, 44, 48), 200, 340, fill=(255, 255, 255, 255), width=5)
        d.line((24, 30, 36, 16), fill=(255, 255, 255, 255), width=4)
        d.ellipse((20, 26, 28, 34), fill=(255, 255, 255, 255))
        for nome, im in [(ID_ICONA, icona)] + [(ID_CIFRA0 + i, cifra(c, ZIP_W, ZIP_H, 64, ACCENTO)) for i, c in enumerate(list("0123456789") + ["_"])]:
            b = io.BytesIO()
            im.save(b, "PNG", optimize=True)
            z.writestr(f"{nome}.png", b.getvalue())


def zip_testi():
    """SportTest_ALL_Texts.zip: text 1 is the name in the menu (same languages and format as the other plugins)"""
    import zipfile
    # language: (line 100000 = name of the language, as in the other plugins; line 1 = name in the menu)
    nomi = {"de": ("Deutsch", "Sport-Test"), "en-UK": ("English (UK)", "Sport test"), "en-US": ("English (US)", "Sport test"),
            "es-ES": ("Español", "Prueba deportiva"), "es-US": ("Español (US)", "Prueba deportiva"),
            "fr-CA": ("Français (CA)", "Test sport"), "fr-FR": ("Français", "Test sport"), "it": ("Italiano", "Prova sport"),
            "pt": ("Português", "Teste desportivo"), "ru": ("Русский", "Спорт тест")}
    with zipfile.ZipFile(os.path.join(OUT, "SportTest_ALL_Texts.zip"), "w", zipfile.ZIP_STORED) as z:
        for lingua, (nome_lingua, nome) in nomi.items():
            z.writestr(f"{lingua}.txt", f"100000={nome_lingua}\n1={nome}\n".encode("utf-8"))


def manifest():
    with open(os.path.join(OUT, "SportTest.manifest"), "w", encoding="utf-8") as f:
        f.write("""<?xml version="1.0" encoding="utf-8"?>
<plugin>
\t<name>SportTest</name>
\t<type>App</type>
\t<file>{pluginDir}/SportTest</file>
\t<config>
\t\t<autoStart>yes</autoStart>
\t\t<dependencies>
\t\t\t<pluginAppConnector>
\t\t\t\t<type>Etch</type>
\t\t\t\t<id>217</id>
\t\t\t\t<asyncCallbacks>yes</asyncCallbacks>
\t\t\t</pluginAppConnector>
\t\t</dependencies>
\t\t<parameters>
\t\t\t<DescFile>{pluginDir}/SportTest.xml</DescFile>
\t\t\t<TextDBFile>{pluginDir}/SportTest_ALL_Texts.zip</TextDBFile>
\t\t\t<ImageDBFile>{pluginDir}/SportTest_ALL_Images.zip</ImageDBFile>
\t\t</parameters>
\t</config>
</plugin>
""")


def immagine(p, ind, id_, modello, x, y, w, h, visibile=True):
    p(f'{ind}<image id="{id_}" model="{modello}">')
    p(f"{ind}  <properties>")
    for k, v in [(2, "false"), (3, "true" if visibile else "false"), (9, w), (10, h), (20, x), (21, y)]:
        p(f'{ind}    <property id="{k}" value="{v}"/>')
    p(f"{ind}  </properties>")
    p(f"{ind}</image>")


def xml():
    a = []
    p = a.append
    p('<?xml version="1.0" encoding="UTF-8"?>')
    p("<pluginApps>")
    p('  <pluginApp applicationType="Custom" applicationWeight="200" entryButtonFocusID="32761" insertStateID="50" text="Prova sport">')
    p("    <comment>SportTest t1 (nbt-evo-apps): test of car data rates, picture swaps, setProperty, ImageID and 256-colour PNG</comment>")
    p('    <entryButton action="5" id="1" model="3" imageModel="4">')
    p("      <properties/>")
    p("    </entryButton>")
    p("    <actions>")
    p('      <hmiAction id="5" target="100"/>')
    for _, azione, *_ in BOTTONI:
        p(f'      <raAction id="{azione}"/>')
    p("    </actions>")
    p("    <models>")
    p('      <textIdModel id="3" textId="1">')
    p("        <synonyms/>")
    p("      </textIdModel>")
    p(f'      <imageIdModel id="4" imageId="{ID_ICONA}"/>')
    for _, _, testo, img, *_ in BOTTONI:
        p(f'      <raDataModel id="{testo}"/>')
        p(f'      <raImageModel id="{img}"/>')
    for m in [71, 72, 73, 74, 75] + list(range(80, 80 + LED_N)) + list(range(90, 90 + CIFRE)) + list(range(96, 96 + CIFRE)):
        p(f'      <raImageModel id="{m}"/>')
    p("    </models>")
    p("    <events/>")
    p("    <hmiStates>")
    p('      <hmiState id="100" text="Prova sport" textModel="3">')
    p("        <properties>")
    p('          <property id="24" value="3"/>')
    p("        </properties>")
    p("        <components>")
    i = "          "
    immagine(p, i, 300, 71, 0, 0, W, H)
    for m, cid, _, (x, y) in CAMPIONI:
        immagine(p, i, cid, m, x, y, *CAMPIONE)
    immagine(p, i, 310, 74, BAR_X, BAR_Y, BAR_W, BAR_H)
    immagine(p, i, 311, 75, CERCHIO_X - PUNTO // 2, CERCHIO_Y - PUNTO // 2, PUNTO, PUNTO)
    for k in range(LED_N):
        immagine(p, i, 320 + k, 80 + k, BAR_X + k * LED_PASSO, LED_Y, LED_W, LED_H, visibile=False)
    for k in range(CIFRE):
        immagine(p, i, 330 + k, 90 + k, CIFRA_X + k * CIFRA_PASSO, CIFRA_Y, CIFRA_W, CIFRA_H)
    for k in range(CIFRE):
        immagine(p, i, 340 + k, 96 + k, ZIP_X + k * ZIP_PASSO, ZIP_Y, ZIP_W, ZIP_H)
    for cid, azione, testo, img, etichetta, (x, y, w, h) in BOTTONI:
        p(f'{i}<button action="{azione}" id="{cid}" model="{testo}" imageModel="{img}" text="{etichetta}">')
        p(f"{i}  <properties>")
        for k, v in [(9, w), (10, h), (20, x), (21, y)]:
            p(f'{i}    <property id="{k}" value="{v}"/>')
        p(f"{i}  </properties>")
        p(f"{i}</button>")
    p("        </components>")
    p("      </hmiState>")
    p("    </hmiStates>")
    p("  </pluginApp>")
    p("</pluginApps>")
    with open(os.path.join(OUT, "SportTest.xml"), "w", encoding="utf-8") as f:
        f.write("\n".join(a) + "\n")


os.makedirs(OUT, exist_ok=True)
sfondo()
campione("sample256.png", True)
campione("samplergba.png", False)
barra()
punto()
led()
cifre()
zip_immagini()
zip_testi()
manifest()
xml()
print("ok:", OUT)
