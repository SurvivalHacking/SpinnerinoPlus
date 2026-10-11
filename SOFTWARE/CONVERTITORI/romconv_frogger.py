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
# romconv_frogger.py - da `ROMS/frogger.zip` agli header PROGMEM di
#                      `machines/frogger/`.
#
# Set MAME `frogger` (Konami 1981), driver `src/mame/galaxian/galaxian.cpp`.
# ROM_START e CRC copiati riga per riga dalla fonte.
#
# ⭐⭐ UN SOLO SCRIPT AL POSTO DI QUATTRO. Il porting galagino generava questi
# header con `conv_frogger.bat`, che chiama `romconv.py`, `tileconv.py`,
# `spriteconv.py` e `cmapconv.py` con argomenti diversi. Qui i quattro passaggi
# sono trascritti in un file solo (come gia' fatto per Bomb Jack), cosi' non
# serve piu' l'albero di galagino per rigenerare i dati.
#
# ⭐⭐ COME SI VERIFICA (e perche' vale un flash):
#     python romconv_frogger.py --verifica
# riproduce gli header del porting **SPINNERINO PLUS**, che gira su hardware
# vero, e li confronta valore per valore. Un solo confronto prova TRE cose:
# che la zip e' il set giusto, che ogni passaggio di decodifica e' trascritto
# bene, e che i dati che finiranno nel CABINATONE sono quelli gia' visti girare.
# ([[feedback_verify_against_source_not_copy]])
#
# ⛔⛔ UN header NON puo' essere identico, e non e' una svista:
#   · `frogger_cmap.h` - la' i valori RGB565 sono **BYTE-SWAPPED** (galagino li
#     preparava per il display SPI dell'ili9341: `cmapconv.py` fa
#     `rgbs = ((rgb & 0xff00) >> 8) + ((rgb & 0xff) << 8)`). Il pannello DSI del
#     CABINATONE li vuole NATIVI. Stessa trappola di 1942, Bomb Jack, Burnin'
#     Rubber e Nibbler.
#     ⭐ La verifica c'e' lo stesso ed e' piu' forte di un confronto diretto: si
#     RIAPPLICA lo scambio ai nostri valori e si confronta con SPINNERINO. Se
#     combacia, il decode dalla PROM e' identico e l'unica differenza e' lo swap.
#
# ⚠️ Il logo del menu NON si genera da qui: sta sulla SD e lo fa
#    `make_arcade_logos.py` -> `DA COPIARE SU SD/arcade/frogger.scr`.
# ============================================================================
import sys, zipfile, binascii

ZIP  = _os.path.join("ROMS", "frogger.zip")
OUT  = _os.path.join("machines", "frogger")
SPIN = _os.path.join("C:\\", "Users", "Tecnico", "Desktop", "PROGETTI SH",
                     "SPINNERINO PLUS WIFI", "BACKUP SPINNERINO PLUS",
                     "SPINNERINO", "machines", "frogger")

# ---------------------------------------------------------------- ROM_START
# src/mame/galaxian/galaxian.cpp, ROM_START( frogger ).
# (nome, dimensione, CRC32)
MAINCPU = [("frogger.26", 0x1000, 0x597696D6),
           ("frogger.27", 0x1000, 0xB6E6FCC3),
           ("frsm3.7",    0x1000, 0xACA22AE0)]
AUDIOCPU = [("frogger.608", 0x0800, 0xE8AB0256),
            ("frogger.609", 0x0800, 0x7380A48F),
            ("frogger.610", 0x0800, 0x31D7EB27)]
# ⚠️ L'ORDINE QUI E' QUELLO DI GALAGINO, NON QUELLO DI MAME.
# MAME carica gfx1 come 607 a 0x0000 e 606 a 0x0800; galagino passa invece
# (606, 607) ai suoi convertitori e ne ricava lo stesso risultato perche' il
# formato dei tile e' «due ROM, un piano ciascuno» e lui li nomina al contrario.
# Non e' un dettaglio da indovinare: e' il motivo per cui la verifica contro gli
# header che GIRANO vale piu' di una lettura del driver.
GFX = [("frogger.606", 0x0800, 0xF524EE30),
       ("frogger.607", 0x0800, 0x05F7D883)]
