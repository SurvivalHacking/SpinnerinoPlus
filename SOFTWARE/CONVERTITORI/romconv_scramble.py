#!/usr/bin/env python3
"""
romconv_scramble.py - Convert Scramble (Stern) MAME ROM set to galagino .h files.

ROM set: scramble.zip (MAME Scramble - Stern Electronics)
  Main CPU (Z80, 16KB):
    s1.2d  (2KB, $0000-$07FF)
    s2.2e  (2KB, $0800-$0FFF)
    s3.2f  (2KB, $1000-$17FF)
    s4.2h  (2KB, $1800-$1FFF)
    s5.2j  (2KB, $2000-$27FF)
    s6.2l  (2KB, $2800-$2FFF)
    s7.2m  (2KB, $3000-$37FF)
    s8.2p  (2KB, $3800-$3FFF)

  Audio CPU (Z80, 6KB):
    ot1.5c (2KB, $0000-$07FF)  -- same as Galaxian audio
    ot2.5d (2KB, $0800-$0FFF)
    ot3.5e (2KB, $1000-$17FF)

  Graphics (2bpp, tiles 8x8 + sprites 16x16):
    c2.5f  (2KB, bitplane 0)
    c1.5h  (2KB, bitplane 1)

  Color PROM:
    c01s.6e (32 bytes, palette)

  Logo TFT:
    Generated separately from scramble.jpeg using convert_scramble_logo.py

Output files (in machines/scramble/):
  scramble_rom.h       - main CPU ROM
  scramble_rom_audio.h - audio CPU ROM
  scramble_tilemap.h   - 256 tiles, 8x8, 2bpp packed as unsigned short[8]
  scramble_spritemap.h - 64 sprites, 16x16, 2bpp, 4 orientations
  scramble_cmap.h      - 8 palettes x 4 colors, RGB565 byte-swapped
  scramble_dipswitches.h - DIP switch defaults

Usage: python romconv_scramble.py <scramble.zip> <output_dir>
   or: python romconv_scramble.py   (uses default paths)
"""

import sys, os, zipfile, struct

# ─── Default paths ───
DEFAULT_ZIP = os.path.join(os.path.dirname(__file__), "romszip", "scramble.zip")
DEFAULT_OUT = os.path.join(os.path.dirname(__file__), "..", "source", "src", "machines", "scramble")

# ─── MAME ROM names (Scramble - Stern) ───
ROM_MAIN = ["s1.2d", "s2.2e", "s3.2f", "s4.2h", "s5.2j", "s6.2l", "s7.2m", "s8.2p"]
ROM_AUDIO = ["ot1.5c", "ot2.5d", "ot3.5e"]
ROM_GFX = ["c2.5f", "c1.5h"]  # bitplane 0, bitplane 1
ROM_PROM = "c01s.6e"

# Alternative names for common bootleg sets
ALT_MAIN = {
    "s1.2d": ["2d.cpu", "2d", "s1"],
    "s2.2e": ["2e.cpu", "2e", "s2"],
    "s3.2f": ["2f.cpu", "2f", "s3"],
    "s4.2h": ["2h.cpu", "2h", "s4"],
    "s5.2j": ["2j.cpu", "2j", "s5"],
    "s6.2l": ["2l.cpu", "2l", "s6"],
    "s7.2m": ["2m.cpu", "2m", "s7"],
    "s8.2p": ["2p.cpu", "2p", "s8"],
}
ALT_AUDIO = {
    "ot1.5c": ["ot1.5c", "5c", "ot1"],
    "ot2.5d": ["ot2.5d", "5d", "ot2"],
    "ot3.5e": ["ot3.5e", "5e", "ot3"],
}
ALT_GFX = {
    "c2.5f": ["c2.5f", "5f", "c2"],
    "c1.5h": ["c1.5h", "5h", "c1"],
}
ALT_PROM = ["c01s.6e", "c01s", "6e"]


