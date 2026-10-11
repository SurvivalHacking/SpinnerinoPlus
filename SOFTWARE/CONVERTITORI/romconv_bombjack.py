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
# romconv_bombjack.py — da `ROMS/bombjack.zip` agli header di BOMB JACK
# (Tehkan 1984, driver MAME `src/mame/tecmo/bombjack.cpp`).
#
# ⭐⭐ COME SI VERIFICA: con `--verifica` non scrive niente e confronta cio' che
# produce con gli header gia' nel progetto — quelli copiati dal porting **che
# gira** su SPINNERINO PLUS ([[feedback_verify_against_source_not_copy]]).
#
# ⭐ Sul PC la catena esisteva ma SPEZZATA IN SEI SCRIPT separati
# (`cpu_conv.py`, `audio_cpu_conv.py`, `bgmaps_conv.py`, `tiles_bg_conv.py`,
# `tiles_fg_conv.py`, `sprites_conv.py`), ognuno coi suoi percorsi fissi. Qui
# stanno in uno solo, con la zip come unica sorgente: sei file da tenere
# allineati sono sei occasioni di divergere ([[feedback_python_converters]]).
#
# LE COSE CHE NON SI INDOVINANO
# 1. La CPU principale vede **64 KB** con un BUCO: 0x0000-0x7FFF ROM,
#    0x8000-0xBFFF RAM/IO (riempito di 0xFF), 0xC000-0xDFFF ancora ROM.
# 2. La mappa dello sfondo e' una ROM da 4 KB **duplicata** a 8 KB: nel driver
#    c'e' un `ROM_RELOAD`.
# 3. Tutta la grafica e' a **3 piani** (3 bit per pixel, 8 colori) impacchettati
#    3 bit alla volta in `unsigned long`. ⚠️ L'ordine dei piani NON e' lo stesso
#    per tile e sprite: per i tile il primo file e' il bit ALTO, per gli sprite
#    il primo e' il bit BASSO. Sbagliarlo non da' nessun errore, da' i colori
#    scambiati.
# 4. I tile 16x16 dello sfondo hanno le righe 8..15 spostate di 16 byte
#    (`row_byte_offset = (y % 8) + (16 if y >= 8 else 0)`), e gli sprite 32x32
#    saltano di 8 e di 32 byte a meta' riga: sono le macro GATHER dell'hardware.
#
# USO:  python romconv_bombjack.py            (rigenera gli header)
#       python romconv_bombjack.py --verifica (non scrive: confronta e basta)
# ============================================================================
import os, re, sys, zipfile

VERIFICA = "--verifica" in sys.argv

ZIP = _os.path.join("ROMS", "bombjack.zip")
OUT = _os.path.join("machines", "bombjack")

ATTESI = {
    "01_h03t.bin": 8192,   # CPU audio
    "02_p04t.bin": 4096,   # mappa dello sfondo (poi duplicata)
    "03_e08t.bin": 4096, "04_h08t.bin": 4096, "05_k08t.bin": 4096,   # tile 8x8
    "06_l08t.bin": 8192, "07_n08t.bin": 8192, "08_r08t.bin": 8192,   # tile 16x16
    "09_j01b.bin": 8192, "10_l01b.bin": 8192, "11_m01b.bin": 8192,
    "12_n01b.bin": 8192, "13.1r": 8192,                              # CPU principale
    "14_j07b.bin": 8192, "15_l07b.bin": 8192, "16_m07b.bin": 8192,   # sprite
}

if not _os.path.exists(ZIP):
    sys.exit("ERRORE: manca %s" % ZIP)
z = zipfile.ZipFile(ZIP)
dentro = {n.lower(): n for n in z.namelist()}
manca = [n for n in ATTESI if n.lower() not in dentro]
if manca:
    sys.exit("ERRORE: nella zip mancano: %s" % ", ".join(manca))


def load(nome):
    d = z.read(dentro[nome.lower()])
    if len(d) != ATTESI[nome]:
        sys.exit("ERRORE: %s e' %d byte, ne servono %d" % (nome, len(d), ATTESI[nome]))
    return d


print("%s: %d file, dimensioni verificate" % (ZIP, len(ATTESI)))

