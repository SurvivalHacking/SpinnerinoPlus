/*
 * ============================================================================
 * SPINNERINO - C6_BLE_Bridge (ESP32-C6 companion)
 * ============================================================================
 *
 * Firmware per l'ESP32-C6 onboard della scheda Waveshare ESP32-P4-WIFI6.
 * Fa da BLE HID Host: si connette a un gamepad BLE (Xbox, PS, 8BitDo,
 * ShanWan/Q36, Terios, generici HID) e invia lo stato dei pulsanti al
 * P4 via UART. Versione BT-only (niente WiFi/web): emette SOLO pacchetti
 * gamepad, quindi il lato P4 resta semplicissimo.
 *
 * Protocollo UART (115200 baud, 8N1):
 *   [0xAA][buttons][flags][checksum]
 *   - buttons: 8 bit (LEFT|RIGHT|UP|DOWN|FIRE|START|COIN|EXTRA)
 *   - flags:   bit0 = Y button, bit1 = gamepad connesso
 *   - checksum: XOR di byte 0..2
 *
 * Cablaggio verso il P4 (header H4 "ESP32-C6 UART Pads" = IO9 | GND | RXD | TXD):
 *   C6 pad "TXD" (GPIO16) OPPURE pad "RXD" (GPIO17) --ponticello--> P4 GPIO28 (RX)
 *   GND comune (gia' condivisa sulla scheda).
 *   Il C6 trasmette lo stesso stream su ENTRAMBI i pad (GPIO16 via UART0 e
 *   GPIO17 via UART1): qualunque dei due tu abbia cablato al P4 GPIO28 funziona.
 *
 * Arduino IDE:
 *   Board:            "ESP32C6 Dev Module"
 *   USB CDC On Boot:  DISABLED   <-- OBBLIGATORIO! Cosi' Serial = UART0 (GPIO16).
 *                                    Se ENABLED, Serial va sull'USB e NON esce
 *                                    nulla dai pin -> il P4 non riceve niente.
 *   Librerie:         NimBLE-Arduino by h2zero (Library Manager)
 *
 * IMPORTANTE: il protocollo binario viaggia su Serial (UART0, GPIO16 TX).
 * NON usare Serial.print/println in testo: corromperebbe il binario verso il
 * P4. I log ESP-IDF (NimBLE) finiscono su UART0 -> disabilitati in setup().
 *
 * Parser HID derivato da ble_gamepad.cpp di GALAGONE (collaudato).
 * ============================================================================
 */

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <EEPROM.h>
#include <esp_mac.h>
#include <esp_log.h>

// ============================================================================
// BUTTON FLAGS (identici al P4 - emulation/input.h)
// ============================================================================
#define BUTTON_LEFT   0x01
#define BUTTON_RIGHT  0x02
#define BUTTON_UP     0x04
#define BUTTON_DOWN   0x08
#define BUTTON_FIRE   0x10
#define BUTTON_START  0x20
#define BUTTON_COIN   0x40
#define BUTTON_EXTRA  0x80

// ============================================================================
// PROTOCOLLO UART
// ============================================================================
#define PROTO_SYNC      0xAA
#define PROTO_PKT_LEN   4
#define FLAG_Y_BUTTON   0x01
#define FLAG_CONNECTED  0x02

// UART verso il P4: identico a MARCO/GALAGONE (collaudato). Usa il classico
// "Serial" = UART0 = GPIO16 (U0TXD) / GPIO17 (U0RXD). Il C6 trasmette su
// GPIO16, che corrisponde al pad "TXD" dell'header H4 -> P4 GPIO28.
// REQUISITO IDE: "USB CDC On Boot = DISABLED", altrimenti Serial va sull'USB
// e NON esce nulla dai pin fisici (era la causa probabile dei tentativi falliti).
#define UART_TO_P4      Serial

