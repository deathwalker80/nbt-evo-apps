#!/usr/bin/env python3
"""
Builds MINI Sport for the USB stick and the layout shared with the C++ plugin:
  usb/nbt-evo-apps/pacchetti/MiniSport/  MiniSport.xml, .manifest, _ALL_Texts.zip, _ALL_Images.zip (small moving pictures,
                               switched by id), page backgrounds bg_<page>_<theme>.png (sent as pictures, rarely)
  apps/MiniSport/layout.h   ids of components, models and pictures, positions
What the car showed in SportTest t1 decides how things move:
  - moving a picture (setProperty x/y): works (G-meter dot)       -> G dot, centring of numbers
  - switching a picture of the zip by id (ImageID): works          -> LEDs, segments, digits, bars, labels
  - changing the width of a picture (setProperty 9): does NOT work -> bars are pictures in 21 steps
Usage: python3 apps/MiniSport/make.py      (Pillow, numpy, Inter font; ~1-3 minutes)
"""
import io
import math
import os
import sys
import zipfile
from multiprocessing import Pool

QUI = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, QUI)
import minisport_gfx as g                      # noqa: E402

NOME = "MiniSport"
VERSIONE = dict(r.rstrip("\n").split("=", 1) for r in open(os.path.join(QUI, "package.txt"), encoding="utf-8")
                if "=" in r)["version"]                # the version is written only in package.txt
OUT = os.path.join(QUI, "..", "..", "usb", "nbt-evo-apps", "pacchetti", NOME)
HEADER = os.path.join(QUI, "layout.h")
TEMI = ("mid", "sport")
ID_ICONA = 64200
ID_PRIMO = 64201                               # 64201..65499: pictures of the zip
FOCUS = 32760
CONNETTORE = 218

# ---- geometry (screen pixels of the 1280x480 page; safe area x 10-1140, y 6-420) ------------------------------
GC = (222, 244)                                # big round dial (revs, timer)
GR = 172
SEG = dict(r0=147, r1=166, a0=225, a1=-45, n=30, gap=4.6)
GIRI_MAX, GIRI_ROSSO = 7000, 6000
SHIFT = dict(x0=452, x1=1120, y=104, n=10)
VALORI_X, VALORI_Y, VALORI_W = (440, 794), (150, 290), 330
GM = dict(cx=222, cy=244, r=164)
GRAFICO = dict(x0=456, passo=22, w=12, base=410, hmax=80, n=30, livelli=8)
CR_RIGHE_Y0, CR_RIGA = 160, 64
MOT = dict(cx0=150, passo=283, cy=214, r=112, n=22)
MENU_X0, MENU_W = 15, 280

# ---- what goes in the zip and on the screen ------------------------------------------------------------------
# a SET is a list of frames (pictures of the same size); frames of a set are the same for every component that
# uses it. Per theme: ids[theme][frame]. A COMPONENT is an image of a state, with its own model.


class Set:
    def __init__(self, nome, box, frames, disegna, per_tema=True):
        self.nome, self.box, self.frames, self.disegna, self.per_tema = nome, box, list(frames), disegna, per_tema
        self.ids = None


SETS = {}
COMPONENTI = []                                # (state, cname, set name or None, x, y, w, h)


def acceso_spento(disegna):
    """frames "on" / "off": "off" is a transparent picture of the same size (the HMI gets the same size always)"""
    return lambda tema, fr, box: g.vuoto() if fr == "off" else disegna(tema, fr, box)


def nuovo_set(nome, box, frames, disegna, per_tema=True):
    """box = (x0, y0, x1, y1) where it is drawn (screen coordinates); the picture is that rectangle"""
    SETS[nome] = Set(nome, tuple(int(v) for v in box), frames, disegna, per_tema)
    return SETS[nome]


def componente(stato, cnome, set_nome, dx=0, dy=0):
    s = SETS[set_nome]
    x0, y0, x1, y1 = s.box
    COMPONENTI.append((stato, cnome, set_nome, x0 + dx, y0 + dy, x1 - x0, y1 - y0))


# ---- digits ------------------------------------------------------------------------------------------------------
CIFRE = list("0123456789") + ["vuoto"]


def cella(sz, peso="SemiBold"):
    f = g.F(sz, peso, True)
    return int(math.ceil(max(f.getlength(c) for c in "0123456789") / g.S)) + 2


def set_cifre(nome, sz, col=g.BIANCO, peso="SemiBold", scuro=False, per_tema=False, extra=()):
    """digits 0-9 + empty (+ extra characters) in a cell: the box is placed at (0,0) and the components move it"""
    cw = max(cella(sz, peso), int(math.ceil(max([g.F(sz, peso, True).getlength(c) / g.S for c in extra] or [0]))) + 2)
    pad = 0 if scuro else 8
    h = int(sz * 1.25) + pad * 2
    w = cw + pad * 2

    def disegna(tema, fr, box):
        lay = g.vuoto()
        if fr == "vuoto":
            return lay
        c = col(tema) if callable(col) else col
        g.numero(lay, (box[0] + w / 2, box[1] + h / 2), fr, sz, c, "mm", peso, scuro=scuro)
        return lay
    nuovo_set(nome, (0, 0, w, h), CIFRE + list(extra), disegna, per_tema)
    return w, h, pad, cw


def posa(stato, cnome, set_nome, x_cella, y_centro):
    """a digit component whose cell starts at x_cella, centred vertically on y_centro"""
    s = SETS[set_nome]
    w, h = s.box[2], s.box[3]
    pad = (w - cella_di(set_nome)) // 2
    componente(stato, cnome, set_nome, int(round(x_cella - pad)), int(round(y_centro - h / 2)))


def cifre_in_fila(stato, prefisso, set_nome, x_sinistra, y_centro, n):
    """n digit components side by side, the first one's cell starting at x_sinistra"""
    for k in range(n):
        posa(stato, f"{prefisso}{k}", set_nome, x_sinistra + k * cella_di(set_nome), y_centro)


CELLE = {}


def cella_di(set_nome):
    return CELLE[set_nome]


