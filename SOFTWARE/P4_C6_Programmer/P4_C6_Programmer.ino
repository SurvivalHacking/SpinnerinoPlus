/*
 * ============================================================================
 *  SPINNERINO+ C6 PROGRAMMER -- ESP32-P4 come programmatore standalone per ESP32-C6
 *  by Davide Gatti -- Survival Hacking -- C6 Code by Marco Prunca
 * ============================================================================
 *
 * V1.0 - 09/26 - Prima versione
 * V1.1 - 10/26 - Aggiunto supporto WIFi e file manager per futuri usi
 * V1.2 - 10/26 - Aumentata la velocità di trasferimento
 *
 *   Arduino IDE settings esp32 core = 3.3.8 oppure 3.3.11
 *     (con i core < 3.3.9 l'alimentazione dei pin 39..48 la accende
 *      powerIoDomain(), vedi sotto)
 *   Board:                 ESP32P4 Dev Module
 *   PSRAM:                 Enabled
 *   USB Mode:              Hardware CDC and JTAG
 *   USB CDC On Boot:       Enabled
 *
 * COSA FA
 * -------
 * All'accensione, il P4 controlla il firmware attualmente presente sul C6
 * collegato via UART:
 *   - se e' gia' quello giusto (stesso MD5), lo lascia avviare normalmente
 *     e mostra "C6 gia' aggiornato" -- da quel momento in poi tieni premuto
 *     il tasto su GPIO48 per forzare comunque una riprogrammazione;
 *   - se e' diverso (primo avvio su C6 vergine, o hai aggiornato il
 *     firmware embeddato in questo sketch), lo riprogramma subito in
 *     automatico, mostrando lo stato e la barra di progresso a schermo.
 *
 * Tutto avviene senza PC, senza esptool, senza Arduino IDE lato C6: il
 * firmware del C6 e' incluso dentro questo stesso sketch (vedi
 * c6_firmware.h).
 *
 * LIBRERIE RICHIESTE (da installare una volta sola dal Library Manager
 * di Arduino IDE):
 *   - Arduino_GFX_Library (di moononournation)
 *
 * I file della libreria "esp_serial_flasher" (Espressif) sono invece gia'
 * presenti come singoli file .c/.h direttamente in questa cartella
 * (sottoinsieme vendorizzato, solo UART, solo target C6): non vanno
 * installati separatamente, Arduino IDE li compila automaticamente in
 * quanto file sorgente della cartella dello sketch.
 *
 * COME AGGIORNARE IL FIRMWARE DEL C6 IN FUTURO
 * ------------------------------------------------------------------------
 *   1. Compila normalmente il progetto del C6 in Arduino IDE
 *      (Sketch -> Esporta binario compilato).
 *   2. Prendi i 4 file generati: bootloader.bin, partitions.bin,
 *      boot_app0.bin e l'app (<nome>.ino.bin) -- NON il "..._merged.bin"
 *      (quello include anche ~3.3MB di spazio vuoto che non ci serve).
 *   3. Rigenera c6_firmware.h con lo script incluso:
 *        python3 tools/make_c6_header.py \
 *            bootloader.bin partitions.bin boot_app0.bin app.bin
 *      (sovrascrive c6_firmware.h nella cartella dello sketch)
 *   4. Ricompila e carica questo sketch sul P4 come al solito.
 * ============================================================================
 */

#include <Arduino_GFX_Library.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_arduino_version.h"
#include "esp_ldo_regulator.h"

#include "esp_loader.h"
#include "esp_loader_stubs.h"
#include "esp32_port.h"

#include "c6_firmware.h"

// ----------------------------------------------------------------------
// Pin verso il C6 (bridge RS232 + controllo BOOT/EN) -- dal progetto originale
// ----------------------------------------------------------------------
#define C6_RX_PIN   46
#define C6_TX_PIN   3
#define C6_EN_PIN   54
#define C6_BOOT_PIN 29
#define C6_UART_NUM UART_NUM_1

#define C6_BAUD_CONNECT 115200   // baud del bootloader ROM (fisso)
#define C6_BAUD_FLASH   921600   // baud usato durante la scrittura (dopo aver agganciato lo stub)

