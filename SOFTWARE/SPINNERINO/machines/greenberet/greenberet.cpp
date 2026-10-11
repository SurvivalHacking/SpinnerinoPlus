#include "config.h"
#include "machines/greenberet/greenberet.h"

#ifdef ENABLE_GREENBERET

#include "emulation/input.h"

// ============================================================
// Green Beret (Konami 1985) - EXACT match to MAME gberet.cpp
// Render LANDSCAPE come FULL33: pannello commutato in PORTRAIT da
// start_machine (video.setLandscapeGame), render_row scrive righe native
// 240-wide DIRETTAMENTE in frame_buffer (rows 4..31 = 224 px gioco), il loop
// portrait in SPINNERINO.ino le manda nella finestra 240x288.
// ============================================================
//
// prg_map:
//   0x0000-0xBFFF: ROM (48KB)
//   0xC000-0xC7FF: colorram (tile attributes)
//   0xC800-0xCFFF: videoram (tile codes)
//   0xD000-0xD0FF: spriteram2
//   0xD100-0xD1FF: spriteram
//   0xD200-0xDFFF: RAM (3.5KB work RAM!)
//   0xE000-0xE03F: k005849 scroll_r / scroll_w
//   0xE040-0xE047: k005849 ctrl_w
//   0xF000: coin_counter_w
//   0xF200: DSW2 (read) / soundlatch (write)
//   0xF400: DSW3 (read) / sound_w (write)
//   0xF600: DSW1 (read) / watchdog (write)
//   0xF601: P2 / 0xF602: P1 / 0xF603: SYSTEM

void greenberet::reset() {
  machineBase::reset();
  memset(k005849_scroll, 0, sizeof(k005849_scroll));
  memset(k005849_ctrl, 0, sizeof(k005849_ctrl));
  sn_latch = 0;
  sn_latch_reg = 0;
  speed_acc = 0;
  // Anti-flicker: nessuno slot ha cronologia valida all'avvio.
  for (int i = 0; i < GB_SPRITE_SLOTS; i++) gb_slot_age[i] = GB_SPRITE_PERSIST;
}

unsigned char greenberet::opZ80(unsigned short Addr) {
  if (Addr < 0xC000)
    return pgm_read_byte(&greenberet_rom[Addr]);
  return rdZ80(Addr);
}

unsigned char greenberet::rdZ80(unsigned short Addr) {
  if (Addr < 0xC000)
    return pgm_read_byte(&greenberet_rom[Addr]);

  // Color RAM
  if (Addr >= 0xC000 && Addr <= 0xC7FF)
    return memory[GB_CRAM_OFF + (Addr - 0xC000)];

  // Video RAM
  if (Addr >= 0xC800 && Addr <= 0xCFFF)
    return memory[GB_VRAM_OFF + (Addr - 0xC800)];

  // Sprite RAM 2
  if (Addr >= 0xD000 && Addr <= 0xD0FF)
    return memory[GB_SPRAM2_OFF + (Addr - 0xD000)];

  // Sprite RAM
  if (Addr >= 0xD100 && Addr <= 0xD1FF)
    return memory[GB_SPRAM_OFF + (Addr - 0xD100)];

  // Work RAM 0xD200-0xDFFF (3.5KB!)
  if (Addr >= 0xD200 && Addr <= 0xDFFF)
    return memory[GB_WRAM_OFF + (Addr - 0xD200)];

  // k005849 scroll (readable)
  if (Addr >= 0xE000 && Addr <= 0xE03F)
    return k005849_scroll[Addr & 0x3F];

  // k005849 ctrl (readable)
  if (Addr >= 0xE040 && Addr <= 0xE047)
    return k005849_ctrl[Addr & 0x07];

  // Input ports
  if (Addr == 0xF200) return GREENBERET_DSW2;
  if (Addr == 0xF400) return GREENBERET_DSW3;
  if (Addr == 0xF600) return GREENBERET_DSW1;

  if (Addr == 0xF601) return 0xFF;  // P2

  if (Addr == 0xF602) {
    // P1: bit0=LEFT, bit1=RIGHT, bit2=UP, bit3=DOWN, bit4=FIRE1, bit5=FIRE2
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_LEFT)  val &= ~0x01;
    if (keymask & BUTTON_RIGHT) val &= ~0x02;
    if (keymask & BUTTON_UP)    val &= ~0x04;
    if (keymask & BUTTON_DOWN)  val &= ~0x08;
    if (keymask & BUTTON_FIRE)  val &= ~0x10;
    if (keymask & BUTTON_EXTRA) val &= ~0x20;  // FIRE2 (jump)
    return val;
  }

  if (Addr == 0xF603) {
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_COIN)  val &= ~0x01;
    if (keymask & BUTTON_START) val &= ~0x08;
    return val;
  }

  return 0xFF;
}