def registra_cifre(nome, sz, **kw):
    w, h, pad, cw = set_cifre(nome, sz, **kw)
    CELLE[nome] = cw
    return cw


# ---- pages -----------------------------------------------------------------------------------------------------
def colori_giri(tema, k):
    T = g.TEMI[tema]
    n = SEG["n"]
    rosso_da = int(GIRI_ROSSO / GIRI_MAX * n)
    if k >= rosso_da:
        return T["rosso"]
    if k >= rosso_da - 4:
        return T["caldo"]
    if k >= rosso_da - 12:
        return T["acc"]
    return T["chiaro"]


SEGMENTI = g.segmenti_arco(GC[0], GC[1], SEG["r0"], SEG["r1"], SEG["a0"], SEG["a1"], SEG["n"], SEG["gap"])


def shift_forme():
    n = SHIFT["n"]
    w = (SHIFT["x1"] - SHIFT["x0"]) / n
    y = SHIFT["y"]
    out = []
    for i in range(n):
        xa = SHIFT["x0"] + i * w + 6
        xb = xa + w - 12
        out.append([(xa + 5, y - 12), (xb + 5, y - 12), (xb - 5, y + 12), (xa - 5, y + 12)])
    return out


SHIFT_FORME = shift_forme()


def colore_shift(tema, i):
    T = g.TEMI[tema]
    return g.VERDE if i < 4 else (T["caldo"] if i < 7 else T["rosso"])