// ----------------------------------------------------------------------
// Pulsante per forzare una riprogrammazione manuale
// ----------------------------------------------------------------------
#define BUTTON_PIN 48
// Assumiamo pulsante verso GND con pull-up interno (premuto = LOW).
// Se il tuo pulsante e' cablato al contrario, inverti questa macro.
#define BUTTON_PRESSED() (digitalRead(BUTTON_PIN) == LOW)

// ----------------------------------------------------------------------
// Pin del display ST7789
// ----------------------------------------------------------------------
#define TFT_MOSI 21
#define TFT_SCLK 20
#define TFT_CS   22
#define TFT_DC   23
#define TFT_RST  26
#define TFT_BL   27

// Adatta questi due valori alla risoluzione reale del tuo pannello se necessario
#define SCREEN_W 240
#define SCREEN_H 320

// Altezza dell'intestazione fissa (sempre visibile) in alto allo schermo
#define HEADER_H 74

Arduino_DataBus *bus = new Arduino_ESP32SPI(
    TFT_DC, TFT_CS, TFT_SCLK, TFT_MOSI, GFX_NOT_DEFINED /* MISO */, SPI2_HOST);
Arduino_GFX *gfx = new Arduino_ST7789(
    bus, TFT_RST, 0 /* rotation */, false /* IPS */, SCREEN_W, SCREEN_H);
// ⚠️ IPS = false: con true la libreria manda INVON e su questo pannello TUTTI
// i colori escono invertiti (verde -> rosa/viola, giallo -> blu). Anche lo
// SPINNERINO usa INVOFF sullo stesso schermo (config.h, TFT_INVERT spento).

// ----------------------------------------------------------------------
// UI
// ----------------------------------------------------------------------

// Disegna una riga di testo centrata orizzontalmente, alla riga "y" data.
static void drawCentered(const char *text, int16_t y, uint8_t textSize, uint16_t color) {
  gfx->setTextSize(textSize);
  gfx->setTextColor(color);
  int16_t x1, y1;
  uint16_t w, h;
  gfx->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (SCREEN_W - (int16_t)w) / 2;
  if (x < 0) x = 0;
  gfx->setCursor(x, y);
  gfx->print(text);
}

// Colori dell'interfaccia
#define COL_BODY  RGB565_BLUE         // sfondo sotto l'intestazione (stato + barra)
#define COL_BAR   RGB565_YELLOW       // riempimento della barra di avanzamento
#define COL_TEXT  RGB565_WHITE        // scritte di stato, cornice della barra, percentuale
#define COL_OK    RGB565(0, 180, 0)   // sfondo della schermata "OK": verde

// Intestazione fissa: sempre visibile, testi centrati, ridisegnata solo
// quando serve (non viene mai toccata dalle funzioni di stato sottostanti).
static void uiDrawHeader() {
  gfx->fillRect(0, 0, SCREEN_W, HEADER_H, RGB565_NAVY);
  drawCentered("SPINNERINO+ C6 PROGRAMMER V1.2", 1,  1, RGB565_YELLOW);
  drawCentered("BY Davide Gatti",           18, 1, RGB565_WHITE);
  drawCentered("Survival Hacking",          32, 1, RGB565_WHITE);
  drawCentered("C6 Code, by Marco Prunca",  46, 1, RGB565_WHITE);
  gfx->drawFastHLine(0, HEADER_H - 1, SCREEN_W, RGB565_YELLOW);
}

static void uiInit() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  gfx->begin();
  gfx->fillScreen(COL_BODY);
  uiDrawHeader();
}

// Da qui in giu': tutte le funzioni di stato disegnano SOLO nell'area
// sotto l'intestazione (da HEADER_H in poi), che quindi resta sempre
// visibile e non viene mai cancellata.

static void uiClearBody() {
  gfx->fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, COL_BODY);
}

static void uiStatus(const char *msg, uint16_t color) {
  gfx->fillRect(0, HEADER_H + 6, SCREEN_W, 20, COL_BODY);
  gfx->setTextColor(color);
  gfx->setTextSize(1);
  gfx->setCursor(4, HEADER_H + 10);
  gfx->print(msg);
}

