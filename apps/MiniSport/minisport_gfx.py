"""
Drawing materials of MINI Sport (native ID6 look, from photos of the car): translucent lens discs, thin LED
segments with glow, lists with fading lines, light "selected" discs, digits with soft shadow.
Everything is drawn here: no picture of the head unit is used.

All the functions draw in the current WINDOW (screen coordinates of the 1280x480 page, drawn at S times the size and
reduced at the end): the whole page for the backgrounds, a small rectangle for a sprite. Pictures are arrays
(H, W, 4) of premultiplied floats 0..1.
"""
import math
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

W, H = 1280, 480
S = 2
FONT_DIR = "/usr/share/fonts/opentype/inter/"

TEMI = {
    "mid":   dict(acc=(70, 205, 235), acc2=(150, 235, 250), caldo=(255, 170, 40), rosso=(255, 64, 56), chiaro=(200, 240, 255)),
    "sport": dict(acc=(255, 60, 48), acc2=(255, 150, 120), caldo=(255, 160, 30), rosso=(255, 40, 40), chiaro=(240, 240, 240)),
}
BIANCO = (255, 255, 255)
SCURO = (28, 44, 54)
VERDE = (90, 230, 140)
FREDDO = (120, 190, 255)

# ---- window ----------------------------------------------------------------------------------------------------
OX, OY, WS, HS = 0, 0, W * S, H * S
XX = YY = None


def finestra(x0, y0, x1, y1):
    """draw from now on in the rectangle x0..x1, y0..y1 of the page (integers)"""
    global OX, OY, WS, HS, XX, YY
    OX, OY = int(x0), int(y0)
    WS, HS = int(x1 - x0) * S, int(y1 - y0) * S
    yy, xx = np.mgrid[0:HS, 0:WS].astype(np.float32)
    XX = xx / S + OX
    YY = yy / S + OY


def pagina():
    finestra(0, 0, W, H)


def P(x, y):
    return ((x - OX) * S, (y - OY) * S)


def F(sz, peso="SemiBold", disp=True):
    fam = "InterDisplay" if disp else "Inter"
    return ImageFont.truetype(f"{FONT_DIR}{fam}-{peso}.otf", int(round(sz * S)))


# ---- layers --------------------------------------------------------------------------------------------------
def vuoto():
    return np.zeros((HS, WS, 4), np.float32)


def sopra(dst, src):
    a = src[..., 3:4]
    dst[..., :3] = src[..., :3] + dst[..., :3] * (1 - a)
    dst[..., 3:4] = a + dst[..., 3:4] * (1 - a)


def colore(col, alfa):
    c = np.asarray(col, np.float32) / 255
    out = np.zeros((HS, WS, 4), np.float32)
    out[..., 3] = alfa
    out[..., :3] = (c if c.ndim == 3 else c[None, None, :]) * alfa[..., None]
    return out


def liscio(d, w=0.75):
    return np.clip(0.5 - d / w, 0, 1)


def maschera(fn):
    im = Image.new("L", (WS, HS), 0)
    fn(ImageDraw.Draw(im))
    return np.asarray(im, np.float32) / 255


def sfoca_m(m, r):
    im = Image.fromarray((np.clip(m, 0, 1) * 255).astype(np.uint8), "L").filter(ImageFilter.GaussianBlur(r * S))
    return np.asarray(im, np.float32) / 255


def bagliore(a, r, forza=1.0):
    ch = [sfoca_m(a[..., i], r) for i in range(4)]
    return np.clip(np.stack(ch, -1) * forza, 0, 1)


