# ============================================================================
# prepara.py — rigenera tutto il progetto a partire dalle TUE ROM e dai TUOI
# pacchetti Daphne. Si lancia da qui:
#
#     python prepara.py              tutto
#     python prepara.py galaga       solo i convertitori che parlano di galaga
#     python prepara.py --elenco     dice solo cosa manca, senza fare niente
#
# Cosa fa, in ordine:
#   1. controlla Python e Pillow;
#   2. fa girare i convertitori delle ROM (romconv_*, *_rom_convert, bin2h_*);
#   3. poi quelli dei loghi (convert_*);
#   4. poi quelli dei video Daphne (build_*), se in VIDEO\ c'e' qualcosa;
#   5. per ogni header che nessun convertitore ha prodotto, mette il
#      SEGNAPOSTO: il progetto COMPILA lo stesso e quel gioco non parte.
#
# Niente caratteri strani nelle stampe: su Windows la console e' cp1252 e un
# carattere fuori tavolozza fa morire lo script a meta' lavoro.
# ============================================================================
import os, subprocess, sys, shutil, time, textwrap

QUI    = os.path.dirname(os.path.abspath(__file__))
CONV   = os.path.join(QUI, "CONVERTITORI")
SEGNA  = os.path.join(CONV, "SEGNAPOSTO")
PROG   = os.path.join(QUI, "SPINNERINO")
ROMS   = os.path.join(QUI, "ROMS")
VIDEO  = os.path.join(QUI, "VIDEO")
LANCIA = os.path.join(CONV, "_lancia.py")

SENZA_NUMPY = False      # lo mette controlla_ambiente(); riletto nel RIEPILOGO

TEMPO_MAX = 900          # secondi per convertitore
# ⛔ I laserdisc non stanno in 15 minuti: sono ~5 ore di video da decodificare
# e ricodificare DUE volte. Col tempo massimo degli altri venivano ammazzati a
# meta' strada e il referto diceva «tempo scaduto», che sembra un guasto.
TEMPO_MAX_VIDEO = 24 * 3600

# Qualche convertitore non legge la ZIP: vuole i file di ROM SCIOLTI in una
# cartella, e il suo percorso dalla riga di comando. Non si riscrive il
# convertitore: gli si prepara la cartella scompattando la zip.
#   nome script -> (zip da scompattare, cartella di uscita)
VOGLIONO_CARTELLA = {
    "romconv_galaxian.py": ("galaxian.zip", os.path.join("machines", "galaxian")),
    "romconv_phoenix.py":  ("phoenix.zip",  os.path.join("machines", "phoenix")),
}
ARGOMENTI = {
    "romconv_neogeo.py": ["viewpoin", "pulstar"],
}

# ⚠️ I convertitori di galagino (`*_rom_convert.py`) NON aprono la zip: si
# aspettano i file di ROM SCIOLTI in una cartella. Senza, escono in silenzio
# senza scrivere niente — sembrano rotti e invece non trovano gli ingressi.
# Qui si scompatta la zip in ROMS\_estratte\, dove _ambiente.py la ritrova
# cercando per nome.
VOGLIONO_SCIOLTE = {
    "ladybug_rom_convert.py":    "ladybug",
    "mooncresta_rom_convert.py": "mooncrst",
    "mspacman_rom_convert.py":   "mspacman",
    "timeplt_rom_convert.py":    "timeplt",
}

# ⛔⛔⛔ Convertitori di UN ALTRO PROGETTO, finiti qui dentro. Il nome dice
# `romconv_`, quindi sarebbero «ROM da lanciare sempre» — ma scrivono i valori
# della LORO scheda sopra header che questo pacchetto porta GIUSTI. Non e' un
# guasto che si vede: compila lo stesso e si scopre a schermo.
#   nome script -> perche' non deve girare qui
# ⚠️ Il testo viene STAMPATO: solo ASCII, o su Windows il print muore con
# UnicodeEncodeError (cp1252) e sembra un guasto di prepara.
FUORI_POSTO = {
    "romconv_nibbler.py":
        "e' del CABINATONE (si ancora a CABINATONE.ino e lo dice la sua "
        "intestazione). Qui riscriverebbe DUE header con i valori sbagliati: "
        "nibbler_palette.h in RGB565 NATIVI (pannello DSI) al posto dei "
        "BYTE-SWAPPED che vuole il display SPI, e nibbler_crunch.h "
        "ricampionato 3:4 a 32 kHz (11200 campioni) al posto degli 8400 a "
        "24 kHz che girano su questa scheda. "
        "NOTA: nibbler_crunch.h e' un INGRESSO, non un prodotto - il crunch "
        "e' un circuito discreto che MAME non emula, non si ricava da "
        "nibbler.zip e va spedito col pacchetto. "
        "RESTA APERTO: nibbler_rom/gfx/snd_rom.h stanno col segnaposto, e "
        "dalla zip si rifarebbero bene; per averli serve una versione di "
        "questo convertitore che NON tocchi palette e crunch.",
}


