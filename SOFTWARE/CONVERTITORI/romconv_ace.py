# ============================================================================
# romconv_ace.py — Space Ace: da ace.zip a ace_rom.h.
#
# Rev A3: sa_a3_u1..u5, 5 pezzi da 8 KB in fila, 0x0000-0x9FFF.
# Non esisteva nessun convertitore per questo header, ne' qui ne' su
# CABINATONE: scritto il 2026-09-05.
# ⚠️ La formattazione riproduce quella dell'header che gira (16 valori per
# riga, `0x` maiuscolo, virgola anche in fondo all'ultima riga), cosi' il
# confronto col progetto vivo puo' essere byte per byte.
# ============================================================================
import os, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "ace_rom.h")

PEZZI = ["sa_a3_u%d.bin" % i for i in range(1, 6)]
DIM   = 8192


def main():
    zip_ace = os.path.join(ROMS, "ace.zip")
    if not os.path.isfile(zip_ace):
        print("ERRORE: manca ROMS\\ace.zip")
        return 1

    dati = bytearray()
    with zipfile.ZipFile(zip_ace) as z:
        dentro = {n.lower(): n for n in z.namelist()}
        for p in PEZZI:
            if p.lower() not in dentro:
                print("ERRORE: in ace.zip manca %s" % p)
                return 1
            d = z.read(dentro[p.lower()])
            if len(d) != DIM:
                print("ERRORE: %s e' di %d byte, ne servono %d" % (p, len(d), DIM))
                return 1
            dati += d

    righe = []
    for i in range(0, len(dati), 16):
        righe.append("  " + ",".join("0x%02X" % b for b in dati[i:i + 16]) + ",")

    with open(USCITA, "w", encoding="latin-1", newline="\n") as f:
        f.write("// Space Ace ROM (Rev A3): sa_a3_u1..u5, 5x8KB @ 0x0000-0x9FFF."
                " Generato da ace.zip.\n")
        f.write("#pragma once\n#include <stdint.h>\n")
        f.write("#define ACE_ROM_SIZE %d\n" % len(dati))
        f.write("static const uint8_t ACE_ROM[ACE_ROM_SIZE] = {\n")
        f.write("\n".join(righe) + "\n};\n")
    print("  ace_rom.h  (%d byte)" % len(dati))
    return 0


sys.exit(main())
