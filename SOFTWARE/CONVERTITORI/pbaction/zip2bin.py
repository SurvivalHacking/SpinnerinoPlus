#!/usr/bin/env python3
"""
romconv_pbaction.py — pbaction.zip (Tehkan/Tecmo Pinball Action 1985) -> .bin per SD.

5 region MAME concatenate:
  pbaction_main.bin    (40 KB) = b-p7 + b-n7 + b-l7        (Z80 maincpu)
  pbaction_audio.bin   ( 8 KB) = a-e3                       (Z80 audiocpu)
  pbaction_fg.bin      (24 KB) = a-s6 + a-s7 + a-s8         (foreground chars)
  pbaction_bg.bin      (64 KB) = a-j5 + a-j6 + a-j7 + a-j8  (background chars)
  pbaction_sprites.bin (24 KB) = b-c7 + b-d7 + b-f7         (sprite gfx)
"""
import os, sys, zipfile, zlib

REGIONS = {
    "pbaction_main.bin": [
        ("b-p7.bin", 0x8D6DCAAE, 0x4000),
        ("b-n7.bin", 0xD54D5402, 0x4000),
        ("b-l7.bin", 0xE7412D68, 0x2000),
    ],
    "pbaction_audio.bin": [
        ("a-e3.bin", 0x0E53A91F, 0x2000),
    ],
    "pbaction_fg.bin": [
        ("a-s6.bin", 0x9A74A8E1, 0x2000),
        ("a-s7.bin", 0x5CA6AD3C, 0x2000),
        ("a-s8.bin", 0x9F00B757, 0x2000),
    ],
    "pbaction_bg.bin": [
        ("a-j5.bin", 0x21EFE866, 0x4000),
        ("a-j6.bin", 0x7F984C80, 0x4000),
        ("a-j7.bin", 0xDF69E51B, 0x4000),
        ("a-j8.bin", 0x0094CB8B, 0x4000),
    ],
    "pbaction_sprites.bin": [
        ("b-c7.bin", 0xD1795EF5, 0x2000),
        ("b-d7.bin", 0xF28DF203, 0x2000),
        ("b-f7.bin", 0xAF6E9817, 0x2000),
    ],
}

def read_member(zf, name, expect_crc, expect_size):
    if name not in zf.namelist():
        raise FileNotFoundError(f"chip mancante: {name}")
    data = zf.read(name)
    if len(data) != expect_size:
        raise ValueError(f"{name}: size {len(data)} != {expect_size}")
    crc = zlib.crc32(data) & 0xFFFFFFFF
    tag = "OK  " if crc == expect_crc else "WARN"
    print(f"  {tag} {name}: {len(data):5d} byte CRC32 {crc:08X}")
    return data

def main():
    here = os.path.dirname(os.path.abspath(__file__))
    zip_path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(here, "ROMZIP", "pbaction.zip")
    out_dir  = sys.argv[2] if len(sys.argv) > 2 else os.path.join(here, "sd_rom_staging")
    os.makedirs(out_dir, exist_ok=True)
    print(f"[pbaction] zip:    {zip_path}")
    print(f"[pbaction] outdir: {out_dir}")
    if not os.path.isfile(zip_path):
        print("ERRORE: zip non trovata.", file=sys.stderr); return 1

    with zipfile.ZipFile(zip_path, "r") as zf:
        for out_name, members in REGIONS.items():
            print(f"[pbaction] region -> {out_name}:")
            chunks = [read_member(zf, n, c, s) for (n, c, s) in members]
            data = b"".join(chunks)
            out_path = os.path.join(out_dir, out_name)
            with open(out_path, "wb") as f: f.write(data)
            print(f"  scritto {out_path} ({len(data)} byte)")

    print()
    print("Upload su SD del P4 via C6 web bridge:")
    for n in REGIONS.keys():
        print(f"  POST http://192.168.1.100/api/ota/p4?path=/rom/{n}  (file={n})")
    return 0

if __name__ == "__main__":
    sys.exit(main())
