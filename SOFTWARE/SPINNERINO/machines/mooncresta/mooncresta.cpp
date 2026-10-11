#include "machines/mooncresta/mooncresta.h"
#include "emulation/input.h"

// Moon Cresta memory map (MAME mooncrst_map_base + mooncrst_map):
//   0x0000-0x3FFF: ROM (16KB, decrypted)
//   0x8000-0x83FF: Work RAM (1KB, mirror 0x8400-0x87FF)
//   0x9000-0x93FF: Video RAM (1KB, mirror 0x9400-0x97FF)
//   0x9800-0x98FF: Sprite/ObjRAM (256B, mirror 0x9900-0x9FFF)
//   0xA000:        IN0 read / GFX bank write (0xA000-0xA002), coin, LFO
//   0xA800:        IN1 read / Sound latches write (0xA800-0xA807)
//   0xB000:        IN2 read / NMI enable (0xB000) / Stars enable (0xB004)
//   0xB800:        Watchdog read / Pitch write
// Logica 1:1 dal porting PAOLO (GALAGONE 3.5). Render avvolto nel trasposto
// SPINNERINO: i blit scrivono in `rt` (strip nativa di scr_full), render_row
// traspone 90 in frame_buffer.

unsigned char mooncresta::opZ80(unsigned short Addr) {
  if(Addr < 0x4000)
    return mooncresta_rom[Addr];
  return 0xff;
}

unsigned char mooncresta::rdZ80(unsigned short Addr) {
  if(Addr < 0x4000)
    return mooncresta_rom[Addr];

  if((Addr & 0xf800) == 0x8000)               // Work RAM 0x8000-0x83FF (+mirror)
    return memory[Addr & 0x03FF];

  if((Addr & 0xf800) == 0x9000)               // Video RAM 0x9000-0x93FF (+mirror)
    return memory[0x0800 + (Addr & 0x03FF)];

  if((Addr & 0xf800) == 0x9800)               // Sprite/Color RAM 0x9800-0x98FF
    return memory[0x0C00 + (Addr & 0x00FF)];

  if((Addr & 0xf800) == 0xA000) {             // IN0
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0x00;
    if(keymask & BUTTON_COIN)   retval |= 0x01;
    if(keymask & BUTTON_LEFT)   retval |= 0x04;
    if(keymask & BUTTON_RIGHT)  retval |= 0x08;
    if(keymask & BUTTON_FIRE)   retval |= 0x10;
    return retval;
  }

  if((Addr & 0xf800) == 0xA800) {             // IN1
    unsigned char keymask = input->buttons_get();
    unsigned char retval = MOONCRESTA_DIP_IN1;
    if(keymask & BUTTON_START)  retval |= 0x01;
    return retval;
  }

  if((Addr & 0xf800) == 0xB000)               // IN2
    return MOONCRESTA_DIP_IN2;

  if((Addr & 0xf800) == 0xB800)               // Watchdog
    return 0xFF;

  return 0x00;
}

