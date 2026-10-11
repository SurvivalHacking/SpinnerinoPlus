# ============================================================================
# _ambiente.py — fa girare i convertitori DENTRO il pacchetto.
#
# IL PROBLEMA: i convertitori sono nati in una dozzina di progetti diversi e
# hanno i percorsi scritti dentro, assoluti e del PC di chi li ha scritti
#   C:\Users\Tecnico\Desktop\PROGETTI SH\GALAGONE 2.8VPE\...
# Su un altro computer quei percorsi non esistono e lo script muore.
#
# LA CURA: invece di correggere 94 script a mano (94 occasioni di sbagliare su
# codice che gia' funziona), si mette UN solo blocco davanti a tutti che
# DIROTTA ogni percorso fuori dal pacchetto verso il posto giusto qui dentro.
# Il dirottamento non e' indovinato: le ROM stanno in ROMS\, e per gli header
# c'e' INDICE_HEADER.txt, che dice per ognuno dove va rimesso (326 voci,
# nessuna collisione di nome: la corrispondenza e' univoca).
#
# ⚠️ Dirotta SEMPRE, anche se il percorso assoluto esistesse davvero: su una
# macchina che ha anche i progetti originali, scrivere li' sarebbe peggio che
# fallire.
# ============================================================================
import builtins, os, re, stat as _statmod, sys

# Un header e' ARTE (logo, copertina, splash) se il nome lo dice: quelli
# viaggiano gia' pronti nel pacchetto e non vanno rigenerati dalle ROM.
_E_ARTE = re.compile(r"(_logo|_grid_logo|_card_logo|_oled_logo|^logo\.h$|"
                     r"splash_|giochi_card|giochi_header|_overlay)", re.I)
_PROTEGGI_ARTE = os.environ.get("PACCHETTO_RIFAI_LOGHI", "0") != "1"
_NON_SI_RISCRIVONO = {"nibbler_crunch.h"}

QUI        = os.path.dirname(os.path.abspath(__file__))
PACCHETTO  = os.path.dirname(QUI)
PROGETTO   = os.path.join(PACCHETTO, "SPINNERINO")
ROMS       = os.path.join(PACCHETTO, "ROMS")
VIDEO      = os.path.join(PACCHETTO, "VIDEO")
LOGHI      = os.path.join(PROGETTO, "LOGHI")
MACHINES   = os.path.join(PROGETTO, "machines")
_SCARTO    = os.path.join(QUI, "_scarto")   # dove finiscono le
                                            # riscritture dei loghi

IMMAGINI = (".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp")

# ------------------------------------------------------------------ indice
_INDICE = {}
_percorso_indice = os.path.join(QUI, "INDICE_HEADER.txt")
if os.path.isfile(_percorso_indice):
    with open(_percorso_indice, encoding="latin-1") as _f:
        for _riga in _f:
            if _riga.startswith("#") or "\t" not in _riga:
                continue
            # ⚠️ Il file ha PIU' colonne (nome, percorso, e altro): si prende
            # la SECONDA e basta. Prendendo «tutto il resto» il percorso si
            # portava dietro i tabulatori e la open moriva con
            # «[Errno 22] Invalid argument» in dieci convertitori, che
            # sembravano rotti loro.
            _campi = _riga.rstrip("\n").split("\t")
            if len(_campi) < 2:
                continue
            # ⛔⛔ I percorsi dell'indice sono scritti col BACKSLASH
            # (`machines\pang\pang_rom.h`): li ha generati `fai_indice.py` su
            # Windows. Su Mac un `join` con quella stringa da' un nome di file
            # che CONTIENE i backslash — l'header non finisce in
            # `machines/pang/`, finisce in un file con un nome assurdo nella
            # radice del progetto, e nessuno se ne accorge finche' il firmware
            # non compila. Si converte in ingresso, una volta per riga.
            _INDICE[_campi[0].strip().lower()] = \
                _campi[1].strip().replace("\\", os.sep)

# ------------------------------------------------------------------ cache
_trovati = {}


def _cerca_nel_pacchetto(base):
    """Cerca un file per nome dentro il pacchetto. Risultato messo in cache:
    la ricerca costa, e i convertitori chiedono lo stesso file molte volte."""
    b = base.lower()
    if b in _trovati:
        return _trovati[b]
    for radice in (ROMS, LOGHI, PROGETTO, VIDEO, QUI):
        if not os.path.isdir(radice):
            continue
        for cartella, _dirs, files in os.walk(radice):
            for nome in files:
                if nome.lower() == b:
                    _trovati[b] = os.path.join(cartella, nome)
                    return _trovati[b]
    _trovati[b] = None
    return None


