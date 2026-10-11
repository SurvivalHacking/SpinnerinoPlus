#!/usr/bin/env python3
"""
romconv_boblbobl.py — converte boblbobl.zip (set "Bobble Bobble", bootleg di
Bubble Bobble SENZA MCU 68705: la protezione e' realizzata da PAL) in header
C++ per il porting SPINNERINO WIFI (ESP32-P4).

Set: boblbobl  (Taito 1986, hardware Bubble Bobble)
Riferimento MAME: src/mame/taito/bublbobl.cpp  ROM_START(boblbobl)

ROM usate (tutte 0x8000 = 32 KB salvo PROM/PAL):
  maincpu  (region 0x30000):
    bb3          0x00000   main fisso     0x0000-0x7FFF
    bb5          0x10000   banchi 16 KB   (commutati da 0xFB40)
    bb4          0x18000   banchi 16 KB
    (0x20000-0x2FFFF vuoto)
  subcpu (CPU video/slave):
    a78-08.37    0x0000    32 KB
  audiocpu (CPU sonora, Z80 @ 3 MHz, YM2203 + YM3526):
    a78-07.46    0x0000    32 KB
  gfx1 (region 0x80000, ROMREGION_INVERT = bit invertiti, XOR 0xFF):
    META' BASSA  (planes MSB):  a78-09..14  @ 0x00000-0x2FFFF
    META' ALTA   (planes LSB):  a78-15..20  @ 0x40000-0x6FFFF
  proms:
    a71-25.41    256 B   timing video (NON serve all'emulazione, ignorata)
  plds:
    pal16r4.u36 / pal16l8.u38 / pal16l8.u4  (protezione bootleg, gestita a
    codice via boblbobl_ic43_a/b, NON come dump binario)

Output (in SPINNERINO WIFI/SPINNERINO/machines/boblbobl/):
  boblbobl_rom_main.h    const uint8_t boblbobl_rom_main[0x30000]
                           (bb3@0x0000, bb5@0x10000, bb4@0x18000; banchi 16KB a
                            partire da 0x10000, set_entry((data^4)&7); le entry
                            4-7 cadono in 0x20000-0x2FFFF = 0xFF, come in MAME)
  boblbobl_rom_sub.h     const uint8_t boblbobl_rom_sub[0x8000]   (a78-08.37)
  boblbobl_rom_audio.h   const uint8_t boblbobl_rom_audio[0x8000] (a78-07.46)
  boblbobl_gfx_decoded.h const uint8_t boblbobl_gfx_decoded[16384*64]  (1 MB)
                           tile 8x8 4bpp pre-decodificati, 1 byte/pixel (0..15)
  boblbobl_prom.h        const uint8_t boblbobl_prom[256]  (a71-25.41)
                           PROM timing/colonne: il rendering sprite la usa come
                           prom_line = prom+0x80+((gfx_num&0xe0)>>1) per pilotare
                           le colonne (vedi bublbobl_v.cpp).

NB palette: in Bubble Bobble la palette e' in RAM (la CPU scrive 0xF800-0xF9FF,
256 colori x 2 byte, formato RGBx_444 big-endian). NON c'e' PROM di palette: la
palette si costruisce a runtime nella machine (fase 3).

------------------------------------------------------------------------------
DECODE GFX (charlayout MAME, da bublbobl_v / gfxdecode):
  gfx_layout charlayout = {
    8, 8, RGN_FRAC(1,2), 4,
    planes  = { 0, 4, RGN_FRAC(1,2), RGN_FRAC(1,2)+4 },
    xoffset = { 3, 2, 1, 0, 8+3, 8+2, 8+1, 8+0 },
    yoffset = { 0*16, 1*16, ... 7*16 },
    16*8   // 16 byte per char
  };
  region 0x80000, RGN_FRAC(1,2) = 0x40000.
    META' BASSA  gfx[0x00000:0x40000]  -> planes[0] (peso 8 = MSB) e planes[1] (peso 4)
    META' ALTA   gfx[0x40000:0x80000]  -> planes[2] (peso 2)      e planes[3] (peso 1 = LSB)
  Per ogni char (16 byte): 2 byte per riga (byte0 = pixel 0-3, byte1 = pixel 4-7).
  Per pixel p (0..7):  byte_index = p//4,  k = p%4
    lo = gfx_low [tile*16 + y*2 + byte_index]
    hi = gfx_high[tile*16 + y*2 + byte_index]
    plane0(MSB,w8) = (lo >> (4+k)) & 1      # planeoffset 0  -> bit (4+k)
    plane1(   w4)  = (lo >> k)     & 1      # planeoffset 4  -> bit k
    plane2(   w2)  = (hi >> (4+k)) & 1      # planeoffset RGN_FRAC(1,2)
    plane3(LSB,w1) = (hi >> k)     & 1      # planeoffset RGN_FRAC(1,2)+4
    pixel = (plane0<<3)|(plane1<<2)|(plane2<<1)|plane3
  ROMREGION_INVERT: XOR 0xFF su tutta la region PRIMA del decode.
  Mappatura bit VERIFICATA on-device 2026-06-28 (prima era 3-k/7-k = pixel
  scrambled): MAME readbit MSB-first + xoffset {3,2,1,0,8+3,8+2,8+1,8+0}.
------------------------------------------------------------------------------
"""
import zipfile, os, sys