// FIX AMBIGUITA' PAD (2026-06-12): l'header H4 ha sia "TXD"(GPIO16) sia
// "RXD"(GPIO17). Se il ponticello verso P4 GPIO28 e' stato messo sul pad RXD,
// la TX su GPIO16 non arriva mai -> pairing OK ma menu muto. Soluzione robusta:
// trasmettiamo lo STESSO stream anche su UART1 TX = GPIO17 (RX disabilitato).
// Cosi' il P4 riceve qualunque dei due pad sia cablato. RX di UART0 (GPIO17)
// viene sovrascritto come output: innocuo, tanto non leggiamo nulla dal P4.
static HardwareSerial UartAlt(1);
#define UART_ALT_TX_PIN 17

// ============================================================================
// CONFIG
// ============================================================================
#define STATUS_LED_PIN        8     // LED integrato C6 dev board (-1 = off)
#define BLE_SCAN_TIME         0     // 0 = scan indefinito
#define BLE_STICK_DEADZONE    50
#define BLE_HID_SVC           "1812"
#define BLE_REPORT_CHR        "2a4d"
#define BLE_PROTOCOL_MODE     "2a4e"
#define BLE_MAX_RETRIES       3

// EEPROM: salva indirizzo ultimo gamepad
#define EEPROM_SIZE           32
#define EEPROM_GP_TYPE_ADDR   0
#define EEPROM_GP_STR_START   1
#define EEPROM_GP_STR_LEN     18    // "XX:XX:XX:XX:XX:XX" + null
#define EEPROM_GP_MAGIC       19
#define EEPROM_GP_MAGIC_VAL   0xBE

// ============================================================================
// STATO
// ============================================================================
static volatile unsigned char g_buttons   = 0;
static volatile bool          g_connected  = false;
static volatile bool          g_y_pressed  = false;
static volatile bool          g_initialized = false;
static volatile bool          g_connecting = false;
static NimBLEClient   *g_client     = nullptr;
static NimBLEAddress  *g_targetAddr = nullptr;
static uint8_t         g_retryCount = 0;

// ============================================================================
// EEPROM: salva/carica indirizzo gamepad
// ============================================================================
static void saveGamepadAddr(const NimBLEAddress &addr) {
  uint8_t type = addr.getType();
  std::string s = addr.toString();
  EEPROM.write(EEPROM_GP_TYPE_ADDR, type);
  for (int i = 0; i < EEPROM_GP_STR_LEN && i < (int)s.length() + 1; i++)
    EEPROM.write(EEPROM_GP_STR_START + i, i < (int)s.length() ? s[i] : 0);
  EEPROM.write(EEPROM_GP_MAGIC, EEPROM_GP_MAGIC_VAL);
  EEPROM.commit();
}

static bool loadGamepadAddr(NimBLEAddress &addr) {
  if (EEPROM.read(EEPROM_GP_MAGIC) != EEPROM_GP_MAGIC_VAL) return false;
  uint8_t type = EEPROM.read(EEPROM_GP_TYPE_ADDR);
  char buf[EEPROM_GP_STR_LEN];
  for (int i = 0; i < EEPROM_GP_STR_LEN; i++)
    buf[i] = EEPROM.read(EEPROM_GP_STR_START + i);
  buf[EEPROM_GP_STR_LEN - 1] = 0;
  addr = NimBLEAddress(std::string(buf), type);
  return true;
}

// ============================================================================
// INVIO PACCHETTO UART (solo dati binari su Serial!)
// ============================================================================
static void sendUartPacket(unsigned char buttons, unsigned char flags) {
  uint8_t pkt[PROTO_PKT_LEN];
  pkt[0] = PROTO_SYNC;
  pkt[1] = buttons;
  pkt[2] = flags;
  pkt[3] = pkt[0] ^ pkt[1] ^ pkt[2];   // checksum XOR
  UART_TO_P4.write(pkt, PROTO_PKT_LEN);   // UART0 -> GPIO16 (pad TXD)
  UartAlt.write(pkt, PROTO_PKT_LEN);      // UART1 -> GPIO17 (pad RXD), ridondante
}

