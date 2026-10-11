#!/bin/bash
# ===========================================================================
#  SPINNERINO PLUS - setup.sh   (Mac e Linux)        gemello Windows: setup.bat
#
#  Prepara un Python che vive TUTTO dentro questa cartella e apre
#  spinnerino_tool.py:
#    .venv/     l'ambiente con pillow e numpy (si fa sempre)
#    .python/   un Python portatile, SOLO se sul computer non ce n'e' uno adatto
#  Niente installatori, niente Homebrew, niente apt, niente password.
#  Cancellata questa cartella, sul computer non resta niente.
#
#  COME SI LANCIA: apri il Terminale, scrivi   bash   poi uno spazio,
#  trascina questo file dentro la finestra e premi Invio.
#  (cosi' funziona anche se lo zip ha perso il permesso di esecuzione)
#
#  DUE PYTHON, DUE MESTIERI. Non e' detto che siano lo stesso:
#    il MOTORE    fa le conversioni: deve essere 3.12 o successivo
#    la FINESTRA  disegna l'interfaccia: basta 3.8, ma il suo Tk deve FUNZIONARE
#  Su un Mac vero il Python di sistema (3.10) apriva bene la finestra ma faceva
#  fallire i convertitori, e quello portatile (3.12) convertiva bene ma andava
#  in "segmentation fault" aprendo la finestra. Quindi si scelgono separati.
# ===========================================================================
cd "$(dirname "$0")" || exit 1
QUI="$(pwd)"
TOOL="spinnerino_tool.py"
VPY="$QUI/.venv/bin/python"
MIO="$QUI/.python/bin/python3"
SISTEMA="$(uname -s)"

PYVER="3.12.15"
PYREL="20261009"
# pip non deve lasciare la sua cache nella cartella dell'utente
export PIP_NO_CACHE_DIR=1
export PIP_DISABLE_PIP_VERSION_CHECK=1

# Il motore: Python 3.12 o successivo. Con 3.10 e precedenti i convertitori che
# usano pathlib muoiono ("stat() takes exactly 1 positional argument"), perche'
# _ambiente.py aggancia os.stat e il vecchio pathlib ci inciampa.
va_bene() {
  "$1" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 12) else 1)' >/dev/null 2>&1
}

# La finestra: non basta che tkinter si importi, bisogna APRIRNE una. La prova
# gira in un processo a parte: se quel Tk e' rotto muore lui, non questo script.
apre_finestra() {
  ( "$1" -c '
import sys, tkinter
if sys.version_info < (3, 8) or tkinter.TkVersion < 8.6:
    sys.exit(1)
r = tkinter.Tk()
r.withdraw()
r.update()
r.destroy()
' ) >/dev/null 2>&1
}

# ATTENZIONE: su un Mac senza gli strumenti di Xcode, /usr/bin/python3 e' un
# FINTO python: non esegue niente e fa comparire la richiesta di installare
# 2 GB di strumenti da sviluppatore. Non lo si prova nemmeno.
finto_di_apple() {
  [ "$SISTEMA" = "Darwin" ] || return 1
  case "$1" in /usr/bin/python3) ;; *) return 1 ;; esac
  xcode-select -p >/dev/null 2>&1 && return 1
  return 0
}

# I Python del computer (non il nostro portatile), uno per riga.
di_sistema() {
  for c in "$(command -v python3 2>/dev/null)" \
           /opt/homebrew/bin/python3 \
           /usr/local/bin/python3 \
           /Library/Frameworks/Python.framework/Versions/Current/bin/python3 \
           /usr/bin/python3; do
    [ -n "$c" ] && [ -x "$c" ] || continue
    finto_di_apple "$c" && continue
    echo "$c"
  done
}

scarica() {       # scarica URL FILE: curl c'e' su ogni Mac, su Linux a volte solo wget
  if command -v curl >/dev/null 2>&1; then
    curl -L --fail -o "$2" "$1"
  elif command -v wget >/dev/null 2>&1; then
    wget -O "$2" "$1"
  else
    echo " Non trovo ne' curl ne' wget."
    return 1
  fi
}

scarica_python() {
  case "$SISTEMA-$(uname -m)" in
    Darwin-arm64)                TRIPLA="aarch64-apple-darwin" ;;
    Darwin-*)                    TRIPLA="x86_64-apple-darwin" ;;
    Linux-x86_64)                TRIPLA="x86_64-unknown-linux-gnu" ;;
    Linux-aarch64|Linux-arm64)   TRIPLA="aarch64-unknown-linux-gnu" ;;
    *) echo " Niente Python portatile per $SISTEMA $(uname -m)."; return 1 ;;
  esac
  URL="https://github.com/astral-sh/python-build-standalone/releases/download/${PYREL}/cpython-${PYVER}%2B${PYREL}-${TRIPLA}-install_only.tar.gz"
  echo ""
  echo " =================================================================="
  echo "  Su questo computer manca Python 3.12 o successivo. Ne scarico"
  echo "  uno portatile (25-65 MB) e lo metto QUI DENTRO, nella cartella"
  echo "  .python: non tocca il sistema e non chiede password."
  echo " =================================================================="
  echo ""
  TMP="$QUI/.python_scarico.tar.gz"
  if scarica "$URL" "$TMP"; then
    rm -rf "$QUI/.python"
    mkdir -p "$QUI/.python"
    if tar -xzf "$TMP" -C "$QUI/.python" --strip-components=1; then
      echo " Python $PYVER pronto in .python"
    else
      echo " Non riesco a scompattarlo."
      rm -rf "$QUI/.python"
    fi
  else
    echo " Download non riuscito (manca internet?)."
  fi
  rm -f "$TMP"
  va_bene "$MIO"
}

