#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""
convert_commando_logo.py  (SPINNERINO WIFI, 2026-06-19)

Genera l'in-game logo di Commando come C header flash:
  machines/commando/commando_logo.h  ->  const unsigned short commando_logo[]
Formato: 224x96 RGB565 byte-swapped, RLE marker-based (identico agli altri
in-game logo del progetto, es. pacman_logo / mspacman_logo). Serve sia come
splash del gioco (logo()) sia come sorgente per convert_grid_logos.py.

Stretch-fit a 224x96 (no taglio, no bordi) come da regola loghi-da-immagine.
"""
from PIL import Image
import os

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_H = os.path.join(SCRIPT_DIR, "..", "machines", "commando", "commando_logo.h")
SRC_CANDIDATES = [
    r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE PAOLO ULTIMO\GALAGONE_4.3_P4_NEW_multi\PNG\commando.jpg",
    r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE 2.8VPE\GALAGONE\commando.jpg",
]
W, H = 224, 96


def rgb_to_565_swap(r, g, b):
    r5 = (r >> 3) & 0x1F; g6 = (g >> 2) & 0x3F; b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF)


def rle_encode(pixels):
    used = set(pixels)
    marker = next((m for m in range(65536) if m not in used), 1)
    enc = [marker]
    i = 0
    while i < len(pixels):
        run = 1
        while i + run < len(pixels) and pixels[i + run] == pixels[i] and run < 256:
            run += 1
        if pixels[i] == marker or run >= 3:
            enc.extend([marker, run - 1, pixels[i]]); i += run
        else:
            enc.append(pixels[i]); i += 1
    return enc


def main():
    src_path = next((p for p in SRC_CANDIDATES if os.path.exists(p)), None)
    if not src_path:
        raise SystemExit("ERROR: commando.jpg non trovato.")
    print(f"Source: {src_path}")
    img = Image.open(src_path).convert("RGB").resize((W, H), Image.LANCZOS)
    pixels = [rgb_to_565_swap(*img.getpixel((x, y))) for y in range(H) for x in range(W)]
    enc = rle_encode(pixels)
    out = os.path.abspath(OUT_H)
    with open(out, "w") as f:
        f.write(f"// commando - TFT Logo ({W}x{H}, RGB565 byte-swapped, RLE) - stretch-fit\n")
        f.write("const unsigned short commando_logo[] = {\n")
        for i in range(0, len(enc), 12):
            f.write("  " + ",".join(str(v) for v in enc[i:i+12]) + ",\n")
        f.write("};\n")
    print(f"  raw={len(pixels)*2}B RLE={len(enc)*2}B -> {out}")


if __name__ == "__main__":
    main()
