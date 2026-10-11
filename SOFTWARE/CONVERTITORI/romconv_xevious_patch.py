# ============================================================================
# romconv_xevious_patch.py — rimette in xevious_bgdata.h le 20 correzioni che
# nel progetto che GIRA erano state fatte a mano.
#
# Perche' serve: `romconv_xevious.py` ricava l'header dalla ROM ed e' giusto
# in 16.366 valori su 16.386. Gli altri 20, tutti raggruppati verso la fine
# della mappa dello sfondo, nella ROM non ci sono: qualcuno li ha corretti nel
# porting. Senza, quel pezzo di fondale esce sbagliato.
#
# ⭐ E' lo stesso meccanismo che il progetto usa gia' per Gyruss
# (`gyruss_patches\*.table`): la patch e' un dato piccolo e dichiarato, non una
# ROM travestita — venti numeri con la loro posizione.
#
# Gira DOPO romconv_xevious.py (prepara.py li lancia in ordine alfabetico).
# ============================================================================
import os, re, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
PATCH     = os.path.join(QUI, "xevious_patches", "bgdata_patch.table")
HEADER    = os.path.join(PACCHETTO, "SPINNERINO", "machines", "xevious",
                         "xevious_bgdata.h")

if not os.path.isfile(PATCH):
    print("(patch bgdata assente: salto)")
    sys.exit(0)
if not os.path.isfile(HEADER):
    print("(xevious_bgdata.h non ancora generato: salto)")
    sys.exit(0)

correzioni = {}
with open(PATCH, encoding="latin-1") as f:
    for riga in f:
        if riga.startswith("#") or "\t" not in riga:
            continue
        pos, valore = riga.split("\t")
        correzioni[int(pos)] = int(valore.strip(), 0)

testo = open(HEADER, encoding="latin-1").read()
inizio = testo.index("{")
testa, corpo = testo[:inizio], testo[inizio:]

# Si sostituisce il numero N-esimo, senza toccare la formattazione intorno.
NUM = re.compile(r"0x[0-9A-Fa-f]+|(?<![\w.])\d+")
indice = [0]
fatte = [0]


def rimpiazza(m):
    i = indice[0]
    indice[0] += 1
    if i in correzioni:
        fatte[0] += 1
        return "0x%02x" % correzioni[i]
    return m.group(0)


corpo = NUM.sub(rimpiazza, corpo)

# ⚠️ Si conta quanti rimpiazzi sono usciti e si confronta con quanti erano
# attesi: una sostituzione automatica che non torna nel numero e' un difetto
# che non da' nessun errore.
if fatte[0] != len(correzioni):
    print("ERRORE: attese %d correzioni, applicate %d"
          % (len(correzioni), fatte[0]))
    sys.exit(1)

with open(HEADER, "w", encoding="latin-1", newline="\n") as f:
    f.write(testa + corpo)
print("  xevious_bgdata.h: %d correzioni applicate" % fatte[0])