_nelle_rom = {}

# ⛔⛔⛔ NON basta prendere `os.path.exists`/`isfile`/`isdir` PRIMA del
# dirottamento: **catturare una funzione non ne congela il corpo.** Su Windows
# fino a Python 3.11 quelle tre sono scritte in Python (`genericpath`) e
# cercano `os.stat` dentro il modulo `os` A OGNI CHIAMATA — quindi la copia
# "vecchia" trova comunque la NOSTRA `os.stat`, e l'aggancio si avvita:
#   os.stat -> _stat -> risolvi -> _esiste_davvero -> os.stat -> ...
# Referto di un amico con **Python 3.9.13**: `RecursionError: maximum
# recursion depth exceeded in comparison` su TUTTI e 65 i convertitori, dal
# primo. Dalla **3.12** `ntpath` le prende in C da `nt` (`_path_exists`,
# `_path_isdir`, `_path_isfile`) e il difetto non si vede — per questo qui
# non si e' mai visto.
# ⭐ `os.stat` invece E' una funzione C: catturarla congela davvero il
# comportamento. Da qui in giu' l'esistenza si chiede SOLO a lei.
_stat_reale = os.stat


def _esiste_davvero(p):
    """Esistenza vera, senza passare dal dirottatore (o si avvita)."""
    try:
        _stat_reale(p)
    except (OSError, ValueError):
        return False
    return True


def _e_file_davvero(p):
    try:
        return _statmod.S_ISREG(_stat_reale(p).st_mode)
    except (OSError, ValueError):
        return False


def _isdir_reale(p):
    try:
        return _statmod.S_ISDIR(_stat_reale(p).st_mode)
    except (OSError, ValueError):
        return False


def _cerca_nelle_rom(base):
    """Cerca un file per nome SOLO dentro ROMS\\ (zip scompattate comprese)."""
    if not base:
        return None
    b = base.lower()
    if b in _nelle_rom:
        return _nelle_rom[b]
    trovato = None
    if _isdir_reale(ROMS):
        for cartella, _dirs, files in os.walk(ROMS):
            for nome in files:
                if nome.lower() == b:
                    trovato = os.path.join(cartella, nome)
                    break
            if trovato:
                break
    _nelle_rom[b] = trovato
    return trovato


def _dentro_pacchetto(p):
    try:
        return os.path.commonpath([os.path.abspath(p), PACCHETTO]) == PACCHETTO
    except ValueError:          # unita' diverse su Windows
        return False


# ⛔⛔ SI DIROTTANO SOLO I DATI, MAI IL RESTO.
# Al primo giro il dirottamento valeva per QUALUNQUE percorso fuori dal
# pacchetto: cosi' finivano dentro anche le cartelle di Python e delle
# librerie, e `import numpy` moriva con
#   «DLL load failed while importing _multiarray_umath»
# in tre convertitori, che sembravano rotti loro. Un dirottatore troppo
# largo non da' un errore che lo nomina: rompe il vicino.
ESTENSIONI_DATI = (".zip", ".h", ".bin", ".rom", ".pcm", ".idx", ".mjpg",
                   ".m2v", ".ogg", ".wav", ".scr", ".mgi", ".txt", ".dat",
                   ".png", ".jpg", ".jpeg", ".bmp", ".gif", ".webp",
                   ".table", ".pal", ".prom")


_PARE_WINDOWS = re.compile(r"^[A-Za-z]:[\\/]|^\\\\")