PROM = ("pr-91.6l", 0x0020, 0x413703BF)


def leggi(z, nome, ln, crc):
    try:
        d = z.read(nome)
    except KeyError:
        sys.exit("ERRORE: manca %s dentro %s" % (nome, ZIP))
    if len(d) != ln:
        sys.exit("ERRORE: %s e' %d byte, ne servono %d" % (nome, len(d), ln))
    c = binascii.crc32(d) & 0xFFFFFFFF
    if c != crc:
        sys.exit("ERRORE: %s ha CRC %08X, atteso %08X (set sbagliato)" % (nome, c, crc))
    return d


# ============================================================ codice delle CPU
# ⭐ La prima ROM della CPU sonora ha **D0 e D1 SCAMBIATI**. MAME lo fa in
# `decode_frogger_sound()` sui primi 0x800 byte; galagino lo fa riconoscendo la
# firma dei primi 8 byte. Qui si applica come dice MAME — sul PRIMO file
# dell'audiocpu — e la verifica contro l'header di SPINNERINO conferma che le
# due strade danno lo stesso risultato.
def swap_d0d1(b):
    return bytes(((x & 0xFC) | ((x & 2) >> 1) | ((x & 1) << 1)) for x in b)


def header_rom(nome_array, dati, descr):
    r = ["// " + descr,
         "// Rigenerato da romconv_frogger.py - non modificare a mano.",
         "const unsigned char %s[] PROGMEM = {" % nome_array]
    for i in range(0, len(dati), 16):
        r.append("  " + ", ".join("0x%02X" % b for b in dati[i:i + 16]) + ",")
    r.append("};")
    return "\n".join(r) + "\n"


# ================================================================ caratteri
# Trascritto da `tileconv.py::parse_charmap_frogger` + `parse_chr_2` + `dump_chr`.
def parse_chr_2(d0, d1):
    car = []
    for y in range(8):
        riga = []
        for x in range(8):
            c0 = 1 if d0[7 - x] & (0x80 >> y) else 0
            c1 = 2 if d1[7 - x] & (0x80 >> y) else 0
            riga.append(c0 + c1)
        car.append(riga)
    return car


def dump_chr(car):
    out = []
    for y in range(8):
        v = 0
        for x in range(8):
            v = (v >> 2) + (car[y][x] << (16 - 2))
        out.append(v)
    return out


def genera_tilemap(g0, g1):
    """g0 = frogger.606, g1 = frogger.607 (ordine di galagino)."""
    def bit01(a):
        return (a & 0xFFFC) | ((a & 2) >> 1) | ((a & 1) << 1)
    # ⚠️ galagino decide lo scambio ANNUSANDO i primi 16 byte: se le due ROM
    # sono uguali a meno di D0/D1, applica lo scambio alla prima. Si trascrive
    # com'e', perche' e' il codice che ha prodotto i dati che girano.
    swap = all(bit01(g0[i]) == g1[i] for i in range(16))
    if swap:
        g0 = bytes(bit01(x) for x in g0)
    # ⚠️ **256 caratteri, non 1024.** Il `parse_charmap_frogger` di galagino
    # scrive `range(1024)`, che con due ROM da 2 KB uscirebbe dal buffer: il
    # numero vero lo dicono i DATI — l'header che gira ha 2048 valori = 256 x 8.
    # (Un conto sbagliato nello script d'origine, non nel risultato.)
    caratteri = []
    for c in range(256):
        caratteri.append(parse_chr_2(g0[8 * c:8 * (c + 1)], g1[8 * c:8 * (c + 1)]))
    return [dump_chr(c) for c in caratteri], swap


