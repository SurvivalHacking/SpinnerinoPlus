#!/usr/bin/env python3
"""
bin2h_pbaction.py — converte i .bin di Pinball Action (gia' estratti da
romconv_pbaction.py nel P4) in 5 file .h PROGMEM per il sambi 3.5".

Sorgenti (.bin):
  P4 NEW_multi/sd_rom_staging/pbaction_main.bin    (40 KB)
  P4 NEW_multi/sd_rom_staging/pbaction_audio.bin   ( 8 KB)
  P4 NEW_multi/sd_rom_staging/pbaction_fg.bin      (24 KB)
  P4 NEW_multi/sd_rom_staging/pbaction_bg.bin      (64 KB)
  P4 NEW_multi/sd_rom_staging/pbaction_sprites.bin (24 KB)

Output (machines/pbaction/):
  pbaction_rom_main.h    -> const unsigned char pbaction_rom_main[40960]
  pbaction_rom_audio.h   -> const unsigned char pbaction_rom_audio[8192]
  pbaction_rom_fg.h      -> const unsigned char pbaction_rom_fg[24576]
  pbaction_rom_bg.h      -> const unsigned char pbaction_rom_bg[65536]
  pbaction_rom_sprites.h -> const unsigned char pbaction_rom_sprites[24576]

Tutti PROGMEM. Letti via pgm_read_byte nel porting sambi.

Usage: python bin2h_pbaction.py
"""

import os, sys

P4_STAGING = r"C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE PAOLO ULTIMO\GALAGONE_4.3_P4_NEW_multi\sd_rom_staging"
HERE       = os.path.dirname(os.path.abspath(__file__))
OUT_DIR    = os.path.join(HERE, "machines", "pbaction")
os.makedirs(OUT_DIR, exist_ok=True)

# (basename, expected_size, c_array_name)
ROMS = [
    ("pbaction_main.bin",    40960, "pbaction_rom_main"),
    ("pbaction_audio.bin",    8192, "pbaction_rom_audio"),
    ("pbaction_fg.bin",      24576, "pbaction_rom_fg"),
    ("pbaction_bg.bin",      65536, "pbaction_rom_bg"),
    ("pbaction_sprites.bin", 24576, "pbaction_rom_sprites"),
]

for fname, expected, arr_name in ROMS:
    src = os.path.join(P4_STAGING, fname)
    if not os.path.isfile(src):
        print(f"ERRORE: {src} non trovato")
        sys.exit(1)
    with open(src, "rb") as f:
        data = f.read()
    if len(data) != expected:
        print(f"ERRORE: {fname} size {len(data)} != atteso {expected}")
        sys.exit(1)

    base = arr_name[len("pbaction_rom_"):]   # main / audio / fg / bg / sprites
    out_h = os.path.join(OUT_DIR, f"pbaction_rom_{base}.h")
    guard = f"PBACTION_ROM_{base.upper()}_H"
    print(f"  {fname:25s} -> {out_h}  ({len(data)} byte)")
    with open(out_h, "w") as f:
        f.write(f"// Auto-generated from {fname}\n")
        f.write(f"// {len(data)} byte PROGMEM\n\n")
        f.write(f"#ifndef {guard}\n#define {guard}\n\n")
        f.write("#include <Arduino.h>\n\n")
        f.write(f"const unsigned char {arr_name}[{len(data)}] PROGMEM = {{\n")
        # 16 byte per riga
        for j in range(0, len(data), 16):
            chunk = data[j:j+16]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n\n")
        f.write(f"#endif // {guard}\n")

print("\nDone! Tutti i .h PROGMEM generati in machines/pbaction/")