void greenberet::wrZ80(unsigned short Addr, unsigned char Value) {
  // Color RAM
  if (Addr >= 0xC000 && Addr <= 0xC7FF) {
    memory[GB_CRAM_OFF + (Addr - 0xC000)] = Value;
    return;
  }

  // Video RAM
  if (Addr >= 0xC800 && Addr <= 0xCFFF) {
    memory[GB_VRAM_OFF + (Addr - 0xC800)] = Value;
    return;
  }

  // Sprite RAM 2
  if (Addr >= 0xD000 && Addr <= 0xD0FF) {
    memory[GB_SPRAM2_OFF + (Addr - 0xD000)] = Value;
    return;
  }

  // Sprite RAM
  if (Addr >= 0xD100 && Addr <= 0xD1FF) {
    memory[GB_SPRAM_OFF + (Addr - 0xD100)] = Value;
    return;
  }

  // Work RAM 0xD200-0xDFFF (3.5KB!)
  if (Addr >= 0xD200 && Addr <= 0xDFFF) {
    memory[GB_WRAM_OFF + (Addr - 0xD200)] = Value;
    return;
  }

  // k005849 scroll
  if (Addr >= 0xE000 && Addr <= 0xE03F) {
    k005849_scroll[Addr & 0x3F] = Value;
    return;
  }

  // k005849 ctrl
  if (Addr >= 0xE040 && Addr <= 0xE047) {
    k005849_ctrl[Addr & 0x07] = Value;
    // game_started ONLY after POST enables interrupts (not during VRAM test writes!)
    if (!game_started && (Addr == 0xE044) && (Value & 0x03))
      game_started = 1;
    return;
  }

  // Coin counter
  if (Addr == 0xF000) return;

  // Sound latch
  if (Addr == 0xF200) {
    sn_latch = Value;
    return;
  }

  // Sound trigger: send latched byte to SN76489
  if (Addr == 0xF400) {
    sn76489_write(sn_latch);
    return;
  }

  // Watchdog
  if (Addr == 0xF600) return;
}

unsigned char greenberet::inZ80(unsigned short Port) { return 0xFF; }
void greenberet::outZ80(unsigned short Port, unsigned char Value) { }

// ============================================================
// SN76489AN
// ============================================================

void greenberet::sn76489_write(unsigned char data) {
  if (data & 0x80) {
    sn_latch_reg = (data >> 4) & 0x07;
    unsigned char val4 = data & 0x0F;
    int ch = sn_latch_reg >> 1;
    if (sn_latch_reg & 1) {
      sn_volume[0][ch] = val4;
      sn_min_volume[0][ch] = val4;
      if (val4 < 15) sn_hold[0][ch] = 6;
    } else {
      sn_period[0][ch] = (sn_period[0][ch] & 0x3F0) | val4;
    }
  } else {
    int ch = sn_latch_reg >> 1;
    if (sn_latch_reg & 1) {
      unsigned char val4 = data & 0x0F;
      sn_volume[0][ch] = val4;
      sn_min_volume[0][ch] = val4;
      if (val4 < 15) sn_hold[0][ch] = 6;
    } else {
      sn_period[0][ch] = (sn_period[0][ch] & 0x00F) | ((data & 0x3F) << 4);
    }
  }
}

