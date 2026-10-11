#!/usr/bin/env python3
# --- ancoraggio (copia SPINNERINO PLUS) --------------------------------------
# Copia di CABINATONE\romconv_outrun.py. Cambiano SOLO due cose:
#   1. l'ancoraggio: qui non esiste CABINATONE.ino, quindi ci si sposta
#      esplicitamente sul progetto CABINATONE, dove stanno ROMS/outrun.zip e
#      _verify_gfx/ (le ROM dell'utente non sono state duplicate);
#   2. OUT_DIR: i file finiscono in "SD SPINNERINO/outrun/", lo staging della
#      microSD dello SPINNERINO PLUS, invece che in "DA COPIARE SU SD/outrun".
# Il RESTO (estrazione, interlacciamento, i 31 CRC verificati contro MAME) e'
# byte per byte quello di CABINATONE: i file prodotti sono gli STESSI
# (MD5 verificati 7/7). Vale [[feedback_sync_py]]: se un giorno si corregge il
# converter, va corretto in ENTRAMBE le copie.
import os as _os
# --- uscita adattata al PACCHETTO (2026-09-05) ------------------------
# Questi quattro giochi NON hanno la ROM negli header: la leggono dalla SD.
# I file prodotti qui vanno in "DA COPIARE SU SD\<gioco>" nella radice del
# pacchetto, che e' quello che l'utente copia poi sulla microSD.
_PACCHETTO_SD = _os.path.join(
    _os.path.dirname(_os.path.dirname(_os.path.abspath(__file__))),
    "DA COPIARE SU SD")

_CABINATONE = r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE"
_SD_SPIN    = r"C:\Users\Tecnico\Desktop\PROGETTI SH\SPINNERINO PLUS WIFI\BACKUP SPINNERINO PLUS\SD SPINNERINO"
_os.chdir(_CABINATONE)
# ----------------------------------------------------------------------------
# ============================================================================
# romconv_outrun.py — Out Run (Sega, 1986) -> binari per la SD del CABINATONE.
#
# FASE 1 del porting. Estrae le region cosi' come le costruisce
# ROM_START(outrun) in src/mame/sega/segaorun.cpp (MAME corrente), senza
# espandere la grafica: come per ghouls, su SD va il formato NATIVO e
# l'espansione la fa il firmware in PSRAM al boot.
#
# ⭐ FATTI VERIFICATI DAI BYTE (non supposti), vedi _verify_gfx/outrun_probe.py:
#   - le ROM del 68000 NON sono cifrate (niente FD1089/FD1094): interlacciando
#     come qui sotto, maincpu da SSP=00068000 (cima della work RAM 060000-067FFF)
#     e PC=00007B1E, dove c'e' un vero loop di copia
#     (LEA $FF20.W,A0 / LEA $7B40,A1 / MOVE.L #$0F,D0 / MOVE.W (A1)+,(A0)+ / DBF).
#     subcpu da SSP=00064000 (shared RAM) e PC=00001000 (PEA/MOVE.W #$0700,-(A7)/RTE).
#   - i vettori confermano lo schema di interrupt del driver: il main ha handler
#     propri SOLO su IRQ2 (0x7C6C, HBLANK) e IRQ4 (0x7CBA, VBLANK), il sub solo
#     su IRQ4 (0x1084). Tutti gli altri puntano a un handler comune.
#   - 31/31 CRC identici a MAME (_verify_gfx/outrun_crc.py).
#
# ⚠️ Attenzione a due dettagli che NON sono come negli altri porting:
#   - gli sprite sono ROM_REGION32_LE / ROM_LOAD32_BYTE: QUATTRO file
#     interlacciati a byte dentro una dword LITTLE-endian (non 16 bit big-endian).
#   - il PCM usa ROM_RELOAD: ogni ROM da 32K viene ricaricata a +0x8000, cosi'
#     ogni banco da 64K del SegaPCM e' pieno. La region parte a 0xFF (ERASEFF).
# ============================================================================
import sys, zipfile, zlib, struct, hashlib

