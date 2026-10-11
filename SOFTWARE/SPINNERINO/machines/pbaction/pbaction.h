#ifndef PBACTION_H
#define PBACTION_H

#include "../machineBase.h"

#ifdef ENABLE_PBACTION

// ============================================================================
// Tehkan/Tecmo Pinball Action (1985) — porting da P4 NEW_multi a sambi 3.5".
//
// Hardware (MAME tecmo/pbaction.cpp):
//   Main CPU:  Z80 @ 4 MHz, 40 KB ROM ($0000-$9FFF)
//   Audio CPU: Z80 @ ~3 MHz, 8 KB ROM
//   Video:     2 tile layers (fg + bg) + sprites + palette RAM xBGR_444
//              Game internal 256x224 landscape, cabinet ROT90 CW (verticale)
//   Sound:     3× AY-3-8910 + Z80 CTC IRQ
//
// Memory map (main Z80):
//   $0000-$BFFF  ROM (40 KB max range; effettiva 0..9FFF)
//   $C000-$CFFF  work RAM (4 KB)
//   $D000-$D3FF  videoram[1] (FG)
//   $D400-$D7FF  colorram[1]
//   $D800-$DBFF  videoram[0] (BG)
//   $DC00-$DFFF  colorram[0]
//   $E000-$E07F  sprite RAM (128 byte)
//   $E400-$E5FF  palette RAM (512 byte, xBGR_444 LE)
//   $E600        P1 input read / NMI mask write
//   $E601        P2 input read (cocktail unused)
//   $E602        SYSTEM (coin+start)
//   $E604        DSW1 read / flipscreen write
//   $E605        DSW2 read
//   $E606        scroll write
//   $E800        sound command write (latch a audio CPU + CTC IRQ)
//
// ROT90 CW SW: mame_x = dy_display, mame_y = 239 - dx_display.
// Display dopo rotazione: 224 wide × 256 tall = framework default sambi.
// ============================================================================

#define PBACTION_MAIN_SIZE     0x0A000  // 40 KB
#define PBACTION_AUDIO_SIZE    0x02000  //  8 KB
#define PBACTION_FG_SIZE       0x06000  // 24 KB (3 plane × 8 KB)
#define PBACTION_BG_SIZE       0x10000  // 64 KB (4 plane × 16 KB)
#define PBACTION_SPR_SIZE      0x06000  // 24 KB (3 plane × 8 KB)

class pbaction : public machineBase {
public:
  pbaction();

  void init(Input *input, unsigned short *framebuffer,
            sprite_S *spritebuffer, unsigned char *memorybuffer) override;
  void reset() override;

  signed char machineType()      override { return MCH_PBACTION; }
  bool        isLandscape()      override { return false; }
  // FREE-RUN: il render (3 layer + SPI) e' < 60fps; con l'emulazione gated dal
  // render (1 run_frame per refresh) gameplay e musica risultavano lenti. Con
  // freeRunEmulation()=true l'emulazione gira a 60Hz REALI a wall-clock
  // (emulation.cpp), indipendente dal display. run_frame usa i rapporti REALI di
  // PAOLO (audio 3/4, CTC 625 = ~120Hz). Vedi feedback_multi_step_emulation.
  bool        freeRunEmulation() override { return true; }

  unsigned char rdZ80(unsigned short Addr) override;
  void          wrZ80(unsigned short Addr, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;
  void          outZ80(unsigned short Port, unsigned char Value) override;

  void run_frame()      override;
  void prepare_frame()  override;
  void render_row(short row) override;
  // 224x256 native portrait -> trasposto in landscape SP (256 wide come Pacman)
  int videoGameWidth() override { return 256; }

  const unsigned short *logo(void) override;

private:
  // Render trasposto: render_native_row riempie pb_full (224x256 portrait),
  // render_row traspone 90 in frame_buffer landscape (vedi 1942/pacman).
  void render_native_row(short row);
  unsigned short *pb_full = nullptr;
  unsigned short *rt = nullptr;

  // Working RAM (allocated in init from main `memory` buffer slice).
  // Layout interno (totale ~9 KB, RAMSIZE=9344 da config):
  //   memory[0x0000-0x0FFF] = work RAM 4 KB     ($C000-$CFFF)
  //   memory[0x1000-0x13FF] = videoram[1] FG    ($D000-$D3FF)
  //   memory[0x1400-0x17FF] = colorram[1]       ($D400-$D7FF)
  //   memory[0x1800-0x1BFF] = videoram[0] BG    ($D800-$DBFF)
  //   memory[0x1C00-0x1FFF] = colorram[0]       ($DC00-$DFFF)
  //   memory[0x2000-0x207F] = sprite RAM 128 B  ($E000-$E07F)
  //   memory[0x2080-0x227F] = palette RAM 512 B ($E400-$E5FF)
  //   memory[0x2280-0x2A7F] = audio RAM 2 KB    ($4000-$47FF audio CPU)
  // ⇒ totale 0x2A80 = 10880 byte → RAMSIZE=11008 (next 1KB)
  // (rivisto in config: useremo RAMSIZE 11008 per pbaction)

  // Stato I/O
  unsigned char nmi_enable;
  unsigned char flip_screen;
  unsigned char scroll;
  unsigned char buttons;

  // Audio CPU state
  unsigned char soundlatch;
  unsigned char ctc_vector;
  unsigned char ay_addr[3];     // AY register address latch per chip
  unsigned char last_e604_write;

  unsigned int frame_count;
};

#endif // ENABLE_PBACTION
#endif // PBACTION_H