void mooncresta::wrZ80(unsigned short Addr, unsigned char Value) {
  if((Addr & 0xf800) == 0x8000) {             // Work RAM
    memory[Addr & 0x03FF] = Value;
    return;
  }

  if((Addr & 0xf800) == 0x9000) {             // Video RAM
    unsigned short offset = Addr & 0x03FF;
    if(!game_started && offset == 800 && Value != 0x10)
      game_started = 1;
    memory[0x0800 + offset] = Value;
    return;
  }

  if((Addr & 0xf800) == 0x9800) {             // Sprite/Color RAM
    memory[0x0C00 + (Addr & 0x00FF)] = Value;
    return;
  }

  // 0xA000-0xA7FF: GFX bank (0-2), coin (3), LFO (4-7) — MAME mirror 0x07f8
  if((Addr & 0xf800) == 0xA000) {
    unsigned char offset = Addr & 0x07;
    if(offset == 0) { gfxbank = Value & 1; return; }   // solo m_gfxbank[0] (mooncrst)
    if(offset <= 3) return;                             // gfxbank[1-2]/coin: ignorati
    soundregs[1 + (offset & 0x03)] = Value & 1;         // LFO DAC bits
    return;
  }

  // 0xA800-0xAFFF: Sound latches — MAME mirror 0x07f8
  if((Addr & 0xf800) == 0xA800) {
    soundregs[8 + (Addr & 0x07)] = Value & 1;
    return;
  }

  // 0xB000-0xB7FF: NMI enable (0), stars (4), flip (6-7) — MAME mirror 0x07f8
  if((Addr & 0xf800) == 0xB000) {
    unsigned char offset = Addr & 0x07;
    if(offset == 0) irq_enable[0] = Value & 1;
    if(offset == 4) stars_enabled = (Value & 1);
    return;
  }

  // 0xB800-0xBFFF: Pitch register (VCO) — MAME mirror 0x07ff
  if((Addr & 0xf800) == 0xB800) {
    soundregs[0] = Value;
    return;
  }
}

void mooncresta::outZ80(unsigned short Port, unsigned char Value) { }
unsigned char mooncresta::inZ80(unsigned short Port) { return 0x00; }

void mooncresta::run_frame(void) {
  if(!game_started) game_started = 1;

  current_cpu = 0;
  for(int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
  }

  if(irq_enable[0])
    IntZ80(&cpu[0], INT_NMI);
}

void mooncresta::reset() {
  machineBase::reset();
  // Alloca lazy il buffer nativo trasposto (come scramble/pacman/dkong).
  if(!scr_full)
    scr_full = (unsigned short*)ps_malloc(SCR_NW * SCR_NH * sizeof(unsigned short));
  gfxbank = 0;
  bullet_active = 0;
  stars_enabled = false;
  stars_initialized = false;
  star_count = 0;
  star_scroll_offset = 0;
}