// ============================================================================
// FORMAT DETECTION + HID REPORT PARSER (da ble_gamepad.cpp di GALAGONE)
// ============================================================================
enum GpFormat { FMT_UNKNOWN, FMT_STANDARD_8, FMT_XBOX_BLE, FMT_LONG_GENERIC, FMT_Q36_10 };
static GpFormat g_format = FMT_UNKNOWN;

static bool isKnownGamepad(const std::string &name) {
  if (name.empty()) return false;
  const char *known[] = {
    "Q36", "ShanWan", "shanwan", "Terios", "GameSir", "Xbox", "XBOX",
    "Wireless Controller", "Pro Controller", "8BitDo", "80EL",
    "GamePad", "Gamepad", "gamepad", "Controller", "Joystick", NULL
  };
  for (int i = 0; known[i]; i++)
    if (name.find(known[i]) != std::string::npos) return true;
  return false;
}

static unsigned char parseHat(uint8_t hat) {
  unsigned char b = 0;
  if (hat == 0 || hat == 1 || hat == 7) b |= BUTTON_UP;
  if (hat == 1 || hat == 2 || hat == 3) b |= BUTTON_RIGHT;
  if (hat == 3 || hat == 4 || hat == 5) b |= BUTTON_DOWN;
  if (hat == 5 || hat == 6 || hat == 7) b |= BUTTON_LEFT;
  return b;
}

static unsigned char parseStick8(uint8_t x, uint8_t y) {
  unsigned char b = 0;
  if (x < (128 - BLE_STICK_DEADZONE)) b |= BUTTON_LEFT;
  if (x > (128 + BLE_STICK_DEADZONE)) b |= BUTTON_RIGHT;
  if (y < (128 - BLE_STICK_DEADZONE)) b |= BUTTON_UP;
  if (y > (128 + BLE_STICK_DEADZONE)) b |= BUTTON_DOWN;
  return b;
}

