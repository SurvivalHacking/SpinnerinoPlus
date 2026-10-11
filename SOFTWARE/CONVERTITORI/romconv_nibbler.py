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
# romconv_nibbler.py - da `ROMS/nibbler.zip` agli header PROGMEM di
#                      `machines/nibbler/`.
#
# Set MAME `nibbler` (Rock-Ola 1982, rev 9 set 1), driver `src/mame/snk/snk6502.cpp`.
# L'ordine di caricamento e le regioni sono presi VERBATIM da ROM_START(nibbler),
# non dedotti dai nomi dei file (che NON sono in ordine di indirizzo:
# ic12 sta a 0x3000 e ic7 a 0x4000).
#
# ⭐⭐ COME SI VERIFICA (e perche' vale un flash):
#     python romconv_nibbler.py --verifica
# riproduce gli header del porting **SPINNERINO PLUS**, che gira su hardware
# vero, e li confronta valore per valore. Un solo confronto prova TRE cose:
#   1. che `ROMS/nibbler.zip` e' davvero il set giusto;
#   2. che ogni passaggio di decodifica e' trascritto bene;
#   3. che i dati che finiranno nel CABINATONE sono quelli gia' visti girare.
# ([[feedback_verify_against_source_not_copy]])
#
# ⛔⛔ DUE header NON possono essere identici a SPINNERINO, e non e' una svista:
#
#   · `nibbler_palette.h` - la' i valori RGB565 sono **BYTE-SWAPPED** per il
#     display SPI (lo dice l'intestazione stessa: "precomputed RGB565
#     (byte-swapped)"). Il pannello DSI del CABINATONE li vuole **NATIVI**.
#     E' la stessa trappola gia' pagata su 1942, Bomb Jack e Burnin' Rubber.
#     ⭐ La verifica c'e' lo stesso, ed e' piu' forte di un confronto diretto:
#     si RIAPPLICA lo scambio ai nostri valori e si confronta con SPINNERINO.
#     Se combacia, e' provato che il decode dalla PROM e' identico al loro e che
#     l'UNICA differenza e' lo scambio.
#
#   · `nibbler_crunch.h` - il "crunch" (il rumore del serpente che mangia) e' un
#     circuito DISCRETO che MAME non emula: e' un campione registrato. Quello di
#     SPINNERINO e' a **24 kHz** e li' viene letto 1:1 su un I2S che gira a 32:
#     sul CABINATONE (ES8311 a 32 kHz) uscirebbe il 33% piu' corto e piu' acuto.
#     Si **RICAMPIONA 3:4** - rapporto esatto, nessuna deriva
#     ([[feedback_adattare_il_ritmo_non_le_costanti]]).
#     ⚠️ Il master a 48 kHz esiste (`NUOVO GALAGONE PAOLO 1504\
#     nibbler_crunch_raw.wav`, 9,4 s) ma non si sa da quale istante sia stato
#     ritagliato l'estratto: la sorgente affidabile e' l'header che gia' suona.
#
# ⚠️ Il logo del menu NON si genera da qui: sta sulla SD e lo fa
#    `convert_nibbler_logo.py` -> `DA COPIARE SU SD/arcade/nibbler.scr`.
# ============================================================================
import sys, zipfile, struct

ZIP  = _os.path.join("ROMS", "nibbler.zip")
OUT  = _os.path.join("machines", "nibbler")
SPIN = _os.path.join("C:\\", "Users", "Tecnico", "Desktop", "PROGETTI SH",
                     "SPINNERINO PLUS WIFI", "BACKUP SPINNERINO PLUS",
                     "SPINNERINO", "machines", "nibbler")

