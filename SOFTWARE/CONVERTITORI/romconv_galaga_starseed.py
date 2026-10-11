# ============================================================================
# romconv_galaga_starseed.py — il campo stellato di Galaga.
#
# ⭐ Non serve nessuna ROM: le stelle di Galaga non sono dati, sono il
# risultato di un generatore pseudo-casuale a scorrimento, e `starsets.py` di
# galagino lo riproduce. Percio' questo header si rigenera SEMPRE, anche se
# l'amico non ha la zip di Galaga.
# ============================================================================
import os, subprocess, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
SCRIPT    = os.path.join(QUI, "galagino", "starsets.py")
USCITA    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "galaga",
                         "galaga_starseed.h")

if not os.path.isfile(SCRIPT):
    print("ERRORE: manca galagino\\starsets.py")
    sys.exit(1)

os.makedirs(os.path.dirname(USCITA), exist_ok=True)
p = subprocess.run([sys.executable, SCRIPT, USCITA], capture_output=True, text=True)
if p.returncode != 0 or not os.path.isfile(USCITA):
    print("ERRORE nel generare galaga_starseed.h")
    print((p.stdout or "") + (p.stderr or ""))
    sys.exit(1)
print("  galaga_starseed.h")
