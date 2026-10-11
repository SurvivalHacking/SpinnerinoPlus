#ifndef DKONG3_H
#define DKONG3_H

// ============================================================================
// Donkey Kong 3 (Nintendo 1983) — porting galagino-main -> SPINNERINO WIFI (P4).
//
// Z80 main + 2x RP2A03 (CPU NES + APU) per il suono. Video HW tipo Donkey Kong
// (tilemap + sprite, gfx_bank/palette_bank/PROM color_codes), render TRASPOSTO
// come dkongjr (dk3_full nativo 224x288 -> render_row traspone). Logica/memory
// map/APU da galagino dkong3.cpp.
//
// MEMORIA: build DRAM stretta -> snd_ram e dk3_samples in PSRAM (ps_malloc),
// core m6502 in flash (M6502_IRAM vuoto). Solo apu[]/snd_cpu[] in DRAM (~240B).
// ============================================================================

#include "config.h"
#ifdef ENABLE_DKONG3

#include "dkong3_rom.h"
#include "dkong3_tilemap.h"
#include "dkong3_spritemap.h"
#include "dkong3_cmap.h"
#include "dkong3_color_codes.h"
#include "dkong3_sound_roms.h"
#include "dkong3_logo.h"
#include "../../cpus/m6502/m6502.h"
#include "../tileaddr.h"
#include "../machineBase.h"

#define DK3_NW   224
#define DK3_NH   288
#define DK3_DSW1 0x00
#define DK3_DSW2 0x00

// Stato APU NES minimale (no alloc, ~90 byte) — da galagino dkong3.
struct dk3_apu_t {
  uint8_t  p_ctrl[2];  uint16_t p_timer[2]; bool p_en[2]; int p_cnt[2];
  uint8_t  p_seq[2];   uint8_t  p_env_vol[2]; uint8_t p_env_cnt[2]; bool p_env_rst[2];
  uint8_t  p_len[2];   uint8_t  p_sweep[2]; uint8_t p_sweep_cnt[2]; bool p_sweep_rst[2];
  uint16_t t_timer;    bool t_en; int t_cnt; uint8_t t_seq;
  uint8_t  t_lin_ctrl; uint8_t t_lin; bool t_lin_reload; uint8_t t_len;
  uint8_t  n_ctrl;     uint8_t n_period_idx; bool n_mode; bool n_en; int n_cnt;
  uint16_t n_lfsr;     uint8_t n_env_vol; uint8_t n_env_cnt; bool n_env_rst; uint8_t n_len;
};

class dkong3 : public machineBase {
public:
  dkong3() { memset(snd_cpu, 0, sizeof(snd_cpu)); }
  ~dkong3() {}

  void reset() override;
  signed char machineType() override { return MCH_DKONG3; }
  bool isLandscape() override { return false; }
  int videoGameWidth() override { return DK3_NH; }

  // Audio a ring buffer (2x RP2A03 a 24kHz): l'emulazione DEVE girare a 60Hz
  // reali per generare 24000 sample/s e non andare in underflow. In GATED il
  // gioco segue il render (<60fps = audio distorto + musica lenta). Free-run
  // (come Bubble Bobble) gira a 60Hz wall-clock -> rate audio corretto.
  bool freeRunEmulation() override { return true; }

  unsigned char opZ80(unsigned short Addr) override;
  unsigned char rdZ80(unsigned short Addr) override;
  void wrZ80(unsigned short Addr, unsigned char Value) override;

  void run_frame(void) override;
  void prepare_frame(void) override;
  void render_row(short row) override;
  const unsigned short *logo(void) override;

  // Audio: drena il ring buffer dk3_samples (chiamato da audio.cpp).
  static const int DK3_SAMPLES = 1024;
  int audioPop(int16_t *out, int n);

protected:
  void render_strip(short srow);
  void blit_tile(short row, char col) override;
  void blit_sprite(short row, unsigned char s) override;

private:
  unsigned char *general_ram = nullptr;
  unsigned char *sprite_ram  = nullptr;
  unsigned char *video_ram   = nullptr;

  unsigned char palette_bank = 0;
  unsigned char gfx_bank     = 0;
  unsigned char flip_screen  = 0;

  unsigned char sound_latch[3] = {0, 0, 0};   // $7C00/$7C80/$7D00 -> RP2A03
  uint8_t coin_latch = 0;
  bool    coin_prev  = false;

  // Sound: 2x RP2A03 (m6502 + APU). snd_ram/dk3_samples in PSRAM.
  m6502_t    snd_cpu[2];
  dk3_apu_t  apu[2];
  // Readback registri APU $4000-$4013: su DK3 lo spazio $4xxx e' condiviso con
  // RAM (decoding A15), quindi i registri sono RILEGGIBILI. Il firmware fa
  // read-modify-write su $4002/$4006/$400A per gli slide di pitch (jingle morte/
  // bonus/nascita/fine livello). Senza readback gli slide partono da 0 = distorti.
  // (Vedi MAME bug 08520.)
  unsigned char apu_shadow[2][0x14] = {{0}, {0}};
  unsigned char *snd_ram[2] = {nullptr, nullptr};   // 2x 2KB PSRAM
  short        *dk3_samples = nullptr;              // ring buffer PSRAM
  uint16_t      dk3_rptr = 0, dk3_wptr = 0;

  static uint8_t snd0_read (m6502_t *cpu, uint16_t addr);
  static void    snd0_write(m6502_t *cpu, uint16_t addr, uint8_t val);
  static uint8_t snd1_read (m6502_t *cpu, uint16_t addr);
  static void    snd1_write(m6502_t *cpu, uint16_t addr, uint8_t val);

  // Render trasposto: buffer nativo 224x288 (PSRAM) + base strip corrente.
  unsigned short *dk3_full = nullptr;
  unsigned short *rt       = nullptr;
};

#endif // ENABLE_DKONG3
#endif // DKONG3_H
