# ============================================================================
# romconv_neogeo_sram.py — `neogeo\<gioco>\sram.bin`, la backup RAM dei giochi
# NEO GEO che fanno lo «slot check» SNK: oggi METAL SLUG 2 e PULSTAR.
#
# ⛔⛔ SENZA QUESTO FILE QUEI GIOCHI NON PARTONO: il menu dice «DATI MANCANTI».
# La protezione SNK guarda la backup RAM e, se la trova vergine, si ferma per
# sempre sulla schermata «ORIGINAL SNK BOARDS ONLY» (sulla scheda vera la RAM e'
# tamponata a batteria, quindi non e' mai vergine; qui il watchdog che la
# inizializzerebbe non e' emulato). Il firmware lo dichiara obbligatorio
# (`probe2` in `NG_GAMES`, boot_menu.cpp).
#
# ⭐ NON si ricava dalle ROM: e' l'immagine di una NVRAM gia' «vissuta». Viaggia
# col pacchetto come TABELLA dei soli byte non nulli
# (`neogeo_sram\<gioco>_sram.table`, un valore per riga): qui si ricostruisce
# il file, 65.536 zeri e poi quei valori.
#   mslug2   212 valori   (dal pacchetto SPINNERINO PLUS del 5/9)
#   pulstar  134 valori   (banco offline NEO GEO, 2026-10-08)
# View Point NON ne ha bisogno (nessuno slot check).
#
# USO:  python romconv_neogeo_sram.py            -> tutti quelli con la tabella
#       python romconv_neogeo_sram.py pulstar    -> uno solo
# Copia di romconv_neogeo_sram.py del pacchetto (solo Metal Slug 2),
# generalizzata al nome del gioco. Nessun percorso di un PC preciso.
# ============================================================================
import os, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
TABELLE   = os.path.join(QUI, "neogeo_sram")
SD_NEOGEO = os.path.join(PACCHETTO, "DA COPIARE SU SD", "neogeo")
DIMENSIONE = 65536


def fai(gioco):
    tabella = os.path.join(TABELLE, "%s_sram.table" % gioco)
    if not os.path.isfile(tabella):
        print("ERRORE: manca CONVERTITORI\\neogeo_sram\\%s_sram.table" % gioco)
        return False
    dati = bytearray(DIMENSIONE)
    scritti = 0
    for riga in open(tabella, encoding="latin-1"):
        riga = riga.split("#")[0].strip()
        if not riga:
            continue
        off, val = riga.split()
        dati[int(off, 0)] = int(val, 0)
        scritti += 1
    uscita = os.path.join(SD_NEOGEO, gioco)
    os.makedirs(uscita, exist_ok=True)
    with open(os.path.join(uscita, "sram.bin"), "wb") as f:
        f.write(dati)
    print("  neogeo\\%s\\sram.bin  (%d byte, %d valori dalla tabella)"
          % (gioco, DIMENSIONE, scritti))
    return True


def main():
    if len(sys.argv) > 1:
        giochi = sys.argv[1:]
    else:
        giochi = sorted(f[:-len("_sram.table")] for f in os.listdir(TABELLE)
                        if f.endswith("_sram.table"))
    ok = all([fai(g) for g in giochi])
    return 0 if ok and giochi else 1


sys.exit(main())