def header_tilemap(tiles):
    r = ["// Frogger - caratteri 8x8 a 2 bit, 1024 voci (2 pixel per bit-coppia).",
         "// Rigenerato da romconv_frogger.py - non modificare a mano.",
         "const unsigned short frogger_tilemap[][8] PROGMEM = {"]
    r.append(",\n".join(" { " + ",".join(hex(v) for v in t) + " }" for t in tiles))
    r.append("};")
    return "\n".join(r) + "\n"


# ================================================================== sprite
# Trascritto da `spriteconv.py::parse_sprite_frogger` + `dump_sprite`.
def parse_sprite_frogger(d0, d1):
    d0 = swap_d0d1(d0)          # D0/D1 scambiati nella prima ROM
    spr = []
    for y in range(16):
        riga = []
        for x in range(16):
            ym = y & 7 | ((x & 8) ^ 8)
            xm = x & 7 | (y & 8)
            c0 = 1 if d0[(xm ^ 7) + ((ym & 8) << 1)] & (0x80 >> (ym & 7)) else 0
            c1 = 2 if d1[(xm ^ 7) + ((ym & 8) << 1)] & (0x80 >> (ym & 7)) else 0
            riga.append(c0 + c1)
        spr.append(riga)
    return spr


def dump_sprite(s, flip_x, flip_y):
    out = []
    for y in (range(16) if not flip_y else reversed(range(16))):
        v = 0
        for x in range(16):
            if not flip_x:
                v = (v >> 2) + (s[y][x] << (32 - 2))
            else:
                v = (v << 2) + s[y][x]
        out.append(v)
    return out


def genera_spritemap(g0, g1):
    sprites = []
    for i in range(64):
        sprites.append(parse_sprite_frogger(g0[32 * i:32 * (i + 1)],
                                            g1[32 * i:32 * (i + 1)]))
    return sprites


def header_spritemap(sprites):
    r = ["// Frogger - sprite 16x16 a 2 bit, 64 voci x 4 ORIENTAMENTI.",
         "// L'ordine dei quattro blocchi e' quello di galagino:",
         "//   0 = dritto, 1 = specchiato in Y, 2 = specchiato in X, 3 = tutti e due.",
         "// Rigenerato da romconv_frogger.py - non modificare a mano.",
         "const unsigned long frogger_sprites[][%d][16] PROGMEM = {" % len(sprites)]
    blocchi = []
    for fx, fy in ((False, False), (False, True), (True, False), (True, True)):
        corpi = ["  { " + ",".join(hex(v) for v in dump_sprite(s, fx, fy)) + " }"
                 for s in sprites]
        blocchi.append(" {\n" + ",\n".join(corpi) + "\n }")
    r.append(",\n".join(blocchi))
    r.append("};")
    return "\n".join(r) + "\n"


# =============================================================== tavolozza
# La PROM ha 32 voci a 8 bit nel formato **bbgggrrr**.
#
# ⛔⛔ QUI IL PORTING DI PARTENZA E' DICHIARATAMENTE APPROSSIMATO, e lo dice da
# solo. `cmapconv.py` di galagino pesa i tre bit 4/7, 2/7 e 1/7, col commento:
#     «This doesn't 100% match the weighting each bit has on the real machine
#      with 1000, 470 and 220 ohms resistors. Here the weighting is 4/7, 2/7 and
#      1/7 while on the real thing it's 4.15/7, 1.94/7 and 0.91/7.»
# ✅ Qui si usano i pesi VERI, calcolati come li calcola MAME
# (`galaxian_v.cpp::galaxian_palette` -> `compute_resistor_weights` con la scala
# a `RGB_MAXIMUM = 224` e un pull-down da 470 ohm). Non e' un capriccio: con
# l'approssimazione il titolo NON combacia con lo screenshot MAME, con questi
# SI'. E' [[feedback_verify_against_source_not_copy]] — la fonte
# dell'EMULAZIONE e' il driver, anche quando il porting di partenza gira.
#
# ⭐ L'approssimazione resta disponibile (`pesi="galagino"`) ed e' quella che
# `--verifica` usa per il confronto con SPINNERINO: cosi' si prova AL TEMPO
# STESSO che la trascrizione e' fedele e che il cambio di pesi e' l'unica
# differenza voluta, oltre al byte-swap.
RGB_MAXIMUM = 224.0


