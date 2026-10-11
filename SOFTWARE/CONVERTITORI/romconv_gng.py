#!/usr/bin/env python3
"""
ROM converter for Ghosts'n Goblins (Capcom 1985) — GALAGONE 3.5 sambi variant.
Generates header files (.h with const arrays, NO PROGMEM, dim esplicita [N])
directly into machines/gng/.

Pattern sambi (NON usa SD card):
  - I file .h sono inclusi UNA SOLA volta da gng.cpp ROOT (definitions).
  - gng.h dichiara `extern const ...[N]` (declarations) per .ino e .cpp.

Hardware:
  Main CPU: MC6809E @ 1.5 MHz
  Sound CPU: Z80 @ 3.579545 MHz, 2x YM2203 (SSG portion = AY-3-8910)
  Video: 256x224 ROT0 (horizontal/landscape)
    FG chars: 8x8, 2bpp, transparent pen 3
    BG tiles: 16x16, 3bpp, scrolling, priority groups
    Sprites: 16x16, 4bpp, transparent pen 15
  Palette: 256 entries, CPU-written RGBx_444 split bank (0x3800/0x3900)
  PROMs: 2x 256B color lookup tables (NOT used by GnG render — timing/priority only)

ROM mapping (set: gg1-gg17):
  Main CPU (MC6809):
    gg4.bin (16K) -> fixed ROM low (0x4000-0x7FFF in region)
    gg3.bin (32K) -> fixed ROM high (0x8000-0xFFFF in region)
    gg5.bin (32K) -> banked ROM (4 x 8K banks)
    Bank 4 = gg4[0:8K] (first half of gg4)
  Sound CPU (Z80):
    gg2.bin (32K) -> Z80 ROM 0x0000-0x7FFF
  FG Chars (8x8, 2bpp):
    gg1.bin (16K) -> 1024 chars
  BG Tiles (16x16, 3bpp, 3 planes x 32K):
    Plane 0: gg11.bin + gg10.bin (32K)
    Plane 1: gg9.bin  + gg8.bin  (32K)
    Plane 2: gg7.bin  + gg6.bin  (32K)
  Sprites (16x16, 4bpp, 2 halves) — MAME gng ROM layout with 16K gaps:
    First half  (planes 0/1): gg17 + gg16 + gg15 + 16K GAP  (64K)
    Second half (planes 2/3): gg14 + gg13 + gg12 + 16K GAP  (64K)
  PROMs:
    tbp24s10.14k (256B) -> char/tile CLUT (kept for completeness, unused by render)
    63s141.2e    (256B) -> sprite CLUT    (kept for completeness, unused by render)

Path layout (2.8VPE):
  Script:  GALAGONE/romconv_gng.py
  ROM zip: GALAGONE/gng.zip (or Downloads/Desktop)
  Output:  GALAGONE/machines/gng/gng_*.h
"""

import sys, os, struct, zipfile

# Search paths for ROM zip — project root = same dir as this script
SEARCH_PATHS = [
    os.path.dirname(os.path.abspath(__file__)),  # project root sambi
    r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE 2.8VPE\GALAGONE",  # sorgente con gng.zip
    os.path.expanduser('~/Desktop/GALAGHINO V4'),
    os.path.expanduser('~/Desktop'),
    os.path.expanduser('~/Downloads'),
    '.',
]

def find_rom_zip():
    for path in SEARCH_PATHS:
        fp = os.path.join(path, 'gng.zip')
        if os.path.exists(fp):
            return fp
    return None

def read_bit(data, bit_pos):
    """Read a single bit from data array — MAME convention (MSB-first).
    MAME's readbit(): src[bitnum/8] & (0x80 >> (bitnum%8)). Bit 0 of a byte
    is its most-significant bit, so gfx_layout offsets point at MSB-indexed
    positions. Using LSB-first here scrambles every pixel."""
    byte_idx = bit_pos >> 3
    bit_idx = bit_pos & 7
    if byte_idx >= len(data):
        return 0
    return (data[byte_idx] >> (7 - bit_idx)) & 1

