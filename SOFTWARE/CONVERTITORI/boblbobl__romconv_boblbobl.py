#!/usr/bin/env python3
# ----------------------------------------------------------------------------
# Funziona sia dalla radice del progetto sia da "ROM CONVERTER PY/": si sposta
# da solo sulla cartella che contiene CABINATONE.ino (convenzione del progetto).
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
# romconv_boblbobl.py - prepara i file di BUBBLE BOBBLE (set "boblbobl", il
# bootleg "Bobble Bobble" SENZA MCU 68705) per la microSD del CABINATONE.
#
# FONTE: gli header .h del porting SPINNERINO PLUS (machines/boblbobl), che
# sono gia' il prodotto **verificato** del converter di li': ROM dei 3 Z80,
# PROM a71-25.41 delle colonne sprite e soprattutto la GRAFICA GIA'
# DECODIFICATA a 1 byte/pixel (16384 tile 8x8 4bpp), con la mappatura dei piani
# gia' corretta e verificata a schermo su quel progetto.
# Rifare qui la decodifica dalla boblbobl.zip significherebbe riscrivere quella
# logica (charlayout 4bpp, planes {0,4,RGN_FRAC(1,2),RGN_FRAC(1,2)+4} +
# ROMREGION_INVERT) e poterla sbagliare - e' esattamente la scelta gia' fatta
# per Gyruss (romconv_gyruss.py).
#
# USO:  python romconv_boblbobl.py [cartella_header] [cartella_uscita] [--png]
#       default: <SPINNERINO PLUS>/machines/boblbobl -> DA COPIARE SU SD/boblbobl
#       --png  : scrive anche i fogli di controllo della grafica in _verify_gfx/
#                (si GUARDANO le forme prima di flashare, metodo Black Tiger)
#
# Stampa i CRC32 da incastonare in boblbobl.cpp (bb_load li verifica a ogni
# avvio: una copia troncata sulla SD deve dare un errore leggibile, non uno
# schermo nero).
# ============================================================================
import os, re, sys, zlib, struct

# Sorgente predefinita: il porting SPINNERINO PLUS (stessa CPU, ESP32-P4).
SRC_DEFAULT = os.path.join("..", "SPINNERINO PLUS WIFI", "SPINNERINO",
                           "machines", "boblbobl")

