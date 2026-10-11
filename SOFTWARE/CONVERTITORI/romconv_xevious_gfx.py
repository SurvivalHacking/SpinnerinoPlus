# ============================================================================
# romconv_xevious_gfx.py — i sei header di XEVIOUS che nessun convertitore
# faceva: sfondo, tile, caratteri, scorrimento, la PROM sr3 e i colori.
#
#   xevious_bgmap.h        la mappa dello sfondo (16 KB da tre ROM)
#   xevious_bgtiles.h      512 tile di sfondo 8x8 a 2 bit
#   xevious_tilemap.h      512 caratteri 8x8 a 1 bit, portati a 2
#   xevious_scrolltable.h  le due PROM dello scorrimento
#   xevious_sr3.h          la PROM xvi-1.5n, copiata tale e quale
#   xevious_cmap.h         tavolozza e tabelle dei colori
#
# ⛔⛔ CI VUOLE IL SET BOOTLEG `xevios.zip`, non solo `xevious.zip`.
# Lo sfondo del gioco che gira nasce da `10.1d` + `xvi_10.2b` + `12.3d`, e i
# primi due file esistono SOLO nel bootleg: col set originale lo sfondo esce
# diverso (861 byte su 16384). Confrontato byte per byte, non dedotto.
# Se hai solo `xevious.zip` il convertitore lavora lo stesso, ma te lo dice.
#
# ⚠️ I CINQUE header grafici sono stati verificati VALORE PER VALORE contro il
# progetto che gira: zero differenze. Il SESTO, `xevious_cmap.h`, e' l'unico
# approssimato: vedi la nota sopra `scrivi_cmap()`.
# ============================================================================
import os, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "xevious")

# nome nel set originale, nome nel bootleg xevios
FG        = ("xvi_12.3b", "xvi_12.3b")
BG_P0     = ("xvi_14.3d", "xvi_14.3d")   # ⛔ piano 0 = 14.3d, NON 13.3c
BG_P1     = ("xvi_13.3c", "xvi_13.3c")
BGMAP     = [("xvi_9.2a", "10.1d"), ("xvi_10.2b", "xvi_10.2b"),
             ("xvi_11.2c", "12.3d")]
SCROLL_X  = ("xvi-6.4f", "xvi_6bpr.4f")
SCROLL_Y  = ("xvi-7.4h", "xvi_7bpr.4h")
SR3       = ("xvi-1.5n", "xvi_1bpr.5n")
PROM_R    = ("xvi-8.6a", "xvi_8bpr.6a")
PROM_G    = ("xvi-9.6d", "xvi_9bpr.6d")
PROM_B    = ("xvi-10.6e", "xvi10bpr.6e")
# le due coppie «indice colore» = nibble basso di A | nibble basso di B << 4
BG_CLR    = (SCROLL_Y, SCROLL_X)                     # 7.4h + 6.4f
SPR_CLR   = (("xvi-4.3l", "xvi_4bpr.3l"), ("xvi-5.3m", "xvi_5bpr.3m"))

# scala a 8 bit di una scaletta di resistenze a 4 bit (la solita Namco)
SCALA4 = [0x00, 0x0e, 0x1f, 0x2d, 0x43, 0x51, 0x62, 0x70,
          0x8f, 0x9d, 0xae, 0xbc, 0xd2, 0xe0, 0xf1, 0xff]

_file = {}          # nome dentro la zip -> contenuto


def carica():
    """Legge tutt'e due le zip, se ci sono. Il bootleg per ultimo: dove i due
    set hanno un file con lo stesso nome ma contenuto diverso, vince lui."""
    trovate = []
    for nome in ("xevious.zip", "xevios.zip"):
        p = os.path.join(ROMS, nome)
        if os.path.isfile(p):
            trovate.append(nome)
            with zipfile.ZipFile(p) as z:
                for i in z.namelist():
                    if not i.endswith("/"):
                        _file[os.path.basename(i)] = z.read(i)
    return trovate


_bootleg = False        # valorizzato da carica(): c'e' anche xevios.zip?


def rom(defs, obbligatorio=True):
    # ⛔ L'ordine conta. `defs` e' (nome nell'originale, nome nel bootleg): con
    # tutt'e due le zip aperte, provando sempre il primo si prendeva `xvi_9.2a`
    # dell'originale invece di `10.1d`, e lo sfondo usciva con 861 byte su
    # 16384 diversi da quello che gira. Col bootleg presente si prova PRIMA il
    # suo nome.
    ordine = tuple(reversed(defs)) if _bootleg else tuple(defs)
    for n in ordine:
        if n in _file:
            return _file[n]
    if obbligatorio:
        raise SystemExit("ERRORE: manca il file di ROM %s" % (defs,))
    return None


def ruota270(righe):
    """Gira il tile di 270 gradi: lo schermo dello SPINNERINO e' verticale."""
    fuori = []
    for r in range(8):
        v = 0
        for c in range(8):
            v |= ((righe[c] >> ((7 - r) * 2)) & 3) << (c * 2)
        fuori.append(v)
    return fuori


