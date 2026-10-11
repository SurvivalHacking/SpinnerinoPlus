#!/usr/bin/env python3
# ============================================================================
# romconv_supxevs.py - SPINNERINO PLUS WIFI
#
# Vs. Super Xevious (Namco 1986) -> header PROGMEM per il porting
# machines/supxevs/.
#
# ATTENZIONE, NON E' L'XEVIOUS ARCADE
# -----------------------------------
# Il set `supxevs` NON gira sull'hardware Namco a 3x Z80 di `xevious`: e' un
# **Nintendo VS. UniSystem**, cioe' un NES da sala giochi (CPU RP2A03 + PPU
# RP2C04-0001). Driver MAME: src/mame/nintendo/vsnes.cpp.
# Il Super Xevious ARCADE Namco e' un altro set (`sxevious`) e da' un altro
# porting - vedi i commenti in machines/supxevs/supxevs.h.
#
# CONTENUTO DELLO ZIP (misurato, CRC32 verificati qui sotto):
#   prg2  64 KB  -> PRG $00000   (regione "prg" di MAME, 128 KB in totale)
#   prg1  64 KB  -> PRG $10000
#   cha   32 KB  -> CHR (PPU pattern table, 32 banchi da 1 KB)
#   rp2c04-0001.pal  192 byte -> 64 colori RGB888 della PPU VS
#
# ⛔ L'ORDINE prg2-poi-prg1 NON E' UN REFUSO: e' quello di ROM_START(supxevs)
#    in vsnes.cpp. Invertirlo sposta tutti i banchi e il gioco non parte.
#
# OUTPUT (in machines/supxevs/):
#   supxevs_prg.h  const unsigned char supxevs_prg[131072]
#   supxevs_chr.h  const unsigned char supxevs_chr[32768]
#   supxevs_pal.h  const unsigned short supxevs_pal[64]  (RGB565 byte-swapped)
#
# USO:  python romconv_supxevs.py [supxevs.zip] [cartella_output]
# ============================================================================
# --- percorsi: questo script viveva altrove, vedi PY\_fix_percorsi.py ---
import os as _os
__file__ = _os.path.abspath(_os.path.join(
    _os.path.dirname(_os.path.abspath(__file__)), "..", "..", "FILE ROMCONV PY",
    _os.path.basename(__file__)))
# --- fine blocco percorsi ---

import os
import sys
import zlib
import zipfile

QUI = os.path.dirname(os.path.abspath(__file__))
# ⛔ "BACKUP SPINNERINO PLUS\SPINNERINO", non "SPINNERINO": il progetto su cui
# l'utente compila e flasha e' quello dentro BACKUP, malgrado il nome. La
# cartella SPINNERINO di primo livello e' una copia VECCHIA — prima di questa
# correzione lo script scriveva li', cioe' in un progetto che non viene
# compilato da nessuno.
VIVO = os.path.join(QUI, "..", "BACKUP SPINNERINO PLUS", "SPINNERINO")
ZIP_DEFAULT = os.path.join(VIVO, "ROM SPINNERINO", "supxevs.zip")
OUT_DEFAULT = os.path.join(VIVO, "machines", "supxevs")

# nome nello zip -> (dimensione attesa, CRC32 atteso da ROM_START(supxevs))
ATTESI = {
    "prg1":            (0x10000, 0xFF762CEB),
    "prg2":            (0x10000, 0x645669F0),
    "cha":             (0x08000, 0xE27C7434),
    "rp2c04-0001.pal": (0x000C0, 0xA6293FAA),
}


def rgb565_swap(r, g, b):
    """RGB888 -> RGB565 byte-swapped: e' il formato colore di tutto il progetto
    (display SPI big-endian). Stessa funzione di romconv_xevious.py."""
    c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
    return ((c >> 8) & 0xFF) | ((c & 0xFF) << 8)


def pal3bit(v):
    """⛔ La PROM del 2C04 tiene **3 BIT per canale** (valori 0..7), NON RGB888:
    misurato sul file (max = 7) e confermato dal sorgente MAME, che fa
    `pal3bit(m_palette_data[color_num * 3 + n])` in ppu2c0x_rgb_device::
    init_palette_tables(). Prendere quei byte per RGB888 dava un'immagine
    quasi tutta nera (7/255 di rosso invece di 255/255)."""
    v &= 7
    return (v << 5) | (v << 2) | (v >> 1)


