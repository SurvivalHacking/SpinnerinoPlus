#!/usr/bin/env python3
# ============================================================================
# spinnerino_tool.py — la faccia di prepara.py, uguale su Windows e su Mac.
#
# Si lancia da  setup.bat  (Windows)  o da  setup.sh  (Mac / Linux): quei due
# preparano un Python tutto dentro questa cartella (.venv, e .python se sul
# computer non ce n'e' uno), poi aprono questo. Cancellata la cartella, sul
# computer non resta niente.
#
#     python spinnerino_tool.py               finestra
#     python spinnerino_tool.py --terminale   senza finestra, a colori
#     (gli altri argomenti passano a prepara.py: galaga, --senza-video, ...)
#
# Cosa fa:
#   - mostra a colpo d'occhio cosa c'e' e cosa manca (ROM, laserdisc, header,
#     librerie, ffmpeg);
#   - lancia prepara.py e ne segue l'avanzamento convertitore per convertitore;
#   - se serve scarica ffmpeg dentro FFMPEG/;
#   - tiene TUTTA l'uscita in un file dentro log/.
#
# ⚠️ prepara.py NON viene toccato ne' importato (chiama main() all'import):
# gira in un processo suo e qui se ne legge l'uscita. Le righe che si
# riconoscono sono quelle che stampa lui: `[  3/65] nome.py   ok   1.2s (4 header)`.
# ============================================================================
import datetime, locale, os, queue, re, shutil, signal, subprocess, sys
import threading, time, zipfile

sys.dont_write_bytecode = True     # niente __pycache__ sparsi nel pacchetto

# QUI    = Spinnerino_Maker/ : il tool e TUTTO quello che crea o scarica
#          (.venv, .python, FFMPEG, log). Cancellata lei, non resta niente.
# RADICE = dove sta il pacchetto (prepara.py, CONVERTITORI, ROMS, VIDEO,
#          SPINNERINO, DA COPIARE SU SD). Di norma e' la stessa cartella:
#          ROM, video e file convertiti stanno tutti qui dentro. Se qui
#          prepara.py non c'e', si lavora sul pacchetto della cartella sopra.
QUI     = os.path.dirname(os.path.abspath(__file__))
RADICE  = QUI
if not os.path.isfile(os.path.join(QUI, "prepara.py")):
    RADICE = os.path.dirname(QUI)
CONV    = os.path.join(RADICE, "CONVERTITORI")
PROG    = os.path.join(RADICE, "SPINNERINO")
ROMS    = os.path.join(RADICE, "ROMS")
VIDEO   = os.path.join(RADICE, "VIDEO")
SD      = os.path.join(RADICE, "DA COPIARE SU SD")
PREPARA = os.path.join(RADICE, "prepara.py")
ERRORI  = os.path.join(RADICE, "prepara_errori.txt")
FFDIR   = os.path.join(QUI, "FFMPEG")
LOGDIR  = os.path.join(QUI, "log")

WIN = os.name == "nt"
MAC = sys.platform == "darwin"
SENZA_FINESTRA = 0x08000000 if WIN else 0      # CREATE_NO_WINDOW

FFMPEG_WIN = ("https://github.com/BtbN/FFmpeg-Builds/releases/download/"
              "latest/ffmpeg-master-latest-win64-gpl.zip")
FFMPEG_MAC = {"ffmpeg":  "https://evermeet.cx/ffmpeg/getrelease/zip",
              "ffprobe": "https://evermeet.cx/ffmpeg/getrelease/ffprobe/zip"}
_BTBN = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-%s-gpl.tar.xz"
FFMPEG_LINUX = {"x86_64": _BTBN % "linux64", "amd64": _BTBN % "linux64",
                "aarch64": _BTBN % "linuxarm64", "arm64": _BTBN % "linuxarm64"}

RE_RIGA    = re.compile(r"^\[\s*(\d+)/(\d+)\]\s+(\S+\.py)\s+(.*)$")
RE_PREFISSO = re.compile(r"^\[\s*(\d+)/(\d+)\]\s+(\S+\.py)\s+")
RE_INIZIO  = re.compile(r"^\[\s*(\d+)/(\d+)\]\s+(\S+\.py)\s*$")
RE_OK      = re.compile(r"^ok\s+([\d.]+)s\s+\((\d+) header(, con avvisi)?\)")
RE_CONTI   = (("ok",         re.compile(r"^convertitori andati bene\s+(\d+)")),
              ("manca",      re.compile(r"^zip mancanti [. ]*(\d+)")),
              ("errore",     re.compile(r"^convertitori in errore [. ]*(\d+)")),
              ("segnaposto", re.compile(r"^segnaposto messi [. ]*(\d+)")),
              ("veri",       re.compile(r"^header veri [. ]*(\d+)")))

ETICHETTE = {"corso": "in corso", "ok": "ok", "avvisi": "ok (avvisi)",
             "manca": "senza zip", "vuoto": "niente",
             "errore": "ERRORE"}


# ------------------------------------------------------------------ stato
def python_vero():
    """⚠️ La finestra gira con pythonw.exe (niente console). Se anche i figli
    partissero con pythonw, ogni ffmpeg lanciato dai convertitori dei video
    aprirebbe una console sua: centinaia di finestre nere che lampeggiano.
    I figli partono con python.exe e una console nascosta, che ereditano."""
    # ⭐ Chi disegna la finestra e chi converte possono essere DUE Python
    # diversi. Su un Mac il Python portatile (3.12, quello che serve ai
    # convertitori) aveva un Tk che andava in crash aprendo la finestra,
    # mentre quello di sistema (3.10) la apriva bene ma faceva fallire i
    # convertitori. setup.sh allora lancia la finestra con l'uno e mette qui
    # il percorso dell'altro.
    motore = os.environ.get("SPINNERINO_PYTHON", "")
    if motore and os.path.isfile(motore):
        return motore
    exe = sys.executable
    if WIN and os.path.basename(exe).lower() == "pythonw.exe":
        vero = os.path.join(os.path.dirname(exe), "python.exe")
        if os.path.isfile(vero):
            return vero
    return exe


_MOTORE = {"quando": 0.0, "dati": None}


def stato_motore(fresco=False):
    """(versione, pillow, numpy) del Python che fara' le conversioni. Se e' lo
    stesso che gira qui si guarda in casa, altrimenti glielo si chiede; la
    risposta si tiene qualche secondo, perche' la finestra lo domanda spesso."""
    if not os.environ.get("SPINNERINO_PYTHON"):
        return sys.version_info[:3], _ha_modulo("PIL"), _ha_modulo("numpy")
    if not fresco and _MOTORE["dati"] and time.time() - _MOTORE["quando"] < 10:
        return _MOTORE["dati"]
    dati = ((0, 0, 0), False, False)
    try:
        p = subprocess.run(
            [python_vero(), "-c",
             "import sys, importlib.util as u; print(*sys.version_info[:3], "
             "int(bool(u.find_spec('PIL'))), int(bool(u.find_spec('numpy'))))"],
            capture_output=True, text=True, timeout=30, creationflags=SENZA_FINESTRA)
        v = [int(x) for x in p.stdout.split()]
        dati = (tuple(v[:3]), bool(v[3]), bool(v[4]))
    except Exception:
        pass
    _MOTORE.update(quando=time.time(), dati=dati)
    return dati


def _elenco_leggimi(cartella):
    """Le zip attese: stanno scritte nel LEGGIMI.txt della cartella."""
    nomi = []
    try:
        with open(os.path.join(cartella, "LEGGIMI.txt"), encoding="latin-1") as f:
            for riga in f:
                m = re.match(r"^\s+(\S.*\.zip)\s*$", riga)
                if m:
                    nomi.append(m.group(1))
    except OSError:
        pass
    return nomi


def _zip_presenti(cartella):
    try:
        return {n.lower() for n in os.listdir(cartella)
                if n.lower().endswith(".zip")}
    except OSError:
        return set()


def percorsi_header():
    """Gli header che i convertitori generano, dall'indice: percorsi assoluti."""
    fuori = []
    try:
        with open(os.path.join(CONV, "INDICE_HEADER.txt"), encoding="latin-1") as f:
            for riga in f:
                if riga.startswith("#") or "\t" not in riga:
                    continue
                fuori.append(os.path.join(
                    PROG, riga.split("\t")[1].strip().replace("\\", os.sep)))
    except OSError:
        pass
    return fuori


def togli_fantasmi():
    """Su Mac e Linux i convertitori hanno dentro percorsi di Windows
    (`C:\\Users\\Tecnico\\...`), che li' diventano cartelle VERE sotto
    SPINNERINO/: una `C:` (il Finder la mostra come `C/`) con dentro mezza
    scrivania di chi li ha scritti. _ambiente.py ora non le crea piu'; qui si
    tolgono quelle rimaste da prima. ⚠️ Si cancellano SOLO cartelle vuote: se
    dentro c'e' un file lo si lascia dov'e' e lo si dice.
    Torna (cartelle tolte, file rimasti)."""
    tolte, rimasti = 0, []
    if WIN or not os.path.isdir(PROG):
        return tolte, rimasti
    for nome in os.listdir(PROG):
        radice = os.path.join(PROG, nome)
        if not re.match(r"^[A-Za-z]:$", nome) or not os.path.isdir(radice):
            continue
        for cartella, _sotto, files in os.walk(radice, topdown=False):
            veri = [f for f in files if f != ".DS_Store"]
            if veri:
                rimasti += [os.path.join(cartella, f) for f in veri]
                continue
            try:
                for f in files:
                    os.remove(os.path.join(cartella, f))
                os.rmdir(cartella)
                tolte += 1
            except OSError:
                pass
    return tolte, rimasti