static unsigned char parseReport(const uint8_t *data, size_t len) {
  if (len < 2) return 0;
  unsigned char buttons = 0;
  // ⛔⛔⛔ ATTENZIONE: QUESTA CORREZIONE NON E' MAI STATA FLASHATA SUL C6.
  // Scelta dell'utente (2026-08-11): «non voglio flashare il C6». Il sorgente
  // qui e' quindi PIU' AVANTI del firmware che gira davvero sul ponte. Metal
  // Slug intanto usa START come terzo tasto (vedi ng_read_inputs in neogeo.cpp).
  // Se un giorno si flasha il C6, la granata funziona anche su Y senza toccare
  // nient'altro.
  //
  // ⛔⛔ IL TASTO Y ARRIVAVA DA UN SOLO FORMATO SU QUATTRO (2026-08-11).
  // `g_y_pressed` era impostato **solo** dentro FMT_Q36_10: con un pad Xbox BLE
  // (o STANDARD_8, o LONG_GENERIC) restava false per sempre, e sul lato P4
  // `Input::button_y_pressed()` — che esiste, compila e si legge benissimo —
  // non diventava mai vera. E' il difetto di sempre: una funzione che ESISTE
  // non e' un dato che ARRIVA; va guardato CHI SCRIVE il flag, non chi lo legge.
  // Sintomo: su Metal Slug la granata non partiva dal gamepad, e nient'altro.
  // ✅ Ora `y` si calcola in OGNI formato e si assegna UNA VOLTA in fondo, cosi'
  // non puo' nemmeno restare incollata a true quando si rilascia il tasto.
  bool y = false;

  if (g_format == FMT_UNKNOWN) {
    if (len >= 14 && data[0] == 0x01)  g_format = FMT_XBOX_BLE;
    else if (len == 10)                g_format = FMT_Q36_10;
    else if (len == 8)                 g_format = FMT_STANDARD_8;
    else if (len >= 10)                g_format = FMT_LONG_GENERIC;
    else                               g_format = FMT_STANDARD_8;
  }

  switch (g_format) {
    case FMT_STANDARD_8:
      if (len >= 5) {
        buttons |= parseStick8(data[0], data[1]);
        buttons |= parseHat(data[4] & 0x0F);
        if (data[4] & 0x10) buttons |= BUTTON_FIRE;
        if (data[4] & 0x20) buttons |= BUTTON_EXTRA;
        if (data[4] & 0x80) y = true;              // Y (0x40 = X, non usato)
        if (len >= 6) {
          if (data[5] & 0x04) buttons |= BUTTON_COIN;
          if (data[5] & 0x08) buttons |= BUTTON_START;
        }
      }
      break;

    case FMT_XBOX_BLE:
      if (len >= 14) {
        uint8_t btnsLo = data[1];
        buttons |= parseHat(data[3]);
        int16_t lx = (int16_t)(data[4] | (data[5] << 8));
        int16_t ly = (int16_t)(data[6] | (data[7] << 8));
        uint8_t nx = (uint8_t)((lx >> 8) + 128);
        uint8_t ny = (uint8_t)((ly >> 8) + 128);
        buttons |= parseStick8(nx, ny);
        if (btnsLo & 0x01) buttons |= BUTTON_FIRE;    // A
        if (btnsLo & 0x02) buttons |= BUTTON_EXTRA;   // B
        if (btnsLo & 0x04) buttons |= BUTTON_FIRE;    // X
        // ⭐ Y: bit 3, la posizione standard dell'HID Xbox — coerente col fatto
        // che qui sopra il bit 2 e' gia' trattato come X. E' il tasto della
        // GRANATA di Metal Slug. ⚠️ NON alza anche BUTTON_FIRE, o premendo la
        // granata partirebbe pure lo sparo (lo stesso difetto gia' corretto sul
        // lato P4, dove due tasti finivano nello stesso bit).
        if (btnsLo & 0x08) y = true;                  // Y
        if (btnsLo & 0x40) buttons |= BUTTON_COIN;
        if (btnsLo & 0x80) buttons |= BUTTON_START;
      }
      break;

    case FMT_Q36_10:
      if (len >= 7) {
        buttons |= parseStick8(data[0], data[1]);
        uint8_t hat = data[4];
        if (hat <= 7) buttons |= parseHat(hat);
        uint8_t b1 = data[5];
        uint8_t b2 = data[6];
        if (b1 & 0x01) buttons |= BUTTON_FIRE;                          // A
        if (b1 & 0x02) buttons |= BUTTON_EXTRA;                         // B
        if (b1 & 0x04) buttons |= BUTTON_FIRE;                          // X
        // ⚠️ Qui Y alza ANCHE BUTTON_FIRE, e resta com'era: e' il comportamento
        // storico di questo formato e altri giochi ci contano. Su Metal Slug
        // significa che con un pad Q36 la granata spara anche; con un Xbox no.
        if (b1 & 0x08) { buttons |= BUTTON_FIRE; y = true; }             // Y
        if (b1 & 0x10) buttons |= BUTTON_FIRE;                          // L1
        if (b1 & 0x20) buttons |= BUTTON_FIRE;                          // R1
        if (b1 & 0x40) buttons |= BUTTON_FIRE;                          // L2
        if (b1 & 0x80) buttons |= BUTTON_FIRE;                          // R2
        if (b2 & 0x04) buttons |= BUTTON_COIN;                          // Select
        if (b2 & 0x08) buttons |= BUTTON_START;                         // Start
      }
      break;

    case FMT_LONG_GENERIC:
      if (len >= 7) {
        buttons |= parseStick8(data[0], data[1]);
        buttons |= parseHat(data[6]);
        if (data[4] & 0x01) buttons |= BUTTON_FIRE;
        if (data[4] & 0x02) buttons |= BUTTON_EXTRA;
        if (data[4] & 0x08) y = true;              // Y (0x04 = X, non usato)
        if (data[5] & 0x10) buttons |= BUTTON_COIN;
        if (data[5] & 0x20) buttons |= BUTTON_START;
      }
      break;

    default: break;
  }
  g_y_pressed = y;      // UNA sola assegnazione: non puo' restare incollata
  return buttons;
}