static void uiProgress(int percent) {
  const int x = 10, y = HEADER_H + 34, w = SCREEN_W - 20, h = 22;
  gfx->drawRect(x, y, w, h, COL_TEXT);
  int fillw = (w - 4) * percent / 100;
  gfx->fillRect(x + 2, y + 2, w - 4, h - 4, COL_BODY);
  if (fillw > 0) gfx->fillRect(x + 2, y + 2, fillw, h - 4, COL_BAR);

  char buf[8];
  snprintf(buf, sizeof(buf), "%3d%%", percent);
  gfx->fillRect(0, y + h + 6, SCREEN_W, 20, COL_BODY);
  gfx->setTextColor(COL_TEXT);
  gfx->setTextSize(2);
  gfx->setCursor(SCREEN_W / 2 - 24, y + h + 8);
  gfx->print(buf);
}

// Messaggio "grande" (OK / ERRORE / info), sotto l'intestazione, che
// occupa il resto dello schermo -- l'intestazione in alto resta visibile.
static void uiBigMessage(uint16_t bg, const char *big, const char *small1, const char *small2) {
  gfx->fillRect(0, HEADER_H, SCREEN_W, SCREEN_H - HEADER_H, bg);
  int16_t y = HEADER_H + 40;
  drawCentered(big, y, 3, RGB565_WHITE);
  y += 40;
  if (small1 && small1[0]) {
    drawCentered(small1, y, 1, RGB565_WHITE);
    y += 16;
  }
  if (small2 && small2[0]) {
    drawCentered(small2, y, 1, RGB565_WHITE);
    y += 16;
  }
}

static void uiError(const char *msg) {
  uiBigMessage(RGB565_RED, "ERRORE", msg, NULL);
}

static void uiSuccess(const char *msg) {
  uiBigMessage(COL_OK, "OK", msg, NULL);
}

// Schermata "C6 gia' aggiornato" con invito a premere il tasto per forzare
// una riprogrammazione -- resta cosi' finche' non arriva una pressione.
static void uiAlreadyUpToDate() {
  uiBigMessage(RGB565_NAVY, "C6 aggiornato", "Premi il tasto per", "riprogrammare");
}

// ----------------------------------------------------------------------
// esp-serial-flasher
// ----------------------------------------------------------------------
static esp32_port_t s_port;
static esp_loader_t s_loader;

// Selettore dello stub: usiamo solo quello per ESP32-C6 (l'unico target
// possibile su questo bridge), cosi' non serve linkare gli stub degli
// altri chip.
static const esp_stub_t *c6StubProvider(esp_loader_t *loader, target_chip_t chip, void *ctx) {
  (void)loader;
  (void)ctx;
  if (chip == ESP32C6_CHIP) {
    return &esp_stub_esp32c6;
  }
  return NULL;
}

static bool initLoaderPort() {
  s_port.port.ops    = &esp32_uart_ops;
  s_port.baud_rate   = C6_BAUD_CONNECT;
  s_port.uart_port   = C6_UART_NUM;
  // ATTENZIONE al verso: C6_RX_PIN/C6_TX_PIN sono nominati dal punto di
  // vista del C6 (il pin del P4 collegato rispettivamente a RX e TX del
  // C6). Il P4 deve quindi TRASMETTERE sul pin collegato all'RX del C6
  // (C6_RX_PIN) e RICEVERE su quello collegato al TX del C6 (C6_TX_PIN).
  s_port.uart_rx_pin = (gpio_num_t)C6_TX_PIN;
  s_port.uart_tx_pin = (gpio_num_t)C6_RX_PIN;
  s_port.reset_pin   = (gpio_num_t)C6_EN_PIN;
  s_port.boot_pin    = (gpio_num_t)C6_BOOT_PIN;

  return esp_loader_init_serial(&s_loader, &s_port.port) == ESP_LOADER_SUCCESS;
}

// Controllo rapido: il C6 ha gia' questi esatti 4 pezzi (bootloader,
// partition table, boot_app0, app)? Se si', evitiamo di riscrivere
// ~750KB inutilmente.
static bool c6AlreadyUpToDate() {
  esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
  // Il comando SPI_FLASH_MD5 non e' gestito dal solo bootloader ROM su
  // ESP32-C6 (nessuna risposta, timeout) -- serve connettersi con lo stub,
  // esattamente come per la scrittura.
  esp_loader_error_t connErr =
      esp_loader_connect_with_stub_provider(&s_loader, &args, c6StubProvider, NULL);
  Serial.printf("[P4_C6] c6AlreadyUpToDate: connect_with_stub -> %d\n", (int)connErr);
  if (connErr != ESP_LOADER_SUCCESS) {
    return false;
  }
  for (int i = 0; i < C6_FLASH_REGIONS_COUNT; i++) {
    const c6_flash_region_t *r = &C6_FLASH_REGIONS[i];
    esp_loader_error_t err =
        esp_loader_flash_verify_known_md5(&s_loader, r->offset, r->size, (const uint8_t *)r->md5_hex);
    if (err != ESP_LOADER_SUCCESS) {
      return false;
    }
  }
  return true;
}

