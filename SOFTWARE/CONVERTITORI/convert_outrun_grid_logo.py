#!/usr/bin/env python3
# ============================================================================
# convert_outrun_grid_logo.py — logo 72x48 di "Out Run" (Sega 1986) per la
# cella del menu GIOCHI ARCADE di SPINNERINO PLUS.
#
# Clone di convert_ghouls_grid_logo.py: cambia solo il sorgente e il nome.
#
# ⚠️ ASPECT RATIO PRESERVATO + letterbox su nero, NON stretch: per le piccole
#    icone della griglia l'utente vuole le proporzioni originali (regola
#    [[feedback_logo_stretch_fit]], eccezione "grid logo del menu"). Lo stretch
#    vale per i logo GRANDI fullscreen/flyer, non per queste.
#
# Formato di uscita identico agli altri *_grid_logo.h del progetto:
#   const unsigned short outrun_grid_logo[72*48] PROGMEM = { ... };
#   RGB565 BYTE-SWAPPED (come tutti i display SPI di questo progetto).
#
# Uso:  python convert_outrun_grid_logo.py
# ============================================================================
import os
import sys

from PIL import Image

try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT_DIR = os.path.join(ROOT, "SPINNERINO")

# marquee vero, gia' usato per la card .scr di CABINATONE
SRC = r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE\outrun logo.jpg"
BASE = "outrun"                   # -> outrun_grid_logo.h, simbolo outrun_grid_logo
W, H = 72, 48


def rgb_to_565_swap(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)        # byte-swap, come gli altri header


def autocrop_nonblack(img, thr=24):
    """Toglie la cornice nera del sorgente prima di scalare."""
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
    print("sorgente : %dx%d" % img.size)
    img = autocrop_nonblack(img)
    print("ritagliato: %dx%d" % img.size)

    # contain: sta tutto dentro, proporzioni intatte, bande nere dove avanza
    iw, ih = img.size
    scala = min(W / iw, H / ih)
    nw, nh = max(1, int(iw * scala)), max(1, int(ih * scala))
    img = img.resize((nw, nh), Image.LANCZOS)
    tela = Image.new("RGB", (W, H), (0, 0, 0))
    tela.paste(img, ((W - nw) // 2, (H - nh) // 2))
    print("finale   : %dx%d dentro %dx%d (scala %.3f)" % (nw, nh, W, H, scala))

    px = tela.load()
    valori = [rgb_to_565_swap(*px[x, y]) for y in range(H) for x in range(W)]

    out = os.path.join(OUT_DIR, BASE + "_grid_logo.h")
    with open(out, "w", encoding="utf-8") as f:
        f.write("// Auto-generato da convert_outrun_grid_logo.py da %s\n"
                % os.path.basename(SRC))
        f.write("// Aspect ratio PRESERVATO + letterbox: e' la regola dei grid logo.\n")
        f.write("#pragma once\n")
        f.write("#define OUTRUN_GRID_W %d\n" % W)
        f.write("#define OUTRUN_GRID_H %d\n" % H)
        f.write("const unsigned short %s_grid_logo[%d*%d] PROGMEM = {\n" % (BASE, W, H))
        for y in range(H):
            riga = valori[y * W:(y + 1) * W]
            f.write("  " + ",".join("0x%04X" % v for v in riga) + ",\n")
        f.write("};\n")
    print("scritto  : %s (%d byte)" % (out, os.path.getsize(out)))


if __name__ == "__main__":
    main()
