#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""
romconv_commando.py  (SPINNERINO WIFI port, 2026-06-19)

Convert Commando (Capcom 1985) ROM set -> FLASH C headers for SPINNERINO.

A differenza del sorgente PAOLO (che produce .bin caricati da SD), SPINNERINO
carica tutto da flash: questo script emette direttamente i .h C-array dentro
  machines/commando/
Il decode (layout MAME, rotazione ROT270, decrypt opcodi Capcom, palette dai
PROM 444 con rgb565 byte-swapped P4 = compatibile col pannello SPINNERINO) e'
IDENTICO a quello di GALAGONE PAOLO.

Simboli generati (consumati da commando.cpp):
  commando_rom_cpu1[49152]      unsigned char   (cm04 + cm03, data)
  commando_rom_cpu1_op[49152]   unsigned char   (decrypted opcodes)
  commando_rom_cpu2[16384]      unsigned char   (sound Z80)
  commando_charmap[1024*8]      unsigned short  (8x8 2bpp, 8 ushort/char)
  commando_tilemap[1024*32]     unsigned long   (16x16, lo,hi per row)
  commando_spritemap[4*768*32]  unsigned long   (4 orient x 768 sprite)
  commando_cmap_chars[16*4]     unsigned short  (RGB565 swapped)
  commando_cmap_tiles[16*8]     unsigned short
  commando_cmap_sprites[4*16]   unsigned short

