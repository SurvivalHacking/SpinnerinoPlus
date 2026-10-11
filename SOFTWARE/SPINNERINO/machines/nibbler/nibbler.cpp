// nibbler.cpp - Nibbler (Rock-Ola, 1982) implementation for GALAGINO
// Memory map matches MAME's fantasy_map (modern snk6502.cpp driver).
//
// Layer model:
//   BG (background): videoram  @ 0x0800, char bitmap from nibbler_gfx ROM
//   FG (foreground): videoram2 @ 0x0400, char bitmap from charram (CGRAM @ 0x1000)
//   Color RAM @ 0x0C00: bits 0-2 = FG palette bank, bits 3-5 = BG palette bank
//   FG pen 0 is transparent (BG shows through)

#include "nibbler.h"
#include "nibbler_rom.h"
#include "nibbler_gfx.h"
#include "nibbler_palette.h"
#include "nibbler_dipswitches.h"
#include "nibbler_logo.h"
#include "../../emulation/input.h"

// ── External 6502 emulator (fake6502.c) ──
extern void reset6502();
extern void exec6502(uint32_t tickcount);
extern void irq6502();
extern void irq6502_safe();
extern void nmi6502();
extern uint32_t clockticks6502;
extern uint32_t clockgoal6502;

// SPINNERINO: il glue read6502/write6502/opread6502 chiama
// currentMachine->rd6502/wr6502/op6502 -> niente m6502_current.

// ══════════════════════════════════════════════════════════
// Reset
// ══════════════════════════════════════════════════════════

void nibbler::reset() {
    // Alloca lazy il buffer nativo trasposto (come fantasy/scramble).
    if (!nib_full)
        nib_full = (unsigned short*)ps_malloc(NIB_NW * NIB_NH * sizeof(unsigned short));

    memset(memory, 0, NIB_MEM_TOTAL);
    memset(soundregs, 0, sizeof(soundregs));
    game_started = 0;

    clockticks6502 = 0;
    clockgoal6502 = 0;
    reset6502();

    coin_prev = false;
    scroll_x = 0;
    scroll_y = 0;
    ctrl_reg = 0;
    crunch_pos = -1;  // sample crunch idle

    // Init voci audio (MAME 2003-plus snk6502_sound_device fedele).
    for (int i = 0; i < NIB_NUM_VOICES; i++) {
        voices[i].base = 0;
        voices[i].offset = 0;
        voices[i].mask = 0xFF;
        voices[i].sample_cur = 0;
        voices[i].sample_step = 0;
        voices[i].mute = true;
        for (int j = 0; j < 16; j++) voices[i].form[j] = 0;
    }
    // Default waveform per evitare silenzio se il game non scrive 0x2102 subito.
    build_waveform(0, 0xF);
    build_waveform(1, 0xF);
    build_waveform(2, 0xF);
    bomb_active = 0;
    bomb_prev = false;
    sound0_stop_on_rollover = false;
}

// build_waveform da MAME 2003-plus snk6502_sound_device::build_waveform.
// Genera 16-sample waveform da 4-bit mask. Il game scrive 0x2102 per
// impostare le forme d'onda di ch0 e ch1.
void nibbler::build_waveform(int channel, int mask) {
    int bit0 = 0, bit1 = 0, bit2 = 0, bit3 = 0;
    if ((mask & 1) || (mask & 2))      bit3 = 8;
    else if (mask & 4)                  bit3 = 4;
    else if (mask & 8)                  bit3 = 2;
    if (mask & 4)                       bit2 = 8;
    else if ((mask & 2) || (mask & 8))  bit2 = 4;
    if (mask & 8)                       bit1 = 8;
    else if (mask & 4)                  bit1 = 4;
    else if (mask & 2)                  bit1 = 2;
    bit0 = bit1 >> 1;
    if (bit0 + bit1 + bit2 + bit3 < 16) {
        bit0 <<= 1; bit1 <<= 1; bit2 <<= 1; bit3 <<= 1;
    }
    const int base = (bit0 + bit1 + bit2 + bit3 + 1) / 2;
    for (int i = 0; i < 16; i++) {
        int data = ((i & 1) ? bit0 : 0) + ((i & 2) ? bit1 : 0)
                 + ((i & 4) ? bit2 : 0) + ((i & 8) ? bit3 : 0);
        voices[channel].form[i] = (short)((data - base) * 16);
    }
}

// ══════════════════════════════════════════════════════════
// Memory read
// ══════════════════════════════════════════════════════════

