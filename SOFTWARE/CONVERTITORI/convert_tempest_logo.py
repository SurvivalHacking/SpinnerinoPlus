#!/usr/bin/env python3
import os as _os, sys as _sys
def _project_root():
    d = _os.path.dirname(_os.path.abspath(__file__))
    # SPINNERINO PLUS: lo script sta in "CONVERTITORI", la radice del pacchetto
    # e' la cartella sopra ([[feedback_mai_percorsi_del_mio_pc]]).
    if _os.path.basename(d).upper() in ("ROM CONVERTER PY", "CONVERTITORI"):
        d = _os.path.dirname(d)
    return d
_os.chdir(_project_root())
# ----------------------------------------------------------------------------
# convert_tempest_logo.py — logo di TEMPEST per il CAROSELLO del menu di avvio
#
# Ingresso : SPINNERINO/LOGHI/tempest logo.png   (la scritta TEMPEST su fondo
#            bianco mandata dall'utente l'08/10/2026; va bene anche .jpg)
# Uscita   : SPINNERINO/machines/tempest/tempest_logo.h
#            224x96 RGB565 byte-swapped (pannello SPI), come gli altri loghi
#            del carosello (boot_menu.cpp, LOGO_W x LOGO_H).
#
# Come gli altri loghi a scritta (Galaga, Arkanoid II): il FONDO BIANCO si
# toglie riempiendo dai bordi (non tutti i bianchi: dentro la scritta ci sono
# filetti bianchi che devono restare), poi si centra su nero mantenendo le
# proporzioni, a tutta larghezza meno un margine. Anteprima PNG accanto.
# ----------------------------------------------------------------------------
from PIL import Image

W, H = 224, 96
MARGINE = 4                     # px liberi per lato
SOGLIA_BIANCO = 215             # canali tutti sopra = fondo
CANDIDATI = ["tempest logo.png", "tempest logo.jpg", "tempest logo.jpeg"]
OUT = _os.path.join("SPINNERINO", "machines", "tempest", "tempest_logo.h")
ANTEPRIMA = _os.path.join("CONVERTITORI", "tempest_logo_anteprima.png")


def trova():
    for n in CANDIDATI:
        p = _os.path.join("SPINNERINO", "LOGHI", n)
        if _os.path.isfile(p):
            return p
    _sys.exit("ERRORE: manca SPINNERINO/LOGHI/tempest logo.png")


def scontorna(im):
    """Fondo bianco collegato ai BORDI -> trasparente (flood fill a 4 vicini)."""
    im = im.convert("RGBA")
    w, h = im.size
    px = im.load()
    bianco = lambda p: p[0] > SOGLIA_BIANCO and p[1] > SOGLIA_BIANCO and p[2] > SOGLIA_BIANCO
    visto = bytearray(w * h)
    pila = [(x, 0) for x in range(w)] + [(x, h - 1) for x in range(w)] + \
           [(0, y) for y in range(h)] + [(w - 1, y) for y in range(h)]
    while pila:
        x, y = pila.pop()
        if x < 0 or y < 0 or x >= w or y >= h or visto[y * w + x]:
            continue
        visto[y * w + x] = 1
        if not bianco(px[x, y]):
            continue
        px[x, y] = (0, 0, 0, 0)
        pila += [(x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)]
    return im


def main():
    src = trova()
    im = scontorna(Image.open(src))
    bbox = im.getbbox()
    if bbox:
        im = im.crop(bbox)
    s = min((W - 2 * MARGINE) / im.width, (H - 2 * MARGINE) / im.height)
    nw, nh = max(1, round(im.width * s)), max(1, round(im.height * s))
    im = im.resize((nw, nh), Image.LANCZOS)
    tela = Image.new("RGB", (W, H), (0, 0, 0))
    tela.paste(im, ((W - nw) // 2, (H - nh) // 2), im)
    tela.save(ANTEPRIMA)
    righe = ["// Generato da CONVERTITORI/convert_tempest_logo.py da '%s'" % _os.path.basename(src),
             "// 224x96 RGB565 byte-swap (ESP32 SPI BE), RAW",
             "#define TEMPEST_LOGO_W 224",
             "#define TEMPEST_LOGO_H 96",
             "const unsigned short tempest_logo[224*96] PROGMEM = {"]
    vals = []
    raw = tela.tobytes()
    for i in range(0, len(raw), 3):
        r, g, b = raw[i], raw[i + 1], raw[i + 2]
        c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        vals.append("0x%04X" % (((c >> 8) & 0xFF) | ((c & 0xFF) << 8)))
    for i in range(0, len(vals), 12):
        righe.append("  " + ", ".join(vals[i:i + 12]) + ",")
    righe.append("};")
    with open(OUT, "w", newline="\r\n") as f:
        f.write("\n".join(righe) + "\n")
    print("  %s -> %s (%dx%d dentro 224x96), anteprima %s" % (src, OUT, nw, nh, ANTEPRIMA))
    print("OK")


if __name__ == "__main__":
    main()
