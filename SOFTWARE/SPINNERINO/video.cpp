#include "emulation/video.h"

#ifndef TFT_PANEL_OFFSET
#define TFT_PANEL_OFFSET 0
#endif

static spi_device_interface_config_t if_cfg {
  .command_bits = 0,
  .address_bits = 0,
  .dummy_bits = 0,
  .mode = SPI_MODE0,
  .duty_cycle_pos = 128,
  .cs_ena_pretrans = 0,
  .cs_ena_posttrans = 0,
  .clock_speed_hz = TFT_SPICLK,
  .input_delay_ns = 0,
  .spics_io_num = -1,
  .flags = SPI_DEVICE_HALFDUPLEX,
  .queue_size = 3,
  .pre_cb = NULL,
  .post_cb = NULL,
};

#if !defined(TFT_MISO)
#define TFT_MISO -1
#endif
#ifndef TFT_MOSI
#define TFT_MOSI MOSI
#endif
#ifndef TFT_SCLK
#define TFT_SCLK SCK
#endif
#ifndef TFT_MAC
#define TFT_MAC 0xC0
#endif

// SPINNERINO SH: dma buffer = TFT_FB_W (window width post-HW-offset)
#ifndef ARK_FB_WIDTH
  #define ARK_FB_WIDTH TFT_FB_W
#endif

spi_bus_config_t bus_cfg{
    .mosi_io_num = TFT_MOSI,
    .miso_io_num = TFT_MISO,
    .sclk_io_num = TFT_SCLK,
    .max_transfer_sz = ARK_FB_WIDTH * 8 * 2,
    .flags = SPICOMMON_BUSFLAG_MASTER,
};

#define W16(a)    (a>>8), (a&0xff)