OUT_DIR    = _os.path.join(_PACCHETTO_SD, "outrun")
VERIFY_DIR = "_verify_gfx"

# ----------------------------------------------------------------------------
# ROM_START(outrun): nome -> CRC32 atteso. Ogni byte estratto viene confrontato,
# cosi' un set sbagliato si ferma qui invece che sul device.
CRC = {
    "epr-10380b.133": 0x1f6cadad, "epr-10382b.118": 0xc4c3fa1a,
    "epr-10381b.132": 0xbe8c412b, "epr-10383b.117": 0x10a2014a,
    "epr-10327a.76":  0xe28a5baf, "epr-10329a.58":  0xda131c81,
    "epr-10328a.75":  0xd5ec5e5d, "epr-10330a.57":  0xba9ec82a,
    "opr-10268.99":   0x95344b04, "opr-10232.102":  0x776ba1eb,
    "opr-10267.100":  0xa85bb823, "opr-10231.103":  0x8908bcbf,
    "opr-10266.101":  0x9f6f1a74, "opr-10230.104":  0x686f5e50,
    "mpr-10371.9":    0x7cc86208, "mpr-10373.10":   0xb0d26ac9,
    "mpr-10375.11":   0x59b60bd7, "mpr-10377.12":   0x17a1b04a,
    "mpr-10372.13":   0xb557078c, "mpr-10374.14":   0x8051e517,
    "mpr-10376.15":   0xf3b8f318, "mpr-10378.16":   0xa1062984,
    "opr-10186.47":   0x22794426, "opr-10185.11":   0x22794426,
    "epr-10187.88":   0xa10abaa9,
    "opr-10193.66":   0xbcd10dde, "opr-10192.67":   0x770f1270,
    "opr-10191.68":   0x20a284ab, "opr-10190.69":   0x7cab70e2,
    "opr-10189.70":   0x01366b54, "opr-10188.71":   0xbad30ad9,
}

_z = None
def rd(name):
    """Legge un file dallo zip e ne verifica il CRC contro MAME."""
    d = _z.read(name)
    c = zlib.crc32(d) & 0xFFFFFFFF
    if c != CRC[name]:
        sys.exit(f"ERRORE: {name} CRC {c:08x}, atteso {CRC[name]:08x}")
    return d

# ----------------------------------------------------------------------------
def build_68k(pairs, size):
    """ROM_LOAD16_BYTE: il file 'even' da' il byte ALTO (indirizzo pari), l'altro
    il basso. Il 68000 e' big-endian, quindi cosi' esce gia' nell'ordine giusto."""
    out = bytearray(b'\xff' * size)
    for even, odd, base in pairs:
        e, o = rd(even), rd(odd)
        assert len(e) == len(o)
        out[base     : base + 2*len(e) : 2] = e
        out[base + 1 : base + 2*len(o) : 2] = o
    return bytes(out)

def build_tiles():
    """gfx1: 3 bitplane da 0x10000, ognuno formato da due ROM da 32K.
    Tile 8x8: il pixel (x,y) del tile n ha il bit del piano p in
    rom[p*0x10000 + n*8 + y], bit (7-x). 0x10000/8 = 8192 tile."""
    out = bytearray()
    for a, b in (("opr-10268.99",  "opr-10232.102"),     # piano 0
                 ("opr-10267.100", "opr-10231.103"),     # piano 1
                 ("opr-10266.101", "opr-10230.104")):    # piano 2
        out += rd(a) + rd(b)
    assert len(out) == 0x30000
    return bytes(out)

