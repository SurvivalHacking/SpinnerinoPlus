// ROOT copy compilata da Arduino IDE 2.x. Sync con machines/gng/gng.cpp (VUOTO).
//
// FASE 3: gameplay completo (porting from GALAGONE 2.8VPE machines/gng/gng.cpp).
// MC6809 main + Z80 sound + 2x YM2203 (SSG via soundregs[], FM via ym_regs[]).
// ROM in PSRAM (lazy-load al primo reset, pattern Commando).
//
// Sorgente di riferimento:
//   PROGETTI SH\GALAGONE 2.8VPE\GALAGONE\machines\gng\gng.cpp

#include "config.h"
#include "machines/gng/gng.h"

#ifdef ENABLE_GNG

#include "esp_heap_caps.h"

// ROM definitions (extern decl in gng.h, una sola copia in flash).
#include "machines/gng/gng_rom.h"
#include "machines/gng/gng_bank_rom.h"
#include "machines/gng/gng_snd_rom.h"
#include "machines/gng/gng_charmap.h"
#include "machines/gng/gng_tilemap.h"
#include "machines/gng/gng_spritemap.h"

// Helper: copia 'src' (flash) in PSRAM e ritorna il puntatore.
static const void *load_to_psram(const void *src, size_t size, const char *name) {
    void *dst = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    if (!dst) {
        Serial.printf("[GnG] PSRAM alloc FALLITA per %s (%u B), uso flash\n",
                      name, (unsigned)size);
        return src;
    }
    memcpy(dst, src, size);
    Serial.printf("[GnG] PSRAM %s -> %p (%u B)\n", name, dst, (unsigned)size);
    return dst;
}

// ============================================================
// M6809 callback bridge
// ============================================================

gng *g_gng_instance = nullptr;

static uint8_t gng_m6809_read(uint16_t addr)        { return g_gng_instance->main_read(addr); }
static void    gng_m6809_write(uint16_t addr, uint8_t val) { g_gng_instance->main_write(addr, val); }
static uint8_t gng_m6809_read_opcode(uint16_t addr) { return g_gng_instance->main_read(addr); }

// ============================================================
// Init / Reset
// ============================================================

void gng::init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) {
    machineBase::init(input, framebuffer, spritebuffer, memorybuffer);
    g_gng_instance = this;
    if (!fg_vram) fg_vram = (uint8_t*)malloc(2048);
    if (!bg_vram) bg_vram = (uint8_t*)malloc(2048);
}

void gng::reset() {
    // Lazy-load ROM in PSRAM la prima volta (idempotente).
    if (!rom_main) {
        rom_main       = (const unsigned char *)load_to_psram(gng_rom,        40960,  "rom_main");
        rom_bank       = (const unsigned char *)load_to_psram(gng_bank_rom,   40960,  "rom_bank");
        rom_snd        = (const unsigned char *)load_to_psram(gng_snd_rom,    32768,  "rom_snd");
        charmap_data   = (const unsigned char *)load_to_psram(gng_charmap,    65536,  "charmap");
        tilemap_data   = (const unsigned char *)load_to_psram(gng_tilemap,    262144, "tilemap");
        spritemap_data = (const unsigned char *)load_to_psram(gng_spritemap,  262144, "spritemap");
    }

    memset(memory, 0, 8192);
    memset(soundregs, 0, sizeof(soundregs));
    current_cpu = 0;
    game_started = 0;

    // Reset sound Z80
    ResetZ80(&cpu[0]);

    // Set up M6809 callbacks (function pointer globals shared with other m6809 games)
    g_gng_instance = this;
    m6809_read_fn   = gng_m6809_read;
    m6809_write_fn  = gng_m6809_write;
    m6809_opcode_fn = gng_m6809_read_opcode;

    // Clear video RAM
    if (fg_vram) memset(fg_vram, 0, 2048);
    if (bg_vram) memset(bg_vram, 0, 2048);

    // Clear palette RAM
    memset(pal_hi, 0, sizeof(pal_hi));
    memset(pal_lo, 0, sizeof(pal_lo));
    memset(pal_rgb565, 0, sizeof(pal_rgb565));

    // Reset control registers
    scroll_x = 0;
    scroll_y = 0;
    bank_select = 0;
    soundlatch = 0;
    flipscreen = 0;
    memset(mainlatch, 0, sizeof(mainlatch));
    // MAME: q5 reset state = 0, q5=0 → IRQ ASSERT (enabled). Senza, SYNC del
    // game ROM resta bloccato in attesa di IRQ → freeze.
    vblank_irq_enable = 1;

    // Reset sound state
    memset(snd_ram, 0, sizeof(snd_ram));
    memset(ym_addr, 0, sizeof(ym_addr));
    memset(ym_regs, 0, sizeof(ym_regs));
    memset(fm_keyed, 0, sizeof(fm_keyed));
    snd_irq_pending = 0;

    // Reset M6809 (reads reset vector from 0xFFFE)
    m6809_reset(&main_cpu);

    // Silence "Unrecognized instruction" spam
    cpu[0].TrapBadOps = 0;
}