def scompatta_per(nome):
    import zipfile
    gioco = VOGLIONO_SCIOLTE[nome]
    for cand in (gioco + ".zip", nome.split("_")[0] + ".zip"):
        z = os.path.join(ROMS, cand)
        if os.path.isfile(z):
            d = os.path.join(ROMS, "_estratte", gioco)
            os.makedirs(d, exist_ok=True)
            with zipfile.ZipFile(z) as a:
                a.extractall(d)
            return True
    return False


def prepara_cartella_rom(nome):
    """Scompatta la zip di quel gioco e restituisce gli argomenti da passare
    allo script. None se la zip non c'e'.
    ⚠️ Si scompatta DENTRO il pacchetto, non in una cartella temporanea di
    sistema: i file di ROM hanno estensioni che _ambiente.py dirotta, e da
    fuori venivano portati altrove - galaxian diceva «1h.bin non trovato»
    con il file appena estratto sotto il naso."""
    import zipfile
    zip_nome, uscita = VOGLIONO_CARTELLA[nome]
    percorso_zip = os.path.join(ROMS, zip_nome)
    if not os.path.isfile(percorso_zip):
        return None
    d = os.path.join(ROMS, "_estratte", os.path.splitext(zip_nome)[0])
    os.makedirs(d, exist_ok=True)
    with zipfile.ZipFile(percorso_zip) as z:
        z.extractall(d)
    return [d, os.path.join(PROG, uscita)]


def titolo(t):
    print("")
    print("=" * 70)
    print(t)
    print("=" * 70)


# ⚠️ Non tutti i .py qui dentro sono convertitori da lanciare in fila:
#   ROM      i convertitori veri: dalla zip MAME all'header.  SEMPRE.
#   LOGHI    rifanno i loghi dalle immagini ORIGINALI, che NON sono nel
#            pacchetto: i loro header ci sono gia' pronti, quindi si saltano.
#            Si lanciano solo con --loghi, e solo se hai tu le immagini.
#   VIDEO    i pacchetti Daphne: solo se in VIDEO\ c'e' qualcosa.
#   ATTREZZI banchi di prova e utilita' che vogliono argomenti a mano.
#            Lanciarli in fila produce solo errori che sembrano guasti.
def famiglia(nome):
    n = nome.lower()
    # ⛔ Prima di ogni altra cosa: quelli di un altro progetto non girano qui.
    # Va messo in TESTA, o il prefisso `romconv_` li fa passare per ROM.
    if nome in FUORI_POSTO:
        return "ATTREZZI"
    if n.startswith(("romconv_", "bin2h_")) or n.endswith(
            ("_rom_convert.py", "_h2bin.py")):
        return "ROM"
    # ⛔ Dei `build_*` ne parte UNO SOLO: `build_laserdisc.py`, che e' il regista
    # e chiama gli altri nell'ordine giusto e con i percorsi giusti. Gli altri
    # sono i suoi due passi: lanciati in fila da soli fallivano tutti e quattro,
    # perche' il passo 2 cerca i file 800x600 che il passo 1 non ha ancora fatto.
    if n == "build_laserdisc.py":
        return "VIDEO"
    if n.startswith("build_"):
        return "ATTREZZI"
    if n.startswith("mk_") or n.startswith("export_"):
        return "ATTREZZI"
    # ⚠️ Non tutti i `convert_*` sono loghi: `convert_xevious_boom.py` e
    # `convert_si_samples.py` producono CAMPIONI audio, e venivano saltati
    # insieme ai loghi lasciando due giochi col segnaposto. Si guarda cosa
    # dice il nome, non solo il prefisso.
    # ⚠️ Quasi tutti i `convert_*` fanno GRAFICA (loghi, copertine, schermate)
    # e si saltano. Le due eccezioni fanno CAMPIONI AUDIO e vanno lanciate:
    # si elencano per nome, perche' dedurlo dal nome ha gia' sbagliato due
    # volte in due direzioni opposte.
    if n.startswith("convert_") or "__convert_" in n:
        if n in ("convert_si_samples.py", "convert_xevious_boom.py", "convert_pulstar_cover.py", "convert_viewpoin_cover.py"):
            return "ROM"
        return "LOGHI"
    return "ATTREZZI"


