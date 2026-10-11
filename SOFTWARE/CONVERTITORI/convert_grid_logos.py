# ============================================================================
# convert_grid_logos.py
# Genera i loghi PICCOLI per la griglia 3x3 del nuovo menu "GIOCHI ARCADE".
#
# Sorgente: i *_logo.h di GALAGONE 2.8VPE, che sono 224x96 RGB565 byte-swapped
# in formato RLE (marker-based, vedi convert_motorace_logo.py del sorgente):
#   data[0] = marker (valore 16-bit non usato nei pixel)
#   stream:  se v == marker -> seguono (run-1, pixel) = run copie di pixel
#            altrimenti      -> v e' un pixel letterale (1 copia)
#
# Pipeline: decode RLE -> unswap a RGB888 -> autocrop bbox non-nero ->
#           fit dentro GRID_LOGO_W x GRID_LOGO_H mantenendo l'aspetto,
#           centrato su nero -> re-encode RGB565 swapped RAW (no RLE).
#
# Output: <nome>_grid_logo.h  ->  const unsigned short <nome>_grid_logo[W*H]
#   Raw row-major portrait (come gli altri loghi RAW del menu SPINNERINO).
# ============================================================================
import re, os
from PIL import Image

SRC_DIR = r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE 2.8VPE\GALAGONE\machines"
OUT_DIR = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO"

SRC_W, SRC_H = 224, 96          # dimensione canvas dei loghi sorgente
GRID_LOGO_W, GRID_LOGO_H = 72, 48   # box logo nella cella della griglia

# (file sorgente .h, nome simbolo nel sorgente, nome base output)
SP_MACHINES = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO\machines"
JOBS = [
    (os.path.join(SRC_DIR, "xevious", "xevious_logo.h"),       "xevious_logo", "xevious"),
    (os.path.join(SRC_DIR, "gyruss",  "gyruss_logo_tft.h"),    "gyruss_logo",  "gyruss"),
    (os.path.join(SP_MACHINES, "pacman", "pacman_logo.h"),     "pacman_logo",  "pacman"),
    (os.path.join(SP_MACHINES, "1942",   "1942_logo.h"),       "_1942_logo",   "_1942"),
    (os.path.join(SP_MACHINES, "pbaction", "pbaction_logo.h"), "pbaction_logo", "pbaction"),
    (os.path.join(SP_MACHINES, "bombjack", "bombjack_logo.h"), "bombjack_logo", "bombjack"),
    # --- Nuovi porting da FULL33 (2026-06-17) ---
    (os.path.join(SP_MACHINES, "dkongjr",    "dkongjr_logo.h"),    "dkongjr_logo",    "dkongjr"),
    (os.path.join(SP_MACHINES, "frogger",    "frogger_logo.h"),    "frogger_logo",    "frogger"),
    (os.path.join(SP_MACHINES, "greenberet", "greenberet_logo.h"), "greenberet_logo", "greenberet"),
    (os.path.join(SP_MACHINES, "ladybug",    "ladybug_logo.h"),    "ladybug_logo",    "ladybug"),
    (os.path.join(SP_MACHINES, "moonpatrol", "moonpatrol_logo.h"), "moonpatrol_logo", "moonpatrol"),
    (os.path.join(SP_MACHINES, "mariobros",  "mariobros_logo.h"),  "mariobros_logo",  "mariobros"),
    # --- Nuovo porting MS PAC-MAN (2026-06-19): logo in-game P4 da PAOLO ---
    (os.path.join(SP_MACHINES, "mspacman",   "mspacman_logo.h"),   "mspacman_logo",   "mspacman"),
    # --- Nuovo porting COMMANDO (2026-06-19): logo da commando.jpg ---
    (os.path.join(SP_MACHINES, "commando",   "commando_logo.h"),   "commando_logo",   "commando"),
    # --- Nuovo porting DONKEY KONG (2026-06-19): logo già nel target ---
    (os.path.join(SP_MACHINES, "dkong",      "dkong_logo.h"),      "dkong_logo",      "dkong"),
    # --- Nuovo porting BURNIN' RUBBER (2026-06-19): logo 224x76 (non 224x96) ---
    (os.path.join(SP_MACHINES, "brubber",    "brubber_logo.h"),    "brubber_logo",    "brubber", (224, 76)),
    # --- Nuovo porting SCRAMBLE (2026-06-19): simbolo Scramble_logo (S maiusc) ---
    (os.path.join(SP_MACHINES, "scramble",   "scramble_logo.h"),   "Scramble_logo",   "scramble"),
    # --- Nuovo porting POPEYE (2026-06-19) ---
    (os.path.join(SP_MACHINES, "popeye",     "popeye_logo.h"),     "popeye_logo",     "popeye"),
    # --- Nuovo porting FANTASY (2026-06-19) ---
    (os.path.join(SP_MACHINES, "fantasy",    "fantasy_logo.h"),    "fantasy_logo",    "fantasy"),
    # --- Nuovo porting NIBBLER (2026-06-19) ---
    (os.path.join(SP_MACHINES, "nibbler",    "nibbler_logo.h"),    "nibbler_logo",    "nibbler"),
    # --- Nuovo porting MOON CRESTA (2026-06-25): logo in-game RLE 224x96 da PAOLO 3.5 ---
    (os.path.join(SP_MACHINES, "mooncresta", "mooncresta_logo.h"), "mooncresta_logo", "mooncresta"),
    # --- Nuovo porting TIME PILOT (2026-06-25): logo in-game RLE 224x96 da FULL33 ---
    (os.path.join(SP_MACHINES, "timeplt",    "timeplt_logo.h"),    "timeplt_logo",    "timeplt"),
    # --- Nuovo porting GHOSTS'N GOBLINS (2026-06-28): logo TFT RLE 224x96 da PAOLO 4.3 ---
    (os.path.join(SP_MACHINES, "gng",        "gng_logo.h"),        "gng_logo",        "gng"),
    # --- Nuovo porting SUPER COBRA (2026-06-30): logo RLE 224x96 da galagino-main ---
    (os.path.join(SP_MACHINES, "supercobra", "supercobra_logo.h"), "supercobra_logo", "supercobra"),
    # --- Nuovo porting DONKEY KONG 3 (2026-06-30): logo RLE 224x96 da galagino-main ---
    (os.path.join(SP_MACHINES, "dkong3", "dkong3_logo.h"), "dkong3_logo", "dkong3"),
]