def find_rom(zf, primary, alternatives=None):
    """Find a ROM file in the zip, trying primary name then alternatives."""
    names = zf.namelist()
    names_lower = {n.lower(): n for n in names}

    # Try primary
    if primary.lower() in names_lower:
        return zf.read(names_lower[primary.lower()])

    # Try alternatives
    if alternatives:
        alts = alternatives if isinstance(alternatives, list) else alternatives.get(primary, [])
        for alt in alts:
            if alt.lower() in names_lower:
                return zf.read(names_lower[alt.lower()])

    raise FileNotFoundError(f"ROM '{primary}' not found in ZIP (tried alternatives too)")


def rgb565_swap(r, g, b):
    """Convert RGB to byte-swapped RGB565 for ESP32 SPI display."""
    rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
    return ((rgb >> 8) & 0xFF) | ((rgb & 0xFF) << 8)


def write_hex_array(f, data, per_line=16):
    """Write a byte array as hex values."""
    for i, b in enumerate(data):
        f.write(f"0x{b:02X}")
        if i < len(data) - 1:
            f.write(", ")
        if (i + 1) % per_line == 0:
            f.write("\n  ")


def decode_tile(bp0_byte, bp1_byte):
    """Decode one row of an 8x8 2bpp tile, return 8 pixels packed in unsigned short.
    Galaxian-family: pixels are MSB-first, 2bpp interleaved."""
    row = 0
    for bit in range(8):
        p0 = (bp0_byte >> (7 - bit)) & 1
        p1 = (bp1_byte >> (7 - bit)) & 1
        pixel = p0 | (p1 << 1)
        row |= pixel << (2 * bit)
    return row


def generate_tiles(gfx0, gfx1):
    """Generate 256 tiles, 8x8, 2bpp. Return list of 256 tiles, each is list of 8 unsigned shorts."""
    tiles = []
    for t in range(256):
        tile = []
        for row in range(8):
            bp0 = gfx0[t * 8 + row]
            bp1 = gfx1[t * 8 + row]
            tile.append(decode_tile(bp0, bp1))
        tiles.append(tile)
    return tiles


def flip_tile_h(tile):
    """Flip tile horizontally (reverse bit pairs in each row)."""
    flipped = []
    for row in tile:
        new_row = 0
        for i in range(8):
            pixel = (row >> (2 * i)) & 3
            new_row |= pixel << (2 * (7 - i))
        flipped.append(new_row)
    return flipped


def flip_tile_v(tile):
    """Flip tile vertically (reverse row order)."""
    return list(reversed(tile))