def intesta(f, righe):
    for r in righe:
        f.write(r + "\n")


def scrivi_bytes(nome, simbolo, dati, commenti):
    p = os.path.join(USCITA, nome)
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        intesta(f, commenti)
        f.write("const unsigned char %s[%d] = {\n" % (simbolo, len(dati)))
        for i in range(0, len(dati), 16):
            f.write("  " + ",".join("0x%02x" % b for b in dati[i:i + 16])
                    + (",\n" if i + 16 < len(dati) else "\n"))
        f.write("};\n")
    print("  %s" % nome)


def scrivi_tile(nome, simbolo, tiles, commenti):
    p = os.path.join(USCITA, nome)
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        intesta(f, commenti)
        f.write("const unsigned short %s[%d][8] = {\n" % (simbolo, len(tiles)))
        for i, righe in enumerate(tiles):
            f.write(" { " + ",".join("0x%x" % r for r in righe) + " }"
                    + (",\n" if i < len(tiles) - 1 else "\n"))
        f.write("};\n")
    print("  %s" % nome)


# ------------------------------------------------------------------ i sei
def scrivi_bgmap(bootleg):
    dati = b"".join(rom(d) for d in BGMAP)
    scrivi_bytes("xevious_bgmap.h", "xevious_bgmap", dati, [
        "// Xevious background map PROM data",
        "// ROM1 (4096B) + ROM2 (8192B) + ROM3 (4096B) = 16384B",
        "// Generato da romconv_xevious_gfx.py, set %s"
        % ("xevios (bootleg)" if bootleg else "xevious (originale)")])


