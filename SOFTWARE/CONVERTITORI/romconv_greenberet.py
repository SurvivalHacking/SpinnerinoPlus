#!/usr/bin/env python3
"""
ROM converter for Green Beret (Konami 1985)
Generates header files for GALAGINO ESP32 emulator.

Hardware:
  Main CPU: Z80 @ 18.432MHz/6 = 3.072 MHz
  Sound: SN76489AN @ 18.432MHz/12 = 1.536 MHz
  Video: 240x224 visible, 64x32 tilemap with per-row scroll, ROT0 (landscape!)
  Display: ROT0 landscape, CW software rotation + mirror_x (like Gyruss)
  Tile pixel CW: game(c,r) -> frame[c*224 + pcol*8 + (7-r)]
  tilemap_row = 29-pcol, base_game_col = row-3, rows 3-32
  mirror_x=true in src.ino swaps pixels L-R in framebuffer (no MADCTL change)

MAME ROM regions (VERIFIED from gberet.cpp ROM_START):
  "maincpu" 0xC000:  577l03.10c(16KB@0x0000) + 577l02.8c(16KB@0x4000) + 577l01.7c(16KB@0x8000)
  "tiles"   0x4000:  577l07.3f (16KB) → 512 tiles, gfx_8x8x4_packed_msb
  "sprites" 0x10000: 577l06.5e(16KB@0) + 577l05.4e(16KB@0x4000) + 577l08.4f(16KB@0x8000) + 577l04.3e(16KB@0xC000)
                     → 512 sprites (16x16), gfx_8x8x4_row_2x2_group_packed_msb
  "proms"   0x0220:  577h09.2f(32B@0x0000) + 577h11.6f(256B@0x0020) + 577h10.5f(256B@0x0120)
                     577h11 = TILE clut (|0x10 → palette 16-31)
                     577h10 = SPRITE clut (palette 0-15)

GFX format: gfx_8x8x4_packed_msb
  planes={0,1,2,3}, xoffset={28,24,20,16,12,8,4,0} (MSB first!), yoffset={0,32,64,...}
  Pixel 0 = HIGH nibble of byte 3, Pixel 7 = LOW nibble of byte 0

Sprite format: gfx_8x8x4_row_2x2_group_packed_msb
  16x16 = 2x2 group of 8x8 tiles (each MSB packed)
  Block 0(top-left)=bytes 0-31, Block 1(top-right)=32-63,
  Block 2(bottom-left)=64-95, Block 3(bottom-right)=96-127

MAME memory map (Z80, from gberet_state::prg_map):
  0x0000-0xBFFF: ROM (48KB)
  0xC000-0xC7FF: colorram (tile attributes)
  0xC800-0xCFFF: videoram (tile codes)
  0xD000-0xD0FF: spriteram2
  0xD100-0xD1FF: spriteram
  0xD200-0xDFFF: RAM (3.5KB work RAM)
  0xE000-0xE03F: k005849 scroll (64B: 0-31=low, 32-63=high bit)
  0xE040-0xE047: k005849 ctrl (bit0=NMI_EN, bit1=IRQ_EN, bit3=flip)
  0xF200: DSW2/soundlatch, 0xF400: DSW3/sound_w
  0xF602: P1, 0xF601: P2, 0xF603: SYSTEM

Interrupts (k005849):
  NMI: timer ~8x/frame (ctrl[4] bit0) - ONLY fire when IFF1 set
  IRQ: VBLANK 1x/frame (ctrl[4] bit1) - Z80 IM1 RST38
  FIRQ: VBLANK every OTHER frame (ctrl[4] bit2) - ALSO RST38!
  Green Beret uses ctrl[4]=0x05: NMI+FIRQ enabled, NOT IRQ!
  POST RAM test writes 4 patterns (00,55,AA,FF) and loops forever on mismatch.
  FIX: game_started delayed until ctrl[4] enables interrupts (after POST passes).
  CPU speed: normal INST_PER_FRAME (NOT doubled - breaks NMI timing).

Palette: Blue = 0x52*bit6 + 0xAD*bit7 (MAME verified)
"""

import zipfile, sys, os

