# ============================================================================
# convert_neogeo_card.py
# Logo della voce "GIOCHI NEO GEO" del carosello + banner del suo sottomenu.
# Gemello di convert_giochi_header.py (voce "GIOCHI ARCADE"), stesso formato e
# stessa regola: STRETCH-FIT, niente taglio, niente bordi neri.
#
# ⚠️ A differenza di convert_giochi_header.py, che scrive nel progetto
#    "SPINNERINO WIFI" (un ALTRO progetto), qui i percorsi puntano al progetto
#    VIVO: BACKUP SPINNERINO PLUS\SPINNERINO.
#
# Uscita (RGB565 BYTE-SWAPPATO, come tutti gli altri *_logo.h del menu):
#   neogeo_card_logo.h  224x96
#
# ⭐ UNA sola misura, a differenza di "GIOCHI ARCADE" che ne ha due: lo stesso
#   224x96 fa sia la card del carosello sia il banner in cima al sottomenu.
#   E' la scelta gia' fatta per "GIOCHI LASERDISC" (laserdisc_card_logo lo usano
#   tutti e due), e vale 44 KB di flash risparmiati rispetto a generare anche il
#   232x96 della griglia arcade.
#
# USO:
#   python convert_neogeo_card.py [percorso_immagine.png]
# ============================================================================
import os
import sys
from PIL import Image

CARD_W, CARD_H = 224, 96      # slot logo del carosello, e banner del sottomenu

_PY = os.path.dirname(os.path.abspath(__file__))
_PROG = os.path.abspath(os.path.join(_PY, "..", "SPINNERINO"))   # progetto VIVO

INPUT_DEFAULT = os.path.abspath(os.path.join(_PY, "..", "logo neogeo.png"))
OUTPUT_CARD = os.path.join(_PROG, "neogeo_card_logo.h")


def rgb565_swapped(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)   # byte-swap (high-first per lo SPI)


def emit(src_img, w, h, symbol, out_path, wmacro, hmacro):
    img = src_img.resize((w, h), Image.LANCZOS)   # stretch-fit (deforma, no bordi)
    px = img.load()
    with open(out_path, "w") as f:
        f.write("// Auto-generato da convert_neogeo_card.py - NON modificare a mano\n")
        f.write("#pragma once\n")
        f.write("#define %s %d\n" % (wmacro, w))
        f.write("#define %s %d\n" % (hmacro, h))
        f.write("const unsigned short %s[%s * %s] PROGMEM = {\n" % (symbol, wmacro, hmacro))
        for y in range(h):
            row = ["0x%04X" % rgb565_swapped(*px[x, y]) for x in range(w)]
            f.write("  " + ",".join(row) + ",\n")
        f.write("};\n")
    print("OK: %s  (%dx%d, %d byte)" % (out_path, w, h, w * h * 2))


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else INPUT_DEFAULT
    img = Image.open(src).convert("RGB")
    emit(img, CARD_W, CARD_H, "neogeo_card_logo", OUTPUT_CARD,
         "NEOGEO_CARD_W", "NEOGEO_CARD_H")


if __name__ == "__main__":
    main()