def scrivi_bgtiles():
    p0, p1 = rom(BG_P0), rom(BG_P1)
    tiles = []
    for t in range(min(len(p0), len(p1)) // 8):
        base, righe = t * 8, []
        for y in range(8):
            b0, b1, v = p0[base + y], p1[base + y], 0
            for x in range(8):
                v |= (((b0 >> x) & 1) | (((b1 >> x) & 1) << 1)) << (x * 2)
            righe.append(v)
        tiles.append(ruota270(righe))
    scrivi_tile("xevious_bgtiles.h", "xevious_bgtiles", tiles, [
        "// Xevious BG tiles - 512 tile, 8x8, 2bpp",
        "// Generato da romconv_xevious_gfx.py",
        "// ATTENZIONE: piano 0 = xvi_14.3d, piano 1 = xvi_13.3c: e' l'ordine",
        "//    INVERSO di quello che verrebbe naturale. Scambiandoli, 2876",
        "//    valori su 4096 escono sbagliati."])


def scrivi_tilemap():
    dati = rom(FG)
    tiles = []
    for c in range(len(dati) // 8):
        base, righe = c * 8, []
        for y in range(8):
            b, v = dati[base + y], 0
            for x in range(8):
                v |= (3 if (b >> x) & 1 else 0) << (x * 2)
            righe.append(v)
        tiles.append(ruota270(righe))
    scrivi_tile("xevious_tilemap.h", "xevious_tilemap", tiles, [
        "// Xevious FG character tiles - 512 caratteri, 8x8, 1bpp portato a 2bpp",
        "// 0=trasparente, 3=colorato (il colore arriva dall'attributo)",
        "// Generato da romconv_xevious_gfx.py"])


def scrivi_scroll():
    sx, sy = rom(SCROLL_X), rom(SCROLL_Y)
    p = os.path.join(USCITA, "xevious_scrolltable.h")
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        intesta(f, ["// Xevious scroll lookup PROMs",
                    "// Generato da romconv_xevious_gfx.py", ""])
        for simbolo, dati in (("xevious_scroll_x", sx), ("xevious_scroll_y", sy)):
            f.write("const unsigned char %s[%d] = {\n" % (simbolo, len(dati)))
            for i in range(0, len(dati), 16):
                f.write("  " + ",".join("0x%02x" % b for b in dati[i:i + 16])
                        + (",\n" if i + 16 < len(dati) else "\n"))
            f.write("};\n\n")
    print("  xevious_scrolltable.h")


def scrivi_sr3():
    dati = rom(SR3)
    p = os.path.join(USCITA, "xevious_sr3.h")
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        # ⚠️ Le guardie ci vogliono: l'header viene incluso da piu' punti e
        # senza si prende «redefinition of xevious_sr3».
        f.write("#ifndef XEVIOUS_SR3_H\n#define XEVIOUS_SR3_H\n\n")
        f.write("// Xevious sr3 PROM data (xvi-1.5n, 256 byte), copiata tale e quale\n")
        f.write("// Serve a xevious_bb_r() per le letture fra 0xB800 e 0xB8FF\n")
        f.write("const unsigned char xevious_sr3[%d] = {\n" % len(dati))
        for i in range(0, len(dati), 16):
            f.write("  " + ",".join("0x%02x" % b for b in dati[i:i + 16])
                    + (",\n" if i + 16 < len(dati) else "\n"))
        f.write("};\n\n#endif\n")
    print("  xevious_sr3.h")


def scrivi_cmap():
    """⚠️ L'UNICO APPROSSIMATO DEI SEI.

    La STRUTTURA e' esatta, verificata contro il progetto che gira:
      - fg[c]      = { trasparente, tav[c], tav[c], tav[c] }        (esatta)
      - bg[c][p]   = tav[(7.4h & 15) | ((6.4f & 15) << 4)]  512/512 in accordo
      - sprites    = stessa forma con 4.3l e 5.3m
    Quello che NON si ricava sono i 128 COLORI della tavolozza: nel progetto
    che gira hanno 31 livelli di rosso distinti, mentre le PROM sono a 4 bit e
    di livelli ne possono dare 16. Quella tavolozza non nasce da queste PROM, e
    da nessuna formula per canale: e' lo stesso problema gia' noto di 1942 e
    Road Fighter. Qui si usa la scaletta di resistenze Namco: le tinte possono
    risultare un po' diverse, la grafica e' giusta."""
    R, G, B = rom(PROM_R), rom(PROM_G), rom(PROM_B)
    tav = []
    for i in range(128):
        r, g, b = SCALA4[R[i] & 15], SCALA4[G[i] & 15], SCALA4[B[i] & 15]
        v = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
        tav.append(((v & 0xff) << 8) | (v >> 8))     # RGB565 a byte scambiati
    tav.append(0x0000)                               # 129esimo = trasparente

    a, b_ = rom(BG_CLR[0]), rom(BG_CLR[1])
    bg = [tav[((a[k] & 15) | ((b_[k] & 15) << 4)) & 0x7f] for k in range(512)]
    a, b_ = rom(SPR_CLR[0]), rom(SPR_CLR[1])
    spr = [tav[((a[k] & 15) | ((b_[k] & 15) << 4)) & 0x7f] for k in range(512)]

    p = os.path.join(USCITA, "xevious_cmap.h")
    with open(p, "w", encoding="latin-1", newline="\n") as f:
        intesta(f, [
            "// Xevious color palettes - RGB565 (byte scambiati per lo SPI)",
            "// 128 colori da tre PROM da 4 bit (R/G/B) + trasparente",
            "// Generato da romconv_xevious_gfx.py",
            "// ATTENZIONE: le TINTE possono differire dalle originali: la tavolozza",
            "//    del progetto ha piu' livelli di quanti ne dia una PROM a 4",
            "//    bit, quindi non si ricava dalle ROM. La grafica e' giusta.",
            ""])
        f.write("const unsigned short xevious_palette[%d] = {\n" % len(tav))
        for i in range(0, len(tav), 16):
            f.write("  " + ",".join("0x%04x" % v for v in tav[i:i + 16])
                    + (",\n" if i + 16 < len(tav) else "\n"))
        f.write("};\n\n")

        f.write("// FG char colors: 64 gruppi x 4 colori\n")
        f.write("// Niente PROM: pixel 0 = trasparente, 1/2/3 = palette[gruppo]\n")
        f.write("const unsigned short xevious_cmap_fg[64][4] = {\n")
        for c in range(64):
            f.write("  {0x0000,0x%04x,0x%04x,0x%04x}%s\n"
                    % (tav[c], tav[c], tav[c], "," if c < 63 else ""))
        f.write("};\n\n")

        f.write("// BG tile colors: 128 gruppi x 4 colori (2bpp)\n")
        f.write("const unsigned short xevious_cmap_bg[128][4] = {\n")
        for c in range(128):
            f.write("  {" + ",".join("0x%04x" % v for v in bg[c * 4:c * 4 + 4])
                    + "}%s\n" % ("," if c < 127 else ""))
        f.write("};\n\n")

        f.write("// Sprite colors: 64 gruppi x 8 colori (3bpp)\n")
        f.write("const unsigned short xevious_cmap_sprites[64][8] = {\n")
        for c in range(64):
            f.write("  {" + ",".join("0x%04x" % v for v in spr[c * 8:c * 8 + 8])
                    + "}%s\n" % ("," if c < 63 else ""))
        f.write("};\n")
    print("  xevious_cmap.h  (tinte approssimate, vedi la nota nel file)")


def main():
    trovate = carica()
    if not trovate:
        print("ERRORE: manca ROMS\\xevious.zip (e ROMS\\xevios.zip)")
        return 1
    global _bootleg
    _bootleg = "10.1d" in _file and "12.3d" in _file
    bootleg = _bootleg
    if not bootleg:
        print("  ATTENZIONE: senza xevios.zip lo sfondo esce dal set originale")
        print("              (861 byte su 16384 diversi da quello che gira)")
    os.makedirs(USCITA, exist_ok=True)
    scrivi_bgmap(bootleg)
    scrivi_bgtiles()
    scrivi_tilemap()
    scrivi_scroll()
    scrivi_sr3()
    scrivi_cmap()
    print("xevious: 6 header scritti")
    return 0


sys.exit(main())
