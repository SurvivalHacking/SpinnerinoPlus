# ============================================================================
# romconv_xevious_bg2c.py — xevious_bg_rom2c.h da xevious.zip.
#
# E' `xvi_11.2c` del set ORIGINALE xevious, 4096 byte, copiati grezzi.
# Il porting lo usa al posto di `12.3d` del bootleg per togliere i quadrati
# neri dal terreno (sta scritto nell'intestazione dell'header che gira).
# ⚠️ Serve la zip di **xevious**, non quella di supxevs.
# ============================================================================
import os, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "xevious",
                         "xevious_bg_rom2c.h")
PEZZO = "xvi_11.2c"
DIM   = 4096


def main():
    zip_x = os.path.join(ROMS, "xevious.zip")
    if not os.path.isfile(zip_x):
        print("ERRORE: manca ROMS\\xevious.zip")
        return 1
    with zipfile.ZipFile(zip_x) as z:
        dentro = {n.lower(): n for n in z.namelist()}
        if PEZZO not in dentro:
            print("ERRORE: in xevious.zip manca %s" % PEZZO)
            return 1
        dati = z.read(dentro[PEZZO])
    if len(dati) != DIM:
        print("ERRORE: %s e' di %d byte, ne servono %d" % (PEZZO, len(dati), DIM))
        return 1

    righe = ["  " + ",".join("0x%02x" % b for b in dati[i:i + 16])
             for i in range(0, len(dati), 16)]
    os.makedirs(os.path.dirname(USCITA), exist_ok=True)
    with open(USCITA, "w", encoding="latin-1", newline="\n") as f:
        f.write("// Xevious BG rom2c = xvi_11.2c ORIGINALE (set xevious).\n")
        f.write("// Usato da bb_r al posto di 12.3d (bootleg) per togliere "
                "quadrati neri/tile\n")
        f.write("// sbagliati, tenendo rom2a=10.1d grezza (terreno coerente "
                "con CPU xevios).\n")
        f.write("// Generato da ROMS/xevious.zip (xvi_11.2c).\n")
        f.write("const unsigned char xevious_bg_rom2c[%d] = {\n" % DIM)
        f.write(",\n".join(righe) + "\n};\n")
    print("  xevious_bg_rom2c.h")
    return 0


sys.exit(main())