def generate_sprites(gfx0, gfx1):
    """Generate 64 sprites, 16x16, 2bpp, 4 orientations.
    Each sprite is two tiles stacked vertically (top=tile N, bottom=tile N+1).
    Galaxian-family layout: sprites are in the first 64 tile pairs."""
    sprites = [[] for _ in range(4)]  # 4 orientations

    for s in range(64):
        # Sprite = tile[s*2] (top) + tile[s*2+1] (bottom)
        top_tile = []
        bot_tile = []
        for row in range(8):
            bp0_t = gfx0[(s * 2) * 8 + row]
            bp1_t = gfx1[(s * 2) * 8 + row]
            bp0_b = gfx0[(s * 2 + 1) * 8 + row]
            bp1_b = gfx1[(s * 2 + 1) * 8 + row]
            top_tile.append(decode_tile(bp0_t, bp1_t))
            bot_tile.append(decode_tile(bp0_b, bp1_b))

        # Orientation 0: normal (top then bottom, each row is 16px wide = two 8px halves)
        # For 16x16: combine two 8x8 tiles side by side into 16px wide rows
        # Actually in Galaxian, sprite is 16 rows of 16 pixels
        # Layout: tile N = left half, tile N+1 = right half? No...
        # Galaxian: each 16x16 sprite = 4 tiles arranged as:
        #   T0(top-left) T1(bottom-left)   -- wait, it's simpler for Scramble
        # Actually for Scramble (Galaxian family), sprites are 2 tiles stacked:
        #   top 8 rows from tile code, bottom 8 rows from tile code+1
        # Each row is 16 pixels wide, packed as unsigned long (32 bits, 2bpp * 16px)

        # Build 16x16 sprite rows
        sprite_rows = []
        for row in range(8):
            # Top half: 16px row = top_tile row (left 8px) + bot_tile row (right 8px)
            left = top_tile[row]   # 16 bits (8 pixels * 2bpp)
            right = bot_tile[row]  # 16 bits
            combined = left | (right << 16)  # unsigned long 32 bits
            sprite_rows.append(combined)
        for row in range(8):
            left = top_tile[row]
            right = bot_tile[row]
            combined = left | (right << 16)
            sprite_rows.append(combined)

        # Wait - I need to match the existing format exactly.
        # Looking at existing data: each sprite has 16 unsigned longs.
        # Let me just use the same approach as the existing file.
        # The existing sprites[0][0] first entry = { 0x0FC03FF0, ... } which is 32-bit
        # This is tile 0 row 0 (0x0FC0) combined with tile 1 row 0 (0x3FF0)

        sprite_normal = []
        for row in range(8):
            combined = top_tile[row] | (bot_tile[row] << 16)
            sprite_normal.append(combined)
        # Second half (rows 8-15) - but wait, Galaxian sprites are only 8+8=16 rows
        # Actually looking at the data more carefully, each sprite has exactly 16 entries
        # which means it IS 16 rows. So the tile pair gives top 8 + bottom 8 rows.
        # But top_tile and bot_tile each have 8 rows...
        # So rows 0-7 come from one tile pair, rows 8-15 from the next?
        # No - let me re-examine. In Galaxian, sprite code N uses tiles N*4..N*4+3
        # Actually it depends on the exact hardware. Let me just match what exists.

        # The simplest interpretation: 16 rows, each 16 pixels wide
        # Row i (0-7): top_tile[i] as left 8px, bot_tile[i] as right 8px
        # Row i (8-15): this would need tiles s*2+2 and s*2+3, but we only have 256 tiles
        # and 64 sprites * 4 tiles = 256. Yes!

        t0 = s * 4
        t1 = s * 4 + 1
        t2 = s * 4 + 2
        t3 = s * 4 + 3

        tiles_data = []
        for t_idx in [t0, t1, t2, t3]:
            if t_idx < 256:
                tile = []
                for row in range(8):
                    bp0 = gfx0[t_idx * 8 + row]
                    bp1 = gfx1[t_idx * 8 + row]
                    tile.append(decode_tile(bp0, bp1))
                tiles_data.append(tile)
            else:
                tiles_data.append([0] * 8)

        # Galaxian sprite layout: 16 rows x 16 cols
        # Top 8 rows: left=tile0, right=tile1
        # Bottom 8 rows: left=tile2, right=tile3
        sprite_normal = []
        for row in range(8):
            combined = tiles_data[0][row] | (tiles_data[1][row] << 16)
            sprite_normal.append(combined)
        for row in range(8):
            combined = tiles_data[2][row] | (tiles_data[3][row] << 16)
            sprite_normal.append(combined)

        # Generate 4 orientations
        # Orientation 0: normal
        sprites[0].append(sprite_normal)

        # Orientation 1: flip X
        sprite_fx = []
        for row_val in sprite_normal:
            new_val = 0
            for px in range(16):
                pixel = (row_val >> (2 * px)) & 3
                new_val |= pixel << (2 * (15 - px))
            sprite_fx.append(new_val)
        sprites[1].append(sprite_fx)

        # Orientation 2: flip Y
        sprites[2].append(list(reversed(sprite_normal)))

        # Orientation 3: flip X+Y
        sprites[3].append(list(reversed(sprite_fx)))

    return sprites


