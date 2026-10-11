// brubber.cpp - Burnin' Rubber machine implementation for GALAGINO
// Data East BNJ board: DECO C10707 (encrypted 6502) + 6502 sound + 2x AY-3-8910
// ROOT copy compilata da Arduino IDE. Sync con machines/brubber/brubber.cpp.

#include "config.h"
#ifdef ENABLE_BRUBBER

#include "machines/brubber/brubber.h"
#include "machines/brubber/brubber_rom.h"
#include "machines/brubber/brubber_snd_rom.h"
#include "machines/brubber/brubber_tilemap.h"
#include "machines/brubber/brubber_spritemap.h"
#include "machines/brubber/brubber_bgmap.h"
#include "machines/brubber/brubber_dipswitches.h"
#include "machines/brubber/brubber_logo.h"
#include "emulation/input.h"          // BUTTON_* + Input
// m6502_state_t + m6502_save/m6502_restore + reset6502/exec6502/nmi6502/irq6502/
// clockticks6502/clockgoal6502 sono definiti in cpus/m6502/fake6502.c, che e'
// #include-ato in SPINNERINO.ino PRIMA di questo file (stessa TU) -> in scope.

// SPINNERINO: il glue read6502/write6502/opread6502 (emulation.cpp) chiama
// currentMachine->rd6502/wr6502/op6502 -> niente m6502_current.

// ── CPU state storage for dual-CPU switching ──
static m6502_state_t main_cpu_state;
static m6502_state_t sound_cpu_state;

// ══════════════════════════════════════════════════════════
// Reset - initialize 6502 CPUs instead of Z80
// ══════════════════════════════════════════════════════════

void brubber::reset() {
    // Alloca lazy il buffer nativo trasposto (come dkong/1942).
    if (!bb_full)
        bb_full = (unsigned short*)ps_malloc(BB_NW * BB_NH * sizeof(unsigned short));

    memset(memory, 0, BB_MEM_TOTAL);
    memset(soundregs, 0, sizeof(soundregs));
    game_started = 0;

    // Initialize main CPU
    is_sound_cpu = false;
    clockticks6502 = 0;
    clockgoal6502 = 0;
    reset6502();
    m6502_save(&main_cpu_state);

    // Initialize sound CPU
    is_sound_cpu = true;
    clockticks6502 = 0;
    clockgoal6502 = 0;
    reset6502();
    m6502_save(&sound_cpu_state);

    is_sound_cpu = false;

    // Init palette to black
    for (int i = 0; i < 16; i++) {
        palette_cache[i] = 0;
    }

    scroll_reg[0] = 0;
    scroll_reg[1] = 0;
    sound_latch = 0;
    sound_nmi_enable = false;
    nmi_enable = false;
    video_control = 0;
}

// 6502 memory callbacks are now defined globally in src.ino and
// dispatch through m6502_current->rdZ80/wrZ80/opZ80. brubber::opZ80
// still applies DECO C10707 decryption when needed.

// ══════════════════════════════════════════════════════════
// Memory access - Main CPU ($0000-$FFFF)
// ══════════════════════════════════════════════════════════