def sfondo_giri(tema):
    T = g.TEMI[tema]
    dst = g.vuoto()
    g.testata(dst, "Giri e cambiata", tema, "cambiata a 6.200")
    cx, cy = GC
    g.lente_nativa(dst, cx, cy, GR, interno=0.78)
    for f in SEGMENTI:
        g.led_spento(dst, f)
    ri = GR * 0.78
    for k in range(8):
        a = math.radians(225 - k * 270 / 7)
        colr = T["rosso"] if k >= 6 else g.BIANCO
        g.linea(dst, (cx + (ri - 4) * math.cos(a), cy - (ri - 4) * math.sin(a)),
                (cx + (ri - 16) * math.cos(a), cy - (ri - 16) * math.sin(a)), 3, colr, 0.85)
        g.testo(dst, (cx + (ri - 32) * math.cos(a), cy - (ri - 32) * math.sin(a)), str(k), 18, colr, 0.88, "Medium", "mm", disp=True)
    g.testo(dst, (cx, cy + 20), "giri/min", 17, g.BIANCO, 0.80, "Regular", "mm")
    g.disco_chiaro(dst, cx, cy + 80, 30, tema)
    # shift light: groove fading at the ends, switched-off LEDs
    x0, x1, y = SHIFT["x0"], SHIFT["x1"], SHIFT["y"]
    t = g.np.clip(1 - g.np.abs((g.XX - (x0 + x1) / 2) / ((x1 - x0) / 2 + 30)) ** 6, 0, 1)
    g.sopra(dst, g.colore((0, 0, 0), g.liscio(g.np.abs(g.YY - y) - 22) * t * 0.40))
    g.separatore(dst, x0 - 20, x1 + 20, y - 23, 0.30)
    g.separatore(dst, x0 - 20, x1 + 20, y + 23, 0.18)
    for f in SHIFT_FORME:
        g.led_spento(dst, f)
    for i, (ic, nome, unita) in enumerate(VALORI_GIRI):
        x, yv = VALORI_X[i % 2], VALORI_Y[i // 2]
        g.disco_icona(dst, x + 24, yv + 30, ic)
        g.etichetta(dst, x + 62, yv + 10, nome)
        g.testo(dst, (x + 62 + 3 * cella_di("c40") + 8, yv + 60), unita, 20, g.BIANCO, 0.82, "Regular", "lm")
        g.binario(dst, x + 62, yv + 92, x + VALORI_W - 8, 6)
    g.separatore(dst, 440, 1132, 272)
    g.separatore_v(dst, 786, 160, 400)
    return dst


VALORI_GIRI = [("goccia", "Olio", "°C"), ("termo", "Acqua", "°C"), ("pedale", "Gas", "%"), ("coppia", "Coppia", "Nm")]
BARRE_GIRI = ["verde", "verde", "acc", "caldo"]


def colore_barra(tema, tipo):
    T = g.TEMI[tema]
    return {"verde": g.VERDE, "acc": T["acc"], "caldo": T["caldo"]}[tipo]


def sfondo_gmeter(tema):
    T = g.TEMI[tema]
    dst = g.vuoto()
    g.testata(dst, "G-meter", tema, "accelerazioni")
    cx, cy, R = GM["cx"], GM["cy"], GM["r"]
    g.lente_nativa(dst, cx, cy, R + 10, anello=T["acc"], interno=0.0, ombra_lunga=False)
    g.lente_nativa(dst, cx, cy, R * 0.52, interno=0.0, ombra_lunga=False)
    rr = g.np.hypot(g.XX - cx, g.YY - cy)
    for ang in (0, 90):
        a = math.radians(ang)
        m = g.maschera(lambda d: d.line([g.P(cx - R * math.cos(a), cy - R * math.sin(a)), g.P(cx + R * math.cos(a), cy + R * math.sin(a))],
                                        fill=255, width=2 * g.S))
        g.sopra(dst, g.colore(g.BIANCO, m * 0.25 * g.liscio(rr - R + 4)))
    g.testo(dst, (cx + R * 0.52 + 6, cy + 14), "0,5", 17, g.BIANCO, 0.75, "Regular", "lm")
    g.testo(dst, (cx + R - 6, cy + 14), "1 g", 17, g.BIANCO, 0.75, "Regular", "rm")
    g.etichetta(dst, 452, 84, "Adesso")
    xs = posizioni_g_adesso()
    g.numero(dst, (xs["virgola"], 150), ",", 84, anchor="mm")
    g.testo(dst, (xs["fine"] + 10, 168), "g", 26, g.BIANCO, 0.82, "Regular", "lm")
    for i, n in enumerate(["Sinistra", "Destra", "Spinta", "Frenata"]):
        x, y = 800 + (i % 2) * 170, 84 + (i // 2) * 76
        g.etichetta(dst, x, y, n)
        p = posizioni_g_max(x)
        g.numero(dst, (p["virgola"], y + 42), ",", 34, anchor="mm")
        g.testo(dst, (p["fine"] + 6, y + 48), "g", 18, g.BIANCO, 0.78, "Regular", "lm")
    g.separatore(dst, 800, 1132, 154, 0.35)
    g.separatore_v(dst, 786, 84, 226)
    g.separatore(dst, 440, 1132, 244)
    g.etichetta(dst, 452, 258, "Ultimo minuto")
    g.separatore(dst, 450, 1126, GRAFICO["base"] + 3, 0.30)
    return dst


def posizioni_g_adesso():
    c = cella_di("c84")
    v = g.larghezza(",", 84, "SemiBold", True)
    x = 452
    return {"d0": x, "virgola": x + c + v / 2, "d1": x + c + v, "d2": x + 2 * c + v, "fine": x + 3 * c + v}


def posizioni_g_max(x):
    c = cella_di("c34")
    v = g.larghezza(",", 34, "SemiBold", True)
    return {"d0": x, "virgola": x + c + v / 2, "d1": x + c + v, "d2": x + 2 * c + v, "fine": x + 3 * c + v}


TEST_CRONO = [("0-100 km/h", "s"), ("0-50 km/h", "s"), ("80-120 km/h", "s"), ("Frenata 100-0", "m")]


def posizioni_riga(i):
    """digits of row i, right-aligned before the unit: dd,dd s  or  ddd,d m"""
    y = CR_RIGHE_Y0 + i * CR_RIGA + 31
    unita = TEST_CRONO[i][1]
    xu = 1110 - g.larghezza(unita, 20, "Regular")
    c = cella_di("c30")
    v = g.larghezza(",", 30, "SemiBold", True)
    fine = xu - 6
    if unita == "s":     # d d , d d
        return y, xu, [fine - 4 * c - v, fine - 3 * c - v, fine - 2 * c, fine - c], fine - 2 * c - v / 2
    return y, xu, [fine - 4 * c - v, fine - 3 * c - v, fine - 2 * c - v, fine - c], fine - c - v / 2


def sfondo_crono(tema):
    T = g.TEMI[tema]
    dst = g.vuoto()
    g.testata(dst, "Cronometri", tema, "parti da fermo")
    cx, cy = GC
    g.lente_nativa(dst, cx, cy, GR, interno=0.78)
    for f in SEGMENTI:
        g.led_spento(dst, f)
    g.testo(dst, (cx - 122, cy + 146), "0", 18, g.BIANCO, 0.82, "Medium", "mm", disp=True)
    g.testo(dst, (cx + 122, cy + 146), "100", 18, g.BIANCO, 0.82, "Medium", "mm", disp=True)
    g.testo(dst, (cx, cy + 40), "secondi", 18, g.BIANCO, 0.82, "Regular", "mm")
    g.testo(dst, (cx + 6, cy + 82), "km/h", 20, T["acc2"], 1.0, "Medium", "lm")
    for i in range(3):
        g.lente_nativa(dst, 760 + i * 52, 94, 18, interno=0.0, ombra_lunga=False)
    g.testo(dst, (452, 126), "parti quando vuoi: il tempo scatta da solo", 18, g.BIANCO, 0.78, "Regular", "lm")
    for i, (nome, unita) in enumerate(TEST_CRONO):
        y, xu, _, xv = posizioni_riga(i)
        g.testo(dst, (474, y), nome, 24, g.BIANCO, 1.0, "Medium", "lm")
        g.testo(dst, (850, y), "migliore", 18, g.BIANCO, 0.75, "Regular", "lm")
        g.testo(dst, (1110, y + 4), unita, 20, g.BIANCO, 0.82, "Regular", "rm")
        g.numero(dst, (xv, y), ",", 30, anchor="mm")
        if i < len(TEST_CRONO) - 1:
            g.separatore(dst, 452, 1120, y + 31, 0.40)
    return dst


DATI_MOTORE = [("goccia", "Olio", "°C", 50, 150), ("termo", "Acqua", "°C", 40, 130), ("pedale", "Gas", "%", 0, 100),
               ("coppia", "Coppia", "Nm", 0, 300)]


def zone_motore(tema, i):
    T = g.TEMI[tema]
    return [[(0, g.FREDDO), (.35, g.VERDE), (.85, T["rosso"])], [(0, g.FREDDO), (.35, g.VERDE), (.85, T["rosso"])],
            [(0, T["acc"]), (.9, T["caldo"])], [(0, T["acc"]), (.8, T["caldo"])]][i]


def segmenti_motore(i):
    cx = MOT["cx0"] + i * MOT["passo"]
    return g.segmenti_arco(cx, MOT["cy"], MOT["r"] - 18, MOT["r"] - 4, 220, -40, MOT["n"], 4.0)


def sfondo_motore(tema):
    dst = g.vuoto()
    g.testata(dst, "Strumenti motore", tema, "dal motore, in tempo reale")
    for i, (ic, nome, unita, _, _) in enumerate(DATI_MOTORE):
        cx, cy, r = MOT["cx0"] + i * MOT["passo"], MOT["cy"], MOT["r"]
        g.lente_nativa(dst, cx, cy, r + 10, interno=0.64)
        for f in segmenti_motore(i):
            g.led_spento(dst, f)
        g.glifo(dst, cx, cy - 46, ic, g.BIANCO, 0.75)
        g.testo(dst, (cx, cy + 36), unita, 18, g.BIANCO, 0.82, "Regular", "mm")
        g.etichetta(dst, cx, cy + r + 26, nome, "mm", 0.88)
    g.separatore(dst, 18, 1132, 374)
    g.etichetta(dst, 30, 398, "Batteria", "lm")
    g.testo(dst, (POS_BATT + 3 * cella_di("c28") + 6, 401), "%", 18, g.BIANCO, 0.78, "Regular", "lm")
    g.etichetta(dst, 380, 398, "Sterzo", "lm")
    g.testo(dst, (POS_STERZO + 4 * cella_di("c28") + 4, 398), "°", 22, g.BIANCO, 0.78, "Regular", "lm")
    g.etichetta(dst, 760, 398, "Frizione", "lm")
    g.lente_nativa(dst, 900, 398, 12, interno=0.0, ombra_lunga=False)
    return dst


POS_BATT, POS_STERZO = 160, 480


# icons: apps/MiniSport/icone_mie/ if present (your own, never in the repository), else apps/MiniSport/icone/
ICONE = os.path.join(QUI, "icone_mie") if os.path.isdir(os.path.join(QUI, "icone_mie")) else os.path.join(QUI, "icone")
ICONE_MENU = {"giri": "giri", "g": "gmeter", "crono": "crono", "motore": "motore"}   # <icons folder>/<name>.png
LATO_ICONA_MENU = 160
VOCI_MENU = [("giri", "Giri e cambiata", "shift light"), ("g", "G-meter", "accelerazioni"),
             ("crono", "Cronometri", "0-100 e frenata"), ("motore", "Motore", "temperature e coppia")]


def sfondo_menu(tema, scelta):
    T = g.TEMI[tema]
    dst = g.vuoto()
    g.testata(dst, "MINI Sport", tema)
    for i, (ic, nome, sotto) in enumerate(VOCI_MENU):
        x = MENU_X0 + i * MENU_W
        cx, cy = x + MENU_W / 2, 180
        scelto = i == scelta
        if scelto:
            g.colonna_scelta(dst, x + 34, x + MENU_W - 34)
        g.immagine(dst, os.path.join(ICONE, f"{ICONE_MENU[ic]}.png"), cx, cy, LATO_ICONA_MENU,
                   tinta=T["acc"] if tema == "mid" else None)          # icons drawn red: blue in MID/GREEN
        g.testo(dst, (cx, 300), nome.upper(), 19, g.BIANCO, 1.0, "Medium", "mm", track=0.8)
        g.testo(dst, (cx, 334), sotto, 19, T["acc2"] if scelto else g.BIANCO, 1.0 if scelto else 0.80, "Regular", "mm")
        g.sopra(dst, g.colore(g.BIANCO, g.liscio(g.d_rrect(cx - 22, 367, cx + 22, 369, 1)) * 0.85))
    return dst


SFONDI = {"giri": sfondo_giri, "gmeter": sfondo_gmeter, "crono": sfondo_crono, "motore": sfondo_motore}


# ---- sets and components -----------------------------------------------------------------------------------------
def pad_box(pts, pad):
    return g.bbox(pts, pad)


def definisci():
    # digit sets (cells) - must exist before the backgrounds use cella_di
    registra_cifre("c56", 56, extra=["."])                     # revs (with the thousands dot)
    registra_cifre("c40", 40)                                   # values of the revs page
    registra_cifre("c84", 84)                                   # G now
    registra_cifre("c34", 34)                                   # G max
    registra_cifre("c76", 76, extra=[","])                     # timer (comma moves with the digits)
    registra_cifre("c22", 22, col=lambda t: g.TEMI[t]["acc2"], peso="Medium", per_tema=True)   # timer speed
    registra_cifre("c30", 30, extra=["-"])                     # timer records ("-" = no record yet)
    registra_cifre("c42", 42)                                   # engine dials
    registra_cifre("c28", 28, extra=["-"])                     # battery, steering
    registra_cifre("marcia", 40, col=g.SCURO, scuro=True, extra=["N", "R", "-"])

    # --- revs page (state 110) ---
    for k, f in enumerate(SEGMENTI):
        b = pad_box(f, 12)
        nuovo_set(f"seg{k}", b, ["on", "off"], acceso_spento(lambda tema, fr, box, k=k, f=f: _led(tema, f, colori_giri(tema, k), 6)))
        componente(110, f"giri_seg{k}", f"seg{k}")
    for i, f in enumerate(SHIFT_FORME):
        b = pad_box(f, 14)
        nuovo_set(f"shift{i}", b, ["on", "off"], acceso_spento(lambda tema, fr, box, i=i, f=f: _led(tema, f, colore_shift(tema, i), 8)))
        componente(110, f"giri_shift{i}", f"shift{i}")
    # revs digits: d0 . d1 d2 d3, centred by the plugin around GC[0]
    for k in range(4):                                      # placed by the plugin (centred)
        posa(110, f"giri_cifra{k}", "c56", GC[0] - 2 * cella_di("c56") + k * cella_di("c56"), GC[1] - 18)
    posa(110, "giri_punto", "c56", GC[0], GC[1] - 18)
    componente(110, "giri_marcia", "marcia", int(GC[0] - SETS["marcia"].box[2] / 2), int(GC[1] + 81 - SETS["marcia"].box[3] / 2))
    for i in range(4):
        x, y = VALORI_X[i % 2], VALORI_Y[i // 2]
        cifre_in_fila(110, f"giri_val{i}_", "c40", x + 62, y + 52, 3)
        bx0, by, bx1 = x + 62, y + 92, x + VALORI_W - 8
        tipo = BARRE_GIRI[i]
        nuovo_set(f"barra_{tipo}", (bx0 - 10, by - 14, bx1 + 10, by + 14), [str(s) for s in range(21)],
                  lambda tema, fr, box, tipo=tipo, bx0=bx0, bx1=bx1, by=by: _barra(tema, tipo, bx0, by, bx1, int(fr) / 20))
        COMPONENTI.append((110, f"giri_barra{i}", f"barra_{tipo}", bx0 - 10, by - 14, bx1 - bx0 + 20, 28))

    # --- G-meter page (state 111) ---
    for nome, r, al in (("punto", 14, 1.0), ("scia0", 7, 0.38), ("scia1", 6, 0.24), ("scia2", 5, 0.14)):
        lato = 2 * (r + 14)
        nuovo_set(f"g_{nome}", (0, 0, lato, lato), ["on"], lambda tema, fr, box, r=r, al=al, nome=nome: _punto(tema, box, r, al, nome == "punto"))
        componente(111, f"g_{nome}", f"g_{nome}", -1000, -1000)
    xs = posizioni_g_adesso()
    for k in range(3):
        posa(111, f"g_adesso{k}", "c84", xs[f"d{k}"], 150)
    for k in range(4):
        q = posizioni_g_max(800 + (k % 2) * 170)
        for j in range(3):
            posa(111, f"g_max{k}_{j}", "c34", q[f"d{j}"], 84 + (k // 2) * 76 + 42)
    nuovo_set("g_testo", (446, 196, 790, 228), ["dritto", "curva a destra", "curva a sinistra", "accelera", "frena"],
              lambda tema, fr, box: _scritta(tema, box, fr))
    componente(111, "g_testo", "g_testo")
    gr = GRAFICO
    nuovo_set("g_barra", (0, 0, gr["w"] + 8, gr["hmax"] + 8), [str(s) for s in range(gr["livelli"] + 1)],
              lambda tema, fr, box: _barretta(tema, box, int(fr)))
    for k in range(gr["n"]):
        componente(111, f"g_graf{k}", "g_barra", gr["x0"] + k * gr["passo"] - 4, gr["base"] - gr["hmax"] - 4)

    # --- timers page (state 112) ---
    for k, f in enumerate(SEGMENTI):
        b = pad_box(f, 12)
        nuovo_set(f"cseg{k}", b, ["on", "off"], acceso_spento(lambda tema, fr, box, f=f: _led(tema, f, g.TEMI[tema]["acc"], 6)))
        componente(112, f"cr_seg{k}", f"cseg{k}")
    nuovo_set("cr_nome", (GC[0] - 120, GC[1] - 92, GC[0] + 120, GC[1] - 64), [t[0] for t in TEST_CRONO],
              lambda tema, fr, box: _etichetta_c(box, fr))
    componente(112, "cr_nome", "cr_nome")
    c76 = cella_di("c76")
    for k in range(5):                                      # d0 d1 , d2 d3 centred by the plugin
        posa(112, f"cr_tempo{k}", "c76", GC[0] - 2.5 * c76 + k * c76, GC[1] - 8)
    for k in range(3):                                      # speed: right-aligned before "km/h"
        posa(112, f"cr_vel{k}", "c22", GC[0] - 2 - (3 - k) * cella_di("c22"), GC[1] + 82)
    nuovo_set("cr_stato", (446, 72, 700, 98), ["pronto", "via", "fermati", "fatto", "fermo"],
              lambda tema, fr, box: _stato(box, fr))
    componente(112, "cr_stato", "cr_stato")
    nuovo_set("cr_luci", (736, 70, 888, 118), ["spente", "rosso", "verde", "corsa"], lambda tema, fr, box: _luci(tema, fr))
    componente(112, "cr_luci", "cr_luci")
    for i in range(4):
        yr, _, xs4, _ = posizioni_riga(i)
        for j in range(4):
            posa(112, f"cr_riga{i}_{j}", "c30", xs4[j], yr)
        y = CR_RIGHE_Y0 + i * CR_RIGA + 31
        nuovo_set(f"cr_segno{i}", (448, int(y - 12), 472, int(y + 12)), ["on", "off"],
                  acceso_spento(lambda tema, fr, box, y=y: _segno(tema, y)))
        componente(112, f"cr_segno{i}", f"cr_segno{i}")

    # --- engine page (state 113) ---
    for i in range(4):
        segs = segmenti_motore(i)
        tutti = [p for f in segs for p in f]
        b = pad_box(tutti, 10)
        nuovo_set(f"quadrante{i}", b, [str(s) for s in range(MOT["n"] + 1)],
                  lambda tema, fr, box, i=i, segs=segs: _quadrante(tema, i, segs, int(fr)))
        componente(113, f"mot_quad{i}", f"quadrante{i}")
        cx = MOT["cx0"] + i * MOT["passo"]
        for j in range(3):                                  # centred by the plugin
            posa(113, f"mot_val{i}_{j}", "c42", cx - 1.5 * cella_di("c42") + j * cella_di("c42"), MOT["cy"] + 4)
    cifre_in_fila(113, "mot_batt", "c28", POS_BATT, 398, 3)
    cifre_in_fila(113, "mot_sterzo", "c28", POS_STERZO, 398, 4)
    nuovo_set("frizione", (884, 382, 916, 414), ["on", "off"], acceso_spento(lambda tema, fr, box: _frizione(tema)))
    componente(113, "mot_frizione", "frizione")


# drawing helpers of the sprites (run in the sprite window)
def _led(tema, forma, col, alone):
    lay = g.vuoto()
    g.led_acceso(lay, forma, col, alone)
    return lay


def _barra(tema, tipo, x0, y, x1, frac):
    lay = g.vuoto()
    g.riempimento(lay, x0, y, x1, frac, colore_barra(tema, tipo), 6)
    return lay


def _punto(tema, box, r, al, pieno):
    T = g.TEMI[tema]
    lay = g.vuoto()
    cx, cy = (box[0] + box[2]) / 2, (box[1] + box[3]) / 2
    if not pieno:
        g.sopra(lay, g.colore(T["acc"], g.liscio(g.d_cerchio(cx, cy, r)) * al))
        return lay
    c = g.colore(T["acc"], g.liscio(g.d_cerchio(cx, cy, r)))
    g.sopra(lay, g.bagliore(c, 10, 1.4))
    g.sopra(lay, c)
    rr2 = g.np.hypot(g.XX - (cx - 4), g.YY - (cy - 5))
    g.sopra(lay, g.colore(g.BIANCO, g.np.clip(1 - rr2 / 8, 0, 1) ** 1.5 * 0.85 * g.liscio(g.d_cerchio(cx, cy, r))))
    g.sopra(lay, g.colore(g.BIANCO, g.liscio(g.np.abs(g.d_cerchio(cx, cy, r)) - 0.8) * 0.8))
    return lay


def _scritta(tema, box, fr):
    lay = g.vuoto()
    if fr != "dritto":
        g.testo(lay, (box[0] + 6, (box[1] + box[3]) / 2), fr, 20, g.TEMI[tema]["acc2"], 1.0, "Regular", "lm")
    return lay


def _barretta(tema, box, livello):
    lay = g.vuoto()
    if livello == 0:
        return lay
    h = GRAFICO["hmax"] * livello / GRAFICO["livelli"]
    x0 = box[0] + 4
    yb = box[3] - 4
    g.sopra(lay, g.colore(g.TEMI[tema]["acc"], g.liscio(g.d_rrect(x0, yb - h, x0 + GRAFICO["w"], yb, GRAFICO["w"] / 2 - 2)) * 0.85))
    return lay


def _etichetta_c(box, fr):
    lay = g.vuoto()
    g.etichetta(lay, (box[0] + box[2]) / 2, (box[1] + box[3]) / 2, fr, "mm", 0.92)
    return lay


def _stato(box, fr):
    lay = g.vuoto()
    testo = {"pronto": "Pronto", "via": "Via!", "fermati": "Fermati per partire", "fatto": "Fatto", "fermo": "In attesa"}[fr]
    g.etichetta(lay, box[0] + 6, (box[1] + box[3]) / 2, testo, "lm", 0.92)
    return lay


def _luci(tema, fr):
    lay = g.vuoto()
    T = g.TEMI[tema]
    for i in range(3):
        sx, sy = 760 + i * 52, 94
        col = None
        if fr == "rosso" and i == 0:
            col = T["rosso"]
        if fr == "verde" and i == 2:
            col = g.VERDE
        if fr == "corsa":
            col = T["acc"]
        if col:
            c = g.colore(col, g.liscio(g.d_cerchio(sx, sy, 13)))
            g.sopra(lay, g.bagliore(c, 8, 1.3))
            g.sopra(lay, c)
            g.sopra(lay, g.colore(g.BIANCO, g.np.clip(1 - g.np.hypot(g.XX - sx + 4, g.YY - sy + 4) / 7, 0, 1) * 0.7))
    return lay


def _segno(tema, y):
    lay = g.vuoto()
    c = g.colore(g.TEMI[tema]["acc"], g.liscio(g.d_cerchio(460, y, 5)))
    g.sopra(lay, g.bagliore(c, 4, 1.2))
    g.sopra(lay, c)
    return lay


def _quadrante(tema, i, segs, accesi):
    lay = g.vuoto()
    n = MOT["n"]
    for k in range(accesi):
        col = g.TEMI[tema]["acc"]
        for soglia, c in zone_motore(tema, i):
            if k / n >= soglia:
                col = c
        g.led_acceso(lay, segs[k], col, 4)
    return lay


def _frizione(tema):
    lay = g.vuoto()
    c = g.colore(g.TEMI[tema]["acc"], g.liscio(g.d_cerchio(900, 398, 8)))
    g.sopra(lay, g.bagliore(c, 5, 1.3))
    g.sopra(lay, c)
    return lay


# ---- rendering (parallel) ----------------------------------------------------------------------------------------
def _png(im):
    b = io.BytesIO()
    im.save(b, "PNG", optimize=True)
    return b.getvalue()


def _png_leggero(im):
    """256-colour PNG when it looks the same (checked over a dark and a blue background), else full colour"""
    import numpy as np
    from PIL import Image
    pieno = _png(im)
    if im.width * im.height < 400:
        return pieno
    q = im.quantize(256, method=Image.Quantize.FASTOCTREE)
    leggero = _png(q)
    if len(leggero) >= len(pieno):
        return pieno
    qa = q.convert("RGBA")
    for c in ((10, 10, 12, 255), (40, 90, 105, 255)):
        bg = Image.new("RGBA", im.size, c)
        d = np.abs(np.asarray(Image.alpha_composite(bg, im), float) - np.asarray(Image.alpha_composite(bg, qa), float))[..., :3]
        if d.mean() > 1.5 or d.max() > 48:
            return pieno
    return leggero


def lavoro_set(arg):
    nome, tema = arg
    definisci_una_volta()
    s = SETS[nome]
    x0, y0, x1, y1 = s.box
    g.finestra(x0, y0, x1, y1)
    out = []
    for fr in s.frames:
        lay = s.disegna(tema, fr, s.box)
        out.append(_png_leggero(g.a_pil(lay)))
    return nome, tema, out


def lavoro_sfondo(arg):
    tipo, tema, scelta = arg
    definisci_una_volta()
    g.pagina()
    lay = sfondo_menu(tema, scelta) if tipo == "menu" else SFONDI[tipo](tema)
    nome = f"bg_menu{scelta}_{tema}.png" if tipo == "menu" else f"bg_{tipo}_{tema}.png"
    return nome, _png(g.a_pil(lay))


_FATTO = False


def definisci_una_volta():
    global _FATTO
    if not _FATTO:
        definisci()
        _FATTO = True


def icona():
    """entry of the MINI Connected list: 63 x 54 like the iDrive Plus icons, from app.png of the icons folder"""
    g.finestra(0, 0, 63, 54)
    lay = g.vuoto()
    g.immagine(lay, os.path.join(ICONE, "app.png"), 31.5, 27, 54, tinta=g.TEMI["sport"]["acc"], da="ciano")   # details red
    return _png(g.a_pil(lay))


# ---- XML, manifest, texts, header --------------------------------------------------------------------------------
STATI = [(110, "Giri e cambiata"), (111, "G-meter"), (112, "Cronometri"), (113, "Strumenti motore")]


def numera():
    """component and model ids: models 101.., components 1001.. in the order of COMPONENTI"""
    out = []
    for i, (stato, cnome, set_nome, x, y, w, h) in enumerate(COMPONENTI):
        out.append(dict(stato=stato, nome=cnome, set=set_nome, x=x, y=y, w=w, h=h, modello=101 + i, comp=1001 + i))
    return out


MOD_SFONDO = {100: 71, 110: 72, 111: 73, 112: 74, 113: 75}
COMP_SFONDO = {100: 300, 110: 301, 111: 302, 112: 303, 113: 304}


def immagine(p, ind, cid, modello, x, y, w, h):
    p(f'{ind}<image id="{cid}" model="{modello}">')
    p(f"{ind}  <properties>")
    for k, v in [(2, "false"), (3, "true"), (9, w), (10, h), (20, x), (21, y)]:
        p(f'{ind}    <property id="{k}" value="{v}"/>')
    p(f"{ind}  </properties>")
    p(f"{ind}</image>")


def xml(comp):
    a = []
    p = a.append
    p('<?xml version="1.0" encoding="UTF-8"?>')
    p("<pluginApps>")
    p(f'  <pluginApp applicationType="Custom" applicationWeight="200" entryButtonFocusID="{FOCUS}" insertStateID="50" text="MINI Sport">')
    p(f"    <comment>MINI Sport {VERSIONE} (nbt-evo-apps): shift light and revs, G-meter, timers, engine gauges - car data from the head unit only</comment>")
    p('    <entryButton action="5" id="1" model="3" imageModel="4">')
    p("      <properties/>")
    p("    </entryButton>")
    p("    <actions>")
    p('      <hmiAction id="5" target="100"/>')
    for i in range(4):
        p(f'      <hmiAction id="{6 + i}" target="{110 + i}"/>')
    for i in range(4):
        p(f'      <raAction id="{20 + i}"/>')
    p("    </actions>")
    p("    <models>")
    p('      <textIdModel id="3" textId="1">')
    p("        <synonyms/>")
    p("      </textIdModel>")
    p(f'      <imageIdModel id="4" imageId="{ID_ICONA}"/>')
    for i in range(4):
        p(f'      <raDataModel id="{41 + i}"/>')
        p(f'      <raImageModel id="{61 + i}"/>')
    for m in MOD_SFONDO.values():
        p(f'      <raImageModel id="{m}"/>')
    for c in comp:
        p(f'      <raImageModel id="{c["modello"]}"/>')
    p("    </models>")
    p("    <events/>")
    p("    <hmiStates>")
    i_ = "          "
    p('      <hmiState id="100" text="MINI Sport" textModel="3">')
    p("        <properties>")
    p('          <property id="24" value="3"/>')
    p("        </properties>")
    p("        <components>")
    immagine(p, i_, COMP_SFONDO[100], MOD_SFONDO[100], 0, 0, g.W, g.H)
    for i, (_, nome, _) in enumerate(VOCI_MENU):
        x = MENU_X0 + i * MENU_W
        p(f'{i_}<button action="{6 + i}" selectAction="{20 + i}" id="{200 + i}" model="{41 + i}" imageModel="{61 + i}" text="{nome}">')
        p(f"{i_}  <properties>")
        for k, v in [(9, MENU_W), (10, 336), (20, x), (21, 82)]:
            p(f'{i_}    <property id="{k}" value="{v}"/>')
        p(f"{i_}  </properties>")
        p(f"{i_}</button>")
    p("        </components>")
    p("      </hmiState>")
    for stato, titolo in STATI:
        p(f'      <hmiState id="{stato}" parentId="100" text="{titolo}" textModel="3">')
        p("        <properties>")
        p('          <property id="24" value="3"/>')
        p("        </properties>")
        p("        <components>")
        immagine(p, i_, COMP_SFONDO[stato], MOD_SFONDO[stato], 0, 0, g.W, g.H)
        for c in comp:
            if c["stato"] == stato:
                immagine(p, i_, c["comp"], c["modello"], c["x"], c["y"], c["w"], c["h"])
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


def testi_zip():
    nomi = {"de": "Deutsch", "en-UK": "English (UK)", "en-US": "English (US)", "es-ES": "Español", "es-US": "Español (US)",
            "fr-CA": "Français (CA)", "fr-FR": "Français", "it": "Italiano", "pt": "Português", "ru": "Русский"}
    b = io.BytesIO()
    with zipfile.ZipFile(b, "w", zipfile.ZIP_STORED) as z:
        for lingua, nome_lingua in nomi.items():
            z.writestr(f"{lingua}.txt", f"100000={nome_lingua}\n1=MINI Sport\n".encode("utf-8"))
    return b.getvalue()


def cname(s):
    return "".join(ch if ch.isalnum() else "_" for ch in s).upper()


def header(comp):
    h = []
    p = h.append
    p("// GENERATED by tools/make_minisport.py - do not edit: change the generator and run it again.")
    p("#ifndef MINISPORT_LAYOUT_H")
    p("#define MINISPORT_LAYOUT_H")
    p(f'#define MS_VERSIONE "{VERSIONE}"')
    p("")
    p("struct Pezzo { u32 modello; u32 comp; int x; int y; int w; int h; };")
    p("")
    p("// models of the page backgrounds (pictures sent from the files bg_*.png)")
    for st, m in MOD_SFONDO.items():
        p(f"const u32 kModSfondo{st} = {m};")
    p("")
    p(f"const u32 kNumPezzi = {len(comp)};")
    p("const Pezzo kPezzi[] = {")
    for c in comp:
        p(f'    {{{c["modello"]}, {c["comp"]}, {c["x"]}, {c["y"]}, {c["w"]}, {c["h"]}}},   // {c["nome"]} (state {c["stato"]}, {c["set"]})')
    p("};")
    p("enum {")
    for i, c in enumerate(comp):
        p(f'    P_{cname(c["nome"])} = {i},')
    p("};")
    p("")
    p("// picture ids of the zip: ID_<SET>[theme 0 = MID/GREEN, 1 = SPORT][frame]")
    for nome, s in SETS.items():
        p(f"const u32 ID_{cname(nome)}[2][{len(s.frames)}] = {{   // {', '.join(s.frames)}")
        for t in range(2):
            p("    {" + ", ".join(str(v) for v in s.ids[t]) + "},")
        p("};")
    p("")
    p("// the same ids grouped: IDG_<GROUP>[n][theme][frame] for the sets <group>0..<group>n-1")
    import re
    gruppi = {}
    for nome in SETS:
        m = re.match(r"^(.*?)(\d+)$", nome)
        if m and nome not in CELLE:
            gruppi.setdefault(m.group(1), []).append((int(m.group(2)), nome))
    for gn, el in gruppi.items():
        el.sort()
        nf = len(SETS[el[0][1]].frames)
        p(f"const u32 IDG_{cname(gn)}[{len(el)}][2][{nf}] = {{")
        for _, nome in el:
            ids = SETS[nome].ids
            p("    {{" + ", ".join(map(str, ids[0])) + "}, {" + ", ".join(map(str, ids[1])) + "}},")
        p("};")
    p("")
    p("// advance of the separators (pixels)")
    p(f"const int kAvanzoPunto56 = {int(round(g.larghezza('.', 56, 'SemiBold', True)))}, kAvanzoVirgola76 = {int(round(g.larghezza(',', 76, 'SemiBold', True)))};")
    p("")
    p("// cells of the digit sets (width of one digit, pixels) and size of the pictures")
    for nome, cw in CELLE.items():
        s = SETS[nome]
        p(f"const int kCella_{cname(nome)} = {cw}, kLarg_{cname(nome)} = {s.box[2]}, kAlt_{cname(nome)} = {s.box[3]};")
    p("")
    p("// geometry used by the plugin")
    p(f"const int kGiriCx = {GC[0]}, kGiriCy = {GC[1]}, kGiriMax = {GIRI_MAX}, kGiriRosso = {GIRI_ROSSO};")
    p(f"const int kSegN = {SEG['n']};")
    p(f"const int kGmCx = {GM['cx']}, kGmCy = {GM['cy']}, kGmR = {GM['r']};")
    p(f"const int kGrafN = {GRAFICO['n']}, kGrafX0 = {GRAFICO['x0']}, kGrafPasso = {GRAFICO['passo']}, kGrafBase = {GRAFICO['base']}, "
      f"kGrafLivelli = {GRAFICO['livelli']}, kGrafAltezzaBox = {SETS['g_barra'].box[3]};")
    xs = posizioni_g_adesso()
    p(f"const int kGAdessoX[3] = {{{int(xs['d0'])}, {int(xs['d1'])}, {int(xs['d2'])}}}; const int kGAdessoY = 150;")
    maxx = []
    for k in range(4):
        q = posizioni_g_max(800 + (k % 2) * 170)
        maxx.append(f"{{{int(q['d0'])}, {int(q['d1'])}, {int(q['d2'])}, {84 + (k // 2) * 76 + 42}}}")
    p(f"const int kGMax[4][4] = {{{', '.join(maxx)}}};   // x of the 3 digits, y centre")
    righe = []
    for i in range(4):
        y, _, xs4, _ = posizioni_riga(i)
        righe.append("{" + ", ".join(str(int(v)) for v in xs4) + f", {int(y)}}}")
    p(f"const int kCrRighe[4][5] = {{{', '.join(righe)}}};   // x of the 4 digits, y centre")
    p(f"const int kCrTempoCx = {GC[0]}, kCrTempoCy = {GC[1] - 8}, kCrVelFine = {GC[0] - 2}, kCrVelCy = {GC[1] + 82};")
    p(f"const int kMotCx0 = {MOT['cx0']}, kMotPasso = {MOT['passo']}, kMotCy = {MOT['cy'] + 4}, kMotN = {MOT['n']};")
    mins = ", ".join(str(d[3]) for d in DATI_MOTORE)
    maxs = ", ".join(str(d[4]) for d in DATI_MOTORE)
    p(f"const int kMotMin[4] = {{{mins}}}, kMotMax[4] = {{{maxs}}};")
    ks = ", ".join(str(int(MOT["n"] * 65536 / (d[4] - d[3]))) for d in DATI_MOTORE)
    p(f"const int kMotK[4] = {{{ks}}};   // lit segments = (value - min) * K >> 16")
    p(f"const int kGiriCifreCy = {GC[1] - 18};")
    p(f"const u32 kIdIcona = {ID_ICONA};")
    p("")
    p("#endif")
    return "\n".join(h) + "\n"


def main():
    definisci_una_volta()
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(os.path.dirname(HEADER), exist_ok=True)
    lavori = []
    for nome, s in SETS.items():
        for tema in (TEMI if s.per_tema else ("mid",)):
            lavori.append((nome, tema))
    sfondi = [(t, tema, 0) for t in SFONDI for tema in TEMI] + [("menu", tema, k) for tema in TEMI for k in range(4)]
    with Pool() as pool:
        ris_set = pool.map(lavoro_set, lavori)
        ris_sf = pool.map(lavoro_sfondo, sfondi)
    png = {}
    for nome, tema, frames in ris_set:
        png[(nome, tema)] = frames
    prossimo = ID_PRIMO
    zip_b = io.BytesIO()
    with zipfile.ZipFile(zip_b, "w", zipfile.ZIP_STORED) as z:
        z.writestr(f"{ID_ICONA}.png", icona())
        for nome, s in SETS.items():
            s.ids = [[0] * len(s.frames), [0] * len(s.frames)]
            for t, tema in enumerate(TEMI):
                if not s.per_tema and t == 1:
                    s.ids[1] = list(s.ids[0])
                    continue
                for f, dati in enumerate(png[(nome, tema)]):
                    s.ids[t][f] = prossimo
                    z.writestr(f"{prossimo}.png", dati)
                    prossimo += 1
    if prossimo > 65500:
        raise SystemExit(f"too many pictures: last id {prossimo}")
    with open(os.path.join(OUT, f"{NOME}_ALL_Images.zip"), "wb") as f:
        f.write(zip_b.getvalue())
    with open(os.path.join(OUT, f"{NOME}_ALL_Texts.zip"), "wb") as f:
        f.write(testi_zip())
    for nome, dati in ris_sf:
        with open(os.path.join(OUT, nome), "wb") as f:
            f.write(dati)
    comp = numera()
    with open(os.path.join(OUT, f"{NOME}.xml"), "w", encoding="utf-8") as f:
        f.write(xml(comp))
    with open(os.path.join(OUT, f"{NOME}.manifest"), "w", encoding="utf-8") as f:
        f.write(manifest())
    with open(HEADER, "w", encoding="utf-8") as f:
        f.write(header(comp))
    grandi = max(len(d) for _, d in ris_sf)
    print(f"ok: {len(comp)} components, pictures in the zip {prossimo - ID_PRIMO + 1} (ids {ID_ICONA}..{prossimo - 1}), "
          f"zip {len(zip_b.getvalue()) // 1024} KB, largest background {grandi // 1024} KB")


if __name__ == "__main__":
    main()
