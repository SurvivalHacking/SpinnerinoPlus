#ifndef _COMMANDO_H
#define _COMMANDO_H

// Commando (Capcom 1985) — port SPINNERINO WIFI (ESP32-P4).
// Driver da GALAGONE PAOLO P4 (caricava da SD); qui tutte le ROM/GFX sono in
// FLASH (header generati da romconv_commando.py) e il rendering è TRASPOSTO
// alla convenzione SPINNERINO (buffer nativo 224x288 -> render_row ruota 90°,
// videoGameWidth()=288), come 1942.

#include "commando_dipswitches.h"
#include "commando_rom_cpu1.h"
#include "commando_rom_cpu1_op.h"
#include "commando_rom_cpu2.h"
#include "commando_charmap.h"
#include "commando_tilemap.h"
#include "commando_spritemap.h"
#include "commando_cmap_chars.h"
#include "commando_cmap_tiles.h"
#include "commando_cmap_sprites.h"
#include "commando_logo.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class commando : public machineBase
{
public:
    commando() { }
    ~commando() { }

    signed char machineType() override { return MCH_COMMANDO; }
    int videoGameWidth() override { return 288; }   // verticale 224x288 trasposto
    void reset() override;
    unsigned char rdZ80(unsigned short Addr) override;
    void wrZ80(unsigned short Addr, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;
    const unsigned short *logo(void) override;
    bool hasNamcoAudio() override { return false; }
    // Render trasposto + sprite grandi < 16.6ms: emulazione free-run a 60Hz
    // reali (wall-clock), altrimenti gameplay E tempo musica rallentano col
    // render gated (come pbaction/bombjack). Vedi emulation.cpp.
    bool freeRunEmulation() override { return true; }

    // Stato FM esposto al renderer audio (approssimazione YM2203, schema GnG):
    // ym_regs ombreggia i registri raw così il task audio legge fnum/block/TL;
    // fm_keyed[chip][ch] != 0 quando un operatore del canale è keyed-on.
    unsigned char ym_regs[2][256] = {};
    unsigned char fm_keyed[2][3]  = {};

protected:
    void blit_tile(short row, char col) override;
    void blit_sprite(short row, unsigned char s) override;
    void blit_bg_row(short row);

private:
    unsigned char commando_bank = 0;
    unsigned short commando_scroll_x = 0;
    unsigned short commando_scroll_y = 0;
    unsigned char commando_sound_latch = 0;
    unsigned char commando_ay_addr[2] = {0, 0};
    char sub_cpu_reset = 1;

    // Spriteram BUFFERIZZATA (come MAME: draw_sprites legge m_spriteram->buffer()).
    // Copia latchata al trigger DMA 0xC806. Il gioco pulisce/riscrive la spriteram
    // viva ogni frame: leggerla direttamente in prepare_frame la beccava a meta'
    // aggiornamento -> sprite (moto + ponte) che sparivano/lampeggiavano al centro
    // del ponte. Renderizzando da questa copia stabile il tearing sparisce.
    unsigned char sprite_ram_buf[0x180] = {};   // 384 byte = 96 sprite x 4 (FE00-FF7F)

    // ROM/GFX in flash (puntatori inizializzati agli array header).
    const unsigned char *rom_cpu1    = commando_rom_cpu1;
    const unsigned char *rom_cpu1_op = commando_rom_cpu1_op;
    const unsigned char *rom_cpu2    = commando_rom_cpu2;
    const unsigned short *charmap_data   = commando_charmap;
    const unsigned long  *tilemap_data   = commando_tilemap;
    const unsigned long  *spritemap_data = commando_spritemap;
    const unsigned short *cmap_chars     = commando_cmap_chars;
    const unsigned short *cmap_tiles     = commando_cmap_tiles;
    const unsigned short *cmap_sprites   = commando_cmap_sprites;

    // Render trasposto SPINNERINO: frame nativo 224x288 in PSRAM, poi
    // render_row() lo ruota 90° nel framebuffer (stride 288). rt = base
    // della strip corrente dentro cmd_full.
    unsigned short *cmd_full = nullptr;
    unsigned short *rt       = nullptr;
};

#endif