// ============================================================================
// Init sequence: ST7789 (240x240/240x320) oppure ILI9341 (320x240)
// Selezione via #define USE_ILI9341 in config.h (default ST7789)
// ============================================================================
#ifdef USE_ILI9341
// ----- ILI9341 init (per pannelli 2.4" 320x240) -----
static const uint8_t init_cmd[] = {
  0x01, 0,                                                      // SWRESET
  0xff, 150,
  0xCF, 3, 0x00, 0xC1, 0x30,                                    // Power Control B
  0xED, 4, 0x64, 0x03, 0x12, 0x81,                              // Power on Sequence
  0xE8, 3, 0x85, 0x00, 0x78,                                    // Driver Timing Control A
  0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,                        // Power Control A
  0xF7, 1, 0x20,                                                // Pump Ratio Control
  0xEA, 2, 0x00, 0x00,                                          // Driver Timing Control B
  0xC0, 1, 0x23,                                                // Power Control 1
  0xC1, 1, 0x10,                                                // Power Control 2
  0xC5, 2, 0x3E, 0x28,                                          // VCOM Control 1
  0xC7, 1, 0x86,                                                // VCOM Control 2
  0x36, 1, TFT_MAC,                                             // MADCTL (orientation)
  0x3A, 1, 0x55,                                                // COLMOD: 16-bit RGB565
  0xB1, 2, 0x00, 0x18,                                          // Frame Rate Control
  0xB6, 3, 0x08, 0x82, 0x27,                                    // Display Function Control
  0xF2, 1, 0x00,                                                // 3Gamma Disable
  0x26, 1, 0x01,                                                // Gamma curve
  0xE0, 15, 0x0F,0x31,0x2B,0x0C,0x0E,0x08,0x4E,0xF1,
            0x37,0x07,0x10,0x03,0x0E,0x09,0x00,                 // Positive Gamma
  0xE1, 15, 0x00,0x0E,0x14,0x03,0x11,0x07,0x31,0xC1,
            0x48,0x08,0x0F,0x0C,0x31,0x36,0x0F,                 // Negative Gamma
  0x11, 0,                                                      // SLPOUT
  0xff, 120,
  0x29, 0,                                                      // DISPON
  0xff, 50,
  0x00
};
#else
// ----- ST7789 init (per pannelli 240x240 / 240x320 / 320x240 landscape) -----
// Init essenziale, compatibile con la maggior parte dei moduli ST7789 cinesi.
// INVON e' tipico dei pannelli IPS; per TFT non-IPS produce colori invertiti.
// Lo lasciamo controllabile via TFT_INVERT.
static const uint8_t init_cmd[] = {
  0x01, 0,                                                      // SWRESET
  0xff, 150,
  0x11, 0,                                                      // SLPOUT
  0xff, 120,
  0x3a, 1, 0x55,                                                // COLMOD: 16-bit RGB565
  0xff, 10,
  0x36, 1, TFT_MAC,                                             // MADCTL
#ifdef TFT_INVERT
  0x21, 0,                                                      // INVON  (IPS)
#else
  0x20, 0,                                                      // INVOFF (TFT non-IPS)
#endif
  0xff, 10,
  // ===== FIX ESP-IDF #15332 adattato a LANDSCAPE (MAC ha MV=1) =====
  // In landscape post-MADCTL: CASET copre 320 col (0x13F), RASET 240 righe (0xEF).
  // Senza questi, alcuni ST7789V hanno offset persistente.
  0x2A, 4, 0x00, 0x00, 0x01, 0x3F,                              // CASET 0..319
  0x2B, 4, 0x00, 0x00, 0x00, 0xEF,                              // RASET 0..239
  0xff, 10,
  // --- Power / VCOM / Gamma (valori standard da datasheet ST7789) ---
  0xB2, 5, 0x0C, 0x0C, 0x00, 0x33, 0x33,
  0xB7, 1, 0x35,
  0xBB, 1, 0x19,                                                // VCOM
  0xC0, 1, 0x2C,
  0xC2, 1, 0x01,
  0xC3, 1, 0x12,
  0xC4, 1, 0x20,
  // FRCTRL2 (RTNA) = frequenza di scansione interna del pannello. 0x0F = 60 Hz.
  // ⛔ NON METTERLO A 0x00 (111 Hz): provato il 2026-08-04 per attenuare il
  // tearing e il risultato sul device e' stato **schermo NERO** (il firmware
  // girava — l'audio si sentiva — ma il pannello non mostrava piu' niente).
  // Questo modulo non regge quel valore. Se si vuole riprovare, muoversi di un
  // passo per volta (0x0E, 0x0D...) e mai insieme ad altre modifiche.
  0xC6, 1, 0x0F,
  0xD0, 2, 0xA4, 0xA1,
  0xE0, 14, 0xD0,0x04,0x0D,0x11,0x13,0x2B,0x3F,0x54,
            0x4C,0x18,0x0D,0x0B,0x1F,0x23,
  0xE1, 14, 0xD0,0x04,0x0C,0x11,0x13,0x2C,0x3F,0x44,
            0x51,0x2F,0x1F,0x1F,0x20,0x23,
  0x13, 0,                                                      // NORON
  0xff, 10,
  0x29, 0,                                                      // DISPON
  0xff, 50,
  0x00
};
#endif // USE_ILI9341


Video::Video() {
  dma_active = 0;
  dma_buffer = NULL;
  handle = NULL;
}