def _capisci_windows(p):
    """⛔⛔⛔ SUL MAC I PERCORSI WINDOWS NON SI CAPISCONO DA SOLI (2026-09-12).

    Gli script hanno dentro i percorsi del PC di chi li ha scritti, scritti col
    BACKSLASH. Su Windows `os.path` li spezza bene; su Mac e Linux il backslash
    **non e' un separatore**, e' un carattere qualunque di nome, quindi:

        posixpath.basename(r"C:\\Users\\...\\ROMS\\1942.zip")
            -> 'C:\\Users\\...\\ROMS\\1942.zip'      (TUTTA la stringa!)

    Cioe' `base` non e' piu' `1942.zip`: l'indice degli header non trova mai
    niente, la zip viene cercata come `<pacchetto>/ROMS/C:\\Users\\...\\1942.zip`
    e il referto dice «manca 1942.zip» con la zip al suo posto. **Nessuno dei
    52 script che hanno un percorso assoluto dentro verrebbe dirottato**, e in
    silenzio: gli header finirebbero nello scarto.

    Cura: su un sistema non-Windows, un percorso che PARE di Windows (`C:\\...`,
    `\\\\server\\...`) o che contiene backslash viene riscritto con `/`, cosi'
    `os.path` lo spezza come si deve.
    ⚠️ Prezzo dichiarato: su Mac/Linux il backslash puo' stare davvero dentro un
    nome di file. In questo pacchetto non succede (verificato sui 925 file), e
    comunque qui si guardano solo percorsi da dirottare, mai nomi inventati
    dall'utente."""
    if os.sep == "\\":
        return p
    if "\\" in p:
        p = p.replace("\\", "/")
    # ⛔ (Spinnerino Maker, 2026-10-11) Tolti i backslash NON basta: `C:/Users/...`
    # su Mac e Linux non comincia con `/`, quindi e' un percorso RELATIVO e
    # finisce sotto la cartella di lavoro, che e' SPINNERINO/. Risultato visto
    # su un Mac vero: una cartella `SPINNERINO/C:/Users/Tecnico/Desktop/...`
    # con dentro mezza scrivania di chi ha scritto i convertitori (il Finder la
    # mostra come `C/`). E siccome cade DENTRO il pacchetto, `_riaggancia` non
    # la toccava mai.
    # Gli si mette davanti `/`: diventa assoluto e ESTRANEO come lo e' su
    # Windows, e da li' in giu' vale la stessa strada (zip e header per nome,
    # cartelle riagganciate a SPINNERINO/, il resto nello scarto).
    if re.match(r"^[A-Za-z]:/", p):
        p = "/" + p
    return p


