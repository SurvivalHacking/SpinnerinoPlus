# P4_C6_Programmer

Sketch per ESP32-P4 che programma automaticamente, a ogni accensione, un
ESP32-C6 collegato via UART (RX/TX + BOOT/EN), **senza PC, senza esptool,
senza Arduino IDE lato C6**. Il firmware del C6 e' incluso dentro lo sketch
stesso (`c6_firmware.h`), scritto ai suoi 4 offset reali (bootloader,
tabella delle partizioni, boot_app0, app -- circa 730KB totali, non i 4MB
del file "merged" che includono anche spazio vuoto mai usato).

Usa la libreria ufficiale Espressif
[esp-serial-flasher](https://github.com/espressif/esp-serial-flasher)
(un sottoinsieme vendorizzato, presente come singoli file .c/.h direttamente
nella cartella dello sketch) per parlare direttamente il protocollo del
bootloader ROM del C6.

**Nota tecnica**: i file della libreria sono "flat" nella cartella dello
sketch (invece che in una sottocartella `libraries/`) perche' la scoperta
automatica di librerie private per-sketch non e' garantita su tutte le
versioni di Arduino IDE. Un file `.c`/`.h` nella cartella principale dello
sketch, invece, viene sempre compilato -- e' un comportamento di base
stabile di Arduino IDE.

## Come compilare

1. **Installa Arduino_GFX_Library** dal Library Manager di Arduino IDE
   (cerca "GFX Library for Arduino" di moononournation). E' l'unica
   libreria esterna richiesta -- i file di `esp_serial_flasher` sono gia'
   nella cartella dello sketch e non vanno installati separatamente.
2. Apri `P4_C6_Programmer.ino` in Arduino IDE.
3. Seleziona la board P4 corretta. Lo sketch (con il C6 embeddato) pesa
   circa 1-1.5MB compilato -- va bene qualsiasi schema di partizioni
   standard con una app-partition di almeno 2MB.
4. Upload come al solito.

## Come aggiornare il firmware del C6 in futuro

1. Ricompila il progetto del C6 in Arduino IDE.
2. Sketch -> Esporta binario compilato.
3. Nella cartella dello sketch C6 troverai (tra gli altri) questi file:
   - `<nome>.ino.bootloader.bin`
   - `<nome>.ino.partitions.bin`
   - `boot_app0.bin` (nella sottocartella indicata nel log di compilazione,
     preso dal core -- stesso file per tutti gli sketch)
   - `<nome>.ino.bin` (l'applicazione vera e propria)
4. Rigenera l'header (nell'ordine: bootloader, partitions, boot_app0, app):
   ```
   python3 tools/make_c6_header.py \
       bootloader.bin partitions.bin boot_app0.bin app.bin
   ```
5. Ricompila e carica questo sketch sul P4.

Se il progetto C6 usa uno schema di partizioni diverso da quello di
default, gli offset (0x0 / 0x8000 / 0xe000 / 0x10000) potrebbero cambiare:
controllali nel `.partitions.bin` o nel log di compilazione e correggi
la mappa `OFFSETS` in cima a `tools/make_c6_header.py`.

## Note

- **Controllo rapido a ogni boot**: prima di riscrivere qualsiasi cosa, il
  P4 controlla l'MD5 di tutti e 4 i pezzi gia' presenti sul C6. Se
  coincidono con quelli embeddati, la programmazione viene saltata e si
  passa direttamente al rilascio del C6 in RUN (pochi secondi). La
  riscrittura completa scatta solo al primo avvio su un C6 vergine o dopo
  che hai aggiornato il firmware embeddato nel P4.
- **Velocita'**: la connessione usa lo stub flasher (solo quello specifico
  per ESP32-C6) per poter alzare il baud rate a 921600 durante la
  scrittura vera e propria; una programmazione completa di ~730KB dovrebbe
  richiedere pochi secondi.
- **Pin C6** (bridge RS232 + BOOT/EN), riutilizzati dal progetto originale:
  RX=46, TX=3, EN=54, BOOT=29.
- **Pin display ST7789**: MOSI=21, SCLK=20, CS=22, DC=23, RST=26, BL=27.
  Risoluzione impostata a 320x200 in cima allo sketch (`SCREEN_W`/`SCREEN_H`)
  -- adattala se il pannello reale e' diverso.
