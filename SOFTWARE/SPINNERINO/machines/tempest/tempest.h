#ifndef TEMPEST_H
#define TEMPEST_H

#include "../machineBase.h"

#ifdef ENABLE_TEMPEST

// ============================================================================
// Tempest (Atari 1981, set MAME `tempest3`) — GUSCIO per SPINNERINO.
//
// L'emulazione vera sta in tempest_core.h (CPU, IRQ, POKEY, EAROM) e avg.cpp
// (generatore vettoriale + rasterizzatore), file senza Arduino che il banco di
// prova sul PC compila tali e quali (`_verify_gfx/tempest`). Qui restano solo
// le cose dell'AMBIENTE: memoria, ingressi, pannello, audio.
//
//   CPU 6502 1,512 MHz (fake6502) · IRQ 246 Hz tenuto fino a $5000
//   2 POKEY (suono ciclo-esatto, a eventi) -> ring buffer -> task audio
//   AVG: lista vettoriale -> segmenti -> strisce del pannello
//
// Schermo: MAME e' ROT270 e 3:4 -> sul pannello 224 x TP_GW (296).
// ============================================================================
#define TP_GW 296

class Tempest : public machineBase {
public:
  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;
  void freeBuffers() override;

  signed char machineType()       override { return MCH_TEMPEST; }
  bool        isLandscape()       override { return false; }
  int         videoGameWidth()    override { return TP_GW; }
  // L'audio nasce nell'emulazione (ring buffer): deve girare a 60 Hz di
  // orologio, non al ritmo del render, o la coda si svuota a scatti e la
  // musica rallenta ([[feedback_dac_unipolare_va_centrato]], parte PACING).
  bool        freeRunEmulation()  override { return true; }
  // 10/10: niente piu' `disableVolMode()`. Era li' perche' il FUOCO tenuto
  // sparava a raffica e il «FIRE tenuto 3 s» avrebbe aperto la barra in
  // partita; ma la guardia spegneva ANCHE il COIN, e TEMPEST restava senza
  // regolazione del volume. Ora l'autofire e' tolto (run_frame: un colpo per
  // pressione), quindi tenere FIRE 3 s apre la barra come negli altri giochi a
  // manopola.

  unsigned char rd6502(unsigned short Addr) override;
  void          wr6502(unsigned short Addr, unsigned char Value) override;
  unsigned char op6502(unsigned short Addr) override;

  void run_frame()     override;
  void prepare_frame() override;
  void render_row(short row) override;

  const unsigned short *logo(void) override;

  // Audio: chiamato dal task audio (audio.cpp, tempest_render_buffer)
  int audioPop(int16_t *out, int n);

private:
  void blit_tile(short row, char col)          override {}
  void blit_sprite(short row, unsigned char s) override {}

  static const int TP_RING = 2048;              // potenza di 2
  int16_t *ring = nullptr;
  volatile uint16_t ring_r = 0, ring_w = 0;

  uint8_t dial_last = 0;
  bool    dial_init = false;
  int     knob_acc  = 0;

  // Autofire tolto: fronte del FUOCO -> impulso tenuto finche' parte UN colpo
  // (o al massimo TP_FIRE_MAX quadri). Vedi run_frame().
  static const uint8_t TP_FIRE_MAX = 4;
  bool    fire_prev  = false;
  uint8_t fire_pulse = 0;
};

#endif // ENABLE_TEMPEST
#endif // TEMPEST_H