def generate_colormap(prom_data):
    """Generate 8 palettes x 4 colors from Scramble color PROM.
    PROM is 32 bytes: 8 palettes * 4 entries.
    Each byte: bit 0-2 = RGB (Galaxian-style: bit0=B1, bit1=B0, bit2=G2, bit3=G1, bit4=G0, bit5=R2, bit6=R1, bit7=R0...
    Actually Galaxian PROM format: bits 7-5=R, bits 4-2=G, bits 1-0=B (2 bits blue, 3 bits green, 3 bits red)
    """
    palettes = []

    for pal in range(8):
        colors = []
        for entry in range(4):
            byte = prom_data[pal * 4 + entry]

            # Galaxian/Scramble color PROM decoding
            # Bit 0: Blue bit 0  (weight 0x47)
            # Bit 1: Blue bit 1  (weight 0x97)
            # Bit 2: Green bit 0 (weight 0x21)
            # Bit 3: Green bit 1 (weight 0x47)
            # Bit 4: Green bit 2 (weight 0x97)
            # Bit 5: Red bit 0   (weight 0x21)
            # Bit 6: Red bit 1   (weight 0x47)
            # Bit 7: Red bit 2   (weight 0x97)
            r_weights = [0x21, 0x47, 0x97]
            g_weights = [0x21, 0x47, 0x97]
            b_weights = [0x47, 0x97]

            r = 0
            for i in range(3):
                if byte & (1 << (i + 5)):
                    r += r_weights[i]
            g = 0
            for i in range(3):
                if byte & (1 << (i + 2)):
                    g += g_weights[i]
            b = 0
            for i in range(2):
                if byte & (1 << i):
                    b += b_weights[i]

            if r > 255: r = 255
            if g > 255: g = 255
            if b > 255: b = 255

            colors.append(rgb565_swap(r, g, b))
        palettes.append(colors)

    return palettes