ORDINE = {"ROM": 0, "LOGHI": 1, "VIDEO": 2, "ATTREZZI": 3}


def elenca_convertitori(filtro, con_loghi, con_video):
    fuori, saltati = [], {"LOGHI": 0, "VIDEO": 0, "ATTREZZI": 0}
    for nome in sorted(os.listdir(CONV)):
        if not nome.endswith(".py") or nome.startswith("_"):
            continue
        if filtro and filtro.lower() not in nome.lower():
            continue
        f = famiglia(nome)
        if f == "ATTREZZI" or (f == "LOGHI" and not con_loghi) \
                or (f == "VIDEO" and not con_video):
            saltati[f] = saltati.get(f, 0) + 1
            continue
        fuori.append(nome)
    fuori.sort(key=lambda n: (ORDINE[famiglia(n)], n))
    return fuori, saltati


def header_attesi():
    """Dai segnaposto: nome del file -> dove va dentro SPINNERINO\\."""
    attesi = {}
    for radice, _d, files in os.walk(SEGNA):
        for nome in files:
            if nome.endswith(".h"):
                rel = os.path.relpath(os.path.join(radice, nome), SEGNA)
                attesi[rel] = os.path.join(SEGNA, rel)
    return attesi


# Le due librerie che servono ai convertitori. Se mancano si INSTALLANO da
# sole: chi riceve il pacchetto non deve sapere cos'e' pip.
# ⚠️ L'installazione si fa QUI, in prepara.py, e non dentro un convertitore:
# li' _ambiente.py ha gia' agganciato os.stat e i percorsi sono dirottati, e
# pip lavora proprio sui file delle librerie ([[feedback_bus_condiviso...]] e
# il commento in _ambiente.py: un dirottatore troppo largo aveva gia' rotto
# numpy una volta).
def assicura(modulo, pacchetto, senza_cosa):
    """True se alla fine il modulo c'e'. Se manca prova a installarlo."""
    try:
        __import__(modulo)
        return True
    except ImportError:
        pass
    # ⚠️ flush: pip scrive sui suoi canali senza passare da qui, e senza il
    # flush i suoi «ERROR:» comparivano PRIMA della riga che li spiega —
    # sembrava che prepara fosse esploso.
    print("")
    print("Manca %s: provo a installarlo da solo (serve internet)..."
          % pacchetto, flush=True)
    for extra in ([], ["--user"]):
        codice = -1
        try:
            p = subprocess.run([sys.executable, "-m", "pip", "install",
                                "--disable-pip-version-check", "-q",
                                pacchetto] + extra, timeout=900)
            codice = p.returncode
        except Exception as e:
            print("   pip non e' partito: %s" % str(e)[:60])
        if codice != 0:
            continue
        # ⚠️ Un pacchetto installato ADESSO puo' non essere visibile a questo
        # processo: le cartelle site-packages si leggono all'avvio. Con
        # --user serve rileggere i .pth, e in ogni caso la cache degli import.
        import importlib
        try:
            import site
            site.main()
        except Exception:
            pass
        importlib.invalidate_caches()
        try:
            __import__(modulo)
            print("   %s installato." % pacchetto)
            return True
        except ImportError:
            print("   %s installato, ma questo giro non lo vede:" % pacchetto)
            print("   CHIUDI questa finestra e rilancia prepara.bat.")
            return False
    print("   non ci sono riuscito. Apri una finestra CMD e scrivi:")
    print("      pip install %s" % pacchetto)
    print("   %s" % senza_cosa)
    return False