ZIP_PATHS = [
    "gberet.zip",
    "../romszip/gberet.zip",
    os.path.expanduser("~/Downloads/gberet.zip"),
    r"C:/Users/Paolo/Desktop/gberet.zip",
    r"C:/Users/Tecnico/Downloads/gberet.zip",
    r"C:/Users/Tecnico/Desktop/gberet.zip",
]

romzip = None
for p in ZIP_PATHS:
    if os.path.exists(p):
        romzip = p
        break

if romzip is None:
    print("ERROR: gberet.zip not found"); sys.exit(1)

print(f"Using ROM zip: {romzip}")
z = zipfile.ZipFile(romzip)

def load(name):
    data = z.read(name)
    print(f"  Loaded {name}: {len(data)} bytes")
    return data

OUT_DIR = "machines/greenberet"
os.makedirs(OUT_DIR, exist_ok=True)

def write_header(filename, content):
    path = os.path.join(OUT_DIR, filename)
    with open(path, "w") as f:
        f.write(content)
    print(f"  Written {path}")

def write_rom_header(filename, varname, data, comment=""):
    path = os.path.join(OUT_DIR, filename)
    with open(path, "w") as f:
        f.write(f"// {comment}\n")
        f.write(f"const unsigned char {varname}[] PROGMEM = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n")
    print(f"  Written {path} ({len(data)} bytes)")


# ══════════════════════════════════════════════════════════════════
# 1. CPU ROM — 48KB
# ══════════════════════════════════════════════════════════════════
print("\n=== CPU ROM (0x0000-0xBFFF) ===")
cpu_rom = load("577l03.10c") + load("577l02.8c") + load("577l01.7c")
assert len(cpu_rom) == 49152
write_rom_header("greenberet_rom.h", "greenberet_rom", cpu_rom,
                 "Green Beret CPU ROM (48KB: 0x0000-0xBFFF)")


# ══════════════════════════════════════════════════════════════════
# 2. COLOR PROMs
# ══════════════════════════════════════════════════════════════════
print("\n=== Color PROMs ===")
prom_palette = load("577h09.2f")   # 32B palette
prom_clut_1  = load("577h11.6f")   # 256B → TILES (first in PROM region after palette)
prom_clut_2  = load("577h10.5f")   # 256B → SPRITES (second in PROM region)

palette_rgb = []
for i in range(32):
    b = prom_palette[i]
    r = 0x21 * ((b >> 0) & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1)
    g = 0x21 * ((b >> 3) & 1) + 0x47 * ((b >> 4) & 1) + 0x97 * ((b >> 5) & 1)
    bl = 0x52 * ((b >> 6) & 1) + 0xAD * ((b >> 7) & 1)  # MAME verified
    palette_rgb.append((r, g, bl))

def rgb_to_565_swap(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF)

palette_565 = [rgb_to_565_swap(r, g, b) for r, g, b in palette_rgb]

# TILE colormap: 577h11 with |0x10 (palette 16-31) — MAME verified
tile_cmap = []
for group in range(16):
    colors = []
    for pixel in range(16):
        idx = (prom_clut_1[group * 16 + pixel] & 0x0F) | 0x10
        colors.append(palette_565[idx])
    tile_cmap.append(colors)

# SPRITE colormap: 577h10 (palette 0-15), pixel 0 = transparent
spr_cmap = []
for group in range(16):
    colors = []
    for pixel in range(16):
        idx = prom_clut_2[group * 16 + pixel] & 0x0F
        c = palette_565[idx] if pixel != 0 else 0x0000
        colors.append(c)
    spr_cmap.append(colors)

cmap_lines = ["// Green Beret colormaps (MAME verified)",
    "// 577h11=TILE clut (|0x10, palette 16-31), 577h10=SPRITE clut (palette 0-15)",
    "// Blue: 0x52*bit6 + 0xAD*bit7\n",
    "const unsigned short greenberet_tile_colormap[][16] PROGMEM = {"]
for g in range(16):
    cmap_lines.append("  {" + ", ".join(f"0x{c:04x}" for c in tile_cmap[g]) + "},")
cmap_lines.append("};\n\nconst unsigned short greenberet_sprite_colormap[][16] PROGMEM = {")
for g in range(16):
    cmap_lines.append("  {" + ", ".join(f"0x{c:04x}" for c in spr_cmap[g]) + "},")