# ============================================================ 1. CPU principale
cpu1 = bytearray([0xFF] * 0x10000)
cpu1[0x0000:0x2000] = load("09_j01b.bin")
cpu1[0x2000:0x4000] = load("10_l01b.bin")
cpu1[0x4000:0x6000] = load("11_m01b.bin")
cpu1[0x6000:0x8000] = load("12_n01b.bin")
# ⚠️ Il buco 0x8000-0xBFFF resta a 0xFF: li' ci sono RAM e I/O, non ROM.
cpu1[0xC000:0xE000] = load("13.1r")
cpu1 = bytes(cpu1)

# ⭐ Prova indipendente: la Z80 parte da 0x0000. Se la zip fosse un altro set,
# il primo byte non sarebbe un'istruzione plausibile di avvio.
if cpu1[0] == 0xFF:
    sys.exit("ERRORE: la ROM principale comincia con FF: la zip non e' il set giusto.")
print("  CPU principale: 64 KB con il buco 0x8000-0xBFFF a 0xFF")

cpu2 = bytearray([0xFF] * 0x4000)
cpu2[0x0000:0x2000] = load("01_h03t.bin")
cpu2 = bytes(cpu2)
print("  CPU audio: 16 KB (8 KB di ROM piu' riempimento)")

# ============================================================ 2. mappa sfondo
# ROM_RELOAD nel driver: 4 KB duplicati a 8 KB.
bg_maps = load("02_p04t.bin") * 2
print("  mappa dello sfondo: 4096 byte duplicati a %d (ROM_RELOAD)" % len(bg_maps))

# ============================================================ 3. tile 8x8 (fg)
# ⚠️ ORDINE DEI PIANI: il PRIMO file e' il bit ALTO (valore 4).
fg0, fg1, fg2 = load("03_e08t.bin"), load("04_h08t.bin"), load("05_k08t.bin")
N_FG = len(fg0) // 8


def fg_tile(t):
    off = t * 8
    m = [[(4 if fg0[off + y] & (1 << (7 - x)) else 0) |
          (2 if fg1[off + y] & (1 << (7 - x)) else 0) |
          (1 if fg2[off + y] & (1 << (7 - x)) else 0)
          for x in range(8)] for y in range(8)]
    # ⛔⛔ I TILE DEI CARATTERI SONO PRE-RUOTATI DI 90 IN SENSO ORARIO, gli altri
    # no. Non e' una stranezza del convertitore: e' che i tre blitter del gioco
    # trattano i dati in modo diverso — `bj_blit_bg` e `bj_blit_sprite` ruotano
    # NEL CAMPIONAMENTO (`source_x = dest_y`...), mentre `bj_blit_fg` legge la
    # riga dritta. Quindi la rotazione dei caratteri deve stare QUI.
    # ⭐ Trovato confrontando: senza rotazione il primo tile non vuoto (il 12)
    # gia' non tornava. Provate le tre possibilita', l'oraria da' **512 tile su
    # 512 identici** ([[feedback_verify_against_source_not_copy]]).
    m = [[m[7 - x][y] for x in range(8)] for y in range(8)]
    righe = []
    for riga in m:
        val = 0
        for p in riga:
            val = (val << 3) | p
        righe.append(val)
    return righe


fg_tiles = [fg_tile(t) for t in range(N_FG)]
print("  tile 8x8: %d, tre piani, 3 bit per pixel" % N_FG)

# ============================================================ 4. tile 16x16 (bg)
bg0, bg1, bg2 = load("06_l08t.bin"), load("07_n08t.bin"), load("08_r08t.bin")
N_BG = len(bg0) // 32


def bg_tile(t):
    base = t * 32
    fuori = []
    for y in range(16):
        # ⚠️ le righe 8..15 stanno 16 byte piu' avanti: e' la disposizione
        # dell'hardware, non un capriccio
        o = base + (y % 8) + (16 if y >= 8 else 0)
        m0 = (bg0[o] << 8) | bg0[o + 8]
        m1 = (bg1[o] << 8) | bg1[o + 8]
        m2 = (bg2[o] << 8) | bg2[o + 8]
        v1 = v2 = 0
        for x in range(16):
            bit = 15 - x
            pen = (4 if m0 & (1 << bit) else 0) | \
                  (2 if m1 & (1 << bit) else 0) | \
                  (1 if m2 & (1 << bit) else 0)
            if x < 8: v1 = (v1 << 3) | pen
            else:     v2 = (v2 << 3) | pen
        fuori.append(v1); fuori.append(v2)
    return fuori