void Video::begin(void) {
  pinMode(TFT_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  pinMode(TFT_DC, OUTPUT);
  digitalWrite(TFT_DC, HIGH);

  dma_buffer = (unsigned char*)heap_caps_malloc(ARK_FB_WIDTH * 8 * 2, MALLOC_CAP_DMA);

  spi_bus_initialize(SPI2_HOST, &bus_cfg, SPI_DMA_CH_AUTO);
  spi_bus_add_device(SPI2_HOST, &if_cfg, &handle);

  if (TFT_RST >= 0) {
    pinMode(TFT_RST, OUTPUT);
    digitalWrite(TFT_RST, LOW);
    delay(100);
    digitalWrite(TFT_RST, HIGH);
    delay(200);
  } else {
    sendCommand(0x01, NULL, 0);
    delay(150);
  }

  uint8_t cmd;
  const uint8_t *addr = init_cmd;
  while(cmd = *addr++) {
    const uint8_t num = *addr++;
    if(cmd != 0xff) {
      sendCommand(cmd, (uint8_t*)addr, num);
      addr += num;
    } else
      delay(num);
  }

  // Clear schermo a NERO su tutto il pannello (320 wide x 240 tall in landscape)
  digitalWrite(TFT_CS, LOW);
  setAddrWindow(0, TFT_PANEL_OFFSET, 320, 240);
  memset(dma_buffer, 0, ARK_FB_WIDTH * 8 * 2);
  // 320*240 px = 76800 px; mandiamo a chunk di ARK_FB_WIDTH*8 = 2048 px
  for (int i = 0; i < (320 * 240) / (ARK_FB_WIDTH * 8); i++) {
    transaction.flags = 0;
    transaction.length = ARK_FB_WIDTH * 8 * 16;
    transaction.tx_buffer = (const void *)dma_buffer;
    spi_device_transmit(handle, &transaction);
  }

  // Window con offset HARDWARE: parte da CASET=TFT_HW_X (sposta tutto a destra
  // a livello controller, no clip software). Width = TFT_FB_W.
  setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);

#if defined(TFT_BL) && TFT_BL >= 0
  // PWM backlight: 5kHz, 8-bit resolution (0-255)
  // API LEDC compatibile con core Arduino ESP32 2.x e 3.x
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcAttach(TFT_BL, 5000, 8);
    ledcWrite(TFT_BL, _bl_duty);
  #else
    ledcSetup(0, 5000, 8);                 // canale 0
    ledcAttachPin(TFT_BL, 0);
    ledcWrite(0, _bl_duty);
  #endif
#endif
}


void Video::flipVertical(char flip) {
  if (dma_active)
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);

  // Clear screen BEFORE MADCTL change so no old content is reinterpreted
  digitalWrite(TFT_CS, LOW);
  setAddrWindow(0, TFT_PANEL_OFFSET, 240, 240);
  memset(dma_buffer, 0, 240 * 4 * 2);
  for (int i = 0; i < 240 / 4; i++) {
    transaction.flags = 0;
    transaction.length = 240 * 4 * 16;
    transaction.tx_buffer = (const void *)dma_buffer;
    spi_device_transmit(handle, &transaction);
  }

  // Now change MADCTL on a black screen
  writeCommand(0x36);
  write8(flip == 1 ? 0 : TFT_MAC);

  // ST7789 240x240 panel offset: MY=1 needs offset 80, MY=0 needs offset 0
  // (320 - 240 - 80 = 0)
  int panel_y = (flip == 1) ? TFT_Y_OFFSET : (TFT_PANEL_OFFSET + TFT_Y_OFFSET);
  setAddrWindow(TFT_X_OFFSET, panel_y, 224, TFT_SCREEN_H);
  dma_active = 0;
}

void Video::flipHorizontal(char flip) {
  if (dma_active)
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);

  // Clear screen BEFORE MADCTL change so no old content is reinterpreted
  digitalWrite(TFT_CS, LOW);
  setAddrWindow(0, TFT_PANEL_OFFSET, 240, 240);
  memset(dma_buffer, 0, 240 * 4 * 2);
  for (int i = 0; i < 240 / 4; i++) {
    transaction.flags = 0;
    transaction.length = 240 * 4 * 16;
    transaction.tx_buffer = (const void *)dma_buffer;
    spi_device_transmit(handle, &transaction);
  }

  // Now change MADCTL on a black screen
  writeCommand(0x36);
  write8(flip == 1 ? 0x00 : TFT_MAC);
  mirror_x = (flip == 1);

  // ST7789 240x240 panel offset: MY=1 needs offset 80, MY=0 needs offset 0
  // (320 - 240 - 80 = 0)
  int panel_y = (flip == 1) ? TFT_Y_OFFSET : (TFT_PANEL_OFFSET + TFT_Y_OFFSET);
  setAddrWindow(TFT_X_OFFSET, panel_y, 224, TFT_SCREEN_H);
  dma_active = 0;
}