unsigned char brubber::rd6502(unsigned short Addr) {
    if (is_sound_cpu) {
        // ── Sound CPU memory map ──
        if (Addr < 0x0400) {
            return memory[BB_MEM_SNDRAM + (Addr & 0x3FF)];
        }
        if (Addr < 0x2000) {
            return memory[BB_MEM_SNDRAM + (Addr & 0x3FF)];  // mirrors
        }
        if (Addr >= 0xA000 && Addr < 0xC000) {
            return sound_latch;  // sound latch read
        }
        if (Addr >= 0xE000) {
            return pgm_read_byte_near(brubber_snd_rom + (Addr & 0xFFF));  // 4KB mirrored
        }
        return 0xFF;
    }

    // ── Main CPU memory map ──
    // RAM: $0000-$07FF
    if (Addr < 0x0800) {
        return memory[BB_MEM_RAM + Addr];
    }

    // Input ports: $1000-$1004
    if (Addr >= 0x1000 && Addr <= 0x1004) {
        unsigned short btns = input->buttons_get();
        switch (Addr & 0x07) {
            case 0: {                            // DSW1 + VBLANK on bit 7
                unsigned char val = BRUBBER_DSW1 & 0x7F;  // clear bit 7
                if (vblank_flag) val |= 0x80;             // bit 7 = VBLANK (active HIGH)
                return val;
            }
            case 1: return BRUBBER_DSW2;        // DSW2
            case 2: {                            // P1: joystick + fire
                unsigned char val = 0xFF;
                if (btns & BUTTON_RIGHT)  val &= ~0x01;  // bit 0
                if (btns & BUTTON_LEFT)   val &= ~0x02;  // bit 1
                if (btns & BUTTON_UP)     val &= ~0x04;  // bit 2
                if (btns & BUTTON_DOWN)   val &= ~0x08;  // bit 3
                if (btns & BUTTON_FIRE)   val &= ~0x10;  // bit 4 = jump
                return val;
            }
            case 3: return 0xFF;                // P2
            case 4: {                            // SYSTEM: coin(6-7) + start(3-4)
                unsigned char val = 0xFF;
                if (btns & BUTTON_START)  val &= ~0x08;  // bit 3 = start 1
                if (btns & BUTTON_COIN)   val &= ~0x40;  // bit 6 = coin 1
                return val;
            }
        }
    }

    // Video RAM: $4000-$43FF
    if (Addr >= 0x4000 && Addr < 0x4400) {
        return memory[BB_MEM_VRAM + (Addr & 0x3FF)];
    }

    // Color RAM: $4400-$47FF
    if (Addr >= 0x4400 && Addr < 0x4800) {
        return memory[BB_MEM_CRAM + (Addr & 0x3FF)];
    }

    // Mirror Video RAM (X/Y swapped): $4800-$4BFF
    if (Addr >= 0x4800 && Addr < 0x4C00) {
        int offs = Addr & 0x3FF;
        int x = offs / 32;
        int y = offs % 32;
        return memory[BB_MEM_VRAM + y * 32 + x];
    }

    // Mirror Color RAM (X/Y swapped): $4C00-$4FFF
    if (Addr >= 0x4C00 && Addr < 0x5000) {
        int offs = Addr & 0x3FF;
        int x = offs / 32;
        int y = offs % 32;
        return memory[BB_MEM_CRAM + y * 32 + x];
    }

    // Background RAM: $5000-$51FF
    if (Addr >= 0x5000 && Addr < 0x5200) {
        return memory[BB_MEM_BGRAM + (Addr & 0x1FF)];
    }

    // Extra RAM: $5200-$53FF
    if (Addr >= 0x5200 && Addr < 0x5400) {
        return memory[BB_MEM_EXTRA + (Addr & 0x1FF)];
    }

    // Palette RAM: $5C00-$5C0F
    if (Addr >= 0x5C00 && Addr < 0x5C10) {
        return memory[BB_MEM_PAL + (Addr & 0x0F)];
    }

    // ROM: $A000-$FFFF (brubber only uses $C000-$FFFF)
    if (Addr >= 0xC000) {
        return pgm_read_byte_near(brubber_rom + (Addr - 0xC000));
    }
    if (Addr >= 0xA000) {
        return 0xFF;  // service ROM area (not populated)
    }

    return 0xFF;
}