def _pesi(res, pulldown):
    g = [1.0 / r for r in res]
    tot = sum(g) + (1.0 / pulldown if pulldown else 0.0)
    return [x / tot for x in g]


_RG = _pesi([1000, 470, 220], 470)
_BL = _pesi([470, 220], 470)
_SCALA = RGB_MAXIMUM / sum(_RG)          # scaler = -1.0: normalizza sul massimo
RW = [w * _SCALA for w in _RG]           # -> 29, 62, 133   (somma 224)
BW = [w * _SCALA for w in _BL]           # -> 69, 148       (somma 217)


def prom_a_rgb565(c, swap, pesi="mame"):
    if pesi == "galagino":
        # l'approssimazione del porting di partenza
        r8 = (31 * ((c >> 0) & 0x7) // 7) << 3
        g8 = (63 * ((c >> 3) & 0x7) // 7) << 2
        b8 = (31 * ((c >> 6) & 0x3) // 3) << 3
        v = ((r8 >> 3) << 11) + ((g8 >> 2) << 5) + (b8 >> 3)
    else:
        # MAME: combine_weights = int(somma + 0.5) su 8 bit, poi a RGB565
        r8 = int(sum(RW[i] for i in range(3) if (c >> i) & 1) + 0.5)
        g8 = int(sum(RW[i] for i in range(3) if (c >> (3 + i)) & 1) + 0.5)
        b8 = int(sum(BW[i] for i in range(2) if (c >> (6 + i)) & 1) + 0.5)
        v = ((r8 * 31 // 255) << 11) + ((g8 * 63 // 255) << 5) + (b8 * 31 // 255)
    if swap:
        v = ((v & 0xFF00) >> 8) + ((v & 0xFF) << 8)
    return v


def genera_cmap(prom, swap, pesi="mame"):
    return [prom_a_rgb565(c, swap, pesi) for c in prom]


def header_cmap(val, swap):
    nota = "byte-swapped, per il display SPI" if swap else \
           "NATIVI, come li vuole il pannello DSI"
    r = ["// Frogger - tavolozza: 32 voci da `pr-91.6l`, formato bbgggrrr (%s)." % nota,
         "// Rigenerato da romconv_frogger.py - non modificare a mano.",
         "const unsigned short frogger_colormap[][4] PROGMEM = {"]
    r.append(",\n".join("{" + ",".join(hex(v) for v in val[4 * i:4 * i + 4]) + "}"
                        for i in range(8)))
    r.append("};")
    return "\n".join(r) + "\n"


# --------------------------------------------------------------------- main
def valori(txt):
    """Tutti i letterali numerici dentro le graffe, in ordine. Si confrontano i
    VALORI, non il testo: gli header di SPINNERINO hanno un'impaginazione
    diversa e sono a CRLF ([[feedback_python_converters]])."""
    # ⚠️ Le graffe qui sono ANNIDATE (tile e sprite sono array di array): non si
    # puo' tagliare fra la prima `{` e la prima `}`. Si prendono i letterali.
    import re
    corpo = txt[txt.find("{"):]
    corpo = re.sub(r"//[^\n]*", " ", corpo)
    return [int(t, 0) for t in
            re.findall(r"0[xX][0-9a-fA-F]+|(?<![\w.])\d+(?![\w.])", corpo)]


def confronta(nome, miei, loro_path):
    if not _os.path.isfile(loro_path):
        print("  %-34s SPINNERINO non trovato, salto" % nome)
        return None
    loro = valori(open(loro_path, "r", encoding="utf-8", errors="replace").read())
    if isinstance(miei, str):
        miei = valori(miei)
    if len(miei) != len(loro):
        print("  %-34s DIVERSO: %d valori contro %d" % (nome, len(miei), len(loro)))
        return False
    diff = [k for k in range(len(miei)) if miei[k] != loro[k]]
    if diff:
        print("  %-34s DIVERSO: %d valori su %d (primo all'indice %d: %s vs %s)"
              % (nome, len(diff), len(miei), diff[0], miei[diff[0]], loro[diff[0]]))
        return False
    print("  %-34s IDENTICO (%d valori)" % (nome, len(miei)))
    return True


def main():
    solo_verifica = "--verifica" in sys.argv
    if not _os.path.isfile(ZIP):
        sys.exit("ERRORE: manca %s" % ZIP)
    z = zipfile.ZipFile(ZIP)

    cpu1 = b"".join(leggi(z, n, l, c) for n, l, c in MAINCPU)          # 12288
    a = [leggi(z, n, l, c) for n, l, c in AUDIOCPU]
    cpu2 = swap_d0d1(a[0]) + a[1] + a[2]                               # 6144
    g0 = leggi(z, *GFX[0])                                             # frogger.606
    g1 = leggi(z, *GFX[1])                                             # frogger.607
    prom = leggi(z, *PROM)                                             # 32

    h_rom1 = header_rom("frogger_rom_cpu1", cpu1,
                        "Frogger - codice della CPU principale (12 KB: .26 + .27 + frsm3.7)")
    h_rom2 = header_rom("frogger_rom_cpu2", cpu2,
                        "Frogger - codice della CPU sonora (6 KB: .608 + .609 + .610). "
                        "La PRIMA ROM ha D0 e D1 SCAMBIATI (MAME decode_frogger_sound)")
    tiles, swap_gfx = genera_tilemap(g0, g1)
    h_tile = header_tilemap(tiles)
    h_spr  = header_spritemap(genera_spritemap(g0, g1))
    h_cmap = header_cmap(genera_cmap(prom, swap=False, pesi="mame"), swap=False)
    # ⭐ per la verifica servono i pesi di galagino PIU' lo scambio: cosi' il
    #    confronto isola le DUE differenze volute una alla volta.
    cmap_swap = genera_cmap(prom, swap=True, pesi="galagino")

    print("Confronto con il porting SPINNERINO PLUS (che gira su hardware):")
    ok = True
    ok &= confronta("frogger_rom1.h", h_rom1,
                    _os.path.join(SPIN, "frogger_rom1.h")) is not False
    ok &= confronta("frogger_rom2.h", h_rom2,
                    _os.path.join(SPIN, "frogger_rom2.h")) is not False
    ok &= confronta("frogger_tilemap.h", h_tile,
                    _os.path.join(SPIN, "frogger_tilemap.h")) is not False
    ok &= confronta("frogger_spritemap.h", h_spr,
                    _os.path.join(SPIN, "frogger_spritemap.h")) is not False
    # ⭐ La tavolozza si confronta RIAPPLICANDO lo scambio: se i 32 valori
    # combaciano, il decode dalla PROM e' identico e l'unica differenza e' lo swap.
    ok &= confronta("frogger_cmap.h (scambio + pesi galagino)", cmap_swap,
                    _os.path.join(SPIN, "frogger_cmap.h")) is not False
    print("  (lo scambio D0/D1 sui caratteri e' stato %s)"
          % ("APPLICATO" if swap_gfx else "SALTATO: le due ROM non hanno la firma"))

    if solo_verifica:
        print("\n--verifica: nessun file scritto.")
        return 0 if ok else 1

    for nome, testo in (("frogger_rom1.h", h_rom1), ("frogger_rom2.h", h_rom2),
                        ("frogger_tilemap.h", h_tile), ("frogger_spritemap.h", h_spr),
                        ("frogger_cmap.h", h_cmap)):
        p = _os.path.join(OUT, nome)
        open(p, "w", encoding="utf-8", newline="\n").write(testo)
        print("scritto %s (%d byte)" % (p, _os.path.getsize(p)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