def leggi_array(testo, nome):
    """estrae i valori numerici dell'array `nome` dall'header .h"""
    i = testo.index(nome)
    i = testo.index("{", i)
    liv, j = 0, i
    while True:
        if testo[j] == "{": liv += 1
        elif testo[j] == "}":
            liv -= 1
            if liv == 0: break
        j += 1
    return [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\b\d+\b", testo[i:j])]

def carica(dirin, file, nome):
    t = open(os.path.join(dirin, file), encoding="utf-8", errors="replace").read()
    return leggi_array(t, nome)

# ---------------------------------------------------------------- fogli PNG
def fogli_png(gfx, dirout):
    """Fogli di controllo: le tile 8x8 gia' decodificate, 64 per riga, con la
    palette di comodo dei 16 pen (la palette vera e' in RAM, costruita dal
    gioco). Serve a vedere OFFLINE che le forme siano forme - font leggibile,
    draghetti riconoscibili - prima di mettere qualunque cosa sul device."""
    try:
        from PIL import Image
    except ImportError:
        print("  (PIL non installato: salto i fogli PNG)")
        return
    os.makedirs(dirout, exist_ok=True)
    # 16 pen -> grigi + colori distinti; pen 15 = trasparente -> magenta acceso
    pal = [(0, 0, 0), (255, 255, 255), (255, 0, 0), (0, 255, 0),
           (0, 0, 255), (255, 255, 0), (255, 0, 255), (0, 255, 255),
           (128, 128, 128), (192, 96, 0), (0, 128, 0), (0, 0, 128),
           (128, 0, 128), (0, 128, 128), (192, 192, 192), (255, 0, 255)]
    NT, COLS = 16384, 64
    PER_FOGLIO = 4096                      # 4 fogli da 64x64 tile = 512x512 px
    for f in range(NT // PER_FOGLIO):
        righe = PER_FOGLIO // COLS
        im = Image.new("RGB", (COLS * 8, righe * 8))
        px = im.load()
        for t in range(PER_FOGLIO):
            base = (f * PER_FOGLIO + t) * 64
            tx, ty = (t % COLS) * 8, (t // COLS) * 8
            for y in range(8):
                for x in range(8):
                    px[tx + x, ty + y] = pal[gfx[base + y * 8 + x] & 15]
        p = os.path.join(dirout, "boblbobl_gfx_%d.png" % f)
        im.save(p)
        print("  %s: %d tile" % (p, PER_FOGLIO))

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    png  = "--png" in sys.argv
    dirin  = args[0] if len(args) > 0 else SRC_DEFAULT
    dirout = args[1] if len(args) > 1 else os.path.join("DA COPIARE SU SD", "boblbobl")
    if not os.path.isdir(dirin):
        print("ERRORE: cartella header non trovata: %s" % dirin)
        return 1
    os.makedirs(dirout, exist_ok=True)

    # (file di uscita, header, nome array, lunghezza attesa)
    #   main : bb3+bb5+bb4, 0x0000-0x7FFF fisso + 8 banchi da 0x4000 (0x10000+)
    #   sub  : a78-08, Z80 slave/video
    #   audio: a78-07, Z80 sonoro (YM2203 + YM3526)
    #   prom : a71-25.41, pilota le COLONNE degli sprite (essenziale, non e'
    #          una palette: bit3=salta riga, bit2=nuova colonna, bit0-1=offset)
    #   gfx  : 16384 tile 8x8 4bpp gia' decodificate, 1 byte/pixel
    tabella = [
        ("main.bin",  "boblbobl_rom_main.h",     "boblbobl_rom_main[196608]",   0x30000),
        ("sub.bin",   "boblbobl_rom_sub.h",      "boblbobl_rom_sub[32768]",     0x8000),
        ("audio.bin", "boblbobl_rom_audio.h",    "boblbobl_rom_audio[32768]",   0x8000),
        ("prom.bin",  "boblbobl_prom.h",         "boblbobl_prom[256]",          0x100),
        ("gfx.bin",   "boblbobl_gfx_decoded.h",  "boblbobl_gfx_decoded[1048576]", 0x100000),
    ]

    print("BUBBLE BOBBLE (boblbobl)")
    print("  da  : %s" % dirin)
    print("  a   : %s\n" % dirout)
    crc, gfx = {}, None
    for nomefile, header, array, atteso in tabella:
        v = carica(dirin, header, array)
        dati = struct.pack("<%dB" % len(v), *v)
        if len(dati) != atteso:
            print("  ERRORE: %-10s %d byte invece di %d - NON scritto"
                  % (nomefile, len(dati), atteso))
            return 1
        open(os.path.join(dirout, nomefile), "wb").write(dati)
        crc[nomefile] = zlib.crc32(dati) & 0xFFFFFFFF
        print("  %-10s %8d byte   CRC32 %08X" % (nomefile, len(dati), crc[nomefile]))
        if nomefile == "gfx.bin":
            gfx = v

    tot = sum(os.path.getsize(os.path.join(dirout, f)) for f in crc)
    print("\ntotale %d byte (%.2f MB)" % (tot, tot / 1048576.0))

    if png and gfx:
        print("\nfogli di controllo grafica:")
        fogli_png(gfx, "_verify_gfx")

    print("\nDa incollare in boblbobl.cpp (bb_load):")
    for f in ("main.bin", "sub.bin", "audio.bin", "prom.bin", "gfx.bin"):
        print('    { "/sdcard/boblbobl/%-11s 0x%08X },' % (f + '",', crc[f]))
    return 0

if __name__ == "__main__":
    sys.exit(main())
