#ifndef TIMEPLT_H
#define TIMEPLT_H

#include <pgmspace.h>
#include "timeplt_logo.h"
#include "timeplt_rom.h"
#include "timeplt_snd_rom.h"
#include "timeplt_dipswitches.h"
#include "timeplt_tilemap.h"
#include "timeplt_spritemap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

// ============================================================================
// Time Pilot (Konami 1982) -> SPINNERINO P4.
// HW: Z80 main @3.072MHz + Z80 sound @1.789MHz + 2x AY-3-8910. Portrait
// verticale, tile 8x8 (2 categorie di priorita') + sprite 16x16 con
// MULTIPLEXING (snapshot della sprite RAM a meta' frame). Audio: AY copiati in
// soundregs[0..13]/[16..29] -> path AY generico di audio.cpp (come scramble).
// Logica CPU/IO/blit/audio ripresa 1:1 da FULL33. Render AVVOLTO nel trasposto
// SPINNERINO (buffer nativo scr_full 224x288 + render_row transpose 90), come
// scramble/mooncresta: i blit scrivono in `rt`, render_row ruota nel framebuffer.
// ============================================================================

// Native arcade portrait 224x288 (asse lungo = 288), trasposto sul display.
#define SCR_NW   224
#define SCR_NH   288

// ANTI-FLICKER per-slot (stesso pattern di Green Beret, vedi greenberet.h):
// SPINNERINO mostra a 60Hz reali, FULL33 gira a 30Hz (VIDEO_HALF_RATE). Time
// Pilot fa lampeggiare alcuni sprite (le nuvole) a frame alterni: a 30Hz non si
// nota (stessa fase sempre campionata), a 60Hz si vede il blink. Con persistenza
// di N frame per-slot, uno slot spento questo frame ma valido di recente viene
// ridisegnato dall'ultimo sprite valido -> nuvole sempre complete, senza
// ghosting (gli slot attivi usano sempre la posizione corrente). Resta dentro
// l'approccio FULL33 (multiplex 2-pass intatto). 1 = anti-blink; 0 = originale.
#define TP_SPRITE_SLOTS   24   // slot HW: offs 0x10..0x3E, step 2
#define TP_SPRITE_PERSIST 1

class timeplt : public machineBase
{
public:
  timeplt() { }
  ~timeplt() { }

  signed char machineType() override { return MCH_TIMEPLT; }
  int videoGameWidth() override { return SCR_NH; }   // 224x288 trasposto
  // Gioco pesante (2 Z80 + render trasposto): se l'emulazione e' agganciata al
  // render gira sotto i 60fps -> gioco/musica lenti. freeRun = emulazione a 60Hz
  // reali (wall-clock) indipendente dal display (come commando/brubber/pbaction).
  bool freeRunEmulation() override { return true; }

  void reset() override {
    machineBase::reset();
    if(!scr_full)
      scr_full = (unsigned short*)ps_malloc(SCR_NW * SCR_NH * sizeof(unsigned short));
    nmi_enable = 0;
    video_enable = 0;
    flip_screen = 0;
    soundlatch = 0;
    scanline_counter = 0;
    multiplexUsed = 0;
    multiplexUsedCopy = 0;
    memset(multiplexBank0, 0, sizeof(multiplexBank0));
    memset(multiplexBank1, 0, sizeof(multiplexBank1));
    // Anti-flicker: nessuno slot ha cronologia valida all'avvio.
    for(int p = 0; p < 2; p++)
      for(int i = 0; i < TP_SPRITE_SLOTS; i++) tp_slot_age[p][i] = TP_SPRITE_PERSIST;
    snd_irq_pending = 0;
    snd_irq_last = 0;
    ay_timer_counter = 0;
    snd_icnt = 0;
    memset(snd_ram, 0, sizeof(snd_ram));
    memset(ay_addr, 0, sizeof(ay_addr));
    memset(ay_regs, 0, sizeof(ay_regs));
  }

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
  void blit_tile_cat(short row, char col, signed char cat_filter);
  void blit_sprite(short row, unsigned char s) override;
  void extract_sprites(const unsigned char *bank0, const unsigned char *bank1, int pass);
  void render_strip(short row);   // corpo render (tile cat0 + sprite + tile cat1) verso rt

private:
  unsigned char nmi_enable = 0;
  unsigned char video_enable = 0;
  unsigned char flip_screen = 0;
  unsigned char soundlatch = 0;
  unsigned char scanline_counter = 0;

  // Sprite multiplexing (la sprite RAM viene aggiornata a meta' frame; snapshot
  // al momento in cui la main CPU legge lo scanline counter a 0xC000).
  unsigned char multiplexUsed = 0;
  unsigned char multiplexUsedCopy = 0;
  unsigned char multiplexBank0[0x100];
  unsigned char multiplexBank1[0x100];

  // Anti-flicker per-slot: ultimo sprite valido + eta' (frame dall'ultimo valido)
  // per ciascuna delle 2 passate (0=snapshot, 1=corrente).
  sprite_S      tp_slot_spr[2][TP_SPRITE_SLOTS];
  unsigned char tp_slot_age[2][TP_SPRITE_SLOTS];

  // Sound CPU state (la sound CPU gira su cpu[1]; snd_cpu non usato, retaggio FULL33)
  Z80 snd_cpu;
  unsigned char snd_ram[1024];
  unsigned char snd_irq_pending = 0;
  unsigned char snd_irq_last = 0;

  // AY-3-8910 registri
  unsigned char ay_addr[2] = {0, 0};
  unsigned char ay_regs[2][16] = {{0}};
  unsigned char ay_timer_counter = 0;
  unsigned long snd_icnt = 0;

#ifdef LED_PIN
  const CRGB menu_leds[7] = { LED_CYAN, LED_WHITE, LED_CYAN, LED_WHITE, LED_CYAN, LED_WHITE, LED_CYAN };
#endif

  // Render trasposto SPINNERINO: buffer nativo 224x288 (PSRAM) + base strip.
  unsigned short *scr_full = nullptr;
  unsigned short *rt       = nullptr;
};

#endif
