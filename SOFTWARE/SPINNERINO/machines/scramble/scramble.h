#ifndef SCRAMBLE_H
#define SCRAMBLE_H

#include <pgmspace.h>
#include "scramble_logo.h"
#include "scramble_rom_main.h"
#include "scramble_rom_audio.h"
#include "scramble_dipswitches.h"
#include "scramble_tilemap.h"
#include "scramble_spritemap.h"
#include "scramble_cmap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

#define SCRAMBLE_MAX_STARS 256

// Render SPINNERINO trasposto (come pacman/dkong): scramble usa la convenzione
// galagino 224-wide ruotata (gioco orizzontale ruotato 90 nel framebuffer
// portrait). Native 224x288 in scr_full (PSRAM), render_row ruota 90 nel
// framebuffer (stride SCR_NH) -> mostra Scramble orizzontale corretto.
#define SCR_NW   224
#define SCR_NH   288

class scramble : public machineBase
{
public:
    scramble() { }
    ~scramble() { }

    signed char machineType() override { return MCH_SCRAMBLE; }
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
    virtual void blit_tile_scroll(short row, signed char col, unsigned char scroll);
    void render_strip(short row);   // corpo render (stars+tile+sprite+bullet) verso rt

protected:
    // NB: era 'private'. Reso 'protected' per il porting di Super Cobra
    // (supercobra : scramble) che riusa render trasposto, AY, PPI, protezione,
    // stelle e coin-latch della base, overridando solo memory-map e gfx.
    bool star_visible[SCRAMBLE_MAX_STARS];
    unsigned int frame_counter = 0;
    static inline unsigned short rgb_to_swapped565(unsigned char r, unsigned char g, unsigned char b) {
        unsigned short c = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        return (c >> 8) | (c << 8);
    }

    // main/video
    short bullet_x[8], bullet_y[8];
    unsigned char bullet_active = 0;
    unsigned char nmi_enable = 0;
    unsigned char stars_enabled = 0;
    unsigned char background_enable = 0;   // 0x6803: sfondo blu (sezioni dopo il 1o livello)
    unsigned char flip_x = 0;
    unsigned char flip_y = 0;

    struct star_entry {
        unsigned char x;
        unsigned char y;
        unsigned short color;
    };
    star_entry stars[SCRAMBLE_MAX_STARS];
    int star_count = 0;
    int star_scroll_offset = 0;
    bool stars_initialized = false;
    void stars_init();

    // sound cpu + PPI + AY
    unsigned char soundlatch = 0;
    unsigned char snd_ram[1024] = {0};
    unsigned char snd_irq_pending = 0;

    // coin pulse handled locally for Scramble so one press = one credit
    bool coin_pin_prev = false;
    unsigned char coin_read_pulse = 0;
    unsigned char snd_irq_last = 0;
    unsigned char ay_addr[2] = {0, 0};
    unsigned char ay_regs[2][16] = {{0}};
    unsigned long snd_icnt = 0;

    unsigned char read_ppi0(unsigned char port);
    unsigned char read_ppi1(unsigned char port);
    void write_ppi0(unsigned char port, unsigned char value);
    void write_ppi1(unsigned char port, unsigned char value);
    unsigned char scramble_portB_r();

    unsigned char ppi0_ctrl = 0x9B;
    unsigned char ppi1_ctrl = 0x88;
    unsigned char ppi0_latch[3] = {0xFF, 0xFF, 0xFF};
    unsigned char ppi1_latch[3] = {0x00, 0x00, 0x0F};
    unsigned short protection_state = 0;
    unsigned char protection_result = 0x00;

    unsigned char ppi_port_read(unsigned char ctrl, unsigned char latch, unsigned char input_value, unsigned char port);
    void ppi_port_write(unsigned char &ctrl, unsigned char *latch, unsigned char port, unsigned char value);
    void protection_write(unsigned char value);

#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_RED, LED_YELLOW, LED_RED, LED_YELLOW, LED_RED, LED_YELLOW, LED_RED };
#endif

    // Render trasposto SPINNERINO: buffer nativo 224x288 (PSRAM) + base strip.
    unsigned short *scr_full = nullptr;
    unsigned short *rt       = nullptr;
};

#endif
