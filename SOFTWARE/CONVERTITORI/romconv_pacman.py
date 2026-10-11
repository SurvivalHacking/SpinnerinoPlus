#!/usr/bin/env python3

# --- ancoraggio alla RADICE DEL PROGETTO ------------------------------------
import os as _os
def _project_root():
    d = _os.path.dirname(_os.path.abspath(__file__))
    for cand in (d, _os.path.dirname(d)):
        if _os.path.isfile(_os.path.join(cand, "CABINATONE.ino")):
            return cand
    return d
_os.chdir(_project_root())
# ----------------------------------------------------------------------------
# ============================================================================
# romconv_pacman.py — da `ROMS/pacman.zip` e `ROMS/mspacman.zip` agli header
# PROGMEM di PAC-MAN (Namco 1980) e MS. PAC-MAN (Midway 1981).
#
# ⭐⭐ COME SI VERIFICA, senza flashare: con `--verifica` non scrive niente e
# confronta cio' che produce con gli header gia' nel progetto — quelli copiati
# dal porting **che gira** su SPINNERINO PLUS. Un solo confronto prova TRE cose:
# che le zip sono i set giusti, che ogni passaggio di decodifica qui e'
# trascritto bene, e che i dati compilati nel firmware sono quelli collaudati
# ([[feedback_verify_against_source_not_copy]]).
#
# ⚠️ Il confronto e' sui VALORI, non sul testo: gli header vengono da stesure
# diverse e l'impaginazione non coincide. Su Galaga coincideva ed era testuale.
#
# LE COSE CHE NON SI INDOVINANO, E DA DOVE VENGONO
# 1. ⛔ **La palette NON usa i pesi delle resistenze di MAME.** Galagino usa una
#    ripartizione lineare, e lo dichiara nel suo `romconv/cmapconv.py`:
#        «This doesn't 100% match the weighting each bit has on the real machine
#         with 1000, 470 and 220 ohms resistors»
#    cioe' 4/7-2/7-1/7 invece di 4,15/7-1,94/7-0,91/7. Provarci con i pesi di
#    MAME da' colori vicini ma DIVERSI, e `--verifica` lo boccerebbe. Formula
#    presa dal sorgente, non ricavata a occhio.
# 2. La tavola d'onda e' il **nibble basso della PROM meno 7** (0..15 -> -7..+8).
# 3. Il formato di tile e sprite e' quello dell'hardware Pac-Man: i DUE piani di
#    bit stanno nello STESSO byte (nibble basso e nibble alto) e l'ordine dei
#    byte e' rimescolato. Trascritto dal convertitore di Galagino.
# 4. Ms. Pac-Man: le ROM u5/u6/u7 hanno indirizzi E dati rimescolati, e sopra ci
#    vanno **40 rappezzi da 8 byte** che copiano pezzi della ROM di estensione
#    dentro quella originale. E' `mspacman_install_patches` di MAME.
#
# USO:  python romconv_pacman.py            (rigenera gli header)
#       python romconv_pacman.py --verifica (non scrive: confronta e basta)
# ============================================================================
import os, re, sys, zipfile

VERIFICA = "--verifica" in sys.argv

ZIP_PAC = _os.path.join("ROMS", "pacman.zip")
ZIP_MS  = _os.path.join("ROMS", "mspacman.zip")
OUT_PAC = _os.path.join("machines", "pacman")
OUT_MS  = _os.path.join("machines", "mspacman")

ATTESI_PAC = {"pacman.6e": 4096, "pacman.6f": 4096, "pacman.6h": 4096,
              "pacman.6j": 4096, "pacman.5e": 4096, "pacman.5f": 4096,
              "82s123.7f": 32, "82s126.4a": 256, "82s126.1m": 256}
ATTESI_MS  = {"pacman.6e": 4096, "pacman.6f": 4096, "pacman.6h": 4096,
              "u5": 2048, "u6": 4096, "u7": 4096, "5e": 4096, "5f": 4096}


def apri(path, attesi):
    if not _os.path.exists(path):
        sys.exit("ERRORE: manca %s" % path)
    z = zipfile.ZipFile(path)
    dentro = {n.lower(): n for n in z.namelist()}
    manca = [n for n in attesi if n.lower() not in dentro]
    if manca:
        sys.exit("ERRORE: in %s mancano: %s" % (path, ", ".join(manca)))

    def load(nome):
        d = z.read(dentro[nome.lower()])
        if len(d) != attesi[nome]:
            sys.exit("ERRORE: %s e' %d byte, ne servono %d"
                     % (nome, len(d), attesi[nome]))
        return bytearray(d)
    print("%s: %d file, dimensioni verificate" % (path, len(attesi)))
    return load