def risolvi(percorso, scrittura=False):
    """Percorso di partenza -> percorso dentro il pacchetto.
    ⚠️ Accetta anche gli oggetti di pathlib: `Path.exists()` chiama os.stat con
    un Path, non con una stringa, e un dirottatore che guarda solo le stringhe
    se lo lascia passare. Popeye diceva «zip non trovato» con la zip al suo
    posto, e sembrava un difetto del convertitore."""
    if percorso is not None and not isinstance(percorso, str):
        try:
            percorso = os.fspath(percorso)
        except TypeError:
            return percorso
    if not isinstance(percorso, str) or not percorso:
        return percorso
    if percorso.startswith(("http://", "https://")):
        return percorso
    # ⛔⛔ Le ZONE DI SISTEMA (Python, le sue librerie, la cartella temporanea)
    # passano INTATTE, in lettura e in scrittura. Non e' una comodita': e' la
    # riga che impedisce di rifare il danno gia' fatto a numpy, e quella che
    # lascia lavorare `tempfile` e Pillow, che i file di brutta se li fanno
    # dove vogliono loro.
    if _e_di_sistema(percorso):
        return percorso
    percorso = _capisci_windows(percorso)
    if os.path.splitext(percorso)[1].lower() not in ESTENSIONI_DATI:
        # ⭐ I file di ROM degli arcade hanno estensioni qualsiasi (`xvi-1.5n`,
        # `c_5et_g.bin`, `galmidw.u`, `sb-0.f1`): un elenco non li copre.
        # Regola stretta e SICURA: se il percorso non esiste e un file con
        # quel nome sta in ROMS\, si usa quello. Cerca solo dentro ROMS, mai
        # in giro: cosi' non puo' toccare i file di Python o delle librerie,
        # che e' l'errore che aveva rotto numpy.
        if not scrittura and not _esiste_davvero(percorso):
            fra_le_rom = _cerca_nelle_rom(os.path.basename(percorso))
            if fra_le_rom:
                return fra_le_rom
        # ⛔⛔ LE CARTELLE passano di qui (non hanno estensione) e uscivano
        # intatte: `os.listdir(r"C:\...\LOGHI")` e `os.chdir` andavano a
        # sbattere sul percorso del MIO PC. Su Windows, da me, quelle cartelle
        # esistono e non si vede niente. Si riaggancia solo se nel percorso si
        # riconosce una cartella di casa: cosi' Python e le librerie non
        # vengono toccati. Stesso difetto di `_cartella_giusta`, altra porta.
        if not _esiste_davvero(percorso) and not _dentro_pacchetto(percorso):
            return _riaggancia(percorso, scarto=False)
        return percorso

    p = percorso.replace("/", os.sep)
    # ⛔ (Spinnerino Maker, 2026-10-11) `abspath` SEMPRE, anche se il percorso e'
    # gia' assoluto: serve a togliere i `..`. Windows li risolve sulla carta,
    # Mac e Linux no — vogliono che ogni cartella attraversata ESISTA.
    # `romconv_mariobros.py` scrive in
    #   CONVERTITORI/../machines/mariobros/../../logo/mariobros.bin
    # e `<pacchetto>/machines/mariobros` non esiste (la vera sta dentro
    # SPINNERINO/, dirottata): su Windows passa, su un Mac vero e' morto con
    # «FileNotFoundError: [Errno 2] No such file or directory: '/Us...».
    p = os.path.abspath(p)

    base = os.path.basename(p)
    if not base:                                    # e' una cartella
        return p
    nome, est = os.path.splitext(base)
    est = est.lower()

    # ⛔ Zip e header si portano al loro posto SEMPRE, anche se il percorso di
    # partenza cade gia' dentro il pacchetto. Con la scorciatoia «e' dentro,
    # lascia stare», un convertitore che cerca un `ROMS\` RELATIVO finiva in
    # SPINNERINO\ROMS\ - che non esiste - e diceva «manca galaga.zip» con la
    # zip al suo posto. Il posto giusto di una zip e' uno solo.
    if est == ".zip":
        # I pacchetti Daphne stanno in VIDEO, non in ROMS. Mandando OGNI
        # zip fra le ROM, romconv_dl_header.py diceva «manca dl_ntsc.zip»
        # con la zip al suo posto.
        in_rom = os.path.join(ROMS, base)
        if _esiste_davvero(in_rom):
            return in_rom
        in_video = os.path.join(VIDEO, base)
        if _esiste_davvero(in_video):
            return in_video
        return in_rom     # non c-e: il messaggio deve nominare ROMS

    if est == ".h":
        # ⛔⛔ I LOGHI NON SI RIGENERANO. Sono nel pacchetto gia' fatti, nascono
        # dalle IMMAGINI dell'utente e non dalle ROM — ma diversi convertitori
        # di ROM ne scrivono una propria versione, sbagliata, sopra quella
        # buona: `romconv_xevious.py` rovinava `xevious_logo.h` e
        # `xevious_oled_logo.h` che erano perfetti. La scrittura si manda in
        # una cartella di scarto: il convertitore non se ne accorge e il logo
        # resta quello giusto. Con `prepara.py --loghi` la protezione si toglie.
        # Oltre all'arte ci sono header che il pacchetto porta gia' pronti e
        # che servono da INGRESSO ai convertitori. `romconv_nibbler.py` (che
        # viene da CABINATONE) LEGGE nibbler_crunch.h a 24 kHz e ci scrive
        # sopra la sua versione ricampionata: se lo lasci fare, la prima volta
        # lo distrugge (52 KB -> 463 byte) e la seconda non trova piu' niente.
        # ⛔ Un `temp_*.h` NON e' un header del progetto: e' il foglio di brutta
        # del convertitore, che lo scrive, lo rilegge e poi lo CANCELLA con
        # `os.remove(nome_relativo)`. `os.remove` non passa di qui, quindi se
        # noi spostiamo la scrittura altrove la cancellazione non trova piu'
        # niente e il convertitore muore alla fine, dopo aver lavorato bene
        # (`mspacman_rom_convert.py`). Si lascia dov'e'.
        if base.lower().startswith("temp_"):
            return p
        if scrittura and base.lower() in _NON_SI_RISCRIVONO:
            _makedirs_vero(_SCARTO, exist_ok=True)
            return os.path.join(_SCARTO, base)
        if scrittura and _E_ARTE.search(base) and _PROTEGGI_ARTE:
            _makedirs_vero(_SCARTO, exist_ok=True)
            return os.path.join(_SCARTO, base)
        rel = _INDICE.get(base.lower())
        if rel:
            return os.path.join(PROGETTO, rel)
        # ⚠️ Header non censito (l'indice elenca solo quelli GENERATI). In
        # lettura puo' essere un header che il pacchetto contiene gia' e che
        # serve come INGRESSO: `romconv_nibbler.py` legge `nibbler_crunch.h`.
        # Mandarlo nella radice del progetto lo faceva sparire.
        if not scrittura:
            trovato = _cerca_nel_pacchetto(base)
            if trovato:
                return trovato
        # ⛔⛔⛔ In SCRITTURA un header NON CENSITO va nella cartella di scarto.
        # L'indice elenca TUTTI e 321 gli header che questo progetto genera:
        # se un convertitore ne scrive un altro, quello viene da un porting
        # diverso e il suo contenuto e' per un altro firmware.
        #   - mandarlo nella radice del progetto (com'era) lasciava 28 DOPPIONI
        #     accanto ai buoni, quattro con valori sbagliati;
        #   - mandarlo DOVE IL FILE GIA' STA (provato, e ritirato) e' peggio:
        #     `romconv_frogger.py`, `mooncresta_rom_convert.py`,
        #     `romconv_nibbler.py` e `romconv_scramble.py` hanno SOVRASCRITTO
        #     con valori sbagliati quattro header che il pacchetto porta
        #     GIUSTI. Stessa famiglia di `nibbler_crunch.h`.
        # Nello scarto non danno fastidio a nessuno e restano da guardare.
        _makedirs_vero(_SCARTO, exist_ok=True)
        return os.path.join(_SCARTO, base)

    # ⚠️ «e' gia' dentro il pacchetto, lascia stare» vale solo se li' c'e'
    # davvero. Gli script riscrivono `__file__` verso cartelle che qui non
    # esistono (`SPINNERINO\FILE ROMCONV PY\gyruss_patches`): il percorso cade
    # dentro il pacchetto, ma il file no, e la patch veniva saltata in
    # silenzio - con Gyruss che usciva sbagliato e nessun messaggio.
    if _dentro_pacchetto(p) and (scrittura or _esiste_davvero(p)):
        return p

    trovato = _cerca_nel_pacchetto(base)
    if trovato and not scrittura:
        return trovato

    if est in IMMAGINI:
        return os.path.join(LOGHI, base)
    if est in (".m2v", ".ogg", ".txt", ".mjpg", ".idx", ".pcm", ".wav"):
        return os.path.join(VIDEO, base)
    return os.path.join(PROGETTO, base)