cmap_lines.append("};")
write_header("greenberet_cmap.h", "\n".join(cmap_lines))


# ══════════════════════════════════════════════════════════════════
# 3. TILES — ONLY 577l07.3f (16KB = 512 tiles), MSB packed
# ══════════════════════════════════════════════════════════════════
print("\n=== Tile GFX (ONLY 577l07, MSB packed) ===")
tile_rom = load("577l07.3f")
NTILES = len(tile_rom) // 32
print(f"  {NTILES} tiles (8x8, 4bpp, packed_msb)")

# gfx_8x8x4_packed_msb: pixel 0 = high nibble of byte 3, pixel 7 = low nibble of byte 0
# We store in our format: unsigned long with pixel 0 at bits 0-3 (LSB)
# So we need to REVERSE the nibble order from ROM

tile_data = []
for t in range(NTILES):
    rows = []
    for r in range(8):
        off = t * 32 + r * 4
        # Swap nibbles within each byte: pixel pairs 0<->1, 2<->3, 4<->5, 6<->7
        b0 = ((tile_rom[off]   >> 4) | (tile_rom[off]   << 4)) & 0xFF
        b1 = ((tile_rom[off+1] >> 4) | (tile_rom[off+1] << 4)) & 0xFF
        b2 = ((tile_rom[off+2] >> 4) | (tile_rom[off+2] << 4)) & 0xFF
        b3 = ((tile_rom[off+3] >> 4) | (tile_rom[off+3] << 4)) & 0xFF
        rows.append(b0 | (b1 << 8) | (b2 << 16) | (b3 << 24))
    tile_data.append(rows)

lines = [f"// Green Beret tiles: {NTILES} tiles, 8x8, 4bpp, packed_msb (MAME verified)",
    f"// Source: ONLY 577l07.3f (16KB). 577l08 is SPRITE ROM!\n",
    f"#define GB_NTILES {NTILES}\n",
    f"const unsigned long greenberet_tilemap[{NTILES}][8] PROGMEM = {{"]
for t in range(NTILES):
    lines.append("  {" + ", ".join(f"0x{v:08X}" for v in tile_data[t]) + "},")
lines.append("};")
write_header("greenberet_tilemap.h", "\n".join(lines))


# ══════════════════════════════════════════════════════════════════
# 4. SPRITES — 4 ROMs (577l06+577l05+577l08+577l04), 2x2 group MSB
# ══════════════════════════════════════════════════════════════════
print("\n=== Sprite GFX (4 ROMs, 2x2 group MSB) ===")
spr_rom = load("577l06.5e") + load("577l05.4e") + load("577l08.4f") + load("577l04.3e")
print(f"  Sprite ROM total: {len(spr_rom)} bytes")

NSPRITES = len(spr_rom) // 128
print(f"  {NSPRITES} sprites (16x16, 4bpp, 2x2_group_packed_msb)")

def decode_8x8_row(rom, block_offset, row):
    """Decode one row of an 8x8 packed block, return 8 pixels as list.
    packed_msb: swap nibbles within each byte (pixel pairs 0<->1, 2<->3, etc.)"""
    off = block_offset + row * 4
    b0 = ((rom[off]   >> 4) | (rom[off]   << 4)) & 0xFF
    b1 = ((rom[off+1] >> 4) | (rom[off+1] << 4)) & 0xFF
    b2 = ((rom[off+2] >> 4) | (rom[off+2] << 4)) & 0xFF
    b3 = ((rom[off+3] >> 4) | (rom[off+3] << 4)) & 0xFF
    val = b0 | (b1 << 8) | (b2 << 16) | (b3 << 24)
    return [(val >> (px * 4)) & 0xF for px in range(8)]

def pack_8_pixels_lsb(pixels):
    """Pack 8 pixel values into unsigned long, pixel 0 at bits 0-3"""
    out = 0
    for i, p in enumerate(pixels):
        out |= (p & 0xF) << (i * 4)
    return out

def flip_8_pixels(pixels):
    """Reverse order of 8 pixels"""
    return list(reversed(pixels))

sprite_data = [[], [], [], []]  # 4 flip variants

