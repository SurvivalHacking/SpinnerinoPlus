#!/usr/bin/env python3
"""
ROM converter for Fantasy (Rock-Ola/SNK 1981, US set "fantasyu")
Generates header files for GALAGINO emulator.

Hardware: same Rock-Ola family as Nibbler (snk6502.cpp).
  Main CPU: MOS 6502 @ ~1.4 MHz
  Audio:    snk6502_sound_device (3 tone voices, waveform ROM) + bomb noise
            + HC55516 CVSD speech (NEW vs Nibbler — stubbed in stage 2a)
  Video:    256x224 visible, ROT270 portrait, 8x8 tile 2bpp
  Palette:  2 x 32B PROM (ic7 = fg, ic6 = bg) — same decode as Nibbler

MAME memory map ("fantasy_map" — Nibbler shares this map):
  0x0000-0x03FF  RAM
  0x0400-0x07FF  Video RAM 2 (FG tile codes)
  0x0800-0x0BFF  Video RAM   (BG tile codes)
  0x0C00-0x0FFF  Color RAM
  0x1000-0x1FFF  Character Generator RAM (FG dynamic tiles)
  0x2000-0x2001  MC6845 CRTC (ignored)
  0x2100-0x2103  sound chip writes (0x2103 = ctrl_reg, see below)
  0x2104-0x2107  inputs / DSW
  0x2200/0x2300  scroll X/Y
  0x2400         HC55516 CVSD speech write (NEW vs Nibbler) - STUBBED
  0x3000-0xBFFF  Program ROM (36KB)
  0xF000-0xFFFF  ROM mirror of 0x8000 (vectors)

ctrl_reg @ $2103 layout (MAME flipscreen_w):
  bit 0-2  = backcolor (selezione pen 0 globale per BG: palette[32+4*backcolor])
  bit 3    = char_bank, INVERTED -> 0=high 256 chars, 1=low 256 chars
  bit 4-6  = music 2 selector
  bit 7    = flipscreen (flip both X and Y world + pixel within tile)

ROM layout (fantasyu.zip):
  CPU ROM (36KB):
    0x3000  ic12.cpu  (4KB)
    0x4000  ic07.cpu  (4KB)
    0x5000  ic08.cpu  (4KB)
    0x6000  ic09.cpu  (4KB)
    0x7000  ic10.cpu  (4KB)
    0x8000  ic14.cpu  (4KB) <-- mirrored at 0xF000 for vectors
    0x9000  ic15.cpu  (4KB)
    0xA000  ic16.cpu  (4KB)
    0xB000  ic17.cpu  (4KB)

  GFX ROM (8KB) - "gfx1" region (512 chars 2bpp):
    0x0000  fs10ic50.bin (4KB)
    0x1000  fs11ic51.bin (4KB)
    Layout: 512 chars, planes 4KB apart, byte = 8 pixels.
    BITPLANE SWAP applicato in fantasy.cpp::blit_bg_tile -- bp0 (LSB)
    legge da offset 0x1000 (fs11) e bp1 (MSB) da offset 0 (fs10).
    Verifica empirica: senza swap mare diventa verde. Effetto collaterale
    aperto: uccelli liv.3 rossi invece di verdi, muri liv.2 mancanti.

  Color PROM (64B):
    0x0000  fantasy.ic7  (32B) fg colors -> palette[0..31]
    0x0020  fantasy.ic6  (32B) bg colors -> palette[32..63]
    Decode rockola_to_rgb565: bits 0-2=R(0x21,0x47,0x97), 3-5=G(stesso), 6-7=B(0x52,0xAD)

  Sound ROM (6KB) - "snk6502" region for waveform tunes:
    0x0000  fs_b_51.bin  (2KB) <- channel 0 base
    0x0800  fs_a_52.bin  (2KB) <- channel 1 base
    0x1000  fs_c_53.bin  (2KB) <- channel 2 base
    Voice base addresses match exactly Nibbler's mapping in fantasy.cpp.
    Audio render in audio.cpp::fantasy_render_buffer; advance counter 41.6Hz
    (counter += 64; threshold 577 a 24kHz output) per matchare MAME
    fantasy_sound_device::device_reset() music_clock = 1/41.6.

  Speech ROM (6KB linear) - HC55516 CVSD samples (still stubbed):
    0x0000  fs_d_7.bin   (2KB)
    0x0800  fs_e_8.bin   (2KB)
    0x1000  fs_f_11.bin  (2KB)
    NOTE: in MAME the speech region is 0x5800 with ROMs at offset 0x4000-0x57FF.
          We emit a flat 6KB blob - speech engine TBD (stub no-op a $2400).

Usage: python romconv_fantasy.py
"""