// ============================================================
// M6809 Main CPU memory map (MAME gng.cpp - VERIFICATO)
// ============================================================

uint8_t gng::main_read(uint16_t addr) {
    if (addr < 0x2000) return memory[addr];
    if (addr < 0x2800) return fg_vram[addr - 0x2000];
    if (addr < 0x3000) return bg_vram[addr - 0x2800];

    if (addr == 0x3000) {
        unsigned char k = input->buttons_get();
        unsigned char r = 0xFF;
        if (k & BUTTON_START) r &= ~0x01;  // START1
        if (k & BUTTON_COIN)  r &= ~0x40;  // COIN1
        return r;
    }
    if (addr == 0x3001) {
        unsigned char k = input->buttons_get();
        unsigned char r = 0xFF;
        if (k & BUTTON_RIGHT) r &= ~0x01;
        if (k & BUTTON_LEFT)  r &= ~0x02;
        if (k & BUTTON_DOWN)  r &= ~0x04;
        if (k & BUTTON_UP)    r &= ~0x08;
        if (k & BUTTON_FIRE)  r &= ~0x10;  // Attack
        if (k & BUTTON_EXTRA) r &= ~0x20;  // Jump
        return r;
    }
    if (addr == 0x3002) return 0xFF;
    if (addr == 0x3003) return GNG_DSW1;
    if (addr == 0x3004) return GNG_DSW2;

    // Banked ROM 0x4000-0x5FFF (8KB per bank)
    if (addr >= 0x4000 && addr < 0x6000)
        return rom_bank[(uint32_t)bank_select * 0x2000 + (addr - 0x4000)];

    // Fixed ROM 0x6000-0xFFFF (40KB)
    if (addr >= 0x6000) return rom_main[addr - 0x6000];

    return 0xFF;
}

void gng::main_write(uint16_t addr, uint8_t val) {
    if (addr < 0x2000) {
        if (!game_started) game_started = 1;
        memory[addr] = val;
        return;
    }
    if (addr < 0x2800) { fg_vram[addr - 0x2000] = val; return; }
    if (addr < 0x3000) { bg_vram[addr - 0x2800] = val; return; }

    // Palette EXT (RRRR_GGGG) 0x3800-0x38FF
    if (addr >= 0x3800 && addr < 0x3900) {
        uint8_t idx = addr - 0x3800;
        pal_hi[idx] = val;
        update_palette(idx);
        return;
    }
    // Palette MAIN (BBBB_xxxx) 0x3900-0x39FF
    if (addr >= 0x3900 && addr < 0x3A00) {
        uint8_t idx = addr - 0x3900;
        pal_lo[idx] = val;
        update_palette(idx);
        return;
    }

    // Sound latch 0x3A00 (no Z80 IRQ — Z80 polls; IRQ driven by 240 Hz timer)
    if (addr == 0x3A00) { soundlatch = val; return; }

    // scroll_x 0x3B08-0x3B09 (LSB/MSB)
    if (addr == 0x3B08) { scroll_x = (scroll_x & 0xFF00) | val; return; }
    if (addr == 0x3B09) { scroll_x = (scroll_x & 0x00FF) | ((uint16_t)val << 8); return; }
    // scroll_y 0x3B0A-0x3B0B (LSB/MSB)
    if (addr == 0x3B0A) { scroll_y = (scroll_y & 0xFF00) | val; return; }
    if (addr == 0x3B0B) { scroll_y = (scroll_y & 0x00FF) | ((uint16_t)val << 8); return; }

    // Sprite RAM buffer trigger 0x3C00 — single-buffer, noop.
    if (addr == 0x3C00) return;

    // Mainlatch LS259 0x3D00-0x3D07
    if (addr >= 0x3D00 && addr < 0x3D08) {
        uint8_t bit = addr & 7;
        mainlatch[bit] = val & 1;
        if (bit == 3) {
            flipscreen = val & 1;
        } else if (bit == 5) {
            // q5 inverted = IRQ enable: q5=0 enabled, q5=1 disabled/ack
            vblank_irq_enable = !(val & 1);
            if (val & 1) main_cpu.irq_pending = 0;
        }
        return;
    }

    // Bank switch 0x3E00
    if (addr == 0x3E00) {
        if (val == 4) bank_select = 4;       // special "first 8K extra" bank
        else          bank_select = val & 0x03;
        return;
    }
}