Uso: python romconv_commando.py [path/to/commando.zip]
"""
import zipfile, os, sys, struct
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent
# Output: machines/commando/ del progetto SPINNERINO
OUT_DIR = (SCRIPT_DIR.parent / "machines" / "commando").resolve()

ROM_CANDIDATES = [
    Path(r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE PAOLO ULTIMO\GALAGONE_4.3_P4_NEW_multi\ROMZIP\commando.zip"),
    SCRIPT_DIR.parent / "ROM SPINNERINO" / "commando.zip",
    SCRIPT_DIR / "commando.zip",
]

def find_rom_zip():
    for p in ROM_CANDIDATES:
        if p.exists(): return p
    if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
        return Path(sys.argv[1])
    print("ERROR: commando.zip not found.")
    sys.exit(1)

def rgb565swap(r, g, b):
    r5 = (r >> 3) & 0x1F; g6 = (g >> 2) & 0x3F; b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF)

def get_bit(rom, bit_offset):
    bi = bit_offset >> 3
    if bi >= len(rom): return 0
    return 1 if (rom[bi] & (0x80 >> (bit_offset & 7))) else 0

def decode_gfx(rom, num_tiles, width, height, planes_bit, xoffs, yoffs, stride_bits):
    nplanes = len(planes_bit)
    tiles = []
    for t in range(num_tiles):
        base = t * stride_bits
        rows = []
        for y in range(height):
            row = []
            for x in range(width):
                pix = 0
                for p_idx, p_off in enumerate(planes_bit):
                    if get_bit(rom, base + yoffs[y] + xoffs[x] + p_off):
                        pix |= 1 << (nplanes - 1 - p_idx)
                row.append(pix)
            rows.append(row)
        tiles.append(rows)
    return tiles

def rotate_ccw(tile):
    N = len(tile)
    return [[tile[x][N-1-y] for x in range(N)] for y in range(N)]

def rotate_cw(tile):
    N = len(tile)
    return [[tile[N-1-x][y] for x in range(N)] for y in range(N)]

def pack_char_2bpp(tile):
    out = []
    for row in tile:
        v = 0
        for x, p in enumerate(row):
            v |= (p & 3) << (2 * x)
        out.append(v & 0xFFFF)
    return out

def pack_tile_4bpp(tile):
    out = []
    for row in tile:
        lo = 0; hi = 0
        for x in range(8):
            lo |= (row[x]   & 0x0F) << (4 * x)
        for x in range(8):
            hi |= (row[x+8] & 0x0F) << (4 * x)
        out.append((lo & 0xFFFFFFFF, hi & 0xFFFFFFFF))
    return out

def sprite_orient(tile, orient):
    rows = tile
    if orient & 2:
        rows = rows[::-1]
    if orient & 1:
        rows = [row[::-1] for row in rows]
    return rows

def build_palette(prom_r, prom_g, prom_b):
    weights = [0x0E, 0x1F, 0x43, 0x8F]
    pal = []
    for i in range(256):
        r = sum(weights[b] for b in range(4) if prom_r[i] & (1 << b))
        g = sum(weights[b] for b in range(4) if prom_g[i] & (1 << b))
        b = sum(weights[b] for b in range(4) if prom_b[i] & (1 << b))
        pal.append((min(r, 255), min(g, 255), min(b, 255)))
    return pal

def build_cmap(palette, base, groups, colors):
    out = []
    for g in range(groups):
        for c in range(colors):
            pi = base + g * colors + c
            r, gg, b = palette[pi] if pi < len(palette) else (0, 0, 0)
            out.append(rgb565swap(r, gg, b))
    return out

# ---- C header writers --------------------------------------------------------

def write_bytes_h(name, sym, data):
    with open(OUT_DIR / name, "w") as f:
        f.write(f"// Auto-generato da romconv_commando.py (SPINNERINO) - {len(data)} byte\n")
        f.write(f"const unsigned char {sym}[] = {{\n")
        for i in range(0, len(data), 16):
            f.write("  " + ",".join(str(b) for b in data[i:i+16]) + ",\n")
        f.write("};\n")
    print(f"  {name}: {len(data)} B")

def write_ushort_h(name, sym, vals):
    with open(OUT_DIR / name, "w") as f:
        f.write(f"// Auto-generato da romconv_commando.py (SPINNERINO) - {len(vals)} ushort\n")
        f.write(f"const unsigned short {sym}[] = {{\n")
        for i in range(0, len(vals), 16):
            f.write("  " + ",".join(str(v) for v in vals[i:i+16]) + ",\n")
        f.write("};\n")
    print(f"  {name}: {len(vals)} ushort")

def write_ulong_h(name, sym, vals):
    with open(OUT_DIR / name, "w") as f:
        f.write(f"// Auto-generato da romconv_commando.py (SPINNERINO) - {len(vals)} ulong\n")
        f.write(f"const unsigned long {sym}[] = {{\n")
        for i in range(0, len(vals), 8):
            f.write("  " + ",".join(f"{v}u" for v in vals[i:i+8]) + ",\n")
        f.write("};\n")
    print(f"  {name}: {len(vals)} ulong")

def main():
    zip_path = find_rom_zip()
    print(f"ROM: {zip_path}")
    print(f"OUT: {OUT_DIR}")
    os.makedirs(OUT_DIR, exist_ok=True)
    z = zipfile.ZipFile(str(zip_path))

    # --- CPU ROMs ---
    cm04 = z.read('cm04.9m')
    cm03 = z.read('cm03.8m')
    cpu1 = cm04 + cm03
    cpu2 = z.read('cm02.9f')
    write_bytes_h('commando_rom_cpu1.h', 'commando_rom_cpu1', cpu1)
    write_bytes_h('commando_rom_cpu2.h', 'commando_rom_cpu2', cpu2)

    # --- Decrypted opcode ROM (Capcom bitswap) ---
    op = bytearray(len(cpu1))
    op[0] = cpu1[0]
    for a in range(1, len(cpu1)):
        s = cpu1[a]
        op[a] = (s & 0x11) | ((s & 0xE0) >> 4) | ((s & 0x0E) << 4)
    write_bytes_h('commando_rom_cpu1_op.h', 'commando_rom_cpu1_op', bytes(op))

    # --- Char ROM: 16 KB, 1024 chars, 2bpp ---
    chars_rom = z.read('vt01.5d')
    char_x = [0, 1, 2, 3, 8, 9, 10, 11]
    char_y = [i * 16 for i in range(8)]
    chars = decode_gfx(chars_rom, 1024, 8, 8, [4, 0], char_x, char_y, 16 * 8)
    charvals = []
    for ch in chars:
        charvals += pack_char_2bpp(rotate_ccw(ch))
    write_ushort_h('commando_charmap.h', 'commando_charmap', charvals)

    # --- Tile ROM: 96KB, 3 planes, 1024 tiles 16x16 3bpp ---
    tile_rom = b''.join([z.read(f'vt{n:02d}.{s}') for n, s in
                         [(11,'5a'), (12,'6a'), (13,'7a'),
                          (14,'8a'), (15,'9a'), (16,'10a')]])
    tpb = len(tile_rom) // 3
    tile_planes = [0, tpb * 8, 2 * tpb * 8]
    tile_x = [0,1,2,3,4,5,6,7, 128,129,130,131,132,133,134,135]
    tile_y = [i * 8 for i in range(16)]
    tiles = decode_gfx(tile_rom, 1024, 16, 16, tile_planes, tile_x, tile_y, 32 * 8)
    tilevals = []
    for t in tiles:
        for lo, hi in pack_tile_4bpp(rotate_cw(t)):
            tilevals.append(lo); tilevals.append(hi)
    write_ulong_h('commando_tilemap.h', 'commando_tilemap', tilevals)

    # --- Sprite ROM: 96KB, 4 planes, 768 sprites 16x16 4bpp ---
    spr_rom = b''.join([z.read(f'vt{n:02d}.{s}') for n, s in
                        [(5,'7e'), (6,'8e'), (7,'9e'),
                         (8,'7h'), (9,'8h'), (10,'9h')]])
    shb = (len(spr_rom) // 2) * 8
    spr_planes = [shb + 4, shb + 0, 4, 0]
    spr_x = [0,1,2,3,8,9,10,11, 32*8+0,32*8+1,32*8+2,32*8+3, 33*8+0,33*8+1,33*8+2,33*8+3]
    spr_y = [i * 16 for i in range(16)]
    sprites = decode_gfx(spr_rom, 768, 16, 16, spr_planes, spr_x, spr_y, 64 * 8)
    sprvals = []
    for orient in range(4):
        for s in sprites:
            rows = sprite_orient(rotate_ccw(s), orient)
            for lo, hi in pack_sprite_4bpp(rows):
                sprvals.append(lo); sprvals.append(hi)
    write_ulong_h('commando_spritemap.h', 'commando_spritemap', sprvals)

    # --- Palette + cmaps ---
    palette = build_palette(z.read('vtb1.1d'), z.read('vtb2.2d'), z.read('vtb3.3d'))
    write_ushort_h('commando_cmap_chars.h',   'commando_cmap_chars',   build_cmap(palette, 192, 16, 4))
    write_ushort_h('commando_cmap_tiles.h',   'commando_cmap_tiles',   build_cmap(palette,   0, 16, 8))
    write_ushort_h('commando_cmap_sprites.h', 'commando_cmap_sprites', build_cmap(palette, 128,  4, 16))

    z.close()
    print("\nDone. Header generati in machines/commando/.")

# pack_sprite_4bpp == pack_tile_4bpp
def pack_sprite_4bpp(tile):
    return pack_tile_4bpp(tile)

if __name__ == '__main__':
    main()