// ============================================================
// Frame execution - k005849 interrupt model (FULL33 verbatim)
// ============================================================
// NMI: timer fires 8x/frame (when ctrl[4] bit0 = 1)
// IRQ/FIRQ: VBLANK (ctrl[4] bit1 / bit2)

void greenberet::run_frame(void) {

  current_cpu = 0;

  int nmi_interval = INST_PER_FRAME / 8;
  static int gb_frame_counter = 0;

  // Manopola velocita': NUM/DEN frame di gioco per frame video (vedi GB_SPEED_*).
  speed_acc += GB_SPEED_NUM;
  int nframes = 0;
  while (speed_acc >= GB_SPEED_DEN) { speed_acc -= GB_SPEED_DEN; nframes++; }

  for (int f = 0; f < nframes; f++) {
    for (int i = 0; i < INST_PER_FRAME; i++) {
      StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);

      // NMI 8x per frame (k005849 timer) - only if enabled AND not in NMI handler
      if ((i % nmi_interval) == (nmi_interval / 2)) {
        if ((k005849_ctrl[4] & 0x01) && (cpu[0].IFF & 0x01))
          IntZ80(&cpu[0], INT_NMI);
      }
    }

    // IRQ at VBLANK (ctrl[4] bit 1)
    if (k005849_ctrl[4] & 0x02)
      IntZ80(&cpu[0], INT_RST38);

    // FIRQ at VBLANK every OTHER frame (ctrl[4] bit 2)
    // Green Beret uses FIRQ (bit2=0x04), NOT IRQ (bit1)! Both -> INT_RST38.
    if ((k005849_ctrl[4] & 0x04) && (gb_frame_counter & 1))
      IntZ80(&cpu[0], INT_RST38);
    gb_frame_counter++;
  }
}

// ============================================================
// Sprite preparation - EXACT MAME draw_sprites logic (FULL33 verbatim)
// ============================================================

void greenberet::prepare_frame(void) {
  // Driver long-press reset (regola SPINNERINO: chiamare buttons_get ogni frame).
  if (input) input->buttons_get();

  active_sprites = 0;

  // flip_screen: k005849 ctrl[4] bit 3 (MAME: cocktail/flip mode)
  int flip = (k005849_ctrl[4] & 0x08) ? 1 : 0;

  // Sprite bank select from k005849 ctrl[3] bit3
  unsigned char *sr;
  if (k005849_ctrl[3] & 0x08)
    sr = memory + GB_SPRAM2_OFF;
  else
    sr = memory + GB_SPRAM_OFF;

  // MAME: for (offs = 0; offs < 0xc0; offs += 4) — un slot HW ogni 4 byte.
  // ANTI-FLICKER per-slot (vedi GB_SPRITE_PERSIST): se uno slot e' valido lo
  // disegniamo (posizione corrente) e azzeriamo l'eta'; se non lo e' ma lo era
  // entro GB_SPRITE_PERSIST frame, ridisegniamo l'ultimo sprite valido di quello
  // slot -> i nemici che lampeggiano a frame alterni restano completi, senza
  // ghosting (gli sprite attivi usano sempre la posizione attuale).
  int slot = 0;
  for (int offs = 0; offs < 0xC0; offs += 4, slot++) {
    bool valid_now = false;
    struct sprite_S spr;

    if (sr[offs + 3]) {  // visible
      int attr = sr[offs + 1];
      int code = sr[offs + 0] + ((attr & 0x40) << 2);
      int color = attr & 0x0F;
      int sx = sr[offs + 2] - 2 * (attr & 0x80);
      int sy = sr[offs + 3];

      unsigned char flipx = (attr >> 4) & 1;
      unsigned char flipy = (attr >> 5) & 1;

      // MAME flip_screen handling
      if (flip) {
        sx = 240 - sx;
        sy = 240 - sy;
        flipx = !flipx;
        flipy = !flipy;
      }

      if (code < GB_NSPRITES) {
        // LANDSCAPE mode: direct game coordinates (like MAME, no rotation)
        // MAME visible area: x=[8,247], y=[16,239]. Offset sprites accordingly.
        spr.x = sx - 8;
        spr.y = sy + 16;
        spr.code = code;
        spr.color = color;
        spr.flags = (flipx ? 1 : 0) | (flipy ? 2 : 0);

        if ((spr.x > -16) && (spr.x < 256) && (spr.y > -16) && (spr.y < 288))
          valid_now = true;
      }
    }

    if (valid_now) {
      gb_slot_spr[slot] = spr;
      gb_slot_age[slot] = 0;
      if (active_sprites < 64) sprite[active_sprites++] = spr;
    } else if (gb_slot_age[slot] < GB_SPRITE_PERSIST) {
      // Slot spento questo frame ma valido di recente -> ridisegna l'ultimo.
      gb_slot_age[slot]++;
      if (active_sprites < 64) sprite[active_sprites++] = gb_slot_spr[slot];
    }
  }
}