// ============================================================
// Palette: RGBx_444 → RGB565 byte-swapped
// ============================================================

void gng::update_palette(uint8_t idx) {
    uint8_t hi = pal_hi[idx];
    uint8_t lo = pal_lo[idx];
    uint8_t r4 = (hi >> 4) & 0x0F;
    uint8_t g4 = hi & 0x0F;
    uint8_t b4 = (lo >> 4) & 0x0F;
    uint8_t r8 = r4 * 17, g8 = g4 * 17, b8 = b4 * 17;
    uint16_t r5 = r8 >> 3, g6 = g8 >> 2, b5 = b8 >> 3;
    uint16_t rgb565 = (r5 << 11) | (g6 << 5) | b5;
    pal_rgb565[idx] = ((rgb565 >> 8) & 0xFF) | ((rgb565 & 0xFF) << 8);
}

// ============================================================
// Sound CPU (Z80) — 2x YM2203 (SSG/PSG portion only for now)
// ============================================================

unsigned char gng::opZ80(unsigned short Addr) {
    if (Addr < 0x8000) return rom_snd[Addr];
    return 0xFF;
}

unsigned char gng::rdZ80(unsigned short Addr) {
    if (Addr < 0x8000) return rom_snd[Addr];
    if (Addr >= 0xC000 && Addr < 0xC800) return snd_ram[Addr - 0xC000];
    if (Addr >= 0xC800 && Addr < 0xD000) return soundlatch;

    if (Addr == 0xE000) return 0x00;
    if (Addr == 0xE001) {
        uint8_t reg = ym_addr[0];
        if (reg < 16) return ym_regs[0][reg];
        return 0xFF;
    }
    if (Addr == 0xE002) return 0x00;
    if (Addr == 0xE003) {
        uint8_t reg = ym_addr[1];
        if (reg < 16) return ym_regs[1][reg];
        return 0xFF;
    }
    return 0xFF;
}

void gng::wrZ80(unsigned short Addr, unsigned char Value) {
    if (Addr >= 0xC000 && Addr < 0xC800) { snd_ram[Addr - 0xC000] = Value; return; }

    if (Addr == 0xE000) { ym_addr[0] = Value; return; }
    if (Addr == 0xE001) {
        uint8_t reg = ym_addr[0];
        ym_regs[0][reg] = Value;
        if (reg < 14) soundregs[reg] = Value;
        if (reg == 0x28) {
            uint8_t ch = Value & 0x03;
            if (ch < 3) fm_keyed[0][ch] = (Value >> 4) & 0x0F;
        }
        return;
    }
    if (Addr == 0xE002) { ym_addr[1] = Value; return; }
    if (Addr == 0xE003) {
        uint8_t reg = ym_addr[1];
        ym_regs[1][reg] = Value;
        if (reg < 14) soundregs[16 + reg] = Value;
        if (reg == 0x28) {
            uint8_t ch = Value & 0x03;
            if (ch < 3) fm_keyed[1][ch] = (Value >> 4) & 0x0F;
        }
        return;
    }
}

