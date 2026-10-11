# ============================================================================
# convert_boblbobl_logo.py
# Logo per la cella della griglia "GIOCHI ARCADE" di Bubble Bobble, generato
# dall'immagine reale del titolo (logo bubble.jpeg).
#
# Aspect ratio ORIGINALE preservato (feedback utente 2026-06-28: lo stretch-fit
# deformava i dinosauri). Come convert_grid_logos.py: thumbnail mantenendo
# l'aspetto dentro 72x48, centrato su nero (letterbox). L'immagine sorgente e'
# ~2.84:1 -> ~72x25 centrato verticalmente nella cella 72x48.
#
# Output: boblbobl_grid_logo.h -> const unsigned short boblbobl_grid_logo[72*48]
#   Raw row-major, RGB565 byte-swapped (come gli altri *_grid_logo.h).
# ============================================================================
import os
from PIL import Image

SRC = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\logo bubble.jpeg"
OUT_DIR = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO"
W, H = 72, 48
BASE = "boblbobl"


def rgb_to_565_swap(r, g, b):
    val = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F)
    return ((val & 0xFF) << 8) | (val >> 8)


def main():
    img = Image.open(SRC).convert("RGB")
    img.thumbnail((W, H), Image.LANCZOS)       # mantiene aspect ratio
    canvas = Image.new("RGB", (W, H), (0, 0, 0))
    canvas.paste(img, ((W - img.width) // 2, (H - img.height) // 2))  # centrato (letterbox)
    px = canvas.load()

    out_path = os.path.join(OUT_DIR, BASE + "_grid_logo.h")
    SYM = BASE + "_grid_logo"
    with open(out_path, "w") as f:
        f.write(f"// Auto-generato da convert_boblbobl_logo.py da {os.path.basename(SRC)}\n")
        f.write("#pragma once\n")
        f.write(f"#define {BASE.upper()}_GRID_W {W}\n")
        f.write(f"#define {BASE.upper()}_GRID_H {H}\n")
        f.write(f"const unsigned short {SYM}[{W}*{H}] PROGMEM = {{\n")
        for y in range(H):
            row = [f"0x{rgb_to_565_swap(*px[x, y]):04X}" for x in range(W)]
            f.write("  " + ",".join(row) + ",\n")
        f.write("};\n")
    print(f"OK: {out_path}  ({W}x{H}) da {os.path.basename(SRC)}")


if __name__ == "__main__":
    main()
