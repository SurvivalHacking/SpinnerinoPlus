// fantasy.h - Fantasy (Rock-Ola/SNK, 1981, US set "fantasyu") for GALAGINO
// Hardware: Rock-Ola "fantasy" board (snk6502.cpp). Same memory map as Nibbler
// (Nibbler in MAME literally uses fantasy_map). Differences vs Nibbler:
//   - Sound ROM is 8KB (2x 4KB) instead of Nibbler's 6KB region
//   - HC55516 CVSD speech at $2400 (Nibbler does not have speech) - VOCE: 12 campioni MAME, vedi voce_w
//   - Larger CPU ROM area is identical: 0x3000-0xBFFF (36KB)
//
// Stage 2a: skeleton for compilation + menu entry. Audio render reuses the
// Nibbler-style snk6502 waveform path but on fantasy_snd_rom. Voce: campioni.
// Video render copies Nibbler's tile-based logic; correctness of palette/bank
// will be tuned in stage 2b after first boot test.

#ifndef FANTASY_H
#define FANTASY_H

#include "../machineBase.h"

// Memory layout in GALAGINO's memory[] buffer (8KB total, same as Nibbler)
#define FAN_MEM_RAM     0x0000  // 1KB work RAM   ($0000-$03FF)
#define FAN_MEM_VRAM2   0x0400  // 1KB FG tile codes ($0400-$07FF)
#define FAN_MEM_VRAM    0x0800  // 1KB BG tile codes ($0800-$0BFF)
#define FAN_MEM_CRAM    0x0C00  // 1KB color attrs ($0C00-$0FFF)
#define FAN_MEM_CGRAM   0x1000  // 4KB char gen RAM ($1000-$1FFF)
#define FAN_MEM_TOTAL   0x2000  // 8KB

// CPU timing: 6502 @ 11.289MHz/8 = 1.411 MHz nominale -> 23520 cicli/frame.
// Test empirici: 11760 (1/2 puro per RDY wait state) rende le nuvole a scatti.
// 15000 e' il minimo per smooth rendering. Il problema "balloon explode on
// landing" NON dipende dai cicli CPU - va cercato altrove (collisioni, BCD,
// memory map, IRQ timing).
#define FAN_CYCLES_PER_FRAME 15000

// Display geometry: 256x224 ROT270 -> 224x256 portrait, 28 cols x 32 rows
#define FAN_COLS 28
#define FAN_ROWS 32
#define FAN_FB_STRIDE 224

// Render SPINNERINO trasposto (come scramble/dkong): nativo 224x256 in fan_full
// (PSRAM), render_row ruota 90 nel framebuffer (stride FAN_NH).
#define FAN_NW   224
#define FAN_NH   256

// Audio MAME-style: 3 tone voices on snk6502_sound_device.
#define FAN_NUM_VOICES 3
struct fantasy_voice {
  unsigned int base = 0;
  unsigned int offset = 0;
  unsigned int mask = 0xFF;
  unsigned int sample_cur = 0;
  unsigned int sample_step = 0;
  short form[16];
  bool mute = true;
};

class fantasy : public machineBase {
public:
    signed char machineType() override { return MCH_FANTASY; }
    bool isLandscape() override { return false; }   // ROT270 portrait
    int videoGameWidth() override { return FAN_NH; }   // 224x256 trasposto

    void reset() override;

    // 6502 (glue target: read6502->rd6502, write6502->wr6502, opread6502->op6502)
    unsigned char rd6502(unsigned short Addr) override;
    void wr6502(unsigned short Addr, unsigned char Value) override;
    unsigned char op6502(unsigned short Addr) override;

    void run_frame() override;
    void prepare_frame() override;
    void render_row(short row) override;

    const unsigned short *logo() override;

#ifdef LED_PIN
    void menuLeds(CRGB *leds) override { memcpy(leds, _leds_menu, sizeof(_leds_menu)); }
    void gameLeds(CRGB *leds) override { memcpy(leds, _leds_menu, sizeof(_leds_menu)); }
#endif

protected:
    void blit_tile(short row, char col) override {}
    void blit_sprite(short row, unsigned char s) override {}

public:
    fantasy_voice voices[FAN_NUM_VOICES];
    unsigned int  bomb_active = 0;
    bool          bomb_prev = false;
    bool          sound0_stop_on_rollover = false;

    // VOCE HD38880 (portata dal CABINATONE): il 6502 sceglie la frase scrivendo
    // a $2400 (voce_w), l'audio la suona da fantasy_voce.h. voce_cmd: -1 niente,
    // 0..11 parti, 99 ferma. voce_attiva la scrive l'audio, la legge il 6502.
    volatile int  voce_cmd = -1;
    volatile bool voce_attiva = false;
    int           voce_idx = -1;
    unsigned int  voce_pos = 0;

private:
    unsigned char hd_cmd = 0, hd_byte = 0;
    unsigned int  hd_addr = 0;
    void voce_w(unsigned char data);

    bool coin_prev = false;
    unsigned char coin_impulse = 0;
    unsigned char scroll_x = 0;
    unsigned char scroll_y = 0;
    unsigned char ctrl_reg = 0;

    void blit_bg_tile(short row, char col);
    void blit_fg_tile(short row, char col);
    void render_strip(short row);   // corpo render (bg+fg tile) verso rt

    void build_waveform(int channel, int mask);

    // Render trasposto SPINNERINO: buffer nativo 224x256 (PSRAM) + base strip.
    unsigned short *fan_full = nullptr;
    unsigned short *rt       = nullptr;

#ifdef LED_PIN
    const CRGB _leds_menu[7] = { LED_MAGENTA, LED_BLACK, LED_MAGENTA, LED_BLACK,
                                  LED_MAGENTA, LED_BLACK, LED_MAGENTA };
#endif
};

#endif // FANTASY_H