def leggi(z, nome):
    """Legge un file dallo zip verificandone dimensione e CRC32."""
    dati = None
    try:
        dati = z.read(nome)
    except KeyError:
        for n in z.namelist():
            if os.path.basename(n).lower() == nome.lower():
                dati = z.read(n)
                break
    if dati is None:
        raise SystemExit("ERRORE: '%s' non trovato nello zip" % nome)

    atteso_len, atteso_crc = ATTESI[nome]
    if len(dati) != atteso_len:
        raise SystemExit("ERRORE: '%s' e' %d byte invece di %d"
                         % (nome, len(dati), atteso_len))
    crc = zlib.crc32(dati) & 0xFFFFFFFF
    if crc != atteso_crc:
        raise SystemExit("ERRORE: '%s' ha CRC %08X invece di %08X: non e' il set supxevs"
                         % (nome, crc, atteso_crc))
    print("  %-16s %6d byte  CRC=%08X ok" % (nome, len(dati), crc))
    return dati


def scrivi_bytes(path, nome_array, dati, intestazione):
    righe = []
    for i in range(0, len(dati), 16):
        righe.append("  " + ",".join("0x%02x" % b for b in dati[i:i + 16]))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(intestazione)
        f.write("const unsigned char %s[%d] = {\n" % (nome_array, len(dati)))
        f.write(",\n".join(righe))
        f.write("\n};\n")
    print("  scritto %s (%d byte)" % (path, len(dati)))


def scrivi_words(path, nome_array, valori, intestazione):
    righe = []
    for i in range(0, len(valori), 8):
        righe.append("  " + ",".join("0x%04x" % v for v in valori[i:i + 8]))
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(intestazione)
        f.write("const unsigned short %s[%d] = {\n" % (nome_array, len(valori)))
        f.write(",\n".join(righe))
        f.write("\n};\n")
    print("  scritto %s (%d colori)" % (path, len(valori)))


def main():
    zip_path = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else ZIP_DEFAULT)
    out_dir = os.path.abspath(sys.argv[2] if len(sys.argv) > 2 else OUT_DEFAULT)

    if not os.path.isfile(zip_path):
        raise SystemExit("ERRORE: zip non trovato: %s" % zip_path)
    os.makedirs(out_dir, exist_ok=True)

    print("zip: %s" % zip_path)
    z = zipfile.ZipFile(zip_path, "r")
    prg1 = leggi(z, "prg1")
    prg2 = leggi(z, "prg2")
    cha = leggi(z, "cha")
    pal = leggi(z, "rp2c04-0001.pal")
    z.close()

    # ROM_START(supxevs): prg2 a $00000, prg1 a $10000.
    prg = prg2 + prg1

    scrivi_bytes(
        os.path.join(out_dir, "supxevs_prg.h"), "supxevs_prg", prg,
        "// Vs. Super Xevious - PRG ROM 128 KB (16 banchi da 8 KB).\n"
        "// Ordine da ROM_START(supxevs) in MAME vsnes.cpp: prg2 a $00000, prg1 a $10000.\n"
        "// All'avvio il mapper 108 punta i 4 slot agli ULTIMI 32 KB (banchi 12..15).\n"
        "// (!) NON modificare a mano: rigenerare con FILE ROMCONV PY/romconv_supxevs.py\n")

    scrivi_bytes(
        os.path.join(out_dir, "supxevs_chr.h"), "supxevs_chr", cha,
        "// Vs. Super Xevious - CHR ROM 32 KB = pattern table della PPU,\n"
        "// vista dal mapper 108 come 32 banchi da 1 KB.\n"
        "// (!) NON modificare a mano: rigenerare con FILE ROMCONV PY/romconv_supxevs.py\n")

    colori = [rgb565_swap(pal3bit(pal[i * 3]), pal3bit(pal[i * 3 + 1]), pal3bit(pal[i * 3 + 2]))
              for i in range(64)]
    scrivi_words(
        os.path.join(out_dir, "supxevs_pal.h"), "supxevs_pal", colori,
        "// Vs. Super Xevious - palette della PPU **RP2C04-0001** (64 colori).\n"
        "// La PPU del VS. System non ha i colori NES: li prende da questa PROM,\n"
        "// che nel set e' il file `rp2c04-0001.pal` (64 x RGB888).\n"
        "// Qui convertiti in RGB565 byte-swapped come tutto il resto del progetto.\n"
        "// (!) NON modificare a mano: rigenerare con FILE ROMCONV PY/romconv_supxevs.py\n")

    print("PRG %d byte, CHR %d byte, palette 64 colori: fatto." % (len(prg), len(cha)))


if __name__ == "__main__":
    main()
