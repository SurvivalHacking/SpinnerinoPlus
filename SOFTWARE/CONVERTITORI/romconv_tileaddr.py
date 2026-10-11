# ============================================================================
# romconv_tileaddr.py — machines/tileaddr.h, la tabella degli indirizzi dei
# tile condivisa da piu' giochi.
#
# ⭐ Non serve nessuna ROM: e' una tabella CALCOLATA, e `tileaddr.py` di
# galagino la genera. Come lo starseed di Galaga, questo header si rifa'
# sempre, anche a ROMS\ vuota.
# ============================================================================
import os, subprocess, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
SCRIPT    = os.path.join(QUI, "galagino", "tileaddr.py")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "tileaddr.h")

if not os.path.isfile(SCRIPT):
    print("ERRORE: manca galagino\tileaddr.py")
    sys.exit(1)
os.makedirs(os.path.dirname(USCITA), exist_ok=True)
p = subprocess.run([sys.executable, SCRIPT, USCITA], capture_output=True, text=True)
if p.returncode != 0 or not os.path.isfile(USCITA):
    print("ERRORE nel generare tileaddr.h")
    print((p.stdout or "") + (p.stderr or ""))
    sys.exit(1)
# ⚠️ galagino include questo file UNA volta sola e non ci mette le guardie.
# Qui lo includono NOVE giochi: senza guardie il compilatore si ferma con
# «redefinition of tileaddr» ripetuto per ognuno. La versione che gira le ha.
testo = open(USCITA, encoding="latin-1").read()
if "_TILEADDR_H_" not in testo:
    with open(USCITA, "w", encoding="latin-1", newline="\n") as f:
        f.write("#ifndef _TILEADDR_H_\n#define _TILEADDR_H_\n\n")
        f.write(testo)
        f.write("\n#endif\n")
print("  tileaddr.h")
