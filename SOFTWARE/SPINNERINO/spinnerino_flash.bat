@echo off
REM ============================================================================
REM SPINNERINO P4 - Flash script (Windows)
REM
REM Prerequisiti:
REM   1) Arduino IDE 2.x con esp32 core 3.3.8 installato
REM   2) Sketch SPINNERINO.ino compilato con
REM        Board:           ESP32P4 Dev Module
REM        Flash Size:      32MB
REM        Partition:       SPINNERINO 30MB (30MB APP/1.9MB SPIFFS)
REM   3) Menu Arduino: Sketch -> Export Compiled Binary
REM      (produce file in cartella build\esp32.esp32.esp32p4\)
REM   4) Python con esptool installato:  pip install esptool
REM
REM Uso:
REM   spinnerino_flash.bat COM3
REM   (se ometti la porta usa COM3 di default)
REM ============================================================================
setlocal

set PORT=%1
if "%PORT%"=="" set PORT=COM3

set BAUD=921600
set FLASH_SIZE=32MB
set BUILD_DIR=build\esp32.esp32.esp32p4
set BOOTLOADER=%BUILD_DIR%\SPINNERINO.ino.bootloader.bin
set PARTITIONS=%BUILD_DIR%\SPINNERINO.ino.partitions.bin
set APP=%BUILD_DIR%\SPINNERINO.ino.bin

if not exist "%APP%" (
  echo.
  echo [ERRORE] Binario non trovato: %APP%
  echo.
  echo Apri Arduino IDE, seleziona la partizione "SPINNERINO 30MB"
  echo e fai: Sketch  ^>  Export Compiled Binary
  echo.
  exit /b 1
)

echo.
echo Flashing SPINNERINO P4 on %PORT% (32 MB flash, partition SPINNERINO 30MB)
echo.

esptool --chip esp32p4 --port %PORT% --baud %BAUD% ^
  --before default_reset --after hard_reset write_flash -z ^
  --flash_mode dio --flash_freq 80m --flash_size %FLASH_SIZE% ^
  0x2000 "%BOOTLOADER%" ^
  0x8000 "%PARTITIONS%" ^
  0x10000 "%APP%"

if errorlevel 1 (
  echo.
  echo [ERRORE] Flash fallito. Controlla porta seriale, driver USB-CDC e cavo.
  exit /b 1
)

echo.
echo Flash completato.
endlocal
