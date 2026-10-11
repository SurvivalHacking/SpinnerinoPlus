#ifndef MOONCRESTA_H
#define MOONCRESTA_H

#include <pgmspace.h>
#include "mooncresta_logo.h"
#include "mooncresta_rom.h"
#include "mooncresta_dipswitches.h"
#include "mooncresta_tilemap.h"
#include "mooncresta_spritemap.h"
#include "mooncresta_cmap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

// ============================================================================
// Moon Cresta (Nichibutsu 1980) -> SPINNERINO P4.
// HW Galaxian-derivato: Z80 @ 3.072MHz, tile 8x8 + sprite 16x16 + 8 bullet +
// starfield LFSR + GFX bank (fasi docking, 512 tile / 128 sprite). Audio
// discreto via Audio::galaxian_render_buffer() (gia' cablato in audio.cpp).
// Logica CPU/IO/blit ripresa 1:1 dal porting di PAOLO (GALAGONE 3.5), che usa
// la convenzione galagino 224-wide. Qui e' AVVOLTA nel render trasposto
// SPINNERINO (buffer nativo scr_full 224x288 + render_row transpose 90), come
// scramble/pacman/dkong: i blit scrivono in `rt`, render_row ruota nel
// framebuffer (stride SCR_NH) -> Moon Cresta appare diritto sul pannello.
// ============================================================================

#define MC_MAX_STARS 256

// Native arcade portrait 224x288 (asse lungo = 288), trasposto sul display.
#define SCR_NW   224
#define SCR_NH   288

class mooncresta : public machineBase
{
public:
  mooncresta() { }
  ~mooncresta() { }

  signed char machineType() override { return MCH_MOONCRESTA; }
  int videoGameWidth() override { return SCR_NH; }   // 224x288 trasposto
  void reset() override;

  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;
  void outZ80(unsigned short Port, unsigned char Value) override;
  unsigned char opZ80(unsigned short Addr) override;
  unsigned char inZ80(unsigned short Port) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;

#ifdef LED_PIN
  void menuLeds(CRGB *leds) override;
  void gameLeds(CRGB *leds) override;
#endif

protected:
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;
  void blit_tile_scroll(short row, signed char col, unsigned char scroll);
  void render_strip(short row);   // corpo render (stars+tile+sprite+bullet) verso rt

private:
  // GFX bank switching (Moon Cresta: 512 tile / 128 sprite)
  unsigned char gfxbank = 0;

  // Bullet rendering (8 bullets: 7 enemy shells + 1 player missile)
  short bullet_x[8], bullet_y[8];
  unsigned char bullet_active = 0;

  // Starfield
  struct star_entry {
    unsigned char x;
    unsigned char y;
    unsigned short color;
  };
  star_entry stars[MC_MAX_STARS];
  int star_count = 0;
  int star_scroll_offset = 0;
  bool stars_enabled = false;
  bool stars_initialized = false;
  void stars_init();
  static inline unsigned short rgb_to_swapped565(unsigned char r, unsigned char g, unsigned char b) {
    unsigned short c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    return (c >> 8) | (c << 8);
  }

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_BLUE, LED_CYAN, LED_BLUE, LED_CYAN, LED_BLUE, LED_CYAN, LED_BLUE };
#endif

  // Render trasposto SPINNERINO: buffer nativo 224x288 (PSRAM) + base strip.
  unsigned short *scr_full = nullptr;
  unsigned short *rt       = nullptr;
};

#endif
