# ============================================================================
# convert_giochi_header.py
# Converte il banner "GIOCHI ARCADE GAMEPAD" nell'header .h del nuovo menu
# boot (griglia 3x3 con scroll verticale).
#
# Regola loghi da immagine reale (memoria utente): STRETCH-FIT, niente taglio,
# niente bordi -> img.resize((W,H), LANCZOS) anche se deforma leggermente.
#
# Output: giochi_header.h  ->  const unsigned short giochi_header[W*H] PROGMEM
#   formato RGB565 byte-swappato (high-first) come gli altri *_logo.h del menu.
#   Layout row-major portrait: giochi_header[y*W + x], drawn con rotazione SW.
#
# USO:
#   python convert_giochi_header.py [percorso_immagine.png]
#   (se non passi il path, usa INPUT_DEFAULT qui sotto)
# ============================================================================
import sys
from PIL import Image

# --- Dimensioni header nella vista portrait utente (x 8..239 = 232 px) ---
HEADER_W = 232
HEADER_H = 96

# Percorso immagine sorgente: cambia qui o passa come argomento.
INPUT_DEFAULT = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\logo giochi arcade.png"
OUTPUT_H = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO\giochi_header.h"
OUTPUT_CARD = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO\giochi_card_logo.h"


def rgb565_swapped(r, g, b):
    v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((v & 0xFF) << 8) | (v >> 8)   # byte-swap (high-first per lo SPI)


def emit(src_img, w, h, symbol, out_path, wmacro, hmacro):
    img = src_img.resize((w, h), Image.LANCZOS)   # stretch-fit (deforma, no bordi)
    px = img.load()
    with open(out_path, "w") as f:
        f.write("// Auto-generato da convert_giochi_header.py - NON modificare a mano\n")
        f.write("#pragma once\n")
        f.write(f"#define {wmacro} {w}\n")
        f.write(f"#define {hmacro} {h}\n")
        f.write(f"const unsigned short {symbol}[{wmacro} * {hmacro}] PROGMEM = {{\n")
        for y in range(h):
            row = [f"0x{rgb565_swapped(*px[x, y]):04X}" for x in range(w)]
            f.write("  " + ",".join(row) + ",\n")
        f.write("};\n")
    print(f"OK: {out_path}  ({w}x{h}, {w*h*2} byte)")


def main():
    src = sys.argv[1] if len(sys.argv) > 1 else INPUT_DEFAULT
    img = Image.open(src).convert("RGB")
    # 1) Header griglia GIOCHI ARCADE (232x96, x_p 8..239)
    emit(img, HEADER_W, HEADER_H, "giochi_header", OUTPUT_H,
         "GIOCHI_HEADER_W", "GIOCHI_HEADER_H")
    # 2) Logo card del carosello: occupa lo slot logo 224x96 (LOGO_W x LOGO_H)
    #    al posto delle scritte "GIOCHI/ARCADE/JOYSTICK". Aspetto ~2.33:1 vicino
    #    all'originale del banner, quindi deformazione minima.
    emit(img, 224, 96, "giochi_card_logo", OUTPUT_CARD,
         "GIOCHI_CARD_W", "GIOCHI_CARD_H")


if __name__ == "__main__":
    main()
