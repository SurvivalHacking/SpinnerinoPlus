#!/usr/bin/env python3
# ============================================================================
# convert_xevious_grid_logo.py — logo 72x48 di "Xevious" per la cella del menu
# GIOCHI ARCADE di SPINNERINO PLUS (prima cella, indice 0).
#
# ⚠️ DIFFERENZA DAGLI ALTRI CONVERTER DI QUESTA CARTELLA: il sorgente che
#    l'utente ha fornito e' su FONDO BIANCO, mentre tutte le altre celle della
#    griglia sono su NERO. Un bianco secco (`se e' chiaro -> nero`) mangerebbe i
#    riflessi bianchi DENTRO le lettere, che in questo logo ci sono e sono
#    parecchi. Quindi il fondo si toglie con un RIEMPIMENTO A PARTIRE DAI BORDI:
#    diventa nero solo il bianco che comunica con l'esterno.
#
# ⚠️ ASPECT RATIO PRESERVATO + letterbox, non stretch: e' la regola dei grid
#    logo ([[feedback_logo_stretch_fit]], eccezione "grid logo del menu").
#
# Formato di uscita identico agli altri *_grid_logo.h:
#   const unsigned short xevious_grid_logo[72*48] PROGMEM = { ... };
#   RGB565 BYTE-SWAPPED (come tutti i display SPI di questo progetto).
#
# Uso:  python convert_xevious_grid_logo.py
# ============================================================================
import os
import sys
from collections import deque

from PIL import Image

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_DIR = os.path.join(ROOT, "SPINNERINO")

SRC = os.path.join(OUT_DIR, "LOGHI", "xevious logo.jpeg")
BASE = "xevious"                  # -> xevious_grid_logo.h, simbolo xevious_grid_logo
W, H = 72, 48
CHIARO = 200                      # soglia "e' fondo bianco" su tutti e tre i canali


def rgb_to_565_swap(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)        # byte-swap, come gli altri header


def fondo_a_nero(img):
    """Riempimento dai bordi: annerisce SOLO il bianco che comunica con l'esterno,
    lasciando intatti i bianchi chiusi dentro le lettere."""
    px = img.load()
    w, h = img.size

    def chiaro(x, y):
        r, g, b = px[x, y][:3]
        return r >= CHIARO and g >= CHIARO and b >= CHIARO

    visti = bytearray(w * h)
    coda = deque()
    for x in range(w):
        for y in (0, h - 1):
            if chiaro(x, y) and not visti[y * w + x]:
                visti[y * w + x] = 1; coda.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if chiaro(x, y) and not visti[y * w + x]:
                visti[y * w + x] = 1; coda.append((x, y))

    n = 0
    while coda:
        x, y = coda.popleft()
        px[x, y] = (0, 0, 0)
        n += 1
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = x + dx, y + dy
            if 0 <= nx < w and 0 <= ny < h and not visti[ny * w + nx] and chiaro(nx, ny):
                visti[ny * w + nx] = 1; coda.append((nx, ny))
    return n


def ritaglia_su_nero(img, thr=24):
    """Toglie la cornice nera prima di scalare, cosi' il logo riempie la cella."""
    px = img.load()
    w, h = img.size
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y][:3]
            if r > thr or g > thr or b > thr:
                if x < x0: x0 = x
                if y < y0: y0 = y
                if x > x1: x1 = x
                if y > y1: y1 = y
    if x1 < 0:
        return img
    return img.crop((x0, y0, x1 + 1, y1 + 1))


def main():
    if not os.path.isfile(SRC):
        print("ERRORE: non trovo %s" % SRC)
        sys.exit(1)

    img = Image.open(SRC).convert("RGB")
    print("sorgente  : %dx%d" % img.size)
    n = fondo_a_nero(img)
    print("fondo     : %d pixel bianchi anneriti (riempimento dai bordi)" % n)
    img = ritaglia_su_nero(img)
    print("ritagliato: %dx%d" % img.size)

    # contain: sta tutto dentro, proporzioni intatte, bande nere dove avanza
    iw, ih = img.size
    scala = min(W / iw, H / ih)
    nw, nh = max(1, int(iw * scala)), max(1, int(ih * scala))
    img = img.resize((nw, nh), Image.LANCZOS)
    tela = Image.new("RGB", (W, H), (0, 0, 0))
    tela.paste(img, ((W - nw) // 2, (H - nh) // 2))
    print("finale    : %dx%d dentro %dx%d (scala %.3f)" % (nw, nh, W, H, scala))

    px = tela.load()
    valori = [rgb_to_565_swap(*px[x, y]) for y in range(H) for x in range(W)]

    out = os.path.join(OUT_DIR, BASE + "_grid_logo.h")
    with open(out, "w", encoding="utf-8") as f:
        f.write("// Auto-generato da convert_xevious_grid_logo.py da %s\n"
                % os.path.basename(SRC))
        f.write("// Fondo bianco del sorgente annerito con un riempimento dai bordi:\n")
        f.write("// i riflessi bianchi DENTRO le lettere restano.\n")
        f.write("// Aspect ratio PRESERVATO + letterbox: e' la regola dei grid logo.\n")
        f.write("#pragma once\n")
        f.write("#define XEVIOUS_GRID_W %d\n" % W)
        f.write("#define XEVIOUS_GRID_H %d\n" % H)
        f.write("const unsigned short %s_grid_logo[%d*%d] PROGMEM = {\n" % (BASE, W, H))
        for y in range(H):
            riga = valori[y * W:(y + 1) * W]
            f.write("  " + ",".join("0x%04X" % v for v in riga) + ",\n")
        f.write("};\n")
    print("scritto   : %s (%d byte)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