static bool flashC6() {
  esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();

  uiStatus("Connessione al C6...", COL_TEXT);
  esp_loader_error_t err =
      esp_loader_connect_with_stub_provider(&s_loader, &args, c6StubProvider, NULL);
  Serial.printf("[P4_C6] esp_loader_connect_with_stub_provider() -> %d\n", (int)err);
  if (err != ESP_LOADER_SUCCESS) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Impossibile collegarsi al C6 (errore %d).\nVedi Serial Monitor.", (int)err);
    uiError(buf);
    return false;
  }

  esp_loader_change_transmission_rate(&s_loader, C6_BAUD_FLASH);

  uint32_t doneTotal = 0;
  int lastPercent = -1;

  for (int i = 0; i < C6_FLASH_REGIONS_COUNT; i++) {
    const c6_flash_region_t *r = &C6_FLASH_REGIONS[i];

    char buf[48];
    snprintf(buf, sizeof(buf), "Scrittura %s...", r->name);
    uiStatus(buf, COL_TEXT);

    esp_loader_flash_cfg_t cfg = {};
    cfg.offset      = r->offset;
    cfg.image_size  = r->size;
    cfg.block_size  = 4096;
    cfg.skip_verify = true;  // verifica fatta a parte, con connessione fresca

    if (esp_loader_flash_start(&s_loader, &cfg) != ESP_LOADER_SUCCESS) {
      uiError("Errore all'avvio della scrittura flash.");
      return false;
    }

    uint32_t offset = 0;
    while (offset < cfg.image_size) {
      uint32_t chunk = cfg.block_size;
      if (offset + chunk > cfg.image_size) {
        chunk = cfg.image_size - offset;
      }
      if (esp_loader_flash_write(&s_loader, &cfg,
                                  (void *)(r->data + offset), chunk) != ESP_LOADER_SUCCESS) {
        uiError("Errore durante la scrittura della flash.");
        return false;
      }
      offset += chunk;
      doneTotal += chunk;
      int percent = (int)((uint64_t)doneTotal * 100 / C6_FLASH_TOTAL_BYTES);
      if (percent != lastPercent) {
        uiProgress(percent);
        lastPercent = percent;
      }
    }

    if (esp_loader_flash_finish(&s_loader, &cfg) != ESP_LOADER_SUCCESS) {
      snprintf(buf, sizeof(buf), "Verifica MD5 fallita su %s.\nRiprovare.", r->name);
      uiError(buf);
      return false;
    }
  }

  return true;
}

static void releaseC6ToRun() {
  esp_loader_reset_target(&s_loader);
}

// Verifica finale, in una connessione fresca, DOPO che tutte le regioni
// sono state scritte.
static bool verifyAllRegionsFresh() {
  esp_loader_connect_args_t args = ESP_LOADER_CONNECT_DEFAULT();
  esp_loader_error_t connErr =
      esp_loader_connect_with_stub_provider(&s_loader, &args, c6StubProvider, NULL);
  Serial.printf("[P4_C6] verifica finale: connect_with_stub -> %d\n", (int)connErr);
  if (connErr != ESP_LOADER_SUCCESS) {
    return false;
  }

  bool allOk = true;
  for (int i = 0; i < C6_FLASH_REGIONS_COUNT; i++) {
    const c6_flash_region_t *r = &C6_FLASH_REGIONS[i];
    esp_loader_error_t err =
        esp_loader_flash_verify_known_md5(&s_loader, r->offset, r->size, (const uint8_t *)r->md5_hex);
    Serial.printf("[P4_C6] verifica finale %-10s (offset 0x%05lx, %lu byte) -> %d %s\n",
                  r->name, (unsigned long)r->offset, (unsigned long)r->size, (int)err,
                  (err == ESP_LOADER_SUCCESS) ? "OK" : "MISMATCH/ERRORE");
    if (err != ESP_LOADER_SUCCESS) {
      allOk = false;
    }
  }
  return allOk;
}

