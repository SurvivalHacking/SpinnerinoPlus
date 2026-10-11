#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""
Donkey Kong 3 (dkong3j) ROM converter -> SPINNERINO WIFI (ESP32-P4).

Porting unificato dei converter galagino-main (romconv/dkong3/*.py) allo stile
SPINNERINO: path PORTABILI, ROM letta direttamente dalla zip (auto-ricerca o
argv[1]), output in machines/dkong3/.

Genera:
  dkong3_rom.h          dkong3_rom_cpu[0x10000]   (Z80 main: 7b@0,7c@2000,7d@4000,7e@8000)
  dkong3_sound_roms.h   dkong3_rom_sound_a/b[0x2000]  (RP2A03: 5l, 6h)
  dkong3_tilemap.h      dkong3_tilemap[512][8]    (orient. TILE_ROT, vedi sotto)
  dkong3_spritemap.h    dkong3_sprites[4][256][16]
  dkong3_cmap.h         dkong3_colormap_sprite[64][4]  (RGB565 byte-swapped)
  dkong3_color_codes.h  color_codes_prom[256]

NB ORIENTAMENTO: galagino rende NON trasposto e pre-ruota i tile. SPINNERINO usa
il render TRASPOSTO (render_row) come dkongjr, quindi i tile NON vanno pre-ruotati
(TILE_TRANSPOSE=False di default). Verificare con preview_dkong3.py.
"""
import os, sys, zipfile

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SKETCH_DIR = os.path.dirname(SCRIPT_DIR)
OUT_DIR    = os.path.join(SKETCH_DIR, "machines", "dkong3")

ROM_CANDIDATES = [
    os.path.join(os.path.expanduser("~"), "Downloads", "dkong3.zip"),
    os.path.normpath(os.path.join(SKETCH_DIR, "..", "..", "SPECK OILER PROJECT",
                                  "galagino-main", "romszip", "dkong3.zip")),
    os.path.join(SCRIPT_DIR, "dkong3.zip"),
]

# Orientamento tile: False = nessuna pre-rotazione (per render trasposto SPINNERINO).
# True = transpose come galagino (render diretto). Cambiare se la preview e' storta.
TILE_TRANSPOSE = True

def find_zip():
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        return sys.argv[1]
    for c in ROM_CANDIDATES:
        if os.path.isfile(c):
            return c
    print("ERRORE: dkong3.zip non trovata. Cercata in:")
    for c in ROM_CANDIDATES: print("  -", os.path.abspath(c))
    sys.exit(1)

ZIP = find_zip()
_zf = zipfile.ZipFile(ZIP)
def rom(name):
    return bytearray(_zf.read(name))

def hx8(v):  return "0x{:02X}".format(v & 0xFF)
def hx16(v): return "0x{:04x}".format(v & 0xFFFF)
def hx32(v): return "0x{:08x}".format(v & 0xFFFFFFFF)

def write_bytes(fn, name, data, progmem=True):
    with open(fn, "w") as f:
        f.write("// Donkey Kong 3 - {} ({} bytes)\n".format(name, len(data)))
        f.write("#include <pgmspace.h>\n" if progmem else "")
        f.write("const unsigned char {}{}[{}] = {{\n  ".format(
            "PROGMEM " if progmem else "", name, len(data)))
        for i, b in enumerate(data):
            f.write(hx8(b))
            if i < len(data) - 1:
                f.write(",")
                if (i + 1) % 16 == 0: f.write("\n  ")
        f.write("\n};\n")
    print("Wrote", os.path.abspath(fn), "(%d B)" % len(data))

# ---- CPU ROM (Z80) -------------------------------------------------------
def conv_cpu():
    mem = bytearray([0xFF] * 0x10000)
    mem[0x0000:0x2000] = rom("dk3c.7b")
    mem[0x2000:0x4000] = rom("dk3c.7c")
    mem[0x4000:0x6000] = rom("dk3c.7d")
    mem[0x8000:0xA000] = rom("dk3c.7e")
    write_bytes(os.path.join(OUT_DIR, "dkong3_rom.h"), "dkong3_rom_cpu", mem, progmem=False)

# ---- Sound ROMs (RP2A03) -------------------------------------------------
def conv_sound():
    a = rom("dk3c.5l")   # sound CPU #0
    b = rom("dk3c.6h")   # sound CPU #1
    with open(os.path.join(OUT_DIR, "dkong3_sound_roms.h"), "w") as f:
        f.write("// Donkey Kong 3 - sound ROMs RP2A03 (dk3c.5l, dk3c.6h)\n")
        f.write("#include <pgmspace.h>\n")
        for nm, d in [("dkong3_rom_sound_a", a), ("dkong3_rom_sound_b", b)]:
            f.write("const unsigned char PROGMEM {}[{}] = {{\n  ".format(nm, len(d)))
            for i, x in enumerate(d):
                f.write(hx8(x))
                if i < len(d) - 1:
                    f.write(",")
                    if (i + 1) % 16 == 0: f.write("\n  ")
            f.write("\n};\n")
    print("Wrote dkong3_sound_roms.h (%d + %d B)" % (len(a), len(b)))

# ---- Tiles ---------------------------------------------------------------
def parse_chr_2(d0, d1):
    ch = [[0]*8 for _ in range(8)]
    for y in range(8):
        for x in range(8):
            b0 = 1 if d0[y] & (1 << (7 - x)) else 0
            b1 = 2 if d1[y] & (1 << (7 - x)) else 0
            ch[y][x] = b0 + b1
    if TILE_TRANSPOSE:
        t = [[0]*8 for _ in range(8)]
        for y in range(8):
            for x in range(8):
                t[x][y] = ch[y][x]
        return t
    return ch

def dump_chr(ch):
    out = []
    for y in range(8):
        v = 0
        for x in range(8):
            v = (v << 2) | ch[y][x]
        out.append(hx16(v))
    return ",".join(out)

def conv_tiles():
    d0 = rom("dk3v.3n"); d1 = rom("dk3v.3p")
    assert len(d0) == 4096 and len(d1) == 4096
    tiles = [parse_chr_2(d0[i*8:i*8+8], d1[i*8:i*8+8]) for i in range(512)]
    with open(os.path.join(OUT_DIR, "dkong3_tilemap.h"), "w") as f:
        f.write("// Donkey Kong 3 tilemap 512x 8x8 2bpp (TILE_TRANSPOSE=%s)\n" % TILE_TRANSPOSE)
        f.write("const unsigned short dkong3_tilemap[512][8] = {\n")
        f.write(",\n".join("  { " + dump_chr(t) + " }" for t in tiles))
        f.write("\n};\n")
    print("Wrote dkong3_tilemap.h (512 tiles)")

# ---- Sprites -------------------------------------------------------------
def parse_sprite(chunks):
    spr = []
    for y in range(16):
        row = []
        for x in range(16):
            c1 = 2 if chunks[y // 8 + 2][15 - x] & (0x80 >> (y & 7)) else 0
            c0 = 1 if chunks[y // 8][15 - x] & (0x80 >> (y & 7)) else 0
            row.append(c0 + c1)
        spr.append(row)
    return spr

def dump_sprite(s, fx, fy):
    out = []
    yr = reversed(range(16)) if fy else range(16)
    for yi in yr:
        v = 0
        prow = s[yi][::-1] if fx else s[yi]
        for p in prow:
            v = (v << 2) | p
        out.append(hx32(v))
    return ",".join(out)

def conv_sprites():
    roms = [rom("dk3v.7c"), rom("dk3v.7d"), rom("dk3v.7e"), rom("dk3v.7f")]
    sprites = []
    for i in range(256):
        off = i * 16
        sprites.append(parse_sprite([r[off:off+16] for r in roms]))
    with open(os.path.join(OUT_DIR, "dkong3_spritemap.h"), "w") as f:
        f.write("// Donkey Kong 3 sprites 256x 16x16 2bpp, 4 flip\n")
        f.write("#include <pgmspace.h>\n")
        f.write("const unsigned long PROGMEM dkong3_sprites[4][256][16] = {\n")
        for flag in range(4):
            fx = (flag & 1) != 0; fy = (flag & 2) != 0
            f.write("  { // flip x=%s y=%s\n" % (fx, fy))
            f.write(",\n".join("    { " + dump_sprite(s, fx, fy) + " }" for s in sprites))
            f.write("\n  }" + ("," if flag < 3 else "") + "\n")
        f.write("};\n")
    print("Wrote dkong3_spritemap.h (256 sprites x4)")

# ---- Colormap + color codes ---------------------------------------------
def conv_palette():
    rg = rom("dkc1-c.1d"); b = rom("dkc1-c.1c")
    assert len(rg) >= 512 and len(b) >= 512
    pal = []
    for i in range(512):
        if i < 256:
            brg = (~rg[i]) & 0xFF; bb = (~b[i]) & 0xFF
        else:
            brg = rg[i]; bb = b[i]
        r4 = (brg >> 4) & 0x0F; g4 = brg & 0x0F; b4 = bb & 0x0F
        r8 = int((r4/15.0)*255); g8 = int((g4/15.0)*255); b8 = int((b4/15.0)*255)
        v = ((r8 >> 3) << 11) | ((g8 >> 2) << 5) | (b8 >> 3)
        pal.append(((v & 0xFF00) >> 8) | ((v & 0x00FF) << 8))   # byte-swap
    sprite_pal = pal[256:]   # 256 colori = 64 palette x4
    with open(os.path.join(OUT_DIR, "dkong3_cmap.h"), "w") as f:
        f.write("// Donkey Kong 3 colormap sprite/tile 64 palette x4 (RGB565 byte-swapped)\n")
        f.write("const unsigned short dkong3_colormap_sprite[64][4] = {\n")
        rows = []
        for i in range(64):
            g = sprite_pal[i*4:i*4+4]
            rows.append("  { " + ",".join(hx16(c) for c in g) + " }")
        f.write(",\n".join(rows))
        f.write("\n};\n")
    print("Wrote dkong3_cmap.h (64 palette)")

def conv_color_codes():
    prom = rom("dkc1-v.2n")
    assert len(prom) == 256
    with open(os.path.join(OUT_DIR, "dkong3_color_codes.h"), "w") as f:
        f.write("// Donkey Kong 3 color codes PROM (dkc1-v.2n)\n")
        f.write("#include <pgmspace.h>\n")
        f.write("const unsigned char PROGMEM color_codes_prom[256] = {\n  ")
        f.write(",\n  ".join(", ".join(hx8(prom[i+j]) for j in range(16)) for i in range(0,256,16)))
        f.write("\n};\n")
    print("Wrote dkong3_color_codes.h")

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("ROM:", os.path.abspath(ZIP)); print("OUT:", os.path.abspath(OUT_DIR))
    print("Files in zip:", _zf.namelist())
    conv_cpu(); conv_sound(); conv_tiles(); conv_sprites(); conv_palette(); conv_color_codes()
    print("--- DK3 ROM conversion complete ---")

if __name__ == "__main__":
    main()
