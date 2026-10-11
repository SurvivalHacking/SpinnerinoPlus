// brubber.h - Burnin' Rubber (Data East, 1982) machine class for GALAGINO
// DECO C10707 (encrypted 6502) + 6502 sound, 2x AY-3-8910
// 256x240 ROT270, 8x8 3bpp tiles, 16x16 3bpp sprites, 16x16 3bpp bg tiles

#ifndef BRUBBER_H
#define BRUBBER_H

#include "../machineBase.h"

// ── Memory layout in GALAGINO's memory[] buffer ──
#define BB_MEM_RAM       0x0000  // 2KB work RAM ($0000-$07FF)
#define BB_MEM_VRAM      0x0800  // 1KB video RAM ($4000-$43FF)
#define BB_MEM_CRAM      0x0C00  // 1KB color RAM ($4400-$47FF)
#define BB_MEM_BGRAM     0x1000  // 512B background RAM ($5000-$51FF)
#define BB_MEM_EXTRA     0x1200  // 512B extra RAM ($5200-$53FF)
#define BB_MEM_PAL       0x1400  // 16B palette RAM ($5C00-$5C0F)
#define BB_MEM_SNDRAM    0x1410  // 1KB sound CPU RAM ($0000-$03FF)
#define BB_MEM_TOTAL     0x1810  // ~6.2KB total

// ── CPU timing ──
// Main CPU: DECO C10707 @ 750 kHz (12MHz/16) / 60 fps ≈ 12500 cycles/frame
#define BB_MAIN_CYCLES_PER_FRAME 12500

// Sound CPU: 6502 @ 500 kHz / 60 fps ≈ 8333 cycles/frame
#define BB_SND_CYCLES_PER_FRAME 8333

// Display: 30 columns × 32 rows after ROT270 (240×256). Area visibile MAME
// 256x240 -> ROT270 -> 240 largo × 256 alto. 240 = pieno schermo, niente bordi.
#define BB_COLS  30
#define BB_ROWS  32

// Render trasposto SPINNERINO: nativo 240x256 in bb_full (PSRAM), render_row
// ruota 90 nel framebuffer (stride BB_NH). NW=larghezza, NH=altezza native.
#define BB_NW  240
#define BB_NH  256

class brubber : public machineBase {
public:
    signed char machineType() override { return MCH_BRUBBER; }
    int videoGameWidth() override { return BB_NH; }   // verticale 240x256 trasposto
    void reset() override;

    // 6502 (glue target: read6502->rd6502, write6502->wr6502, opread6502->op6502)
    unsigned char rd6502(unsigned short Addr) override;
    void wr6502(unsigned short Addr, unsigned char Value) override;
    unsigned char op6502(unsigned short Addr) override;

    void run_frame() override;
    void prepare_frame() override;
    void render_row(short row) override;

    const unsigned short *logo() override;
    // Render pesante (transpose + bg scrolling + sprite) > 16.6ms: emulazione
    // free-run a 60Hz reali, altrimenti main+sound CPU (e tempo musica)
    // rallentano col render gated. Stesso fix di commando/pbaction.
    bool freeRunEmulation() override { return true; }

#ifdef LED_PIN
    void menuLeds() override {}
    void gameLeds() override {}
#endif

protected:
    void blit_tile(short row, char col) override;
    void blit_sprite(short row, unsigned char s) override;

private:
    // CPU state
    bool is_sound_cpu = false;   // flag for read6502/write6502 dispatch
    bool coin_prev = false;      // edge detect for coin NMI
    bool vblank_flag = false;    // VBLANK polling bit (DSW1 bit 7)
    bool nmi_enable = false;     // main CPU NMI enable (write $1000)

    // Video
    unsigned char scroll_reg[2] = {0, 0};  // scroll registers
    unsigned char video_control = 0;

    // Sound
    unsigned char sound_latch = 0;
    bool sound_latch_pending = false;
    bool sound_nmi_enable = false;
    unsigned char ay_addr[2] = {0, 0};  // AY register addresses
    unsigned char ay_regs[2][16] = {{0}};  // AY register mirrors

    // Palette cache (BGR233 inverted → RGB565 byte-swapped)
    unsigned short palette_cache[16] = {0};
    void update_palette(unsigned char idx, unsigned char val);

    // Background + tile rendering helpers
    void render_strip(short row);   // corpo render (bg+tile+sprite) verso rt
    void render_background(short row);
    void blit_tile_ex(short row, char col, bool transparent);

    // Sound CPU helpers
    void run_sound_frame();

    // Render trasposto SPINNERINO: buffer nativo 240x256 (PSRAM) + base strip.
    unsigned short *bb_full = nullptr;
    unsigned short *rt      = nullptr;
};

#endif // BRUBBER_H