# ============================================================================
# Graphics decode (following MAME gfx_layout exactly)
# ============================================================================

def decode_chars(char_data):
    """Decode 8x8 2bpp chars from gg1.bin (16KB = 1024 chars).
    MAME charlayout: planes={4,0}, x={0,1,2,3,8,9,10,11},
    y={0*16..7*16}, step=16*8=128 bits=16 bytes/char"""
    num_chars = len(char_data) * 8 // 128  # = 1024
    xoffs = [0, 1, 2, 3, 8, 9, 10, 11]
    yoffs = [i * 16 for i in range(8)]
    # MAME planes {4,0} are listed MSB-first. Our decoder iterates LSB-first
    # (p=0 sets bit0), so the list must be reversed: bit0=0, bit1=4.
    plane_offs = [0, 4]
    step = 128  # bits per char

    all_chars = []
    for c in range(num_chars):
        pixels = []
        for y in range(8):
            row = []
            for x in range(8):
                pixel = 0
                for p in range(2):
                    bp = c * step + plane_offs[p] + yoffs[y] + xoffs[x]
                    if read_bit(char_data, bp):
                        pixel |= (1 << p)
                row.append(pixel)
            pixels.append(row)
        all_chars.append(pixels)
    return all_chars

def decode_tiles(tile_data):
    """Decode 16x16 3bpp BG tiles.
    MAME tilelayout: planes={FRAC(2,3),FRAC(1,3),FRAC(0,3)},
    x={0..7,128..135}, y={0*8..15*8}, step=32*8=256 bits=32 bytes/tile/plane
    Total region = 96KB, 3 planes x 32KB each, 1024 tiles."""
    region_size = len(tile_data)
    third = region_size // 3
    third_bits = third * 8
    num_tiles = third * 8 // 256  # = 1024

    xoffs = list(range(8)) + [128 + i for i in range(8)]
    yoffs = [i * 8 for i in range(16)]
    # MAME planes {FRAC(2,3), FRAC(1,3), FRAC(0,3)} are MSB-first. Our decoder
    # iterates LSB-first, so reversed: bit0=FRAC(0,3)=0, bit1=FRAC(1,3), bit2=FRAC(2,3).
    plane_offs = [0, 1 * third_bits, 2 * third_bits]
    step = 256  # bits per tile

    all_tiles = []
    for t in range(num_tiles):
        pixels = []
        for y in range(16):
            row = []
            for x in range(16):
                pixel = 0
                for p in range(3):
                    bp = t * step + plane_offs[p] + yoffs[y] + xoffs[x]
                    if read_bit(tile_data, bp):
                        pixel |= (1 << p)
                row.append(pixel)
            pixels.append(row)
        all_tiles.append(pixels)
    return all_tiles

def decode_sprites(sprite_data):
    """Decode 16x16 4bpp sprites (MAME gng.cpp spritelayout verbatim).
       planes = { FRAC(1,2)+4, FRAC(1,2)+0, 4, 0 }  (MSB..LSB)
       x      = { 0,1,2,3, 8+0..8+3, 32*8+0..32*8+3, 33*8+0..33*8+3 }
       y      = { 0*16, 1*16, 2*16, ... 15*16 }     (contiguous!)
       step   = 64*8 = 512 bits / sprite
       region = 128 KB, two halves of 64 KB."""
    region_size = len(sprite_data)
    half = region_size // 2
    half_bits = half * 8

    xoffs = [0, 1, 2, 3, 8, 9, 10, 11, 256, 257, 258, 259, 264, 265, 266, 267]
    yoffs = [i * 16 for i in range(16)]  # contiguous 0..15
    # Our read_bit-based decoder stores plane p into bit p of the pixel,
    # so plane_offs[p] must correspond to MAME's LSB-first plane list,
    # i.e. reversed from the gfx_layout {MSB..LSB} order.
    plane_offs = [0, 4, half_bits, half_bits + 4]
    step = 512  # bits per sprite

    num_sprites = half * 8 // step  # = 1024

    all_sprites = []
    for s in range(num_sprites):
        pixels = []
        for y in range(16):
            row = []
            for x in range(16):
                pixel = 0
                for p in range(4):
                    bp = s * step + plane_offs[p] + yoffs[y] + xoffs[x]
                    if read_bit(sprite_data, bp):
                        pixel |= (1 << p)
                row.append(pixel)
            pixels.append(row)
        all_sprites.append(pixels)
    return all_sprites

