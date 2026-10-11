@echo off
setlocal
cd /d "%~dp0"

rem ===========================================================================
rem  SPINNERINO PLUS - prepara.bat
rem  Rigenera il progetto dalle TUE zip MAME. Se Python non c'e', SE LO
rem  INSTALLA DA SOLO: per il solo utente, senza password di amministratore.
rem  Le due librerie (pillow e numpy) se le installa poi prepara.py.
rem ===========================================================================

rem --- 1. Cerco un Python che FUNZIONI DAVVERO ------------------------------
rem  ATTENZIONE: `where python` NON basta, ed e' il motivo per cui questo
rem  controllo e' cambiato. Windows 10 e 11 hanno un FINTO python.exe in
rem  ...\AppData\Local\Microsoft\WindowsApps\ (l'alias del Microsoft Store):
rem  c'e' anche su un PC dove Python non e' mai stato installato, quindi
rem  `where` lo trovava e il controllo passava. Poi pero' quel finto python
rem  apre il negozio Microsoft e non esegue niente: partiva prepara.bat e
rem  non succedeva NULLA, senza un errore da leggere.
rem  Quindi non si guarda se il file esiste: si PROVA a eseguirlo.
set "PY="
py -3 -c "import sys" >nul 2>nul && set "PY=py -3"
if not defined PY (
  python -c "import sys" >nul 2>nul && set "PY=python"
)
if defined PY goto lancia

rem --- 2. Non c'e': lo scarico e lo installo io -----------------------------
set "PYVER=3.12.10"
set "PYFILE=python-%PYVER%-amd64.exe"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "PYFILE=python-%PYVER%-arm64.exe"

echo.
echo  ==================================================================
echo   Su questo PC manca Python, che serve a rigenerare il progetto.
echo   Lo scarico da python.org e lo installo io: sono circa 26 MB e ci
echo   vogliono un paio di minuti. NON serve la password di
echo   amministratore. Non chiudere questa finestra.
echo  ==================================================================
echo.

curl -L --fail -o "%TEMP%\%PYFILE%" "https://www.python.org/ftp/python/%PYVER%/%PYFILE%"
if errorlevel 1 goto a_mano
rem  Un download interrotto lascia un file MOZZO: se lo si lancia lo stesso,
rem  l'errore che esce parla di un installatore corrotto e non dice che il
rem  problema era la rete. L'installatore vero sta sopra i 25 MB.
if not exist "%TEMP%\%PYFILE%" goto a_mano
for %%A in ("%TEMP%\%PYFILE%") do if %%~zA LSS 20000000 goto a_mano

rem  /passive          barra di avanzamento, nessuna domanda
rem  InstallAllUsers=0 solo per questo utente -> niente amministratore
rem  PrependPath=1     da qui in avanti `python` funziona anche a mano
rem  Include_test=0    salta la suite di collaudo, non serve a niente qui
echo  Installo Python %PYVER%...
start /wait "" "%TEMP%\%PYFILE%" /passive InstallAllUsers=0 PrependPath=1 Include_test=0
del "%TEMP%\%PYFILE%" >nul 2>nul

rem --- 3. Lo ricerco --------------------------------------------------------
rem  ATTENZIONE: in QUESTA finestra il PATH e' ancora quello di prima
rem  (il PATH si legge quando la finestra si apre): non basta riprovare
rem  `python`, bisogna andare a prendere l'exe dove l'installatore l'ha messo.
py -3 -c "import sys" >nul 2>nul && set "PY=py -3"
rem  ATTENZIONE: si guardano SOLO le cartelle di primo livello (`for /d`).
rem  Con una ricerca ricorsiva (`dir /s`) si pesca anche
rem  ...\Python312\Lib\venv\scripts\nt\python.exe, che e' il MODELLO usato per
rem  creare gli ambienti virtuali: lanciato da solo risponde
rem  "No pyvenv.cfg file" e non esegue niente. Successo davvero in prova.
if not defined PY (
  for /d %%d in ("%LOCALAPPDATA%\Programs\Python\Python3*") do (
    if exist "%%d\python.exe" set "PY="%%d\python.exe""
  )
)
if not defined PY (
  for /d %%d in ("%ProgramFiles%\Python3*") do (
    if exist "%%d\python.exe" set "PY="%%d\python.exe""
  )
)
if not defined PY (
  python -c "import sys" >nul 2>nul && set "PY=python"
)
if not defined PY goto a_mano
echo  Python installato.
echo.

:lancia
%PY% prepara.py %*
echo.
pause
exit /b

:a_mano
echo.
echo  Non ce l'ho fatta a installare Python da solo (di solito manca la
echo  connessione a internet, oppure un antivirus ha bloccato il download).
echo.
echo  Scaricalo a mano da     https://www.python.org/downloads/
echo  e quando lo installi SPUNTA la casella "Add python.exe to PATH".
echo  Poi rilancia questo prepara.bat.
echo.
pause
exit /b 1