ZIP = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO\ROM SPINNERINO\boblbobl.zip"
OUT = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO WIFI\SPINNERINO\machines\boblbobl"

# ROM CPU "lineari" (nome zip -> (var, dimensione attesa))
ROM_SUB   = ("a78-08.37", "boblbobl_rom_sub",   0x8000)
ROM_AUDIO = ("a78-07.46", "boblbobl_rom_audio", 0x8000)

# maincpu: assemblata in una region 0x20000 (scarta coda vuota 0x20000-0x2FFFF)
MAIN_LAYOUT = [
    ("bb3", 0x00000, 0x8000),   # main fisso 0x0000-0x7FFF
    ("bb5", 0x10000, 0x8000),   # banchi
    ("bb4", 0x18000, 0x8000),   # banchi
]
MAIN_SIZE = 0x30000             # come region MAME (0x20000-0x2FFFF = 0xFF, entry 4-7)

PROM = ("a71-25.41", "boblbobl_prom", 0x100)   # 256 B timing/colonne

# gfx1: meta' bassa (planes MSB) e meta' alta (planes LSB)
GFX_LOW = [   # -> gfx[0x00000:0x40000]
    ("a78-09.12", 0x00000), ("a78-10.13", 0x08000), ("a78-11.14", 0x10000),
    ("a78-12.15", 0x18000), ("a78-13.16", 0x20000), ("a78-14.17", 0x28000),
]
GFX_HIGH = [  # -> gfx[0x40000:0x80000]  (offset relativo alla meta')
    ("a78-15.30", 0x00000), ("a78-16.31", 0x08000), ("a78-17.32", 0x10000),
    ("a78-18.33", 0x18000), ("a78-19.34", 0x20000), ("a78-20.35", 0x28000),
]
GFX_HALF  = 0x40000           # RGN_FRAC(1,2)
NUM_CHARS = GFX_HALF // 16    # 16384 char 8x8 (16 byte/char nella meta')


def write_byte_array(path, var_name, data):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w') as f:
        f.write(f"// Auto-generated by romconv_boblbobl.py\n")
        f.write(f"// {os.path.basename(path)}  size={len(data)} bytes\n")
        f.write(f"#include <stdint.h>\n\n")
        f.write(f"const uint8_t {var_name}[{len(data)}] PROGMEM = {{\n")
        for i in range(0, len(data), 16):
            chunk = data[i:i+16]
            f.write("  " + ", ".join(f"0x{b:02X}" for b in chunk) + ",\n")
        f.write("};\n")
    print(f"  wrote {path}  ({len(data)} bytes)")


def assemble(zf, names, layout, size):
    """Assembla piu' ROM in una region unica (riempita a 0xFF dove vuota)."""
    region = bytearray(b"\xFF" * size)
    for romname, off, length in layout:
        if romname not in names:
            print(f"!! ROM mancante: {romname}", file=sys.stderr); sys.exit(1)
        data = zf.read(romname)
        if len(data) != length:
            print(f"!! {romname}: size {len(data)} != atteso {length}", file=sys.stderr); sys.exit(1)
        region[off:off+length] = data
    return bytes(region)