def a_pil(a):
    """layer -> RGBA picture at the final size"""
    al = np.clip(a[..., 3:4], 1e-6, 1)
    rgb = np.clip(a[..., :3] / al, 0, 1)
    out = np.concatenate([rgb, np.clip(a[..., 3:4], 0, 1)], -1)
    im = Image.fromarray((out * 255 + .5).astype(np.uint8), "RGBA")
    return im.resize((WS // S, HS // S), Image.LANCZOS)


# ---- shapes --------------------------------------------------------------------------------------------------
def d_cerchio(cx, cy, r):
    return np.hypot(XX - cx, YY - cy) - r


def d_rrect(x0, y0, x1, y1, r):
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = np.abs(XX - cx) - hx, np.abs(YY - cy) - hy
    return np.hypot(np.maximum(qx, 0), np.maximum(qy, 0)) + np.minimum(np.maximum(qx, qy), 0) - r


def ombra(dst, dist, r=6, dy=3, forza=0.45):
    m = sfoca_m(liscio(dist), r)
    m = np.roll(m, int(dy * S), 0) * forza
    sopra(dst, colore((0, 0, 0), m))


def poligono(pts, col, alfa=1.0):
    m = maschera(lambda d: d.polygon([P(x, y) for x, y in pts], fill=255))
    return colore(col, m * alfa)


def ricolora(im, col, da="rosso"):
    """the saturated reds (da="rosso") or cyans (da="ciano") of a picture take the hue of col, lightness and shading
    kept: the icons follow the theme"""
    a = np.asarray(im.convert("RGBA"), np.float32) / 255
    rgb = a[..., :3]
    mx, mn = rgb.max(-1), rgb.min(-1)
    sat = np.where(mx > 0, (mx - mn) / np.maximum(mx, 1e-6), 0)
    r, gg, bb = rgb[..., 0], rgb[..., 1], rgb[..., 2]
    if da == "rosso":
        quanto = np.clip((r - np.maximum(gg, bb)) / np.maximum(mx - mn, 1e-6), 0, 1) * (r >= mx - 1e-6)
    else:
        quanto = np.clip((np.minimum(gg, bb) - r) / np.maximum(mx - mn, 1e-6), 0, 1)
    peso = quanto * np.clip((sat - 0.35) / 0.25, 0, 1) * np.clip((mx - 0.15) / 0.15, 0, 1)
    c = np.asarray(col, np.float32) / 255
    c = c / max(c.max(), 1e-6)                              # target hue at full brightness
    nuovo = mn[..., None] + (mx - mn)[..., None] * c[None, None, :]
    rgb2 = rgb * (1 - peso[..., None]) + nuovo * peso[..., None]
    out = np.concatenate([rgb2, a[..., 3:4]], -1)
    return Image.fromarray((np.clip(out, 0, 1) * 255 + .5).astype(np.uint8), "RGBA")


def immagine(dst, path, cx, cy, lato, tinta=None, da="rosso"):
    """a picture file (RGBA), cut to its drawing and fitted in a lato x lato square centred on cx, cy
    (alpha >= 250 counted as opaque)"""
    im = Image.open(path).convert("RGBA")
    if tinta is not None:
        im = ricolora(im, tinta, da)
    a = np.asarray(im, np.float32) / 255
    a[..., 3] = np.where(a[..., 3] >= 250 / 255, 1.0, a[..., 3])
    im = Image.fromarray((a * 255 + .5).astype(np.uint8), "RGBA")
    bb = Image.fromarray((a[..., 3] > 8 / 255).astype(np.uint8) * 255, "L").getbbox()   # only the drawing, centred
    if bb:
        im = im.crop(bb)
    k = lato * S / max(im.size)
    im = im.resize((max(1, int(round(im.width * k))), max(1, int(round(im.height * k)))), Image.LANCZOS)
    x0, y0 = P(cx - im.width / S / 2, cy - im.height / S / 2)
    lay = np.zeros((HS, WS, 4), np.float32)
    src = np.asarray(im, np.float32) / 255
    src[..., :3] *= src[..., 3:4]
    xa, ya = int(round(x0)), int(round(y0))
    xb, yb = min(WS, xa + im.width), min(HS, ya + im.height)
    sx, sy = max(0, -xa), max(0, -ya)
    xa, ya = max(0, xa), max(0, ya)
    lay[ya:yb, xa:xb] = src[sy:sy + (yb - ya), sx:sx + (xb - xa)]
    sopra(dst, lay)


def linea(dst, p0, p1, larg, col, alfa=1.0):
    m = maschera(lambda d: d.line([P(*p0), P(*p1)], fill=255, width=int(larg * S)))
    sopra(dst, colore(col, m * alfa))


FACCIA_PIATTA = False   # inner disc of the dials: gradient (False) or one translucent colour (True)


def lente_nativa(dst, cx, cy, r, anello=None, interno=0.74, ombra_lunga=True):
    """as the Bluetooth/map discs of the native home: dark translucent disc, light thread, inner raised disc
    (light top-left) with a soft shadow bottom-right"""
    rr = np.hypot(XX - cx, YY - cy)
    cop = liscio(rr - r)
    t = np.clip(rr / r, 0, 1)
    sopra(dst, colore((0, 22, 34), cop * (0.20 + 0.16 * t ** 2)))
    if anello is None:
        sopra(dst, colore((200, 240, 245), liscio(np.abs(rr - r + 1.2) - 0.8) * 0.45))
    else:
        lay = colore(anello, liscio(np.abs(rr - r + 1.8) - 1.4))
        sopra(dst, bagliore(lay, 5, 0.8))
        sopra(dst, lay)
    ri = r * interno
    if ri <= 0:
        return
    if ombra_lunga:
        m = sfoca_m(liscio(d_cerchio(cx + ri * 0.10, cy + ri * 0.14, ri)), ri * 0.12)
        sopra(dst, colore((0, 10, 18), m * 0.45 * cop))
    ci = liscio(d_cerchio(cx, cy, ri))
    g = np.clip(((XX - cx) * -0.6 + (YY - cy) * -0.8) / ri * 0.5 + 0.5, 0, 1)
    if FACCIA_PIATTA:      # one translucent colour (the middle of the gradient): no bands in a 256-colour PNG
        rgb = np.stack([np.full_like(g, 75.0), np.full_like(g, 124.0), np.full_like(g, 136.0)], -1)
    else:
        rgb = np.stack([40 + 70 * g, 78 + 92 * g, 92 + 88 * g], -1)
    sopra(dst, colore(rgb, ci * 0.55))
    sopra(dst, colore((0, 0, 0), ci * 0.22))
    sopra(dst, colore(BIANCO, liscio(np.abs(d_cerchio(cx, cy, ri)) - 0.7) * (0.12 + 0.30 * g)))


def disco_chiaro(dst, cx, cy, r, tema, anello=True):
    """as the selected icon of the native main menu: light disc (the content goes on it, dark)"""
    ombra(dst, d_cerchio(cx + 2, cy + 3, r), 6, 3, 0.45)
    rr = np.hypot(XX - cx, YY - cy)
    g = np.clip(((XX - cx) * -0.5 + (YY - cy) * -0.85) / r * 0.5 + 0.5, 0, 1)
    rgb = np.stack([212 + 34 * g, 222 + 28 * g, 224 + 28 * g], -1)
    sopra(dst, colore(rgb, liscio(rr - r) * 0.96))
    if anello:
        lay = colore(TEMI[tema]["acc"], liscio(np.abs(rr - r - 4) - 1.4))
        sopra(dst, bagliore(lay, 5, 0.8))
        sopra(dst, lay)


def colonna_scelta(dst, x0, x1, y0=70, y1=430):
    """native highlight of the selected entry: a light column fading at top and bottom"""
    t = (YY - y0) / (y1 - y0)
    f = np.clip(np.minimum(t / 0.12, (1 - t) / 0.12), 0, 1)
    cop = liscio(np.maximum(x0 - XX, XX - x1)) * f
    sopra(dst, colore((235, 242, 242), cop * 0.42))


def segmenti_arco(cx, cy, r0, r1, a0, a1, n, gap_deg=1.6):
    """n segments between a0 and a1 degrees (counter-clockwise positive), as polygons in page coordinates"""
    out = []
    span = (a1 - a0) / n
    for k in range(n):
        aa = a0 + k * span + gap_deg / 2 * np.sign(span)
        ab = a0 + (k + 1) * span - gap_deg / 2 * np.sign(span)
        pts = []
        for i in range(7):
            a = math.radians(aa + (ab - aa) * i / 6)
            pts.append((cx + r1 * math.cos(a), cy - r1 * math.sin(a)))
        for i in range(7):
            a = math.radians(ab + (aa - ab) * i / 6)
            pts.append((cx + r0 * math.cos(a), cy - r0 * math.sin(a)))
        out.append(pts)
    return out


def bbox(pts, pad=0):
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    return (int(math.floor(min(xs) - pad)), int(math.floor(min(ys) - pad)), int(math.ceil(max(xs) + pad)), int(math.ceil(max(ys) + pad)))


def led_spento(dst, forma):
    sopra(dst, poligono(forma, BIANCO, 0.10))


def led_acceso(dst, forma, col, alone=6):
    lay = poligono(forma, col)
    sopra(dst, bagliore(lay, alone, 1.25))
    sopra(dst, lay)
    c2 = tuple(min(255, int(v * 0.5 + 128)) for v in col)
    cx = sum(p[0] for p in forma) / len(forma)
    cy = sum(p[1] for p in forma) / len(forma)
    sopra(dst, poligono([(cx + (x - cx) * 0.55, cy + (y - cy) * 0.45) for x, y in forma], c2, 0.55))


def separatore(dst, x0, x1, y, forza=0.45):
    t = np.clip(1 - np.abs((XX - (x0 + x1) / 2) / ((x1 - x0) / 2)) ** 4, 0, 1)
    dentro = liscio(np.abs(XX - (x0 + x1) / 2) - (x1 - x0) / 2)
    sopra(dst, colore(BIANCO, liscio(np.abs(YY - y) - 0.6) * t * forza * dentro))


def separatore_v(dst, x, y0, y1, forza=0.35):
    t = np.clip(1 - np.abs((YY - (y0 + y1) / 2) / ((y1 - y0) / 2)) ** 3, 0, 1)
    dentro = liscio(np.abs(YY - (y0 + y1) / 2) - (y1 - y0) / 2)
    sopra(dst, colore(BIANCO, liscio(np.abs(XX - x) - 0.6) * t * forza * dentro))


def binario(dst, x0, y, x1, h=8):
    """empty track of a bar (in the background)"""
    d = d_rrect(x0, y - h / 2, x1, y + h / 2, h / 2)
    sopra(dst, colore((0, 0, 0), liscio(d) * 0.35))
    sopra(dst, colore(BIANCO, liscio(np.abs(d + 0.5) - 0.5) * 0.18))


def riempimento(dst, x0, y, x1, frac, col, h=8):
    """the lit part of a bar (a sprite over the track)"""
    if frac <= 0:
        return
    xe = x0 + max(h, (x1 - x0) * frac)
    d2 = d_rrect(x0, y - h / 2, xe, y + h / 2, h / 2)
    lay = colore(col, liscio(d2))
    sopra(dst, bagliore(lay, 5, 0.9))
    sopra(dst, lay)
    sopra(dst, colore(BIANCO, liscio(d_rrect(x0 + 2, y - h / 2 + 1.5, xe - 2, y - 0.5, 1.5)) * 0.35))


# ---- text ----------------------------------------------------------------------------------------------------
def testo(dst, xy, s, sz, col=BIANCO, alfa=1.0, peso="SemiBold", anchor="la", disp=False, track=0.0, ombra_t=True):
    f = F(sz, peso, disp)

    def disegna(d):
        if not track:
            d.text(P(*xy), s, font=f, fill=255, anchor=anchor)
            return
        x, y = xy
        tot = sum(f.getlength(ch) / S + track for ch in s) - track
        if anchor[0] == "m":
            x -= tot / 2
        elif anchor[0] == "r":
            x -= tot
        for ch in s:
            d.text(P(x, y), ch, font=f, fill=255, anchor="l" + anchor[1])
            x += f.getlength(ch) / S + track
    m = maschera(disegna)
    if ombra_t:
        sh = np.roll(sfoca_m(m, 2.2), 2 * S, 0) * 0.55
        sopra(dst, colore((0, 0, 0), sh))
    sopra(dst, colore(col, m * alfa))


def larghezza(s, sz, peso="SemiBold", disp=False):
    return F(sz, peso, disp).getlength(s) / S


def etichetta(dst, x, y, s, anchor="la", alfa=0.78):
    testo(dst, (x, y), s.upper(), 17, BIANCO, alfa, "Medium", anchor, track=1.0)


def numero(dst, xy, s, sz, col=BIANCO, anchor="mm", peso="SemiBold", alone=None, scuro=False):
    """big digits: vertical shading (white -> pearl grey), soft shadow, optional glow"""
    f = F(sz, peso, True)
    m = maschera(lambda d: d.text(P(*xy), s, font=f, fill=255, anchor=anchor))
    asc, desc = f.getmetrics()
    yc = xy[1]
    y0 = yc - asc / S / 2
    t = np.clip((YY - y0) / max(asc / S, 1), 0, 1)
    c = np.asarray(col, np.float32)
    grad = c[None, None, :] * (1 - (0.0 if scuro else 0.16) * t[..., None])
    if not scuro:
        sh = np.roll(sfoca_m(m, 3), 3 * S, 0) * 0.6
        sopra(dst, colore((0, 0, 0), sh))
    if alone:
        sopra(dst, bagliore(colore(alone, m), 10, 0.9))
    sopra(dst, colore(grad, m))


def glifo(dst, cx, cy, tipo, col=BIANCO, k=1.0):
    """simple line icons, as the pictograms of the native menu"""
    s = S * k

    def pp(x, y):
        return P(cx + x * k, cy + y * k)

    def disegna(d):
        if tipo == "goccia":                     # oil
            d.polygon([pp(0, -13), pp(-9, 1), pp(9, 1)], fill=255)
            d.ellipse([*pp(-9, -6), *pp(9, 12)], fill=255)
            d.ellipse([*pp(-5, -2), *pp(5, 8)], fill=0)
        elif tipo == "termo":                    # water
            d.rounded_rectangle([*pp(-4, -14), *pp(4, 6)], 4 * s, outline=255, width=int(2.5 * s))
            d.ellipse([*pp(-8, 1), *pp(8, 15)], fill=255)
            for i in range(3):
                d.line([pp(8, -10 + i * 6), pp(13, -10 + i * 6)], fill=255, width=int(2 * s))
        elif tipo == "pedale":                   # throttle
            d.rounded_rectangle([*pp(-8, -14), *pp(8, 10)], 4 * s, outline=255, width=int(2.5 * s))
            for i in range(3):
                d.line([pp(-4, -7 + i * 6), pp(4, -7 + i * 6)], fill=255, width=int(2 * s))
            d.line([pp(-10, 15), pp(10, 15)], fill=255, width=int(2.5 * s))
        elif tipo == "coppia":                   # torque
            d.arc([*pp(-13, -13), *pp(13, 13)], 150, 390, fill=255, width=int(2.6 * s))
            a = math.radians(30)
            ex, ey = 13 * math.cos(a), 13 * math.sin(a)
            d.polygon([pp(ex - 6, ey - 1), pp(ex + 5, ey - 6), pp(ex + 3, ey + 6)], fill=255)
            d.ellipse([*pp(-4, -4), *pp(4, 4)], fill=255)
    m = maschera(disegna)
    sopra(dst, colore(col, m))


def disco_icona(dst, cx, cy, tipo, r=24):
    lente_nativa(dst, cx, cy, r, interno=0.0, ombra_lunga=False)
    glifo(dst, cx, cy, tipo, BIANCO, r / 26)


def testata(dst, titolo, tema, destra=""):
    """page title as the native ones, two bonnet stripes, thin line fading at the ends"""
    acc = TEMI[tema]["acc"]
    testo(dst, (18, 33), titolo, 28, peso="Medium", anchor="lm")
    x = 18 + larghezza(titolo, 28, "Medium") + 16
    for i in range(2):
        sopra(dst, poligono([(x + i * 13, 44), (x + i * 13 + 8, 22), (x + i * 13 + 16, 22), (x + i * 13 + 8, 44)], acc, 0.95))
    if destra:
        testo(dst, (1132, 33), destra, 18, BIANCO, 0.80, "Regular", "rm")
    separatore(dst, 10, 1140, 62, 0.45)
