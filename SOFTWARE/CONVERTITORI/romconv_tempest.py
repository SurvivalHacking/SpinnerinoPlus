#!/usr/bin/env python3
import os as _os, sys as _sys
def _project_root():
    d = _os.path.dirname(_os.path.abspath(__file__))
    # SPINNERINO PLUS: lo script sta in "CONVERTITORI" e la radice del pacchetto
    # (con ROMS/ e SPINNERINO/) e' la cartella sopra. Nessun percorso del PC
    # ([[feedback_mai_percorsi_del_mio_pc]]).
    if _os.path.basename(d).upper() in ("ROM CONVERTER PY", "CONVERTITORI"):
        d = _os.path.dirname(d)
    return d
_os.chdir(_project_root())
# ----------------------------------------------------------------------------
# romconv_tempest.py — TEMPEST (Atari 1981) per SPINNERINO PLUS, ROM in flash.
#
# Set MAME **tempest3** (driver `src/mame/atari/tempest.cpp`, riga
# `GAME( 1980, tempest3, tempest, tempest, tempest, tempest_state, empty_init,
# ROT270, "Atari", "Tempest (rev 3)", ...)`): e' quello dello zip, dieci ROM di
# programma da 2 KB (scheda A037383-01/02). I 12 CRC qui sotto sono quelli di
# `ROM_START( tempest3 )`, controllati file per file
# ([[feedback_set_rom_sbagliato_e_alias]]). Lo zip NON contiene le PROM
# (AVG 136002-125, Math Box 126..132): il porting non le usa (AVG e Math Box
# sono emulati per funzioni, come il MAME storico e atari/mathbox.cpp).
#
# Ingresso : ROMS/tempest.zip
# Uscita   : SPINNERINO/machines/tempest/
#   tempest_rom_cpu.h     20480 byte = $9000-$DFFF (il guscio mappa anche
#                         $F800-$FFFF su $D800, il ROM_RELOAD di MAME; $F000-
#                         $F7FF in tempest3 e' VUOTO)
#   tempest_vector_rom.h   4096 byte = $3000-$3FFF (136002-123 + 136002-124)
#
# ⛔ Il convertitore precedente (stesso nome, progetto SPINNERINO NEW P4p)
#    aveva il percorso dello zip SCRITTO del PC e copiava $D000-$DFFF anche in
#    $F000-$F7FF, che e' il mirror del set PADRE (ROM da 4 KB), non di tempest3.
# ----------------------------------------------------------------------------
import zipfile, zlib

ZIP = _os.path.join("ROMS", "tempest.zip")
OUT = _os.path.join("SPINNERINO", "machines", "tempest")

# (file nello zip, indirizzo, CRC di MAME ROM_START(tempest3))
CPU = [
    ("136002.113", 0x9000, 0x65d61fe7),
    ("136002.114", 0x9800, 0x11077375),
    ("136002.115", 0xA000, 0xf3e2827a),
    ("136002.316", 0xA800, 0xaeb0f7e9),
    ("136002.217", 0xB000, 0xef2eb645),
    ("136002.118", 0xB800, 0xbeb352ab),
    ("136002.119", 0xC000, 0xa4de050f),
    ("136002.120", 0xC800, 0x35619648),
    ("136002.121", 0xD000, 0x73d38e47),
    ("136002.222", 0xD800, 0x707bd5c3),
]
VEC = [
    ("136002.123", 0x000, 0x29f7e937),     # "May be labeled 136002-111, same data"
    ("136002.124", 0x800, 0xc16ec351),     # "May be labeled 136002-112, same data"
]
# nomi alternativi con cui gli stessi file compaiono negli zip MAME recenti
ALIAS = {"136002.113": ["136002-113.d1"], "136002.114": ["136002-114.e1"],
         "136002.115": ["136002-115.f1"], "136002.316": ["136002-316.h1"],
         "136002.217": ["136002-217.j1"], "136002.118": ["136002-118.k1"],
         "136002.119": ["136002-119.lm1"], "136002.120": ["136002-120.mn1"],
         "136002.121": ["136002-121.p1"], "136002.222": ["136002-222.r1"],
         "136002.123": ["136002-123.np3", "136002.111", "136002-111.np3"],
         "136002.124": ["136002-124.r3", "136002.112", "136002-112.r3"]}


def leggi(z, nome, crc):
    nomi = {n.lower(): n for n in z.namelist()}
    for cand in [nome] + ALIAS.get(nome, []):
        if cand.lower() in nomi:
            d = z.read(nomi[cand.lower()])
            if len(d) != 0x800:
                _sys.exit("ERRORE: %s e' lungo %d byte, attesi 2048" % (cand, len(d)))
            if (zlib.crc32(d) & 0xffffffff) != crc:
                _sys.exit("ERRORE: %s ha CRC %08x, atteso %08x (set sbagliato?)"
                          % (cand, zlib.crc32(d) & 0xffffffff, crc))
            return d
    _sys.exit("ERRORE: manca %s in %s" % (nome, ZIP))


def scrivi(percorso, nome, dati, nota):
    righe = ["// Generato da CONVERTITORI/romconv_tempest.py - NON modificare a mano",
             "// %s" % nota,
             "#pragma once",
             "const unsigned int %s_size = %d;" % (nome, len(dati)),
             "const unsigned char %s[] PROGMEM = {" % nome]
    for i in range(0, len(dati), 16):
        righe.append("  " + ", ".join("0x%02X" % b for b in dati[i:i + 16]) + ",")
    righe.append("};")
    with open(percorso, "w", newline="\r\n") as f:
        f.write("\n".join(righe) + "\n")
    print("  scritto %s (%d byte)" % (percorso, len(dati)))


def main():
    if not _os.path.isfile(ZIP):
        _sys.exit("ERRORE: manca %s (metti tempest.zip nella cartella ROMS)" % ZIP)
    with zipfile.ZipFile(ZIP) as z:
        cpu = bytearray(0x5000)
        for nome, ind, crc in CPU:
            off = ind - 0x9000
            cpu[off:off + 0x800] = leggi(z, nome, crc)
        vec = bytearray(0x1000)
        for nome, off, crc in VEC:
            vec[off:off + 0x800] = leggi(z, nome, crc)
    # controllo: vettori del 6502 letti da $F800 = $D800 (ROM_RELOAD)
    rst = cpu[0x4FFC] | cpu[0x4FFD] << 8
    irq = cpu[0x4FFE] | cpu[0x4FFF] << 8
    print("  reset $%04X  irq $%04X  (attesi $D93F e $D704)" % (rst, irq))
    if (rst, irq) != (0xD93F, 0xD704):
        _sys.exit("ERRORE: vettori inattesi")
    _os.makedirs(OUT, exist_ok=True)
    scrivi(_os.path.join(OUT, "tempest_rom_cpu.h"), "tempest_rom_cpu", cpu,
           "CPU $9000-$DFFF (20 KB), set MAME tempest3")
    scrivi(_os.path.join(OUT, "tempest_vector_rom.h"), "tempest_vector_rom", vec,
           "Vector ROM $3000-$3FFF (4 KB), set MAME tempest3")
    print("OK")


if __name__ == "__main__":
    main()
