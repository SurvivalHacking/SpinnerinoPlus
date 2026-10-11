/*
 * commando.cpp - Commando (Capcom 1985) for GALAGINO
 *
 * Hardware: Z80 main @ 3MHz, Z80 sound @ 3MHz, 2x YM2203
 *
 * MAIN CPU memory map (per MAME):
 *   0000-BFFF: ROM (cm04 0000-7FFF, cm03 8000-BFFF)
 *   C000     : IN0 (coin, start, service)
 *   C001     : IN1 player 1
 *   C002     : IN2 player 2
 *   C003     : DSW1
 *   C004     : DSW2
 *   C800     : sound latch write
 *   C804     : bit0=coin1, bit1=coin2, bit4=sub-CPU reset (1=reset asserted), bit7=flip
 *   C806     : sprite DMA trigger (buffered spriteram)
 *   C808-C809: scroll X (low,high)
 *   C80A-C80B: scroll Y (low,high)
 *   D000-D3FF: fg videoram (codes, 32x32)
 *   D400-D7FF: fg colorram (attrs)
 *   D800-DBFF: bg videoram (codes, 32x32)
 *   DC00-DFFF: bg colorram (attrs)
 *   E000-FDFF: work RAM (8KB)
 *   FE00-FF7F: sprite RAM (384 bytes, 96 sprites x 4)
 *   FF80-FFFF: more work RAM
 *
 * SOUND CPU memory map:
 *   0000-3FFF: ROM (cm02)
 *   4000-47FF: RAM
 *   6000     : sound latch read
 *   8000-8001: YM2203 #1
 *   8002-8003: YM2203 #2
 *
 * Our memory[] layout:
 *   0x0000-0x03FF  fg vram (1024 bytes)
 *   0x0400-0x07FF  fg cram
 *   0x0800-0x0BFF  bg vram
 *   0x0C00-0x0FFF  bg cram
 *   0x1000-0x2FFF  main RAM (E000-FFFF, 8KB — spriteram at 0x2E00-0x2F7F matches FE00-FF7F)
 *   0x3000-0x37FF  sound RAM (2KB)
 */

#include "commando.h"
#include "../../emulation/input.h"

// Geometria nativa portrait 224 wide x 288 tall (36 strip da 8px), come 1942.
#define CMD_NW 224
#define CMD_NH 288

// Spostamento orizzontale del layer FG (caratteri/lettere) in CELLE da 8px.
// 0 = nessuno shift (il disallineamento iniziali era del CURSORE, non delle lettere).
#define CMD_FG_DCOL 0

// Offset VBEND (MAME: screen vbend=16). Il layer FG (tileaddr) incorpora il bordo
// di 2 tile, sprite e bg no: per allineare il cursore iniziali (sprite) alle lettere
// (fg) si spostano sprite E bg insieme di questo offset (le palme restano sul
// terreno perche' bg e sprite si muovono assieme). Crediti OK con 16. NB: NON tocca
// la moto tagliata (asse diverso, fb_y=256-sx).
#define CMD_VBEND_DY 16

// Mask gameplay buttons for the first ~90 frames after reset so a FIRE/START/
// EXTRA still held from manual menu selection doesn't disturb ROM boot/POST.
unsigned int commando_kick_frames = 0;

void commando::reset() {
    // ROM/GFX sono in flash (puntatori già inizializzati in commando.h).
    // Alloca lazy il buffer nativo 224x288 in PSRAM (come 1942/pacman).
    if (!cmd_full)
        cmd_full = (unsigned short*)ps_malloc(CMD_NW * CMD_NH * sizeof(unsigned short));

    machineBase::reset();
    sub_cpu_reset = 1;              // audio CPU held in reset until c804 bit4 cleared
    commando_bank = 0;
    commando_scroll_x = 0;
    commando_scroll_y = 0;
    commando_sound_latch = 0;
    commando_kick_frames = 0;
    memset(sprite_ram_buf, 0, sizeof(sprite_ram_buf));   // spriteram bufferizzata pulita

    // Silence "Unrecognized instruction" spam (undocumented Z80 opcodes are NOPs)
    cpu[0].TrapBadOps = 0;
    cpu[1].TrapBadOps = 0;
}

