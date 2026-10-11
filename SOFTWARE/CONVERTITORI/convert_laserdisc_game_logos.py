#!/usr/bin/env python3
# ============================================================================
# convert_laserdisc_game_logos.py
#   Genera i loghi 94x62 delle voci del menu "GIOCHI LASERDISC" (Dragon's Lair
#   e Space Ace), da mostrare al posto delle scritte.
#
#   STESSO SISTEMA della griglia "GIOCHI ARCADE" (convert_grid_logos.py), ma con
#   DUE differenze volute (richiesta utente 2026-07-18):
#     - box 94x62 (era 72x48) = +30% lineare. Celle 99x80 (erano 77x66).
#     - RESA COMUNE 60x62 per TUTTI i giochi laserdisc, invece del thumbnail
#       aspect-preserving. Motivo: le due cover hanno aspetti diversi (DL 0.966,
#       Ace 0.826), quindi col thumbnail Ace veniva 40x48 contro i 46x48 di DL
#       e sembrava piu' piccolo. Ora entrambi rendono ESATTAMENTE 60x62.
#       LD_RENDER_W e' scelto = aspect di DL: DL resta di fatto INTATTO
#       (62*0.966 = 59.9 -> 60), Space Ace viene allargato del 17%.
#     - RGB565 BYTE-SWAPPED raw row-major PROGMEM, simbolo <base>_grid_logo
#   Altra differenza dagli arcade: qui la sorgente e' un'immagine normale
#   (PNG/JPG), non un *_logo.h RLE 224x96 -> niente rle_decode.
#
#   Restano bande nere ai lati (60 < 94): e' il comportamento voluto, le cover
#   sono verticali dentro un box orizzontale.
#
#   Sorgenti copiate nel progetto (rigenerabile anche senza CABINATONE).
#   OUT: dl_grid_logo.h, ace_grid_logo.h in OGNI copia del progetto (vedi
#        OUT_DIRS: la cartella attiva e' "BACKUP SPINNERINO PLUS\SPINNERINO",
#        che malgrado il nome e' la piu' recente e l'unica con spaceace.cpp).
# ============================================================================
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "..", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

import os

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
# Tutte le copie del progetto: scrivere in una sola lasciava l'altra indietro.
OUT_DIRS = [
    os.path.join(HERE, "..", "BACKUP SPINNERINO PLUS", "SPINNERINO"),
    os.path.join(HERE, "..", "SPINNERINO"),
]

GRID_LOGO_W, GRID_LOGO_H = 94, 62       # box (cella 99x80 in boot_menu.cpp)
# Dimensione a cui viene reso OGNI logo dentro il box, uguale per tutti i giochi
# (vedi testata): 60x62 = aspect di Dragon's Lair, che resta cosi' non deformato.
LD_RENDER_W, LD_RENDER_H = 60, 62

# (file sorgente, nome base -> simbolo <base>_grid_logo, crop del bordo bianco)
# crop_white = True per le copertine "sticker" con margine bianco attorno al
# disegno (DL2). Senza, il logo nel menu esce con una cornice bianca che le
# altre due cover non hanno.
JOBS = [
    (os.path.join(HERE, "ld_logo_src_dl.png"),  "dl",  False),  # Dragon's Lair
    (os.path.join(HERE, "ld_logo_src_ace.jpg"), "ace", False),  # Space Ace
    (os.path.join(HERE, "ld_logo_src_dl2.jpg"), "dl2", True),   # Dragon's Lair II
]


def rgb_to_565_swap(r, g, b):
    val = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F)
    return ((val & 0xFF) << 8) | (val >> 8)


def autocrop_nonblack(img, thr=24):
    """Come convert_grid_logos.py: via la cornice nera se c'e'."""
    px = img.load()
    w, h = img.size
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        for x in range(w):
            r, g, b = px[x, y]
            if r + g + b > thr:
                x0 = min(x0, x); y0 = min(y0, y)
                x1 = max(x1, x); y1 = max(y1, y)
    if x1 < x0 or y1 < y0:
        return img
    return img.crop((x0, y0, x1 + 1, y1 + 1))


def crop_white_border(img, inset=0.03):
    """Copertine 'sticker' con bordo bianco (DL2): ritaglia al disegno.

    Identica a crop_white_border di CABINATONE\\make_chooser6_scr.py (da cui
    viene la stessa immagine sorgente). inset = quanto rosicchiare dopo il
    bbox, per togliere i residui bianchi degli angoli arrotondati (il bbox e'
    rettangolare, gli angoli restano bianchi).
    """
    gray = img.convert("L")
    bbox = gray.point(lambda p: 255 if p < 232 else 0).getbbox()
    if bbox:
        img = img.crop(bbox)
    W, H = img.size
    ins = int(min(W, H) * inset)
    return img.crop((ins, ins, W - ins, H - ins))


def process(src_path, base, crop_white=False):
    img = Image.open(src_path)
    if img.mode != "RGB":
        img = img.convert("RGB")

    if crop_white:
        img = crop_white_border(img)
    img = autocrop_nonblack(img)
    src_aspect = img.width / img.height
    # RESIZE (non thumbnail) alla resa comune: tutti i giochi laserdisc escono
    # della stessa dimensione, cosi' nel menu appaiono uguali. Chi ha l'aspect
    # di DL non si deforma; gli altri vengono stirati (voluto).
    img = img.resize((LD_RENDER_W, LD_RENDER_H), Image.LANCZOS)
    canvas = Image.new("RGB", (GRID_LOGO_W, GRID_LOGO_H), (0, 0, 0))
    canvas.paste(img, ((GRID_LOGO_W - img.width) // 2,
                       (GRID_LOGO_H - img.height) // 2))
    px = canvas.load()

    rows = []
    for y in range(GRID_LOGO_H):
        rows.append("  " + ",".join(
            f"0x{rgb_to_565_swap(*px[x, y]):04X}" for x in range(GRID_LOGO_W)) + ",")
    SYM = base + "_grid_logo"
    body = (f"// Auto-generato da convert_laserdisc_game_logos.py "
            f"da {os.path.basename(src_path)} - NON modificare a mano\n"
            "#pragma once\n"
            f"#define {base.upper()}_GRID_W {GRID_LOGO_W}\n"
            f"#define {base.upper()}_GRID_H {GRID_LOGO_H}\n"
            f"const unsigned short {SYM}[{GRID_LOGO_W}*{GRID_LOGO_H}] PROGMEM = {{\n"
            + "\n".join(rows) + "\n};\n")

    stretch = (LD_RENDER_W / LD_RENDER_H) / src_aspect
    for d in OUT_DIRS:
        if not os.path.isdir(d):
            print(f"  (salto destinazione assente: {os.path.normpath(d)})")
            continue
        out_path = os.path.normpath(os.path.join(d, base + "_grid_logo.h"))
        with open(out_path, "w") as f:
            f.write(body)
        print(f"OK: {out_path}")
    print(f"    {base}: reso {LD_RENDER_W}x{LD_RENDER_H} in box "
          f"{GRID_LOGO_W}x{GRID_LOGO_H}, bande {(GRID_LOGO_W - LD_RENDER_W) // 2}px, "
          f"stretch orizzontale x{stretch:.3f}")


if __name__ == "__main__":
    for src, base, crop_white in JOBS:
        if not os.path.isfile(src):
            print(f"SKIP (sorgente assente): {src}")
            continue
        try:
            process(src, base, crop_white)
        except Exception as e:
            print(f"ERRORE su {base}: {e}")
