"""
256-colour PNG with transparency, the same algorithm as the iDrive Plus phone app (PngLeggero.kt), whose pages work in
the car in this format.
Palette: the exact colours if the picture has at most 256; otherwise median cut on reduced colours (5 bits R, G, B,
4 bits alpha) weighted by how many pixels use them. Opaque pixels get error diffusion (Floyd-Steinberg) restricted to
opaque neighbours; semi-transparent pixels (text edges, shadows) take the nearest colour. Fully transparent pixels
(alpha < 8) are palette entry 0.
Usage: codifica(PIL image) -> PNG bytes
"""
import struct
import zlib

import numpy as np


def _trasparente(a):
    return a < 8


def _chiave(r, g, b, a):
    return ((a >> 4) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3)


def _tavolozza(px, w, h, soglia=255, alfa=False):
    """px: uint8 array (h*w, 4) RGBA -> (colori list of (r, g, b, a), indici uint8 array); pixels with alpha >= soglia get
    the error diffusion (255 = opaque only, as PngLeggero.kt)"""
    r = px[:, 0].astype(np.int64)
    g = px[:, 1].astype(np.int64)
    b = px[:, 2].astype(np.int64)
    a = px[:, 3].astype(np.int64)
    tr = _trasparente(a)
    argb = np.where(tr, 0, (a << 24) | (r << 16) | (g << 8) | b)
    unici, inv = np.unique(argb, return_inverse=True)
    # 1) at most 256 colours: exact palette (in order of first appearance, transparent first when present)
    if len(unici) <= 256:
        primo = np.full(len(unici), len(argb), np.int64)
        np.minimum.at(primo, inv, np.arange(len(argb)))
        ordine = np.argsort(primo, kind="stable")
        posto = np.empty(len(unici), np.int64)
        posto[ordine] = np.arange(len(unici))
        colori = []
        for u in unici[ordine]:
            u = int(u)
            colori.append(((u >> 16) & 255, (u >> 8) & 255, u & 255, (u >> 24) & 255))
        return colori, posto[inv].astype(np.uint8)
    # 2) histogram of the reduced colours (fully transparent pixels apart)
    trasparenti = bool(tr.any())
    op = ~tr
    k = _chiave(r[op], g[op], b[op], a[op])
    caselle, kinv = np.unique(k, return_inverse=True)
    n = len(caselle)
    peso = np.bincount(kinv, minlength=n).astype(np.int64)
    v = np.empty((4, n), np.int64)
    for ch, arr in enumerate((r[op], g[op], b[op], a[op])):
        v[ch] = np.bincount(kinv, weights=arr, minlength=n).astype(np.int64) // peso
    # 3) median cut: always split the box with the largest (weighted) error
    posti = 255 if trasparenti else 256
    ord_ = np.arange(n)
    inizio = [0] * posti
    fine = [0] * posti
    errore = [0.0] * posti
    canale = [0] * posti
    fine[0] = n
    vf = v.astype(np.float64)
    pf = peso.astype(np.float64)

    def misura(bx):
        sel = ord_[inizio[bx]:fine[bx]]
        wgt = pf[sel]
        p = wgt.sum()
        tot = 0.0
        meglio = -1.0
        for ch in range(4):
            x = vf[ch][sel]
            s = (wgt * x).sum()
            q = (wgt * x * x).sum()
            sse = (q - s * s / p) * (2.0 if ch == 3 else 1.0)
            tot += sse
            if sse > meglio:
                meglio = sse
                canale[bx] = ch
        errore[bx] = -1.0 if fine[bx] - inizio[bx] < 2 else tot

    misura(0)
    scatole = 1
    while scatole < posti:
        bx = -1
        for i in range(scatole):
            if errore[i] > 0 and (bx < 0 or errore[i] > errore[bx]):
                bx = i
        if bx < 0:
            break
        ch = canale[bx]
        da, fa = inizio[bx], fine[bx]
        seg = ord_[da:fa]
        chiavi = (v[ch][seg] << 20) | seg
        seg = (np.sort(chiavi) & 0xFFFFF)
        ord_[da:fa] = seg
        pw = peso[seg]
        totale = int(pw.sum())
        taglio = fa - 1
        acc = np.cumsum(pw[:-1])
        idx = np.nonzero(acc * 2 >= totale)[0]
        if len(idx):
            taglio = da + int(idx[0]) + 1
        inizio[scatole] = taglio
        fine[scatole] = fa
        fine[bx] = taglio
        misura(bx)
        misura(scatole)
        scatole += 1
    base = 1 if trasparenti else 0
    colori = [(0, 0, 0, 0)] if trasparenti else []
    for bx in range(scatole):
        sel = ord_[inizio[bx]:fine[bx]]
        wgt = pf[sel]
        p = wgt.sum()
        m = [(wgt * vf[ch][sel]).sum() / p for ch in range(4)]
        al = int(m[3] + 0.5)
        if al >= 250:
            al = 255
        colori.append((int(m[0] + 0.5), int(m[1] + 0.5), int(m[2] + 0.5), al))
    # 4) every pixel to the nearest colour; opaque pixels with error diffusion
    pal = np.array(colori[base:], np.int64)
    cache = {}

    def indice(rr, gg, bb, aa):
        kk = _chiave(rr, gg, bb, aa)
        gia = cache.get(kk)
        if gia is not None:
            return gia
        dr = pal[:, 0] - rr
        dg = pal[:, 1] - gg
        db = pal[:, 2] - bb
        dA = pal[:, 3] - aa
        e = 2 * dr * dr + 4 * dg * dg + 3 * db * db + 6 * dA * dA
        meglio = base + int(np.argmin(e))
        cache[kk] = meglio
        return meglio

    pl = px.reshape(h, w, 4).astype(np.int64)
    opaco = pl[..., 3] >= soglia
    ind = np.zeros((h, w), np.uint8)
    qui = [[0, 0, 0, 0] for _ in range(w + 2)]
    for y in range(h):
        sotto = [[0, 0, 0, 0] for _ in range(w + 2)]
        riga = pl[y]
        opr = opaco[y]
        opn = opaco[y + 1] if y + 1 < h else None
        for x in range(w):
            rr, gg, bb, aa = (int(t) for t in riga[x])
            if aa < 8:
                continue
            if aa < soglia:
                ind[y, x] = indice(rr, gg, bb, aa)
                continue
            e = qui[x + 1]
            rr = min(255, max(0, rr + int(e[0] / 16)))
            gg = min(255, max(0, gg + int(e[1] / 16)))
            bb = min(255, max(0, bb + int(e[2] / 16)))
            if alfa:             # the transparency too gets the error diffusion (fades of translucent areas)
                aa = min(255, max(0, aa + int(e[3] / 16)))
            if alfa and aa < 8 and base == 1:
                kk = 0
            else:
                kk = indice(rr, gg, bb, aa)
            ind[y, x] = kk
            c = colori[kk]
            er, eg, eb = rr - c[0], gg - c[1], bb - c[2]
            ea = aa - c[3] if alfa else 0
            for dest, xx, ok, f in ((qui, x + 1, x + 1 < w and opr[x + 1], 7),
                                    (sotto, x - 1, opn is not None and x >= 1 and opn[x - 1], 3),
                                    (sotto, x, opn is not None and opn[x], 5),
                                    (sotto, x + 1, opn is not None and x + 1 < w and opn[x + 1], 1)):
                if ok:
                    d = dest[xx + 1]
                    d[0] += er * f
                    d[1] += eg * f
                    d[2] += eb * f
                    d[3] += ea * f
        qui = sotto
    return colori, ind.reshape(-1)


