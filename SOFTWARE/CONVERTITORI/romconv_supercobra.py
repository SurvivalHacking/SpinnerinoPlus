#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""
Super Cobra (scobra) ROM converter -> SPINNERINO WIFI (ESP32-P4).

Porting del converter galagino-main (romconv/supercobra/supercobra_rom_convert.py)
allo stile SPINNERINO:
  - path PORTABILI: OUT_DIR risolto rispetto alla posizione dello script
    (FILE ROMCONV PY -> root sketch -> machines/supercobra).
  - ROM zip cercata automaticamente in piu' posizioni (vedi ROM_CANDIDATES),
    oppure passata come primo argomento: python romconv_supercobra.py <scobra.zip>
  - output con PROGMEM (coerente con scramble_*.h).
  - colormap NON pre-swappata: la blit di supercobra fa lo swap d'indice
    ((pix&2)>>1)|((pix&1)<<1) come galagino-main (vedi supercobra.cpp).

Genera in machines/supercobra/:
  supercobra_main_rom.h   (unsigned char,  0x6000)
  supercobra_audio_rom.h  (unsigned char,  0x2000)
  supercobra_tilemap.h    (unsigned short [][8])
  supercobra_spritemap.h  (unsigned long  [4][N][16])
  supercobra_cmap.h       (unsigned short [8][4], var: supercobra_colormap)

ROM set (scobra), MAME:
  Main CPU:  epr1265.2c  2e  epr1267.2f  2h  epr1269.2j  2l   (6 x 0x1000 = 0x6000)
  Audio CPU: 5c 5d 5e                                         (3 x 0x0800 = 0x1800)
  gfx1:      epr1274.5h  epr1273.5f                           (2 x 0x0800)
  prom:      82s123.6e                                        (0x20)
