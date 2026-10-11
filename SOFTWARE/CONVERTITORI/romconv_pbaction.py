# ============================================================================
# romconv_pbaction.py — Pinball Action: da pbaction.zip ai 5 header.
#
# La catena e' in DUE passi e li fanno due script che esistevano gia', qui
# dentro in pbaction\ e non modificati:
#   1. zip2bin.py  (da GALAGONE PAOLO ULTIMO): pbaction.zip -> cinque .bin,
#      con i CRC32 di ogni pezzo verificati;
#   2. bin2h.py    (da CONSOLE_QUADRA_NANO):   i cinque .bin -> i cinque .h.
# Questo file fa solo da regista e gli passa i percorsi giusti, perche' i loro
# erano quelli del PC di chi li ha scritti.
# ============================================================================
import os, subprocess, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
PASSI     = os.path.join(QUI, "pbaction")
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "pbaction")
BIN       = os.path.join(ROMS, "_estratte", "pbaction_bin")

ATTESI = ["pbaction_main.bin", "pbaction_audio.bin", "pbaction_fg.bin",
          "pbaction_bg.bin", "pbaction_sprites.bin"]


def main():
    zip_pb = os.path.join(ROMS, "pbaction.zip")
    if not os.path.isfile(zip_pb):
        print("ERRORE: manca ROMS\\pbaction.zip")
        return 1

    os.makedirs(BIN, exist_ok=True)
    os.makedirs(USCITA, exist_ok=True)

    # passo 1: zip -> .bin
    p = subprocess.run([sys.executable, os.path.join(PASSI, "zip2bin.py"),
                        zip_pb, BIN], capture_output=True, text=True)
    mancanti = [b for b in ATTESI if not os.path.isfile(os.path.join(BIN, b))]
    if mancanti:
        print("ERRORE nel passo 1 (zip -> bin): mancano %s" % ", ".join(mancanti))
        print((p.stdout or "") + (p.stderr or ""))
        return 1

    # passo 2: .bin -> .h  (lo script ha i percorsi dentro: glieli riscriviamo
    # in una copia usa-e-getta, cosi' l'originale resta leggibile com'era)
    sorgente = open(os.path.join(PASSI, "bin2h.py"), encoding="latin-1").read()
    sorgente = sorgente.replace(
        'P4_STAGING = r"C:\\Users\\Tecnico\\Desktop\\PROGETTI SH\\GALAGONE PAOLO '
        'ULTIMO\\GALAGONE_4.3_P4_NEW_multi\\sd_rom_staging"',
        'P4_STAGING = r"%s"' % BIN)
    sorgente = sorgente.replace(
        'OUT_DIR    = os.path.join(HERE, "machines", "pbaction")',
        'OUT_DIR    = r"%s"' % USCITA)
    temporaneo = os.path.join(PASSI, "_bin2h_adattato.py")
    with open(temporaneo, "w", encoding="latin-1") as f:
        f.write(sorgente)
    try:
        p = subprocess.run([sys.executable, temporaneo],
                           capture_output=True, text=True)
    finally:
        os.remove(temporaneo)

    fatti = [n for n in os.listdir(USCITA) if n.endswith(".h")]
    if p.returncode != 0 or len(fatti) < 5:
        print("ERRORE nel passo 2 (bin -> h)")
        print((p.stdout or "") + (p.stderr or ""))
        return 1
    print("pbaction: 5 header scritti")
    return 0


sys.exit(main())
