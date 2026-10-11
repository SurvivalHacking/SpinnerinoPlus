#!/usr/bin/env python3
# ============================================================================
# romconv_xevious_rom1.py — SPINNERINO PLUS WIFI
#
# Rigenera  machines/xevious/xevious_rom_cpu1.h  (ROM Z80 principale, 16 KB)
# partendo dal set MAME **PARENT** `xevious.zip`.
#
# PERCHE' ESISTE (2026-08-03)
# ---------------------------
# Il porting girava con il set **xevios** (bootleg), che nell'attract scrive
# «© 1980 WATSON CO.» al posto del copyright e del logo **namco** originali.
# Il set parent invece esegue davvero il POST sulle custom Namco (50XX/51XX/
# 54XX/06XX) che qui non sono emulate ma solo SIMULATE: senza aiuto resterebbe
# per sempre sulla schermata "SOUND n".
# La soluzione (gia' collaudata su CABINATONE) e' partire dalla ROM ORIGINALE e
# applicarle i **12 byte di bypass** che il bootleg ha gia' — e SOLO quelli: gli
# altri 112 byte di differenza fra i due set sono il TESTO del copyright, che
# cosi' resta quello vero.
#
# ⛔ NON serve nient'altro: la palette di SPINNERINO va benissimo, «© 1982
#    NAMCO LTD.» e il logo namco escono rossi lo stesso. Gli altri dieci file
#    (grafica, terreno, sprite, wave) restano quelli che ci sono gia'.
# ⚠️ La patch a 0x01DD (JR Z -> JR) NON va messa qui: e' gia' applicata a
#    runtime in xevious.cpp (`if(current_cpu == 0 && Addr == 0x01DD) return 0x18;`)
#    e serve a ENTRAMBI i set.
# ⛔ La storia della "patch di 11 byte a 0x3ed4 = routine del terreno" e' FALSA:
#    a 0x3ED4 c'e' la stringa del copyright, non codice. Lo sfondo pulito
#    dipende dalle tre ROM del terreno originali, non da quella patch.
#
# USO:  python romconv_xevious_rom1.py [xevious.zip] [xevious_rom_cpu1.h]
#       senza argomenti usa i percorsi di default qui sotto.
#
# VERIFICA: a fine lavoro stampa il CRC32, che DEVE essere 095A19DA — lo stesso
#           del rom1.bin di CABINATONE, dove questo set gira sul device.
#
# ============================================================================
# ✅ XEVIOUS CHIUSO — 2026-08-03. Stato finale confermato sul device.
# Questo converter tocca SOLO la ROM. La geometria del video sta in
# machines/xevious/xevious.cpp e i tre numeri che la definiscono sono, nell'ordine
# in cui si e' arrivati a fissarli:
#
#   riga 403  sprite   sx = sr1[offs+1] - 45      <- basi/bunker
#   riga 460  BG       hw_scroll_x = ... + 28     <- terreno (l'inquadratura
#                                                    storica di SPINNERINO: il
#                                                    driver MAME direbbe 20)
#   riga 556  FG       fg_scroll_x = ... + 40     <- banda colorata del logo
#                                                    dell'attract (era 32)
#
# ⛔ Sono una TERNA: -45 e +28 insieme tengono le basi ferme sulle radure. Se un
#    giorno si porta il BG a +20 per allinearsi al driver, vanno rifatti anche
#    gli altri due, non uno solo.
# ⭐ Il FG e' il piano della BANDA COLORATA del logo; le lettere stanno nel BG e
#    nell'attract gli sprite attivi sono ZERO. Misurato separando i piani con
#    CABINATONE\_verify_gfx\xevious_harness\mk_xevpiani.py (azzera la VRAM di un
#    piano e rende l'altro) e mk_xevsx.py: su quell'asse **+1 = 1 px piu' in alto**.
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

# --- percorsi di default ----------------------------------------------------
# Lo zip del set parent sta in CABINATONE (unica copia sul PC dell'utente).
ZIP_DEFAULT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "..", "CABINATONE", "ROMS", "xevious.zip")
OUT_DEFAULT = os.path.join(
    os.path.dirname(os.path.abspath(__file__)),
    "..", "BACKUP SPINNERINO PLUS", "SPINNERINO", "machines", "xevious", "xevious_rom_cpu1.h")

