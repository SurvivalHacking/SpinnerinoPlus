# ============================================================================
# convert_pulstar_cover.py
# Cover di PULSTAR per il menu "GIOCHI NEO GEO" dello SPINNERINO.
#
# Copia di convert_viewpoin_cover.py: stesso formato e stessa regola.
#     magic 0x5343 ('SC') | w uint16 LE | h uint16 LE | RGB565 LITTLE-ENDIAN
#   94x62 = la cella del menu (LD_LOGO_W / LD_LOGO_H del firmware), aspetto
#   PRESERVATO + letterbox su nero. Lo swap dei byte per il pannello SPI lo fa
#   il firmware al caricamento (NeoGeoMenu::probe_sd), non questo script.
# Sorgente: SPINNERINO/LOGHI/pulstar logo.jpg (1280x720; c'e' anche in ROMS/).
# Uscita:   DA COPIARE SU SD/neogeo/pulstar.scr  (da copiare in /neogeo/)
#
# USO:  python convert_pulstar_cover.py
# ============================================================================
import os
import struct
from PIL import Image

LOGO_W, LOGO_H = 94, 62
NOME_LOGO = "pulstar logo.jpg"

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CANDIDATI = [
    os.path.join(ROOT, "SPINNERINO", "LOGHI", NOME_LOGO),
    os.path.join(ROOT, "ROMS", NOME_LOGO),
]
OUT = os.path.join(ROOT, "DA COPIARE SU SD", "neogeo", "pulstar.scr")


def thumbnail_letterbox(src, w, h):
    im = Image.open(src).convert("RGB")
    iw, ih = im.size
    s = min(w / iw, h / ih)
    nw, nh = max(1, int(round(iw * s))), max(1, int(round(ih * s)))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), (0, 0, 0))
    canvas.paste(im, ((w - nw) // 2, (h - nh) // 2))
    return canvas


def main():
    src = next((p for p in CANDIDATI if os.path.isfile(p)), None)
    if not src:
        raise SystemExit("MANCA il logo: metti '%s' in SPINNERINO/LOGHI" % NOME_LOGO)
    img = thumbnail_letterbox(src, LOGO_W, LOGO_H)
    out = bytearray(struct.pack("<HHH", 0x5343, LOGO_W, LOGO_H))
    for (r, g, b) in img.getdata():
        out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "wb") as f:
        f.write(out)
    img.resize((LOGO_W * 4, LOGO_H * 4), Image.NEAREST).save(
        os.path.join(HERE, "pulstar_cover_anteprima.png"))
    print("OK: pulstar.scr  (%dx%d, %d byte)" % (LOGO_W, LOGO_H, len(out)))


if __name__ == "__main__":
    main()
