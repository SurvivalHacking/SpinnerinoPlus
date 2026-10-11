# ============================================================================
# romconv_arte_sd.py — le copertine che vanno sulla microSD.
#
# I `.scr` dei giochi Neo Geo non nascono dalle ROM: li fa
# `convert_neogeo_sd_logos.py` da IMMAGINI (le foto dei cabinati), che nel
# pacchetto non ci sono. Come i loghi negli header, viaggiano gia' pronti:
# qui vengono solo rimessi al loro posto dentro `DA COPIARE SU SD\`.
# Cosi' se quella cartella viene svuotata, `prepara` la ricostruisce intera.
# ============================================================================
import os, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
SORGENTE  = os.path.join(QUI, "arte_sd")
USCITA    = os.path.join(PACCHETTO, "DA COPIARE SU SD")

if not os.path.isdir(SORGENTE):
    print("(arte_sd\ assente: salto)")
    sys.exit(0)

n = 0
for radice, _d, files in os.walk(SORGENTE):
    for nome in files:
        src = os.path.join(radice, nome)
        rel = os.path.relpath(src, SORGENTE)
        dst = os.path.join(USCITA, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        # ⚠️ NON shutil.copyfile: confronta sorgente e destinazione con os.stat,
        # e lo stat di un file che ancora non esiste fa ritrovare al
        # dirottatore la SORGENTE -> «SameFileError». Si legge e si scrive.
        with open(src, "rb") as f_in:
            dati = f_in.read()
        with open(dst, "wb") as f_out:
            f_out.write(dati)
        n += 1
        print("  %s" % rel)
print("arte per la SD: %d file" % n)
