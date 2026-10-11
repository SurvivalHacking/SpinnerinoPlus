#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""Export galaga PROGMEM graphics/audio tables to little-endian .bin files
for SD-card lazy loading. Writes into ../sd_rom_staging/.

Layouts (verified against the .h sources):
  galaga_tilemap          unsigned short [256][8]       ->   4096 B
  galaga_sprites          unsigned long  [4][128][16]   ->  32768 B
  galaga_colormap_tiles   unsigned short [64][4]        ->    512 B
  galaga_colormap_sprites unsigned short [64][4]        ->    512 B
  galaga_wavetable        signed char    [8][32]        ->    256 B
  galaga_sample_boom      unsigned char  [N]            -> variable
  galaga_star_set         struct {u8,u8,u16}[4][63]     ->   1008 B
"""

import os, re, struct

HERE = os.path.dirname(os.path.abspath(__file__))
M = os.path.join(HERE, "..", "machines", "galaga")
OUT = os.path.join(HERE, "..", "sd_rom_staging")

NUM_RE = re.compile(r'0x[0-9A-Fa-f]+|-?\d+')

def parse_all_numbers(path):
    with open(path) as f:
        text = f.read()
    text = re.sub(r'//.*', '', text)
    text = re.sub(r'/\*.*?\*/', '', text, flags=re.DOTALL)
    first = text.index('{')
    last  = text.rindex('}')
    body  = text[first:last+1]
    return [int(m, 0) for m in NUM_RE.findall(body)]

def write_scalar(name, out_name, pack, count=None):
    nums = parse_all_numbers(os.path.join(M, name))
    if count is not None and len(nums) != count:
        raise SystemExit(f"{name}: expected {count} numbers, got {len(nums)}")
    out_path = os.path.join(OUT, out_name)
    with open(out_path, "wb") as f:
        for n in nums:
            f.write(pack(n))
    print(f"  {out_name}: {len(nums)} values, {os.path.getsize(out_path)} bytes")

def main():
    os.makedirs(OUT, exist_ok=True)
    print("Exporting galaga tables to sd_rom_staging/...")
    write_scalar("galaga_tilemap.h",
        "galaga_tilemap.bin",
        lambda n: struct.pack("<H", n & 0xFFFF),
        256 * 8)
    write_scalar("galaga_spritemap.h",
        "galaga_spritemap.bin",
        lambda n: struct.pack("<L", n & 0xFFFFFFFF),
        4 * 128 * 16)
    write_scalar("galaga_cmap_tiles.h",
        "galaga_cmap_tiles.bin",
        lambda n: struct.pack("<H", n & 0xFFFF),
        64 * 4)
    write_scalar("galaga_cmap_sprites.h",
        "galaga_cmap_sprites.bin",
        lambda n: struct.pack("<H", n & 0xFFFF),
        64 * 4)
    write_scalar("galaga_wavetable.h",
        "galaga_wavetable.bin",
        lambda n: struct.pack("<b", n if n < 128 else n - 256),
        8 * 32)
    # sample_boom is a flat uint8 array of unknown length
    boom_nums = parse_all_numbers(os.path.join(M, "galaga_sample_boom.h"))
    out_path = os.path.join(OUT, "galaga_sample_boom.bin")
    with open(out_path, "wb") as f:
        for n in boom_nums:
            f.write(struct.pack("B", n & 0xFF))
    print(f"  galaga_sample_boom.bin: {len(boom_nums)} bytes")
    # star_set is 4 sets × 63 stars × (u8, u8, u16) = 4 × 63 × 4 = 1008 B
    # Parse manually: each entry is { x, y, col }
    with open(os.path.join(M, "galaga_starseed.h")) as f:
        text = f.read()
    text = re.sub(r'//.*', '', text)
    # Find "x, y, col" tuples inside innermost braces
    inner_re = re.compile(r'\{\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*,\s*(0x[0-9A-Fa-f]+|\d+)\s*\}')
    matches = inner_re.findall(text)
    if len(matches) != 4 * 63:
        raise SystemExit(f"starseed: expected {4*63} entries, got {len(matches)}")
    out_path = os.path.join(OUT, "galaga_starseed.bin")
    with open(out_path, "wb") as f:
        for x, y, col in matches:
            f.write(struct.pack("<BBH", int(x, 0), int(y, 0), int(col, 0)))
    print(f"  galaga_starseed.bin: {len(matches)} entries, {os.path.getsize(out_path)} bytes")
    print("Done.")

if __name__ == "__main__":
    main()
