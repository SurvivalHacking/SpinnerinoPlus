# ============================================================================
# romconv_dl2rom.py — dl2_rom.h: la ROM di Dragon's Lair II (Leland 1991) v3.19.
#
# Sorgente: `dl2_319.bin`, 64 KB, CRC32 E9453A1B — sta nei pacchetti Daphne di
# DL2 (cartella `dl2_roms`). Mettilo in VIDEO\ o in ROMS\: lo trova comunque.
#
# ⭐ La ROM viene copiata GREZZA: le due patch obbligatorie di daphne si
# applicano a RUNTIME in `dl2_hw.cpp` (`dl2_patch_rom`), cosi' restano
# leggibili invece che cotte qui dentro. Non toccarle qui.
# ============================================================================
import os, sys, zipfile, zlib

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "dl2_rom.h")
NOME      = "dl2_319.bin"
DIM       = 0x10000
CRC       = 0xE9453A1B


def cerca():
    """Il .bin sciolto, oppure dentro una qualsiasi zip di VIDEO\\ o ROMS\\."""
    for c in ("VIDEO", "ROMS"):
        radice = os.path.join(PACCHETTO, c)
        if not os.path.isdir(radice):
            continue
        for cartella, _d, files in os.walk(radice):
            for n in files:
                if n.lower() == NOME:
                    return open(os.path.join(cartella, n), "rb").read()
        for cartella, _d, files in os.walk(radice):
            for n in files:
                if not n.lower().endswith(".zip"):
                    continue
                try:
                    with zipfile.ZipFile(os.path.join(cartella, n)) as z:
                        for i in z.namelist():
                            if os.path.basename(i).lower() == NOME:
                                return z.read(i)
                except Exception:
                    pass
    return None


def main():
    dati = cerca()
    if dati is None:
        print("ERRORE: non trovo %s (mettilo in VIDEO\\ o ROMS\\, anche"
              " dentro una zip)" % NOME)
        return 1
    if len(dati) != DIM:
        print("ERRORE: %s e' di %d byte, ne servono %d" % (NOME, len(dati), DIM))
        return 1
    crc = zlib.crc32(dati) & 0xFFFFFFFF
    if crc != CRC:
        print("ATTENZIONE: CRC32 %08X invece di %08X: e' un'altra revisione."
              % (crc, CRC))

    righe = ["  " + ",".join("0x%02X" % b for b in dati[i:i + 16])
             for i in range(0, DIM, 16)]
    with open(USCITA, "w", encoding="latin-1", newline="\n") as f:
        f.write("#pragma once\n")
        f.write("// dl2_rom.h - Dragon's Lair II (Leland 1991) ROM v3.19, 64 KB.\n")
        f.write("// Sorgente: dl2_319.bin, CRC32 %08X. Mappato a 0xF0000.\n" % crc)
        f.write("// ROM GREZZO: le patch di daphne si applicano a RUNTIME in\n"
                "// dl2_hw.cpp (dl2_patch_rom).\n")
        f.write("#include <stdint.h>\n\n")
        f.write("static const uint8_t dl2_rom[0x10000] = {\n")
        f.write(",\n".join(righe) + "\n};\n")
    print("  dl2_rom.h  (64 KB, CRC32 %08X)" % crc)
    return 0


sys.exit(main())