void brubber::wr6502(unsigned short Addr, unsigned char Value) {
    if (is_sound_cpu) {
        // ── Sound CPU writes ──
        if (Addr < 0x0400) {
            memory[BB_MEM_SNDRAM + (Addr & 0x3FF)] = Value;
            return;
        }
        if (Addr < 0x2000) {
            memory[BB_MEM_SNDRAM + (Addr & 0x3FF)] = Value;  // mirrors
            return;
        }
        // AY-3-8910 #1 data write: $2000-$3FFF
        if (Addr >= 0x2000 && Addr < 0x4000) {
            if (ay_addr[0] < 16) {
                ay_regs[0][ay_addr[0]] = Value;
                // Map to GALAGINO soundregs: AY1 regs 0-15 → soundregs[0-15]
                soundregs[ay_addr[0]] = Value;
            }
            return;
        }
        // AY-3-8910 #1 address write: $4000-$5FFF
        if (Addr >= 0x4000 && Addr < 0x6000) {
            ay_addr[0] = Value & 0x0F;
            return;
        }
        // AY-3-8910 #2 data write: $6000-$7FFF
        if (Addr >= 0x6000 && Addr < 0x8000) {
            if (ay_addr[1] < 16) {
                ay_regs[1][ay_addr[1]] = Value;
                // Map to GALAGINO soundregs: AY2 regs 0-15 → soundregs[16-31]
                soundregs[16 + ay_addr[1]] = Value;
            }
            return;
        }
        // AY-3-8910 #2 address write: $8000-$9FFF
        if (Addr >= 0x8000 && Addr < 0xA000) {
            ay_addr[1] = Value & 0x0F;
            return;
        }
        // NMI enable: $C000-$DFFF
        if (Addr >= 0xC000 && Addr < 0xE000) {
            sound_nmi_enable = (Value & 1) != 0;
            return;
        }
        return;
    }

    // ── Main CPU writes ──

    // RAM: $0000-$07FF
    if (Addr < 0x0800) {
        memory[BB_MEM_RAM + Addr] = Value;
        return;
    }

    // $1000 write: no-op (read-only DSW1 port, no NMI enable on BNJ board)

    // Video control: $1001
    if (Addr == 0x1001) {
        video_control = Value;
        return;
    }

    // Sound latch write: $1002
    if (Addr == 0x1002) {
        sound_latch = Value;
        // This triggers IRQ on sound CPU - will be processed next frame
        sound_latch_pending = true;
        return;
    }

    // Video RAM: $4000-$43FF
    if (Addr >= 0x4000 && Addr < 0x4400) {
        memory[BB_MEM_VRAM + (Addr & 0x3FF)] = Value;
        game_started = true;
        return;
    }

    // Color RAM: $4400-$47FF
    if (Addr >= 0x4400 && Addr < 0x4800) {
        memory[BB_MEM_CRAM + (Addr & 0x3FF)] = Value;
        return;
    }

    // Mirror Video RAM (X/Y swapped): $4800-$4BFF
    if (Addr >= 0x4800 && Addr < 0x4C00) {
        int offs = Addr & 0x3FF;
        int x = offs / 32;
        int y = offs % 32;
        memory[BB_MEM_VRAM + y * 32 + x] = Value;
        return;
    }

    // Mirror Color RAM (X/Y swapped): $4C00-$4FFF
    if (Addr >= 0x4C00 && Addr < 0x5000) {
        int offs = Addr & 0x3FF;
        int x = offs / 32;
        int y = offs % 32;
        memory[BB_MEM_CRAM + y * 32 + x] = Value;
        return;
    }

    // Background RAM: $5000-$51FF
    if (Addr >= 0x5000 && Addr < 0x5200) {
        memory[BB_MEM_BGRAM + (Addr & 0x1FF)] = Value;
        return;
    }

    // Extra RAM: $5200-$53FF
    if (Addr >= 0x5200 && Addr < 0x5400) {
        memory[BB_MEM_EXTRA + (Addr & 0x1FF)] = Value;
        return;
    }

    // Scroll registers
    if (Addr == 0x5400) { scroll_reg[0] = Value; return; }
    if (Addr == 0x5800) { scroll_reg[1] = Value; return; }

    // Palette RAM: $5C00-$5C0F
    if (Addr >= 0x5C00 && Addr < 0x5C10) {
        unsigned char idx = Addr & 0x0F;
        memory[BB_MEM_PAL + idx] = Value;
        update_palette(idx, Value);
        return;
    }
}