def load_gfx_half(zf, names, parts):
    """Carica una meta' gfx (0x40000) concatenando 6 ROM da 32 KB."""
    half = bytearray(GFX_HALF)
    for romname, off in parts:
        if romname not in names:
            print(f"!! GFX mancante: {romname}", file=sys.stderr); sys.exit(1)
        data = zf.read(romname)
        half[off:off+len(data)] = data
    # ROMREGION_INVERT: bit invertiti
    return bytes(b ^ 0xFF for b in half)


def decode_gfx(gfx_low, gfx_high):
    """Pre-decodifica NUM_CHARS tile 8x8 4bpp in linear 1 byte/pixel (0..15).
    Vedi commento di testata per la mappatura plane/bit."""
    out = bytearray(NUM_CHARS * 64)
    for tile in range(NUM_CHARS):
        cbase = tile * 16
        obase = tile * 64
        for y in range(8):
            row = cbase + y * 2
            lo0, lo1 = gfx_low[row], gfx_low[row + 1]
            hi0, hi1 = gfx_high[row], gfx_high[row + 1]
            for p in range(8):
                lo = lo0 if p < 4 else lo1
                hi = hi0 if p < 4 else hi1
                k = p & 3
                # MAME readbit MSB-first + xoffset {3,2,1,0,8+3,...}: per il pixel
                # k nel byte, bit = 7-(xoffset&7) => plane @offset+0 -> bit (4+k),
                # plane @offset+4 -> bit k. (Fix 2026-06-28: prima avevo 3-k/7-k.)
                plane0 = (lo >> (4 + k)) & 1   # MSB  (peso 8) planeoffset 0
                plane1 = (lo >> k)       & 1   #      (peso 4) planeoffset 4
                plane2 = (hi >> (4 + k)) & 1   #      (peso 2) planeoffset RGN_FRAC(1,2)
                plane3 = (hi >> k)       & 1   # LSB  (peso 1) planeoffset RGN_FRAC(1,2)+4
                out[obase + y * 8 + p] = (plane0 << 3) | (plane1 << 2) | (plane2 << 1) | plane3
    return bytes(out)


def main():
    print(f"[romconv_boblbobl] {ZIP}")
    if not os.path.exists(ZIP):
        print(f"!! ZIP non trovato: {ZIP}", file=sys.stderr); sys.exit(1)
    zf = zipfile.ZipFile(ZIP)
    names = set(zf.namelist())

    # --- maincpu (assemblata con banchi) ---
    main_region = assemble(zf, names, MAIN_LAYOUT, MAIN_SIZE)
    write_byte_array(os.path.join(OUT, "boblbobl_rom_main.h"), "boblbobl_rom_main", main_region)

    # --- subcpu / audiocpu (lineari) ---
    for romname, var, sz in (ROM_SUB, ROM_AUDIO):
        if romname not in names:
            print(f"!! ROM mancante: {romname}", file=sys.stderr); sys.exit(1)
        data = zf.read(romname)
        if len(data) != sz:
            print(f"!! {romname}: size {len(data)} != atteso {sz}", file=sys.stderr); sys.exit(1)
        write_byte_array(os.path.join(OUT, f"{var}.h"), var, data)

    # --- PROM timing/colonne (a71-25.41) ---
    pname, pvar, psz = PROM
    if pname not in names:
        print(f"!! PROM mancante: {pname}", file=sys.stderr); sys.exit(1)
    pdata = zf.read(pname)
    if len(pdata) != psz:
        print(f"!! {pname}: size {len(pdata)} != atteso {psz}", file=sys.stderr); sys.exit(1)
    write_byte_array(os.path.join(OUT, f"{pvar}.h"), pvar, pdata)

    # --- gfx pre-decodificata ---
    print(f"[gfx decode] {NUM_CHARS} tile 8x8 4bpp -> {NUM_CHARS*64} byte ({NUM_CHARS*64//1024} KB)...")
    gfx_low  = load_gfx_half(zf, names, GFX_LOW)
    gfx_high = load_gfx_half(zf, names, GFX_HIGH)
    decoded  = decode_gfx(gfx_low, gfx_high)
    write_byte_array(os.path.join(OUT, "boblbobl_gfx_decoded.h"), "boblbobl_gfx_decoded", decoded)

    print("Done!")


if __name__ == '__main__':
    main()