# ---------------------------------------------------------------- ROM_START
# src/mame/snk/snk6502.cpp, ROM_START( nibbler ), copiato riga per riga.
# (nome, offset nella regione, dimensione, CRC32)
MAINCPU = [
    ("g-0960-52.ic12", 0x3000, 0x1000, 0x6DFA1BE5),
    ("g-0960-48.ic7",  0x4000, 0x1000, 0x808E1A03),
    ("g-0960-49.ic8",  0x5000, 0x1000, 0x1571D4A2),
    ("g-0960-50.ic9",  0x6000, 0x1000, 0xA599DF10),
    ("g-0960-51.ic10", 0x7000, 0x1000, 0xA6B5ABE5),
    ("g-0960-53.ic14", 0x8000, 0x1000, 0x9F537185),   # ROM_RELOAD a 0xF000
    ("g-0960-54.ic15", 0x9000, 0x1000, 0x7205FB8D),
    ("g-0960-55.ic16", 0xA000, 0x1000, 0x4BB39815),
    ("g-0960-56.ic17", 0xB000, 0x1000, 0xED680F19),
]
GFX1 = [
    ("g-0960-57.ic50", 0x0000, 0x1000, 0x01D4D0C2),
    ("g-0960-58.ic51", 0x1000, 0x1000, 0xFEFF7FAF),
]
PROMS = [
    ("g-0708-05.ic7",  0x0000, 0x0020, 0xA5709FF3),   # FG  -> voci  0..31
    ("g-0708-04.ic6",  0x0020, 0x0020, 0xDACD592D),   # BG  -> voci 32..63
]
# ROM_REGION( 0x1800, "snk6502", ROMREGION_ERASEFF ): il primo mezzo KB resta
# 0xFF. ⭐ Il socket ic51 della musica sulla PCB vera e' VUOTO - Nibbler NON ha
# musica di sottofondo, e il MAME moderno lo rispetta (historic-mame ci metteva
# per errore la ROM di Pioneer Balloon).
SND = [
    ("g-0959-44.ic52", 0x0800, 0x0800, 0x87D67DEE),   # SFX / melodie
    ("g-0959-45.ic53", 0x1000, 0x0800, 0x33189917),   # bassi / gettone
]


def carica(z, lista, dim, riempi=0x00):
    buf = bytearray([riempi]) * dim
    for nome, off, ln, crc in lista:
        try:
            d = z.read(nome)
        except KeyError:
            sys.exit("ERRORE: manca %s dentro %s" % (nome, ZIP))
        if len(d) != ln:
            sys.exit("ERRORE: %s e' %d byte, ne servono %d" % (nome, len(d), ln))
        c = zipfile.crc32(d) & 0xFFFFFFFF if hasattr(zipfile, "crc32") else None
        import binascii
        c = binascii.crc32(d) & 0xFFFFFFFF
        if c != crc:
            sys.exit("ERRORE: %s ha CRC %08X, atteso %08X (set sbagliato)"
                     % (nome, c, crc))
        buf[off:off + ln] = d
    return bytes(buf)


def righe_byte(dati, per_riga=16):
    out = []
    for i in range(0, len(dati), per_riga):
        out.append("  " + ", ".join("0x%02X" % b for b in dati[i:i + per_riga]) + ",")
    return out


def righe_word(dati, per_riga=8):
    out = []
    for i in range(0, len(dati), per_riga):
        out.append("  " + ", ".join("0x%04X" % w for w in dati[i:i + per_riga]) + ",")
    return out


def header_byte(descr, nome, dati):
    r = ["// " + descr, "const unsigned char %s[] PROGMEM = {" % nome]
    r += righe_byte(dati)
    r += ["};"]
    return "\n".join(r) + "\n"