def build_sprites():
    """ROM_REGION32_LE + ROM_LOAD32_BYTE: 4 ROM interlacciate a byte dentro una
    dword little-endian. Due banchi: 0x00000 e 0x80000.
    Ogni dword = 8 pixel da 4 bit (il renderer li legge dal nibble alto in giu')."""
    out = bytearray(b'\x00' * 0x100000)
    banks = [(0x00000, ("mpr-10371.9",  "mpr-10373.10", "mpr-10375.11", "mpr-10377.12")),
             (0x80000, ("mpr-10372.13", "mpr-10374.14", "mpr-10376.15", "mpr-10378.16"))]
    for base, files in banks:
        for byte_idx, fn in enumerate(files):
            d = rd(fn)
            out[base + byte_idx : base + 4*len(d) : 4] = d
    return bytes(out)

def build_road():
    """Le due ROM della strada sono lo STESSO dump (una per strada): il CRC
    identico 22794426 e' una firma del set, non un errore di estrazione."""
    return rd("opr-10186.47") + rd("opr-10185.11")

def build_pcm():
    """ROMREGION_ERASEFF + ROM_RELOAD: ogni ROM da 32K occupa un banco da 64K,
    ripetuta due volte. Sei banchi, poi il resto resta a 0xFF."""
    out = bytearray(b'\xff' * 0x80000)
    for i, fn in enumerate(("opr-10193.66", "opr-10192.67", "opr-10191.68",
                            "opr-10190.69", "opr-10189.70", "opr-10188.71")):
        d = rd(fn)
        base = i * 0x10000
        out[base           : base + 0x8000]  = d      # ROM_LOAD
        out[base + 0x8000  : base + 0x10000] = d      # ROM_RELOAD
    return bytes(out)

# ----------------------------------------------------------------------------
def tiles_8x8(gfx, first, count):
    """Decodifica tile 8x8 a 3 bitplane -> liste di 64 valori 0..7."""
    res = []
    for n in range(first, first + count):
        t = []
        for y in range(8):
            b0 = gfx[0x00000 + n*8 + y]
            b1 = gfx[0x10000 + n*8 + y]
            b2 = gfx[0x20000 + n*8 + y]
            for x in range(8):
                m = 1 << (7 - x)
                t.append(((b0 & m) and 1 or 0) |
                         ((b1 & m) and 2 or 0) |
                         ((b2 & m) and 4 or 0))
        res.append(t)
    return res

def sprite_strip(spr, dword_first, rows, px_per_row=64):
    """Gli sprite di Out Run non sono tile a dimensione fissa: sono sequenze di
    dword, 8 pixel da 4 bit ciascuna, con marker di fine riga. Per la verifica
    li srotolo come una striscia larga px_per_row: basta a vedere se i pixel
    hanno struttura (forme, contorni) invece che rumore."""
    out = []
    dw_per_row = px_per_row // 8
    for r in range(rows):
        row = []
        for c in range(dw_per_row):
            off = (dword_first + r*dw_per_row + c) * 4
            if off + 4 > len(spr):
                row += [0]*8
                continue
            d = struct.unpack_from('<I', spr, off)[0]
            for s in (28, 24, 20, 16, 12, 8, 4, 0):
                row.append((d >> s) & 0xF)
        out.append(row)
    return out