// ── Opcode fetch (DECO C10707 decryption for main CPU) ──
unsigned char brubber::op6502(unsigned short Addr) {
    if (is_sound_cpu) {
        // Sound CPU: standard 6502, no encryption
        if (Addr >= 0xE000) {
            return pgm_read_byte_near(brubber_snd_rom + (Addr & 0xFFF));
        }
        if (Addr < 0x0400) {
            return memory[BB_MEM_SNDRAM + (Addr & 0x3FF)];
        }
        return 0xFF;
    }

    // Main CPU: DECO C10707 = swap bits 5 and 6 of opcode
    unsigned char raw;
    if (Addr >= 0xC000) {
        raw = pgm_read_byte_near(brubber_rom + (Addr - 0xC000));
    } else if (Addr < 0x0800) {
        raw = memory[BB_MEM_RAM + Addr];
    } else {
        raw = 0xFF;
    }

    // DECO C10707 bitswap: bits [7,5,6,4,3,2,1,0]
    // Swap bit 5 and bit 6
    unsigned char decrypted = (raw & 0x9F) | ((raw & 0x20) << 1) | ((raw & 0x40) >> 1);
    return decrypted;
}

// (outZ80/inZ80 rimossi: il 6502 non ha porte I/O)

// ══════════════════════════════════════════════════════════
// Palette conversion: BGR233 inverted → RGB565 byte-swapped
// ══════════════════════════════════════════════════════════

void brubber::update_palette(unsigned char idx, unsigned char val) {
    // BGR233 inverted: invert all bits first
    val = ~val;

    // Extract: bits 7-6 = Blue (2bit), bits 5-3 = Green (3bit), bits 2-0 = Red (3bit)
    unsigned char b2 = (val >> 6) & 0x03;
    unsigned char g3 = (val >> 3) & 0x07;
    unsigned char r3 = val & 0x07;

    // Scale to 5-6-5 bits
    unsigned char r5 = (r3 * 31) / 7;
    unsigned char g6 = (g3 * 63) / 7;
    unsigned char b5 = (b2 * 31) / 3;

    // Pack RGB565 and byte-swap for SPI
    unsigned short rgb565 = (r5 << 11) | (g6 << 5) | b5;
    palette_cache[idx] = ((rgb565 & 0xFF00) >> 8) | ((rgb565 & 0x00FF) << 8);
}

// ══════════════════════════════════════════════════════════
// Frame execution
// ══════════════════════════════════════════════════════════

void brubber::run_frame() {
    if (!game_started) game_started = true;

    // ── Run main CPU ──
    is_sound_cpu = false;
    m6502_restore(&main_cpu_state);
    clockticks6502 = 0;
    clockgoal6502 = 0;

    // The game polls DSW1 bit 7 for VBLANK timing in a tight loop.
    // exec6502() runs N cycles without returning, so vblank_flag must
    // change DURING execution. We split the frame into small slices
    // and toggle vblank_flag at the right point, so the polling loop
    // sees the transition.
    {
        int visible_cycles = BB_MAIN_CYCLES_PER_FRAME * 75 / 100;  // ~9375
        int vblank_cycles  = BB_MAIN_CYCLES_PER_FRAME * 25 / 100;  // ~3125
        int slice = 64;  // small enough that polling loop catches transition

        vblank_flag = false;
        for (int run = 0; run < visible_cycles; run += slice)
            exec6502(slice);

        vblank_flag = true;
        for (int run = 0; run < vblank_cycles; run += slice)
            exec6502(slice);
    }

    // Coin NMI: edge-triggered on coin button press
    unsigned short btns = input->buttons_get();
    if ((btns & BUTTON_COIN) && !coin_prev) {
        nmi6502();
        exec6502(2000);
    }
    coin_prev = (btns & BUTTON_COIN) != 0;

    m6502_save(&main_cpu_state);

    // ── Run sound CPU ──
    run_sound_frame();
}

void brubber::run_sound_frame() {
    is_sound_cpu = true;
    m6502_restore(&sound_cpu_state);
    clockticks6502 = 0;
    clockgoal6502 = 0;

    // Send IRQ if main CPU wrote sound latch
    if (sound_latch_pending) {
        irq6502();
        sound_latch_pending = false;
    }

    // Sound CPU NMI from scanline counter: toggles every 8 lines
    // 272 lines / 8 = 34 periods, NMI fires every OTHER period = ~17 per frame
    // Each NMI chunk = 8333 / 17 ≈ 490 cycles
    int nmi_count = 17;
    int chunk = BB_SND_CYCLES_PER_FRAME / nmi_count;
    for (int i = 0; i < nmi_count; i++) {
        exec6502(chunk);
        if (sound_nmi_enable) {
            nmi6502();
        }
    }

    m6502_save(&sound_cpu_state);
    is_sound_cpu = false;
}