load_pac = apri(ZIP_PAC, ATTESI_PAC)
load_ms  = apri(ZIP_MS,  ATTESI_MS)

# ============================================================ 1. PAC-MAN: ROM
pac_rom = bytes(load_pac("pacman.6e") + load_pac("pacman.6f") +
                load_pac("pacman.6h") + load_pac("pacman.6j"))
# ⭐ Prova indipendente: la Z80 parte da 0x0000 e la prima istruzione di Pac-Man
# e' `DI` (0xF3). Se la zip fosse un altro set, ci si ferma qui.
if pac_rom[0] != 0xF3:
    sys.exit("ERRORE: il primo byte della ROM e' %02X invece di F3 (DI): "
             "la zip non e' il set `pacman`." % pac_rom[0])
print("  CPU ROM Pac-Man: %d byte (parte con DI, come deve)" % len(pac_rom))

# ================================================ 2. tile e sprite (formato HW)
# I due piani di bit stanno nello STESSO byte: nibble basso = piano 0, nibble
# alto = piano 1, e l'ordine dei byte dentro al carattere e' rimescolato.
def converti_tile(rom):
    tiles = []
    for t in range(256):
        d = rom[16 * t: 16 * (t + 1)]
        righe = []
        for y in range(8):
            val = 0
            for x in range(8):
                b = d[15 - x - 2 * (y & 4)]
                px = (1 if b & (0x08 >> (y & 3)) else 0) + \
                     (2 if b & (0x80 >> (y & 3)) else 0)
                val = (val >> 2) | (px << 14)
            righe.append(val)
        tiles.append(righe)
    return tiles


def leggi_sprite(d):
    spr = []
    for y in range(16):
        riga = []
        for x in range(16):
            i = ((y & 8) << 1) + (((x & 8) ^ 8) << 2) + (7 - (x & 7)) + 2 * (y & 4)
            riga.append((1 if d[i] & (0x08 >> (y & 3)) else 0) +
                        (2 if d[i] & (0x80 >> (y & 3)) else 0))
        spr.append(riga)
    # formato Pac-Man: le quattro righe di sopra e quelle di sotto sono scambiate
    return spr[4:] + spr[:4]


def impacchetta_sprite(px, flip_x, flip_y):
    righe = []
    for y in (reversed(range(16)) if flip_y else range(16)):
        val = 0
        for x in range(16):
            if flip_x:
                val = (val << 2) | px[y][x]
            else:
                val = (val >> 2) | (px[y][x] << 30)
        righe.append(val)
    return righe


def converti_sprite(rom):
    # ⚠️ L'ordine dei quattro orientamenti e' quello che il gioco indicizza con
    # `flags & 3`: niente, flipY, flipX, entrambi.
    fuori = []
    grezzi = [leggi_sprite(rom[64 * s: 64 * (s + 1)]) for s in range(64)]
    for f in range(4):
        fuori.append([impacchetta_sprite(grezzi[s], f & 2, f & 1) for s in range(64)])
    return fuori


pac_tiles   = converti_tile(load_pac("pacman.5e"))
pac_sprites = converti_sprite(load_pac("pacman.5f"))
ms_tiles    = converti_tile(load_ms("5e"))
ms_sprites  = converti_sprite(load_ms("5f"))
print("  tile e sprite: 256 tile e 64 sprite x 4 orientamenti, per ciascun set")

# ============================================================ 3. colori
# ⛔ Ripartizione LINEARE di Galagino (romconv/cmapconv.py), non i pesi delle
# resistenze di MAME. Il byte della PROM e' bbgggrrr.
def prom_rgb565_swapped(c):
    r = 31 * ((c >> 0) & 0x07) // 7
    g = 63 * ((c >> 3) & 0x07) // 7
    b = 31 * ((c >> 6) & 0x03) // 3
    v = (r << 11) | (g << 5) | b
    return ((v & 0xFF) << 8) | (v >> 8)          # byte-swapped, come il display SPI


pal = [prom_rgb565_swapped(c) for c in load_pac("82s123.7f")]
ctab = load_pac("82s126.4a")
pac_cmap = [[pal[ctab[p * 4 + i] & 0x0F] for i in range(4)] for p in range(64)]
print("  colormap: 64 tavolozze x 4 colori (formula lineare di Galagino)")

