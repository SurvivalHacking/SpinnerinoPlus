@echo off
setlocal
cd /d "%~dp0"
title SPINNERINO PLUS - setup

rem ===========================================================================
rem  SPINNERINO PLUS - setup.bat   (Windows)        gemello Mac: setup.sh
rem
rem  Prepara un Python che vive TUTTO dentro questa cartella e apre
rem  spinnerino_tool.py:
rem    .venv\     l'ambiente con pillow e numpy (si fa sempre)
rem    .python\   un Python portatile, SOLO se sul PC non ce n'e' uno
rem  Niente installatori, niente PATH, niente registro, niente password di
rem  amministratore. Cancellata questa cartella, sul PC non resta niente.
rem
rem  ATTENZIONE: da qui in giu' i percorsi sono tutti RELATIVI (il `cd` qui
rem  sopra ci ha gia' portati nella cartella): cosi' un percorso con spazi o
rem  parentesi non rompe i blocchi `if ( ... )`.
rem ===========================================================================

set "PYVER=3.12.15"
set "PYREL=20261009"
set "VPY=.venv\Scripts\python.exe"
set "VPYW=.venv\Scripts\pythonw.exe"
rem  Serve Python 3.12 o successivo: con 3.10 e precedenti i convertitori che
rem  usano pathlib muoiono ("stat() takes exactly 1 positional argument"),
rem  perche' _ambiente.py aggancia os.stat e il vecchio pathlib ci inciampa.
rem  Un Python piu' vecchio NON si usa: si scarica quello portatile.
set "PROVA=import sys; sys.exit(0 if sys.version_info[:2] >= (3, 12) else 1)"
rem  pip non deve lasciare la sua cache nella cartella dell'utente
set "PIP_NO_CACHE_DIR=1"
set "PIP_DISABLE_PIP_VERSION_CHECK=1"

rem --- 1. L'ambiente c'e' gia' e parte? -------------------------------------
rem  Un .venv ricorda il percorso ASSOLUTO di dove e' nato: se la cartella e'
rem  stata spostata o rinominata non parte piu', e va rifatto.
if not exist "%VPY%" goto cerca
"%VPY%" -c "%PROVA%" >nul 2>nul && goto librerie
echo  L'ambiente .venv non va piu' bene (cartella spostata, o Python troppo vecchio): lo rifaccio.
rmdir /s /q ".venv" >nul 2>nul

:cerca
rem --- 2. Cerco un Python che FUNZIONI DAVVERO ------------------------------
rem  `where python` NON basta: Windows 10 e 11 hanno un FINTO python.exe
rem  (l'alias del Microsoft Store) anche dove Python non c'e' mai stato: apre
rem  il negozio e non esegue niente. Non si guarda se esiste: lo si PROVA.
set "PY="
if exist ".python\python.exe" ".python\python.exe" -c "%PROVA%" >nul 2>nul && set "PY=.python\python.exe"
if defined PY goto venv
py -3 -c "%PROVA%" >nul 2>nul && set "PY=py -3"
if defined PY goto venv
python -c "%PROVA%" >nul 2>nul && set "PY=python"
if defined PY goto venv

rem --- 3. Non c'e': ne scarico uno portatile QUI DENTRO ---------------------
set "ARCH=x86_64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "ARCH=aarch64"
if /i "%PROCESSOR_ARCHITECTURE%"=="x86" if not defined PROCESSOR_ARCHITEW6432 set "ARCH=i686"
set "URL=https://github.com/astral-sh/python-build-standalone/releases/download/%PYREL%/cpython-%PYVER%%%2B%PYREL%-%ARCH%-pc-windows-msvc-install_only.tar.gz"

echo.
echo  ==================================================================
echo   Su questo PC manca Python 3.12+. Ne scarico uno portatile (45 MB) e lo
echo   metto QUI DENTRO, nella cartella .python: non installa niente nel
echo   sistema e non chiede password. Un paio di minuti.
echo   Non chiudere questa finestra.
echo  ==================================================================
echo.

curl -L --fail -o ".python_scarico.tar.gz" "%URL%"
if errorlevel 1 goto a_mano
rem  Un download interrotto lascia un file MOZZO: quello vero supera i 30 MB.
if not exist ".python_scarico.tar.gz" goto a_mano
for %%A in (".python_scarico.tar.gz") do if %%~zA LSS 30000000 goto a_mano

rmdir /s /q ".python" >nul 2>nul
mkdir ".python"
echo  Lo scompatto...
tar -xzf ".python_scarico.tar.gz" -C ".python" --strip-components=1
del ".python_scarico.tar.gz" >nul 2>nul
".python\python.exe" -c "%PROVA%" >nul 2>nul && set "PY=.python\python.exe"
if not defined PY goto a_mano
echo  Python %PYVER% pronto in .python
echo.

:venv
rem --- 4. L'ambiente del progetto -------------------------------------------
echo  Creo l'ambiente .venv ...
%PY% -m venv ".venv"
if not exist "%VPY%" goto senza_venv

:librerie
rem --- 5. Le due librerie dei convertitori ----------------------------------
"%VPY%" -c "import PIL, numpy" >nul 2>nul && goto lancia
echo  Installo pillow e numpy dentro .venv (serve internet)...
"%VPY%" -m pip install -q pillow numpy
if errorlevel 1 echo  Non ci sono riuscito adesso: ci riprova il tool quando premi PREPARA.

:lancia
rem --- 6. Apro il tool ------------------------------------------------------
rem  Con pythonw.exe la finestra si apre e questa console nera si chiude.
rem  `setup.bat --terminale` salta la finestra e lavora qui dentro.
if /i "%~1"=="--terminale" goto terminale
"%VPY%" -c "import tkinter" >nul 2>nul
if errorlevel 1 goto terminale
if not exist "%VPYW%" goto terminale
start "" "%VPYW%" "spinnerino_tool.py" %*
exit /b

:terminale
rem  Niente tkinter (raro): stesso lavoro, a colori, dentro questa finestra.
"%VPY%" "spinnerino_tool.py" --terminale %*
echo.
pause
exit /b

:senza_venv
echo.
echo  Non sono riuscito a creare l'ambiente .venv con   %PY%
echo  Cancella le cartelle .venv e .python (se ci sono) e rilancia setup.bat.
echo.
pause
exit /b 1

:a_mano
del ".python_scarico.tar.gz" >nul 2>nul
echo.
echo  Non ce l'ho fatta a scaricare Python (di solito manca la connessione
echo  a internet, oppure un antivirus ha bloccato il download).
echo.
echo  Riprova piu' tardi. In alternativa installalo a mano da
echo      https://www.python.org/downloads/
echo  spuntando "Add python.exe to PATH", e rilancia questo setup.bat.
echo.
pause
exit /b 1