def dump_sheet(path, tiles, w, h, cols, title):
    """Foglio PNG per la verifica VISIVA offline (niente debug on-device)."""
    try:
        from PIL import Image
    except ImportError:
        print("  (PIL assente: salto i PNG di verifica)")
        return
    n = len(tiles)
    rows = (n + cols - 1) // cols
    img = Image.new("L", (cols * w, rows * h), 0)
    mx = max(1, max(max(t) for t in tiles))
    for i, t in enumerate(tiles):
        sub = Image.new("L", (w, h)); sub.putdata([v * 255 // mx for v in t])
        img.paste(sub, ((i % cols) * w, (i // cols) * h))
    img.save(path)
    print(f"  {path}: {n} {title} ({w}x{h}), pixel max = {mx}")

def dump_rows(path, rows, title):
    try:
        from PIL import Image
    except ImportError:
        return
    h, w = len(rows), len(rows[0])
    img = Image.new("L", (w, h), 0)
    img.putdata([v * 255 // 15 for r in rows for v in r])
    img.save(path)
    print(f"  {path}: {w}x{h} {title}")

# ----------------------------------------------------------------------------
def main():
    global _z
    zip_path = sys.argv[1] if len(sys.argv) > 1 else _os.path.join("ROMS", "outrun.zip")
    if not _os.path.isfile(zip_path):
        sys.exit(f"ERRORE: zip non trovato: {zip_path}")
    print(f"Sorgente: {zip_path}\n")
    _os.makedirs(OUT_DIR, exist_ok=True)
    _os.makedirs(VERIFY_DIR, exist_ok=True)
    _z = zipfile.ZipFile(zip_path)

    print("Region (ogni file verificato per CRC contro ROM_START(outrun)):")
    main68 = build_68k([("epr-10380b.133", "epr-10382b.118", 0x00000),
                        ("epr-10381b.132", "epr-10383b.117", 0x20000)], 0x60000)
    sub68  = build_68k([("epr-10327a.76",  "epr-10329a.58",  0x00000),
                        ("epr-10328a.75",  "epr-10330a.57",  0x20000)], 0x60000)
    tiles  = build_tiles()
    spr    = build_sprites()
    road   = build_road()
    snd    = rd("epr-10187.88")
    pcm    = build_pcm()

    # I vettori sono la prova che l'interlacciamento pari/dispari e' giusto:
    # SSP deve cadere nella RAM (main: work RAM 060000-068000; sub: shared
    # 060000-068000) e PC dentro la ROM. Se si invertono i file, esce spazzatura.
    for nome, rom, lo, hi in (("maincpu", main68, 0x060000, 0x068000),
                              ("subcpu",  sub68,  0x060000, 0x068000)):
        ssp, pc = struct.unpack(">II", rom[0:8])
        print(f"  {nome:8s} {len(rom):8d} byte   RESET: SSP={ssp:08X} PC={pc:08X}", end="")
        if not (lo <= ssp <= hi and pc % 2 == 0 and pc < len(rom)):
            sys.exit("  <-- vettore NON valido: interlacciamento sbagliato!")
        print("  (SSP in RAM, PC in ROM: OK)")
    print(f"  tiles    {len(tiles):8d} byte   3 bitplane da 0x10000 = 8192 tile 8x8")
    print(f"  sprites  {len(spr):8d} byte   dword LE, 8 pixel da 4 bit")
    print(f"  road     {len(road):8d} byte   due dump identici (una ROM per strada)")
    print(f"  soundcpu {len(snd):8d} byte   Z80")
    print(f"  pcm      {len(pcm):8d} byte   6 banchi da 64K (ROM_RELOAD)")

    print("\nFile su SD:")
    tot = 0
    for nm, data in (("main.bin", main68), ("sub.bin", sub68), ("tiles.bin", tiles),
                     ("sprites.bin", spr), ("road.bin", road), ("snd.bin", snd),
                     ("pcm.bin", pcm)):
        p = _os.path.join(OUT_DIR, nm)
        with open(p, "wb") as f:
            f.write(data)
        tot += len(data)
        print(f"  {p:44s} {len(data):8d} byte  MD5 {hashlib.md5(data).hexdigest()}")

    print("\nFogli PNG di verifica (guardare le FORME, i colori stanno nella palette RAM):")
    dump_sheet(_os.path.join(VERIFY_DIR, "outrun_tiles.png"),
               tiles_8x8(tiles, 0, 512), 8, 8, 32, "tile 8x8 3bpp")
    dump_rows(_os.path.join(VERIFY_DIR, "outrun_sprites.png"),
              sprite_strip(spr, 0, 256), "sprite srotolati (banco 0)")

    print(f"\nFatto. Totale su SD: {tot} byte ({tot/1024/1024:.2f} MB)")
    print(f"Copiare la cartella '{OUT_DIR}' in /outrun/ sulla SD.")

if __name__ == "__main__":
    main()