unsigned char nibbler::rd6502(unsigned short Addr) {
    // Fast path: RAM + VRAM + CRAM + CGRAM ($0000-$1FFF)
    if (Addr < 0x2000) {
        return memory[Addr];
    }

    // I/O area ($2000-$24FF). Only 0x2104-0x2107 are readable.
    // All Nibbler input ports are ACTIVE HIGH (bit set = button pressed).
    if (Addr >= 0x2104 && Addr <= 0x2107) {
        unsigned short btns = input->buttons_get();
        switch (Addr) {
            case 0x2104: {
                // IN0: player 1 joystick (4-way)
                //   bit 4=DOWN, 5=UP, 6=RIGHT, 7=LEFT
                unsigned char v = 0x00;
                if (btns & BUTTON_DOWN)  v |= 0x10;
                if (btns & BUTTON_UP)    v |= 0x20;
                if (btns & BUTTON_RIGHT) v |= 0x40;
                if (btns & BUTTON_LEFT)  v |= 0x80;
                return v;
            }
            case 0x2105:
                // IN1: player 2 cocktail joystick (no input in our setup)
                return 0x00;
            case 0x2106:
                // DSW: active high, defaults 0x00 = 3 lives / 1C/1Cr / upright / no service
                return NIBBLER_DSW;
            case 0x2107: {
                // IN2: bit 1=COIN1 (PORT_IMPULSE(1)), bit 7=START1
                // COIN1 is a 1-frame pulse when coin is newly inserted.
                unsigned char v = 0x00;
                if (coin_impulse > 0) v |= 0x02;      // COIN1 impulse
                if (btns & BUTTON_START) v |= 0x80;   // START1 continuous
                return v;
            }
        }
    }

    // ROM: $3000-$BFFF (linear, addressed as rom[addr - 0x3000])
    if (Addr >= 0x3000 && Addr < 0xC000) {
        return pgm_read_byte_near(nibbler_rom + (Addr - 0x3000));
    }

    // ROM mirror for 6502 vectors: $F000-$FFFF → g-0960-53 (offset 0x5000)
    if (Addr >= 0xF000) {
        return pgm_read_byte_near(nibbler_rom + 0x5000 + (Addr - 0xF000));
    }

    return 0xFF;
}

// ══════════════════════════════════════════════════════════
// Memory write
// ══════════════════════════════════════════════════════════

void nibbler::wr6502(unsigned short Addr, unsigned char Value) {
    // RAM + VRAM2 + VRAM + CRAM + CGRAM are all linear in memory[] up to 0x1FFF.
    if (Addr < 0x2000) {
        memory[Addr] = Value;
        if (Addr >= 0x0400 && Addr <= 0x0BFF) game_started = 1;
        return;
    }

    // I/O area ($2000-$24FF)
    if (Addr <= 0x2001) {
        // MC6845 CRTC - ignore (pure video timing chip, not needed for emulation)
        return;
    }
    // ===== Sound chip MAME 2003-plus snk6502_sound_device fedele =====
    // (fantasy_sound0/1/2_w da rockola_sndhrdw.c). Sostituisce l'AY-mapping
    // approssimato precedente. I 3 canali leggono il waveform ROM e producono
    // tone variabili (sweep di nascita serpente, music, ecc.).
    if (Addr == 0x2100) {
        // fantasy_sound0_w: select tune ROM ch0, ch0+ch2 mute, bomb sample.
        voices[0].base = (Value & 0x07) << 8;
        voices[0].mask = 0xFF;
        sound0_stop_on_rollover = false;
        if (Value & 0x08) {
            voices[0].mute = false;
        } else {
            voices[0].offset = voices[0].base;  // MAME 2003: Sound0Offset = Sound0Base
            voices[0].mute = true;
        }
        if (Value & 0x10) {
            voices[2].mute = false;
        } else {
            voices[2].offset = 0;
            voices[2].mute = true;
        }
        // bit 7: BOMB sample (edge-triggered, LFSR noise in audio.cpp)
        bool bomb_now = (Value & 0x80) != 0;
        if (bomb_now && !bomb_prev) bomb_active = 6000;
        bomb_prev = bomb_now;
        return;
    }
    if (Addr == 0x2101) {
        // fantasy_sound1_w: select tune ROM ch1, ch1 mute.
        unsigned int new_base = 0x0800 + ((Value & 0x07) << 8);
        voices[1].base = new_base;
        voices[1].mask = 0xFF;
        if (Value & 0x08) {
            voices[1].mute = false;
            // Crunch (discrete sound non emulato in MAME): sample WAV su
            // unmute ch1 + bank 1 (= 0x0900). Logica identica a P4_NEW_multi.
            if (new_base == 0x0900) crunch_pos = 0;
        } else {
            voices[1].mute = true;
            voices[1].offset = 0;
        }
        return;
    }
    if (Addr == 0x2102) {
        // MAME 2003-plus build_waveform per ch0 e ch1.
        // Bit pattern ch0: (data & 0x9) | ((data & 2) << 1) | ((data & 4) >> 1)
        // Bit pattern ch1: data >> 4
        int mask0 = (Value & 0x9) | ((Value & 2) << 1) | ((Value & 4) >> 1);
        build_waveform(0, mask0);
        build_waveform(1, (Value >> 4) & 0x0F);
        return;
    }
    if (Addr == 0x2103) {
        // fantasy_sound2_w: ctrl_reg + select tune ch2 base.
        ctrl_reg = Value;
        voices[2].base = 0x1000 + ((Value & 0x70) << 4);
        voices[2].mask = 0xFF;
        return;
    }
    if (Addr == 0x2200) { scroll_x = Value; return; }
    if (Addr == 0x2300) { scroll_y = Value; return; }
    if (Addr == 0x2400) {
        // Speech write (non usato in Nibbler).
        return;
    }

    // ROM area writes are no-ops on real hardware
}