bg_tiles = [bg_tile(t) for t in range(N_BG)]
print("  tile 16x16: %d" % N_BG)

# ============================================================ 5. sprite
# ⚠️ ORDINE OPPOSTO a quello dei tile: qui il PRIMO piano e' il bit BASSO.
sp = [load("14_j07b.bin"), load("15_l07b.bin"), load("16_m07b.bin")]


def sprite(size, idx):
    off = idx * (size * size // 8)
    fuori = []
    for y in range(size):
        if size == 16:
            m0 = (sp[2][off] << 8) | sp[2][off + 8]
            m1 = (sp[1][off] << 8) | sp[1][off + 8]
            m2 = (sp[0][off] << 8) | sp[0][off + 8]
        else:
            m0 = (sp[2][off] << 24) | (sp[2][off + 8] << 16) | (sp[2][off + 32] << 8) | sp[2][off + 40]
            m1 = (sp[1][off] << 24) | (sp[1][off + 8] << 16) | (sp[1][off + 32] << 8) | sp[1][off + 40]
            m2 = (sp[0][off] << 24) | (sp[0][off + 8] << 16) | (sp[0][off + 32] << 8) | sp[0][off + 40]

        riga = []
        for x in range(size):
            bit = size - 1 - x
            riga.append((((m2 >> bit) & 1) << 0) |
                        (((m1 >> bit) & 1) << 1) |
                        (((m0 >> bit) & 1) << 2))
        for c0 in range(0, size, 8):
            val = 0
            for p in riga[c0:c0 + 8]:
                val = (val << 3) | p
            fuori.append(val)

        # avanzamento dell'offset: sono le macro GATHER dell'hardware
        off += 1
        if size == 16:
            if y == 7: off += 8
        else:
            if (y & 7) == 7:  off += 8
            if (y & 15) == 15: off += 32
    return fuori


spr16 = [sprite(16, i) for i in range(256)]
spr32 = [sprite(32, i) for i in range(64)]
print("  sprite: 256 da 16x16 e 64 da 32x32")

# ============================================================ 6. testi e uscita
def t_byte(nome, dati):
    r = ["const unsigned char %s[%d] = {\n  " % (nome, len(dati))]
    for i, b in enumerate(dati):
        r.append("0x%02X" % b)
        if i < len(dati) - 1:
            r.append(",")
            if (i + 1) % 16 == 0: r.append("\n  ")
        else:
            r.append("\n")
    r.append("};")
    return "".join(r)


def t_long(nome, righe, per_voce):
    r = ["const unsigned long %s[%d][%d] = {\n" % (nome, len(righe), per_voce)]
    for v in righe:
        r.append("  { " + ",".join("0x%06X" % x for x in v) + " },\n")
    r.append("};\n")
    return "".join(r)


FILE = [
    ("bombjack_rom1.h",    t_byte("bombjack_rom_cpu1", cpu1)),
    ("bombjack_rom2.h",    t_byte("bombjack_rom_cpu2", cpu2)),
    ("bombjack_bg_maps.h", t_byte("bombjack_bg_maps", bg_maps)),
    ("bombjack_fg_tiles.h", t_long("bombjack_fg_tiles", fg_tiles, 8)),
    ("bombjack_bg_tiles.h", t_long("bombjack_bg_tiles", bg_tiles, 32)),
    ("bombjack_sprites.h",  t_long("bombjack_sprites_16x16", spr16, 32) + "\n" +
                            t_long("bombjack_sprites_32x32", spr32, 128)),
]


def valori(testo):
    """Tutti i numeri DOPO la prima graffa, saltati i commenti."""
    i = testo.find("{")
    corpo = re.sub(r"//[^\n]*", " ", testo[i:])
    corpo = re.sub(r"/\*.*?\*/", " ", corpo, flags=re.S)
    return [int(x, 0) for x in re.findall(r"0[xX][0-9a-fA-F]+", corpo)]


if VERIFICA:
    uguali = diversi = 0
    for nome, corpo in FILE:
        p = _os.path.join(OUT, nome)
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
        f.write("// Generato da romconv_bombjack.py - NON modificare a mano.\n")
        f.write(corpo)
    print("  scritto %s" % nome)
print("\nFatto. Il logo del menu si rifa' con convert_bombjack_logo.py.")