# ------------------------------------------------------------------ palette
# src/mame/snk/snk6502_v.cpp, snk6502_palette(): scala a resistori.
#   rosso  = 0x21*bit0 + 0x47*bit1 + 0x97*bit2      (bit 0-2)
#   verde  = 0x21*bit3 + 0x47*bit4 + 0x97*bit5      (bit 3-5)
#   blu    = 0x52*bit6 + 0xAD*bit7                  (bit 6-7)
def prom_a_rgb565(v, swap):
    r8 = 0x21 * ((v >> 0) & 1) + 0x47 * ((v >> 1) & 1) + 0x97 * ((v >> 2) & 1)
    g8 = 0x21 * ((v >> 3) & 1) + 0x47 * ((v >> 4) & 1) + 0x97 * ((v >> 5) & 1)
    b8 = 0x52 * ((v >> 6) & 1) + 0xAD * ((v >> 7) & 1)
    # ⚠️ Scalatura PROPORZIONALE (x31/255), non uno shift: e' quella del porting
    # che gira e anche la convenzione di questa console (brubber: `(r3*31)/7`).
    # Con `>>3` quindici voci su 64 cadrebbero un gradino piu' in basso, e si
    # perderebbe la prova che l'unica differenza da SPINNERINO e' lo scambio.
    c = ((r8 * 31 // 255) << 11) | ((g8 * 63 // 255) << 5) | (b8 * 31 // 255)
    if swap:
        c = ((c & 0xFF00) >> 8) | ((c & 0x00FF) << 8)
    return c


def genera_palette(proms, swap):
    val = [prom_a_rgb565(b, swap) for b in proms]
    nota = "byte-swapped, per il display SPI" if swap else \
           "NATIVI, come li vuole il pannello DSI"
    r = ["// Nibbler palette: 2x32B PROM + RGB565 precalcolati (%s)" % nota,
         "// ic7 (foreground) 0..31, ic6 (background) 32..63",
         "// Scala a resistori da MAME snk6502_v.cpp: R,G = 0x21/0x47/0x97, B = 0x52/0xAD",
         "",
         "const unsigned char nibbler_palette_raw[64] PROGMEM = {"]
    r += righe_byte(proms)
    r += ["};", "", "const unsigned short nibbler_palette[64] PROGMEM = {"]
    r += righe_word(val)
    r += ["};"]
    return "\n".join(r) + "\n", val


# ------------------------------------------------------------------- crunch
def leggi_crunch_spinnerino():
    """Estrae i campioni dall'header a 24 kHz di SPINNERINO."""
    txt = open(_os.path.join(SPIN, "nibbler_crunch.h"), "r",
               encoding="utf-8", errors="replace").read()
    i = txt.find("{", txt.find("nibbler_crunch"))
    j = txt.find("}", i)
    v = []
    for t in txt[i + 1:j].replace("\n", " ").split(","):
        t = t.strip()
        if t:
            v.append(int(t, 0))
    return v


def ricampiona_3a4(src):
    """24 kHz -> 32 kHz. Rapporto ESATTO 3:4, interpolazione lineare.
    Ogni 3 campioni d'ingresso ne escono 4: nessuna deriva, nessun caso
    particolare ai bordi. len_out = len_in * 4 // 3."""
    n = len(src)
    m = n * 4 // 3
    out = []
    for k in range(m):
        p = k * 3.0 / 4.0
        i = int(p)
        f = p - i
        a = src[i] if i < n else 0
        b = src[i + 1] if i + 1 < n else 0
        out.append(int(round(a * (1.0 - f) + b * f)))
    return out


def genera_crunch(camp):
    r = ["// Nibbler - campione \"crunch\" (il serpente che mangia).",
         "// Il crunch e' un circuito DISCRETO che MAME non emula: e' una",
         "// registrazione. Sorgente: l'header a 24 kHz del porting SPINNERINO PLUS",
         "// (a sua volta da sd_rom_staging/nibbler_crunch.bin di P4_NEW_multi),",
         "// RICAMPIONATO 3:4 a 32 kHz - il clock dell'I2S/ES8311 di questa scheda.",
         "// Rigenerato da romconv_nibbler.py: non modificare a mano.",
         "#ifndef NIBBLER_CRUNCH_H",
         "#define NIBBLER_CRUNCH_H",
         "",
         "#define NIBBLER_CRUNCH_LEN %d" % len(camp),
         "",
         "const signed char nibbler_crunch[%d] PROGMEM = {" % len(camp)]
    for i in range(0, len(camp), 16):
        r.append("  " + ", ".join("%d" % c for c in camp[i:i + 16]) + ",")
    r += ["};", "", "#endif // NIBBLER_CRUNCH_H"]
    return "\n".join(r) + "\n"


# --------------------------------------------------------------------- main
def valori(txt):
    """Tutti i letterali numerici dentro le graffe di un header, in ordine.
    Si confrontano i VALORI, non il testo: gli header di SPINNERINO hanno
    un'impaginazione piu' vecchia e sono a CRLF ([[feedback_python_converters]])."""
    v = []
    i = 0
    while True:
        i = txt.find("{", i)
        if i < 0:
            break
        j = txt.find("}", i)
        for t in txt[i + 1:j].replace("\n", " ").split(","):
            t = t.strip()
            if t:
                v.append(int(t, 0))
        i = j + 1
    return v


def confronta(nome, mio, loro_path, attesi=None):
    if not _os.path.isfile(loro_path):
        print("  %-22s SPINNERINO non trovato, salto" % nome)
        return None
    loro = valori(open(loro_path, "r", encoding="utf-8", errors="replace").read())
    miei = valori(mio) if isinstance(mio, str) else list(mio)
    if attesi is not None:
        loro = attesi
    if len(miei) != len(loro):
        print("  %-22s DIVERSO: %d valori contro %d" % (nome, len(miei), len(loro)))
        return False
    diff = [k for k in range(len(miei)) if miei[k] != loro[k]]
    if diff:
        print("  %-22s DIVERSO: %d valori su %d (primo all'indice %d: %s vs %s)"
              % (nome, len(diff), len(miei), diff[0], miei[diff[0]], loro[diff[0]]))
        return False
    print("  %-22s IDENTICO (%d valori)" % (nome, len(miei)))
    return True


def main():
    solo_verifica = "--verifica" in sys.argv

    if not _os.path.isfile(ZIP):
        sys.exit("ERRORE: manca %s" % ZIP)
    z = zipfile.ZipFile(ZIP)

    rom_full = carica(z, MAINCPU, 0x10000, 0xFF)
    rom      = rom_full[0x3000:0xC000]              # 36864, indicizzata rom[a-0x3000]
    gfx      = carica(z, GFX1,  0x2000, 0x00)
    proms    = carica(z, PROMS, 0x0040, 0x00)
    snd      = carica(z, SND,   0x1800, 0xFF)[:0x1800]

    h_rom = header_byte("Nibbler CPU ROM (36KB: 0x3000-0xBFFF, indicizzata "
                        "rom[addr - 0x3000]); g-0960-53 sta a offset 0x5000 ed e' "
                        "anche lo specchio 0xF000-0xFFFF dei vettori del 6502",
                        "nibbler_rom", rom)
    h_gfx = header_byte("Nibbler background char ROM (8KB: g-0960-57 + g-0960-58)",
                        "nibbler_gfx", gfx)
    h_snd = header_byte("Nibbler sound ROM (6KB: [0000-07FF]=0xFF (ic51 VUOTO sulla "
                        "PCB vera), [0800-0FFF]=ic52, [1000-17FF]=ic53)",
                        "nibbler_snd_rom", snd)
    h_pal, pal_nativa = genera_palette(proms, swap=False)
    _,     pal_swap   = genera_palette(proms, swap=True)

    c24 = leggi_crunch_spinnerino()
    c32 = ricampiona_3a4(c24)
    h_cru = genera_crunch(c32)

    print("Confronto con il porting SPINNERINO PLUS (che gira su hardware):")
    ok = True
    ok &= confronta("nibbler_rom.h",  h_rom, _os.path.join(SPIN, "nibbler_rom.h")) is not False
    ok &= confronta("nibbler_gfx.h",  h_gfx, _os.path.join(SPIN, "nibbler_gfx.h")) is not False
    ok &= confronta("nibbler_snd_rom.h", h_snd,
                    _os.path.join(SPIN, "nibbler_snd_rom.h")) is not False
    # ⭐ La tavolozza si confronta RIAPPLICANDO lo scambio: se i 128 valori
    # combaciano, il decode dalla PROM e' identico e l'unica differenza e' lo swap.
    ok &= confronta("nibbler_palette.h (con lo scambio rimesso)",
                    list(proms) + pal_swap,
                    _os.path.join(SPIN, "nibbler_palette.h")) is not False

    print("Ricampionamento del crunch: %d campioni a 24 kHz -> %d a 32 kHz "
          "(%.0f ms invariati)" % (len(c24), len(c32), 1000.0 * len(c24) / 24000))

    if solo_verifica:
        print("\n--verifica: nessun file scritto.")
        return 0 if ok else 1

    for nome, testo in (("nibbler_rom.h", h_rom), ("nibbler_gfx.h", h_gfx),
                        ("nibbler_snd_rom.h", h_snd), ("nibbler_palette.h", h_pal),
                        ("nibbler_crunch.h", h_cru)):
        p = _os.path.join(OUT, nome)
        open(p, "w", encoding="utf-8", newline="\n").write(testo)
        print("scritto %s (%d byte)" % (p, _os.path.getsize(p)))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