import zipfile, sys, os

# Locate ROM zip (search common locations)
ZIP_PATHS = [
    "fantasyu.zip",
    "../fantasyu.zip",
    "../romszip/fantasyu.zip",
    os.path.expanduser("~/Downloads/fantasyu.zip"),
    r"C:/Users/Tecnico/Downloads/fantasyu.zip",
    r"C:/Users/Tecnico/Desktop/fantasyu.zip",
]

romzip = None
for p in ZIP_PATHS:
    if os.path.exists(p):
        romzip = p
        break

if romzip is None:
    print("ERROR: fantasyu.zip not found in any of:")
    for p in ZIP_PATHS:
        print(f"  {p}")
    sys.exit(1)

print(f"Using ROM zip: {romzip}")
z = zipfile.ZipFile(romzip)
names = set(z.namelist())

def load(name, optional=False):
    if name not in names:
        if optional:
            print(f"  [skip] {name} (not in zip)")
            return None
        print(f"  ERROR: {name} missing from zip"); sys.exit(1)
    data = z.read(name)
    print(f"  Loaded {name}: {len(data)} bytes")
    return data

OUT_DIR = "machines/fantasy"
os.makedirs(OUT_DIR, exist_ok=True)

def write_header(filename, varname, data, comment=""):
    path = os.path.join(OUT_DIR, filename)
    with open(path, "w") as f:
        f.write(f"// {comment}\n")
        f.write(f"const unsigned char {varname}[] PROGMEM = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n")
    print(f"  Written {path} ({len(data)} bytes)")

# 1. CPU ROM - 36KB linear covering 0x3000-0xBFFF
print("\n=== CPU ROM (0x3000-0xBFFF, 36KB) ===")

rom_12 = load("ic12.cpu")   # 0x3000
rom_07 = load("ic07.cpu")   # 0x4000
rom_08 = load("ic08.cpu")   # 0x5000
rom_09 = load("ic09.cpu")   # 0x6000
rom_10 = load("ic10.cpu")   # 0x7000
rom_14 = load("ic14.cpu")   # 0x8000 - mirrored at 0xF000
rom_15 = load("ic15.cpu")   # 0x9000
rom_16 = load("ic16.cpu")   # 0xA000
rom_17 = load("ic17.cpu")   # 0xB000

cpu_rom = rom_12 + rom_07 + rom_08 + rom_09 + rom_10 + rom_14 + rom_15 + rom_16 + rom_17
assert len(cpu_rom) == 36*1024, f"Expected 36KB, got {len(cpu_rom)}"
print(f"  CPU ROM total: {len(cpu_rom)} bytes (36KB, 0x3000-0xBFFF)")

# 6502 vectors are in ic14 (which mirrors at 0xF000-0xFFFF)
# In our linear buffer, ic14 starts at offset 0x5000 (0x8000-0x3000)
nmi_vec   = rom_14[0xFFA] | (rom_14[0xFFB] << 8)
reset_vec = rom_14[0xFFC] | (rom_14[0xFFD] << 8)
irq_vec   = rom_14[0xFFE] | (rom_14[0xFFF] << 8)
print(f"  NMI   vector: 0x{nmi_vec:04X}")
print(f"  RESET vector: 0x{reset_vec:04X}")
print(f"  IRQ   vector: 0x{irq_vec:04X}")

write_header("fantasy_rom.h", "fantasy_rom", cpu_rom,
             "Fantasy CPU ROM (36KB: 0x3000-0xBFFF, indexed as rom[addr - 0x3000]); "
             "ic14 at offset 0x5000 also mirrors to 0xF000-0xFFFF for 6502 vectors")