def foto_header(percorsi):
    f = {}
    for p in percorsi:
        try:
            f[p] = os.path.getmtime(p)
        except OSError:
            f[p] = None
    return f


def trova_ffmpeg():
    """Si chiede a _ffmpeg.py, cioe' a chi lo usera' davvero: cosi' il
    semaforo e i convertitori non possono dare due risposte diverse."""
    mio = os.path.join(FFDIR, "ffmpeg.exe" if WIN else "ffmpeg")
    if os.path.isfile(mio) and (WIN or os.access(mio, os.X_OK)):
        return mio
    try:
        if CONV not in sys.path:
            sys.path.insert(0, CONV)
        import _ffmpeg
        return _ffmpeg._cerca("ffmpeg")
    except Exception:
        return shutil.which("ffmpeg")


def _ha_modulo(nome):
    import importlib, importlib.util
    importlib.invalidate_caches()
    try:
        return importlib.util.find_spec(nome) is not None
    except Exception:
        return False


def stato_ambiente(fresco=False):
    s = {}
    attese_rom, attese_video = _elenco_leggimi(ROMS), _elenco_leggimi(VIDEO)
    rom, video = _zip_presenti(ROMS), _zip_presenti(VIDEO)
    # si contano le zip che SERVONO: una zip sconosciuta non fa numero
    s["rom"] = len([n for n in attese_rom if n.lower() in rom])         if attese_rom else len(rom)
    s["rom_attese"] = len(attese_rom)
    s["rom_mancanti"] = [n for n in attese_rom if n.lower() not in rom]
    # zip che nessun convertitore conosce: il firmware quel gioco non ce l'ha
    conosciute = {n.lower() for n in attese_rom}
    s["rom_sconosciute"] = sorted(rom - conosciute) if attese_rom else []
    s["video"] = len([n for n in attese_video if n.lower() in video]) \
        if attese_video else len(video)
    s["video_attesi"] = len(attese_video) or 3
    s["video_mancanti"] = [n for n in attese_video if n.lower() not in video]

    # header: l'indice elenca quelli che i convertitori generano
    totale = veri = segnaposto = 0
    s["h_mancanti"] = []
    try:
        with open(os.path.join(CONV, "INDICE_HEADER.txt"), encoding="latin-1") as f:
            for riga in f:
                if riga.startswith("#") or "\t" not in riga:
                    continue
                totale += 1
                rel = riga.split("\t")[1].strip().replace("\\", os.sep)
                p = os.path.join(PROG, rel)
                if not os.path.isfile(p):
                    s["h_mancanti"].append(rel)
                    continue
                try:
                    with open(p, encoding="latin-1") as h:
                        if "SEGNAPOSTO" in h.readline():
                            segnaposto += 1
                            s["h_mancanti"].append(rel)
                        else:
                            veri += 1
                except OSError:
                    pass
    except OSError:
        pass
    s["h_totale"], s["h_veri"], s["h_segnaposto"] = totale, veri, segnaposto
    versione, s["pillow"], s["numpy"] = stato_motore(fresco)
    s["versione"] = versione
    s["ffmpeg"] = trova_ffmpeg()
    s["python"] = "%d.%d.%d" % versione
    s["ha_segnaposto"] = os.path.isdir(os.path.join(CONV, "SEGNAPOSTO"))
    return s


def apri(percorso):
    try:
        os.makedirs(percorso, exist_ok=True) if not os.path.splitext(percorso)[1] else None
        if WIN:
            os.startfile(percorso)
        else:
            subprocess.Popen(["open" if MAC else "xdg-open", percorso])
    except Exception:
        pass


# ------------------------------------------------------------------ microSD
ETICHETTA_SD = "SPINNERINO"
# ⛔ Oltre questa misura NON si formatta: una microSD da gioco e' piccola, un
# disco esterno con dentro le foto di una vita no. Meglio rifiutare una scheda
# enorme che cancellare il disco sbagliato.
MAX_FORMATTA = 256 * 1024 ** 3
QUATTRO_GB = 4 * 1024 ** 3 - 1      # FAT32 non tiene file piu' grossi


