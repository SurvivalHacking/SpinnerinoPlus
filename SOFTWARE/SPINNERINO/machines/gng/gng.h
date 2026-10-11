#ifndef GNG_H
#define GNG_H

// FASE 3: gameplay completo (porting 2.8VPE).
// MC6809 main + Z80 sound + 2x YM2203 + render BG/sprites/FG.
// ROM in PSRAM (lazy-load al primo reset, fallback flash).
// Audio: SSG via soundregs[] → ay_render_buffer (FASE 4); FM TODO.
//
// Hardware Ghosts'n Goblins (Capcom 1985):
//   Main CPU: MC6809E @ 1.5 MHz  (NON Z80 — usa cpus/m6809/)
//   Sound CPU: Z80 @ 3.579 MHz + 2x YM2203 (SSG = AY-3-8910 + FM)
//   Video: 256x224 horizontal (landscape), 3 layer FG/BG/sprites
//   Bank-switched ROM (5 bank x 8KB)
//   Palette 256 entries, CPU-written xRGB_444 split bank (0x3800/0x3900)

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "gng_logo.h"
#include "gng_dipswitches.h"

// ROM data: extern declarations. Le definitions sono in gng_*.h, incluse
// UNA SOLA volta da gng.cpp ROOT (linkage external = una sola copia in flash).
extern const unsigned char gng_rom[40960];
extern const unsigned char gng_bank_rom[40960];
extern const unsigned char gng_snd_rom[32768];
extern const unsigned char gng_charmap[65536];     // 1024 chars * 64 px (1 B/pixel)
extern const unsigned char gng_tilemap[262144];    // 1024 tiles * 256 px
extern const unsigned char gng_spritemap[262144];  // 1024 sprites * 256 px

#define GNG_STEPS_PER_FRAME  1250

class gng : public machineBase
{
public:
    gng() { fg_vram = nullptr; bg_vram = nullptr; }
    ~gng() {
        if (fg_vram) free(fg_vram);
        if (bg_vram) free(bg_vram);
    }

    void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) override;
    void reset() override;

    signed char machineType() override { return MCH_GNG; }
    bool isLandscape() override { return true; }
    bool usesPortraitPanel() override { return true; }   // SPINNERINO: pannello portrait, path render 240-wide (come Green Beret)
    signed char useVideoHalfRate() override { return 1; }
    const unsigned short *logo(void) override { return gng_logo; }

    unsigned char rdZ80(unsigned short Addr) override;
    void wrZ80(unsigned short Addr, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;
    unsigned char inZ80(unsigned short Port) override;
    void outZ80(unsigned short Port, unsigned char Value) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;

    // M6809 memory access (called from C callbacks)
    uint8_t main_read(uint16_t addr);
    void main_write(uint16_t addr, uint8_t val);

    // YM2203 register file (chip 0/1, reg 0..255). SSG side (reg 0..13)
    // viene mappato su soundregs[] per il path AY del renderer audio.
    uint8_t ym_regs[2][256];

    // FM key-on state per chip/channel (reg 0x28, bit 4-7 = key on/off mask).
    // Used by audio.cpp's square-wave FM fallback (no Nuked-OPN2 on S3).
    uint8_t fm_keyed[2][3];

private:
    // M6809 main CPU
    m6809_state main_cpu;

    // Video RAM (separate allocations — 8KB shared memory[] non bastano)
    uint8_t *fg_vram;   // 2KB FG char RAM
    uint8_t *bg_vram;   // 2KB BG tile RAM

    // Palette RAM (CPU side — write-only on real HW). MAME GnG splits it in
    // two banks indexed by idx (NOT interleaved):
    //   pal_hi[idx] @ 0x3800-0x38FF = RRRR_GGGG  (high nibble R, low nibble G)
    //   pal_lo[idx] @ 0x3900-0x39FF = BBBB_xxxx  (high nibble B)
    // 256 entries: tiles 0x00..0x3F, sprites 0x40..0x7F, chars 0x80..0xBF.
    uint8_t pal_hi[256];
    uint8_t pal_lo[256];
    uint16_t pal_rgb565[256];  // pre-computed RGB565 byte-swapped

    // Scroll registers
    uint16_t scroll_x;
    uint16_t scroll_y;

    // Control registers
    uint8_t bank_select;
    uint8_t soundlatch;
    uint8_t flipscreen;
    uint8_t mainlatch[8];        // LS259 at 0x3D00-0x3D07, bit 0 of each
    uint8_t vblank_irq_enable;   // q5 of mainlatch inverted = IRQ enable

    // Sound CPU state
    uint8_t snd_ram[2048];
    uint8_t ym_addr[2];
    uint8_t snd_irq_pending;

    void update_palette(uint8_t idx);
    void render_bg_layer(short row, bool front_pass);
    void render_sprites(short row);
    void render_fg_layer(short row);

    // Puntatori a PSRAM (popolati al primo reset(), fallback flash).
    const unsigned char *rom_main      = nullptr;
    const unsigned char *rom_bank      = nullptr;
    const unsigned char *rom_snd       = nullptr;
    const unsigned char *charmap_data  = nullptr;
    const unsigned char *tilemap_data  = nullptr;
    const unsigned char *spritemap_data = nullptr;
};

// Global pointer for M6809 callbacks
extern gng *g_gng_instance;

#endif