unsigned char commando::opZ80(unsigned short Addr) {
    if (current_cpu == 0) {
        // Main CPU: opcode fetches go through decrypted ROM.
        if (Addr < 0xC000) return rom_cpu1_op ? rom_cpu1_op[Addr] : 0xFF;
    } else {
        if (Addr < 0x4000) return rom_cpu2 ? rom_cpu2[Addr] : 0xFF;
    }
    return rdZ80(Addr);
}

unsigned char commando::rdZ80(unsigned short Addr) {
    if (current_cpu == 0) {
        if (Addr < 0xC000) return rom_cpu1 ? rom_cpu1[Addr] : 0xFF;

        // VRAM/CRAM
        if ((Addr & 0xFC00) == 0xD000) return memory[Addr - 0xD000 + 0x0000]; // fg vram
        if ((Addr & 0xFC00) == 0xD400) return memory[Addr - 0xD400 + 0x0400]; // fg cram
        if ((Addr & 0xFC00) == 0xD800) return memory[Addr - 0xD800 + 0x0800]; // bg vram
        if ((Addr & 0xFC00) == 0xDC00) return memory[Addr - 0xDC00 + 0x0C00]; // bg cram

        // Main RAM (E000-FFFF), includes spriteram at FE00-FF7F
        if (Addr >= 0xE000)            return memory[Addr - 0xE000 + 0x1000];

        // Inputs / DIP
        // MAME SYSTEM (C000) bit map:
        //   0x01 = START1, 0x02 = START2, 0x40 = COIN2, 0x80 = COIN1
        if (Addr == 0xC000) {
            unsigned char k = input->buttons_get();
            unsigned char km = (commando_kick_frames < 90) ? (k & BUTTON_COIN) : k;
            unsigned char r = 0xFF;
            if (km & BUTTON_COIN)  r &= ~0x80;                 // COIN1
            if (km & BUTTON_START) r &= ~0x01;                 // START1
            return r;
        }
        if (Addr == 0xC001) {
            unsigned char k = (commando_kick_frames < 90) ? 0 : input->buttons_get();
            unsigned char r = 0xFF;
            if (k & BUTTON_RIGHT) r &= ~0x01;
            if (k & BUTTON_LEFT)  r &= ~0x02;
            if (k & BUTTON_DOWN)  r &= ~0x04;
            if (k & BUTTON_UP)    r &= ~0x08;
            if (k & BUTTON_FIRE)  r &= ~0x10;
            if (k & BUTTON_EXTRA) r &= ~0x20;
            return r;
        }
        if (Addr == 0xC002) return 0xFF;
        if (Addr == 0xC003) return (unsigned char)~COMMANDO_DIP_A;
        if (Addr == 0xC004) return (unsigned char)~COMMANDO_DIP_B;
    } else {
        if (Addr < 0x4000) return rom_cpu2 ? rom_cpu2[Addr] : 0xFF;
        if ((Addr & 0xF800) == 0x4000) return memory[Addr - 0x4000 + 0x3000];
        if (Addr == 0x6000) return commando_sound_latch;
        // YM2203 status register read (timer flags) — no timer IRQs emulated.
        if (Addr == 0x8000 || Addr == 0x8002) return 0x00;
    }
    return 0xFF;
}