// ============================================================================
// CALLBACK NOTIFICA HID
// ============================================================================
static void notifyCallback(NimBLERemoteCharacteristic *chr, uint8_t *data,
                           size_t len, bool isNotify) {
  g_buttons = parseReport(data, len);
}

// ============================================================================
// SCAN + CLIENT CALLBACK
// ============================================================================
class ScanCB : public NimBLEScanCallbacks {
  void onResult(const NimBLEAdvertisedDevice *dev) override {
    bool hasHID = dev->isAdvertisingService(NimBLEUUID(BLE_HID_SVC));
    bool knownName = isKnownGamepad(dev->getName());
    if (hasHID || knownName) {
      NimBLEDevice::getScan()->stop();
      if (g_targetAddr) delete g_targetAddr;
      g_targetAddr = new NimBLEAddress(dev->getAddress());
      g_retryCount = 0;
    }
  }
};

class ClientCB : public NimBLEClientCallbacks {
  void onConnect(NimBLEClient *client) override { g_connected = true; }
  void onDisconnect(NimBLEClient *client, int reason) override {
    g_connected = false;
    g_buttons   = 0;
    g_format    = FMT_UNKNOWN;
    if (!g_initialized) return;
    if (g_targetAddr) { delete g_targetAddr; g_targetAddr = nullptr; }
    g_retryCount = 0;
    if (!g_connecting) NimBLEDevice::getScan()->start(BLE_SCAN_TIME, false);
  }
};

