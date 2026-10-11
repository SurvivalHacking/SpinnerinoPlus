#ifndef POPEYE_H
#define POPEYE_H

#ifndef ENABLE_POPEYE
#error "ENABLE_POPEYE mancante nel config.h!"
#endif

#include <pgmspace.h>
#include <stdint.h>
#include "popeye_logo.h"
#include "popeye_rom_main.h"
#include "popeye_rom_video.h"
#include "popeye_prom_3a.h"
#include "popeye_prom_4a.h"
#include "popeye_prom_5a.h"
#include "popeye_prom_5b.h"
#include "popeye_prom_7j.h"
// V9A: attract screen overlay images (replace broken sprite rendering of
// the title logo, face inside star, and copyright lines).
#include "popeye_face_overlay.h"
#include "popeye_logo_overlay.h"
#include "popeye_nintendo_overlay.h"
#include "popeye_king_features_overlay.h"
#include "../machineBase.h"

class popeye : public machineBase {
public:
    popeye() { }
    ~popeye() { }

    signed char machineType() override { return MCH_POPEYE; }
    bool isLandscape() override { return true; }
    // Landscape: pannello in portrait + render 240-wide diretto (come
    // greenberet/moonpatrol). render_row gia' scrive bande 240x8.
    bool usesPortraitPanel() override { return true; }

    void reset() override;
    unsigned char rdZ80(unsigned short Addr) override;
    void wrZ80(unsigned short Addr, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;
    unsigned char inZ80(unsigned short Port) override;
    void outZ80(unsigned short Port, unsigned char Value) override;
    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;
    const unsigned short *logo(void) override { return popeye_logo; }

private:
    static const int SRC_W = 240;
    static const int SRC_H = 288;
    static const int ORIG_W = 512;
    static const int ORIG_VISIBLE_Y = 32;
    static const int ORIG_VISIBLE_H = 448;

    enum {
      MEM_RAMLOW   = 0x0000, // 0x8800-0x8BFF, 1 KiB
      MEM_DMASRC   = 0x0400, // 0x8C00-0x8E7F, 640 B
      MEM_RAMHIGH  = 0x0680, // 0x8E80-0x8FFF, 384 B
      MEM_VIDEORAM = 0x0800, // 0xA000-0xA3FF
      MEM_COLORRAM = 0x0C00, // 0xA400-0xA7FF
      MEM_BGR      = 0x1000  // 0xC000-0xDFFF logical 4 KiB nibble RAM
    };

    unsigned char ay_addr;
    unsigned char ay_regs[16];
    unsigned char dswbit;
    unsigned char flip_screen;
    unsigned char field;
    unsigned char prot0, prot1, prot_shift;
    char coinFrameCounter;
    char coinBackup;

    // CONSOLE_QUADRA_NANO — DOUBLE BUFFER + LOCAL COPY per snapshot DMA.
    // Bug scoperto: con 2 slot e VIDEO_HALF_RATE l'emul fa 2 run_frame per
    // frame display -> dopo 2 swap, write_idx torna al valore iniziale e
    // sovrascrive il buffer che il main sta usando per il rendering -> race
    // condition mid-frame -> puntini colorati su sprite in movimento.
    // FIX: il main task copia sprite_ram/bg/palette in buffer LOCALI durante
    // prepare_frame. L'emul puo' swappare ovunque senza distrubare.
    // DOUBLE BUFFER ATOMICO ESTESO a tutto memory[]: sprite_ram, bg bitmap,
    // videoram, colorram. L'emul task fa lo snapshot atomicamente DOPO i Z80
    // step (= a frame completato dalla CPU), main task copia in render_*[]
    // dal buffer "stable". Evita race su memory[] che la CPU emul scrive in
    // continuazione (= "puntini" che apparivano dietro sprite in movimento).
    unsigned char sprite_ram_buf[2][0x280];
    unsigned char bg_ram_buf[2][0x1000];        // 4KB doppio
    unsigned char vram_buf[2][0x400];           // 1KB doppio
    unsigned char cram_buf[2][0x400];           // 1KB doppio
    unsigned char bg_scroll_buf[2][3];
    unsigned char palette_bank_buf[2];
    volatile unsigned char dma_write_idx;       // emul task writes here

    // Local copy owned by main task:
    unsigned char render_sprite_ram[0x280];
    unsigned char render_bg_scroll[3];
    unsigned char render_palette_bank;
    unsigned char render_bgr[0x1000];
    unsigned char render_vram[0x400];
    unsigned char render_cram[0x400];

    // V7A: NMI/watchdog state. MAME enables NMI via the Z80 refresh callback
    // (bit 8 of the refresh address). We don't expose refresh on our Z80 core,
    // so we still raise one NMI per frame, BUT we track watchdog so we can
    // simulate the tpp2 watchdog-reset cycle when the game stops kicking.
    unsigned char nmi_enabled;
    unsigned char watchdog_enabled;
    unsigned char watchdog_counter;

    unsigned char read_input_p1();
    unsigned char read_input_p2();
    unsigned char read_input_system();
    unsigned char read_dsw0();
    unsigned char read_dsw1();
    unsigned short rgb_from_prom(unsigned char prom, bool invert, bool background);
    unsigned short bg_color(unsigned char pen);
    unsigned short char_color(unsigned char color, unsigned char pen);
    unsigned short sprite_color(unsigned char color, unsigned char pen);
    unsigned char char_pen(unsigned char code, unsigned char x, unsigned char y);
    unsigned char sprite_pen(unsigned short code, unsigned char x, unsigned char y);
    void render_original_line(unsigned short *line, int y);
    void draw_sprites_line(unsigned short *line, int y);
    void draw_chars_line(unsigned short *line, int y);

    // V9A — Attract screen overlay.
    // The CPU emulation in galagino can't quite reproduce the rev D Popeye
    // attract sequence (specifically, the POPEYE logo / face / © texts come
    // out unreadable because of small differences in NMI gating and palette
    // banking that we can't easily fix without a Z80 refresh callback).
    // Instead, we overlay four pre-rendered images on top of the rendered
    // frame buffer when we detect that the CPU is showing the star+face
    // attract screen. The detection is conservative: we look for specific
    // VRAM tile codes that only appear in that screen (the star outline tiles
    // around the face). Anything else (gameplay demo, story screen, in-game)
    // is left untouched.
    bool is_attract_with_star();
    void overlay_attract_row(short row);
    // RLE-decode helper: decode one row of the overlay image starting from
    // pixel offset `start_pixel` and write up to `pixel_count` pixels to
    // `dst`. Returns nothing; the function is internal use only.
    void rle_decode_to(const unsigned short *src, int target_row,
                       int img_w, unsigned short *dst, int dst_x,
                       int dst_max_x);
};

#endif