# ============================================================ 4. tavola d'onda
# Otto forme d'onda da 32 campioni: nibble basso della PROM, centrato su 0.
snd = load_pac("82s126.1m")
pac_wave = [[(snd[w * 32 + i] & 0x0F) - 7 for i in range(32)] for w in range(8)]
print("  tavola d'onda: 8 forme da 32 campioni")

# ================================================ 5. MS. PAC-MAN: decodifica
def bitswap(v, *bits):
    r = 0
    for i, b in enumerate(bits):
        if v & (1 << b):
            r |= 1 << (len(bits) - 1 - i)
    return r


def dato(b):    return bitswap(b, 0, 4, 5, 7, 6, 3, 2, 1)
def ind11(i):   return bitswap(i, 8, 7, 5, 9, 10, 6, 3, 4, 2, 1, 0)
def ind12(i):   return bitswap(i, 11, 3, 7, 9, 10, 8, 6, 5, 4, 2, 1, 0)


ms6e, ms6f, ms6h = load_ms("pacman.6e"), load_ms("pacman.6f"), load_ms("pacman.6h")
u5, u6, u7 = load_ms("u5"), load_ms("u6"), load_ms("u7")

drom = bytearray([0xFF] * 0xC000)
for i in range(0x1000):
    drom[0x0000 + i] = ms6e[i]
    drom[0x1000 + i] = ms6f[i]
    drom[0x2000 + i] = ms6h[i]
    drom[0x3000 + i] = dato(u7[ind12(i) & 0xFFF])          # u7 sostituisce 6j
for i in range(0x800):
    drom[0x8000 + i] = dato(u5[ind11(i) & 0x7FF])
    drom[0x8800 + i] = dato(u6[(ind12(i) & 0x7FF) + 0x800])
    drom[0x9000 + i] = dato(u6[ind12(i) & 0x7FF])
    drom[0x9800 + i] = ms6f[0x800 + i]                     # rispecchia, non decodifica

# I 40 rappezzi: ognuno copia 8 byte dalla ROM di estensione dentro l'originale.
PATCH = [
    (0x0410, 0x8008), (0x08E0, 0x81D8), (0x0A30, 0x8118), (0x0BD0, 0x80D8),
    (0x0C20, 0x8120), (0x0E58, 0x8168), (0x0EA8, 0x8198),
    (0x1000, 0x8020), (0x1008, 0x8010), (0x1288, 0x8098), (0x1348, 0x8048),
    (0x1688, 0x8088), (0x16B0, 0x8188), (0x16D8, 0x80C8), (0x16F8, 0x81C8),
    (0x19A8, 0x80A8), (0x19B8, 0x81A8),
    (0x2060, 0x8148), (0x2108, 0x8018), (0x21A0, 0x81A0), (0x2298, 0x80A0),
    (0x23E0, 0x80E8), (0x2418, 0x8000), (0x2448, 0x8058), (0x2470, 0x8140),
    (0x2488, 0x8080), (0x24B0, 0x8180), (0x24D8, 0x80C0), (0x24F8, 0x81C0),
    (0x2748, 0x8050), (0x2780, 0x8090), (0x27B8, 0x8190), (0x2800, 0x8028),
    (0x2B20, 0x8100), (0x2B30, 0x8110), (0x2BF0, 0x81D0), (0x2CC0, 0x80D0),
    (0x2CD8, 0x80E0), (0x2CF0, 0x81E0), (0x2D60, 0x8160),
]
for dst, src in PATCH:
    drom[dst:dst + 8] = drom[src:src + 8]

ms_pacrom = bytes(drom[0x0000:0x4000])
ms_auxrom = bytes(drom[0x8000:0xA000])
if ms_pacrom[0] != 0xF3:
    sys.exit("ERRORE: la ROM di Ms. Pac-Man non parte con DI (%02X)" % ms_pacrom[0])
print("  Ms. Pac-Man: u5/u6/u7 decodificate, %d rappezzi applicati" % len(PATCH))

# ============================================================ 6. testi e uscita
def t_byte(nome, dati):
    r = ["const unsigned char %s[] = {\n" % nome]
    for i in range(0, len(dati), 16):
        r.append("  " + ",".join("0x%02X" % b for b in dati[i:i + 16]) + ",\n")
    r.append("};\n")
    return "".join(r)