def _pezzo(tipo, dati):
    return struct.pack(">I", len(dati)) + tipo + dati + struct.pack(">I", zlib.crc32(tipo + dati) & 0xFFFFFFFF)


def codifica(im, soglia=255, alfa=False):
    """PIL image -> 256-colour PNG bytes (palette + tRNS), like PngLeggero.kt; soglia < 255 extends the error diffusion
    to semi-transparent pixels (translucent gradients, e.g. the dial faces of MINI Sport); alfa=True diffuses the error
    of the transparency too (fades to transparent, e.g. the highlight column of the menu)"""
    im = im.convert("RGBA")
    w, h = im.size
    px = np.asarray(im, np.uint8).reshape(-1, 4)
    colori, ind = _tavolozza(px, w, h, soglia, alfa)
    out = b"\x89PNG\r\n\x1a\n" + _pezzo(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 3, 0, 0, 0))
    out += _pezzo(b"PLTE", bytes(v for c in colori for v in c[:3]))
    ultimo = len(colori)
    while ultimo > 0 and colori[ultimo - 1][3] == 255:
        ultimo -= 1
    if ultimo > 0:
        out += _pezzo(b"tRNS", bytes(c[3] for c in colori[:ultimo]))
    righe = b"".join(b"\x00" + ind[y * w:(y + 1) * w].tobytes() for y in range(h))
    out += _pezzo(b"IDAT", zlib.compress(righe, 9))
    out += _pezzo(b"IEND", b"")
    return out
