#ifndef SUPERCOBRA_H
#define SUPERCOBRA_H

// ============================================================================
// Super Cobra (Konami 1981) — porting galagino-main -> SPINNERINO WIFI (P4).
//
// supercobra EREDITA dalla classe base `scramble` (stesso HW Konami Scramble).
// Super Cobra usa lo STESSO core (2x Z80 + 2x AY + PPI 8255 + protezione +
// stelle + bullet + tile-scroll) ma con un ADDRESS-DECODE diverso: la CPU main
// vede RAM/VRAM/attr/sprite/bullet a 0x8xxx-0x9xxx e gli I/O a 0xAxxx-0xBxxx,
// invece dei 0x4xxx-0x8xxx di Scramble.
//
// Strategia di riuso massimo:
//   - Gli indirizzi HW vengono mappati negli STESSI offset interni memory[] di
//     scramble (RAM@0x0000, VRAM@0x0800, attr@0x0C00, sprite@0x0C40,
//     bullet@0x0C60, ram2@0x0C80). Cosi' prepare_frame(), render_strip(),
//     render_row() (transpose), run_frame() (5 main/3 snd + RST38 + NMI), AY
//     (out/inZ80), stelle e coin-latch della BASE funzionano INVARIATI.
//   - Si overrida SOLO: op/rd/wrZ80 (memory map + ROM + input + protezione),
//     le 3 blit (gfx proprie + index-swap colormap) e logo().
//   - La protezione di scobra usa una tabella DIVERSA da scramble (casi
//     0x6/0x9/0xB/0xA/0xF), quindi e' reimplementata qui (sc_protection_write).
// ============================================================================

#include "config.h"
#ifdef ENABLE_SUPERCOBRA

#include "supercobra_main_rom.h"
#include "supercobra_audio_rom.h"
#include "supercobra_tilemap.h"
#include "supercobra_spritemap.h"
#include "supercobra_cmap.h"
#include "supercobra_dipswitches.h"
#include "supercobra_logo.h"
#include "../scramble/scramble.h"
#include "../tileaddr.h"

class supercobra : public scramble {
public:
  supercobra() {}
  ~supercobra() {}

  void reset() override;
  signed char machineType() override { return MCH_SUPERCOBRA; }

  // GATED come Scramble e Galaxian (stessa famiglia HW): l'emulazione e il render
  // si alternano 1:1 -> ogni frame e' uno SNAPSHOT coerente di memory[], niente
  // race. (Il free-run leggeva VRAM/scroll dal vivo durante lo scroll -> scie
  // colorate dietro sprite + ostacoli spuri.) Default machineBase = false = gated;
  // non si overrida, ma lo si lascia documentato.

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  const unsigned short *logo(void) override;

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;
  void blit_tile_scroll(short row, signed char col, unsigned char scroll) override;

private:
  static const unsigned short SC_MAIN_ROM_SIZE  = 0x6000;   // 24 KB program
  static const unsigned short SC_AUDIO_ROM_SIZE = 0x2000;   // 8 KB (usa 0x1800)

  // Protezione scobra/theend (tabella diversa da Scramble).
  unsigned short sc_prot_state  = 0;
  unsigned char  sc_prot_result = 0;
  void sc_protection_write(unsigned char value);
};

#endif // ENABLE_SUPERCOBRA
#endif // SUPERCOBRA_H