# ------------------------------------------------------------ dirottamenti
_open_vero      = builtins.open
# ⛔ NON `os.path.exists/isfile/isdir`: fino a Python 3.11 si avvitano (vedi il
# blocco lungo qui sopra). Queste tre passano da `os.stat` catturata, che e' C.
_exists_vero    = _esiste_davvero
_isfile_vero    = _e_file_davvero
_isdir_vero     = _isdir_reale
_makedirs_vero  = os.makedirs
_listdir_vero   = os.listdir
_scandir_vero   = os.scandir
_chdir_vero     = os.chdir


# ⛔⛔⛔ RETE DI SICUREZZA (2026-09-14) — NIENTE ESCE DAL PACCHETTO.
# Referto dell'amico: due convertitori in
#   `PermissionError: [Errno 13] Permission denied: 'C:\Users\...'`
# Qui non si riproduce nemmeno spegnendo TUTTE le mie cartelle (64 su 64 ok,
# 0 errori): vuol dire che un percorso assoluto e' sfuggito al dirottamento
# per una strada che il banco di prova non tocca. Invece di inseguirlo, si
# toglie la possibilita' stessa: dopo `risolvi()`, una SCRITTURA che sta
# ancora fuori dal pacchetto va nello SCARTO, e una cartella fuori dal
# pacchetto NON SI CREA. Sul disco di un altro non si scrive mai, punto.
# ⚠️ Le zone di SISTEMA restano intoccabili nei due sensi: Python, le sue
# librerie e la cartella temporanea devono poter lavorare — e' l'errore che
# aveva gia' rotto numpy una volta, e Pillow scrive davvero file temporanei.
import errno as _errno
import tempfile as _tempfile

