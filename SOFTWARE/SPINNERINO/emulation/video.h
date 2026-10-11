#ifndef VIDEO_H
#define VIDEO_H

#include <Arduino.h>
#include <driver/spi_master.h>
#include "../config.h"

class Video {
public:
  Video();
  void begin(void);
  void write(uint16_t *colors, uint32_t len);
  // Attende il completamento della transazione SPI DMA in sospeso (write() e'
  // asincrona: lascia una transazione in volo, reaped solo dalla write successiva).
  // Da chiamare PRIMA di operazioni che non toccano il display ma che potrebbero
  // confliggere con la DMA SPI in corso (es. accesso microSD/SDMMC).
  void waitDMA(void);
  void flipVertical(char flip);
  void flipHorizontal(char flip);
  void setLandscape(bool enable);
  // Cambia orientamento HW: portrait (240x320 nativo per splash/menu) o
  // landscape (320x240 con MAC=0x60 per il game). Setta MADCTL + addr window.
  void setOrientation(bool portrait);
  // Commuta il pannello in PORTRAIT (on=true) per i giochi landscape (Green
  // Beret), come FULL33: MADCTL = GB_PORTRAIT_MADCTL + finestra 240x288. on=false
  // ripristina il landscape trasposto normale (MADCTL TFT_MAC + finestra FB).
  void setLandscapeGame(bool on);
  // Disegna un overlay (buf = 240 x h px, RGB565 byte-swapped) nel PADDING NERO
  // in cima al pannello, SOPRA la finestra dei giochi landscape (che parte a
  // GB_PORTRAIT_Y). Drena la DMA in corso, scrive sincrono, poi RIPRISTINA la
  // finestra di gioco landscape. Usato per la barra volume cosi' resta a filo
  // del bordo come nei giochi normali (path trasposto). NON applica mirror_x.
  void blitTopPadding(const uint16_t *buf, int h);
  bool mirror_x = false;
  bool _landscape = false;
  void setAddrWindow(uint16_t x, uint16_t y, uint16_t w, uint16_t h);
  void write16(uint16_t u16);
  void writeCommand(uint8_t cmd);
  void sendCommand(uint8_t commandByte, uint8_t *dataBytes, uint8_t numDataBytes);
  void write8(uint8_t u8);
  void setBrightness(uint8_t level);  // 0=min, 10=max
  uint8_t brightnessLevel = 7;        // default level (0-10)

private:
  uint8_t _bl_duty = 180;             // PWM duty 0-255 (default ~70%)
  char dma_active;
  spi_device_handle_t handle;
  spi_transaction_t* r_trans;
  spi_transaction_t transaction;
  unsigned char *dma_buffer;  // use a second buffer for dma transfers
};

// ============================================================================
// Barra del VOLUME per i player AUTONOMI (1944, Ghouls'n Ghosts, Out Run).
//
// ⛔ Perche' non basta disegnarla dentro il loro framebuffer: quei player hanno
// la finestra SPI STRETTA sulle sole colonne del gioco (le bande nere non si
// rispediscono ad ogni quadro, vale il 30% del traffico), e la banda in cima al
// pannello — dove sta la barra di TUTTI gli altri giochi — e' FUORI da quella
// finestra. In piu' il loro blit RIDUCE l'immagine in orizzontale (384->240 o
// 320->240), quindi una barra disegnata nel framebuffer esce di dimensione
// diversa da gioco a gioco. Qui la barra si scrive direttamente nella banda, in
// pixel di PANNELLO, con la geometria IDENTICA agli altri giochi: 112 px di
// lunghezza, 16 di spessore, bordo bianco, riempimento verde da sinistra,
// ultimi 10 px (verso il MASSIMO) rossi — vedi draw_volume_overlay() e
// fill_volume_bar_portrait() in SPINNERINO.ino.
//   strip     buffer di lavoro del player (servono almeno 1920 pixel)
//   attiva    false = ridipinge la banda di NERO (uscita dal vol mode)
//   vol_1_30  laserdisc_current_volume(): 1..30, **1 = MASSIMO**
// ⚠️ Lascia la finestra SPI cambiata: il chiamante deve ridarla al gioco.
// ⚠️ Da chiamare solo quando qualcosa CAMBIA (ingresso/uscita dal vol mode o
//    riempimento diverso): ridisegnarla ad ogni quadro sarebbero 7,7 KB di SPI
//    buttati e un cambio di finestra in piu'.
void videoVolBarTop(uint16_t *strip, bool attiva, int vol_1_30);

#endif // VIDEO_H