void commando::wrZ80(unsigned short Addr, unsigned char Value) {
    if (current_cpu == 0) {
        // VRAM/CRAM
        if ((Addr & 0xFC00) == 0xD000) { memory[Addr - 0xD000 + 0x0000] = Value; return; }
        if ((Addr & 0xFC00) == 0xD400) { memory[Addr - 0xD400 + 0x0400] = Value; return; }
        if ((Addr & 0xFC00) == 0xD800) { memory[Addr - 0xD800 + 0x0800] = Value; return; }
        if ((Addr & 0xFC00) == 0xDC00) { memory[Addr - 0xDC00 + 0x0C00] = Value; return; }

        if (Addr >= 0xE000)            { memory[Addr - 0xE000 + 0x1000] = Value; return; }

        // I/O
        if (Addr == 0xC800) { commando_sound_latch = Value; return; }
        if (Addr == 0xC804) {
            // bit4 = sub-CPU reset (1 = assert reset / stop)
            char new_reset = (Value & 0x10) ? 1 : 0;
            if (new_reset && !sub_cpu_reset) {
                // assert reset: reset the sound CPU
                char saved = current_cpu;
                current_cpu = 1;
                ResetZ80(&cpu[1]);
                current_cpu = saved;
            }
            sub_cpu_reset = new_reset;
            return;
        }
        if (Addr == 0xC806) {
            // Sprite DMA trigger: latcha una copia STABILE della spriteram (come
            // il buffered_spriteram di MAME). prepare_frame renderizza da qui, non
            // dalla RAM viva -> niente tearing (moto/ponte non lampeggiano piu').
            memcpy(sprite_ram_buf, &memory[0x2E00], 0x180);
            return;
        }
        if (Addr == 0xC808) { commando_scroll_x = (commando_scroll_x & 0xFF00) | Value; return; }
        if (Addr == 0xC809) { commando_scroll_x = (commando_scroll_x & 0x00FF) | (Value << 8); return; }
        if (Addr == 0xC80A) { commando_scroll_y = (commando_scroll_y & 0xFF00) | Value; return; }
        if (Addr == 0xC80B) { commando_scroll_y = (commando_scroll_y & 0x00FF) | (Value << 8); return; }
    } else {
        if ((Addr & 0xF800) == 0x4000) { memory[Addr - 0x4000 + 0x3000] = Value; return; }
        // YM2203 #1 — SSG (regs 0..15) feeds the AY renderer via soundregs,
        // FM (regs 0x28, 0x30..0xB6) is shadowed in ym_regs for the
        // GnG-style square-wave FM approximation in the audio renderer.
        if (Addr == 0x8000) { commando_ay_addr[0] = Value; return; }
        if (Addr == 0x8001) {
            unsigned char reg = commando_ay_addr[0];
            ym_regs[0][reg] = Value;
            if (reg < 16) soundregs[reg] = Value;
            if (reg == 0x28) {
                unsigned char ch = Value & 0x03;
                if (ch < 3) fm_keyed[0][ch] = (Value >> 4) & 0x0F;
            }
            return;
        }
        // YM2203 #2
        if (Addr == 0x8002) { commando_ay_addr[1] = Value; return; }
        if (Addr == 0x8003) {
            unsigned char reg = commando_ay_addr[1];
            ym_regs[1][reg] = Value;
            if (reg < 16) soundregs[16 + reg] = Value;
            if (reg == 0x28) {
                unsigned char ch = Value & 0x03;
                if (ch < 3) fm_keyed[1][ch] = (Value >> 4) & 0x0F;
            }
            return;
        }
    }
}

void commando::run_frame(void) {
    current_cpu = 0;
    game_started = 1;
    if (commando_kick_frames < 120) commando_kick_frames++;

    for (char f = 0; f < 4; f++) {
        for (int i = 0; i < INST_PER_FRAME / 3; i++) {
            current_cpu = 0;
            StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
            if (!sub_cpu_reset) {
                current_cpu = 1;
                StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);
            }
        }
        // Main CPU: RST 10h on VBLANK (IM0, vector from IRQ PROM — HW puts 0xD7 on bus)
        if (f == 3) {
            current_cpu = 0;
            IntZ80(&cpu[0], INT_RST10);
        }
        // Sound CPU: RST 38h (IM1) each quarter-frame - 4 irqs per frame
        if (!sub_cpu_reset) {
            current_cpu = 1;
            IntZ80(&cpu[1], INT_RST38);
        }
    }

}

