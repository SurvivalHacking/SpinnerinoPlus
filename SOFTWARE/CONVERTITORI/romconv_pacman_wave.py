# ============================================================================
# romconv_pacman_wave.py — pacman_wavetable.h con la catena galagino.
#
# ⚠️ Serve perche' `romconv_pacman.py` (che viene da CABINATONE) scrive una
# tavola di forma DIVERSA: 256 valori contro i 272 che il firmware si aspetta.
# Il resto di quel convertitore (rom, spritemap, tilemap) e' giusto e resta.
# Gira DOPO di lui: prepara.py li lancia in ordine alfabetico e
# `romconv_pacman_wave.py` viene dopo `romconv_pacman.py`.
#
# Riga di conv.sh trascritta:
#   ./audioconv.py pacman_wavetable 82s126.1m pacman_wavetable.h
# ============================================================================
import os, subprocess, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ROMS      = os.path.join(PACCHETTO, "ROMS")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "pacman",
                         "pacman_wavetable.h")
SCRIPT    = os.path.join(QUI, "galagino", "audioconv.py")
PEZZI     = ["82s126.1m"]   # ⚠️ UNA sola: conv.sh ne passa due e la
                            # tavola esce DOPPIA (544 valori invece di
                            # 272). Provate tutte e tre le combinazioni
                            # contro l'header che gira: solo 82s126.1m
                            # da' esattamente quello.

zip_pac = os.path.join(ROMS, "pacman.zip")
if not os.path.isfile(zip_pac):
    print("ERRORE: manca ROMS\pacman.zip")
    sys.exit(1)

rom_dir = os.path.join(ROMS, "_estratte", "pacman")
os.makedirs(rom_dir, exist_ok=True)
with zipfile.ZipFile(zip_pac) as z:
    z.extractall(rom_dir)

comando = ([sys.executable, SCRIPT, "pacman_wavetable"]
           + [os.path.join(rom_dir, p) for p in PEZZI] + [USCITA])
p = subprocess.run(comando, capture_output=True, text=True)
if p.returncode != 0 or not os.path.isfile(USCITA):
    print("ERRORE nel generare pacman_wavetable.h")
    print((p.stdout or "") + (p.stderr or ""))
    sys.exit(1)
print("  pacman_wavetable.h")
