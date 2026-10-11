#ifndef SUPXEVS_H
#define SUPXEVS_H
// ============================================================================
// supxevs — Vs. Super Xevious (Namco 1986) su Nintendo VS. UniSystem.
//
// ⚠️ NON E' L'XEVIOUS ARCADE di machines/xevious/: quello e' hardware Namco a
//    3x Z80, questo e' un NES da sala giochi (RP2A03 + PPU RP2C04-0001). Non
//    condividono una riga di codice ne' un byte di ROM. Il «Super Xevious»
//    arcade Namco sarebbe un terzo set ancora (`sxevious`).
//
// Tutta l'emulazione sta in nes_vs.cpp, che non dipende da Arduino e si
// verifica offline con _verify_gfx/supxevs_harness (rende i frame in PNG e
// l'audio in WAV). Questa classe e' solo il raccordo col framework:
// ingressi -> motore, framebuffer -> pannello, ring audio -> audio.cpp.
//
// VIDEO: il gioco e' LANDSCAPE 256x240 (in MAME e' ROT0), al contrario dei
// giochi verticali del progetto. Si usa quindi il percorso "pannello in
// portrait" (usesPortraitPanel), lo stesso di Green Beret e Mario Bros:
// finestra 240x288, righe scritte dritte. I 256 px si riducono a 240
// **tagliando 8 px per lato** — che e' l'overscan che il NES perdeva comunque
// sui televisori — e le 240 righe stanno centrate nelle 288 (24 sopra, 24
// sotto). Vedi SUPXEVS_X_CROP / SUPXEVS_Y_OFF.
// ============================================================================
#include "config.h"
#ifdef ENABLE_SUPXEVS

#include "nes_vs.h"
#include "../machineBase.h"

#define SUPXEVS_X_CROP  8    // colonne tagliate a sinistra (e altrettante a destra)
#define SUPXEVS_Y_OFF  24    // righe nere sopra il gioco nella finestra da 288

class supxevs : public machineBase {
public:
  supxevs() {}
  ~supxevs() {}

  void reset() override;
  signed char machineType() override { return MCH_SUPXEVS; }

  // Gioco landscape: pannello in portrait, come Green Beret.
  bool usesPortraitPanel() override { return true; }
  signed char videoFlipY() override { return 0; }

  // L'audio esce da un ring buffer a 24 kHz riempito in run_frame: l'emulazione
  // DEVE girare a 60 Hz veri o il rate audio sballa. Stesso motivo di dkong3.
  bool freeRunEmulation() override { return true; }
  uint32_t freeRunPeriodUs() override { return 16667; }

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;

  // Drena il ring audio (chiamato dal task audio via audio.cpp).
  int audioPop(int16_t *out, int n);

  // Aggiorna (una volta per quadro) i numeri dell'indicatore di stato disegnato
  // nella banda nera sopra il gioco. Vedi SUPXEVS_DIAG in supxevs.cpp.
  void diag_update(void);

private:
  nes_vs_t nes;
  // Tre framebuffer 256x240 RGB565 (PSRAM, allocati al primo reset): sono il
  // rimedio allo sfarfallio in gioco. Vedi nes_vs.h.
  uint16_t *nes_fb = nullptr;
  uint16_t *nes_fb2 = nullptr;
  uint16_t *nes_fb3 = nullptr;
  // Copia della CHR in RAM INTERNA (32 KB): e' il dato piu' letto del
  // rendering. Se l'allocazione fallisce si legge dalla flash, come prima.
  uint8_t *chr_ram = nullptr;
  // Le due uscite della MEDIA DI DUE QUADRI: e' il rimedio alle ombre che il
  // gioco alterna di proposito. Vedi nes_vs.h.
  uint16_t *nes_out1 = nullptr;
  uint16_t *nes_out2 = nullptr;
  short    *nes_audio = nullptr;   // ring buffer audio (PSRAM)
  bool      prev_coin = false;     // fronti di COIN/START: il motore li allunga
  bool      prev_start = false;
  // Ultimo quadro EMULATO gia' mostrato: prepare_frame() aspetta che
  // nes.frames lo superi, cosi' ogni quadro va sul pannello una volta e una
  // sola (sul device il render girava a 62 contro i 60 dell'emulazione).
  uint32_t  last_shown = 0;
};

#endif // ENABLE_SUPXEVS
#endif // SUPXEVS_H