// ============================================================
// Tile rendering - matches MAME screen_update_gberet (FULL33 verbatim)
// ============================================================
// Game: 240x224 landscape. Frame buffer: 240 wide x 8 tall per band.
// Bands 4-31 = 28 bands = 224 game rows, centered in 36-band (288) structure.

void greenberet::render_row(short row) {
  if (row < 4 || row >= 32) return;

  int game_row_base = (row - 4);  // 0-27: which 8-pixel vertical strip

  // === PASS 1: All tiles opaque ===
  // MAME visible area: x=[8,247], y=[16,239]
  for (int line = 0; line < 8; line++) {
    int game_y = game_row_base * 8 + line + 16;  // +16: MAME visible starts at y=16
    int tile_row = game_y >> 3;
    int tile_r = game_y & 7;

    // Per-row scroll
    int scroll = k005849_scroll[tile_row] | ((k005849_scroll[tile_row | 0x20] & 1) << 8);

    unsigned short *ptr = frame_buffer + line * 240;

    for (int px = 0; px < 240; px++) {
      int game_x = (px + 8 + scroll) & 511;  // +8: MAME visible starts at x=8
      int tile_col = (game_x >> 3) & 63;
      int tile_c = game_x & 7;

      int tile_idx = tile_row * 64 + tile_col;
      unsigned char vram_val = memory[GB_VRAM_OFF + tile_idx];
      unsigned char cram_val = memory[GB_CRAM_OFF + tile_idx];
      unsigned short tile_code = vram_val | ((cram_val & 0x40) << 2);
      if (tile_code >= GB_NTILES) tile_code = 0;
      unsigned char flipx = (cram_val >> 4) & 1;
      unsigned char flipy = (cram_val >> 5) & 1;

      int src_r = flipy ? (7 - tile_r) : tile_r;
      int src_c = flipx ? (7 - tile_c) : tile_c;

      const unsigned long *tile_gfx = greenberet_tilemap[tile_code];
      unsigned long pix_row = pgm_read_dword(&tile_gfx[src_r]);
      unsigned char px_val = (pix_row >> (src_c * 4)) & 0x0F;

      const unsigned short *colors = greenberet_tile_colormap[cram_val & 0x0F];
      ptr[px] = pgm_read_word(&colors[px_val]);
    }
  }

  // === PASS 2: Sprites (use pre-flipped variants from ROM converter) ===
  for (int s = 0; s < active_sprites; s++) {
    short spr_x = sprite[s].x;
    short spr_y = sprite[s].y;

    if ((spr_y >= 8 * (row + 1)) || ((spr_y + 16) <= 8 * row))
      continue;

    int code = sprite[s].code;
    if (code >= GB_NSPRITES) continue;

    int spr_flipx = sprite[s].flags & 1;
    int spr_flipy = (sprite[s].flags >> 1) & 1;

    const unsigned short *colors = greenberet_sprite_colormap[sprite[s].color & 0x0F];
    // Select pre-flipped variant: 0=normal, 1=flipX, 2=flipY, 3=flipXY
    int variant = spr_flipx | (spr_flipy << 1);
    const unsigned long *spr_gfx = greenberet_sprites[variant][code];

    int band_top = 8 * row;

    for (int sub = 0; sub < 8; sub++) {
      int screen_y = band_top + sub;
      if (screen_y < spr_y || screen_y >= spr_y + 16) continue;

      int dy = screen_y - spr_y;  // 0-15

      for (int dx = 0; dx < 16; dx++) {
        int screen_x = spr_x + dx;
        if (screen_x < 0 || screen_x >= 240) continue;

        unsigned char px;
        if (dx < 8)
          px = (pgm_read_dword(&spr_gfx[dy * 2]) >> (dx * 4)) & 0x0F;
        else
          px = (pgm_read_dword(&spr_gfx[dy * 2 + 1]) >> ((dx - 8) * 4)) & 0x0F;

        if (px) {
          frame_buffer[sub * 240 + screen_x] = pgm_read_word(&colors[px]);
        }
      }
    }
  }

  // === PASS 3: Foreground tiles (category=0) over sprites ===
  for (int line = 0; line < 8; line++) {
    int game_y = game_row_base * 8 + line + 16;  // +16: MAME visible starts at y=16
    int tile_row = game_y >> 3;
    int tile_r = game_y & 7;

    int scroll = k005849_scroll[tile_row] | ((k005849_scroll[tile_row | 0x20] & 1) << 8);

    unsigned short *ptr = frame_buffer + line * 240;

    for (int px = 0; px < 240; px++) {
      int game_x = (px + 8 + scroll) & 511;  // +8: MAME visible starts at x=8
      int tile_col = (game_x >> 3) & 63;
      int tile_c = game_x & 7;

      int tile_idx = tile_row * 64 + tile_col;
      unsigned char cram_val = memory[GB_CRAM_OFF + tile_idx];

      // category bit7: 1 = behind sprites (already drawn), 0 = in front of sprites
      if (cram_val & 0x80) continue;  // behind sprites, skip

      unsigned char vram_val = memory[GB_VRAM_OFF + tile_idx];
      unsigned short tile_code = vram_val | ((cram_val & 0x40) << 2);
      if (tile_code >= GB_NTILES) tile_code = 0;
      unsigned char flipx = (cram_val >> 4) & 1;
      unsigned char flipy = (cram_val >> 5) & 1;

      int src_r = flipy ? (7 - tile_r) : tile_r;
      int src_c = flipx ? (7 - tile_c) : tile_c;

      const unsigned long *tile_gfx = greenberet_tilemap[tile_code];
      unsigned long pix_row = pgm_read_dword(&tile_gfx[src_r]);
      unsigned char px_val = (pix_row >> (src_c * 4)) & 0x0F;

      if (px_val) {
        const unsigned short *colors = greenberet_tile_colormap[cram_val & 0x0F];
        unsigned short col565 = pgm_read_word(&colors[px_val]);
        if (col565) ptr[px] = col565;
      }
    }
  }
}

const unsigned short *greenberet::logo(void) {
  return greenberet_logo;
}

#ifdef LED_PIN
void greenberet::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for (char c = 0; c < NUM_LEDS; c++) {
      if (c == il) leds[c] = LED_GREEN;
      else         leds[c] = LED_WHITE;
    }
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}
void greenberet::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif

#endif // ENABLE_GREENBERET
