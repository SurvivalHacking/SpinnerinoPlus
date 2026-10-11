#!/usr/bin/env python3
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "SPINNERINO", "machines", "popeye",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

"""
romconv_popeye.py - ROM/PROM + logo TFT converter per Popeye (Nintendo TPP2).

Progetto: CONSOLE_QUADRA_NANO (ESP32-S3 Waveshare 1.54", ST7789 240x240).

Input:
  ../../romszip/popeye.zip           (ROM/PROM dump MAME)
  ./popeye_logo_FULL.h               (logo TFT originale 224x96 RLE, backup)

Output (in questa cartella machines/popeye/):
  popeye_rom_main.h     - 4x tpp2-c.7a/7b/7c/7e decrypted (32KB Z80 program)
  popeye_rom_video.h    - 1e+1f+1j+1k (sprite gfx 32KB) + 5n (char gfx 4KB)
  popeye_prom_3a.h      - char color PROM (32B)
  popeye_prom_4a.h      - bg color PROM (32B)
  popeye_prom_5a.h      - sprite color HIGH nibble (256B)
  popeye_prom_5b.h      - sprite color LOW nibble (256B)
  popeye_prom_7j.h      - timing PROM (256B)
  popeye_logo.h         - logo TFT 224x96 ridotto al 80% (179x76 in canvas nero)

Verificati CRC32 MAME-standard:
  tpp2-c.3a e950bea1, tpp2-c.4a 375e1602
  tpp2-c.5a c576afba, tpp2-c.5b c5826883

Usage: python romconv_popeye.py
Requires: Pillow (pip install Pillow) per la generazione logo.
"""

import os
import re
import zipfile
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    Image = None

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]                          # = CONSOLE_QUADRA_NANO/
ZIP  = ROOT / "romszip" / "popeye.zip"
OUT  = HERE

LOGO_FULL = HERE / "popeye_logo_FULL.h"         # backup originale (non modificare)
LOGO_OUT  = HERE / "popeye_logo.h"
LOGO_W, LOGO_H = 224, 96
LOGO_SCALE = 0.80                               # regola standard sambi


# ============================================================
# Helpers ROM
# ============================================================
def bitswap(v, order):
    r = 0
    for b in order:
        r = (r << 1) | ((v >> b) & 1)
    return r


def write_array(path, name, data):
    """Scrive un buffer come C-array PROGMEM unsigned char in path."""
    guard = name.upper() + "_H"
    with open(path, "w", encoding="utf-8") as f:
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"const unsigned char {name}[] PROGMEM = {{\n")
        for i in range(0, len(data), 16):
            row = data[i:i + 16]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in row))
            if i + 16 < len(data):
                f.write(",")
            f.write("\n")
        f.write("};\n")
        f.write(f"const unsigned int {name}_len = {len(data)};\n\n")
        f.write(f"#endif // {guard}\n")


def read_join(z, files):
    data = b""
    for fn in files:
        data += z.read(fn)
    return data


def decrypt_tpp2_main(raw):
    """MAME tpp2_state::decrypt_rom: address bitswap+XOR + data bitswap."""
    out = bytearray(len(raw))
    for i in range(len(raw)):
        src = bitswap(i, [15, 14, 13, 12, 11, 10, 8, 7, 6, 3, 9, 5, 4, 2, 1, 0]) ^ 0x3F
        out[i] = bitswap(raw[src], [3, 4, 2, 5, 1, 6, 0, 7])
    return bytes(out)


# ============================================================
# Helpers logo (decode RLE -> resize 80% -> encode RLE)
# ============================================================
def parse_h_array(path):
    """Estrae la sequenza di unsigned short dal file .h C-array."""
    with open(path, "r") as f:
        text = f.read()
    m = re.search(r"\{([^}]+)\}", text, re.DOTALL)
    if not m:
        raise RuntimeError(f"Nessun array trovato in {path}")
    body = m.group(1)
    tokens = re.findall(r"0x[0-9A-Fa-f]+|\d+", body)
    return [int(t, 16) if t.startswith("0x") else int(t) for t in tokens]


def rle_decode(stream, expected):
    """Prima entry = marker; poi [marker, run-1, color] o pixel singolo."""
    if not stream:
        return []
    marker = stream[0]
    out = []
    i = 1
    while i < len(stream) and len(out) < expected:
        v = stream[i]
        if v == marker and i + 2 < len(stream):
            run = stream[i + 1] + 1
            col = stream[i + 2]
            out.extend([col] * run)
            i += 3
        else:
            out.append(v)
            i += 1
    return out[:expected]


def rle_encode(pixels):
    used = set(pixels)
    marker = next((m for m in range(65536) if m not in used), 1)
    encoded = [marker]
    i = 0
    while i < len(pixels):
        run = 1
        while i + run < len(pixels) and pixels[i + run] == pixels[i] and run < 256:
            run += 1
        if pixels[i] == marker or run >= 3:
            encoded.extend([marker, run - 1, pixels[i]])
            i += run
        else:
            encoded.append(pixels[i])
            i += 1
    return encoded


def rgb565_swap_to_rgb(v):
    """unsigned short byte-swapped RGB565 -> (r,g,b) 8-bit."""
    s = ((v & 0xFF) << 8) | ((v >> 8) & 0xFF)
    r = (s >> 11) & 0x1F
    g = (s >> 5)  & 0x3F
    b = s & 0x1F
    return (r << 3, g << 2, b << 3)


