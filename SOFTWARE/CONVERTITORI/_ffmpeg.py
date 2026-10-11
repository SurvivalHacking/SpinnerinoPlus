# ============================================================================
# _ffmpeg.py — trova ffmpeg senza percorsi fissi.
#
# Gli script dei video Daphne avevano dentro il percorso del PC di chi li ha
# scritti (`C:/Users/Tecnico/Desktop/ffmpeg/...`): su un altro computer non
# esiste e la conversione muore. Qui si cerca, in quest'ordine:
#   1. una cartella FFMPEG\ dentro il pacchetto (anche in sottocartelle)
#   2. ffmpeg nel PATH di sistema
#   3. le posizioni tipiche del sistema
#
# ⛔⛔⛔ SUL MAC QUESTO FILE NON FUNZIONAVA (trovato il 2026-09-12).
# Nel pacchetto, `FFMPEG\` contiene i soli **ffmpeg.exe / ffprobe.exe di
# WINDOWS** (100 MB l'uno). La ricerca accettava `nome + ".exe"` **senza mai
# guardare la piattaforma** (verificato: nessun `os.name`, `sys.platform` ne'
# `platform.system()` in tutto il file), quindi su un Mac restituiva
# `.../FFMPEG/ffmpeg.exe` — un file che ESISTE e non si puo' ESEGUIRE — e il
# ramo `shutil.which()`, cioe' quello che avrebbe trovato l'ffmpeg di brew,
# non veniva mai raggiunto. L'amico avrebbe visto un
# `OSError: [Errno 8] Exec format error`, che non nomina la causa, DOPO aver
# installato ffmpeg come gli dice il LEGGIMI.
# E' [[feedback_esistere_non_e_funzionare]]: il controllo non e' «c'e'?» ma
# «fa quello che deve?» — qui basta chiedersi se e' eseguibile su QUESTO
# sistema.
# ============================================================================
import os, shutil, sys

QUI       = os.path.dirname(os.path.abspath(__file__))
PACCHETTO = os.path.dirname(QUI)

_WINDOWS = (os.name == "nt")

# Dove si trova ffmpeg quando non e' nel PATH. Su Mac sono le due cartelle di
# Homebrew (Apple Silicon e Intel); su Linux il posto classico.
_POSTI_TIPICI = ((r"C:\Program Files\ffmpeg\bin", r"C:\ffmpeg\bin") if _WINDOWS
                 else ("/opt/homebrew/bin", "/usr/local/bin", "/usr/bin"))


def _va_bene(percorso, nome):
    """Un candidato vale solo se e' eseguibile SU QUESTO sistema."""
    base = os.path.basename(percorso).lower()
    if _WINDOWS:
        return base in (nome + ".exe", nome)
    # ⛔ Su Mac/Linux un .exe di Windows non si esegue: va SCARTATO, o si
    # prende lui e non si arriva mai a quello vero.
    if base != nome:
        return False
    return os.access(percorso, os.X_OK)


def _cerca(nome):
    dentro = os.path.join(PACCHETTO, "FFMPEG")
    if os.path.isdir(dentro):
        for r, _d, fs in os.walk(dentro):
            for f in fs:
                p = os.path.join(r, f)
                if _va_bene(p, nome):
                    return p
    p = shutil.which(nome)
    if p:
        return p
    for base in _POSTI_TIPICI:
        p = os.path.join(base, nome + ".exe" if _WINDOWS else nome)
        if os.path.isfile(p) and _va_bene(p, nome):
            return p
    return None


def ffmpeg():
    p = _cerca("ffmpeg")
    if not p:
        if _WINDOWS:
            raise SystemExit(
                "ERRORE: ffmpeg non trovato.\n"
                "  Scaricalo da https://ffmpeg.org e mettilo nel PATH,\n"
                "  oppure copia la sua cartella dentro  FFMPEG\\  nel pacchetto.")
        raise SystemExit(
            "ERRORE: ffmpeg non trovato.\n"
            "  Su Mac installalo con:   brew install ffmpeg\n"
            "  (gli ffmpeg.exe dentro FFMPEG\\ sono di Windows e qui non\n"
            "   servono a niente).\n"
            "  Su Linux:   sudo apt install ffmpeg")
    return p


def ffprobe():
    p = _cerca("ffprobe")
    if p:
        return p
    # ⚠️ NON `percorso.replace("ffmpeg", "ffprobe")`: senza limite cambia TUTTE
    # le occorrenze, e il percorso di Homebrew la parola ce l'ha due volte
    # (`/opt/homebrew/Cellar/ffmpeg/7.1/bin/ffmpeg`) -> viene fuori una
    # cartella che non esiste. Si tocca il solo NOME DEL FILE.
    intero = ffmpeg()
    cartella, base = os.path.split(intero)
    return os.path.join(cartella, base.replace("ffmpeg", "ffprobe", 1))