unsigned char gng::inZ80(unsigned short Port) { return 0xFF; }
void gng::outZ80(unsigned short Port, unsigned char Value) { }

// ============================================================
// Frame execution
// ============================================================

void gng::run_frame(void) {
    // Long-press safety + main M6809 + Z80 sound + 240 Hz Z80 IRQ.
    (void)input->buttons_get();

    // M6809 callbacks possono essere stati riassegnati da un altro gioco
    // m6809-based: re-set ad ogni frame per sicurezza.
    g_gng_instance = this;
    m6809_read_fn   = gng_m6809_read;
    m6809_write_fn  = gng_m6809_write;
    m6809_opcode_fn = gng_m6809_read_opcode;

    current_cpu = 0;
    const int irq_slot = GNG_STEPS_PER_FRAME / 4;
    for (int i = 0; i < GNG_STEPS_PER_FRAME; i++) {
        m6809_step(&main_cpu);
        m6809_step(&main_cpu);
        m6809_step(&main_cpu);
        m6809_step(&main_cpu);

        StepZ80(&cpu[0]);
        StepZ80(&cpu[0]);
        StepZ80(&cpu[0]);
        StepZ80(&cpu[0]);
        StepZ80(&cpu[0]);
        StepZ80(&cpu[0]);

        // 240 Hz Z80 IRQ (4 evenly-spaced slots/frame)
        if ((i % irq_slot) == (irq_slot - 1) && (cpu[0].IFF & IFF_1)) {
            IntZ80(&cpu[0], INT_RST38);
        }
    }

    // VBLANK IRQ to main CPU (gated by mainlatch q5)
    if (vblank_irq_enable) m6809_irq(&main_cpu);
}

// ============================================================
// Sprite RAM extraction
// ============================================================

void gng::prepare_frame(void) {
    active_sprites = 0;
    for (int offs = 0x1FC; offs >= 0; offs -= 4) {
        uint8_t *sr = &memory[0x1E00 + offs];
        uint8_t attr = sr[1];
        int code = sr[0] | ((attr & 0xC0) << 2);  // 10-bit code
        int sy = sr[2];
        int sx = sr[3] - ((attr & 0x01) ? 256 : 0);

        if (sy < 1 || sy > 239) continue;
        if (sx < -15 || sx > 255) continue;

        sprite[active_sprites].code        = code & 0xFF;
        sprite[active_sprites].color_block = (code >> 8) & 0x03;  // bank
        sprite[active_sprites].color       = (attr >> 4) & 0x03;
        sprite[active_sprites].x = sx;
        sprite[active_sprites].y = sy;
        sprite[active_sprites].flip_x = (attr & 0x04) ? 1 : 0;
        sprite[active_sprites].flip_y = (attr & 0x08) ? 1 : 0;
        active_sprites++;
        if (active_sprites >= 64) break;
    }
}

// ============================================================
// Rendering — 240 pixels wide landscape (game X 8-247)
// ============================================================

#define GNG_PAL_TILE_BASE   0x00
#define GNG_PAL_SPRITE_BASE 0x40
#define GNG_PAL_CHAR_BASE   0x80

static inline uint16_t lookup_sprite_color(uint8_t pixel, uint8_t attr, const uint16_t *pal) {
    if (pixel == 15) return 0;  // transparent
    return pal[GNG_PAL_SPRITE_BASE + ((attr & 0x03) * 16) + pixel];
}

void gng::render_row(short row) {
    if (row < 2 || row > 29) return;
    render_bg_layer(row, false);
    render_sprites(row);
    render_fg_layer(row);
}