// Esegue scrittura + verifica finale + rilascio, aggiornando lo schermo.
// Usata sia al boot (se serve) sia su richiesta dal pulsante.
static void doFullReflash() {
  unsigned long flashStartMs = millis();
  if (flashC6()) {
    unsigned long elapsedMs = millis() - flashStartMs;
    Serial.printf("[P4_C6] scrittura completata in %lu ms (%.1f s)\n",
                  elapsedMs, elapsedMs / 1000.0);

    uiStatus("Verifica finale...", COL_TEXT);
    bool verified = verifyAllRegionsFresh();

    char buf[64];
    if (verified) {
      snprintf(buf, sizeof(buf), "Programmazione completata e verificata.\n%lu byte scritti.",
                (unsigned long)C6_FLASH_TOTAL_BYTES);
      uiSuccess(buf);
    } else {
      snprintf(buf, sizeof(buf), "Scritto ma verifica finale fallita.\nVedi Serial Monitor.");
      uiError(buf);
    }
  }
  // In caso di errore nella scrittura stessa, il messaggio e' gia' stato
  // mostrato da flashC6().

  releaseC6ToRun();
}

// Attesa con debounce di una pressione del pulsante. Non bloccante nel
// senso che va richiamata da loop(): ritorna true una sola volta per ogni
// pressione (bordo di discesa), poi aspetta il rilascio.
static bool checkButtonPressed() {
  static bool wasPressed = false;
  bool isPressed = BUTTON_PRESSED();

  if (isPressed && !wasPressed) {
    delay(30);  // debounce
    if (BUTTON_PRESSED()) {
      wasPressed = true;
      return true;
    }
  } else if (!isPressed) {
    wasPressed = false;
  }
  return false;
}

// ----------------------------------------------------------------------
// ----------------------------------------------------------------------
// Alimentazione dei pin GPIO39..48 (C6_RX_PIN 46 e BUTTON_PIN 48)
// ----------------------------------------------------------------------
// Sul P4 quei pin stanno in un dominio di I/O alimentato dall'LDO interno
// canale 4 (lo stesso della microSD). Se nessuno lo accende, il pin 46 non
// trasmette: il C6 non sente la sincronizzazione e la connessione fallisce
// con errore 2 (TIMEOUT). Dal core 3.3.9 lo accende il core da solo quando
// si usa un pin di quel gruppo; con i core precedenti (es. 3.3.8, quello
// dello SPINNERINO) va acceso qui. Lo SPINNERINO non ne ha bisogno: lo
// accende gia' la sua sd_card.cpp.
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 3, 9)
static esp_ldo_channel_handle_t s_ldo_io = NULL;
#endif

static void powerIoDomain() {
#if ESP_ARDUINO_VERSION < ESP_ARDUINO_VERSION_VAL(3, 3, 9)
  esp_ldo_channel_config_t cfg = {};
  cfg.chan_id    = 4;
  cfg.voltage_mv = 3300;
  esp_err_t err = esp_ldo_acquire_channel(&cfg, &s_ldo_io);
  Serial.printf("[P4_C6] LDO canale 4 (GPIO39..48) a 3,3 V -> %s\n", esp_err_to_name(err));
  delay(5);  // tempo alla tensione di salire prima di usare i pin
#endif
}

void setup() {
  Serial.begin(115200);
  delay(300);  // da' tempo al monitor seriale di agganciarsi dopo il reset
  Serial.println("[P4_C6] setup() avviato");

  powerIoDomain();  // PRIMA di toccare i pin 46 e 48

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  uiInit();
  Serial.println("[P4_C6] display inizializzato");
  uiStatus("Avvio...", COL_TEXT);

  if (!initLoaderPort()) {
    uiError("Errore inizializzazione porta seriale verso il C6.");
    return;
  }

  uiStatus("Controllo versione sul C6...", COL_TEXT);
  if (c6AlreadyUpToDate()) {
    releaseC6ToRun();
    uiAlreadyUpToDate();
    return;
  }

  doFullReflash();
}

void loop() {
  if (checkButtonPressed()) {
    Serial.println("[P4_C6] pulsante premuto: riprogrammazione forzata richiesta");
    uiClearBody();
    doFullReflash();
  }
  delay(20);
}