# 2. GFX ROM - 8KB tile ROMs (gfx1 region in MAME)
#    fs10 @ offset 0x0000 (4KB), fs11 @ offset 0x1000 (4KB).
#    Layout: 512 chars, 2 bits per pixel, 8x8 each.
#    BITPLANE SWAP: fantasy.cpp::blit_bg_tile reads bp0 (LSB) da offset 0x1000
#    e bp1 (MSB) da offset 0 (verifica empirica: senza swap il mare risulta
#    verde invece che blu - PROM ic6 bank 6 ha pen 1=BLU, pen 2=VERDE).
#    Stesso swap applicato a Nibbler ("matches swapcharlayout" in nibbler.cpp).
print("\n=== GFX ROM (8KB tile data, gfx1 region) ===")

gfx_10 = load("fs10ic50.bin")  # 4KB
gfx_11 = load("fs11ic51.bin")  # 4KB
gfx_rom = gfx_10 + gfx_11
assert len(gfx_rom) == 8192
print(f"  GFX ROM total: {len(gfx_rom)} bytes (8KB)")

write_header("fantasy_gfx.h", "fantasy_gfx", gfx_rom,
             "Fantasy tile ROM (gfx1 region, 8KB: fs10 @ 0x0000 + fs11 @ 0x1000). "
             "512 chars 2bpp, 8x8 each. Render fa bitplane SWAP "
             "(bp0 da offset 0x1000, bp1 da offset 0).")

# 3. PALETTE PROMs - 2x 32B (same decode as Nibbler/Rock-Ola)
print("\n=== PALETTE PROMs ===")

prom_ic7 = load("fantasy.ic7")  # fg
prom_ic6 = load("fantasy.ic6")  # bg
assert len(prom_ic7) == 32 and len(prom_ic6) == 32
palette_raw = prom_ic7 + prom_ic6  # 64 bytes, ic7 first per MAME
print(f"  Palette PROM total: {len(palette_raw)} bytes")