void Video::setOrientation(bool portrait) {
  if (dma_active) {
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
    dma_active = 0;
  }
  digitalWrite(TFT_CS, LOW);

  // 1) Cambio MADCTL PRIMA di tutto (0x00 = portrait nativo, 0x60 = landscape MV+MX).
  uint8_t mac = portrait ? 0x00 : (uint8_t)TFT_MAC;
  sendCommand(0x36, &mac, 1);
  delay(10);    // breve attesa per stabilizzare ST7789 dopo cambio MADCTL

  // 2) Imposta la finestra di scrittura per il nuovo orientamento.
  if (portrait) {
    setAddrWindow(0, 0, 240, 320);          // 240x320 portrait nativo
  } else {
    setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
  }

  // 3) Clear in nero della finestra appena settata (ora dimensioni valide).
  memset(dma_buffer, 0, ARK_FB_WIDTH * 8 * 2);
  int total_pixels  = portrait ? (240 * 320) : (TFT_FB_W * TFT_FB_H);
  int chunk_pixels  = ARK_FB_WIDTH * 8;
  int chunks        = total_pixels / chunk_pixels;
  for (int i = 0; i < chunks; i++) {
    transaction.flags = 0;
    transaction.length = chunk_pixels * 16;
    transaction.tx_buffer = (const void*)dma_buffer;
    spi_device_transmit(handle, &transaction);
  }

  // 4) Re-setta la finestra (auto-wrap al termine, ma per sicurezza la rimettiamo).
  if (portrait) {
    setAddrWindow(0, 0, 240, 320);
  } else {
    setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
  }
}

void Video::setLandscape(bool enable) {
  if (dma_active)
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
  _landscape = enable;
  digitalWrite(TFT_CS, LOW);
  if (enable) {
    setAddrWindow(0, TFT_PANEL_OFFSET, 240, TFT_SCREEN_H);
  } else {
    setAddrWindow(0, TFT_PANEL_OFFSET, 240, 240);
    memset(dma_buffer, 0, 240 * 4 * 2);
    for (int i = 0; i < 240 / 4; i++) {
      transaction.flags = 0;
      transaction.length = 240 * 4 * 16;
      transaction.tx_buffer = (const void *)dma_buffer;
      spi_device_transmit(handle, &transaction);
    }
    setAddrWindow(TFT_X_OFFSET, TFT_PANEL_OFFSET + TFT_Y_OFFSET, ARK_FB_WIDTH, 224);
  }
  dma_active = 0;
}

// Commuta il pannello tra landscape trasposto (normale SPINNERINO) e PORTRAIT
// (per i giochi landscape come Green Beret, identico a FULL33). In portrait il
// pannello e' 240(x) x 320(y) nativo e si scrivono righe orizzontali da 240 px.
void Video::setLandscapeGame(bool on) {
  if (dma_active) {
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
    dma_active = 0;
  }
  _landscape = on;

  digitalWrite(TFT_CS, LOW);

  // Cambio MADCTL tenendo CS basso (pattern di flipHorizontal, testato).
  writeCommand(0x36);
  write8(on ? (uint8_t)GB_PORTRAIT_MADCTL : (uint8_t)TFT_MAC);

  // Clear NERO di tutto il pannello nel nuovo orientamento
  // (portrait 240x320 / landscape 320x240) per togliere residui.
  int pw = on ? 240 : 320;
  int ph = on ? 320 : 240;
  setAddrWindow(0, 0, pw, ph);
  memset(dma_buffer, 0, ARK_FB_WIDTH * 8 * 2);
  int total = pw * ph;
  int chunk = ARK_FB_WIDTH * 8;
  for (int i = 0; i < total / chunk; i++) {
    transaction.flags = 0;
    transaction.length = chunk * 16;
    transaction.tx_buffer = (const void *)dma_buffer;
    spi_device_transmit(handle, &transaction);
  }

  // Finestra di gioco
  if (on)
    setAddrWindow(GB_PORTRAIT_X, GB_PORTRAIT_Y, 240, 288);   // come FULL33 setLandscape
  else
    setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);

  dma_active = 0;
}