def controlla_ambiente():
    global SENZA_NUMPY
    if sys.version_info < (3, 8):
        sys.exit("Serve Python 3.8 o piu' recente (trovato %d.%d)"
                 % sys.version_info[:2])
    if not assicura("PIL", "pillow", "senza Pillow non si rigenera NIENTE."):
        sys.exit("Manca Pillow. Installalo con:   pip install pillow")
    # ⛔ numpy non serve "solo al Neo Geo", come diceva il messaggio di prima:
    # lo vogliono romconv_1944.py, romconv_neogeo.py e convert_xevious_boom.py
    # (e, con --loghi, convert_laserdisc_card.py). Il messaggio nomina i
    # GIOCHI che restano indietro, non uno solo.
    SENZA_NUMPY = not assicura(
        "numpy", "numpy",
        "senza numpy restano col segnaposto 1944, NEO GEO e le esplosioni"
        " di XEVIOUS.")
    for c in (ROMS, VIDEO):
        os.makedirs(c, exist_ok=True)
    zip_trovate = [f for f in os.listdir(ROMS) if f.lower().endswith(".zip")]
    print("Python %d.%d.%d - Pillow ok - numpy %s"
          % (sys.version_info[:3] + ("MANCA" if SENZA_NUMPY else "ok",)))
    print("zip in ROMS\\ ........ %d" % len(zip_trovate))
    print("roba in VIDEO\\ ...... %d elementi" % len(os.listdir(VIDEO)))
    if not zip_trovate:
        print("")
        print("!! ROMS\\ e' vuota: senza zip non si rigenera niente.")
        print("   Metti li' le tue zip MAME e rilancia.")
    return zip_trovate


