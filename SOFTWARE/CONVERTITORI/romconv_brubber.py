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
# romconv_brubber.py — da `ROMS/brubber.zip` agli header di BURNIN' RUBBER
# (Data East 1982, set bootleg `brubber` di `bnj`; driver MAME
# `src/mame/dataeast/btime.cpp`, configurazione `bnj`).
#
# ⭐⭐ COME SI VERIFICA: con `--verifica` non scrive niente e confronta i VALORI
# che produce con quelli degli header gia' nel progetto — che sono la copia di
# `BACKUP SPINNERINO PLUS\SPINNERINO\machines\brubber\`, cioe' del porting **che
# gira su hardware** ([[feedback_verify_against_source_not_copy]]).
# Un solo confronto prova tre cose insieme: che la zip in ROMS/ e' il set giusto,
# che ogni passaggio di decodifica e' trascritto bene, e che i dati nel firmware
# sono davvero quelli che l'utente ha gia' visto funzionare.
# ⚠️ Si confrontano i NUMERI, non il testo: l'impaginazione dei due generatori
# non e' identica.
#
# LE COSE CHE NON SI INDOVINANO — e due sono difetti gia' PAGATI ([[brubber_port]])
# 1. ⛔⛔ **`readbit()` di MAME e' MSB-FIRST** (`0x80 >> (bitnum % 8)`). Con
#    LSB-first i tile dello sfondo escono a coriandoli, e non da' nessun errore.
# 2. ⛔⛔ **L'assegnazione dei piani dello SFONDO non e' quella ovvia**: la
#    `bnj_tile16layout` dichiara `planes = { RGN_FRAC(1,2)+4, RGN_FRAC(0,2)+0,
#    RGN_FRAC(0,2)+4 }`, cioe' bit ALTO = nibble BASSO della **seconda** meta',
#    bit medio = nibble ALTO della prima, bit BASSO = nibble BASSO della prima.
#    Sbagliarla da' i colori scambiati, non un errore.
# 3. ⛔ **Tre rotazioni DIVERSE**: caratteri e sprite sono **pre-ruotati** di 270
#    nei dati, lo **SFONDO NO** (il suo blitter lo campiona diritto, `riga=py`
#    `colonna=px`). E' la stessa asimmetria di Bomb Jack: se si pre-ruotasse
#    anche lo sfondo, `bb_render_background()` lo leggerebbe storto.
# 4. Gli sprite 16x16 hanno la **meta' DESTRA per prima** nella `xoffset`
#    (`{ STEP8(16*8,1), STEP8(0,1) }`), e sono memorizzati **quattro volte**, una
#    per combinazione di ribaltamento.
# 5. Lo sfondo ha la `xoffset` in **quattro gruppi da 4 pixel** a passi di
#    48/32/16/0 byte, cioe' al contrario.
# 6. La ROM principale sono DUE file da 8 KB che stanno a **$C000** e **$E000**:
#    il set `brubber` non popola $A000-$BFFF (l'area della ROM di servizio).
#
# ⚠️ Il logo NON si fa qui: e' `convert_brubber_logo.py` (parte dal logo TFT del
# porting, non dalla zip).
#
# USO:  python romconv_brubber.py            (rigenera gli header)
#       python romconv_brubber.py --verifica (non scrive: confronta e basta)
# ============================================================================
import os, re, sys, zipfile

VERIFICA = "--verifica" in sys.argv

ZIP = _os.path.join("ROMS", "brubber.zip")
OUT = _os.path.join("machines", "brubber")

# nome nella zip -> dimensione attesa
ATTESI = {
    "brubber.12c": 8192, "brubber.12d": 8192,     # CPU principale ($C000, $E000)
    "bnj6c.bin":   4096,                          # CPU sonora ($E000, specchiata)
    "bnj4e.bin":   8192,                          # gfx1 piano 0 (bit BASSO)
    "bnj4f.bin":   8192,                          # gfx1 piano 1
    "bnj4h.bin":   8192,                          # gfx1 piano 2 (bit ALTO)
    "bnj10e.bin":  4096,                          # gfx2 prima meta'  (sfondo)
    "bnj10f.bin":  4096,                          # gfx2 seconda meta'
}

if not _os.path.exists(ZIP):
    sys.exit("ERRORE: manca %s" % ZIP)
z = zipfile.ZipFile(ZIP)
dentro = {n.lower(): n for n in z.namelist()}
manca = [n for n in ATTESI if n.lower() not in dentro]
if manca:
    sys.exit("ERRORE: nella zip mancano: %s" % ", ".join(sorted(manca)))


