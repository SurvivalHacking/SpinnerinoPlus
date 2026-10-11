#!/bin/bash
# ============================================================================
# SPINNERINO P4 - Flash script (macOS / Linux)
#
# Prerequisiti:
#   1) Arduino IDE 2.x con esp32 core 3.3.8 installato
#   2) Sketch SPINNERINO.ino compilato con
#        Board:           ESP32P4 Dev Module
#        Flash Size:      32MB
#        Partition:       SPINNERINO 30MB (30MB APP/1.9MB SPIFFS)
#   3) Menu Arduino: Sketch -> Export Compiled Binary
#      (produce i .bin in build/esp32.esp32.esp32p4/)
#   4) esptool installato:  pip3 install esptool
#
# Uso:
#   ./spinnerino_flash.command /dev/cu.usbserial-XXXX
#   (se ometti la porta usa /dev/cu.usbserial-0001)
#
# Su macOS, doppio click apre Terminale solo se il file ha permessi exec:
#   chmod +x spinnerino_flash.command
# ============================================================================
set -e

cd "$(dirname "$0")"

PORT="${1:-/dev/cu.usbserial-0001}"
BAUD=921600
FLASH_SIZE=32MB
BUILD_DIR="build/esp32.esp32.esp32p4"
BOOTLOADER="$BUILD_DIR/SPINNERINO.ino.bootloader.bin"
PARTITIONS="$BUILD_DIR/SPINNERINO.ino.partitions.bin"
APP="$BUILD_DIR/SPINNERINO.ino.bin"

if [ ! -f "$APP" ]; then
  echo
  echo "[ERRORE] Binario non trovato: $APP"
  echo
  echo "Apri Arduino IDE, seleziona la partizione \"SPINNERINO 30MB\""
  echo "e fai: Sketch  >  Export Compiled Binary"
  echo
  exit 1
fi

# esptool puo' essere installato come "esptool.py" o "esptool"
if command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=esptool.py
elif command -v esptool >/dev/null 2>&1; then
  ESPTOOL=esptool
else
  echo "[ERRORE] esptool non trovato. Installa con: pip3 install esptool"
  exit 1
fi

echo
echo "Flashing SPINNERINO P4 on $PORT (32 MB flash, partition SPINNERINO 30MB)"
echo

"$ESPTOOL" --chip esp32p4 --port "$PORT" --baud "$BAUD" \
  --before default_reset --after hard_reset write_flash -z \
  --flash_mode dio --flash_freq 80m --flash_size "$FLASH_SIZE" \
  0x2000 "$BOOTLOADER" \
  0x8000 "$PARTITIONS" \
  0x10000 "$APP"

echo
echo "Flash completato."