def gb(n):
    return "%.1f GB" % (n / 1024.0 ** 3) if n >= 1024 ** 3 else "%d MB" % (n // 1024 ** 2)


def elenca_sd():
    """Le schede su cui si puo' copiare: SOLO supporti rimovibili, mai il
    disco di sistema. Ognuna: id, radice, nome, fs, totale, libero, disco."""
    fuori = []
    if WIN:
        import ctypes, string
        k = ctypes.windll.kernel32
        k.SetErrorMode(1)       # niente finestra «inserire un disco» sui lettori vuoti
        sistema = os.environ.get("SystemDrive", "C:").upper()
        maschera = k.GetLogicalDrives()
        for i, lettera in enumerate(string.ascii_uppercase):
            unita = lettera + ":"
            radice = unita + "\\"
            if not (maschera >> i) & 1 or unita == sistema:
                continue
            if k.GetDriveTypeW(radice) != 2:        # 2 = DRIVE_REMOVABLE
                continue
            nome = ctypes.create_unicode_buffer(261)
            fs = ctypes.create_unicode_buffer(261)
            if not k.GetVolumeInformationW(radice, nome, 261, None, None, None, fs, 261):
                continue                            # lettore senza scheda
            try:
                u = shutil.disk_usage(radice)
            except OSError:
                continue
            fuori.append(dict(id=unita, radice=radice, nome=nome.value, fs=fs.value,
                              totale=u.total, libero=u.free, disco=unita))
    elif MAC:
        import plistlib
        for n in sorted(os.listdir("/Volumes")):
            m = os.path.join("/Volumes", n)
            try:
                d = plistlib.loads(subprocess.run(
                    ["diskutil", "info", "-plist", m],
                    capture_output=True, timeout=20).stdout)
                if d.get("MountPoint") in (None, "", "/"):
                    continue
                if d.get("BusProtocol") == "Disk Image":
                    continue
                if not (d.get("RemovableMedia") or d.get("Ejectable")
                        or not d.get("Internal", True)):
                    continue
                fs = d.get("FilesystemUserVisibleName") or d.get("FilesystemType") or "?"
                fuori.append(dict(
                    id=m, radice=m, nome=d.get("VolumeName") or n,
                    fs="FAT32" if "FAT32" in fs else fs,
                    totale=int(d.get("TotalSize") or 0),
                    libero=shutil.disk_usage(m).free,
                    disco=d.get("ParentWholeDisk") or ""))
            except Exception:
                continue
    else:
        # Linux: lo dice lsblk. Vale una partizione MONTATA che sia rimovibile
        # (RM o HOTPLUG) oppure montata dove il sistema mette le chiavette.
        import json
        try:
            voci = json.loads(subprocess.run(
                ["lsblk", "-J", "-b", "-l", "-o",
                 "PATH,RM,HOTPLUG,TYPE,SIZE,FSTYPE,FSVER,LABEL,MOUNTPOINT"],
                capture_output=True, text=True, timeout=20).stdout)["blockdevices"]
        except Exception:
            voci = []
        for v in voci:
            m = v.get("mountpoint")
            if not m or v.get("type") not in ("part", "disk"):
                continue
            if m == "/" or m.startswith(("/boot", "/home", "/usr", "/var", "/snap")):
                continue
            vero = lambda x: x in (True, 1, "1", "true")
            if not (vero(v.get("rm")) or vero(v.get("hotplug"))
                    or m.startswith(("/media/", "/run/media/"))):
                continue
            try:
                libero = shutil.disk_usage(m).free
            except OSError:
                continue
            fs = (v.get("fstype") or "?")
            if fs == "vfat":
                fs = v.get("fsver") or "FAT32"      # lsblk vecchi non hanno FSVER
            fuori.append(dict(id=m, radice=m, nome=v.get("label") or os.path.basename(m),
                              fs=fs, totale=int(v.get("size") or 0), libero=libero,
                              disco=v.get("path") or ""))
    return fuori


def descrivi_sd(s):
    return "%s   %s   %s   %s" % (
        s["id"] if WIN else os.path.basename(s["id"]),
        s["nome"] or "(senza nome)", gb(s["totale"]), s["fs"])


def file_per_sd():
    """Cosa va sulla scheda: tutto DA COPIARE SU SD, meno il LEGGIMI e i file
    nascosti (.DS_Store e simili)."""
    elenco = []
    for r, cartelle, files in os.walk(SD):
        cartelle[:] = sorted(c for c in cartelle if not c.startswith("."))
        for f in sorted(files):
            if f.startswith(".") or (r == SD and f.lower() == "leggimi.txt"):
                continue
            p = os.path.join(r, f)
            elenco.append((p, os.path.relpath(p, SD), os.path.getsize(p)))
    return elenco


# ------------------------------------------------------------------ motore
class Motore:
    """Lancia un lavoro in un thread e ne racconta l'andamento su una coda.
    Eventi: ("log", testo)  ("inizio", i, n, nome)  ("fine", nome, stato, dettaglio)
            ("conto", chiave, numero)  ("chiuso", tipo, codice)"""

    def __init__(self):
        self.coda = queue.Queue()
        self.proc = None
        self.fermato = False
        self.file_log = None
        self._log = None
        self._corrente = None
        self._resto = ""
        self._vivo = False

    def attivo(self):
        return self._vivo           # non `proc`: la copia sulla SD non ne ha uno

    # ---- log su file
    def _apri_log(self, tipo, comando):
        os.makedirs(LOGDIR, exist_ok=True)
        quando = datetime.datetime.now()
        self.file_log = os.path.join(
            LOGDIR, "%s_%s.log" % (tipo, quando.strftime("%Y%m%d_%H%M%S")))
        self._log = open(self.file_log, "w", encoding="utf-8", errors="replace")
        self._log.write("SPINNERINO PLUS - %s - %s\n" % (tipo, quando.strftime("%d/%m/%Y %H:%M:%S")))
        self._log.write("sistema  %s\npython   %s (%s)\ncartella %s\ncomando  %s\n%s\n" % (
            sys.platform, "%d.%d.%d" % stato_motore()[0], python_vero(), RADICE,
            " ".join(comando), "=" * 70))
        self._log.flush()

    def scrivi(self, testo):
        self.coda.put(("log", testo))
        if self._log:
            try:
                self._log.write(testo.replace("\r\n", "\n").replace("\r", "\n"))
                self._log.flush()
            except Exception:
                pass

    # ---- processi
    def _lancia(self, comando, **piu):
        # ⚠️ Solo il gestore degli ERRORI di codifica, non la codifica: cosi'
        # una stampa con un carattere fuori tavolozza non ammazza prepara, e i
        # convertitori continuano a leggere e scrivere come hanno sempre fatto
        # (PYTHONUTF8 cambierebbe la codifica delle loro open()).
        # PIP_NO_CACHE_DIR: se prepara installa una libreria, pip non lascia
        # la sua cache nella cartella dell'utente. Fuori di qui non resta niente.
        amb = dict(os.environ, PYTHONIOENCODING=":backslashreplace",
                   PYTHONUNBUFFERED="1", PIP_NO_CACHE_DIR="1",
                   PIP_DISABLE_PIP_VERSION_CHECK="1")
        # ffmpeg sta nella cartella del tool, non in FFMPEG/ del progetto: i
        # convertitori lo trovano perche' qui glielo si mette nel PATH.
        amb["PATH"] = FFDIR + os.pathsep + amb.get("PATH", "")
        kw = dict(cwd=RADICE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                  stdin=subprocess.DEVNULL, env=amb)
        if WIN:
            kw["creationflags"] = SENZA_FINESTRA
        else:
            kw["start_new_session"] = True
        kw.update(piu)
        self.proc = subprocess.Popen(comando, **kw)
        return self.proc

    def _segui(self, analizza=None):
        """Legge l'uscita del processo a pezzi (non a righe: la riga del
        convertitore in corso resta SENZA a-capo finche' non ha finito)."""
        cod = locale.getpreferredencoding(False) or "utf-8"
        fd = self.proc.stdout.fileno()
        while True:
            try:
                pezzo = os.read(fd, 4096)
            except OSError:
                break
            if not pezzo:
                break
            testo = pezzo.decode(cod, errors="replace")
            if analizza:
                testo = analizza(testo)     # legge, e decide cosa far vedere
            if testo:
                self.scrivi(testo)
        return self.proc.wait()

    def ferma(self):
        self.fermato = True         # la copia sulla SD lo guarda fra un pezzo e l'altro
        p = self.proc
        if not p or p.poll() is not None:
            return
        try:
            if WIN:
                subprocess.run(["taskkill", "/F", "/T", "/PID", str(p.pid)],
                               capture_output=True, creationflags=SENZA_FINESTRA)
            else:
                os.killpg(p.pid, signal.SIGTERM)
        except Exception:
            try:
                p.kill()
            except Exception:
                pass

    def _in_thread(self, tipo, lavoro):
        def giro():
            codice = -1
            try:
                codice = lavoro()
            except Exception as e:
                self.scrivi("\n!! %s: %s\n" % (type(e).__name__, e))
            finally:
                self.proc = None
                if self._log:
                    self._log.close()
                    self._log = None
                self._vivo = False
                self.coda.put(("chiuso", tipo, codice))
        self.fermato = False
        self._vivo = True
        threading.Thread(target=giro, daemon=True).start()

    # ---- microSD
    def copia_sd(self, scheda, formatta):
        self._apri_log("sd", ["copia su", scheda["id"]] + (["formattando"] if formatta else []))

        def lavoro():
            s = self._formatta(scheda) if formatta else scheda
            self._copia(s)
            return 0
        self._in_thread("sd", lavoro)

    def _formatta(self, scheda):
        """⛔ Cancella TUTTO. Prima di toccare qualcosa si riguarda che la
        scheda sia ANCORA quella che l'utente ha confermato: stesso posto e
        stessa misura. Se nel frattempo e' stata tolta e al suo posto c'e'
        un'altra cosa, ci si ferma."""
        ora = [x for x in elenca_sd() if x["id"] == scheda["id"]]
        if not ora or ora[0]["totale"] != scheda["totale"] or ora[0]["disco"] != scheda["disco"]:
            raise RuntimeError("la scheda non e' piu' quella di prima: NON formatto")
        s = ora[0]
        if not 0 < s["totale"] <= MAX_FORMATTA:
            raise RuntimeError("scheda da %s: troppo grande, NON formatto" % gb(s["totale"]))
        if os.path.abspath(QUI).lower().startswith(os.path.abspath(s["radice"]).lower()):
            raise RuntimeError("il programma sta su quella scheda: NON formatto")
        self.scrivi("formatto %s in FAT32 (tutto il contenuto viene cancellato)...\n"
                    % descrivi_sd(s))
        if WIN:
            # format.com e non Format-Volume: quello vuole l'amministratore.
            exe = os.path.join(os.environ.get("SystemRoot", r"C:\Windows"),
                               "System32", "format.com")
            p = subprocess.run([exe, s["id"], "/FS:FAT32", "/Q", "/V:" + ETICHETTA_SD, "/Y"],
                               input="\r\n", capture_output=True, text=True,
                               encoding="oem", errors="replace",
                               creationflags=SENZA_FINESTRA, timeout=1800)
            self.scrivi((p.stdout or "") + (p.stderr or "") + "\n")
            codice = p.returncode
        elif MAC:
            if not s["disco"]:
                raise RuntimeError("non so quale disco sia: NON formatto")
            self._lancia(["diskutil", "eraseDisk", "FAT32", ETICHETTA_SD,
                          "MBRFormat", "/dev/" + s["disco"]])
            codice = self._segui()
            time.sleep(3)           # il tempo di rimontarla
        else:
            # Linux: si chiede a UDisks2, lo stesso servizio che usa il gestore
            # dei file. Per i supporti rimovibili non vuole la password di root.
            if not s["disco"] or not shutil.which("udisksctl") or not shutil.which("gdbus"):
                raise RuntimeError(
                    "per formattare servono udisksctl e gdbus (pacchetti udisks2 "
                    "e libglib2.0-bin): formattala tu in FAT32 e togli la spunta")
            subprocess.run(["udisksctl", "unmount", "-b", s["disco"]],
                           capture_output=True, timeout=120)
            oggetto = "/org/freedesktop/UDisks2/block_devices/" + os.path.basename(s["disco"])
            codice = 1
            # `mkfs-args` (per forzare FAT32) c'e' da udisks 2.9: se non lo
            # conosce si riprova senza, e il controllo qui sotto dice com'e' andata.
            for opzioni in ("{'label': <'%s'>, 'mkfs-args': <['-F', '32']>}" % ETICHETTA_SD,
                            "{'label': <'%s'>}" % ETICHETTA_SD):
                p = subprocess.run(
                    ["gdbus", "call", "--system", "--dest", "org.freedesktop.UDisks2",
                     "--object-path", oggetto, "--method",
                     "org.freedesktop.UDisks2.Block.Format", "vfat", opzioni],
                    capture_output=True, text=True, timeout=1800)
                self.scrivi((p.stdout or "") + (p.stderr or ""))
                codice = p.returncode
                if codice == 0:
                    break
            time.sleep(2)
            p = subprocess.run(["udisksctl", "mount", "-b", s["disco"]],
                               capture_output=True, text=True, timeout=120)
            self.scrivi((p.stdout or "") + (p.stderr or ""))
            time.sleep(1)
        dopo = [x for x in elenca_sd() if x["disco"] == s["disco"]]
        if codice != 0 or not dopo or dopo[0]["fs"] != "FAT32":
            raise RuntimeError(
                "formattazione non riuscita. Le schede oltre i 32 GB certe "
                "versioni di Windows non le fanno in FAT32: formattala con un "
                "altro programma e rilancia la copia SENZA la spunta.")
        self.scrivi("formattata: %s\n" % descrivi_sd(dopo[0]))
        return dopo[0]

    @staticmethod
    def _gia_copiato(src, dst, peso):
        """Stesso peso e stessa data (FAT32 arrotonda ai 2 secondi): non si
        ricopia. I laserdisc sono GB: rifarli ogni volta sarebbe mezz'ora."""
        try:
            st = os.stat(dst)
        except OSError:
            return False
        return st.st_size == peso and abs(st.st_mtime - os.path.getmtime(src)) <= 2

    def _copia(self, s):
        radice = s["radice"]
        if not os.path.isdir(radice):
            raise RuntimeError("la scheda %s non c'e' piu'" % s["id"])
        elenco = file_per_sd()
        if not elenco:
            raise RuntimeError("DA COPIARE SU SD e' vuota: prima va fatta la conversione")
        grossi = [rel for _p, rel, peso in elenco if peso > QUATTRO_GB]
        if grossi:
            raise RuntimeError("file oltre i 4 GB, FAT32 non li tiene: " + ", ".join(grossi))
        da_fare = [x for x in elenco
                   if not self._gia_copiato(x[0], os.path.join(radice, x[1]), x[2])]
        totale = sum(x[2] for x in da_fare)
        # lo spazio dei file che verranno sovrascritti torna libero
        recupero = sum(os.path.getsize(os.path.join(radice, x[1])) for x in da_fare
                       if os.path.isfile(os.path.join(radice, x[1])))
        libero = shutil.disk_usage(radice).free + recupero
        self.scrivi("copio su %s\n   %d file (%s), gia' a posto %d\n" % (
            descrivi_sd(s), len(da_fare), gb(totale), len(elenco) - len(da_fare)))
        if totale > libero:
            raise RuntimeError("sulla scheda non c'e' posto: servono %s, liberi %s"
                               % (gb(totale), gb(libero)))
        fatti = 0
        for src, rel, peso in da_fare:
            dst = os.path.join(radice, rel)
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            self.scrivi("   %s\n" % rel)
            with open(src, "rb") as da, open(dst, "wb") as a:
                while True:
                    if self.fermato:
                        raise RuntimeError("copia interrotta: %s e' rimasto a meta'" % rel)
                    pezzo = da.read(1024 * 1024)
                    if not pezzo:
                        break
                    a.write(pezzo)
                    fatti += len(pezzo)
                    self.coda.put(("avanza", fatti, totale, rel))
                a.flush()
                os.fsync(a.fileno())
            shutil.copystat(src, dst)
        # esistere non e' funzionare: si ricontrolla tutto, anche i gia' fatti
        guasti = [rel for _p, rel, peso in elenco
                  if not os.path.isfile(os.path.join(radice, rel))
                  or os.path.getsize(os.path.join(radice, rel)) != peso]
        if guasti:
            raise RuntimeError("sulla scheda non tornano: " + ", ".join(guasti[:8]))
        self.scrivi("microSD pronta: %d file controllati. Espellila in modo "
                    "sicuro prima di toglierla.\n" % len(elenco))

    # ---- prepara.py
    def prepara(self, argomenti):
        comando = [python_vero(), "-u", PREPARA] + argomenti
        self._corrente, self._resto = None, ""
        self._gia, self._conta = 0, {}
        self._indice = percorsi_header()
        self._foto = foto_header(self._indice)
        self._apri_log("prepara", comando)
        self._log.write(
            "nota     gli esiti dei convertitori sono VERIFICATI sugli header: dove\n"
            "         prepara.py scriverebbe NON HA PRODOTTO NIENTE ma gli header\n"
            "         sono stati scritti, qui la riga dice ok.\n" + "=" * 70 + "\n")

        def lavoro():
            self._lancia(comando)
            codice = self._segui(self._analizza)
            if self._resto[self._gia:]:         # una riga rimasta a meta'
                self.scrivi(self._resto[self._gia:] + "\n")
            tolte, rimasti = togli_fantasmi()
            if tolte:
                self.scrivi("\ntolte %d cartelle vuote lasciate dai percorsi "
                            "Windows dentro SPINNERINO (la cartella C:)\n" % tolte)
            if rimasti:
                self.scrivi("\n!! dentro SPINNERINO c'e' una cartella C: con %d "
                            "file veri, non la tocco:\n   %s\n" % (
                                len(rimasti), "\n   ".join(rimasti[:8])))
            return codice
        self._in_thread("prepara", lavoro)

    @staticmethod
    def _esito(testo):
        t = testo.strip()
        m = RE_OK.match(t)
        if m:
            return ("avvisi" if m.group(3) else "ok",
                    "%s file in %s s" % (m.group(2), m.group(1)))
        if t.startswith("NON HA PRODOTTO NIENTE"):
            return "vuoto", "non ha scritto niente"
        if t.startswith("zip mancante"):
            return "manca", "manca la sua zip"
        if t.startswith("ERRORE"):
            return "errore", t[6:].strip()
        return None

    def _verifica(self, e):
        """⛔ prepara.py decide se un convertitore ha scritto qualcosa guardando
        gli header elencati in CONVERTITORI/SEGNAPOSTO. Dove quella cartella
        manca l'elenco e' VUOTO, e ogni convertitore che scrive solo header
        risulta «NON HA PRODOTTO NIENTE» anche quando ha lavorato bene (visto
        su un Mac: 50 righe cosi' su 63).
        Qui si guarda davvero: gli header dell'indice, prima e dopo."""
        dopo = foto_header(self._indice)
        cambiati = sum(1 for p in dopo
                       if dopo[p] is not None and dopo[p] != self._foto.get(p))
        self._foto = dopo
        if e[0] == "vuoto" and cambiati:
            return ("ok", "%d header scritti" % cambiati)
        return e

    def _mostra(self, riga, e):
        """La riga come la si fa VEDERE. prepara.py scrive «NON HA PRODOTTO
        NIENTE» per ogni convertitore che scrive solo header (gli manca
        l'elenco SEGNAPOSTO per accorgersene), e chi legge il registro crede
        che non abbia funzionato niente. Se la verifica sugli header dice che
        ha scritto, qui la riga lo dice."""
        if e and e[0] == "ok" and "NON HA PRODOTTO NIENTE" in riga:
            return riga.replace("NON HA PRODOTTO NIENTE", "ok  (%s)" % e[1])
        return riga

    def _riga(self, riga):
        """Legge una riga intera di prepara.py; torna la riga da mostrare."""
        pulita = riga.split("\r")[-1]
        m = RE_RIGA.match(pulita)
        if m:
            i, n, nome, coda = int(m.group(1)), int(m.group(2)), m.group(3), m.group(4)
            if self._corrente != nome:
                self._corrente = nome
                self.coda.put(("inizio", i, n, nome))
            e = self._esito(coda)
            if e:
                e = self._verifica(e)
                self._conta[e[0]] = self._conta.get(e[0], 0) + 1
                self.coda.put(("fine", nome) + e)
                self._corrente = None
                return self._mostra(riga, e)
            # i video: prepara dice «ore di lavoro», ma su un computer recente
            # sono pochi minuti. Non si promette un tempo che non si conosce.
            return riga.replace("(ore di lavoro, l'avanzamento esce qui sotto)",
                                "(puo' richiedere molto tempo: l'avanzamento esce qui sotto)")
        if self._corrente:
            # i video: l'esito arriva da solo, in fondo all'avanzamento
            e = self._esito(pulita)
            if e:
                e = self._verifica(e)
                self._conta[e[0]] = self._conta.get(e[0], 0) + 1
                self.coda.put(("fine", self._corrente) + e)
                self._corrente = None
                return self._mostra(riga, e)
        for chiave, rx in RE_CONTI:
            c = rx.match(pulita)
            if c:
                self.coda.put(("conto", chiave, int(c.group(1))))
        # il riepilogo: i conti di prepara mettono fra le «zip mancanti» anche
        # chi ha scritto, e «TUTTO RIGENERATO» esce sempre. Si dicono i nostri.
        k = self._conta
        if pulita.startswith("convertitori andati bene"):
            return "convertitori andati bene  %d" % (k.get("ok", 0) + k.get("avvisi", 0))
        if pulita.startswith("zip mancanti"):
            return ("zip mancanti .............. %d\nsenza risultato ........... %d"
                    % (k.get("manca", 0), k.get("vuoto", 0)))
        if pulita.startswith("TUTTO RIGENERATO"):
            pronti = sum(1 for v in self._foto.values() if v is not None)
            if pronti < len(self._foto):
                mancano = sorted(os.path.relpath(p, PROG)
                                 for p, v in self._foto.items() if v is None)
                return ("Header del firmware presenti: %d di %d. Mancano:\n%s"
                        % (pronti, len(self._foto),
                           "\n".join("   " + m for m in mancano)))
        return riga

    def _analizza(self, testo):
        """Riceve l'uscita di prepara.py a pezzi e torna il testo da mostrare.
        Tutto passa subito, tranne l'ESITO di un convertitore: della riga
        `[ 3/63] nome.py   <esito>` si mostra subito l'inizio (cosi' si vede
        chi sta lavorando) e l'esito solo a riga finita, quando lo si e'
        potuto verificare."""
        fuori = []
        self._resto += testo.replace("\r\n", "\n")
        while "\n" in self._resto:
            riga, self._resto = self._resto.split("\n", 1)
            mostra = self._riga(riga)
            fuori.append(mostra[self._gia:] + "\n")
            self._gia = 0
        r = self._resto
        m = RE_PREFISSO.match(r)
        if m:
            visibile = m.group(0)
            if self._corrente != m.group(3):
                self._corrente = m.group(3)
                self.coda.put(("inizio", int(m.group(1)), int(m.group(2)), m.group(3)))
        elif r.startswith("["):
            visibile = ""               # forse e' un `[ 3/63] ...` ancora a meta'
        else:
            visibile = r
        if len(visibile) > self._gia:
            fuori.append(visibile[self._gia:])
            self._gia = len(visibile)
        return "".join(fuori)

    # ---- ffmpeg
    def _scarica(self, url, dove):
        """curl e non urllib: c'e' su Windows 10+ e su ogni Mac, e non
        inciampa nei certificati che a certi Python del Mac mancano."""
        self.scrivi("scarico %s\n" % url)
        self._lancia(["curl", "-L", "--fail", "-sS", "-o", dove, url])
        ultimo = -1
        while self.proc.poll() is None:
            time.sleep(1)
            try:
                mb = os.path.getsize(dove) // (1024 * 1024)
            except OSError:
                mb = 0
            if mb != ultimo:
                ultimo = mb
                self.scrivi("\r   scaricati %d MB" % mb)
        self.scrivi("\n" + (self.proc.stdout.read() or b"").decode("utf-8", "replace"))
        if self.proc.returncode != 0 or not os.path.isfile(dove):
            raise RuntimeError("download non riuscito (manca internet?)")

    def installa_ffmpeg(self):
        self._apri_log("ffmpeg", ["installa ffmpeg"])

        def lavoro():
            os.makedirs(FFDIR, exist_ok=True)
            tmp = os.path.join(FFDIR, "_scarico.zip")
            if WIN:
                self.scrivi("ffmpeg per Windows: circa 190 MB, qualche minuto.\n")
                self._scarica(FFMPEG_WIN, tmp)
                self.scrivi("estraggo ffmpeg.exe e ffprobe.exe...\n")
                with zipfile.ZipFile(tmp) as z:
                    for voce in z.namelist():
                        base = os.path.basename(voce).lower()
                        if base in ("ffmpeg.exe", "ffprobe.exe"):
                            with z.open(voce) as da, \
                                    open(os.path.join(FFDIR, base), "wb") as a:
                                shutil.copyfileobj(da, a)
                os.remove(tmp)
            elif MAC:
                # ⚠️ Niente `brew install`: finirebbe nel sistema. Tutto quello
                # che questo tool scarica resta dentro la cartella del progetto.
                for nome, url in FFMPEG_MAC.items():
                    self._scarica(url, tmp)
                    with zipfile.ZipFile(tmp) as z:
                        z.extract(nome, FFDIR)
                    os.chmod(os.path.join(FFDIR, nome), 0o755)
                    os.remove(tmp)
            else:
                import platform, tarfile
                arch = platform.machine().lower()
                if arch not in FFMPEG_LINUX:
                    raise RuntimeError("niente ffmpeg pronto per %s: installalo dal "
                                       "gestore dei pacchetti" % arch)
                tmp = os.path.join(FFDIR, "_scarico.tar.xz")
                self.scrivi("ffmpeg per Linux: circa 150 MB, qualche minuto.\n")
                self._scarica(FFMPEG_LINUX[arch], tmp)
                self.scrivi("estraggo ffmpeg e ffprobe...\n")
                with tarfile.open(tmp) as t:
                    for voce in t:
                        base = os.path.basename(voce.name)
                        if voce.isfile() and base in ("ffmpeg", "ffprobe"):
                            with t.extractfile(voce) as da, \
                                    open(os.path.join(FFDIR, base), "wb") as a:
                                shutil.copyfileobj(da, a)
                            os.chmod(os.path.join(FFDIR, base), 0o755)
                os.remove(tmp)
            trovato = trova_ffmpeg()
            if not trovato:
                raise RuntimeError("scaricato, ma ffmpeg non si trova")
            # esistere non e' funzionare: lo si fa partire davvero
            p = subprocess.run([trovato, "-version"], capture_output=True,
                               text=True, creationflags=SENZA_FINESTRA)
            if p.returncode != 0:
                raise RuntimeError(
                    "ffmpeg c'e' ma non parte" + (
                        ": su Mac con chip Apple serve Rosetta "
                        "(softwareupdate --install-rosetta)" if MAC else ""))
            self.scrivi("ffmpeg pronto: %s\n%s\n" % (trovato, p.stdout.splitlines()[0]))
            return 0
        self._in_thread("ffmpeg", lavoro)


def argomenti_prepara(senza_video, loghi, filtro, elenco=False):
    a = []
    if elenco:
        a.append("--elenco")
    if senza_video:
        a.append("--senza-video")
    if loghi:
        a.append("--loghi")
    filtro = (filtro or "").strip()
    if filtro and not filtro.startswith("-"):
        a.append(filtro)
    return a


# =============================================================== finestra
# ⚠️ Niente tema disegnato a mano: si usano i controlli NATIVI del sistema
# (vista su Windows, aqua su Mac). Un programma di servizio deve sembrare un
# programma di quel computer, e cosi' segue da solo font, misure e modo scuro.
# Il colore si usa SOLO per dire uno stato; i quattro toni qui sotto si
# leggono sia su fondo chiaro sia su fondo scuro.
VERDE, GIALLO, ROSSO, GRIGIO, BLU = "#2e8b57", "#b7791f", "#c0392b", "#8a8a8a", "#2f6fb5"
COLORE_STATO = {"corso": BLU, "ok": VERDE, "avvisi": GIALLO,
                "manca": GRIGIO, "vuoto": GIALLO, "errore": ROSSO}


def finestra(argomenti):
    import tkinter as tk
    from tkinter import ttk, messagebox
    from tkinter import font as tkfont

    if WIN:
        try:
            import ctypes
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
        except Exception:
            pass

    radice = tk.Tk()
    radice.title("Spinnerino Maker")
    radice.geometry("1020x760")
    radice.minsize(880, 620)

    st = ttk.Style(radice)
    if not WIN and not MAC:
        st.theme_use("clam")    # su Linux il tema di partenza di Tk e' quello degli anni '90
    base = tkfont.nametofont("TkDefaultFont")
    grassetto = base.copy()
    grassetto.configure(weight="bold")
    # TkFixedFont su Windows e' Courier New: largo e d'altri tempi
    mono = tkfont.Font(family="Consolas" if WIN else "Menlo" if MAC else "DejaVu Sans Mono",
                       size=base.actual("size") if WIN else 11 if MAC else 10)
    st.configure("Treeview", rowheight=base.metrics("linespace") + 8)
    st.configure("Nome.TLabel", font=grassetto)
    st.configure("Stato.TLabel", font=grassetto)

    motore = Motore()
    S = {"t0": None, "tot": 0, "fatti": 0, "nome": "", "conti": {}, "esiti": {},
         "ultimo_log": None, "sd_dopo": None}

    corpo = ttk.Frame(radice, padding=(12, 10, 12, 6))
    corpo.pack(fill="both", expand=True)

    # ---- materiale + opzioni
    sopra = ttk.Frame(corpo)
    sopra.pack(fill="x")
    mat = ttk.LabelFrame(sopra, text="Materiale", padding=(10, 6))
    mat.pack(side="left", fill="both", expand=True)
    mat.columnconfigure(2, weight=1)
    righe = {}

    def riga(n, chiave, nome, bottone=None, comando=None):
        punto = ttk.Label(mat, text="●", foreground=GRIGIO)
        punto.grid(row=n, column=0, sticky="w", padx=(0, 6), pady=3)
        ttk.Label(mat, text=nome, style="Nome.TLabel").grid(
            row=n, column=1, sticky="w", padx=(0, 14))
        valore = ttk.Label(mat, text="")
        valore.grid(row=n, column=2, sticky="w")
        b = None
        if bottone:
            b = ttk.Button(mat, text=bottone, command=comando, width=14)
            b.grid(row=n, column=3, sticky="e", padx=(10, 0), pady=1)
        righe[chiave] = (punto, valore)
        return b

    def metti(chiave, testo, colore):
        punto, valore = righe[chiave]
        punto.configure(foreground=colore)
        valore.configure(text=testo)

    riga(0, "rom", "ROM MAME", "Apri cartella", lambda: apri(ROMS))
    riga(1, "video", "Laserdisc", "Apri cartella", lambda: apri(VIDEO))
    b_ffmpeg = riga(2, "ffmpeg", "ffmpeg", "Installa")
    b_manca = riga(3, "header", "Header firmware", "Cosa manca...")
    riga(4, "lib", "Ambiente")

    opz = ttk.LabelFrame(sopra, text="Opzioni", padding=(10, 6))
    opz.pack(side="left", fill="y", padx=(10, 0))
    v_senza_video = tk.BooleanVar(value="--senza-video" in argomenti)
    v_loghi = tk.BooleanVar(value="--loghi" in argomenti)
    v_filtro = tk.StringVar(value=next((a for a in argomenti if not a.startswith("-")), ""))
    ttk.Checkbutton(opz, text="Salta i laserdisc",
                    variable=v_senza_video).pack(anchor="w", pady=2)
    ttk.Checkbutton(opz, text="Rifai anche i loghi",
                    variable=v_loghi).pack(anchor="w", pady=2)
    f_filtro = ttk.Frame(opz)
    f_filtro.pack(anchor="w", pady=(8, 0))
    ttk.Label(f_filtro, text="Solo il gioco:").pack(side="left", padx=(0, 6))
    ttk.Entry(f_filtro, textvariable=v_filtro, width=16).pack(side="left")

    # ---- microSD
    fsd = ttk.LabelFrame(corpo, text="microSD", padding=(10, 6))
    fsd.pack(fill="x", pady=(8, 0))
    fsd.columnconfigure(1, weight=1)
    ttk.Label(fsd, text="Scheda:").grid(row=0, column=0, sticky="w", padx=(0, 8))
    v_sd = tk.StringVar()
    c_sd = ttk.Combobox(fsd, textvariable=v_sd, state="readonly")
    c_sd.grid(row=0, column=1, sticky="ew")
    b_sd_agg = ttk.Button(fsd, text="Cerca", width=10)
    b_sd_agg.grid(row=0, column=2, padx=(8, 0))
    b_sd_ora = ttk.Button(fsd, text="Copia ora", width=12)
    b_sd_ora.grid(row=0, column=3, padx=(6, 0))
    v_sd_dopo = tk.BooleanVar(value=False)
    v_sd_formatta = tk.BooleanVar(value=False)
    f_sd = ttk.Frame(fsd)
    f_sd.grid(row=1, column=1, columnspan=3, sticky="w", pady=(6, 0))
    ttk.Checkbutton(f_sd, text="Copia sulla scheda a fine conversione",
                    variable=v_sd_dopo).pack(side="left")
    ttk.Checkbutton(f_sd, text="Formatta prima in FAT32 (cancella tutto)",
                    variable=v_sd_formatta).pack(side="left", padx=(18, 0))
    schede = []

    # ---- azioni
    az = ttk.Frame(corpo)
    az.pack(fill="x", pady=(10, 0))
    b_via = ttk.Button(az, text="Prepara", width=16, default="active")
    b_via.pack(side="left")
    b_ferma = ttk.Button(az, text="Ferma", width=10, state="disabled")
    b_ferma.pack(side="left", padx=(6, 0))
    b_log = ttk.Button(az, text="Registro", state="disabled")
    b_log.pack(side="right")
    ttk.Button(az, text="Firmware", command=lambda: apri(PROG)).pack(side="right", padx=(0, 6))
    ttk.Button(az, text="File per la SD", command=lambda: apri(SD)).pack(side="right", padx=(0, 6))
    b_err = ttk.Button(az, text="Errori", command=lambda: apri(ERRORI))

    # ---- avanzamento
    av = ttk.Frame(corpo)
    av.pack(fill="x", pady=(10, 2))
    l_stato = ttk.Label(av, text="Pronto.", style="Stato.TLabel")
    l_stato.pack(side="left")
    l_tempo = ttk.Label(av, text="")
    l_tempo.pack(side="right")
    barra = ttk.Progressbar(corpo, mode="determinate", maximum=100)
    barra.pack(fill="x", pady=(0, 8))

    # ---- tabella + registro
    centro = ttk.Panedwindow(corpo, orient="horizontal")
    centro.pack(fill="both", expand=True)
    sx = ttk.Frame(centro)
    dx = ttk.Frame(centro)
    centro.add(sx, weight=1)
    centro.add(dx, weight=1)

    tab = ttk.Treeview(sx, columns=("n", "s", "d"), show="headings", selectmode="browse")
    for col, tit, larg, tira in (("n", "Convertitore", 220, True),
                                 ("s", "Esito", 90, False),
                                 ("d", "Dettaglio", 150, True)):
        tab.heading(col, text=tit, anchor="w")
        tab.column(col, width=larg, stretch=tira, anchor="w")
    for stato, colore in COLORE_STATO.items():
        tab.tag_configure(stato, foreground=colore)
    sb1 = ttk.Scrollbar(sx, orient="vertical", command=tab.yview)
    tab.configure(yscrollcommand=sb1.set)
    sb1.pack(side="right", fill="y")
    tab.pack(side="left", fill="both", expand=True)

    log = tk.Text(dx, font=mono, wrap="word", padx=8, pady=6, state="disabled",
                  borderwidth=1, relief="solid", highlightthickness=0)
    sb2 = ttk.Scrollbar(dx, orient="vertical", command=log.yview)
    log.configure(yscrollcommand=sb2.set)
    sb2.pack(side="right", fill="y")
    log.pack(side="left", fill="both", expand=True)
    mono_grassetto = mono.copy()
    mono_grassetto.configure(weight="bold")
    log.tag_configure("rosso", foreground=ROSSO)
    log.tag_configure("giallo", foreground=GIALLO)
    log.tag_configure("verde", foreground=VERDE)
    log.tag_configure("titolo", font=mono_grassetto)
    log.tag_configure("spento", foreground=GRIGIO)

    # ---- barra di stato
    ttk.Separator(radice, orient="horizontal").pack(fill="x")
    piede = ttk.Label(radice, padding=(12, 3), foreground=GRIGIO,
                      text=RADICE)
    piede.pack(fill="x")

    # ---- registro a colori
    def colore_riga(r):
        t = r.strip()
        if not t:
            return None
        if "ERRORE" in t or "Traceback" in t or t.startswith("!!") or "fallito" in t:
            return "rosso"
        if t.startswith(("manca  ", "da rigenerare", "ignorata", "zip in ROMS che")):
            return "giallo"
        if "zip mancante" in t or "NON HA PRODOTTO" in t or t.startswith(("saltati", "NON lanciato", "salto:")):
            return "giallo"
        if re.search(r"\]\s+\S+\s+ok\s|^ok\s|TUTTO RIGENERATO|pronto|pronta", t):
            return "verde"
        if t.startswith("===") or t.isupper():
            return "titolo"
        if t.startswith(("scaricati", "(")):
            return "spento"
        return None

    def scrivi_log(testo):
        in_fondo = log.yview()[1] > 0.98
        log.configure(state="normal")
        for pezzo in re.split(r"(\r\n|\n|\r)", testo):
            if pezzo in ("\n", "\r\n"):
                tag = colore_riga(log.get("end-1c linestart", "end-1c"))
                if tag:
                    log.tag_add(tag, "end-1c linestart", "end-1c")
                log.insert("end", "\n")
            elif pezzo == "\r":
                log.delete("end-1c linestart", "end-1c")
            elif pezzo:
                log.insert("end", pezzo)
        n = int(log.index("end-1c").split(".")[0])
        if n > 6000:
            log.delete("1.0", "%d.0" % (n - 5000))
        log.configure(state="disabled")
        if in_fondo:
            log.see("end")

    def svuota_log():
        log.configure(state="normal")
        log.delete("1.0", "end")
        log.configure(state="disabled")

    # ---- stato
    def aggiorna(fresco=False):
        s = stato_ambiente(fresco)
        if s["rom_attese"]:
            metti("rom", "%d di %d zip in ROMS" % (s["rom"], s["rom_attese"]),
                  VERDE if not s["rom_mancanti"] else GIALLO if s["rom"] else ROSSO)
        else:
            metti("rom", "%d zip in ROMS" % s["rom"], VERDE if s["rom"] else ROSSO)
        metti("video", "%d di %d pacchetti Daphne in VIDEO" % (s["video"], s["video_attesi"])
              if s["video"] else "nessun pacchetto in VIDEO (facoltativi)",
              VERDE if s["video"] else GRIGIO)
        serve = s["video"] > 0
        metti("ffmpeg", "presente" if s["ffmpeg"] else
              ("manca, e serve per i laserdisc" if serve else "manca (serve solo ai laserdisc)"),
              VERDE if s["ffmpeg"] else (ROSSO if serve else GRIGIO))
        mancano = s["h_totale"] - s["h_veri"]
        metti("header", "%d di %d pronti" % (s["h_veri"], s["h_totale"])
              + ("" if not mancano else ", %d da rigenerare" % mancano),
              VERDE if s["h_totale"] and not mancano else GIALLO if s["h_veri"] else ROSSO)
        lib = [n for n, c in (("pillow", s["pillow"]), ("numpy", s["numpy"])) if not c]
        if s["versione"] < (3, 12):
            # con 3.10 e precedenti i convertitori che usano pathlib muoiono
            metti("lib", "Python %s troppo vecchio: chiudi e rilancia setup"
                  % s["python"], ROSSO)
        else:
            metti("lib", "Python %s, pillow e numpy" % s["python"] if not lib else
                  "manca %s: lo installa Prepara" % " e ".join(lib),
                  VERDE if not lib else GIALLO)
        if not motore.attivo():
            b_ffmpeg.configure(state="disabled" if s["ffmpeg"] else "normal")
        if os.path.isfile(ERRORI) and S["conti"].get("errore"):
            b_err.pack(side="right", padx=(0, 6))
        else:
            b_err.pack_forget()
        return s

    def occupato(si):
        for b in (b_via, b_manca, b_ffmpeg, b_sd_agg, b_sd_ora):
            b.configure(state="disabled" if si else "normal")
        c_sd.configure(state="disabled" if si else "readonly")
        b_ferma.configure(state="normal" if si else "disabled")

    # ---- microSD
    def cerca_sd():
        prima = scheda_scelta()
        schede[:] = elenca_sd()
        c_sd.configure(values=[descrivi_sd(x) for x in schede])
        tenuta = [i for i, x in enumerate(schede) if prima and x["id"] == prima["id"]]
        if tenuta:
            c_sd.current(tenuta[0])
        elif len(schede) == 1:
            c_sd.current(0)             # ce n'e' una sola: e' lei
        else:
            v_sd.set("" if schede else "nessuna scheda inserita")

    def scheda_scelta():
        i = c_sd.current()
        return schede[i] if 0 <= i < len(schede) else None

    def conferma_sd(formatta):
        """Torna la scheda, o None se non si fa niente. Le domande si fanno
        TUTTE qui, prima di partire: a conversione finita (magari ore dopo, a
        computer incustodito) non deve restare una finestra ad aspettare."""
        cerca_sd()
        s = scheda_scelta()
        if not s:
            messagebox.showwarning(
                "microSD", "Scegli la microSD dall'elenco.\n"
                "Se non compare, inseriscila e premi \"Cerca\".")
            return None
        if formatta:
            if s["totale"] > MAX_FORMATTA:
                messagebox.showerror(
                    "Troppo grande", "%s\n\nE' piu' grande di %s: per sicurezza "
                    "non la formatto. Sei sicuro che sia la microSD?"
                    % (descrivi_sd(s), gb(MAX_FORMATTA)))
                return None
            try:
                dentro = len(os.listdir(s["radice"]))
            except OSError:
                dentro = 0
            if not messagebox.askyesno(
                    "Formattare la scheda?",
                    "Sto per CANCELLARE TUTTO quello che c'e' su:\n\n"
                    "      %s\n\n"
                    "Adesso contiene %d fra file e cartelle.\n"
                    "Non si torna indietro.\n\nE' la scheda giusta?"
                    % (descrivi_sd(s), dentro),
                    icon="warning", default="no"):
                return None
        elif s["fs"] not in ("FAT32", "?"):
            if not messagebox.askyesno(
                    "Non e' FAT32",
                    "La scheda e' in %s, ma lo Spinnerino la vuole in FAT32:\n"
                    "cosi' com'e' non la leggera'.\n\nCopio lo stesso?\n"
                    "(per formattarla spunta \"Formatta prima\")" % s["fs"],
                    default="no"):
                return None
        return s

    def avvia_sd(s, formatta):
        S.update(t0=time.time(), sd_dopo=None)
        barra.stop()
        barra.configure(mode="determinate", value=0)
        occupato(True)
        l_stato.configure(text="Formatto la microSD..." if formatta
                          else "Copio sulla microSD...", foreground="")
        motore.copia_sd(s, formatta)
        S["ultimo_log"] = motore.file_log
        b_log.configure(state="normal")

    def copia_ora():
        if motore.attivo():
            return
        if not file_per_sd():
            messagebox.showinfo(
                "Niente da copiare", "DA COPIARE SU SD e' vuota:\n"
                "prima va fatta la conversione con Prepara.")
            return
        s = conferma_sd(v_sd_formatta.get())
        if s:
            avvia_sd(s, v_sd_formatta.get())

    def parti(tipo):
        if motore.attivo():
            return
        s = aggiorna()
        S["sd_dopo"] = None
        if tipo == "prepara":
            if v_sd_dopo.get():
                scheda = conferma_sd(v_sd_formatta.get())
                if not scheda:
                    return
                S["sd_dopo"] = (scheda, v_sd_formatta.get())
            if not s["rom"] and not messagebox.askyesno(
                    "ROMS e' vuota",
                    "In ROMS non c'e' nessuna zip: senza ROM non si rigenera "
                    "niente.\n\nLancio lo stesso?"):
                apri(ROMS)
                return
            if s["video"] and not v_senza_video.get():
                if not s["ffmpeg"]:
                    messagebox.showwarning(
                        "Manca ffmpeg",
                        "Ci sono pacchetti Daphne in VIDEO ma ffmpeg non c'e'.\n"
                        "Premi \"Installa\" sulla riga ffmpeg, oppure spunta "
                        "\"Salta i laserdisc\".")
                    return
        for r in tab.get_children():
            tab.delete(r)
        svuota_log()
        S.update(t0=time.time(), tot=0, fatti=0, nome="", conti={}, esiti={})
        barra.configure(mode="indeterminate")
        barra.start(12)
        occupato(True)
        if tipo == "ffmpeg":
            l_stato.configure(text="Installo ffmpeg...", foreground="")
            motore.installa_ffmpeg()
        else:
            l_stato.configure(text="Controllo l'ambiente...", foreground="")
            motore.prepara(argomenti_prepara(v_senza_video.get(), v_loghi.get(),
                                             v_filtro.get()))
        S["ultimo_log"] = motore.file_log
        b_log.configure(state="normal")
        piede.configure(text="Registro: %s" % motore.file_log)

    def cosa_manca():
        """Risposta immediata, dall'indice degli header: `prepara --elenco`
        conta sui segnaposto, e dove quelli non ci sono risponde sempre zero."""
        if motore.attivo():
            return
        s = aggiorna()
        svuota_log()
        r = ["COSA MANCA", ""]
        r.append("ROM in ROMS: %d su %d" % (s["rom"], s["rom_attese"]))
        for i in range(0, len(s["rom_mancanti"]), 4):
            r.append("   manca  " + "  ".join(s["rom_mancanti"][i:i + 4]))
        if s["rom_sconosciute"]:
            r.append("")
            r.append("zip in ROMS che il firmware NON conosce (verranno ignorate):")
            for i in range(0, len(s["rom_sconosciute"]), 4):
                r.append("   ignorata  " + "  ".join(s["rom_sconosciute"][i:i + 4]))
        r.append("")
        r.append("Laserdisc in VIDEO: %d su %d  (facoltativi)" % (s["video"], s["video_attesi"]))
        for n in s["video_mancanti"]:
            r.append("   manca  " + n)
        r.append("")
        r.append("ffmpeg: %s" % (s["ffmpeg"] or "manca (serve solo ai laserdisc)"))
        r.append("librerie: pillow %s, numpy %s" % tuple(
            "ok" if x else "manca" for x in (s["pillow"], s["numpy"])))
        r.append("")
        r.append("Header del firmware: %d veri su %d" % (s["h_veri"], s["h_totale"]))
        gruppi = {}
        for rel in s["h_mancanti"]:
            gruppi.setdefault(os.path.dirname(rel) or "(radice)", []).append(os.path.basename(rel))
        for g in sorted(gruppi):
            r.append("   da rigenerare  %-28s %d file" % (g, len(gruppi[g])))
            for n in sorted(gruppi[g]):
                r.append("        " + n)
        if not s["h_mancanti"] and s["h_totale"]:
            r.append("   pronto: apri SPINNERINO.ino e compila")
        scrivi_log("\n".join(r) + "\n")
        log.see("1.0")
        l_stato.configure(
            text="Mancano %d zip e %d header." % (len(s["rom_mancanti"]), len(s["h_mancanti"])),
            foreground=VERDE if not s["h_mancanti"] else GIALLO)

    def ferma():
        if motore.attivo() and messagebox.askyesno(
                "Fermo?", "Interrompo il lavoro in corso?\n"
                "Si puo' rilanciare quando vuoi."):
            motore.ferma()

    def chiuso(tipo, codice):
        barra.stop()
        barra.configure(mode="determinate")
        occupato(False)
        S["t0"] = None
        c = S["conti"]
        s = aggiorna(fresco=True)      # prepara puo' aver installato una libreria
        if motore.fermato:
            barra.configure(value=0)
            l_stato.configure(text="Fermato.", foreground=GIALLO)
        elif tipo == "sd":
            barra.configure(value=100 if codice == 0 else 0)
            l_stato.configure(
                text="microSD pronta: espellila in modo sicuro e mettila nello "
                     "Spinnerino." if codice == 0 else
                     "Copia sulla microSD non riuscita: guarda il registro.",
                foreground=VERDE if codice == 0 else ROSSO)
            cerca_sd()
            radice.bell()
        elif tipo == "ffmpeg":
            barra.configure(value=100 if s["ffmpeg"] else 0)
            l_stato.configure(text="ffmpeg installato." if s["ffmpeg"] else
                              "ffmpeg non installato: guarda il registro.",
                              foreground=VERDE if s["ffmpeg"] else ROSSO)
        elif codice != 0:
            barra.configure(value=0)
            l_stato.configure(text="prepara.py si e' fermato (codice %s): "
                              "guarda il registro." % codice, foreground=ROSSO)
        elif not S["tot"]:
            barra.configure(value=100)
            l_stato.configure(text="Finito: nessun convertitore da lanciare.",
                              foreground="")
        else:
            barra.configure(value=100)
            e = list(S["esiti"].values())
            # i conti sono i NOSTRI: quelli di prepara mettono fra le «zip
            # mancanti» anche i convertitori che hanno scritto (vedi _verifica)
            ok = e.count("ok") + e.count("avvisi")
            manca = e.count("manca") + e.count("vuoto")
            err = e.count("errore")
            l_stato.configure(
                text="Finito: %d convertiti, %d senza zip, %d errori. "
                     "Header pronti %d di %d." % (
                         ok, manca, err, s["h_veri"], s["h_totale"]),
                foreground=ROSSO if err else VERDE)
            if not s["ha_segnaposto"] and s["h_veri"] < s["h_totale"]:
                scrivi_log("\n!! Manca CONVERTITORI/SEGNAPOSTO: gli header non "
                           "rigenerati NON hanno il segnaposto,\n   quindi il "
                           "progetto compila solo quando ci sono tutti.\n")
            radice.bell()
        # ---- conversione finita bene: si passa da soli alla scheda
        dopo = S.get("sd_dopo")
        if tipo == "prepara" and dopo and not motore.fermato and codice == 0 and S["tot"]:
            scrivi_log("\n" + "=" * 70 + "\nMICROSD\n" + "=" * 70 + "\n")
            avvia_sd(*dopo)

    def pompa():
        try:
            for _ in range(400):
                ev = motore.coda.get_nowait()
                if ev[0] == "log":
                    scrivi_log(ev[1])
                elif ev[0] == "inizio":
                    _t, i, n, nome = ev
                    if not S["tot"]:
                        barra.stop()
                        barra.configure(mode="determinate")
                    S.update(tot=n, fatti=i - 1, nome=nome)
                    barra.configure(value=100.0 * (i - 1) / n)
                    l_stato.configure(text="%d di %d   %s" % (i, n, nome))
                    if not tab.exists(nome):
                        tab.insert("", "end", iid=nome, tags=("corso",),
                                   values=(nome, "in corso", ""))
                    tab.see(nome)
                elif ev[0] == "fine":
                    _t, nome, stato, dett = ev
                    S["esiti"][nome] = stato
                    S["fatti"] += 1
                    if S["tot"]:
                        barra.configure(value=100.0 * S["fatti"] / S["tot"])
                    if tab.exists(nome):
                        tab.item(nome, tags=(stato,),
                                 values=(nome, ETICHETTE[stato], dett))
                elif ev[0] == "conto":
                    S["conti"][ev[1]] = ev[2]
                elif ev[0] == "avanza":
                    _t, fatti, totale, nome = ev
                    barra.configure(value=100.0 * fatti / max(totale, 1))
                    l_stato.configure(text="microSD   %s di %s   %s" % (
                        gb(fatti), gb(totale), nome))
                elif ev[0] == "chiuso":
                    chiuso(ev[1], ev[2])
        except queue.Empty:
            pass
        if S["t0"]:
            dt = int(time.time() - S["t0"])
            l_tempo.configure(text="%02d:%02d:%02d" % (dt // 3600, dt % 3600 // 60, dt % 60))
        radice.after(80, pompa)

    def esci():
        if motore.attivo():
            if not messagebox.askyesno("Esco?", "C'e' un lavoro in corso: lo "
                                       "interrompo ed esco?"):
                return
            motore.ferma()
        radice.destroy()

    b_via.configure(command=lambda: parti("prepara"))
    b_manca.configure(command=cosa_manca)
    b_ffmpeg.configure(command=lambda: parti("ffmpeg"))
    b_ferma.configure(command=ferma)
    b_sd_agg.configure(command=cerca_sd)
    b_sd_ora.configure(command=copia_ora)
    b_log.configure(command=lambda: S["ultimo_log"] and apri(S["ultimo_log"]))
    radice.protocol("WM_DELETE_WINDOW", esci)
    radice.bind("<FocusIn>", lambda e: e.widget is radice and not motore.attivo() and aggiorna())
    radice.bind("<F5>", lambda _e: not motore.attivo() and (aggiorna(), cerca_sd()))

    cerca_sd()
    s = aggiorna()
    if not s["video"]:
        v_senza_video.set(True)     # niente pacchetti: inutile far partire i video
    scrivi_log("Metti le tue zip MAME in ROMS, poi premi Prepara.\n"
               "I pacchetti Daphne (facoltativi) vanno in VIDEO.\n"
               "F5 rilegge cartelle e schede.\n")
    radice.after(80, pompa)
    # i pesi del Panedwindow non bastano: la tabella chiede poco e resta stretta
    radice.after(250, lambda: centro.sashpos(0, int(centro.winfo_width() * 0.47)))
    radice.lift()
    radice.attributes("-topmost", True)
    radice.after(400, lambda: radice.attributes("-topmost", False))
    radice.mainloop()


# ============================================================== terminale
def terminale(argomenti):
    if WIN:
        os.system("")               # accende i colori ANSI nella console
    colori = sys.stdout.isatty()
    try:                            # una console cp850 non ha tutti i caratteri
        sys.stdout.reconfigure(errors="replace")
    except Exception:
        pass

    def c(codice, t):
        return "\033[%sm%s\033[0m" % (codice, t) if colori else t

    s = stato_ambiente()
    print(c("1;96", "\n  SPINNERINO PLUS - preparazione\n"))
    for nome, valore, bene in (
            ("ROM", "%d / %d zip in ROMS" % (s["rom"], s["rom_attese"]), s["rom"] > 0),
            ("LASERDISC", "%d / %d pacchetti in VIDEO" % (s["video"], s["video_attesi"]), s["video"] > 0),
            ("HEADER", "%d veri su %d" % (s["h_veri"], s["h_totale"]), s["h_veri"] == s["h_totale"]),
            ("LIBRERIE", "pillow %s, numpy %s" % tuple(
                "ok" if x else "manca (lo installa prepara)" for x in (s["pillow"], s["numpy"])),
             s["pillow"] and s["numpy"]),
            ("FFMPEG", s["ffmpeg"] or "manca (serve solo ai laserdisc)", bool(s["ffmpeg"]))):
        print("  %s %-10s %s" % (c("92" if bene else "93", "*"), nome, valore))
    print("")

    motore = Motore()
    if "--ffmpeg" in argomenti:
        motore.installa_ffmpeg()
    else:
        if not s["video"] and "--senza-video" not in argomenti:
            argomenti = argomenti + ["--senza-video"]
        motore.prepara(argomenti)
    riga, codice = "", 0

    def stampa(r, fine="\n"):
        t = r.strip()
        if "ERRORE" in t or "Traceback" in t or t.startswith("!!"):
            r = c("91", r)
        elif "zip mancante" in t or "NON HA PRODOTTO" in t:
            r = c("93", r)
        elif re.search(r"\sok\s+[\d.]+s", t) or "TUTTO RIGENERATO" in t:
            r = c("92", r)
        elif t.startswith("===") or (t.isupper() and len(t) > 3):
            r = c("1;96", r)
        sys.stdout.write(r + fine)

    try:
        while True:
            ev = motore.coda.get()
            if ev[0] == "log":
                riga += ev[1].replace("\r\n", "\n")
                pezzi = riga.split("\n")
                riga = pezzi.pop()
                for p in pezzi:
                    stampa(p.split("\r")[-1])
                if RE_INIZIO.match(riga):
                    sys.stdout.write(riga)      # il convertitore in corso
                    riga = ""
                sys.stdout.flush()
            elif ev[0] == "chiuso":
                codice = ev[2]
                break
    except KeyboardInterrupt:
        motore.ferma()
        print(c("93", "\n  fermato."))
        codice = 1
    if riga:
        stampa(riga)
    print(c("90", "\n  log completo: %s" % motore.file_log))
    return codice


def main():
    argomenti = sys.argv[1:]
    if not os.path.isfile(PREPARA):
        sys.exit("Non trovo prepara.py ne' in %s ne' nella cartella sopra." % QUI)
    if "--terminale" in argomenti:
        argomenti = [a for a in argomenti if a != "--terminale"]
        sys.exit(terminale(argomenti))
    try:
        import tkinter
        tkinter.Tk().destroy()      # c'e' il modulo E si apre davvero uno schermo
    except Exception as e:
        print("Niente finestra (%s): vado a terminale." % str(e)[:80])
        sys.exit(terminale(argomenti))
    try:
        finestra(argomenti)
    except Exception:
        # con pythonw non c'e' una console dove leggere l'errore: va su file
        import traceback
        os.makedirs(LOGDIR, exist_ok=True)
        with open(os.path.join(LOGDIR, "errore_avvio.txt"), "w", encoding="utf-8") as f:
            traceback.print_exc(file=f)
        raise


if __name__ == "__main__":
    main()