def rockola_to_rgb565(b):
    r  = 0x21 * ((b >> 0) & 1) + 0x47 * ((b >> 1) & 1) + 0x97 * ((b >> 2) & 1)
    g  = 0x21 * ((b >> 3) & 1) + 0x47 * ((b >> 4) & 1) + 0x97 * ((b >> 5) & 1)
    bl = 0x52 * ((b >> 6) & 1) + 0xAD * ((b >> 7) & 1)
    r5 = min(31, (r * 31) // 255)
    g6 = min(63, (g * 63) // 255)
    b5 = min(31, (bl * 31) // 255)
    rgb565 = (r5 << 11) | (g6 << 5) | b5
    return ((rgb565 & 0xFF) << 8) | ((rgb565 >> 8) & 0xFF)

path = os.path.join(OUT_DIR, "fantasy_palette.h")
with open(path, "w") as f:
    f.write("// Fantasy palette: 2x32B PROM + precomputed RGB565 (byte-swapped)\n")
    f.write("// ic7 (foreground) at 0..31, ic6 (background) at 32..63\n\n")
    f.write("const unsigned char fantasy_palette_raw[64] PROGMEM = {\n")
    for i in range(0, 64, 16):
        chunk = palette_raw[i:i+16]
        f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
    f.write("};\n\n")
    f.write("const unsigned short fantasy_palette[64] PROGMEM = {\n")
    for i in range(0, 64, 8):
        vals = []
        for j in range(8):
            if i + j < 64:
                b = palette_raw[i + j]
                vals.append(f"0x{rockola_to_rgb565(b):04X}")
        f.write("  " + ", ".join(vals) + ",\n")
    f.write("};\n")
print(f"  Written {path} (64B raw + 64x2B RGB565)")

# 4. SOUND ROM - 6KB linear (snk6502 region, 3x 2KB)
#    region offsets match exactly Nibbler's voice base addresses:
#    voices[0].base = (Value & 7) << 8        -> 0x0000-0x07FF (fs_b, ch0)
#    voices[1].base = 0x0800 + ((Value&7)<<8) -> 0x0800-0x0FFF (fs_a, ch1)
#    voices[2].base = 0x1000 + ((Value&0x70)<<4) -> 0x1000-0x17FF (fs_c, ch2)
print("\n=== SOUND ROM (6KB snk6502 region) ===")

snd_b = load("fs_b_51.bin")    # 2KB ch0
snd_a = load("fs_a_52.bin")    # 2KB ch1
snd_c = load("fs_c_53.bin")    # 2KB ch2
snd_rom = snd_b + snd_a + snd_c
assert len(snd_rom) == 6144
print(f"  Sound ROM total: {len(snd_rom)} bytes (6KB)")

write_header("fantasy_snd_rom.h", "fantasy_snd_rom", snd_rom,
             "Fantasy sound ROM (snk6502 region, 6KB: fs_b @ 0x0000, fs_a @ 0x0800, fs_c @ 0x1000). "
             "Region offsets match voice base addresses in fantasy.cpp.")

# 5. SPEECH ROM - 6KB HC55516 CVSD samples (stubbed in stage 2a)
print("\n=== SPEECH ROM (6KB CVSD samples) ===")

spc_d = load("fs_d_7.bin")    # 2KB
spc_e = load("fs_e_8.bin")    # 2KB
spc_f = load("fs_f_11.bin")   # 2KB
speech_rom = spc_d + spc_e + spc_f
assert len(speech_rom) == 6144
print(f"  Speech ROM total: {len(speech_rom)} bytes (6KB)")

write_header("fantasy_speech_rom.h", "fantasy_speech_rom", speech_rom,
             "Fantasy HC55516 CVSD speech samples (6KB: fs_d + fs_e + fs_f). "
             "Stage 2a: data emitted but speech engine NOT YET implemented "
             "(writes to $2400 are no-op stub).")

# Dipswitches header (MAME snk6502_generic_joy8way + fantasyu Allow_Continue):
#   bit 0    = Cabinet:        1=Upright (DEFAULT) | 0=Cocktail
#   bit 1-3  = Coinage:        000=1C/1C, 010=2C/1C, 100=1C/3C, ecc.
#   bit 4-5  = Lives:          00=3 (DEFAULT), 01=4, 10=5
#   bit 6    = Coinage Bonus:  0=1C/1C, 1=2C/1C
#   bit 7    = Allow Continue: 0=Yes (fantasyu only), 1=No
# CRITICAL: bit 0 MUST be 1 (Upright) altrimenti il gioco va in cocktail e
# la mongolfiera "vola da sola" (IN1 cocktail rimane a 0).
# Default 0x01 = Upright + 1C/1C + 3 lives + bonus 1C/1C + Continue=Yes.
path = os.path.join(OUT_DIR, "fantasy_dipswitches.h")
with open(path, "w") as f:
    f.write("// Fantasy DIP switches - active HIGH @ $2106 (snk6502_generic_joy8way + fantasyu)\n")
    f.write("// bit 0=Cabinet (1=Upright/0=Cocktail), bit 1-3=Coinage, bit 4-5=Lives,\n")
    f.write("// bit 6=Coinage Bonus, bit 7=Allow_Continue (0=Yes per fantasyu).\n")
    f.write("// Default 0x01: Upright + 1C/1C + 3 lives + Continue=Yes.\n")
    f.write("// CRITICAL: bit 0=1 (Upright) altrimenti la mongolfiera vola da sola.\n")
    f.write("#ifndef FANTASY_DSW\n")
    f.write("#define FANTASY_DSW 0x01\n")
    f.write("#endif\n")
print(f"  Written {path}")

print("\nDone! Generated files in", OUT_DIR + "/:")
print("  fantasy_rom.h          - CPU ROM (36KB, 0x3000-0xBFFF)")
print("  fantasy_gfx.h          - tile ROM (8KB, fs10+fs11, bitplane swap in render)")
print("  fantasy_palette.h      - palette PROMs (64B raw + RGB565 LUT, ic7=fg, ic6=bg)")
print("  fantasy_snd_rom.h      - sound ROM (6KB, fs_b/a/c at offsets 0/0x800/0x1000)")
print("  fantasy_speech_rom.h   - HC55516 speech samples (6KB, stubbed)")
print("  fantasy_dipswitches.h  - DSW defaults (placeholder)")