# ============================================================================
# Color map generation from PROMs (kept for completeness — NOT used by render
# since GnG palette is indexed directly via base + sub_palette*N + pixel)
# ============================================================================

def build_color_maps(prom1, prom2):
    """Build color lookup tables from PROMs.
    MAME GFXDECODE:
      chars:   color_base=128, 16 groups x 4 colors (2bpp)
      tiles:   color_base=0,   8 groups x 8 colors (3bpp)
      sprites: color_base=320, 4 groups x 16 colors (4bpp)
    """
    char_cmap = []
    for attr in range(16):
        for pixel in range(4):
            pen = 128 + attr * 4 + pixel
            base_color = prom1[pen] & 0x0F
            char_cmap.append(base_color)

    tile_cmap = []
    for attr in range(8):
        for pixel in range(8):
            pen = attr * 8 + pixel
            base_color = prom1[pen] & 0x0F
            tile_cmap.append(base_color)

    sprite_cmap = []
    for attr in range(16):
        for pixel in range(16):
            pen_idx = 64 + attr * 16 + pixel
            if pen_idx < 256:
                base_color = (prom2[pen_idx] & 0x0F) | 0x10
            else:
                base_color = 0x10
            sprite_cmap.append(base_color)

    return char_cmap, tile_cmap, sprite_cmap

# ============================================================================
# Header file generation
# ============================================================================

def write_array_header(filepath, array_name, data, description, per_line=16, progmem=False):
    """Write a C header file with a const array (sambi pattern: dim esplicita,
    NO PROGMEM — gli array `const` vanno automaticamente in .rodata flash su S3)."""
    pm = ' PROGMEM' if progmem else ''
    with open(filepath, 'w') as f:
        f.write(f'// {description}\n')
        f.write(f'// Auto-generated by romconv_gng.py - DO NOT EDIT\n\n')
        f.write(f'#ifndef {array_name.upper()}_H\n')
        f.write(f'#define {array_name.upper()}_H\n\n')
        f.write(f'const unsigned char {array_name}[{len(data)}]{pm} = {{\n')
        for i in range(0, len(data), per_line):
            chunk = data[i:i+per_line]
            line = ', '.join(f'0x{b:02x}' for b in chunk)
            f.write(f'  {line},\n')
        f.write(f'}};\n\n')
        f.write(f'#endif\n')
    print(f'  Generated {filepath} ({len(data)} bytes)')

def write_tilemap_header(filepath, array_name, tiles, tile_w, tile_h, description):
    """Write pre-decoded tilemap/spritemap as 1 byte per pixel."""
    data = []
    for tile in tiles:
        for row in tile:
            for pixel in row:
                data.append(pixel & 0xFF)
    write_array_header(filepath, array_name, data, description)
    return len(data)

# ============================================================================
# Main
# ============================================================================