// Overlay nel padding nero in cima al pannello (sopra la finestra di gioco
// landscape, che parte a GB_PORTRAIT_Y). Drena la DMA, scrive sincrono il buf
// (240 x h), poi ripristina la finestra di gioco landscape cosi' il prossimo
// frame del render loop continua con l'auto-wrap come prima.
void Video::blitTopPadding(const uint16_t *buf, int h) {
  if (dma_active) {
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
    dma_active = 0;
  }
  // Finestra = padding superiore: x come la finestra di gioco, y da 0 a h-1.
  setAddrWindow(GB_PORTRAIT_X, 0, 240, h);

  const int CAP = ARK_FB_WIDTH * 8;        // capacita' dma_buffer (px)
  int total = 240 * h;
  int off = 0;
  while (off < total) {
    int chunk = total - off;
    if (chunk > CAP) chunk = CAP;
    memcpy(dma_buffer, buf + off, chunk * 2);
    transaction.flags = 0;
    transaction.length = chunk * 16;
    transaction.tx_buffer = (const void *)dma_buffer;
    spi_device_transmit(handle, &transaction);     // sincrono
    off += chunk;
  }

  // Ripristina la finestra di gioco landscape (come setLandscapeGame(true)).
  setAddrWindow(GB_PORTRAIT_X, GB_PORTRAIT_Y, 240, 288);
  dma_active = 0;
}

void Video::write(uint16_t *colors, uint32_t len) {
  if(dma_active)
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);

  memcpy(dma_buffer, colors, 2 * len);

  if (mirror_x) {
    uint16_t *buf = (uint16_t*)dma_buffer;
    int w = ARK_FB_WIDTH;
    int rows = len / w;
    if (rows > 0) {
      for (int row = 0; row < rows; row++) {
        uint16_t *line = buf + row * w;
        for (int x = 0; x < w / 2; x++) {
          uint16_t tmp = line[x];
          line[x] = line[w - 1 - x];
          line[w - 1 - x] = tmp;
        }
      }
    }
  }

  transaction.flags = 0;
  transaction.length = 16 * len;
  transaction.tx_buffer = dma_buffer;
  spi_device_queue_trans(handle, &transaction, portMAX_DELAY);

  if(!dma_active)
    dma_active = 1;
}

void Video::waitDMA(void) {
  if (dma_active) {
    spi_device_get_trans_result(handle, &r_trans, portMAX_DELAY);
    dma_active = 0;
  }
}

void Video::sendCommand(uint8_t commandByte, uint8_t *dataBytes, uint8_t numDataBytes) {
  digitalWrite(TFT_CS, LOW);
  writeCommand(commandByte);
  for(int i=0;i<numDataBytes;i++)
    write8(dataBytes[i]);
  digitalWrite(TFT_CS, HIGH);
}

void Video::writeCommand(uint8_t cmd) {
  digitalWrite(TFT_DC, LOW);
  write8(cmd);
  digitalWrite(TFT_DC, HIGH);
}

void Video::setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  writeCommand(0x2A);
  write16(x);
  write16(x + w - 1);
  writeCommand(0x2B);
  write16(y);
  write16(y + h - 1);
  writeCommand(0x2C);
}

void Video::write16(uint16_t data) {
  transaction.flags = SPI_TRANS_USE_TXDATA;
  transaction.length = 16;
  transaction.rxlength = 0;
  transaction.tx_data[0] = ((data >> 8) & 0xFF);
  transaction.tx_data[1] = data & 0xFF;
  spi_device_transmit(handle, &transaction);
}

void Video::write8(uint8_t data) {
  transaction.flags = SPI_TRANS_USE_TXDATA;
  transaction.length = 8;
  transaction.rxlength = 0;
  transaction.tx_data[0] = data;
  spi_device_transmit(handle, &transaction);
}