def load(nome):
    d = z.read(dentro[nome.lower()])
    if len(d) != ATTESI[nome]:
        sys.exit("ERRORE: %s e' %d byte, ne servono %d" % (nome, len(d), ATTESI[nome]))
    return d


print("%s: %d file, dimensioni verificate" % (ZIP, len(ATTESI)))

# ======================================================== 1. le due CPU 6502
cpu_rom = load("brubber.12c") + load("brubber.12d")     # 16 KB, $C000-$FFFF
snd_rom = load("bnj6c.bin")                             # 4 KB

# ⭐ Prova indipendente che la zip sia il set giusto: il 6502 prende il vettore
# di RESET da $FFFC/$FFFD, cioe' dagli ultimi byte della ROM principale, e deve
# puntare DENTRO la ROM ($C000-$FFFF).
reset_vec = cpu_rom[0x3FFC] | (cpu_rom[0x3FFD] << 8)
if not (0xC000 <= reset_vec <= 0xFFFF):
    sys.exit("ERRORE: il vettore di reset e' $%04X, fuori dalla ROM: la zip non e' "
             "il set giusto (o i due file sono scambiati)." % reset_vec)
# ⚠️ niente caratteri non-ASCII nei print: su Windows la console e' cp1252 e un
# em-dash uscirebbe come un rombo (o farebbe crashare lo script in un .bat con
# `set -e`) — [[feedback_python_converters]].
print("  CPU principale 16 KB ($C000-$FFFF), vettore di reset $%04X -> dentro la ROM"
      % reset_vec)
print("  CPU sonora 4 KB")


# ======================================================== 2. decodifica grafica
# ⚠️ In tutta questa sezione l'estrazione dei bit e' **MSB-FIRST**, come
# `readbit()` di MAME: il pixel 0 e' il bit 7. Vedi il punto 1 in testa al file.