CRC_ATTESO = 0x095A19DA

# --- i 12 bypass sulla ROM principale (indirizzo: valore) -------------------
# Ricavati dal diff con il set xevios, che li ha gia' applicati. Sono tutti
# controlli che aspettano o verificano le custom Namco. Trascritti VERBATIM da
# CABINATONE/romconv_xevious.py: le due copie devono restare identiche.
BYPASS = {
    0x0261: 0x00, 0x0262: 0x00,   # JR NZ,+15   -> NOP NOP
    0x02AF: 0x18,                 # JR C        -> JR (incondizionato)
    0x0313: 0x00, 0x0314: 0x00,   # JR Z,-7 (attesa di un bit dalla custom) -> NOP
    0x0469: 0x1F, 0x046A: 0x00,
    0x0994: 0xC9,                 # RET: chiude qui la routine di POST
    0x09E8: 0xC9,                 # RET
    0x13E0: 0x00, 0x13E1: 0x00, 0x13E2: 0x00,   # JP NZ,0000 (reset su checksum) -> NOP
}

# I quattro quarti da 4 KB della CPU principale, nell'ordine della ROM_REGION.
CPU1_PARTI = ["xvi_1.3p", "xvi_2.3m", "xvi_3.2m", "xvi_4.2l"]

INTESTAZIONE = """\
// Xevious Main CPU ROM (16KB: $0000-$3FFF)
// = set MAME **PARENT** `xevious.zip` + i 12 byte di bypass delle custom Namco
// (50XX/51XX/54XX/06XX), che qui sono simulate e non emulate.
// -> l'attract mostra il logo XEVIOUS e «© 1982 NAMCO LTD.» con il namco ROSSO,
//    al posto del «© 1980 WATSON CO.» del bootleg xevios usato prima.
// I 112 byte di sola differenza testuale restano quelli originali.
// CRC32 = 095A19DA (lo stesso rom1.bin che gira su CABINATONE).
// ⚠️ NON modificare a mano: rigenerare con FILE ROMCONV PY/romconv_xevious_rom1.py
// 2026-08-03.
const unsigned char xevious_rom_cpu1[] = {
"""


def leggi(z, nome, atteso):
    """Legge una ROM dallo zip: nome esatto, poi ignorando maiuscole/percorso."""
    try:
        dati = z.read(nome)
    except KeyError:
        trovato = None
        for n in z.namelist():
            if os.path.basename(n).lower() == nome.lower():
                trovato = n
                break
        if trovato is None:
            raise SystemExit("ERRORE: '%s' non trovato nello zip" % nome)
        dati = z.read(trovato)
    if len(dati) != atteso:
        raise SystemExit("ERRORE: '%s' e' %d byte invece di %d"
                         % (nome, len(dati), atteso))
    return dati


def main():
    zip_path = sys.argv[1] if len(sys.argv) > 1 else ZIP_DEFAULT
    out_path = sys.argv[2] if len(sys.argv) > 2 else OUT_DEFAULT

    if not os.path.isfile(zip_path):
        raise SystemExit("ERRORE: zip non trovato: %s" % zip_path)

    z = zipfile.ZipFile(zip_path, "r")
    rom = bytearray()
    for nome in CPU1_PARTI:
        rom += leggi(z, nome, 4096)
    z.close()

    for indirizzo, valore in BYPASS.items():
        rom[indirizzo] = valore

    crc = zlib.crc32(bytes(rom)) & 0xFFFFFFFF
    print("ROM principale : %d byte" % len(rom))
    print("bypass applicati: %d" % len(BYPASS))
    print("CRC32          : %08X (atteso %08X)" % (crc, CRC_ATTESO))
    if crc != CRC_ATTESO:
        raise SystemExit(
            "ERRORE: CRC diverso da quello atteso -> lo zip non e' il set parent,\n"
            "        oppure i bypass non sono piu' allineati a CABINATONE.\n"
            "        NON scrivo l'header.")

    righe = []
    for i in range(0, len(rom), 16):
        righe.append("  " + ",".join("0x%02x" % b for b in rom[i:i + 16]))
    corpo = ",\n".join(righe)

    out_path = os.path.abspath(out_path)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(INTESTAZIONE)
        f.write(corpo)
        f.write("\n};\n")

    print("scritto        : %s" % out_path)


if __name__ == "__main__":
    main()
