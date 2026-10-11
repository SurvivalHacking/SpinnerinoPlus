# ============================================================================
# _fix_percorsi.py — eseguito UNA VOLTA dopo aver raccolto tutti i .py in PY\.
#
# Gli script calcolano i loro percorsi da `__file__` (`HERE`, `QUI`,
# `SCRIPT_DIR`, `_RC`... ognuno lo chiama a modo suo) e poi risalgono con "..".
# Spostandoli, quel ".." cambia significato e cercano ROM, immagini e header
# nel posto sbagliato.
#
# Invece di rincorrere ogni variabile in ogni file, si riscrive `__file__`:
# tutto quello che ne discende torna a puntare alla cartella di prima,
# **immagini sorgente comprese** (quelle non sono state spostate).
# Una riga, uguale per tutti, indipendente da come si chiama la variabile.
# ============================================================================
import os

QUI = os.path.dirname(os.path.abspath(__file__))

# Dove stava ciascun gruppo PRIMA dello spostamento, relativamente a PY\.
DA_FILE_ROMCONV_DEL_PROGETTO = ("..", "SPINNERINO", "FILE ROMCONV PY")
DA_MACHINES_POPEYE           = ("..", "SPINNERINO", "machines", "popeye")
DA_FILE_ROMCONV_DI_LIVELLO_ALTO = ("..", "..", "FILE ROMCONV PY")

GRUPPI = [
    (DA_FILE_ROMCONV_DEL_PROGETTO, [
        "convert_bombbee_logo.py", "convert_commando_logo.py",
        "convert_motorace_logo.py", "convert_si_samples.py",
        "convert_spaceinvaders_logo.py", "export_gfx_galaga.py",
        "romconv_commando.py", "romconv_dkong3.py", "romconv_gyruss.py",
        "romconv_motorace.py", "romconv_roadfighter.py",
        "romconv_spaceinvaders.py", "romconv_supercobra.py",
    ]),
    (DA_MACHINES_POPEYE, ["romconv_popeye.py"]),
    (DA_FILE_ROMCONV_DI_LIVELLO_ALTO, [
        "convert_laserdisc_card.py", "convert_laserdisc_game_logos.py",
        "convert_supxevs_grid_logo.py", "romconv_supxevs.py",
        "romconv_xevious_rom1.py",
    ]),
]

MARCATORE = "# --- percorsi: questo script viveva altrove"


def blocco(pezzi):
    args = ", ".join('"%s"' % p for p in pezzi)
    return (
        MARCATORE + ", vedi PY\\_fix_percorsi.py ---\n"
        "import os as _os\n"
        "__file__ = _os.path.abspath(_os.path.join(\n"
        "    _os.path.dirname(_os.path.abspath(__file__)), %s,\n"
        "    _os.path.basename(__file__)))\n"
        "# --- fine blocco percorsi ---\n\n" % args
    )


def punto_di_inserimento(righe):
    """Dopo shebang, coding e il blocco di commenti in testa: il codice va
    prima di ogni uso di __file__, ma non prima della riga di encoding."""
    i = 0
    while i < len(righe):
        s = righe[i].strip()
        if s.startswith("#!") or s.startswith("#") or s == "":
            i += 1
            continue
        break
    return i


fatti, saltati = 0, 0
for pezzi, nomi in GRUPPI:
    for nome in nomi:
        path = os.path.join(QUI, nome)
        if not os.path.exists(path):
            print("  MANCA:", nome)
            continue
        testo = open(path, "r", encoding="utf-8", errors="surrogateescape").read()
        if MARCATORE in testo:
            saltati += 1
            continue
        righe = testo.splitlines(keepends=True)
        k = punto_di_inserimento(righe)
        righe.insert(k, blocco(pezzi))
        open(path, "w", encoding="utf-8", errors="surrogateescape",
             newline="").write("".join(righe))
        fatti += 1
        print("  sistemato:", nome)

print("\n%d file sistemati, %d gia' a posto." % (fatti, saltati))