// ── Background tile layer (32x32 tiles 16x16, scrolling) ──
void gng::render_bg_layer(short row, bool front_pass) {
    int game_y_base = row * 8;

    for (int dy = 0; dy < 8; dy++) {
        int game_y = game_y_base + dy;
        unsigned short *dst = frame_buffer + dy * 240;

        int bg_y = (game_y + scroll_y) & 0x1FF;
        int tile_row = bg_y >> 4;
        int py = bg_y & 0x0F;

        int prev_tile_col = -1;
        int code = 0, color = 0, group = 0, flipx = 0, flipy = 0;
        const unsigned char *tile_data = nullptr;

        int sx_scroll_base = (8 + (int)scroll_x) & 0x1FF;

        for (int sx = 0; sx < 240; sx++) {
            int bg_x = (sx_scroll_base + sx) & 0x1FF;
            int tile_col = bg_x >> 4;
            int px = bg_x & 0x0F;

            if (tile_col != prev_tile_col) {
                prev_tile_col = tile_col;
                int tile_idx = (tile_col & 0x1F) * 32 + (tile_row & 0x1F);
                uint8_t attr = bg_vram[tile_idx + 0x400];
                code = bg_vram[tile_idx] | ((attr & 0xC0) << 2);
                color = attr & 0x07;
                group = (attr >> 3) & 1;
                flipx = (attr >> 4) & 1;
                flipy = (attr >> 5) & 1;
                int ty = flipy ? (15 - py) : py;
                tile_data = &tilemap_data[code * 256 + ty * 16];
            }

            int tx = flipx ? (15 - px) : px;
            uint8_t pixel = tile_data[tx];
            if (front_pass) {
                if (group == 0) continue;
                if (pixel == 0) continue;
            }
            dst[sx] = pal_rgb565[GNG_PAL_TILE_BASE + (color * 8) + pixel];
        }
    }
}

// ── Sprite layer ──
void gng::render_sprites(short row) {
    int game_y_base = row * 8;

    for (int s = 0; s < active_sprites; s++) {
        int sy = sprite[s].y;
        int sx = sprite[s].x;
        int code = sprite[s].code | (sprite[s].color_block << 8);
        int color = sprite[s].color;
        int fxf = sprite[s].flip_x;
        int fyf = sprite[s].flip_y;

        for (int dy = 0; dy < 8; dy++) {
            int game_y = game_y_base + dy;
            int spy = game_y - sy;
            if (spy < 0 || spy >= 16) continue;

            unsigned short *dst = frame_buffer + dy * 240;
            int ty = fyf ? (15 - spy) : spy;

            for (int spx = 0; spx < 16; spx++) {
                int gx = sx + spx;
                int screen_x = gx - 8;
                if (screen_x < 0 || screen_x >= 240) continue;

                int tx = fxf ? (15 - spx) : spx;
                uint8_t pixel = spritemap_data[code * 256 + ty * 16 + tx];
                if (pixel == 15) continue;

                uint16_t rgb = lookup_sprite_color(pixel, color, pal_rgb565);
                if (rgb != 0) dst[screen_x] = rgb;
            }
        }
    }
}

// ── Foreground char layer (32x32 chars 8x8, transparent pen 3) ──
void gng::render_fg_layer(short row) {
    int game_y_base = row * 8;
    int char_row = game_y_base >> 3;
    if (char_row >= 32) return;

    for (int char_col = 1; char_col <= 30; char_col++) {
        int tile_idx = char_row * 32 + char_col;
        uint8_t code_lo = fg_vram[tile_idx];
        uint8_t attr = fg_vram[tile_idx + 0x400];
        int code = code_lo | ((attr & 0xC0) << 2);

        if (code == 0 && (attr & 0x0F) == 0) continue;

        int color = attr & 0x0F;
        int flipx = (attr >> 4) & 1;
        int flipy = (attr >> 5) & 1;

        int sx_base = char_col * 8 - 8;

        for (int dy = 0; dy < 8; dy++) {
            int ty = flipy ? (7 - dy) : dy;
            unsigned short *dst = frame_buffer + dy * 240 + sx_base;
            const unsigned char *char_row_data = &charmap_data[code * 64 + ty * 8];

            if (flipx) {
                for (int px = 0; px < 8; px++) {
                    uint8_t pixel = char_row_data[7 - px];
                    if (pixel == 3) continue;
                    dst[px] = pal_rgb565[GNG_PAL_CHAR_BASE + (color * 4) + pixel];
                }
            } else {
                for (int px = 0; px < 8; px++) {
                    uint8_t pixel = char_row_data[px];
                    if (pixel == 3) continue;
                    dst[px] = pal_rgb565[GNG_PAL_CHAR_BASE + (color * 4) + pixel];
                }
            }
        }
    }
}

#endif  // ENABLE_GNG