def main():
    print("=" * 60)
    print("Ghosts'n Goblins ROM Converter for GALAGONE 2.8VPE")
    print("=" * 60)

    rom_zip = find_rom_zip()
    if not rom_zip:
        if len(sys.argv) > 1 and os.path.exists(sys.argv[1]):
            rom_zip = sys.argv[1]
        else:
            print("ERROR: gng.zip not found!")
            print("Place gng.zip in one of:", SEARCH_PATHS)
            sys.exit(1)

    print(f"Using ROM: {rom_zip}")
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'machines', 'gng')
    os.makedirs(out_dir, exist_ok=True)
    print(f"Output: {out_dir}")

    z = zipfile.ZipFile(rom_zip)
    names = {n.lower(): n for n in z.namelist()}

    def load(name, expected_size=None):
        for try_name in [name, names.get(name.lower(), name)]:
            try:
                data = z.read(try_name)
                if expected_size and len(data) != expected_size:
                    print(f"  WARNING: {name} is {len(data)} bytes, expected {expected_size}")
                print(f"  Loaded {name}: {len(data)} bytes")
                return data
            except KeyError:
                continue
        print(f"  ERROR: {name} not found in zip!")
        sys.exit(1)

    print("\nLoading ROMs...")
    gg1 = load('gg1.bin', 16384)
    gg2 = load('gg2.bin', 32768)
    gg3 = load('gg3.bin', 32768)
    gg4 = load('gg4.bin')
    gg5 = load('gg5.bin', 32768)

    gg6  = load('gg6.bin',  16384)
    gg7  = load('gg7.bin',  16384)
    gg8  = load('gg8.bin',  16384)
    gg9  = load('gg9.bin',  16384)
    gg10 = load('gg10.bin', 16384)
    gg11 = load('gg11.bin', 16384)

    gg12 = load('gg12.bin', 16384)
    gg13 = load('gg13.bin', 16384)
    gg14 = load('gg14.bin', 16384)
    gg15 = load('gg15.bin', 16384)
    gg16 = load('gg16.bin', 16384)
    gg17 = load('gg17.bin', 16384)

    prom1 = load('tbp24s10.14k', 256)
    prom2 = load('63s141.2e', 256)

    # ====================================================================
    # Build CPU ROMs
    # ====================================================================
    print("\nBuilding CPU ROMs...")

    # Fixed ROM: CPU 0x6000-0xFFFF (40KB)
    if len(gg4) == 16384:
        fixed_low = gg4[0x2000:0x4000]
    else:
        fixed_low = gg4[0x2000:0x4000]
    fixed_rom = bytes(fixed_low) + bytes(gg3)
    print(f"  Fixed ROM: {len(fixed_rom)} bytes (CPU 0x6000-0xFFFF)")

    reset_vec = (fixed_rom[-1] << 8) | fixed_rom[-2]
    print(f"  Reset vector: 0x{reset_vec:04X}")
    if reset_vec < 0x6000:
        print(f"  WARNING: Reset vector 0x{reset_vec:04X} is outside fixed ROM range!")

    # Banked ROM: 5 banks x 8KB
    if len(gg4) == 16384:
        bank4_data = gg4[0x0000:0x2000]
    else:
        bank4_data = gg4[0x0000:0x2000]
    bank_rom = bytes(gg5) + bytes(bank4_data)
    print(f"  Banked ROM: {len(bank_rom)} bytes (5 banks x 8KB)")

    snd_rom = bytes(gg2)
    print(f"  Sound ROM: {len(snd_rom)} bytes (Z80)")

    # ====================================================================
    # Decode graphics
    # ====================================================================
    print("\nDecoding FG chars (8x8, 2bpp)...")
    chars = decode_chars(gg1)
    print(f"  Decoded {len(chars)} chars")

    print("\nDecoding BG tiles (16x16, 3bpp)...")
    tile_region = bytes(gg11) + bytes(gg10) + bytes(gg9) + bytes(gg8) + bytes(gg7) + bytes(gg6)
    print(f"  Tile region: {len(tile_region)} bytes")
    tiles = decode_tiles(tile_region)
    print(f"  Decoded {len(tiles)} tiles")

    print("\nDecoding sprites (16x16, 4bpp)...")
    gap16k = bytes(16384)
    sprite_first  = bytes(gg17) + bytes(gg16) + bytes(gg15) + gap16k
    sprite_second = bytes(gg14) + bytes(gg13) + bytes(gg12) + gap16k
    sprite_region = sprite_first + sprite_second
    print(f"  Sprite region: {len(sprite_region)} bytes")
    sprites = decode_sprites(sprite_region)
    print(f"  Decoded {len(sprites)} sprites")

    # ====================================================================
    # Build color maps (kept for completeness — unused by render)
    # ====================================================================
    print("\nBuilding color maps from PROMs...")
    char_cmap, tile_cmap, sprite_cmap = build_color_maps(prom1, prom2)

    # ====================================================================
    # Generate header files
    # ====================================================================
    print("\nGenerating header files...")

    write_array_header(os.path.join(out_dir, 'gng_rom.h'),
                      'gng_rom', fixed_rom,
                      'GnG Main CPU fixed ROM (40KB, CPU 0x6000-0xFFFF)')

    write_array_header(os.path.join(out_dir, 'gng_bank_rom.h'),
                      'gng_bank_rom', bank_rom,
                      'GnG Main CPU banked ROM (40KB, 5 banks x 8KB)')

    write_array_header(os.path.join(out_dir, 'gng_snd_rom.h'),
                      'gng_snd_rom', snd_rom,
                      'GnG Sound CPU ROM (32KB, Z80)')

    write_tilemap_header(os.path.join(out_dir, 'gng_charmap.h'),
                         'gng_charmap', chars, 8, 8,
                         'GnG FG chars (1024 x 8x8, 1 byte/pixel, 2bpp values 0-3)')

    write_tilemap_header(os.path.join(out_dir, 'gng_tilemap.h'),
                         'gng_tilemap', tiles, 16, 16,
                         'GnG BG tiles (1024 x 16x16, 1 byte/pixel, 3bpp values 0-7)')

    write_tilemap_header(os.path.join(out_dir, 'gng_spritemap.h'),
                         'gng_spritemap', sprites, 16, 16,
                         'GnG Sprites (1024 x 16x16, 1 byte/pixel, 4bpp values 0-15)')

    # CLUT (unused by render but kept for compatibility — see gng.h #include)
    with open(os.path.join(out_dir, 'gng_cmap.h'), 'w') as f:
        f.write('// GnG Color Lookup Tables (from PROMs).\n')
        f.write('// NOTE: NOT used by the renderer -- GnG indexes the palette\n')
        f.write('// directly via base + sub_palette*N + pixel. Kept here only\n')
        f.write('// because gng.h still #includes this file.\n')
        f.write('// Auto-generated by romconv_gng.py - DO NOT EDIT\n\n')
        f.write('#ifndef GNG_CMAP_H\n')
        f.write('#define GNG_CMAP_H\n\n')

        f.write('// Char CLUT: char_cmap[attr*4 + pixel] -> base_color (0-15)\n')
        f.write('const unsigned char gng_char_cmap[64] = {\n')
        for attr in range(16):
            vals = char_cmap[attr*4:(attr+1)*4]
            f.write(f'  {", ".join(f"0x{v:02x}" for v in vals)},  // attr {attr}\n')
        f.write('};\n\n')

        f.write('// Tile CLUT: tile_cmap[attr*8 + pixel] -> base_color (0-15)\n')
        f.write('const unsigned char gng_tile_cmap[64] = {\n')
        for attr in range(8):
            vals = tile_cmap[attr*8:(attr+1)*8]
            f.write(f'  {", ".join(f"0x{v:02x}" for v in vals)},  // attr {attr}\n')
        f.write('};\n\n')

        f.write('// Sprite CLUT: sprite_cmap[attr*16 + pixel] -> base_color (16-31)\n')
        f.write('const unsigned char gng_sprite_cmap[256] = {\n')
        for attr in range(16):
            vals = sprite_cmap[attr*16:(attr+1)*16]
            f.write(f'  {", ".join(f"0x{v:02x}" for v in vals)},  // attr {attr}\n')
        f.write('};\n\n')

        f.write('#endif\n')
    print(f'  Generated gng_cmap.h')

    print("\n" + "=" * 60)
    print("ROM conversion complete!")
    print(f"Total chars: {len(chars)}, tiles: {len(tiles)}, sprites: {len(sprites)}")
    print("=" * 60)

if __name__ == '__main__':
    main()
