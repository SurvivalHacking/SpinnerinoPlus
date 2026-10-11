# ============================================================================
# romconv_bombjack_gfx.py — sprite e tile di Bomb Jack, versione GALAGINO.
#
# ⚠️ Serve perche' `romconv_bombjack.py` (che viene da CABINATONE) produce
# tabelle di FORMA diversa da quella che il firmware si aspetta:
#   bombjack_sprites.h   16.386 valori invece di 16.706
#   bombjack_bg_tiles.h   8.192  valori invece di  8.448
# Il resto di quel convertitore (le due ROM di CPU) e' giusto e resta.
# Gira DOPO di lui (ordine alfabetico).
#
# I tre script veri stanno in bombjack\ e vengono da GALAGINO NEW 22 GAMES:
# non li tocco, gli riscrivo solo i due percorsi che hanno dentro.
# ============================================================================
import os, subprocess, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
PASSI     = os.path.join(QUI, "bombjack")
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "bombjack")

LAVORI = [
    ("sprites_conv.py",  ["16_m07b.bin", "15_l07b.bin", "14_j07b.bin"],
     "bombjack_sprites.h"),
    ("tiles_bg_conv.py", ["06_l08t.bin", "07_n08t.bin", "08_r08t.bin"],
     "bombjack_bg_tiles.h"),
    ("tiles_fg_conv.py", ["03_e08t.bin", "04_h08t.bin", "05_k08t.bin"],
     "bombjack_fg_tiles.h"),
]


def main():
    zip_bj = os.path.join(ROMS, "bombjack.zip")
    if not os.path.isfile(zip_bj):
        print("ERRORE: manca ROMS\\bombjack.zip")
        return 1
    rom_dir = os.path.join(ROMS, "_estratte", "bombjack")
    os.makedirs(rom_dir, exist_ok=True)
    with zipfile.ZipFile(zip_bj) as z:
        z.extractall(rom_dir)
    os.makedirs(USCITA, exist_ok=True)

    for script, pezzi, header in LAVORI:
        origine = os.path.join(PASSI, script)
        if not os.path.isfile(origine):
            print("ERRORE: manca bombjack\\%s" % script)
            return 1
        testo = open(origine, encoding="latin-1").read()
        elenco = "[" + ", ".join('r"%s"' % os.path.join(rom_dir, p)
                                 for p in pezzi) + "]"
        fuori = 'r"%s"' % os.path.join(USCITA, header)
        nuovo = []
        for riga in testo.split("\n"):
            if riga.startswith("INPUT_ROM_FILES"):
                nuovo.append("INPUT_ROM_FILES = " + elenco)
            elif riga.startswith("OUTPUT_HEADER_FILE"):
                nuovo.append("OUTPUT_HEADER_FILE = " + fuori)
            else:
                nuovo.append(riga)
        temporaneo = os.path.join(PASSI, "_adattato.py")
        with open(temporaneo, "w", encoding="latin-1") as f:
            f.write("\n".join(nuovo))
        try:
            p = subprocess.run([sys.executable, temporaneo],
                               capture_output=True, text=True)
        finally:
            os.remove(temporaneo)
        if p.returncode != 0 or not os.path.isfile(os.path.join(USCITA, header)):
            print("ERRORE su %s -> %s" % (script, header))
            print((p.stdout or "") + (p.stderr or ""))
            return 1
        print("  %s" % header)
    return 0


sys.exit(main())