def main():
    if len(sys.argv) >= 3:
        zip_path = sys.argv[1]
        out_dir = sys.argv[2]
    elif len(sys.argv) == 2:
        zip_path = sys.argv[1]
        out_dir = DEFAULT_OUT
    else:
        zip_path = DEFAULT_ZIP
        out_dir = DEFAULT_OUT

    if not os.path.exists(zip_path):
        print(f"ERROR: ZIP not found: {zip_path}")
        print(f"Place scramble.zip in: {os.path.dirname(zip_path)}")
        sys.exit(1)

    os.makedirs(out_dir, exist_ok=True)

    print(f"Reading: {zip_path}")
    print(f"Output:  {out_dir}")

    with zipfile.ZipFile(zip_path) as zf:
        print(f"Files in ZIP: {zf.namelist()}")

        # ── Main CPU ROM (16KB = 8 x 2KB) ──
        main_rom = bytearray()
        for rom_name in ROM_MAIN:
            data = find_rom(zf, rom_name, ALT_MAIN)
            main_rom.extend(data)
            print(f"  Main: {rom_name} ({len(data)} bytes)")

        # ── Audio CPU ROM (6KB = 3 x 2KB) ──
        audio_rom = bytearray()
        for rom_name in ROM_AUDIO:
            data = find_rom(zf, rom_name, ALT_AUDIO)
            audio_rom.extend(data)
            print(f"  Audio: {rom_name} ({len(data)} bytes)")

        # ── Graphics ROMs (2 x 2KB) ──
        gfx = []
        for rom_name in ROM_GFX:
            data = find_rom(zf, rom_name, ALT_GFX)
            gfx.append(data)
            print(f"  GFX: {rom_name} ({len(data)} bytes)")

        # ── Color PROM ──
        prom = find_rom(zf, ROM_PROM, ALT_PROM)
        print(f"  PROM: {ROM_PROM} ({len(prom)} bytes)")

    # ── Generate scramble_rom_main.h ──
    path = os.path.join(out_dir, "scramble_rom_main.h")
    with open(path, "w") as f:
        f.write(f"// Scramble main CPU ROM ({len(main_rom)} bytes)\n")
        f.write("const unsigned char scramble_rom_main[] PROGMEM = {\n  ")
        write_hex_array(f, main_rom)
        f.write("\n};\n")
    print(f"Written: {path}")

    # ── Generate scramble_rom_audio.h ──
    path = os.path.join(out_dir, "scramble_rom_audio.h")
    with open(path, "w") as f:
        f.write(f"// Scramble sound CPU ROM ({len(audio_rom)} bytes)\n")
        f.write("const unsigned char scramble_rom_audio[] PROGMEM = {\n  ")
        write_hex_array(f, audio_rom)
        f.write("\n};\n")
    print(f"Written: {path}")

    # ── Generate scramble_tilemap.h ──
    # Tiles use only the first 2KB of GFX data (256 tiles * 8 bytes per bitplane)
    # In Galaxian family, tiles and sprites share the same GFX ROMs
    tiles = generate_tiles(gfx[0], gfx[1])
    path = os.path.join(out_dir, "scramble_tilemap.h")
    with open(path, "w") as f:
        f.write("// scramble_tilemap: 256 tiles, 8x8, 2bpp\n")
        f.write("const unsigned short scramble_tilemap[][8] PROGMEM = {\n")
        for t, tile in enumerate(tiles):
            vals = ", ".join(f"0x{v:04X}" for v in tile)
            comma = "," if t < 255 else ""
            f.write(f"  {{ {vals} }}{comma}\n")
        f.write("};\n")
    print(f"Written: {path}")

    # ── Generate scramble_spritemap.h ──
    sprite_data = generate_sprites(gfx[0], gfx[1])
    path = os.path.join(out_dir, "scramble_spritemap.h")
    with open(path, "w") as f:
        f.write("// scramble_spritemap: 64 sprites, 16x16, 2bpp, 4 orientations\n")
        f.write("const unsigned long scramble_spritemap[4][64][16] PROGMEM = {\n")
        for ori in range(4):
            f.write(f"  {{ // orientation {ori}\n")
            for s in range(64):
                vals = ", ".join(f"0x{v:08X}" for v in sprite_data[ori][s])
                comma = "," if s < 63 else ""
                f.write(f"    {{ {vals} }}{comma}\n")
            comma = "," if ori < 3 else ""
            f.write(f"  }}{comma}\n")
        f.write("};\n")
    print(f"Written: {path}")

    # ── Generate scramble_cmap.h ──
    palettes = generate_colormap(prom)
    path = os.path.join(out_dir, "scramble_cmap.h")
    with open(path, "w") as f:
        f.write("// 8 palettes x 4 colors, RGB565\n")
        f.write("const unsigned short scramble_colormap[][4] PROGMEM = {\n")
        for p, pal in enumerate(palettes):
            vals = ", ".join(f"0x{v:04X}" for v in pal)
            comma = "," if p < 7 else ""
            f.write(f"  {{ {vals} }}{comma}  // palette {p}\n")
        f.write("};\n")
    print(f"Written: {path}")

    # ── Generate scramble_dipswitches.h ──
    path = os.path.join(out_dir, "scramble_dipswitches.h")
    with open(path, "w") as f:
        f.write("#ifndef SCRAMBLE_DIPSWITCHES_H\n")
        f.write("#define SCRAMBLE_DIPSWITCHES_H\n\n")
        f.write("// Defaults: lives=3 (bits 0-1 = 00), all gameplay inputs idle-high.\n")
        f.write("#define SCRAMBLE_DIP_IN1 0xFC\n")
        f.write("// Coinage/cabinet defaults = 0, gameplay inputs idle-high.\n")
        f.write("#define SCRAMBLE_DIP_IN2 0xF1\n\n")
        f.write("#endif\n")
    print(f"Written: {path}")

    print("\nDone! All Scramble ROM data generated.")
    print("NOTE: scramble_logo.h is generated separately from scramble.jpeg")


if __name__ == "__main__":
    main()