// ══════════════════════════════════════════════════════════
// Sprite preparation
// ══════════════════════════════════════════════════════════

void brubber::prepare_frame() {
    active_sprites = 0;

    // Sprites are in the mirror videoram area (X/Y swapped)
    // 8 sprites, interleave 0x20 (32 bytes)
    // Sprite data in VRAM mirror column: offsets 0, 0x20, 0x40, 0x60 from base
    // The sprite data lives in the mirror region $4800-$4900
    // Which maps to VRAM via X/Y swap

    // From MAME: sprites are at videoram mirror, 8 sprites, interleave 0x20
    // Byte 0 (offs+0): bit0=enable, bit1=flipY, bit2=flipX
    // Byte 1 (offs+0x20): tile code
    // Byte 2 (offs+0x40): Y position (240 - val)
    // Byte 3 (offs+0x60): X position (240 - val)

    for (int i = 0; i < 8 && active_sprites < 64; i++) {
        int offs = i * 4;  // 4 bytes per sprite in the mirror column

        // Read from mirror VRAM area (first column of swapped region)
        unsigned char ctrl = memory[BB_MEM_VRAM + offs * 32];       // X/Y swapped: col=offs, row=0
        unsigned char code = memory[BB_MEM_VRAM + offs * 32 + 32];  // interleave 0x20 in mirror = next row
        unsigned char sy_raw = memory[BB_MEM_VRAM + offs * 32 + 64];
        unsigned char sx_raw = memory[BB_MEM_VRAM + offs * 32 + 96];

        if (!(ctrl & 0x01)) continue;  // sprite not enabled

        // MAME positions (original coords before ROT270)
        int mx = 240 - sx_raw;   // MAME x
        int my = 240 - sy_raw;   // MAME y

        // ROT270: disp_x = orig_y - 8 (crop), disp_y = 240 - orig_x
        int disp_x = my - 8;
        int disp_y = 240 - mx;   // = sx_raw

        if (disp_x <= -16 || disp_x >= 240 || disp_y <= -16 || disp_y >= 256) continue;

        sprite[active_sprites].code = code;
        sprite[active_sprites].flags = ((ctrl >> 1) & 0x03);  // bit1=flipY→flag bit0, bit2=flipX→flag bit1
        sprite[active_sprites].color = 0;  // all sprites use palette 0-7
        sprite[active_sprites].x = disp_x;
        sprite[active_sprites].y = disp_y;
        active_sprites++;
    }

    // --- Render NATIVO 240x256 in bb_full (blit via rt = base strip) ---
    // render_row() trasporra' poi bb_full nel framebuffer (stride BB_NH).
    if (bb_full) {
        memset(bb_full, 0, BB_NW * BB_NH * sizeof(unsigned short));
        for (short srow = 0; srow < BB_ROWS; srow++) {
            rt = bb_full + srow * 8 * BB_NW;
            render_strip(srow);
        }
    }
}

// ══════════════════════════════════════════════════════════
// Rendering
// ══════════════════════════════════════════════════════════

// render_strip: corpo dell'ex render_row (bg + tile + sprite) verso rt
// (strip nativa di bb_full) invece di frame_buffer.
void brubber::render_strip(short row) {
    if (row < 0 || row >= BB_ROWS) return;

    // ── Background layer (when scroll is active) ──
    if (scroll_reg[0]) {
        render_background(row);
    }

    // ── Foreground tiles (transparent when background active) ──
    bool transparent = (scroll_reg[0] != 0);
    for (char col = 0; col < BB_COLS; col++) {
        blit_tile_ex(row, col, transparent);
    }

    // ── Sprites ──
    for (unsigned char s = 0; s < active_sprites; s++) {
        int sy = sprite[s].y;
        if (sy < 8 * (row + 1) && (sy + 16) > 8 * row) {
            blit_sprite(row, s);
        }
    }
}