def rgb_to_565_swap(r, g, b):
    r5 = (r >> 3) & 0x1F
    g6 = (g >> 2) & 0x3F
    b5 = (b >> 3) & 0x1F
    val = (r5 << 11) | (g6 << 5) | b5
    return ((val & 0xFF) << 8) | ((val >> 8) & 0xFF)


def regen_logo_scaled():
    """Decode popeye_logo_FULL.h, ridimensiona al 80% e riscrive popeye_logo.h."""
    if Image is None:
        print("  WARNING: Pillow non installato, salto generazione logo.")
        return
    if not LOGO_FULL.exists():
        print(f"  WARNING: {LOGO_FULL} non trovato, salto generazione logo.")
        return

    print(f"=== Logo TFT 100% -> {int(LOGO_SCALE*100)}% ===")
    stream = parse_h_array(LOGO_FULL)
    pixels = rle_decode(stream, LOGO_W * LOGO_H)
    if len(pixels) != LOGO_W * LOGO_H:
        pixels = pixels + [0] * (LOGO_W * LOGO_H - len(pixels))
    print(f"  Decoded: {len(pixels)} pixel")

    img = Image.new("RGB", (LOGO_W, LOGO_H), (0, 0, 0))
    for y in range(LOGO_H):
        for x in range(LOGO_W):
            img.putpixel((x, y), rgb565_swap_to_rgb(pixels[y * LOGO_W + x]))

    nw = max(1, int(LOGO_W * LOGO_SCALE))
    nh = max(1, int(LOGO_H * LOGO_SCALE))
    img_small = img.resize((nw, nh), Image.LANCZOS)
    print(f"  Resized: {LOGO_W}x{LOGO_H} -> {nw}x{nh}")

    canvas = Image.new("RGB", (LOGO_W, LOGO_H), (0, 0, 0))
    canvas.paste(img_small, ((LOGO_W - nw) // 2, (LOGO_H - nh) // 2))

    out_pixels = []
    for y in range(LOGO_H):
        for x in range(LOGO_W):
            r, g, b = canvas.getpixel((x, y))
            out_pixels.append(rgb_to_565_swap(r, g, b))

    encoded = rle_encode(out_pixels)
    print(f"  RLE: raw={len(out_pixels)*2}B -> compressed={len(encoded)*2}B")

    with open(LOGO_OUT, "w") as f:
        f.write("// Popeye - TFT Logo (224x96, RGB565 byte-swapped, RLE)\n")
        f.write(f"// Auto-generated by romconv_popeye.py (scale {int(LOGO_SCALE*100)}%)\n")
        f.write("// Source: popeye_logo_FULL.h (originale, non modificare)\n\n")
        f.write("#ifndef POPEYE_LOGO_H\n#define POPEYE_LOGO_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write("const unsigned short popeye_logo[] PROGMEM = {\n")
        for i in range(0, len(encoded), 12):
            chunk = encoded[i:i + 12]
            f.write("  " + ", ".join(str(v) for v in chunk) + ",\n")
        f.write("};\n\n#endif // POPEYE_LOGO_H\n")
    print(f"  Written {LOGO_OUT}")


# ============================================================
# Main
# ============================================================
if not ZIP.exists():
    raise SystemExit(f"ERROR: popeye.zip non trovato in {ZIP}")

print("=== Popeye ROM/PROM converter (CONSOLE_QUADRA_NANO) ===")
print(f"  Source ZIP: {ZIP}")
print(f"  Output dir: {OUT}\n")

with zipfile.ZipFile(ZIP) as z:
    # --- Z80 program ROM (encrypted TPP2, 32KB totale) ---
    print("  -> popeye_rom_main.h (TPP2 decrypted)")
    main_raw = read_join(z, ["tpp2-c.7a", "tpp2-c.7b", "tpp2-c.7c", "tpp2-c.7e"])
    write_array(OUT / "popeye_rom_main.h", "popeye_rom_main",
                decrypt_tpp2_main(main_raw))

    # --- Video ROM: 4x sprite (8KB ciascuno) + char (4KB) ---
    print("  -> popeye_rom_video.h (sprite gfx + char ROM 5n)")
    write_array(OUT / "popeye_rom_video.h", "popeye_rom_video",
                read_join(z, ["tpp2-v.1e", "tpp2-v.1f",
                              "tpp2-v.1j", "tpp2-v.1k",
                              "tpp2-v.5n"]))

    # --- Color PROM (char 3a + bg 4a) ---
    print("  -> popeye_prom_3a.h (char color)")
    write_array(OUT / "popeye_prom_3a.h", "popeye_prom_3a", z.read("tpp2-c.3a"))
    print("  -> popeye_prom_4a.h (bg color)")
    write_array(OUT / "popeye_prom_4a.h", "popeye_prom_4a", z.read("tpp2-c.4a"))

    # --- Sprite color PROM ---
    # prom = (5b & 0x0F) | ((5a & 0x0F) << 4) = MAME ROM_LOAD_NIB_LOW 5b + NIB_HIGH 5a
    print("  -> popeye_prom_5a.h (sprite color HIGH nibble)")
    write_array(OUT / "popeye_prom_5a.h", "popeye_prom_5a", z.read("tpp2-c.5a"))
    print("  -> popeye_prom_5b.h (sprite color LOW nibble)")
    write_array(OUT / "popeye_prom_5b.h", "popeye_prom_5b", z.read("tpp2-c.5b"))

    # --- Timing PROM (7j) ---
    print("  -> popeye_prom_7j.h (timing)")
    write_array(OUT / "popeye_prom_7j.h", "popeye_prom_7j", z.read("tpp2-v.7j"))

print()
regen_logo_scaled()
print("\nDone!")
