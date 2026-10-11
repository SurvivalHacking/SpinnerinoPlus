// nibbler.h - Nibbler (Rock-Ola, 1982) machine class for GALAGINO
// Hardware: SNK/Rockola custom board (fantasy family)
// CPU: 6502 @ ~930 kHz, IRQ on VBLANK, NMI on coin edge
// Video: 256x224 visible, 32x28 tile 8x8 2bpp, no sprites
// Layers: BG (VRAM @ 0x0800 + GFX ROM), FG (VRAM2 @ 0x0400 + CGRAM)
//
// MAME memory map (fantasy_map, from snk6502.cpp):
//   0x0000-0x03FF  RAM
//   0x0400-0x07FF  videoram2 (FG tile codes)
//   0x0800-0x0BFF  videoram  (BG tile codes)
//   0x0C00-0x0FFF  colorram
//   0x1000-0x1FFF  charram (FG character generator RAM)
//   0x2000-0x2001  MC6845 CRTC (ignored in emulation)
//   0x2100-0x2102  sound chip write
//   0x2103         flipscreen + char_bank + bg color + music
//   0x2104-0x2107  input ports (IN0/IN1/DSW/IN2)
//   0x2200         scroll X
//   0x2300         scroll Y
//   0x2400         speech write
//   0x3000-0xBFFF  ROM
//   0xF000-0xFFFF  ROM mirror of 0x8000 region (for vectors)

#ifndef NIBBLER_H
#define NIBBLER_H

#include "../machineBase.h"

// ── Memory layout in GALAGINO's memory[] buffer (8KB total) ──
#define NIB_MEM_RAM     0x0000  // 1KB work RAM   ($0000-$03FF)
#define NIB_MEM_VRAM2   0x0400  // 1KB FG tile codes ($0400-$07FF) - "videoram2"
#define NIB_MEM_VRAM    0x0800  // 1KB BG tile codes ($0800-$0BFF) - "videoram"
#define NIB_MEM_CRAM    0x0C00  // 1KB color attrs ($0C00-$0FFF)
#define NIB_MEM_CGRAM   0x1000  // 4KB char gen RAM ($1000-$1FFF)
#define NIB_MEM_TOTAL   0x2000  // 8KB

// ── CPU timing: 6502 @ 11.289MHz/8 ≈ 1.411 MHz, / 60fps ≈ 23520 cycles/frame ──
#define NIB_CYCLES_PER_FRAME 23520

// ── Display geometry ──
// Game is 256×224 landscape, cabinet is ROT90 (portrait monitor).
// So display is 224×256 portrait = 28 tile-cols × 32 tile-rows.
// The game VRAM stores 32 game-cols × 28 game-rows; we rotate at render time:
//   game_tile_x = display_row, game_tile_y = 27 - display_col.
#define NIB_COLS 28           // display tile columns
#define NIB_ROWS 32           // display tile rows
#define NIB_FB_STRIDE 224     // portrait framebuffer width

// Render SPINNERINO trasposto (come fantasy/scramble): nativo 224x256 in
// nib_full (PSRAM), render_row ruota 90 nel framebuffer (stride NIB_NH).
#define NIB_NW   224
#define NIB_NH   256

// Audio MAME-fedele (snk6502_sound_device, fantasy_sound_device).
// 3 voci tone, ognuna con waveform 16-sample da bit pattern (build_waveform
// MAME 2003-plus). Stato pubblico cosi' audio.cpp::nibbler_render_buffer
// puo' leggerlo durante il render audio.
#define NIB_NUM_VOICES 3
struct nibbler_voice {
  unsigned int base = 0;
  unsigned int offset = 0;
  unsigned int mask = 0xFF;
  unsigned int sample_cur = 0;
  unsigned int sample_step = 0;
  short form[16];
  bool mute = true;
};

class nibbler : public machineBase {
public:
    signed char machineType() override { return MCH_NIBBLER; }
    // Nibbler in MAME is ROT90 → cabinet with rotated monitor (portrait).
    bool isLandscape() override { return false; }
    int videoGameWidth() override { return NIB_NH; }   // 224x256 trasposto

    // Render pesante (ridisegno completo BG+FG ~1800 tile + transpose 90 su SPI)
    // > 16.6ms: se l'emulazione fosse gated dal vblank (default false), gameplay
    // E musica rallenterebbero in proporzione al framerate. Free-run a 60Hz reali
    // wall-clock disaccoppia emulazione dal render. Stesso fix di
    // commando/brubber/bombjack/pbaction. Vedi spinnerino_wifi_freerun_emulation.
    bool freeRunEmulation() override { return true; }

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
    // Crunch sample player (sample WAV registrato da MAME).
    // Il discrete sound "crunch" non e' emulato in MAME — replicato via sample.
    // Triggerato quando il game scrive 0x2101 con bit3=1 + bank 1 (= 0x09).
    // Pubblico cosi' audio.cpp puo' miscelarlo dentro nibbler_render_buffer.
    int  crunch_pos    = -1;   // -1 = inattivo, >=0 = indice sample in corso

    // Audio MAME-fedele: voci tone + bomb LFSR. Pubblico per audio.cpp.
    nibbler_voice voices[NIB_NUM_VOICES];
    unsigned int  bomb_active = 0;       // bomb discrete trigger sample count
    bool          bomb_prev = false;     // edge detection
    bool          sound0_stop_on_rollover = false;

private:
    bool coin_prev = false;
    unsigned char coin_impulse = 0;  // counts frames for PORT_IMPULSE(1) emulation
    unsigned char scroll_x = 0;      // $2200
    unsigned char scroll_y = 0;      // $2300
    unsigned char ctrl_reg = 0;   // $2103: bit 0-2=bg color, bit 3=char bank,
                                  //        bit 4-6=music, bit 7=flipscreen

    void blit_bg_tile(short row, char col);
    void blit_fg_tile(short row, char col);
    void render_strip(short row);   // corpo render (bg+fg tile) verso rt

    // build_waveform da MAME 2003-plus: 4-bit mask -> 16-sample waveform
    void build_waveform(int channel, int mask);

    // Render trasposto SPINNERINO: buffer nativo 224x256 (PSRAM) + base strip.
    unsigned short *nib_full = nullptr;
    unsigned short *rt       = nullptr;

#ifdef LED_PIN
    const CRGB _leds_menu[7] = { LED_GREEN, LED_BLACK, LED_GREEN, LED_BLACK,
                                  LED_GREEN, LED_BLACK, LED_GREEN };
#endif
};

#endif // NIBBLER_H