// ══════════════════════════════════════════════════════════
// Opcode fetch - Nibbler has no encryption
// ══════════════════════════════════════════════════════════

unsigned char nibbler::op6502(unsigned short Addr) {
    return rd6502(Addr);
}

// ══════════════════════════════════════════════════════════
// Frame execution: run CPU, fire VBLANK IRQ, handle coin NMI
// ══════════════════════════════════════════════════════════

void nibbler::run_frame() {

    clockticks6502 = 0;
    clockgoal6502 = 0;

    // MAME sets VBLANK IRQ at the start of the VBLANK period (after visible
    // scanlines). Screen is 262 total / 224 visible → visible ≈ 85.5% of frame.
    int visible_cycles = (NIB_CYCLES_PER_FRAME * 224) / 262;   // ≈ 20102
    int vblank_cycles  = NIB_CYCLES_PER_FRAME - visible_cycles; // ≈ 3418

    exec6502(visible_cycles);
    irq6502_safe();              // fire VBLANK IRQ at start of vblank (honor I flag)
    exec6502(vblank_cycles);

    // Coin handling: PORT_IMPULSE(1) - coin bit 1 stays HIGH for 1 frame only,
    // plus NMI is fired on the coin press edge.
    unsigned short btns = input->buttons_get();
    if ((btns & BUTTON_COIN) && !coin_prev) {
        nmi6502();
        coin_impulse = 2;   // keep COIN1 bit HIGH for ~2 frames (debounce margin)
    }
    coin_prev = (btns & BUTTON_COIN) != 0;
    if (coin_impulse > 0) coin_impulse--;
}

void nibbler::prepare_frame() {
    active_sprites = 0;
    // --- Render NATIVO 224x256 in nib_full (blit via rt = base strip) ---
    if (nib_full) {
        memset(nib_full, 0, NIB_NW * NIB_NH * sizeof(unsigned short));
        for (short srow = 0; srow < NIB_ROWS; srow++) {
            rt = nib_full + srow * 8 * NIB_NW;
            render_strip(srow);
        }
    }
}

// ══════════════════════════════════════════════════════════
// Rendering (ROT90 CW: game 256×224 landscape → display 224×256 portrait)
// ══════════════════════════════════════════════════════════
// Display (col, row) ↔ Game (x_tile, y_tile):
//   x_game = row                   (0..31 display-row = 0..31 game-col)
//   y_game = 27 - col              (0..27 display-col inverted)
// Pixel within tile: display (dx, dy) ↔ game (gpx = dy, gpy = 7 - dx).
// ══════════════════════════════════════════════════════════

// render_strip: corpo dell'ex render_row (bg+fg tile) verso rt invece di
// frame_buffer. nib_full e' gia' azzerato in prepare_frame.
void nibbler::render_strip(short row) {
    if (row < 0 || row >= NIB_ROWS) return;
    // Render all 28 display columns (0..27), background then foreground
    for (char col = 0; col < NIB_COLS; col++) blit_bg_tile(row, col);
    for (char col = 0; col < NIB_COLS; col++) blit_fg_tile(row, col);
}