// ────────────────────────────────────────────────────────────────────
//  Sprite collection: parse 0xFE00..0xFF7F (96 sprites x 4 bytes).
//  MAME layout:
//    sp[0]: code low 8 bits
//    sp[1]: attr  (b7-6 = bank, b5-4 = color, b3 = flipy, b2 = flipx, b0 = x MSB)
//    sp[2]: y (native)
//    sp[3]: x (native low 8)
//  code = sp[0] + 256*bank,  valid if bank < 3   →  max 768 sprites
// ────────────────────────────────────────────────────────────────────
void commando::prepare_frame(void) {
    active_sprites = 0;
    // Sprite RAM BUFFERIZZATA (latchata al DMA trigger 0xC806, vedi wrZ80). MAME
    // renderizza da m_spriteram->buffer(), NON dalla RAM viva: leggere la RAM viva
    // qui la beccava mentre il gioco la stava riscrivendo -> moto e pezzo centrale
    // del ponte che sparivano/lampeggiavano. 96 slot × 4 byte = 384 byte.
    //
    // Ordine BACK-TO-FRONT come MAME (draw_sprites: offs da bytes-4 giu' fino a 0,
    // cioe' indice 95 -> 0). Gli sprite raccolti prima sono disegnati prima (dietro),
    // quindi l'indice 0 finisce SOPRA. Con l'ordine invertito (0->95) il ponte
    // copriva la moto al centro (moto invisibile): qui rispettiamo la priorita' HW.
    for (int idx = 95; idx >= 0; idx--) {
        unsigned char *sp = &sprite_ram_buf[idx * 4];
        unsigned char attr = sp[1];
        unsigned char bank = (attr >> 6) & 0x03;
        if (bank >= 3) continue;                          // invalid / unused
        if (sp[2] == 0) continue;                         // y==0 treated as inactive
        int code = sp[0] + 256 * bank;
        int color = (attr >> 4) & 0x03;
        int flipx = (attr & 0x04) ? 1 : 0;
        int flipy = (attr & 0x08) ? 1 : 0;

        // Landscape native position → portrait framebuffer (ROT90 convention, 1942 style)
        //   fb_x = sp[2] - 16   (native Y becomes fb X)
        //   fb_y = 256 - sx     (native X inverted → fb Y)
        int sx = sp[3] - ((attr & 0x01) << 8);            // 9-bit signed, -256..255
        // ROT270 landscape→portrait mapping. L'area visibile MAME parte a
        // scanline 16 (set_raw ... vbend=16): l'offset di 16px del bordo serve
        // solo sull'asse VERTICALE (fb_y, 240->256 = +16, "le palme salgono").
        // L'asse ORIZZONTALE (fb_x) e' gia' corretto in screen-space = sp[2].
        //   portrait_x = landscape_y - CMD_VBEND_DY  (allinea cursore alle lettere fg)
        //   portrait_y = 256 - landscape_x           (= 240 - sx + 16, fix verticale)
        int fb_x = sp[2] - CMD_VBEND_DY;
        int fb_y = 256 - sx;

        if (fb_x <= -16 || fb_x >= 224) continue;
        if (fb_y <= -16 || fb_y >= 288) continue;

        if (active_sprites >= 96) break;
        sprite[active_sprites].code  = code;
        sprite[active_sprites].color = color;
        sprite[active_sprites].x = fb_x;
        sprite[active_sprites].y = fb_y;
        // After the 90° CCW rotation applied to sprite graphics in the bin:
        //   landscape flipx (horizontal) ≡ portrait flipy (vertical in rotated) → orient bit 1
        //   landscape flipy (vertical)   ≡ portrait flipx (horizontal in rotated) → orient bit 0
        sprite[active_sprites].flags = (flipx ? 2 : 0) | (flipy ? 1 : 0);
        active_sprites++;
    }

    // --- Render trasposto: disegna l'intero frame nativo 224x288 in cmd_full ---
    // Ordine: bg tiles -> sprites -> fg chars (overlay). Righe 0-1 e 34-35 nere
    // (bordi). render_row() trasporrà poi cmd_full nel framebuffer (stride 288).
    if (cmd_full) {
        memset(cmd_full, 0, CMD_NW * CMD_NH * sizeof(unsigned short));
        for (short srow = 2; srow < 34; srow++) {
            rt = cmd_full + srow * 8 * CMD_NW;
            blit_bg_row(srow);
            for (unsigned char s = 0; s < active_sprites; s++)
                if ((sprite[s].y < 8 * (srow + 1)) && ((sprite[s].y + 16) > 8 * srow))
                    blit_sprite(srow, s);
            for (char col = 0; col < 28; col++)
                blit_tile(srow, col);
        }
    }
}