"""

import os
import sys
import zipfile
import hashlib

sys.dont_write_bytecode = True

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SKETCH_DIR = os.path.dirname(SCRIPT_DIR)                       # root sketch SPINNERINO
OUT_DIR    = os.path.join(SKETCH_DIR, "machines", "supercobra")

# Possibili posizioni della zip (la prima esistente vince). Si puo' anche passare
# il path come argv[1].
ROM_CANDIDATES = [
    os.path.join(os.path.expanduser("~"), "Downloads", "scobra.zip"),
    os.path.normpath(os.path.join(SKETCH_DIR, "..", "..", "SPECK OILER PROJECT",
                                  "galagino-main", "romszip", "scobra.zip")),
    os.path.join(SCRIPT_DIR, "scobra.zip"),
    os.path.join(SKETCH_DIR, "scobra.zip"),
]

def find_rom_zip():
    if len(sys.argv) > 1 and os.path.isfile(sys.argv[1]):
        return sys.argv[1]
    for c in ROM_CANDIDATES:
        if os.path.isfile(c):
            return c
    print("ERRORE: scobra.zip non trovata. Posizioni cercate:")
    for c in ROM_CANDIDATES:
        print("  -", os.path.abspath(c))
    print("Oppure: python romconv_supercobra.py <path/scobra.zip>")
    sys.exit(1)

ROM_SET = find_rom_zip()

def hex8(v):  return "0x{:02x}".format(v & 0xFF)
def hex16(v): return "0x{:04x}".format(v & 0xFFFF)
def hex32(v): return "0x{:08x}".format(v & 0xFFFFFFFF)

def load_file(names, sha1):
    with zipfile.ZipFile(ROM_SET) as z:
        nl = z.namelist()
        for name in names:
            if name in nl:
                with z.open(name, 'r') as f:
                    rom = bytearray(f.read())
                digest = hashlib.sha1(rom).hexdigest()
                if sha1 and digest != sha1:
                    print(f"  WARN: hash mismatch {name} {digest} != {sha1}")
                return rom
    print(f"ERRORE: nessuno di {names} trovato in {os.path.abspath(ROM_SET)}")
    return None

# ---- writers (PROGMEM) -------------------------------------------------
def write_rom(filename, name, data):
    with open(filename, 'w') as f:
        f.write("// Super Cobra {} ({} bytes)\n".format(name, len(data)))
        f.write("const unsigned char {}[] PROGMEM = {{\n".format(name))
        for i in range(0, len(data), 16):
            line = ", ".join(hex8(data[j]) for j in range(i, min(i+16, len(data))))
            f.write("  " + line + ("," if i + 16 < len(data) else "") + "\n")
        f.write("};\n")
    print("Wrote:", os.path.abspath(filename), "(%d bytes)" % len(data))

def write_tilemap(filename, tiles):
    with open(filename, 'w') as f:
        f.write("// Super Cobra tilemap: {} tiles, 8x8, 2bpp\n".format(len(tiles)))
        f.write("const unsigned short supercobra_tilemap[][8] PROGMEM = {\n")
        for t, rows in enumerate(tiles):
            f.write("  { " + ", ".join(hex16(r) for r in rows) + " }")
            f.write("," if t < len(tiles) - 1 else "")
            f.write("\n")
        f.write("};\n")
    print("Wrote:", os.path.abspath(filename), "(%d tiles)" % len(tiles))

def write_spritemap(filename, all_orientations):
    num = len(all_orientations[0])
    with open(filename, 'w') as f:
        f.write("// Super Cobra spritemap: {} sprites, 16x16, 2bpp, 4 orient\n".format(num))
        f.write("const unsigned long supercobra_spritemap[4][%d][16] PROGMEM = {\n" % num)
        for o, sprites in enumerate(all_orientations):
            f.write("  { // orientation %d\n" % o)
            for s, rows in enumerate(sprites):
                f.write("    { " + ", ".join(hex32(r) for r in rows) + " }")
                f.write("," if s < len(sprites) - 1 else "")
                f.write("\n")
            f.write("  }" + ("," if o < 3 else "") + "\n")
        f.write("};\n")
    print("Wrote:", os.path.abspath(filename), "(%d sprites x4)" % num)

def write_colormap(filename, rgb565):
    with open(filename, 'w') as f:
        f.write("// Super Cobra colormap: 8 palettes x 4 colors, RGB565 byte-swapped\n")
        f.write("const unsigned short supercobra_colormap[][4] PROGMEM = {\n")
        for pal in range(8):
            colors = rgb565[pal*4: pal*4+4]
            f.write("  { " + ", ".join(hex16(c) for c in colors) + " }")
            f.write("," if pal < 7 else "")
            f.write("  // palette {}\n".format(pal))
        f.write("};\n")
    print("Wrote:", os.path.abspath(filename), "(8 palettes)")

# ---- gfx conversion (algoritmi originali galagino) ---------------------
def parse_chr_2(d0, d1):
    char = []
    for y in range(8):
        row = []
        for x in range(8):
            c0 = 1 if d0[7 - x] & (0x80 >> y) else 0
            c1 = 2 if d1[7 - x] & (0x80 >> y) else 0
            row.append(c0 + c1)
        char.append(row)
    return char

def dump_chr(data):
    vals = []
    for y in range(8):
        val = 0
        for x in range(8):
            val = (val >> 2) + (data[y][x] << (16 - 2))
        vals.append(val)
    return vals

def convert_tiles(p0, p1):
    n = len(p0) // 8
    return [dump_chr(parse_chr_2(p0[t*8:t*8+8], p1[t*8:t*8+8])) for t in range(n)]

def parse_sprite_galaxian(d0, d1):
    sprite = []
    for y in range(16):
        row = []
        for x in range(16):
            ym = (y & 7) | ((x & 8) ^ 8)
            xm = (x & 7) | (y & 8)
            byte_idx = (xm ^ 7) + ((ym & 8) << 1)
            bit_mask = 0x80 >> (ym & 7)
            c0 = 1 if d0[byte_idx] & bit_mask else 0
            c1 = 2 if d1[byte_idx] & bit_mask else 0
            row.append(c0 + c1)
        sprite.append(row)
    return sprite

def dump_sprite(data, flip_x, flip_y):
    vals = []
    y_range = range(16) if not flip_y else reversed(range(16))
    for y in y_range:
        val = 0
        for x in range(16):
            if not flip_x:
                val = (val >> 2) + (data[y][x] << (32 - 2))
            else:
                val = (val << 2) + data[y][x]
        vals.append(val)
    return vals

def convert_sprites(p0, p1):
    n = len(p0) // 32
    sprites = [parse_sprite_galaxian(p0[32*s:32*(s+1)], p1[32*s:32*(s+1)]) for s in range(n)]
    out = []
    for fx, fy in [(False, False), (False, True), (True, False), (True, True)]:
        out.append([dump_sprite(s, fx, fy) for s in sprites])
    return out

def convert_colors(prom):
    rgb565 = []
    for i in range(32):
        bits = prom[i]
        r = 0x21*((bits>>0)&1) + 0x47*((bits>>1)&1) + 0x97*((bits>>2)&1)
        g = 0x21*((bits>>3)&1) + 0x47*((bits>>4)&1) + 0x97*((bits>>5)&1)
        b = 0x4F*((bits>>6)&1) + 0xA8*((bits>>7)&1)
        val = (((r>>3)&0x1F) << 11) | (((g>>2)&0x3F) << 5) | ((b>>3)&0x1F)
        rgb565.append(((val & 0xFF) << 8) | ((val >> 8) & 0xFF))   # byte-swap (ESP32 SPI BE)
    return rgb565

def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    print("Load ROM from:", os.path.abspath(ROM_SET))
    print("Target dir:   ", os.path.abspath(OUT_DIR))

    rom_2c = load_file(["epr1265.2c"], "8949298a04f8ba8a82d5d84a7b012a0e7cff11df")
    rom_2e = load_file(["2e"],         "281504ff364c3ddbf901c92729b139afd93b9785")
    rom_2f = load_file(["epr1267.2f"], "01775ad11dc23469649539ee8fb8a5800df031c6")
    rom_2h = load_file(["2h"],         "f5fff565ed3f6c5f277a4db53c9f569813fcec1d")
    rom_2j = load_file(["epr1269.2j"], "2add8270352d6596052d3ff22c891ceccaa92071")
    rom_2l = load_file(["2l"],         "5681771ed51d504bdcc2999fcbf926a30b137828")

    rom_5c = load_file(["5c"], "5eab4505beb69a5bdd88b23db60e1193371250cf")
    rom_5d = load_file(["5d"], "2b0784c4d05c466e0b7648f16e14f34393d792c3")
    rom_5e = load_file(["5e"], "ec79a73e4a2d7373454b227dd7eff255f1cc60cc")

    rom_5h = load_file(["epr1274.5h"], "7b439bb74d5ecc792e0ca8964bcca8c6b7a51262")
    rom_5f = load_file(["epr1273.5f"], "9de0e94932e91dc34aea7c81880bde6a486d103b")

    prom_6e = load_file(["82s123.6e"], "d11ac5e4a6057301ea2a9cbb404c2b978eb4c1dc")

    if any(v is None for v in [rom_2c, rom_2e, rom_2f, rom_2h, rom_2j, rom_2l,
                               rom_5c, rom_5d, rom_5e, rom_5h, rom_5f, prom_6e]):
        print("ERRORE: ROM incomplete"); sys.exit(1)

    main_cpu = rom_2c + rom_2e + rom_2f + rom_2h + rom_2j + rom_2l   # 0x6000
    if len(main_cpu) < 0x6000:
        main_cpu += bytearray([0xFF] * (0x6000 - len(main_cpu)))
    write_rom(os.path.join(OUT_DIR, "supercobra_main_rom.h"), "supercobra_main_rom", main_cpu)

    audio_cpu = rom_5c + rom_5d + rom_5e                              # 0x1800
    if len(audio_cpu) < 0x2000:
        audio_cpu += bytearray([0xFF] * (0x2000 - len(audio_cpu)))
    write_rom(os.path.join(OUT_DIR, "supercobra_audio_rom.h"), "supercobra_audio_rom", audio_cpu)

    write_tilemap(os.path.join(OUT_DIR, "supercobra_tilemap.h"), convert_tiles(rom_5h, rom_5f))
    write_spritemap(os.path.join(OUT_DIR, "supercobra_spritemap.h"), convert_sprites(rom_5h, rom_5f))
    write_colormap(os.path.join(OUT_DIR, "supercobra_cmap.h"), convert_colors(prom_6e))

    print("\n--- Complete ---")

if __name__ == "__main__":
    main()