_ZONE_DI_SISTEMA = tuple(sorted({
    os.path.abspath(x).lower().rstrip(os.sep) + os.sep
    for x in (sys.prefix, getattr(sys, "base_prefix", sys.prefix),
              os.path.dirname(os.path.abspath(sys.executable)),
              _tempfile.gettempdir())
    if x}))


def _e_di_sistema(p):
    try:
        t = os.path.abspath(p).lower().rstrip(os.sep) + os.sep
    except Exception:
        return True                     # nel dubbio non si tocca
    return any(t.startswith(z) for z in _ZONE_DI_SISTEMA)


def _dentro_casa(p):
    return _dentro_pacchetto(p) or _e_di_sistema(p)


def _mai_fuori(p):
    """SCRITTURA che dopo il dirottamento sta ancora fuori: va nello scarto."""
    if not isinstance(p, str) or _dentro_casa(p):
        return p
    _makedirs_vero(_SCARTO, exist_ok=True)
    return os.path.join(_SCARTO, os.path.basename(p) or "senza_nome")


def _da_leggere(p):
    """LETTURA rimasta fuori dal pacchetto: si prova a ritrovare il file per
    nome qui dentro. ⚠️ E `open()` di una CARTELLA, su Windows, da'
    `[Errno 13] Permission denied` — un messaggio che non nomina mai la vera
    causa: lo si traduce in «non trovato», cosi' si legge."""
    if not isinstance(p, str):
        return p
    if not _dentro_casa(p):
        trovato = _cerca_nel_pacchetto(os.path.basename(p))
        if trovato:
            return trovato
    if _isdir_reale(p):
        raise FileNotFoundError(_errno.ENOENT,
                                "e' una CARTELLA, non un file", p)
    return p


def _open(file, mode="r", *a, **k):
    if file is not None and not isinstance(file, int):
        scrive = any(c in mode for c in "wxa+")
        file = risolvi(file, scrittura=scrive)
        if scrive:
            file = _mai_fuori(file)
            d = os.path.dirname(file)
            if d:
                _makedirs_vero(d, exist_ok=True)
        else:
            file = _da_leggere(file)
    return _open_vero(file, mode, *a, **k)


def _exists(p):   return _exists_vero(risolvi(p))
def _isfile(p):   return _isfile_vero(risolvi(p))


def _isdir(p):
    return _isdir_vero(risolvi(p))


SUE_DEL_PACCHETTO = {"roms", "video", "convertitori", "spinnerino",
                     # ⭐ 1944, Ghouls, Out Run e Neo Geo NON hanno la ROM
                     # negli header: la leggono dalla SD. I loro .bin
                     # vanno QUI, nella radice del pacchetto.
                     "da copiare su sd"}


def _riaggancia(p, scarto=True):
    """Cartella con un percorso ASSOLUTO di un ALTRO computer -> qui dentro.
    Si cerca dall'ULTIMO segmento all'indietro una cartella di casa
    (`SPINNERINO`, `ROMS`, `VIDEO`, `DA COPIARE SU SD`, `CONVERTITORI`) e si
    ricostruisce da li' in giu':
        ...\\SPINNERINO NEW P4p\\SPINNERINO\\machines\\arkanoid
        -> <pacchetto>\\SPINNERINO\\machines\\arkanoid
    Si va dall'ULTIMO perche' quei percorsi contengono la parola SPINNERINO
    piu' volte (il nome del progetto e quello della cartella che lo contiene):
    il segmento buono e' sempre quello piu' a destra.
    Se non si riconosce niente si va nello scarto: una cartella creata a caso
    dentro il progetto e' una cartella fantasma, e nessuno la va a cercare."""
    pezzi = [x for x in os.path.abspath(p).split(os.sep) if x]
    for i in range(len(pezzi) - 1, -1, -1):
        if pezzi[i].lower() in SUE_DEL_PACCHETTO:
            return os.path.join(PACCHETTO, *pezzi[i:])
    # ⚠️ `scarto=False` = «se non riconosco niente, lascialo com'era». Serve
    # per le LETTURE: un percorso che non c'entra col pacchetto (Python, le
    # librerie) non va toccato, o si rompe il vicino — e' l'errore che aveva
    # gia' ammazzato numpy una volta.
    if not scarto:
        return p
    _makedirs_vero(_SCARTO, exist_ok=True)
    return os.path.join(_SCARTO, pezzi[-1] if pezzi else "senza_nome")