// Render trasposto 90: bb_full (240x256, riempito in prepare_frame) ->
// frame_buffer (stride BB_NH=256), come dkong/1942. MA il path verticale di
// SPINNERINO rende solo 224 colonne (28 strip): brubber è 240 largo, quindi
// SCALO 240->224 (squish orizzontale ~7%) per non perdere strada/HUD ai bordi.
#define BB_DISP_W 224   // colonne effettive del path verticale SPINNERINO
void brubber::render_row(short strip_r) {
    if (!bb_full) return;
    for (int fy = 0; fy < 8; fy++) {
        int fb_y = strip_r * 8 + fy;               // 0..223 (asse corto display)
        if (fb_y >= BB_DISP_W) continue;
        // scala 224 colonne display -> 240 native, con mirror (ROT270)
        int col = (BB_NW - 1) - (fb_y * BB_NW) / BB_DISP_W;
        if (col < 0) col = 0;
        if (col >= BB_NW) col = BB_NW - 1;
        unsigned short *dst       = frame_buffer + fy * BB_NH;
        const unsigned short *src = bb_full + col;
        for (int y = 0; y < BB_NH; y++)
            dst[y] = src[y * BB_NW];
    }
}

// ── Background rendering (bnj scrolling background) ──
// BG tiles NOT pre-rotated (direct pixel access row=py, col=px).
// ROT270: screen_x = 255 - display_y, screen_y = display_x + 8
void brubber::render_background(short row) {
    // Scroll formula from MAME (non-flip)
    int scroll_raw = (scroll_reg[0] & 0x02) * 128 + 511 - scroll_reg[1];
    int scroll = 767 - scroll_raw;

    for (int line = 0; line < 8; line++) {
        int display_y = row * 8 + line;
        int screen_x = 255 - display_y;

        // Background bitmap X (512 wide, horizontal scroll)
        int bx = (screen_x - scroll + 2048) & 0x1FF;

        int tile_x = bx >> 4;          // 0-31
        int px = bx & 0x0F;            // pixel X within tile

        // Reverse MAME sx: col_index = 31 - tile_x
        int col_index = 31 - tile_x;

        unsigned short *fb_line = rt + line * BB_NW;

        for (int display_x = 0; display_x < 240; display_x++) {
            int by = display_x + 8;    // screen_y = display_x + 8
            int tile_y = by >> 4;       // 0-15
            int py = by & 0x0F;

            // Background RAM offset (MAME reverse formula)
            int offs;
            int col_bits = (col_index < 16) ? col_index : (col_index - 16);
            int row_bits = tile_y & 7;
            int bit7 = (tile_y >= 8) ? 0x80 : 0;
            offs = (col_bits << 3) | row_bits | bit7;
            if (col_index >= 16) offs |= 0x100;

            unsigned char bg_byte = memory[BB_MEM_BGRAM + offs];
            int tile_code = (bg_byte >> 4) + ((offs & 0x80) >> 3) + 32;
            if (tile_code >= BRUBBER_NUM_BG_TILES) continue;

            // BG tiles NOT pre-rotated: read with row=py, col=px
            int word_idx = py * 2 + (px >= 8 ? 1 : 0);
            int bit_pos = (px & 7) * 4;

            unsigned long pix_word = pgm_read_dword_near(&brubber_bgmap[tile_code][word_idx]);
            unsigned char color_idx = (pix_word >> bit_pos) & 0x07;

            // Background uses palette entries 8-15 (opaque)
            fb_line[display_x] = palette_cache[8 + color_idx];
        }
    }
}

// Extended tile blit with transparency option
void brubber::blit_tile_ex(short row, char col, bool transparent) {
    int vram_offs = row * 32 + (col + 1);
    if (vram_offs >= 1024) return;

    unsigned char tile_code_lo = memory[BB_MEM_VRAM + vram_offs];
    unsigned char color_attr = memory[BB_MEM_CRAM + vram_offs];
    int tile_code = tile_code_lo + 256 * (color_attr & 0x03);

    if (tile_code >= BRUBBER_NUM_TILES) tile_code = 0;

    const unsigned short *colors = palette_cache;
    unsigned short *ptr = rt + 8 * col;

    for (int r = 0; r < 8; r++, ptr += (240 - 8)) {
        unsigned long pix = pgm_read_dword_near(&brubber_tilemap[tile_code][r]);
        for (int c = 0; c < 8; c++, pix >>= 4) {
            unsigned char idx = pix & 0x07;
            if (idx) {
                *ptr = colors[idx];
            } else if (!transparent) {
                *ptr = colors[0];  // draw black background when no bg layer
            }
            ptr++;
        }
    }
}

