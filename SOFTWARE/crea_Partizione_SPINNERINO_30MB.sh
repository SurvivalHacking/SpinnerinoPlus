#!/bin/bash
# ============================================================================
# Installa partizione "SPINNERINO 30MB" per Arduino IDE 2.x (macOS / Linux)
# ESP32-P4, Flash 32 MB -> 30 MB APP / 1.9 MB SPIFFS
#
# Rendi eseguibile una volta:   chmod +x Installa_Partizione_SPINNERINO_30MB.command
# Poi doppio click (macOS) oppure ./Installa_Partizione_SPINNERINO_30MB.command
# ============================================================================
set -u

echo "================================================================"
echo "  Installazione partizione 'SPINNERINO 30MB' (ESP32-P4 32MB)"
echo "================================================================"

# --- Trova la data dir di Arduino15 (macOS: ~/Library/Arduino15, Linux: ~/.arduino15) ---
if   [ -d "$HOME/Library/Arduino15/packages/esp32/hardware/esp32" ]; then
  PKG_DIR="$HOME/Library/Arduino15/packages/esp32/hardware/esp32"
  IDECACHE="$HOME/Library/Application Support/arduino-ide"
elif [ -d "$HOME/.arduino15/packages/esp32/hardware/esp32" ]; then
  PKG_DIR="$HOME/.arduino15/packages/esp32/hardware/esp32"
  IDECACHE="$HOME/.config/arduino-ide"
else
  echo "ERRORE: core ESP32 non trovato. Installa Arduino IDE 2.x + core ESP32."
  read -n1 -r -p "Premi un tasto per uscire..."; exit 1
fi

# --- Contenuto CSV (layout 32MB: 30MB APP + 1.9MB SPIFFS) ---
read -r -d '' CSV <<'EOF' || true
# Name,     Type, SubType,  Offset,     Size,
# ESP32-P4 32MB Flash - SPINNERINO 30MB APP / 1.9MB SPIFFS
nvs,        data, nvs,      0x9000,     0x5000,
factory,    app,  factory,  0x10000,    0x1E00000,
spiffs,     data, spiffs,   0x1E10000,  0x1E0000,
coredump,   data, coredump, 0x1FF0000,  0x10000,
EOF

FOUND=0
for VER_DIR in "$PKG_DIR"/*/; do
  BOARDS="$VER_DIR/boards.txt"
  PARTS_DIR="$VER_DIR/tools/partitions"
  [ -f "$BOARDS" ] || continue
  echo
  echo "--- Core esp32 versione $(basename "$VER_DIR")"
  FOUND=$((FOUND+1))

  if grep -q "spinnerino_30MB=SPINNERINO 30MB" "$BOARDS"; then
    echo "  [=]  voce gia' presente in boards.txt"
  else
    [ -f "$BOARDS.bak_spinnerino30" ] || { cp "$BOARDS" "$BOARDS.bak_spinnerino30"; echo "  [backup] boards.txt.bak_spinnerino30"; }
    {
      echo ""
      echo "esp32p4.menu.PartitionScheme.spinnerino_30MB=SPINNERINO 30MB (30MB APP/1.9MB SPIFFS)"
      echo "esp32p4.menu.PartitionScheme.spinnerino_30MB.build.partitions=spinnerino_30MB"
      echo "esp32p4.menu.PartitionScheme.spinnerino_30MB.upload.maximum_size=31457280"
    } >> "$BOARDS"
    echo "  [OK] aggiunta voce in boards.txt"
  fi

  if [ -d "$PARTS_DIR" ]; then
    printf '%s\n' "$CSV" > "$PARTS_DIR/spinnerino_30MB.csv"
    echo "  [OK] spinnerino_30MB.csv copiato in tools/partitions"
  else
    echo "  [!]  cartella tools/partitions mancante - salto"
  fi
done

echo
echo "================================================================"
if [ "$FOUND" -eq 0 ]; then
  echo "  ERRORE: nessun core ESP32 trovato."
  read -n1 -r -p "Premi un tasto per uscire..."; exit 1
fi

echo "  Partizione registrata su $FOUND core."
echo
echo "  ATTENZIONE: ora chiudo Arduino IDE e svuoto la sua cache UI"
echo "  (altrimenti la partizione NON compare nel menu)."
echo "  >>> SALVA il lavoro aperto nell'IDE, poi premi un tasto <<<"
read -n1 -r -p "  ... "

echo
echo "  Chiudo Arduino IDE e il daemon arduino-cli..."
osascript -e 'quit app "Arduino IDE"' 2>/dev/null || true
pkill -f "Arduino IDE" 2>/dev/null || true
pkill -f "arduino-cli" 2>/dev/null || true
sleep 2

for d in "Local Storage" "Session Storage" "IndexedDB"; do
  if [ -d "$IDECACHE/$d" ]; then
    rm -rf "$IDECACHE/$d.bak" 2>/dev/null || true
    mv "$IDECACHE/$d" "$IDECACHE/$d.bak" && echo "  [OK] cache UI '$d' azzerata (backup .bak)"
  fi
done

echo
echo "  FATTO. PROSSIMI PASSI:"
echo "  1. Riapri Arduino IDE e lo sketch SPINNERINO"
echo "  2. Scheda -> ESP32P4 Dev Module   (Flash Size: 32MB)"
echo "  3. Strumenti -> Partition Scheme -> 'SPINNERINO 30MB'"
echo "  4. Compila / Export Compiled Binary, poi ./spinnerino_flash.command"
echo "================================================================"
read -n1 -r -p "Premi un tasto per chiudere..."