def t_tile(nome, tiles):
    r = ["const unsigned short %s[][8] = {\n" % nome]
    for righe in tiles:
        r.append(" { " + ",".join("0x%x" % v for v in righe) + " },\n")
    r.append("};\n")
    return "".join(r)


def t_sprite(nome, ori):
    r = ["const unsigned long %s[][64][16] = {\n" % nome]
    for sprites in ori:
        r.append(" {\n")
        for righe in sprites:
            r.append("  { " + ",".join("0x%x" % v for v in righe) + " },\n")
        r.append(" },\n")
    r.append("};\n")
    return "".join(r)


def t_cmap():
    r = ["const unsigned short pacman_colormap[][4] = {\n"]
    for p in pac_cmap:
        r.append("{" + ",".join("0x%x" % c for c in p) + "},\n")
    r.append("};\n")
    return "".join(r)


def t_wave():
    r = ["const signed char pacman_wavetable[][32] = {\n"]
    for w, onda in enumerate(pac_wave):
        r.append("  { " + ",".join("%d" % v for v in onda) + " },\n")
    r.append("};\n")
    return "".join(r)


FILE = [
    (OUT_PAC, "pacman_rom.h",        t_byte("pacman_rom", pac_rom)),
    (OUT_PAC, "pacman_tilemap.h",    t_tile("pacman_tilemap", pac_tiles)),
    (OUT_PAC, "pacman_spritemap.h",  t_sprite("pacman_sprites", pac_sprites)),
    (OUT_PAC, "pacman_cmap.h",       t_cmap()),
    (OUT_PAC, "pacman_wavetable.h",  t_wave()),
    (OUT_MS,  "mspacman_pacrom.h",   t_byte("mspacman_pacrom", ms_pacrom)),
    (OUT_MS,  "mspacman_auxrom.h",   t_byte("mspacman_auxrom", ms_auxrom)),
    (OUT_MS,  "mspacman_tilemap.h",  t_tile("mspacman_tilemap", ms_tiles)),
    (OUT_MS,  "mspacman_spritemap.h", t_sprite("mspacman_sprites", ms_sprites)),
]


def valori(testo):
    """Tutti i numeri DOPO la prima graffa, saltando i commenti: e' il contenuto
    vero, indipendente da come e' impaginato. ⚠️ I commenti vanno tolti o i
    numeri che contengono (nomi di PROM, «wave #0») entrano nel confronto."""
    i = testo.find("{")
    corpo = testo[i:]
    corpo = re.sub(r"/\*.*?\*/", " ", corpo, flags=re.S)
    corpo = re.sub(r"//[^\n]*", " ", corpo)
    return [int(x, 0) for x in re.findall(r"-?0[xX][0-9a-fA-F]+|-?\d+", corpo)]


if VERIFICA:
    uguali = diversi = 0
    for cartella, nome, corpo in FILE:
        p = _os.path.join(cartella, nome)
        if not _os.path.exists(p):
            print("  %-24s MANCA nel progetto" % nome); diversi += 1; continue
        mio = valori(corpo)
        suo = valori(open(p, encoding="utf-8", errors="replace").read())
        if mio == suo:
            print("  %-24s IDENTICO all'header del porting che gira (%d valori)"
                  % (nome, len(mio)))
            uguali += 1
        else:
            n = min(len(mio), len(suo))
            primo = next((k for k in range(n) if mio[k] != suo[k]), n)
            print("  %-24s DIVERSO: %d valori contro %d, primo scarto all'indice "
                  "%d (mio %s, suo %s)"
                  % (nome, len(mio), len(suo), primo,
                     mio[primo] if primo < len(mio) else "-",
                     suo[primo] if primo < len(suo) else "-"))
            diversi += 1
    print("\n%d identici, %d diversi." % (uguali, diversi))
    if diversi:
        sys.exit(1)
    print("OK: il convertitore riproduce ESATTAMENTE i dati del porting che gira,\n"
          "   valore per valore: le due zip sono i set giusti e ogni passaggio di\n"
          "   decodifica e' trascritto bene.")
    sys.exit(0)

for cartella, nome, corpo in FILE:
    os.makedirs(cartella, exist_ok=True)
    with open(_os.path.join(cartella, nome), "w", encoding="utf-8") as f:
        f.write("// Generato da romconv_pacman.py - NON modificare a mano.\n")
        f.write(corpo)
    print("  scritto %s" % nome)
print("\nFatto. I loghi del menu si rifanno con convert_pacman_logo.py.")
