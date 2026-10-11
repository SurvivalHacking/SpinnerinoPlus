# ============================================================================
# convert_neogeo_sd_logos.py
# Cover dei giochi NEO GEO per il menu "GIOCHI NEO GEO" dello SPINNERINO.
#
# ⭐ Come su CABINATONE il logo NON sta in flash: sta sulla SD, in
#    /neogeo/<gioco>.scr. Stesso FORMATO di CABINATONE e di splash/chooser:
#        magic 0x5343 ('SC') | w uint16 LE | h uint16 LE | RGB565 LITTLE-ENDIAN
#    ⚠️ Il pannello SPI dello SPINNERINO vuole i pixel BYTE-SWAPPATI: lo swap lo
#    fa il firmware al caricamento (NeoGeoMenu::probe_sd), NON questo script —
#    cosi' il .scr resta lo stesso file per tutti e due i progetti.
#
# DIMENSIONE: 94x62, cioe' la cella del menu GIOCHI LASERDISC, non i 513 px di
# CABINATONE (quello ha un pannello da 1024x600; qui l'area utile e' 232 px).
# ASPETTO PRESERVATO + letterbox su nero: per le celle piccole di griglia
# l'utente vuole la thumbnail, non lo stretch (lo stretch vale per i banner
# grandi come la card del carosello).
#
# USO:  python convert_neogeo_sd_logos.py
# ============================================================================
import os
import struct
from PIL import Image

LOGO_W, LOGO_H = 94, 62        # = LD_LOGO_W / LD_LOGO_H del firmware

_PY = os.path.dirname(os.path.abspath(__file__))
_CAB = r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE"
OUT_DIR = os.path.abspath(os.path.join(_PY, "..", "SD SPINNERINO", "neogeo"))

# (immagine sorgente in CABINATONE\ROMS, nome del .scr sulla SD)
# ⚠️ Le stesse immagini che usa `make_arcade_logos.py` di CABINATONE per le sue
#    card 513 px: una sola sorgente per i due progetti, cosi' non divergono.
GIOCHI = [
    ("ROMS/metal slug logo.jpeg",   "mslug.scr"),
    ("ROMS/metal slug 2 logo.jpeg", "mslug2.scr"),
]


def to_scr(img, path):
    w, h = img.size
    out = bytearray(struct.pack("<HHH", 0x5343, w, h))
    for (r, g, b) in img.convert("RGB").getdata():
        out += struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))
    with open(path, "wb") as f:
        f.write(out)
    print("OK: %s  (%dx%d, %d byte)" % (path, w, h, len(out)))


def thumbnail_letterbox(src, w, h):
    """Aspetto PRESERVATO, centrata, bande nere. Come convert_grid_logos.py."""
    im = Image.open(src).convert("RGB")
    iw, ih = im.size
    s = min(w / iw, h / ih)
    nw, nh = max(1, int(round(iw * s))), max(1, int(round(ih * s)))
    im = im.resize((nw, nh), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), (0, 0, 0))
    canvas.paste(im, ((w - nw) // 2, (h - nh) // 2))
    return canvas


def main():
    if not os.path.isdir(OUT_DIR):
        os.makedirs(OUT_DIR)
    for (rel, name) in GIOCHI:
        src = os.path.join(_CAB, rel.replace("/", os.sep))
        img = thumbnail_letterbox(src, LOGO_W, LOGO_H)
        to_scr(img, os.path.join(OUT_DIR, name))


if __name__ == "__main__":
    main()