// ────────────────────────────────────────────────────────────────────
//  Render one 8-pixel-tall row of the portrait frame_buffer.
//  Order: bg tiles  →  sprites  →  fg chars (transparent overlay).
//  The driver treats frame_buffer as already-rotated portrait (like 1942).
//  Tile and char ROM glyphs are pre-rotated in the ROM for ROT90 displays,
//  so we write pixel data straight into fb and only re-map the VRAM index.
// ────────────────────────────────────────────────────────────────────

void commando::blit_bg_row(short row) {
    if (!tilemap_data || !cmap_tiles) return;

    // Tiles are pre-rotated 90° CW in the bin.
    //   For CW rotation:  rotated[y'][x'] = orig[15-x'][y']
    //   To read orig[py][px]:  access rotated[px][15-py]
    // So bin row index = px (landscape X in tile), column = 15 - py.
    //
    // Landscape (lx, ly) ← portrait (fb_x, fb_y) via ROT270 CW:
    //     lx = 271 - fb_y      (landscape X, inverted)
    //     ly = fb_x + 16       (visible landscape Y starts at 16)

    int scx = commando_scroll_x;
    int scy = commando_scroll_y;

    for (int strip_y = 0; strip_y < 8; strip_y++) {
        int fb_y = 8 * row + strip_y;
        int tmx  = ((271 - fb_y) + scx) & 0x1FF;
        int tile_x = (tmx >> 4) & 0x1F;
        int px_x   = tmx & 15;
        unsigned short *dst = rt + strip_y * 224;

        int fb_x = 0;
        while (fb_x < 224) {
            int tmy  = (fb_x + CMD_VBEND_DY + scy) & 0x1FF;
            int tile_y = (tmy >> 4) & 0x1F;
            int px_y   = tmy & 15;
            int vram_idx = tile_x * 32 + tile_y;
            unsigned char chr  = memory[0x0800 + vram_idx];
            unsigned char attr = memory[0x0C00 + vram_idx];
            int code = chr | ((attr & 0xC0) << 2);
            int colgrp = attr & 0x0F;
            int flipx = (attr & 0x10) != 0;
            int flipy = (attr & 0x20) != 0;
            const unsigned short *colors = &cmap_tiles[colgrp * 8];

            int eff_px_x = flipx ? (15 - px_x) : px_x;   // rotated row index (bin)
            const unsigned long *tile_rows = &tilemap_data[code * 32 + 2 * eff_px_x];

            int remaining_in_tile_y = 16 - px_y;
            if (fb_x + remaining_in_tile_y > 224) remaining_in_tile_y = 224 - fb_x;

            for (int i = 0; i < remaining_in_tile_y; i++) {
                int py = (px_y + i) & 15;
                if (py == 0 && i > 0) {
                    tile_y   = (tile_y + 1) & 0x1F;
                    vram_idx = tile_x * 32 + tile_y;
                    chr  = memory[0x0800 + vram_idx];
                    attr = memory[0x0C00 + vram_idx];
                    code = chr | ((attr & 0xC0) << 2);
                    colgrp = attr & 0x0F;
                    flipx = (attr & 0x10) != 0;
                    flipy = (attr & 0x20) != 0;
                    colors = &cmap_tiles[colgrp * 8];
                    eff_px_x = flipx ? (15 - px_x) : px_x;
                    tile_rows = &tilemap_data[code * 32 + 2 * eff_px_x];
                }
                // rotated column = 15 - py (or py when flipy)
                int r_col = flipy ? py : (15 - py);
                unsigned long pix_dat = (r_col >= 8) ? tile_rows[1] : tile_rows[0];
                int shift = (r_col & 7) * 4;
                int pixel = (pix_dat >> shift) & 0x07;
                dst[fb_x + i] = colors[pixel];
            }
            fb_x += remaining_in_tile_y;
        }
    }
}