void mooncresta::prepare_frame(void) {
  if(!stars_initialized) stars_init();

  if(stars_enabled)
    star_scroll_offset = (star_scroll_offset + 1) % 288;

  active_sprites = 0;

  // Sprite data at ObjRAM + 0x40 (HW 0x9840 -> memory[0x0C40])
  for(int idx = 7; idx >= 0 && active_sprites < 92; idx--) {
    struct sprite_S spr;
    unsigned char *base = memory + 0x0C40 + idx * 4;

    unsigned char raw_code = base[1] & 0x3f;
    spr.flags = (base[1] >> 6) & 3;
    spr.color = base[2] & 7;

    // Moon Cresta sprite bank extension (MAME mooncrst_extend_sprite_info)
    if(gfxbank && (raw_code & 0x30) == 0x20)
      spr.code = (raw_code & 0x0f) | (gfxbank << 4) | 0x20;
    else
      spr.code = raw_code;

    spr.x = base[0] - 16;
    spr.y = base[3] + 16;

    if(base[3] && (spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
      sprite[active_sprites++] = spr;
  }

  // Bullet data at ObjRAM + 0x60 (HW 0x9860 -> memory[0x0C60])
  bullet_active = 0;
  for(int idx = 0; idx < 8; idx++) {
    unsigned char *bbase = memory + 0x0C60 + idx * 4;
    bullet_x[idx] = bbase[1] - 16;
    bullet_y[idx] = 266 - bbase[3];
    if(bbase[1] && bbase[3] && bullet_x[idx] >= 0 && bullet_x[idx] < 224 &&
       bullet_y[idx] > -4 && bullet_y[idx] < 288)
      bullet_active |= (1 << idx);
  }

  // --- Render NATIVO 224x288 in scr_full (blit via rt = base strip) ---
  // render_row() trasporra' poi scr_full nel framebuffer (stride SCR_NH).
  if(scr_full) {
    memset(scr_full, 0, SCR_NW * SCR_NH * sizeof(unsigned short));
    for(short srow = 2; srow < 34; srow++) {
      rt = scr_full + srow * 8 * SCR_NW;
      render_strip(srow);
    }
  }
}

void mooncresta::blit_tile(short row, char col) {
  if((row < 2) || (row >= 34))
    return;

  unsigned short addr = tileaddr[row][col];
  unsigned short tile_code = memory[0x0800 + addr];
  unsigned char attrib = memory[0x0C00 + 2 * (addr & 31) + 1];

  // Moon Cresta tile bank extension (MAME mooncrst_extend_tile_info)
  if(gfxbank && (attrib & 0x40))
    tile_code |= 0x100;

  const unsigned short *tile = mooncresta_tilemap[tile_code];
  int c = attrib & 7;
  const unsigned short *colors = mooncresta_colormap[c];

  unsigned short *ptr = rt + 8 * col;
  for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++;
    for(char x = 0; x < 8; x++, pix >>= 2) {
      if(pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void mooncresta::blit_tile_scroll(short row, signed char col, unsigned char scroll) {
  if((row < 2) || (row >= 34))
    return;

  unsigned short addr;
  unsigned short mask = 0xffff;
  int sub = scroll & 0x07;

  if(col >= 0) {
    addr = tileaddr[row][col];
    addr = (addr + ((scroll & ~7) << 2)) & 1023;
    if((sub != 0) && (col == 27))
      mask = 0xffff >> (2 * sub);
  } else {
    addr = tileaddr[row][0];
    addr = (addr + 32 + ((scroll & ~7) << 2)) & 1023;
    mask = 0xffff << (2 * (8 - sub));
  }

  unsigned short tile_code = memory[0x0800 + addr];
  unsigned char attrib = memory[0x0C00 + 2 * (addr & 31) + 1];

  if(gfxbank && (attrib & 0x40))
    tile_code |= 0x100;

  const unsigned short *tile = mooncresta_tilemap[tile_code];
  int c = attrib & 7;
  const unsigned short *colors = mooncresta_colormap[c];
  unsigned short *ptr = rt + 8 * col + sub;

  for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++ & mask;
    for(char x = 0; x < 8; x++, pix >>= 2) {
      if(pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void mooncresta::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = mooncresta_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = mooncresta_colormap[sprite[s].color];

  unsigned long mask = 0xffffffff;
  if(sprite[s].x < 0)        mask <<= -2 * sprite[s].x;
  if(sprite[s].x > 224 - 16) mask >>= 2 * (sprite[s].x - (224 - 16));

  short y_offset = sprite[s].y - 8 * row;

  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  if(y_offset < 0) spr -= y_offset;

  unsigned short *ptr = rt + sprite[s].x + 224 * startline;

  for(char r = 0; r < lines2draw; r++, ptr += (224 - 16)) {
    unsigned long pix = *spr++ & mask;
    for(char c = 0; c < 16; c++, pix >>= 2) {
      if(pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void mooncresta::stars_init() {
  static const unsigned char starmap[4] = { 0, 150, 200, 255 };

  unsigned short star_color_lut[64];
  for(int i = 0; i < 64; i++) {
    unsigned char r = starmap[((i >> 4) & 2) | ((i >> 5) & 1)];
    unsigned char g = starmap[((i >> 2) & 2) | ((i >> 3) & 1)];
    unsigned char b = starmap[((i >> 0) & 2) | ((i >> 1) & 1)];
    star_color_lut[i] = rgb_to_swapped565(r, g, b);
  }

  star_count = 0;
  uint32_t shiftreg = 0;
  for(int i = 0; i < 131071 && star_count < MC_MAX_STARS; i++) {
    if((shiftreg & 0x1fe01) == 0x1fe00) {
      int color_idx = (~shiftreg >> 3) & 0x3f;
      int x = (i % 512) / 2;
      int y = i / 512;

      if((y ^ (x >> 3)) & 1) {
        int gx = 255 - y;
        int gy = x + 16;

        if(gx >= 0 && gx < 224 && gy >= 16 && gy < 288) {
          stars[star_count].x = gx;
          stars[star_count].y = gy;
          stars[star_count].color = star_color_lut[color_idx];
          star_count++;
        }
      }
    }
    shiftreg = (shiftreg >> 1) | ((((shiftreg >> 12) ^ ~shiftreg) & 1) << 16);
  }
  stars_initialized = true;
}

// render_strip: corpo dell'ex render_row (stars + tile + sprite + bullet) verso
// rt (strip nativa di scr_full) invece di frame_buffer.
void mooncresta::render_strip(short row) {
  if(row <= 1 || row >= 34) return;

  // Stelle (background, i tile le sovrascrivono)
  if(stars_enabled && stars_initialized) {
    int row_top = 8 * row;
    int row_bot = row_top + 8;
    for(int i = 0; i < star_count; i++) {
      int sy = ((int)stars[i].y + star_scroll_offset) % 288;
      if(sy < 16) sy += 272;
      if(sy >= row_top && sy < row_bot) {
        int sx = stars[i].x;
        if(sx >= 0 && sx < 224) {
          int fb_idx = (sy - row_top) * 224 + sx;
          if(rt[fb_idx] == 0x0000)
            rt[fb_idx] = stars[i].color;
        }
      }
    }
  }

  unsigned char scroll = memory[0x0C00 + 2 * (row - 2)];
  if(scroll == 0) {
    for(char col = 0; col < 28; col++)
      blit_tile(row, col);
  } else {
    if(scroll & 7)
      blit_tile_scroll(row, -1, scroll);
    for(char col = 0; col < 28; col++)
      blit_tile_scroll(row, col, scroll);
  }

  for(unsigned char s = 0; s < active_sprites; s++) {
    if((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);
  }

  // Bullets: piccolo quadrato 2x2 (shells bianchi, player giallo)
  if(bullet_active) {
    short row_top = 8 * row;
    short row_bot = row_top + 8;
    for(int b = 0; b < 8; b++) {
      if(!(bullet_active & (1 << b))) continue;
      short bx = bullet_x[b];
      short by = bullet_y[b];
      if(bx < 0 || bx >= 224) continue;
      if(by + 2 <= row_top || by >= row_bot) continue;

      unsigned short color = (b == 7) ? 0xE0FF : 0xFFFF;
      for(int py = 0; py < 2; py++) {
        short sy = by + py;
        if(sy < row_top || sy >= row_bot) continue;
        int line = (sy - row_top) * 224;
        rt[line + bx] = color;
        if(bx + 1 < 224) rt[line + bx + 1] = color;
      }
    }
  }
}

// Render trasposto 90: scr_full (224x288, riempito in prepare_frame) ->
// frame_buffer (stride SCR_NH), come scramble/pacman/dkong. Ruota il
// framebuffer galagino-portrait sul pannello SPINNERINO.
void mooncresta::render_row(short strip_r) {
  if(!scr_full) return;
  for(int fy = 0; fy < 8; fy++) {
    int fb_y = strip_r * 8 + fy;               // 0..223 (asse corto display)
    int col  = (SCR_NW - 1) - fb_y;            // colonna nativa 223..0
    if(col < 0 || col >= SCR_NW) continue;
    unsigned short *dst       = frame_buffer + fy * SCR_NH;
    const unsigned short *src = scr_full + col;
    for(int y = 0; y < SCR_NH; y++)
      dst[y] = src[y * SCR_NW];
  }
}

const unsigned short *mooncresta::logo(void) {
  return mooncresta_logo;
}

#ifdef LED_PIN
void mooncresta::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if(sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for(char c = 0; c < NUM_LEDS; c++)
      leds[c] = (c == il) ? LED_CYAN : LED_BLUE;
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void mooncresta::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