def _cartella_giusta(name):
    """Una cartella creata da un convertitore va dentro SPINNERINO\\.
    ⚠️ Molti costruiscono i percorsi da `__file__`, che qui sta in
    CONVERTITORI\\: un `..\\machines` atterra nella RADICE del pacchetto e
    lascia una cartella fantasma accanto a prepara.bat."""
    if not isinstance(name, str) or not name:
        return name
    p = os.path.abspath(name)
    if _e_di_sistema(p):
        return name                     # temp, Python, librerie: non si toccano
    if not _dentro_pacchetto(p):
        # ⛔⛔⛔ QUESTA RIGA SI VEDEVA SOLO SUL PC DI UN ALTRO (2026-09-12).
        # `risolvi()` dirotta i FILE guardandone l'ESTENSIONE: una CARTELLA non
        # ce l'ha, quindi usciva di qui intatta — col percorso assoluto del PC
        # di chi ha scritto il convertitore
        # (`...\SPINNERINO NEW P4p\SPINNERINO\machines\arkanoid`).
        # Su QUESTO computer quella cartella ESISTE davvero: la makedirs riesce,
        # il file .h viene comunque dirottato da `risolvi()` e non si vede
        # niente. Sul computer di chiunque altro e' un errore secco e il
        # convertitore muore. Misurato fingendo un PC senza le mie cartelle:
        # **9 convertitori su 63 in errore, tutti da qui** (arkangc, arkanoid2,
        # boblbobl, gigas, gigas2, goindol, pang, sbrkout, tempest) e 140
        # segnaposto invece di 93.
        # ⚠️ Ed e' anche cio' che l'intestazione di questo file PROMETTE:
        # «Dirotta SEMPRE, anche se il percorso assoluto esistesse davvero: su
        # una macchina che ha anche i progetti originali, scrivere li' sarebbe
        # peggio che fallire». Per i file era vero, per le cartelle no.
        p = _riaggancia(p)
        if not _dentro_pacchetto(p):
            return p
    rel = os.path.relpath(p, PACCHETTO)
    if rel in (".", os.curdir):
        return p
    pezzi = rel.split(os.sep)
    prima = pezzi[0].lower()
    # ⛔⛔ `SPINNERINO\DA COPIARE SU SD\` — una cartella FANTASMA, e non era un
    # caso isolato: `_chdir()` qui sotto porta la cartella di lavoro DENTRO
    # `SPINNERINO\`, quindi un convertitore che compone un percorso RELATIVO
    # («DA COPIARE SU SD/ghouls», `romconv_ghouls.py` e `romconv_outrun.py`)
    # atterra sotto il progetto. I FILE finivano al posto giusto — `risolvi()`
    # conosce le estensioni di dati — ma le CARTELLE no, e restavano li' vuote.
    # Nessuno se ne accorgeva: il progetto compila lo stesso e la cartella e'
    # vuota. Se il primo pezzo SOTTO il progetto e' una cartella di casa, si
    # rimette alla radice del pacchetto, dove il convertitore la voleva.
    if prima == "spinnerino" and len(pezzi) > 1 \
            and pezzi[1].lower() in SUE_DEL_PACCHETTO:
        return os.path.join(PACCHETTO, *pezzi[1:])
    if prima in SUE_DEL_PACCHETTO:
        return p                       # ROMS, VIDEO, CONVERTITORI, SPINNERINO
    return os.path.join(PROGETTO, rel)  # tutto il resto va dentro il progetto


def _makedirs(name, *a, **k):
    # ⛔ (Spinnerino Maker, 2026-10-11) Una cartella dal nome VUOTO non si crea
    # e non si protesta. `romconv_pang.py` fa
    #   os.makedirs(os.path.dirname(OUT))   con   OUT = r"C:\Users\...\pang_rom_assets.h"
    # Su Windows `dirname` da' la cartella; su Mac e Linux il backslash non e'
    # un separatore, quindi `dirname` da' '' e `makedirs('')` muore con
    # «FileNotFoundError: [Errno 2] No such file or directory: ''» (visto su un
    # Mac vero). La cartella giusta la crea comunque `_open` quando si scrive
    # il file, che viene dirottato per nome.
    if isinstance(name, str) and not name:
        return None
    if isinstance(name, str):
        name = _cartella_giusta(risolvi(name, scrittura=True))
        # ⛔⛔⛔ Una cartella che dopo tutto il dirottamento sta ANCORA fuori
        # non si crea e non si protesta: su un altro PC `makedirs` risalirebbe
        # fino a `C:\Users\<un nome che non esiste>` e morirebbe li' con
        # `[Errno 13] Permission denied` — un errore che non nomina ne' il
        # convertitore ne' il file, e che qui non si vede MAI perche' quelle
        # cartelle da me esistono davvero.
        if not _dentro_casa(name):
            return None
    return _makedirs_vero(name, *a, **k)


