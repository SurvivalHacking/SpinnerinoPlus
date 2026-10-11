# ============================================================================
# romconv_neogeo_tutti.py — regista dei giochi NEO GEO.
#
# `romconv_neogeo.py` converte UN gioco per volta e vuole il nome sulla riga di
# comando (senza, fa solo `mslug`). Qui si lanciano tutti quelli di cui c'e' la
# zip in ROMS\, cosi' entra nella fila di prepara.py come gli altri.
#
# ⚠️ Servono SEMPRE tre zip: quella del gioco, `neogeo.zip` (il BIOS) e
# `gngeo_data.zip` (le tabelle di decrittazione CMC). Senza, si salta.
# ⭐ I file finiscono in `DA COPIARE SU SD\neogeo\<gioco>\`: il Neo Geo NON ha
# la ROM negli header, la legge dalla microSD.
# ============================================================================
import os, subprocess, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
ROMS      = os.path.join(PACCHETTO, "ROMS")
SCRIPT    = os.path.join(QUI, "romconv_neogeo.py")
# ⛔⛔⛔ Il figlio si lancia SEMPRE passando da `_lancia.py`, mai con
# `sys.executable romconv_neogeo.py`: e' `_lancia.py` a chiamare
# `_ambiente.attiva()`, e senza quello il figlio gira SENZA il dirottamento dei
# percorsi. `romconv_neogeo.py` (come `romconv_outrun.py`) comincia con un
# `os.chdir(r"C:\Users\Tecnico\Desktop\PROGETTI SH\CABINATONE")` e legge le ROM
# da un percorso RELATIVO: su questo PC quella cartella esiste e non succede
# niente, su QUALUNQUE altro PC il chdir muore con
# «FileNotFoundError: [WinError 3] The system cannot find the path specified»
# e tutti e quattro i giochi Neo Geo restano col segnaposto.
# (referto di un amico, 2026-09-13 — riprodotto spegnendo `PROGETTI SH`.)
LANCIA    = os.path.join(QUI, "_lancia.py")

# I set che il firmware conosce. Si convertono solo quelli di cui c'e' la zip.
GIOCHI = ["mslug", "mslug2", "mslug3", "aof2"]


def main():
    for f in ("neogeo.zip", "gngeo_data.zip"):
        if not os.path.isfile(os.path.join(ROMS, f)):
            print("ERRORE: manca ROMS\\%s (serve a TUTTI i giochi Neo Geo)" % f)
            return 1

    fatti, saltati, guai = [], [], []
    for g in GIOCHI:
        if not os.path.isfile(os.path.join(ROMS, "%s.zip" % g)):
            saltati.append(g)
            continue
        p = subprocess.run([sys.executable, LANCIA, SCRIPT, g],
                           capture_output=True, text=True)
        if p.returncode == 0:
            fatti.append(g)
            print("  %s" % g)
        else:
            guai.append(g)
            print("  ERRORE su %s" % g)
            print((p.stdout or "")[-400:] + (p.stderr or "")[-400:])

    if saltati:
        print("  (saltati, zip mancante: %s)" % ", ".join(saltati))
    if guai:
        return 1
    if not fatti:
        print("ERRORE: nessuna zip di gioco Neo Geo in ROMS\\")
        return 1
    return 0


sys.exit(main())