def decode_tiles_8x8(p0, p1, p2):
    """Caratteri 8x8 a 3 piani (`gfx_8x8x3_planar`): 8 byte per tile per piano.
    Il piano elencato per PRIMO da MAME e' `RGN_FRAC(2,3)` = bnj4h = bit ALTO."""
    tiles = []
    for t in range(len(p0) // 8):
        tile = []
        for row in range(8):
            b0, b1, b2 = p0[t * 8 + row], p1[t * 8 + row], p2[t * 8 + row]
            tile.append([((b0 >> bit) & 1) | (((b1 >> bit) & 1) << 1)
                         | (((b2 >> bit) & 1) << 2) for bit in range(7, -1, -1)])
        tiles.append(tile)
    return tiles


def decode_sprites_16x16(p0, p1, p2):
    """Sprite 16x16 a 3 piani (`tile16layout`), 32 byte per sprite per piano.
    ⚠️ `xoffset = { STEP8(16*8,1), STEP8(0,1) }`: la meta' DESTRA del dato viene
    per prima, cioe' i pixel 0..7 stanno nel byte a +16 e i pixel 8..15 in +0."""
    BPS = 32
    sprites = []
    for s in range(len(p0) // BPS):
        base = s * BPS
        sprite = []
        for row in range(16):
            r0, r1, r2 = p0[base + 16 + row], p1[base + 16 + row], p2[base + 16 + row]
            l0, l1, l2 = p0[base + row],      p1[base + row],      p2[base + row]
            px = [((r0 >> b) & 1) | (((r1 >> b) & 1) << 1) | (((r2 >> b) & 1) << 2)
                  for b in range(7, -1, -1)]
            px += [((l0 >> b) & 1) | (((l1 >> b) & 1) << 1) | (((l2 >> b) & 1) << 2)
                   for b in range(7, -1, -1)]
            sprite.append(px)
        sprites.append(sprite)
    return sprites


def decode_bg_tiles(rom):
    """Sfondo 16x16 a 3 piani (`bnj_tile16layout`), 64 byte per tile.
    ⛔⛔ I piani NON sono tre ROM: sono NIBBLE di due meta' della stessa regione.
       planes = { RGN_FRAC(1,2)+4, RGN_FRAC(0,2)+0, RGN_FRAC(0,2)+4 }
         bit 2 (ALTO)  = seconda meta', nibble BASSO
         bit 1         = prima meta',   nibble ALTO
         bit 0 (BASSO) = prima meta',   nibble BASSO
    ⛔ E la `xoffset` sono QUATTRO gruppi da 4 pixel a 48/32/16/0 byte, al
       contrario: `{ STEP4(3*128,1), STEP4(2*128,1), STEP4(1*128,1), STEP4(0,1) }`."""
    half = len(rom) // 2
    prima, seconda = rom[:half], rom[half:]
    BPT = 64
    tiles = []
    for t in range(half // BPT):
        base = t * BPT
        tile = []
        for row in range(16):
            px = []
            for gruppo in range(4):
                off = base + (3 - gruppo) * 16 + row
                bp = prima[off]   if off < len(prima)   else 0
                bs = seconda[off] if off < len(seconda) else 0
                for bit in range(4):                       # MSB-first dentro il nibble
                    p1 = (bp >> (7 - bit)) & 1             # nibble ALTO della prima
                    p2 = (bp >> (3 - bit)) & 1             # nibble BASSO della prima
                    p0 = (bs >> (3 - bit)) & 1             # nibble BASSO della seconda
                    px.append((p0 << 2) | (p1 << 1) | p2)
            tile.append(px)
        tiles.append(tile)
    return tiles


def ruota_270(t):
    """270 in senso orario (= 90 antiorario), per il display ROT270.
    ⭐ Si applica a CARATTERI e SPRITE, **non** allo sfondo."""
    n = len(t)
    return [[t[c][n - 1 - r] for c in range(n)] for r in range(n)]


def flip_y(s): return s[::-1]
def flip_x(s): return [r[::-1] for r in s]


def pack8(px):
    """8 pixel a 3 bit in una ulong, 4 bit per pixel, il pixel 0 nei bit bassi."""
    v = 0
    for i in range(8):
        v |= (px[i] & 7) << (i * 4)
    return v


def pack16(px):
    return pack8(px[:8]), pack8(px[8:])


gfx1 = (load("bnj4e.bin"), load("bnj4f.bin"), load("bnj4h.bin"))

tiles = [ruota_270(t) for t in decode_tiles_8x8(*gfx1)]
print("  caratteri: %d x 8x8 3bpp, pre-ruotati 270" % len(tiles))

sprites = [ruota_270(s) for s in decode_sprites_16x16(*gfx1)]
print("  sprite: %d x 16x16 3bpp, pre-ruotati 270, x4 orientamenti" % len(sprites))

bg = decode_bg_tiles(load("bnj10e.bin") + load("bnj10f.bin"))
print("  sfondo: %d x 16x16 3bpp, **NON** pre-ruotati" % len(bg))


# ======================================================== 3. scrittura header
def t_byte(sym, dati):
    r = ["// Auto-generated by romconv_brubber.py\n",
         "const unsigned char %s[] PROGMEM = {\n" % sym]
    for i in range(0, len(dati), 16):
        r.append("  " + ", ".join("0x%02X" % b for b in dati[i:i + 16]) + ",\n")
    r.append("};\n")
    return "".join(r)


def t_tilemap():
    r = ["// Auto-generated by romconv_brubber.py\n",
         "#define BRUBBER_NUM_TILES %d\n\n" % len(tiles),
         "const unsigned long brubber_tilemap[%d][8] PROGMEM = {\n" % len(tiles)]
    for t in tiles:
        r.append("  { " + ", ".join("0x%08X" % pack8(t[row]) for row in range(8)) + " },\n")
    r.append("};\n")
    return "".join(r)


def t_spritemap():
    r = ["// Auto-generated by romconv_brubber.py\n",
         "#define BRUBBER_NUM_SPRITES %d\n\n" % len(sprites),
         "const unsigned long brubber_spritemap[4][%d][32] PROGMEM = {\n" % len(sprites)]
    # ⚠️ L'ordine degli orientamenti e' quello che legge `brubber.cpp`:
    #    0 = normale, 1 = flipY, 2 = flipX, 3 = entrambi
    # (in `bb_extract_sprites` il bit1 dell'attributo e' flipY -> bit 0 del flag,
    #  il bit2 e' flipX -> bit 1 del flag).
    varianti = (lambda s: s, flip_y, lambda s: flip_x(s), lambda s: flip_x(flip_y(s)))
    for orient in range(4):
        r.append("  {\n")
        for s in sprites:
            d = varianti[orient](s)
            riga = []
            for row in range(16):
                lo, hi = pack16(d[row])
                riga.append("0x%08X,0x%08X" % (lo, hi))
            r.append("    { " + ", ".join(riga) + " },\n")
        r.append("  },\n")
    r.append("};\n")
    return "".join(r)


def t_bgmap():
    r = ["// Auto-generated by romconv_brubber.py\n",
         "#define BRUBBER_NUM_BG_TILES %d\n\n" % len(bg),
         "const unsigned long brubber_bgmap[%d][32] PROGMEM = {\n" % len(bg)]
    for t in bg:
        riga = []
        for row in range(16):
            lo, hi = pack16(t[row])
            riga.append("0x%08X,0x%08X" % (lo, hi))
        r.append("  { " + ", ".join(riga) + " },\n")
    r.append("};\n")
    return "".join(r)


# ⭐ I DIP li scrive questo script, non si toccano a mano: se si rigenerano gli
# header senza rigenerare questo, tornano i due difetti gia' pagati
# ([[brubber_dsw_lives_fix]], [[feedback_sync_py]]).
DIPS = """// Auto-generated by romconv_brubber.py
// Burnin' Rubber DIP switch defaults (active-low unless noted)
//
// DSW1 ($1000 read):
//   bit 0-1: Coin A (11=1C/1Cr, 00=2C/1Cr, 10=1C/2Cr, 01=1C/3Cr)
//   bit 2-3: Coin B (same encoding)
//   bit 4-5: unused (keep high)
//   bit 6:   Cabinet (1=upright, 0=cocktail) - MAME default: upright
//   bit 7:   VBLANK (ACTIVE_HIGH, read-only from hardware - handled dynamically)
//
// NOTE: bit 7 is NOT a DIP switch - it's the VBLANK signal.
// brubber.cpp handles bit 7 dynamically via vblank_flag.
// This define covers bits 0-6 only (bit 7 masked off in code).
// FIX: 0x80 dava 2 coin per 1 credito perche' bit 0-1 erano entrambi 0.
#define BRUBBER_DSW1  0x43  // 1C/1Cr (bit0-1=11) + UPRIGHT (bit6)

// DSW2 ($1001 read) - il bootleg `brubber` ha la logica INVERTITA rispetto al
// `bnj` originale: con 0xF7 i bit 3-4 valevano "VITE INFINITE" (cheat tipico dei
// bootleg) e la macchina non perdeva mai una vita.
#define BRUBBER_DSW2  0x00  // 3 lives, normal difficulty (brubber bootleg)
"""

FILE = [
    ("brubber_rom.h",         t_byte("brubber_rom", cpu_rom)),
    ("brubber_snd_rom.h",     t_byte("brubber_snd_rom", snd_rom)),
    ("brubber_tilemap.h",     t_tilemap()),
    ("brubber_spritemap.h",   t_spritemap()),
    ("brubber_bgmap.h",       t_bgmap()),
    ("brubber_dipswitches.h", DIPS),
]


def valori(testo):
    """Tutti i numeri DOPO la prima graffa, saltati i commenti. Decimali o
    esadecimali: si confrontano i VALORI, non l'impaginato."""
    i = testo.find("{")
    if i < 0:
        i = 0
    corpo = re.sub(r"//[^\n]*", " ", testo[i:])
    corpo = re.sub(r"/\*.*?\*/", " ", corpo, flags=re.S)
    return [int(x, 0) for x in re.findall(r"0[xX][0-9a-fA-F]+|\d+", corpo)]


def valori_dip(testo):
    """Per i DIP la graffa non c'e': si prendono i due #define."""
    return [int(m, 0) for m in re.findall(r"#define\s+BRUBBER_DSW\d\s+(0[xX][0-9a-fA-F]+)",
                                          testo)]


if VERIFICA:
    uguali = diversi = 0
    for nome, corpo in FILE:
        p = _os.path.join(OUT, nome)
        if not _os.path.exists(p):
            print("  %-24s MANCA nel progetto" % nome)
            diversi += 1
            continue
        testo_suo = open(p, encoding="utf-8", errors="replace").read()
        est = valori_dip if nome.endswith("dipswitches.h") else valori
        mio, suo = est(corpo), est(testo_suo)
        if mio == suo:
            print("  %-24s IDENTICO all'header del porting che gira (%d valori)"
                  % (nome, len(mio)))
            uguali += 1
        else:
            n = min(len(mio), len(suo))
            primo = next((k for k in range(n) if mio[k] != suo[k]), n)
            print("  %-24s DIVERSO: %d valori contro %d, primo scarto all'indice "
                  "%d (mio %X, suo %X)"
                  % (nome, len(mio), len(suo), primo,
                     mio[primo] if primo < len(mio) else 0,
                     suo[primo] if primo < len(suo) else 0))
            diversi += 1
    print("\n%d identici, %d diversi." % (uguali, diversi))
    sys.exit(1 if diversi else 0)

os.makedirs(OUT, exist_ok=True)
for nome, corpo in FILE:
    with open(_os.path.join(OUT, nome), "w", encoding="utf-8") as f:
        f.write(corpo)
    print("  scritto %s" % nome)
print("\nFatto. Il logo del menu si rifa' con convert_brubber_logo.py.")