void brubber::blit_tile(short row, char col) {
    // After ROT270, GALAGINO row R maps to original tile column (31-R) if we flip,
    // or directly to VRAM based on our tileaddr mapping.
    // VRAM layout: offset = orig_x_tile * 32 + orig_y_tile
    // Where: orig_x_tile = row (0-31), orig_y_tile = col + 1 (skip invisible col 0)

    int vram_offs = row * 32 + (col + 1);  // +1 to skip invisible column
    if (vram_offs >= 1024) return;

    unsigned char tile_code_lo = memory[BB_MEM_VRAM + vram_offs];
    unsigned char color_attr = memory[BB_MEM_CRAM + vram_offs];
    int tile_code = tile_code_lo + 256 * (color_attr & 0x03);  // up to 1024 tiles

    if (tile_code >= BRUBBER_NUM_TILES) tile_code = 0;

    // Get palette colors (chars use palette entries 0-7)
    const unsigned short *colors = palette_cache;  // entries 0-7

    unsigned short *ptr = rt + 8 * col;

    for (int r = 0; r < 8; r++, ptr += (240 - 8)) {
        unsigned long pix = pgm_read_dword_near(&brubber_tilemap[tile_code][r]);
        for (int c = 0; c < 8; c++, pix >>= 4) {
            unsigned char idx = pix & 0x07;
            if (idx) {
                *ptr = colors[idx];
            }
            ptr++;
        }
    }
}

void brubber::blit_sprite(short row, unsigned char s) {
    int code = sprite[s].code;
    int flags = sprite[s].flags;
    int sx = sprite[s].x;
    int sy = sprite[s].y;

    if (code >= BRUBBER_NUM_SPRITES) return;

    // Sprites use palette entries 0-7 (same as chars)
    const unsigned short *colors = palette_cache;

    int y_offset = sy - 8 * row;

    // Calculate visible lines within this 8-pixel row band
    int startline = 0;
    int lines2draw = 8;

    if (y_offset > 0) {
        startline = y_offset;
        lines2draw = 8 - y_offset;
    } else if (y_offset < -8) {
        startline = 0;
        lines2draw = 16 + y_offset;
    }
    if (lines2draw <= 0 || lines2draw > 16) return;

    int spr_start_row = (y_offset < 0) ? -y_offset : 0;

    for (int r = 0; r < lines2draw; r++) {
        int spr_row = spr_start_row + r;
        if (spr_row < 0 || spr_row >= 16) continue;

        // Each sprite row = 2 unsigned longs (lo: pixels 0-7, hi: pixels 8-15)
        unsigned long pix_lo = pgm_read_dword_near(&brubber_spritemap[flags & 3][code][spr_row * 2]);
        unsigned long pix_hi = pgm_read_dword_near(&brubber_spritemap[flags & 3][code][spr_row * 2 + 1]);

        unsigned short *ptr = rt + sx + BB_NW * (startline + r);

        // Left half (pixels 0-7)
        for (int c = 0; c < 8; c++) {
            int px = sx + c;
            if (px >= 0 && px < 240) {
                unsigned char idx = pix_lo & 0x07;
                if (idx) {
                    *ptr = colors[idx];
                }
            }
            pix_lo >>= 4;
            ptr++;
        }

        // Right half (pixels 8-15)
        for (int c = 0; c < 8; c++) {
            int px = sx + 8 + c;
            if (px >= 0 && px < 240) {
                unsigned char idx = pix_hi & 0x07;
                if (idx) {
                    *ptr = colors[idx];
                }
            }
            pix_hi >>= 4;
            ptr++;
        }
    }
}

// ══════════════════════════════════════════════════════════
// Logo (provisional text logo for TFT menu)
// ══════════════════════════════════════════════════════════

const unsigned short *brubber::logo() {
    return brubber_logo;
}

#endif // ENABLE_BRUBBER