def _listdir(p="."):
    return _listdir_vero(risolvi(p))


def _scandir(p="."):
    # ⚠️ `os.walk` passa da `scandir`, non da `listdir`: senza questo,
    # `romconv_dl_header.py`, `romconv_dl2rom.py` e `romconv_arte_sd.py`
    # camminavano su percorsi non dirottati.
    return _scandir_vero(risolvi(p))




def _chdir(p):
    # I convertitori fanno chdir per «tornare alla radice del progetto»:
    # qui la radice e' una sola e la decidiamo noi.
    # ⚠️ E' il PROGETTO, non il pacchetto: chi scrive con un percorso RELATIVO
    # (`machines\...`, `DA COPIARE SU SD\...`) deve atterrare dentro
    # SPINNERINO\. Puntando al pacchetto si creavano tre cartelle fantasma
    # nella radice, accanto a prepara.bat.
    return _chdir_vero(PROGETTO)


def attiva():
    builtins.open = _open
    os.path.exists, os.path.isfile, os.path.isdir = _exists, _isfile, _isdir
    os.makedirs, os.listdir, os.chdir = _makedirs, _listdir, _chdir
    os.scandir = _scandir
    # ⛔ `os.mkdir` e `os.remove` NON si agganciano, provato e ritirato il
    # 14/09: `os.makedirs` VERA chiama `mkdir` al suo interno, e cosi' fa
    # `tempfile.mkdtemp()`. Agganciandola, la cartella temporanea di
    # `romconv_1942.py` veniva creata nello SCARTO mentre mkdtemp restituiva
    # il nome ORIGINALE: da quel momento il convertitore lavorava su una
    # cartella che non esisteva (`FileExistsError [WinError 183]`).
    # ⭐ La lezione: un aggancio a una primitiva su cui e' COSTRUITA la
    # funzione che gia' si aggancia si applica DUE volte.

    # ⚠️ pathlib NON passa di qui: `Path.exists()` chiama os.stat e
    # `Path.open()` chiama io.open, non builtins.open. Sei convertitori
    # dicevano «zip non trovato» con la zip al suo posto, e la colpa
    # sembrava della zip.
    import io
    global _stat_vero, _io_open_vero
    _stat_vero = os.stat
    _io_open_vero = io.open

    def _stat(p, *a, **k):
        return _stat_vero(risolvi(p), *a, **k)

    def _io_open(file, mode="r", *a, **k):
        if isinstance(file, str):
            scrive = any(c in mode for c in "wxa+")
            file = risolvi(file, scrittura=scrive)
            if scrive:
                file = _mai_fuori(file)          # stessa rete di builtins.open
                if os.path.dirname(file):
                    _makedirs_vero(os.path.dirname(file), exist_ok=True)
            else:
                file = _da_leggere(file)
        return _io_open_vero(file, mode, *a, **k)

    os.stat = _stat
    io.open = _io_open

    import zipfile
    if not getattr(zipfile.ZipFile, "_dirottata", False):
        _zip_init = zipfile.ZipFile.__init__

        def init(self, file, *a, **k):
            if isinstance(file, (str, os.PathLike)):
                file = risolvi(file)
            return _zip_init(self, file, *a, **k)
        zipfile.ZipFile.__init__ = init
        zipfile.ZipFile._dirottata = True

    try:
        from PIL import Image
        if not getattr(Image, "_dirottata", False):
            _img_open = Image.open

            def apri(fp, *a, **k):
                if isinstance(fp, (str, os.PathLike)):
                    fp = risolvi(fp)
                return _img_open(fp, *a, **k)
            Image.open = apri
            Image._dirottata = True
    except ImportError:
        pass

    for c in (ROMS, VIDEO, PROGETTO):
        _makedirs_vero(c, exist_ok=True)
    _chdir_vero(PROGETTO)