void Video::setBrightness(uint8_t level) {
  if (level > 10) level = 10;
  brightnessLevel = level;
  // Map level 0-10 to PWM duty: 10=25, 9=48, ... 0=255
  // Exponential-ish curve for natural feel
  static const uint8_t duty_table[] = { 25, 35, 48, 64, 82, 105, 130, 160, 195, 230, 255 };
  _bl_duty = duty_table[level];
#if defined(TFT_BL) && TFT_BL >= 0
  #if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    ledcWrite(TFT_BL, _bl_duty);  // 3.x: pin
  #else
    ledcWrite(0, _bl_duty);       // 2.x: canale 0
  #endif
#endif
}

// ============================================================================
// videoVolBarTop() — barra del volume per i player AUTONOMI (1944, Ghouls,
// Out Run). Documentazione e motivazione in emulation/video.h.
//
// ⭐ GEOMETRIA IDENTICA agli altri giochi, in pixel di PANNELLO: e' l'unico modo
// perche' sia davvero la stessa barra. I player disegnano il gioco in una
// finestra STRETTA (le sole colonne del gioco) e con una riduzione orizzontale
// diversa l'uno dall'altro, quindi qualunque cosa disegnata nel loro
// framebuffer uscirebbe di misura diversa e nel posto sbagliato.
//
// ⚠️ Il pannello si pilota TRASPOSTO: una "riga" scritta e' lunga TFT_FB_W=320
// (l'asse VERTICALE dell'utente) e ce ne sono 240 (l'asse ORIZZONTALE, con la
// colonna utente = 239 - r). Quindi:
//   - lo SPESSORE della barra sta sull'asse da 320, alle posizioni 0..15 =
//     il bordo ALTO visto dall'utente, dentro la banda nera;
//   - la LUNGHEZZA sta sull'asse da 240, e il riempimento va da r = HI
//     (sinistra utente) verso r = LO (destra utente = MASSIMO).
// ============================================================================
void videoVolBarTop(uint16_t *strip, bool attiva, int vol_1_30)
{
  extern Video video;
  const int TH  = 16;                   // spessore, come gli altri giochi
  const int LEN = 112;                  // lunghezza, come gli altri giochi
  const int RED = 10;                   // px finali verso il MAX, rossi
  const int LO  = (240 - LEN) / 2;      // 64
  const int HI  = LO + LEN - 1;         // 175

  int fill = 0;
  if (attiva) {
    fill = (LEN * (31 - vol_1_30)) / 30;      // 1 = max -> barra piena
    if (fill < 0) fill = 0; else if (fill > LEN) fill = LEN;
  }

  video.waitDMA();
  video.setAddrWindow(TFT_HW_X, 0, TH, 240);
  // 120 righe x 16 px = 1920 pixel per blocco: sta nel dma_buffer (2560) e
  // nella strip di lavoro dei player (320x8 = 2560).
  const int PASSO = 120;
  for (int r0 = 0; r0 < 240; r0 += PASSO) {
    uint16_t *p = strip;
    for (int r = r0; r < r0 + PASSO; r++) {
      const bool dentro  = attiva && (r >= LO && r <= HI);
      const bool bordo_l = dentro && (r == LO || r == HI);
      const bool pieno   = dentro && (r > HI - fill);
      const bool rosso   = pieno  && (r <= LO + RED - 1);
      for (int x = 0; x < TH; x++) {
        uint16_t c = 0x0000;                       // banda nera
        if (dentro) {
          const bool bordo_t = (x == 0) || (x == TH - 1);
          if      (bordo_t || bordo_l) c = 0xFFFF;  // bianco bordo
          else if (rosso)              c = 0x00F8;  // rosso (byte-swap di 0xF800)
          else if (pieno)              c = 0xE007;  // verde (byte-swap di 0x07E0)
        }
        *p++ = c;
      }
    }
    video.write(strip, PASSO * TH);
  }
  video.waitDMA();
}
