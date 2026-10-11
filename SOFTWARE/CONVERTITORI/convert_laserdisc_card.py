#!/usr/bin/env python3
# ============================================================================
# convert_laserdisc_card.py
#   Genera laserdisc_card_logo.h — la card 224x96 della voce "GIOCHI LASERDISC"
#   nel carosello di boot, accanto a giochi_card_logo.h ("GIOCHI ARCADE").
#
#   La card e' la FOTO fornita dall'utente (mockup del cabinato laserdisc),
#   con ASPECT RATIO ORIGINALE PRESERVATO e CENTRATA IN ORIZZONTALE.
#   Scelta esplicita dell'utente 2026-07-15: niente stretch, niente crop.
#
#   Sorgente 1408x768 = 1.8333:1, card 224x96 = 2.3333:1 -> si scala
#   sull'altezza piena: 96 * 1408/768 = 176 esatti (nessun arrotondamento),
#   x = (224-176)/2 = 24 -> due bande nere simmetriche da 24 px ai lati.
#
#   OUT: laserdisc_card_logo.h  — 224x96 RGB565 BYTE-SWAPPED raw, PROGMEM
#        (stesso formato di giochi_card_logo.h: lo SPI dell'ESP32 trasmette
#         big-endian, quindi i byte vanno gia' invertiti nell'header)
# ============================================================================
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "..", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

import os

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))

# La foto e' copiata dentro il progetto: il converter resta rigenerabile
# anche se la sorgente sparisce da Downloads.
SRC = os.path.join(HERE, "laserdisc_card_src.png")
OUT_H_FILE = os.path.join(HERE, "..", "BACKUP SPINNERINO PLUS", "SPINNERINO", "laserdisc_card_logo.h")
OUT_PNG = None          # se valorizzato, salva anche un PNG di anteprima 3x

W, H = 224, 96
BG = (0, 0, 0)          # bande laterali


def build_card():
    im = Image.open(SRC)
    if im.mode != "RGB":
        # la sorgente e' RGBA: appiattire su nero, non scartare l'alpha
        im = Image.alpha_composite(Image.new("RGBA", im.size, (0, 0, 0, 255)),
                                   im.convert("RGBA")).convert("RGB")

    sw, sh = im.size
    # contain: scala col fattore piu' piccolo, cosi' l'immagine ci sta tutta
    # e l'aspect ratio resta quello originale. Con 1408x768 vince l'altezza
    # (96/768 < 224/1408) -> 176x96, margini ai lati.
    k = min(W / sw, H / sh)
    tw, th = round(sw * k), round(sh * k)

    im = im.resize((tw, th), Image.LANCZOS)

    card = Image.new("RGB", (W, H), BG)
    card.paste(im, ((W - tw) // 2, (H - th) // 2))    # centrata h e v
    print(f"foto {sw}x{sh} ({sw/sh:.4f}:1) -> {tw}x{th} @ x={(W - tw) // 2}, "
          f"y={(H - th) // 2}  (card {W}x{H})")
    return card


def main():
    card = build_card()

    if OUT_PNG:
        card.resize((W * 3, H * 3), Image.NEAREST).save(OUT_PNG)

    # --- RGB888 -> RGB565 byte-swapped ---
    a = np.asarray(card).astype(np.uint16)
    v = ((a[:, :, 0] >> 3) << 11) | ((a[:, :, 1] >> 2) << 5) | (a[:, :, 2] >> 3)
    v = ((v & 0xFF) << 8) | (v >> 8)          # byte swap (SPI big-endian)

    px = v.flatten()
    out = os.path.normpath(OUT_H_FILE)
    with open(out, "w") as f:
        f.write("// Auto-generato da convert_laserdisc_card.py - NON modificare a mano\n")
        f.write("#pragma once\n")
        f.write(f"#define LASERDISC_CARD_W {W}\n")
        f.write(f"#define LASERDISC_CARD_H {H}\n")
        f.write("const unsigned short laserdisc_card_logo"
                "[LASERDISC_CARD_W * LASERDISC_CARD_H] PROGMEM = {\n")
        for y in range(H):
            row = px[y * W:(y + 1) * W]
            f.write("  " + ",".join("0x%04X" % t for t in row) + ",\n")
        f.write("};\n")
    print(f"scritto {out}  ({W}x{H}, {os.path.getsize(out)} byte)")


if __name__ == "__main__":
    main()