crea_venv() {     # crea_venv PYTHON
  rm -rf "$QUI/.venv"
  "$1" -m venv "$QUI/.venv" >/dev/null 2>&1 && va_bene "$VPY" \
    && "$VPY" -m pip --version >/dev/null 2>&1
}

# --- 1. L'ambiente c'e' gia' e va bene? -------------------------------------
# Un .venv ricorda il percorso ASSOLUTO di dove e' nato: se la cartella e'
# stata spostata o rinominata non parte piu', e va rifatto. Lo stesso se e'
# nato con un Python troppo vecchio.
if [ -e "$VPY" ] && ! va_bene "$VPY"; then
  echo " L'ambiente .venv non va piu' bene (cartella spostata, o Python troppo vecchio): lo rifaccio."
  rm -rf "$QUI/.venv"
fi

if [ ! -x "$VPY" ]; then
  # --- 2. Il motore: un 3.12+ del computer, altrimenti quello portatile ------
  PY=""
  if va_bene "$MIO"; then
    PY="$MIO"
  else
    while IFS= read -r c; do
      if va_bene "$c"; then PY="$c"; break; fi
    done <<EOF
$(di_sistema)
EOF
  fi
  if [ -z "$PY" ]; then
    scarica_python && PY="$MIO"
  fi
  if [ -z "$PY" ]; then
    echo ""
    echo " Non ho trovato Python 3.12+ e non sono riuscito a scaricarlo."
    echo " Riprova con internet attivo, poi:   bash setup.sh"
    exit 1
  fi

  # --- 3. L'ambiente del progetto -------------------------------------------
  # Su Debian e Ubuntu il Python di sistema spesso NON sa fare un venv (manca
  # il pacchetto python3-venv). Invece di chiedere un `sudo apt install`, si
  # ripiega sul Python portatile, che sa farlo da solo.
  echo " Creo l'ambiente .venv ..."
  if ! crea_venv "$PY"; then
    if [ "$PY" != "$MIO" ] && { va_bene "$MIO" || scarica_python; }; then
      echo " Con quel Python non riesco: uso quello portatile."
      crea_venv "$MIO"
    fi
  fi
  if ! va_bene "$VPY"; then
    rm -rf "$QUI/.venv"
    echo ""
    echo " Non sono riuscito a creare l'ambiente .venv."
    exit 1
  fi
fi

# --- 4. Le due librerie dei convertitori ------------------------------------
if ! "$VPY" -c 'import PIL, numpy' >/dev/null 2>&1; then
  echo " Installo pillow e numpy dentro .venv (serve internet)..."
  "$VPY" -m pip install -q pillow numpy \
    || echo " Non ci sono riuscito adesso: ci riprova il tool quando premi Prepara."
fi

# --- 5. Apro il tool --------------------------------------------------------
# Le conversioni le fa SEMPRE il Python di .venv, chiunque disegni la finestra.
export SPINNERINO_PYTHON="$VPY"

# `bash setup.sh --terminale` salta la finestra e lavora qui dentro.
if [ "$1" = "--terminale" ]; then
  exec "$VPY" "$TOOL" "$@"
fi
# Su Linux senza schermo grafico (ssh, server) la finestra non puo' aprirsi.
if [ "$SISTEMA" = "Linux" ] && [ -z "$DISPLAY" ] && [ -z "$WAYLAND_DISPLAY" ]; then
  echo " Nessuno schermo grafico: lavoro qui nel terminale."
  exec "$VPY" "$TOOL" --terminale "$@"
fi

# Chi disegna la finestra: PRIMA i Python del computer, che hanno il Tk fatto
# per quel sistema; quello di .venv solo se non ce n'e' uno che la apra.
# (l'ordine conta: provare per primo un Tk rotto su Mac fa comparire la
#  finestra di sistema "Python si e' chiuso inaspettatamente")
PYF=""
while IFS= read -r c; do
  if apre_finestra "$c"; then PYF="$c"; break; fi
done <<EOF
$(di_sistema)
EOF
if [ -z "$PYF" ] && apre_finestra "$VPY"; then
  PYF="$VPY"
fi

if [ -n "$PYF" ]; then
  echo " Apro Spinnerino Maker... (lascia aperta questa finestra)"
  exec "$PYF" "$TOOL" "$@"
fi
echo " Nessun Python di questo computer sa aprire una finestra: lavoro qui nel terminale."
exec "$VPY" "$TOOL" --terminale "$@"
