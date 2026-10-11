# ============================================================================
# build_laserdisc.py — i video dei tre laserdisc, dal pacchetto Daphne alla SD.
#
# La catena e' in DUE passi, e prima erano script scollegati con dentro i
# percorsi del PC di chi li ha scritti. Questo e' il regista che li unisce.
#
#   passo 1   pacchetto Daphne (.m2v + .ogg + framefile)
#             -> <clip>.mjpg + .idx + .pcm  a 800x600     (VIDEO\_800x600\)
#   passo 2   riduce a 176x240 per lo schermo dello SPINNERINO
#             -> DA COPIARE SU SD\<gioco>\
#
# COSA TI SERVE in VIDEO\ :  dl_ntsc.zip  ·  "Space Ace.zip"
#                            ·  "Dragon's Lair 2.zip"
# piu' ffmpeg (in FFMPEG\ dentro il pacchetto, oppure nel PATH).
#
# ⏳ E' LUNGHISSIMO: sono ~5 ore di video da decodificare e ricodificare DUE
# volte, quindi ORE di CPU e ~40 GB di spazio per il passo intermedio. E'
# ripartibile: se lo interrompi, al giro dopo riprende da dove era.
#
# ⛔ Ogni gioco ha la SUA catena, non sono intercambiabili:
#   dl   build_all_dl.py     lavora dentro lo zip; i nomi perdono il ".vob"
#                            (il firmware cerca `dls00.mjpg`, non `dls00.vob`)
#   ace  build_ace_video.py  un solo .m2v da 58643 frame, tagliato a 1000
#        + build_ace_audio.py   e l'audio tagliato agli STESSI confini
#   dl2  build_dl2.py        un clip per scena, ognuno col suo frame rate
# ============================================================================
import os, re, subprocess, sys, zipfile

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)
VIDEO     = os.path.join(PACCHETTO, "VIDEO")
USCITA_SD = os.path.join(PACCHETTO, "DA COPIARE SU SD")
GREZZI    = os.path.join(VIDEO, "_800x600")

# gioco, zip, serve scompattarlo?, passo 1 (uno o due script), passo 2
GIOCHI = [
    ("dl",  "dl_ntsc.zip",         False,
     ["build_all_dl.py"],                         "build_dl_video_176x240.py"),
    ("ace", "Space Ace.zip",       True,
     ["build_ace_video.py", "build_ace_audio.py"], "build_ace_video_176x240.py"),
    ("dl2", "Dragon's Lair 2.zip", True,
     ["build_dl2.py"],                            "build_dl2_video_176x240.py"),
]


def trova_pacchetto(nome_zip):
    if not os.path.isdir(VIDEO):
        return None
    for r, _d, fs in os.walk(VIDEO):
        for n in fs:
            if n.lower() == nome_zip.lower():
                return os.path.join(r, n)
    return None


def scompatta(zip_path, dove):
    """Torna la cartella che contiene davvero i .m2v: gli zip Daphne hanno una
    cartella dentro (`ace.daphne\\`, `lair2.daphne\\`) e gli script del passo 1
    vogliono QUELLA, non la radice."""
    if not (os.path.isdir(dove) and os.listdir(dove)):
        os.makedirs(dove, exist_ok=True)
        print("    scompatto (e' grosso, qualche minuto)...", flush=True)
        with zipfile.ZipFile(zip_path) as z:
            z.extractall(dove)
    for r, _d, fs in os.walk(dove):
        if any(f.lower().endswith(".m2v") for f in fs):
            return r
    return dove


def passo2(script, src, dst):
    """Lo script del passo 2 ha SRC e DST scritti dentro: glieli riscriviamo in
    una copia usa-e-getta, cosi' l'originale resta leggibile com'era."""
    t = open(os.path.join(QUI, script), encoding="latin-1", errors="replace").read()
    # ⛔ La sostituzione va passata come FUNZIONE, non come stringa: un percorso
    # Windows ci mette dentro `C:\Users`, e nel testo di rimpiazzo di `re` il
    # `\U` e' una sequenza di escape -> «bad escape \U» e la conversione muore
    # prima di cominciare. Con la lambda il testo passa alla lettera.
    t, n1 = re.subn(r'^SRC\s*=\s*r?"[^"]*"', lambda m: 'SRC = r"%s"' % src,
                    t, count=1, flags=re.M)
    t, n2 = re.subn(r'^DST\s*=\s*r?"[^"]*"', lambda m: 'DST = r"%s"' % dst,
                    t, count=1, flags=re.M)
    if not (n1 and n2):
        print("    ERRORE: in %s non trovo SRC/DST da ripuntare" % script)
        return 1
    tmp = os.path.join(QUI, "_passo2_adattato.py")
    with open(tmp, "w", encoding="latin-1", errors="replace") as f:
        f.write(t)
    try:
        return subprocess.run([sys.executable, tmp], text=True).returncode
    finally:
        os.remove(tmp)


def main():
    # ⭐ `--prova` fa due clip per gioco invece di tutti: cinque minuti per
    # sapere se la catena e' a posto, invece di scoprirlo dopo ore. Non produce
    # una SD giocabile (di Space Ace manca ace.meta: sarebbe una mappa monca).
    prova = "--prova" in sys.argv
    if prova:
        print("PROVA: solo due clip per gioco, la SD non sara' giocabile\n")

    fatti, saltati = 0, []
    for gioco, nome_zip, va_scompattato, passo1, s2 in GIOCHI:
        zip_path = trova_pacchetto(nome_zip)
        if not zip_path:
            saltati.append("%s (manca VIDEO\\%s)" % (gioco, nome_zip))
            continue

        print("\n=== %s ===" % gioco.upper(), flush=True)
        sorgente = (scompatta(zip_path, os.path.join(VIDEO, "_pack_" + gioco))
                    if va_scompattato else zip_path)

        grezzo = os.path.join(GREZZI, gioco)
        os.makedirs(grezzo, exist_ok=True)
        guasto = False
        for s1 in passo1:
            if prova and s1 == "build_ace_audio.py":
                continue          # vuole ace.meta, che in prova non si scrive
            print("  passo 1: %s ..." % s1, flush=True)
            p = subprocess.run([sys.executable, os.path.join(QUI, s1),
                                sorgente, grezzo]
                               + (["--limit", "2"] if prova else []), text=True)
            if p.returncode != 0:
                print("  %s: %s fallito" % (gioco, s1))
                guasto = True
                break
        if guasto or not any(f.endswith(".mjpg") for f in os.listdir(grezzo)):
            saltati.append("%s (passo 1 senza risultato)" % gioco)
            continue

        dst = os.path.join(USCITA_SD, gioco)
        os.makedirs(dst, exist_ok=True)
        print("  passo 2: %s ..." % s2, flush=True)
        if passo2(s2, grezzo, dst) == 0:
            fatti += 1
            print("  %s: fatto -> DA COPIARE SU SD\\%s" % (gioco, gioco))
        else:
            saltati.append("%s (passo 2 fallito)" % gioco)

    for s in saltati:
        print("  salto: %s" % s)
    if not fatti:
        print("nessun laserdisc convertito")
        return 1
    print("laserdisc convertiti: %d su 3" % fatti)
    return 0


sys.exit(main())
