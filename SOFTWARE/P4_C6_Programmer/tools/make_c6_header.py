#!/usr/bin/env python3
"""
Rigenera c6_firmware.h a partire dai QUATTRO file che Arduino IDE genera
con "Sketch -> Esporta binario compilato" sul progetto del C6:

  - <nome>.bootloader.bin
  - <nome>.partitions.bin
  - boot_app0.bin               (preso dal core, stesso per tutti gli sketch)
  - <nome>.ino.bin               (l'applicazione vera e propria)

A differenza del file "..._merged.bin" (immagine flat da 4MB, piena di
spazio vuoto per lo slot OTA secondario, la spiffs e il coredump mai
usati), qui embeddiamo SOLO i quattro pezzi realmente necessari, ai loro
offset reali -- molto piu' leggero (circa 700-800KB invece di 4MB).

IMPORTANTE: gli MD5 vengono salvati come stringhe ASCII esadecimali (32
caratteri), NON come array binari a 16 byte -- e' il formato richiesto da
esp_loader_flash_verify_known_md5() della libreria esp-serial-flasher.

Uso:
    python3 tools/make_c6_header.py \\
        percorso/bootloader.bin \\
        percorso/partitions.bin \\
        percorso/boot_app0.bin \\
        percorso/app.bin

Gli offset (0x0, 0x8000, 0xe000, 0x10000) sono quelli standard usati da
Arduino IDE per ESP32-C6 con lo schema di partizioni di default. Se il tuo
progetto C6 usa uno schema di partizioni diverso, controlla gli offset
nel file .partitions.bin (o nel log di compilazione di Arduino IDE) e
correggi la lista OFFSETS qui sotto di conseguenza.
"""
import hashlib
import sys
from pathlib import Path

# Offset standard per ESP32-C6 con schema di partizioni di default.
# Verificare e correggere se il progetto usa uno schema custom.
OFFSETS = {
    "BOOTLOADER": 0x0,
    "PARTITIONS": 0x8000,
    "BOOT_APP0":  0xe000,
    "APP":        0x10000,
}

def main():
    if len(sys.argv) != 5:
        print(__doc__)
        sys.exit(1)

    files = {
        "BOOTLOADER": Path(sys.argv[1]),
        "PARTITIONS": Path(sys.argv[2]),
        "BOOT_APP0":  Path(sys.argv[3]),
        "APP":        Path(sys.argv[4]),
    }
    dst = Path(__file__).resolve().parent.parent / "c6_firmware.h"

    total = 0
    with dst.open("w") as f:
        f.write("// File generato automaticamente -- NON MODIFICARE A MANO\n")
        f.write("// Rigenerare con tools/make_c6_header.py ogni volta che il firmware del C6 cambia.\n")
        f.write("#pragma once\n#include <stdint.h>\n\n")

        f.write("typedef struct {\n")
        f.write("  uint32_t offset;\n")
        f.write("  const uint8_t *data;\n")
        f.write("  uint32_t size;\n")
        f.write("  const char *md5_hex;   // stringa ASCII esadecimale di 32 caratteri (NON binario)\n")
        f.write("  const char *name;\n")
        f.write("} c6_flash_region_t;\n\n")

        for name, path in files.items():
            data = path.read_bytes()
            total += len(data)
            md5hex = hashlib.md5(data).hexdigest()
            f.write(f"// {name}: offset 0x{OFFSETS[name]:x}, {len(data)} byte, MD5 {md5hex}\n")
            f.write(f"static const uint8_t C6_{name}_BIN[] = {{\n")
            line = []
            for b in data:
                line.append(str(b))
                if len(line) == 24:
                    f.write(",".join(line) + ",\n")
                    line = []
            if line:
                f.write(",".join(line) + ",\n")
            f.write("};\n")
            f.write(f"static const uint32_t C6_{name}_SIZE = {len(data)}UL;\n")
            f.write(f'static const char C6_{name}_MD5_HEX[] = "{md5hex}";\n\n')

        f.write("static const c6_flash_region_t C6_FLASH_REGIONS[] = {\n")
        for name in files:
            f.write(f'  {{ 0x{OFFSETS[name]:x}UL, C6_{name}_BIN, C6_{name}_SIZE, C6_{name}_MD5_HEX, "{name}" }},\n')
        f.write("};\n")
        f.write(f"static const int C6_FLASH_REGIONS_COUNT = {len(files)};\n")
        f.write(f"static const uint32_t C6_FLASH_TOTAL_BYTES = {total}UL;\n")

    print(f"Scritto {dst}")
    print(f"  totale reale da scrivere: {total} byte (~{total/1024:.0f} KB)")

if __name__ == "__main__":
    main()