def parse_ints(path, symbol):
    """Estrae gli interi dell'array C `symbol[] = { ... }`."""
    txt = open(path, "r", errors="ignore").read()
    # isola il blocco dell'array giusto (il file gyruss ha anche l'XBM OLED)
    m = re.search(symbol + r"\s*\[\s*\]\s*=\s*\{(.*?)\};", txt, re.DOTALL)
    if not m:
        m = re.search(r"\{(.*?)\};", txt, re.DOTALL)   # fallback: primo blocco
    body = m.group(1)
    return [int(x, 0) for x in re.findall(r"-?\b\d+\b|0x[0-9A-Fa-f]+", body)]


def rle_decode(data, n_pixels):
    marker = data[0]
    out, i = [], 1
    while i < len(data) and len(out) < n_pixels:
        v = data[i]; i += 1
        if v == marker:
            run = data[i] + 1; i += 1
            pix = data[i]; i += 1
            out.extend([pix] * run)
        else:
            out.append(v)
    if len(out) < n_pixels:
        out.extend([0] * (n_pixels - len(out)))
    return out[:n_pixels]


def unswap_to_rgb(v):
    v = ((v & 0xFF) << 8) | (v >> 8)        # annulla il byte-swap
    r = ((v >> 11) & 0x1F) << 3
    g = ((v >> 5) & 0x3F) << 2
    b = (v & 0x1F) << 3
    return (r, g, b)


def rgb_to_565_swap(r, g, b):
    val = (((r >> 3) & 0x1F) << 11) | (((g >> 2) & 0x3F) << 5) | ((b >> 3) & 0x1F)
    return ((val & 0xFF) << 8) | (val >> 8)


def autocrop_nonblack(img, thr=24):
    px = img.load(); w, h = img.size
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


def process(src_path, symbol, base, sw=SRC_W, sh=SRC_H):
    ints = parse_ints(src_path, symbol)
    pixels = rle_decode(ints, sw * sh)
    img = Image.new("RGB", (sw, sh))
    img.putdata([unswap_to_rgb(v) for v in pixels])

    img = autocrop_nonblack(img)
    # fit mantenendo aspetto dentro il box griglia, centrato su nero
    img.thumbnail((GRID_LOGO_W, GRID_LOGO_H), Image.LANCZOS)
    canvas = Image.new("RGB", (GRID_LOGO_W, GRID_LOGO_H), (0, 0, 0))
    canvas.paste(img, ((GRID_LOGO_W - img.width) // 2,
                       (GRID_LOGO_H - img.height) // 2))
    px = canvas.load()

    out_path = os.path.join(OUT_DIR, base + "_grid_logo.h")
    SYM = base + "_grid_logo"
    with open(out_path, "w") as f:
        f.write(f"// Auto-generato da convert_grid_logos.py da {os.path.basename(src_path)}\n")
        f.write("#pragma once\n")
        f.write(f"#define {base.upper()}_GRID_W {GRID_LOGO_W}\n")
        f.write(f"#define {base.upper()}_GRID_H {GRID_LOGO_H}\n")
        f.write(f"const unsigned short {SYM}[{GRID_LOGO_W}*{GRID_LOGO_H}] PROGMEM = {{\n")
        for y in range(GRID_LOGO_H):
            row = [f"0x{rgb_to_565_swap(*px[x, y]):04X}" for x in range(GRID_LOGO_W)]
            f.write("  " + ",".join(row) + ",\n")
        f.write("};\n")
    print(f"OK: {out_path}  ({GRID_LOGO_W}x{GRID_LOGO_H})")


if __name__ == "__main__":
    for job in JOBS:
        # job = (src, sym, base) oppure (src, sym, base, (sw, sh)) per loghi
        # con canvas sorgente diverso da 224x96 (es. brubber = 224x76).
        if len(job) == 4:
            src, sym, base, (sw, sh) = job
        else:
            src, sym, base = job
            sw, sh = SRC_W, SRC_H
        # Tollerante: alcuni sorgenti (xevious/gyruss) stanno in cartelle GALAGONE
        # che potrebbero non essere presenti; salta con avviso invece di crashare.
        if not os.path.isfile(src):
            print(f"SKIP (sorgente assente): {src}")
            continue
        try:
            process(src, sym, base, sw, sh)
        except Exception as e:
            print(f"ERRORE su {base}: {e}")