def main():
    filtro, solo_elenco, con_loghi = None, False, False
    salta_video = False
    for a in sys.argv[1:]:
        if a in ("--elenco", "-l"):
            solo_elenco = True
        elif a == "--loghi":
            con_loghi = True
        elif a == "--senza-video":
            salta_video = True
        elif not a.startswith("-"):
            filtro = a

    titolo("SPINNERINO PLUS - preparazione")
    controlla_ambiente()

    # ⭐ I laserdisc sono ORE di lavoro. Rilanciando prepara per un solo gioco
    # nuovo, ripartivano da capo: `--senza-video` li lascia stare.
    con_video = bool(os.listdir(VIDEO)) and not salta_video
    conv, saltati = elenca_convertitori(filtro, con_loghi, con_video)
    attesi = header_attesi()
    print("convertitori da lanciare %d" % len(conv))
    print("header attesi ........... %d" % len(attesi))
    if saltati.get("LOGHI"):
        print("saltati %d convertitori di LOGHI: gli header ci sono gia'."
              % saltati["LOGHI"])
        print("   (--loghi per rifarli, ma servono le immagini originali)")
    if saltati.get("VIDEO"):
        print("saltati %d convertitori VIDEO: %s."
              % (saltati["VIDEO"],
                 "l'hai chiesto tu con --senza-video" if salta_video
                 else "VIDEO\\ e' vuota"))
    if saltati.get("ATTREZZI"):
        print("saltati %d attrezzi (banchi di prova, utilita' a mano)."
              % saltati["ATTREZZI"])
    # ⭐ Un'esclusione MUTA e' peggio di un errore: nessuno la va a cercare.
    # Questi si nominano uno per uno, con il perche'.
    for nome, perche in sorted(FUORI_POSTO.items()):
        if os.path.isfile(os.path.join(CONV, nome)):
            print("NON lanciato %s:" % nome)
            for riga in textwrap.wrap(perche, 66):
                print("   %s" % riga)

    if solo_elenco:
        mancano = [r for r in sorted(attesi)
                   if not os.path.isfile(os.path.join(PROG, r))]
        titolo("HEADER ANCORA MANCANTI: %d" % len(mancano))
        for r in mancano:
            print("   %s" % r)
        return

    # ⚠️ I segnaposto del giro PRECEDENTE vanno via PRIMA, o al secondo giro
    # risultano «header gia' presenti» e il referto dice «tutto rigenerato»
    # mentre meta' del progetto e' vuota. Un conto disonesto e' peggio di un
    # errore: non lo si va a controllare.
    puliti, esistono, guai = 0, 0, []
    for rel in attesi:
        p = os.path.join(PROG, rel)
        if not os.path.isfile(p):
            continue
        esistono += 1
        try:
            with open(p, "r", encoding="latin-1") as f:
                prima = f.readline()
            if "SEGNAPOSTO" in prima:
                os.remove(p);  puliti += 1
        except Exception as e:
            guai.append((rel, str(e)[:60]))
    print("header gia' presenti %d, di cui segnaposto tolti %d"
          % (esistono, puliti))
    for rel, e in guai[:5]:
        print("   non riesco a leggere %s: %s" % (rel, e))

    SD_USCITA = os.path.join(QUI, "DA COPIARE SU SD")

    def foto_header():
        """Istantanea di quali header esistono e quando sono stati scritti:
        serve a capire se un convertitore ha davvero fatto qualcosa."""
        f = {}
        for rel in attesi:
            p = os.path.join(PROG, rel)
            try:
                f[rel] = os.path.getmtime(p)
            except OSError:
                f[rel] = None
        # ⚠️ Non bastano gli header: 1944, Ghouls, Out Run e Neo Geo non ne
        # producono NESSUNO — scrivono i .bin della microSD. Guardando i soli
        # header risultavano «non ha prodotto niente» mentre avevano lavorato.
        for radice, _d, files in os.walk(SD_USCITA):
            for nome in files:
                q = os.path.join(radice, nome)
                f["SD:" + os.path.relpath(q, SD_USCITA)] = os.path.getmtime(q)
        return f

    prima_foto = foto_header()
    ok, falliti, vuoti = [], [], []
    titolo("CONVERTITORI")
    for i, nome in enumerate(conv, 1):
        print("[%3d/%d] %-46s " % (i, len(conv), nome), end="", flush=True)
        # con --loghi la protezione dell'arte si toglie, e i convertitori dei
        # loghi possono riscrivere i loro header
        os.environ["PACCHETTO_RIFAI_LOGHI"] = "1" if con_loghi else "0"
        if nome in VOGLIONO_SCIOLTE and not scompatta_per(nome):
            print("zip mancante")
            vuoti.append((nome, "manca la zip di %s" % VOGLIONO_SCIOLTE[nome]))
            continue
        extra = ARGOMENTI.get(nome, [])
        if nome in VOGLIONO_CARTELLA:
            extra = prepara_cartella_rom(nome)
            if extra is None:
                print("zip mancante")
                vuoti.append((nome, "manca %s" % VOGLIONO_CARTELLA[nome][0]))
                continue
        t0 = time.time()
        # ⭐ I video durano ORE: il loro avanzamento si LASCIA PASSARE a schermo,
        # altrimenti si guarda una riga ferma senza sapere se sta lavorando o
        # se e' piantato.
        e_video = famiglia(nome) == "VIDEO"
        try:
            if e_video:
                print("(ore di lavoro, l'avanzamento esce qui sotto)", flush=True)
                # ⛔⛔ Qui prima c'era `uscita = ""`: l'avanzamento passava a
                # schermo e NON veniva raccolto. Se poi il convertitore usciva
                # male, il referto diventava `ERRORE` seguito dal VUOTO — un
                # errore che non dice niente e che non si puo' nemmeno
                # classificare come «zip mancante», perche' la frase che nomina
                # la zip era stata buttata via. Ora si fa tutt'e due: si stampa
                # riga per riga E si tiene.
                righe = []
                p = subprocess.Popen([sys.executable, LANCIA, nome] + extra,
                                     cwd=QUI, text=True, bufsize=1,
                                     stdout=subprocess.PIPE,
                                     stderr=subprocess.STDOUT)
                for riga in p.stdout:
                    sys.stdout.write(riga)
                    sys.stdout.flush()
                    righe.append(riga)
                    if time.time() - t0 > TEMPO_MAX_VIDEO:
                        p.kill()
                        raise subprocess.TimeoutExpired(nome, TEMPO_MAX_VIDEO)
                p.stdout.close()
                esito, uscita = p.wait(), "".join(righe)

            else:
                if nome == "romconv_neogeo.py" and isinstance(extra, list):
                    uscita_totale = ""
                    esito = 0
                    for gioco_neo in extra:
                        p = subprocess.run([sys.executable, LANCIA, nome, gioco_neo],
                                           cwd=QUI, capture_output=True, text=True,
                                           timeout=TEMPO_MAX)
                        uscita_totale += (p.stdout or "") + (p.stderr or "")
                        if p.returncode != 0:
                            esito = p.returncode
                    uscita = uscita_totale
                else:
                    p = subprocess.run([sys.executable, LANCIA, nome] + extra,
                                       cwd=QUI, capture_output=True, text=True,
                                       timeout=TEMPO_MAX)
                    esito, uscita = p.returncode, (p.stdout or "") + (p.stderr or "")
        except subprocess.TimeoutExpired:
            esito, uscita = -9, "tempo scaduto (%d s)" % (
                TEMPO_MAX_VIDEO if e_video else TEMPO_MAX)
        dt = time.time() - t0

        # ⚠️ Un convertitore puo' uscire con SUCCESSO e non aver scritto NIENTE
        # (quattro dei galagino lo fanno quando non trovano le ROM). Un «ok»
        # falso e' peggio di un errore: nessuno va a controllarlo. Quindi non
        # si crede al codice d'uscita, si guarda se qualche header e' cambiato.
        dopo = foto_header()
        cambiati = [r for r in dopo if dopo[r] != prima_foto.get(r)]
        prima_foto = dopo

        # ⚠️ E il contrario: un convertitore puo' uscire con codice DIVERSO da
        # zero e aver fatto tutto (romconv_galaga confronta con gli header di
        # un altro progetto e si lamenta della differenza di formato). Se ha
        # scritto e non e' esploso, e' andata bene: si dice che c'e' un avviso.
        esploso = "Traceback" in uscita if esito else False
        if esito != 0 and cambiati and not esploso:
            print("ok    %5.1fs  (%d header, con avvisi)" % (dt, len(cambiati)))
            ok.append(nome)
            continue

        if esito == 0 and cambiati:
            print("ok    %5.1fs  (%d header)" % (dt, len(cambiati)))
            ok.append(nome)
        elif esito == 0:
            print("NON HA PRODOTTO NIENTE")
            vuoti.append((nome, "uscito bene ma non ha scritto nessun header"))
        else:
            # ⛔⛔⛔ 2026-09-14: questo `[:60]` ha nascosto la causa di un
            # guasto per due giri di referti. L'amico mandava
            #   `ERRORE  PermissionError: [Errno 13] Permission denied: 'C:\U`
            # e quella riga finisce **esattamente al 60esimo carattere**: il
            # percorso — cioe' l'unica cosa che serviva — era tagliato via
            # DA NOI, non dal suo terminale. Il taglio resta (una riga sola a
            # schermo e' leggibile) ma l'uscita INTERA finisce in un file.
            breve = ""
            for riga in reversed(uscita.strip().splitlines()):
                if riga.strip():
                    breve = riga.strip()[:60]
                    break
            if not breve:
                breve = "uscito col codice %s e NESSUN messaggio" % esito
            # ⚠️ «manca la zip» ogni convertitore lo dice a modo suo: «manca»,
            # «non trovato», «not found», «Place invaders.zip in...», «Oppure:
            # python romconv_x.py <path/scobra.zip>», Errno 2. Inseguire le
            # FRASI e' una corsa che non finisce: si guarda il FATTO.
            # Regola: se nel messaggio compare il nome di una zip che in ROMS\
            # non c'e', il convertitore non e' rotto - gli manca la ROM.
            import re as _re
            zip_citate = set(_re.findall(r"[\w\-\.]+\.zip", uscita, _re.I))
            # ⚠️ Non solo ROMS\: i pacchetti Daphne stanno in VIDEO\, e
            # cercandoli solo fra le ROM un convertitore che li usa risultava
            # «zip mancante» con la zip al suo posto.
            def _c_e(z):
                b = os.path.basename(z)
                return any(os.path.isfile(os.path.join(d, b))
                           for d in (ROMS, VIDEO))
            assenti = [z for z in zip_citate if not _c_e(z)]
            if assenti:
                print("zip mancante")
                vuoti.append((nome, breve))
            else:
                print("ERRORE  %s" % breve)
                falliti.append((nome, breve, uscita))

    # ------------------------------------------------------------ scarti
    # ⛔⛔ Alcuni convertitori lasciano dentro il progetto roba che il firmware
    # NON include mai: sono DATI DI ROM e grafica estratta dalle ROM, cioe'
    # proprio quello che da questo pacchetto deve restare fuori. Nessuno se ne
    # accorge, perche' il progetto compila lo stesso.
    #   scramble_*_rom.h   150 KB, li scrive scramble_rom_convert.py (viene da
    #                      CABINATONE, dove servivano). Qui: zero #include.
    #   _verify_gfx\       immagini di controllo di Ghouls e Out Run.
    #   logo\ e flyer\     .bin di logo/copertina PER LA SD. Su GALAGONE/sambi
    #                      i loghi stanno sulla scheda; su SPINNERINO stanno in
    #                      FLASH, negli header *_logo.h / *_grid_logo.h che
    #                      boot_menu.cpp include. Verificato: il firmware non
    #                      nomina MAI "logo/" ne' "flyer/" (zero occorrenze) e
    #                      la SD vera ha solo 1944 ace dl dl2 ghouls neogeo
    #                      outrun. Li scrivono romconv_mariobros.py e
    #                      romconv_xevious.py, portati da quell'altro porting.
    n = 0
    for rel in ("scramble_main_rom.h", "scramble_audio_rom.h"):
        p = os.path.join(PROG, rel)
        if os.path.isfile(p):
            os.remove(p)
            n += 1
    vg = os.path.join(PROG, "_verify_gfx")
    if os.path.isdir(vg):
        for f in os.listdir(vg):
            os.remove(os.path.join(vg, f))
            n += 1
        os.rmdir(vg)
    for nome_c in ("logo", "flyer"):
        c = os.path.join(QUI, nome_c)
        if not os.path.isdir(c):
            continue
        for f in os.listdir(c):
            if f.lower().endswith(".bin"):
                os.remove(os.path.join(c, f))
                n += 1
        if not os.listdir(c):
            os.rmdir(c)
    if n:
        print("\ntolti %d file di scarto dei convertitori (dati di ROM che il"
              " firmware non usa)" % n)

    # ------------------------------------------------------------ segnaposto
    titolo("SEGNAPOSTO per cio' che non e' stato rigenerato")
    messi = 0
    for rel in sorted(attesi):
        dst = os.path.join(PROG, rel)
        if os.path.isfile(dst):
            continue
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copyfile(attesi[rel], dst)
        messi += 1
    print("segnaposto messi ........ %d" % messi)
    print("header veri ............. %d" % (len(attesi) - messi))

    # ---------------------------------------------------------------- fine
    titolo("RIEPILOGO")
    print("convertitori andati bene  %d" % len(ok))
    print("zip mancanti .............. %d" % len(vuoti))
    print("convertitori in errore .... %d" % len(falliti))
    if falliti:
        print("")
        print("Da guardare:")
        for nome, breve, _u in falliti:
            print("   %-42s %s" % (nome, breve))
        with open(os.path.join(QUI, "prepara_errori.txt"), "w",
                  encoding="utf-8") as f:
            for nome, breve, uscita in falliti:
                f.write("=" * 70 + "\n" + nome + "\n" + "=" * 70 + "\n")
                f.write(uscita + "\n\n")
        print("")
        print("   ^ questa riga e' TAGLIATA a 60 caratteri.")
        print("   L'errore INTERO (percorso compreso) e' nel file")
        print("      %s" % os.path.join(QUI, "prepara_errori.txt"))
        print("   Se chiedi aiuto, manda QUEL file: la riga qui sopra non basta.")
    print("")
    if messi == 0:
        print("TUTTO RIGENERATO: apri SPINNERINO\\SPINNERINO.ino e compila.")
    else:
        print("Puoi gia' compilare: i %d giochi col segnaposto non partiranno." % messi)
    # ⭐ L'avviso di numpy sta in cima, prima di 65 righe di convertitori: li'
    # non lo legge nessuno. Si ripete ALLA FINE, che e' dove si guarda.
    if SENZA_NUMPY:
        print("")
        print("!! numpy NON e' installato: 1944, NEO GEO e le esplosioni di")
        print("   XEVIOUS non sono state rigenerate. Apri una finestra CMD,")
        print("   scrivi   pip install numpy   e rilancia prepara.bat.")


main()