// Render trasposto 90: nib_full (224x256) -> frame_buffer (stride NIB_NH),
// come fantasy/scramble.
void nibbler::render_row(short strip_r) {
    if (!nib_full) return;
    for (int fy = 0; fy < 8; fy++) {
        int fb_y = strip_r * 8 + fy;               // 0..223 (asse corto display)
        int col  = (NIB_NW - 1) - fb_y;            // colonna nativa 223..0
        if (col < 0 || col >= NIB_NW) continue;
        unsigned short *dst       = frame_buffer + fy * NIB_NH;
        const unsigned short *src = nib_full + col;
        for (int y = 0; y < NIB_NH; y++)
            dst[y] = src[y * NIB_NW];
    }
}

// ── Background: game-side VRAM @ 0x0800 (+256 tiles via char_bank bit 3).
//    Bitmap from nibbler_gfx ROM (2bpp, bp0 @ 0x0000, bp1 @ 0x1000).
//    Palette: 32 entries in ic6 PROM region (offset 32..63).
void nibbler::blit_bg_tile(short row, char col) {
    int game_x = row;
    int game_y = 27 - col;
    int offs = game_y * 32 + game_x;
    // Nibbler BG char bank: tiles 256-511 contain the actual graphics
    // (tiles 0-255 are solid-fill backgrounds). We force char_bank = 1 since
    // the game does not explicitly toggle ctrl_reg bit 3 at runtime but the
    // hardware pinout makes it active by default for this board revision.
    int code = memory[NIB_MEM_VRAM + offs] + 256;
    unsigned char attr = memory[NIB_MEM_CRAM + offs];
    unsigned char bank = (attr >> 3) & 0x07;

    // Pre-load both bitplanes. Planes are swapped (matches swapcharlayout)
    // so bp0 ← g-0960-58 data (high plane) and bp1 ← g-0960-57 data (low plane).
    // This turns "yellow" walls into "white" as in the real Nibbler attract.
    unsigned char bp0[8], bp1[8];
    for (int i = 0; i < 8; i++) {
        bp0[i] = pgm_read_byte_near(nibbler_gfx + 0x1000 + code * 8 + i);
        bp1[i] = pgm_read_byte_near(nibbler_gfx + code * 8 + i);
    }

    const unsigned short *pal = &nibbler_palette[32 + bank * 4];

    // Write into framebuffer starting at display pixel (col*8, row*8), stride 224.
    // BG uses MSB-first bit order (same as MAME charlayout for ROM tiles).
    unsigned short *fb = rt + col * 8;
    for (int dy = 0; dy < 8; dy++, fb += (NIB_FB_STRIDE - 8)) {
        int gpx = dy;
        for (int dx = 0; dx < 8; dx++, fb++) {
            int gpy = 7 - dx;
            unsigned char p = ((bp0[gpy] >> (7 - gpx)) & 1)
                            | (((bp1[gpy] >> (7 - gpx)) & 1) << 1);
            *fb = pgm_read_word_near(&pal[p]);
        }
    }
}

// ── Foreground: game-side VRAM2 @ 0x0400, bitmap from CGRAM (dynamic, RAM-based).
//    Pen 0 transparent (BG shows through).
void nibbler::blit_fg_tile(short row, char col) {
    int game_x = row;
    int game_y = 27 - col;
    int offs = game_y * 32 + game_x;
    unsigned char code = memory[NIB_MEM_VRAM2 + offs];
    unsigned char attr = memory[NIB_MEM_CRAM + offs];
    unsigned char bank = attr & 0x07;

    // Bitplanes in CGRAM. MAME uses charlayout_memory { 0, 256*8*8 } meaning
    // plane 0 is at offset 0x000 and plane 1 at 0x800 — but that produced
    // white-instead-of-red for the snake. Swap them to match swapcharlayout.
    const unsigned char *bp0 = memory + NIB_MEM_CGRAM + 0x800 + code * 8;
    const unsigned char *bp1 = memory + NIB_MEM_CGRAM + code * 8;

    const unsigned short *pal = &nibbler_palette[bank * 4];

    unsigned short *fb = rt + col * 8;
    for (int dy = 0; dy < 8; dy++, fb += (NIB_FB_STRIDE - 8)) {
        int gpx = dy;
        for (int dx = 0; dx < 8; dx++, fb++) {
            int gpy = 7 - dx;
            unsigned char p = ((bp0[gpy] >> (7 - gpx)) & 1)
                            | (((bp1[gpy] >> (7 - gpx)) & 1) << 1);
            if (p == 0) continue;  // transparent
            *fb = pgm_read_word_near(&pal[p]);
        }
    }
}

const unsigned short *nibbler::logo() {
    return nibbler_logo;
}