// ============================================================================
// CONNESSIONE + SUBSCRIBE
// ============================================================================
static void tryConnect() {
  if (!g_targetAddr) return;

  if (g_client == nullptr) {
    g_client = NimBLEDevice::createClient();
    g_client->setClientCallbacks(new ClientCB(), false);
  }

  if (!g_client->connect(*g_targetAddr, false)) {
    g_retryCount++;
    if (g_retryCount >= BLE_MAX_RETRIES) {
      NimBLEDevice::deleteBond(*g_targetAddr);
      delete g_targetAddr; g_targetAddr = nullptr;
      g_retryCount = 0;
      NimBLEDevice::getScan()->start(BLE_SCAN_TIME, false);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
    return;
  }

  g_client->updateConnParams(12, 24, 0, 400);   // bassa latenza
  g_retryCount = 0;

  g_client->secureConnection();                  // "Just Works"
  vTaskDelay(pdMS_TO_TICKS(300));

  NimBLERemoteService *hidSvc = g_client->getService(NimBLEUUID(BLE_HID_SVC));
  if (!hidSvc) { g_client->disconnect(); return; }

  NimBLERemoteCharacteristic *protMode =
      hidSvc->getCharacteristic(NimBLEUUID(BLE_PROTOCOL_MODE));
  if (protMode && protMode->canWrite()) {
    uint8_t reportProto = 0x01;
    protMode->writeValue(&reportProto, 1, true);
  }
  vTaskDelay(pdMS_TO_TICKS(100));

  int subscribed = 0;
  auto chars = hidSvc->getCharacteristics(true);
  for (auto &chr : chars) {
    if (chr->getUUID() == NimBLEUUID(BLE_REPORT_CHR) && chr->canNotify()) {
      vTaskDelay(pdMS_TO_TICKS(100));
      if (chr->subscribe(true, notifyCallback)) subscribed++;
    }
  }

  if (subscribed > 0) {
    if (g_targetAddr) saveGamepadAddr(*g_targetAddr);
  } else {
    g_client->disconnect();
  }
}

static void connectTask(void *param) {
  tryConnect();
  g_connecting = false;
  vTaskDelete(NULL);
}

// ============================================================================
// SETUP
// ============================================================================
void setup() {
  // CRITICO: spegni i log ESP-IDF (NimBLE) PRIMA di tutto: scrivono su UART0
  // e corromperebbero il protocollo binario verso il P4.
  esp_log_level_set("*", ESP_LOG_NONE);

  // UART0 classico (come MARCO): TX=GPIO16 (pad TXD) -> P4 GPIO28.
  // Richiede "USB CDC On Boot = DISABLED" nell'IDE.
  UART_TO_P4.begin(115200);
  // UART1 ridondante: TX=GPIO17 (pad RXD), RX disabilitato (-1). Copre il caso
  // in cui il ponticello al P4 GPIO28 sia stato messo sul pad RXD anziche' TXD.
  UartAlt.begin(115200, SERIAL_8N1, -1, UART_ALT_TX_PIN);

#if STATUS_LED_PIN >= 0
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);
#endif

  EEPROM.begin(EEPROM_SIZE);

  NimBLEDevice::init("SPINNERINO_C6");

  if (!NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_PUBLIC)) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    mac[5] |= 0xC0;                              // random-static (2 MSB = 11)
    NimBLEDevice::setOwnAddr(mac);
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  }

  // Pulisci i bond vecchi (chiavi stantie impediscono la riconnessione).
  int numBonds = NimBLEDevice::getNumBonds();
  for (int i = numBonds - 1; i >= 0; i--)
    NimBLEDevice::deleteBond(NimBLEDevice::getBondedAddress(i));

  NimBLEDevice::setSecurityAuth(true, false, true);          // Just Works
  NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);

  NimBLEScan *scan = NimBLEDevice::getScan();
  scan->setScanCallbacks(new ScanCB(), false);
  scan->setActiveScan(true);
  scan->setInterval(80);                         // 50 ms
  scan->setWindow(40);                           // 25 ms (50% duty)

  NimBLEAddress savedAddr;
  loadGamepadAddr(savedAddr);                    // match in scan se presente

  scan->start(BLE_SCAN_TIME, false);
  g_initialized = true;
}

// ============================================================================
// LOOP - invia stato pulsanti via UART ogni 20 ms (o subito se cambia)
// ============================================================================
static unsigned long lastSendTime    = 0;
static unsigned char lastSentButtons = 0xFF;     // forza primo invio
static unsigned char lastSentFlags   = 0xFF;

void loop() {
  if (g_targetAddr && !g_connected && !g_connecting) {
    g_connecting = true;
    xTaskCreatePinnedToCore(connectTask, "bleConn", 8192, NULL, 1, NULL, 0);
  }

  unsigned char flags = 0;
  if (g_y_pressed) flags |= FLAG_Y_BUTTON;
  if (g_connected) flags |= FLAG_CONNECTED;

  unsigned char buttons = g_buttons;
  unsigned long now = millis();

  if (buttons != lastSentButtons || flags != lastSentFlags ||
      (now - lastSendTime >= 20)) {
    sendUartPacket(buttons, flags);
    lastSentButtons = buttons;
    lastSentFlags   = flags;
    lastSendTime    = now;
  }

#if STATUS_LED_PIN >= 0
  if (g_connected)        digitalWrite(STATUS_LED_PIN, HIGH);            // fisso = connesso
  else if (g_targetAddr)  digitalWrite(STATUS_LED_PIN, (now / 100) & 1);// blink veloce = connessione
  else                    digitalWrite(STATUS_LED_PIN, (now / 500) & 1);// blink lento = scan
#endif

  delay(5);
}
