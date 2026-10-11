#ifndef QUESTER_H
#define QUESTER_H

#include "../machineBase.h"

#ifdef ENABLE_QUESTER

// ============================================================================
// Quester (Namco 1987) — STUB stub di porting per SPINNERINO
//
// Per ora solo voce nel menu carousel + logo. La classe risponde con render
// schermo nero e nessuna logica CPU. Quando sara' caricata la ROM, qui
// implementeremo CPU + memory map + I/O paddle.
//
// Hardware reale Namco System 1 (per riferimento):
//   CPU main : MC6809 @ 1.5 MHz
//   CPU sound: HD63701 (sub MCU)
//   Video    : tilemap + sprite + paddle controller
// ============================================================================
class Quester : public machineBase {
public:
  Quester() {}

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override {
    machineBase::init(input, framebuffer, spritebuffer, memorybuffer);
  }
  void reset() override { machineBase::reset(); }

  signed char machineType()      override { return MCH_QUESTER; }
  bool         isLandscape()     override { return false; }
  bool         hasOpaqueBG()     override { return true;  }
  bool         freeRunEmulation()override { return false; }

  void run_frame()      override {}
  void prepare_frame()  override {}
  void render_row(short row) override {
    // Schermo nero: framebuffer 256x8 px azzerato.
    if (frame_buffer)
      memset(frame_buffer, 0, 256 * 8 * sizeof(unsigned short));
  }

  const unsigned short *logo(void) override;

private:
  void blit_tile(short row, char col)          override {}
  void blit_sprite(short row, unsigned char s) override {}
};

#endif // ENABLE_QUESTER
#endif // QUESTER_H