void commando::blit_tile(short row, char col) {
    if (!charmap_data || !cmap_chars) return;
    // fg: 32x32 8x8 chars, use tileaddr lookup (portrait→native linear).
    // CMD_FG_DCOL ricentra orizzontalmente il layer FG (vedi #define). La
    // posizione su schermo resta 8*col; cambia solo QUALE cella VRAM si legge,
    // cosi' la riga di lettere scorre senza overflow del framebuffer.
    int vcol = (27 - col) + CMD_FG_DCOL;
    if (vcol < 0 || vcol > 27) return;                 // bordo: nessun char
    unsigned short addr = tileaddr[35 - row][vcol];
    if (addr >= 0x400) return;
    unsigned char chr  = memory[0x0000 + addr];
    unsigned char attr = memory[0x0400 + addr];
    int code = chr | ((attr & 0xC0) << 2);                 // 10 bits (but chars max 1024)
    if (code >= 1024) code = 0;
    int colgrp = attr & 0x0F;                              // 4 bits → 16 groups
    const unsigned short *tile   = &charmap_data[code * 8];
    const unsigned short *colors = &cmap_chars[colgrp * 4];
    unsigned short *ptr = rt + 8 * col;
    // Commando fg chars use palette index 3 as the transparent pen (not 0).
    for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
        unsigned short pix = tile[r];
        for (char c = 0; c < 8; c++, pix >>= 2) {
            unsigned p = pix & 3;
            if (p != 3) *ptr = colors[p];
            ptr++;
        }
    }
}

void commando::blit_sprite(short row, unsigned char s) {
    if (!spritemap_data || !cmap_sprites) return;
    int code = sprite[s].code;
    if (code >= 768) return;
    int orient = sprite[s].flags & 3;
    const unsigned long *spr = &spritemap_data[((orient * 768 + code) * 16) * 2];
    const unsigned short *colors = &cmap_sprites[(sprite[s].color & 0x03) * 16];

    int fb_x = sprite[s].x;
    // y_offset = position of sprite top relative to strip top (strip = 8*row..8*row+7)
    short y_offset = sprite[s].y - 8 * row;
    int strip_start, spr_start, lines2draw;
    if (y_offset >= 0) {
        strip_start = y_offset;
        spr_start   = 0;
        lines2draw  = 8 - y_offset;
        if (lines2draw > 16) lines2draw = 16;
    } else {
        strip_start = 0;
        spr_start   = -y_offset;
        lines2draw  = 8;
        if (spr_start + lines2draw > 16) lines2draw = 16 - spr_start;
    }
    if (lines2draw <= 0) return;

    for (int r = 0; r < lines2draw; r++) {
        unsigned long lo = spr[(spr_start + r) * 2];
        unsigned long hi = spr[(spr_start + r) * 2 + 1];
        unsigned short *ptr = rt + (strip_start + r) * 224 + fb_x;
        for (int c = 0; c < 8; c++, lo >>= 4) {
            int sx = fb_x + c;
            if (sx >= 0 && sx < 224) {
                unsigned int p = lo & 0x0F;
                if (p != 15) *ptr = colors[p];
            }
            ptr++;
        }
        for (int c = 0; c < 8; c++, hi >>= 4) {
            int sx = fb_x + 8 + c;
            if (sx >= 0 && sx < 224) {
                unsigned int p = hi & 0x0F;
                if (p != 15) *ptr = colors[p];
            }
            ptr++;
        }
    }
}

// Render trasposto 90°: cmd_full (224x288 portrait nativo, riempito in
// prepare_frame) -> frame_buffer (stride CMD_NH=288), come 1942.
void commando::render_row(short strip_r) {
    if (!cmd_full) return;
    for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
        int fb_y  = strip_r * 8 + fb_y_strip;
        int gfb_x = (CMD_NW - 1) - fb_y;          // colonna nativa <- riga landscape
        if (gfb_x < 0 || gfb_x >= CMD_NW) continue;
        unsigned short *dst       = frame_buffer + fb_y_strip * CMD_NH;
        const unsigned short *src = cmd_full + gfb_x;
        for (int gfb_y = 0; gfb_y < CMD_NH; gfb_y++)
            dst[gfb_y] = src[gfb_y * CMD_NW];
    }
}

const unsigned short *commando::logo(void) {
    return commando_logo;
}