for s in range(NSPRITES):
    base = s * 128
    # 2x2 group: block 0=top-left(0-31), 1=top-right(32-63), 2=bot-left(64-95), 3=bot-right(96-127)

    # Build 16 rows of 16 pixels each
    rows_normal = []  # list of (lo_pixels[8], hi_pixels[8]) per row
    for r in range(16):
        if r < 8:
            lo_px = decode_8x8_row(spr_rom, base + 0,  r)     # block 0
            hi_px = decode_8x8_row(spr_rom, base + 32, r)     # block 1
        else:
            lo_px = decode_8x8_row(spr_rom, base + 64, r - 8) # block 2
            hi_px = decode_8x8_row(spr_rom, base + 96, r - 8) # block 3
        rows_normal.append((lo_px, hi_px))

    # Variant 0: no flip
    v = []
    for lo_px, hi_px in rows_normal:
        v.extend([pack_8_pixels_lsb(lo_px), pack_8_pixels_lsb(hi_px)])
    sprite_data[0].append(v)

    # Variant 1: flipX
    v = []
    for lo_px, hi_px in rows_normal:
        v.extend([pack_8_pixels_lsb(flip_8_pixels(hi_px)), pack_8_pixels_lsb(flip_8_pixels(lo_px))])
    sprite_data[1].append(v)

    # Variant 2: flipY
    v = []
    for lo_px, hi_px in reversed(rows_normal):
        v.extend([pack_8_pixels_lsb(lo_px), pack_8_pixels_lsb(hi_px)])
    sprite_data[2].append(v)

    # Variant 3: flipXY
    v = []
    for lo_px, hi_px in reversed(rows_normal):
        v.extend([pack_8_pixels_lsb(flip_8_pixels(hi_px)), pack_8_pixels_lsb(flip_8_pixels(lo_px))])
    sprite_data[3].append(v)

lines = [f"// Green Beret sprites: {NSPRITES} sprites, 16x16, 4bpp",
    "// Source: 577l06+577l05+577l08+577l04 (64KB), 2x2_group_packed_msb",
    "// [variant][sprite][row*2+half]\n",
    f"#define GB_NSPRITES {NSPRITES}\n",
    f"const unsigned long greenberet_sprites[4][{NSPRITES}][32] PROGMEM = {{"]
for v in range(4):
    vn = ["normal", "flipX", "flipY", "flipXY"][v]
    lines.append(f"  {{ // {vn}")
    for s in range(NSPRITES):
        lines.append("    {" + ", ".join(f"0x{x:08X}" for x in sprite_data[v][s]) + "},")
    lines.append("  },")
lines.append("};")
write_header("greenberet_spritemap.h", "\n".join(lines))


# ══════════════════════════════════════════════════════════════════
# 5. DIP SWITCHES
# ══════════════════════════════════════════════════════════════════
print("\n=== DIP Switches ===")
write_header("greenberet_dipswitches.h", """// Green Beret DIP Switch defaults
// CLUT: 577h11=tiles(|0x10), 577h10=sprites (MAME PROM order verified)
#define GREENBERET_DSW1  0xFF
#define GREENBERET_DSW2  0x4A   // 3 lives, normal difficulty
#define GREENBERET_DSW3  0xFF   // no flip
""")


# ══════════════════════════════════════════════════════════════════
# 6. TFT LOGO placeholder
# ══════════════════════════════════════════════════════════════════
print("\n=== TFT Logo placeholder ===")
BG = 0x8001
pixels = [BG] * (224 * 96)
used = set(pixels)
m = 0
while m in used: m += 1
enc = [m]
i = 0
while i < len(pixels):
    run = 1
    while i+run < len(pixels) and pixels[i+run] == pixels[i] and run < 256: run += 1
    if pixels[i] == m or run >= 3:
        enc.extend([m, run-1, pixels[i]]); i += run
    else:
        enc.append(pixels[i]); i += 1
lines = ["// Green Beret TFT Logo placeholder\nconst unsigned short greenberet_logo[] PROGMEM = {"]
for i in range(0, len(enc), 14):
    lines.append("  " + ", ".join(f"0x{v:04x}" for v in enc[i:i+14]) + ",")
lines.append("};")
write_header("greenberet_logo.h", "\n".join(lines))

print(f"\n=== DONE === Generated {NTILES} tiles, {NSPRITES} sprites")
