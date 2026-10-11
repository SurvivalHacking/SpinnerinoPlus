@echo off
setlocal enabledelayedexpansion
title Installa partizione "SPINNERINO 30MB" per Arduino IDE
color 0A

echo ================================================================
echo   Installazione partizione "SPINNERINO 30MB"
echo   per ESP32-P4 (Flash 32 MB) - 30 MB APP / 1.9 MB SPIFFS
echo ================================================================
echo.

set "ARDUINO15=%LOCALAPPDATA%\Arduino15\packages"
if not exist "%ARDUINO15%" (
    echo ERRORE: cartella Arduino15 non trovata in %LOCALAPPDATA%
    echo Installa prima Arduino IDE 2.x e il core ESP32.
    pause
    exit /b 1
)

REM ---- Genera il CSV in TEMP (layout 32MB: 30MB APP + 1.9MB SPIFFS) ----
set "CSV_FILE=%TEMP%\spinnerino_30MB.csv"
(
    echo # Name,     Type, SubType,  Offset,     Size,
    echo # ESP32-P4 32MB Flash - SPINNERINO 30MB APP / 1.9MB SPIFFS
    echo nvs,        data, nvs,      0x9000,     0x5000,
    echo factory,    app,  factory,  0x10000,    0x1E00000,
    echo spiffs,     data, spiffs,   0x1E10000,  0x1E0000,
    echo coredump,   data, coredump, 0x1FF0000,  0x10000,
) > "%CSV_FILE%"

set /a FOUND=0

REM ---- Solo core esp32 standard (la board ESP32P4 Dev Module sta li') ----
set "PKG_DIR=%ARDUINO15%\esp32\hardware\esp32"
if exist "%PKG_DIR%" (
    for /D %%V in ("%PKG_DIR%\*") do (
        set "VER_DIR=%%V"
        set "VER_NAME=%%~nxV"
        set "BOARDS=!VER_DIR!\boards.txt"
        set "PARTS_DIR=!VER_DIR!\tools\partitions"
        if exist "!BOARDS!" (
            echo.
            echo --- Core esp32 versione !VER_NAME!
            set /a FOUND+=1

            findstr /C:"spinnerino_30MB=SPINNERINO 30MB" "!BOARDS!" >nul 2>&1
            if errorlevel 1 (
                if not exist "!BOARDS!.bak_spinnerino30" (
                    copy /Y "!BOARDS!" "!BOARDS!.bak_spinnerino30" >nul
                    echo   [backup] boards.txt.bak_spinnerino30
                )
                echo.>> "!BOARDS!"
                echo esp32p4.menu.PartitionScheme.spinnerino_30MB=SPINNERINO 30MB ^(30MB APP/1.9MB SPIFFS^)>> "!BOARDS!"
                echo esp32p4.menu.PartitionScheme.spinnerino_30MB.build.partitions=spinnerino_30MB>> "!BOARDS!"
                echo esp32p4.menu.PartitionScheme.spinnerino_30MB.upload.maximum_size=31457280>> "!BOARDS!"
                echo   [OK] aggiunta voce in boards.txt
            ) else (
                echo   [=]  voce gia' presente in boards.txt
            )

            if exist "!PARTS_DIR!" (
                copy /Y "%CSV_FILE%" "!PARTS_DIR!\spinnerino_30MB.csv" >nul
                echo   [OK] spinnerino_30MB.csv copiato in tools\partitions
            ) else (
                echo   [!]  cartella tools\partitions mancante - salto
            )
        )
    )
)

del "%CSV_FILE%" >nul 2>&1

echo.
echo ================================================================
if !FOUND! EQU 0 (
    color 0C
    echo   ERRORE: nessun core ESP32 trovato.
    echo   Installa il core ESP32 dal Boards Manager di Arduino IDE.
) else (
    echo   Partizione registrata su !FOUND! core.
    echo.
    echo   ----------------------------------------------------------------
    echo   ATTENZIONE: ora chiudo Arduino IDE e svuoto la sua cache UI.
    echo   Senza questo passaggio la partizione NON compare nel menu
    echo   ^(l'IDE 2.x tiene in cache la lista vecchia per ogni sketch^).
    echo   ^>^>^>  SALVA il lavoro aperto nell'IDE, poi premi un tasto  ^<^<^<
    echo   ----------------------------------------------------------------
    pause

    echo.
    echo   Chiudo Arduino IDE e il daemon arduino-cli...
    taskkill /F /IM "Arduino IDE.exe" /T >nul 2>&1
    taskkill /F /IM "arduino-cli.exe" /T >nul 2>&1
    timeout /t 2 /nobreak >nul

    set "IDECACHE=%APPDATA%\arduino-ide"
    for %%D in ("Local Storage" "Session Storage" "IndexedDB") do (
        if exist "!IDECACHE!\%%~D" (
            if exist "!IDECACHE!\%%~D.bak" rmdir /S /Q "!IDECACHE!\%%~D.bak"
            ren "!IDECACHE!\%%~D" "%%~D.bak"
            echo   [OK] cache UI '%%~D' azzerata ^(backup .bak^)
        )
    )

    echo.
    echo   FATTO. PROSSIMI PASSI:
    echo   1. Riapri Arduino IDE e lo sketch SPINNERINO
    echo   2. Scheda -^> ESP32P4 Dev Module   ^(Flash Size: 32MB^)
    echo   3. Strumenti -^> Partition Scheme -^> "SPINNERINO 30MB"
    echo   4. Compila / Export Compiled Binary, poi spinnerino_flash.bat
)
echo ================================================================
echo.
pause
endlocal
