#include "1942.h"

// Geometria nativa portrait 1942: 224 wide x 288 tall (36 strip da 8px)
#define _1942_NW 224
#define _1942_NH 288

void _1942::reset() {
  machineBase::reset();
  if (!_1942_full)
    _1942_full = (unsigned short *)ps_malloc(_1942_NW * _1942_NH * sizeof(unsigned short));
}

unsigned char _1942::opZ80(unsigned short Addr) {
  if (current_cpu == 0)
    return _1942_rom_cpu1[Addr];
  else
    return _1942_rom_cpu2[Addr];
}

unsigned char _1942::rdZ80(unsigned short Addr) {
  if(current_cpu == 0) {
    // main CPU    
    if(Addr < 32768) return _1942_rom_cpu1[Addr];

    // CPU1 banked ROM
    if((Addr & 0xc000) == 0x8000) {
      if(_1942_bank == 0)                           return _1942_rom_cpu1_b0[Addr - 0x8000];
      else if((_1942_bank == 1) && (Addr < 0xb000)) return _1942_rom_cpu1_b1[Addr - 0x8000];
      else if(_1942_bank == 2)	                    return _1942_rom_cpu1_b2[Addr - 0x8000];
    }
    
    // RAM mapping
    // 0000-0fff    4k main CPU work RAM
    // 1000-17ff    2k fgvram
    // 1800-1fff    2k audio CPU work RAM
    // 2000-23ff    1k bgvram
    // 2400-247f   128 spriteram
    // if(Addr == 0xe0a5) return 2;  // invincibility
    
    // 4k main RAM
    if((Addr & 0xf000) == 0xe000) 
      return memory[Addr - 0xe000];
    
    // 128 byte spriteram
    if((Addr & 0xff80) == 0xcc00)
      return memory[Addr - 0xcc00 + 0x2400];
    
    // 2k fgvram
    if((Addr & 0xf800) == 0xd000)
      return memory[Addr - 0xd000 + 0x1000];
    
    // 1k bgvram
    if((Addr & 0xfc00) == 0xd800)
      return memory[Addr - 0xd800 + 0x2000];
    
    // ------------ dip/ctrl io -----------
    
    // COIN1,COIN2,X,SERVICE1,X,START2,START1
    if(Addr == 0xc000) {
      unsigned char keymask = input->buttons_get();
      unsigned char retval = 0xff;
      static unsigned char last_coin = 0;
      if(keymask & BUTTON_COIN && !last_coin)  retval &= ~0x80;
      if(keymask & BUTTON_START) retval &= ~0x01;
      last_coin = keymask & BUTTON_COIN;
      return retval;
    }
    
    // P1: X,X,B2,B1,U,D,L,R
    if(Addr == 0xc001) {
      unsigned char keymask = input->buttons_get();
      unsigned char retval = 0xff;
      if(keymask & BUTTON_RIGHT) retval &= ~0x01;
      if(keymask & BUTTON_LEFT)  retval &= ~0x02;
      if(keymask & BUTTON_DOWN)  retval &= ~0x04;
      if(keymask & BUTTON_UP)    retval &= ~0x08;
      if(keymask & BUTTON_FIRE)  retval &= ~0x10;
      if(keymask & BUTTON_EXTRA)  retval &= ~0x20;  // B2 loop/barrel = BUTTON_EXTRA (Pin 5 I2C, B/Circle BT)
      return retval;
    }
    
    // P2: X,X,B2,B1,U,D,L,R
    if(Addr == 0xc002) return 0xff;
    
    // DIP A
    if(Addr == 0xc003) return (unsigned char)~_1942_DIP_A;
    
    // DIP B
    if(Addr == 0xc004) return (unsigned char)~_1942_DIP_B;
    
  } else {
    // second/audio CPU
    if(Addr < 16384) return _1942_rom_cpu2[Addr];
    
    // 2k audio ram
    if((Addr & 0xf800) == 0x4000)
      return memory[Addr - 0x4000 + 0x1800];
    
    // read sound command from latch from main CPU
    if(Addr == 0x6000)
      return _1942_sound_latch;
  }  
  return 0xff;
}

void _1942::wrZ80(unsigned short Addr, unsigned char Value) {
  // RAM mapping
  // 0000-0fff    4k main CPU work RAM
  // 1000-17ff    2k fgvram
  // 1800-1fff    2k audio CPU work RAM
  // 2000-23ff    1k bgvram
  // 2400-247f   128 spriteram
  
  if(current_cpu == 0) {
    // 4k main RAM
    if((Addr & 0xf000) == 0xe000) {
      memory[Addr - 0xe000] = Value;
      return;
    }
    
    // 128 byte spriteram
    if((Addr & 0xff80) == 0xcc00) {
      memory[Addr - 0xcc00 + 0x2400] = Value;
      return;
    }
    
    // 2k fgvram
    if((Addr & 0xf800) == 0xd000) {
      // printf("#%d@%04x fgvram write %04x = %02x\n", current_cpu, cpu[current_cpu].PC.W, Addr, Value);
      memory[Addr - 0xd000 + 0x1000] = Value;
      return;
    }
    
    // 1k bgvram
    if((Addr & 0xfc00) == 0xd800) {
      memory[Addr - 0xd800 + 0x2000] = Value;
      return;
    }
    
    if((Addr & 0xfff0) == 0xc800) {    
      if(Addr == 0xc800) {
	      _1942_sound_latch = Value;
	      return;
      }
      
      if(Addr == 0xc802) {
	      _1942_scroll = Value | (_1942_scroll & 0xff00);
	      return;
      }
      
      if(Addr == 0xc803) {
	      _1942_scroll = (Value << 8)|(_1942_scroll & 0xff);
	      return;
      }
      
      if(Addr == 0xc804) {
	      // bit 7: flip screen
	      // bit 4: cpu b reset
	      // bit 0: coin counter
	      sub_cpu_reset = Value & 0x10;
	
	      if(sub_cpu_reset) {
	        current_cpu = 1;
	        ResetZ80(&cpu[1]);
	      }
	      return;
      }
      
      if(Addr == 0xc805) {
	      _1942_palette = Value;
	      return;
      }
      
      if(Addr == 0xc806) {
	      _1942_bank = Value;
	      return;
      }
    }    
  } else {
    // 2k audio ram
    if((Addr & 0xf800) == 0x4000) {
      memory[Addr - 0x4000 + 0x1800] = Value;
      return;
    }
    
    // AY's
    if((Addr & 0x8000) == 0x8000) {
      int ay = (Addr & 0x4000) != 0;
      
      if(Addr & 1) soundregs[16 * ay + _1942_ay_addr[ay]] = Value;      
      else   	   _1942_ay_addr[ay] = Value & 15;
      
      return;
    }
  }
} 

void _1942::run_frame(void) {
  current_cpu = 0;
  game_started = 1;
      
  for(char f = 0; f < 4; f++) {
    for(int i = 0; i < INST_PER_FRAME / 3; i++) {
      current_cpu = 0; 
      StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
      if(!sub_cpu_reset) {
        current_cpu = 1;
        StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);
      }
    }

    // generate interrupts, main CPU two times per frame
    if(f & 1) {
      current_cpu = 0;
      IntZ80(&cpu[0], 0xc7 | ((f & 2) ? (1 << 3) : (2 << 3)));
    }

    // sound CPU four times per frame
    if(!sub_cpu_reset) {
      // the audio CPU updates the AY registers exactly one time
      // during each interrupt.
      current_cpu = 1;
      IntZ80(&cpu[1], INT_RST38);
    }
  }
}

void _1942::prepare_frame(void) {
  // Do all the preparations to render a screen.
  
  /* preprocess sprites */
  active_sprites = 0;
  for(int idx = 0; idx < 32 && active_sprites < 124; idx++) {
    struct sprite_S spr;         
    unsigned char *sprite_base_ptr = memory + 0x2400 + 4 * (31 - idx);
   
    if(sprite_base_ptr[3] && sprite_base_ptr[2]) {
      // unlike all other machine, 1942 has 512 sprites
      spr.code = (sprite_base_ptr[0] & 0x7f) +
        4*(sprite_base_ptr[1] & 0x20);

      spr.color = sprite_base_ptr[1] & 0x0f;
      spr.x = sprite_base_ptr[2] - 16;
      spr.y = 256 - (sprite_base_ptr[3] - 0x10 * (sprite_base_ptr[1] & 0x10));

      // store 9th code bit in flags
      spr.flags = (sprite_base_ptr[0] & 0x80) >> 7;

      // 16 .. 23 only on left screen
      if((idx >= 8) && (idx < 16))
        spr.flags |= 0x80;   // flag for "left screen half only"
  
      if((idx >= 0) && (idx < 8)) 
        spr.flags |= 0x40;   // flag for "right screen half only"
  
      // attr & 0xc0: double and quad height
      if((spr.y > -16) && (spr.y < 288) &&
         (spr.x > -16) && (spr.x < 224)) {      
      
        // save sprite in list of active sprites
        sprite[active_sprites++] = spr;

        // handle double and quad sprites
        if(sprite_base_ptr[1] & 0xc0) {
          spr.x += 16;
          spr.code += 1;
          if(spr.x < 224) {
            sprite[active_sprites++] = spr;
            if(sprite_base_ptr[1] & 0x80) {
              spr.x += 16;
              spr.code += 1;
              if(spr.x < 224) {
                sprite[active_sprites++] = spr;
                spr.x += 16;
                spr.code += 1;
                if(spr.x < 224)
                  sprite[active_sprites++] = spr;
              }
            }
          }
        }
      }
    }
  }

  // --- Render trasposto: disegna l'intero frame nativo 224x288 in _1942_full ---
  // (le righe 0-1 e 34-35 restano nere -> bordi)
  if (_1942_full) {
    memset(_1942_full, 0, _1942_NW * _1942_NH * sizeof(unsigned short));
    for (short srow = 2; srow < 34; srow++) {
      rt = _1942_full + srow * 8 * _1942_NW;
      blit_bgtile_row(srow);
      for (unsigned char s = 0; s < active_sprites; s++)
        if ((sprite[s].y < 8 * (srow + 1)) && ((sprite[s].y + 16) > 8 * srow))
          blit_sprite(srow, s);
      for (char col = 0; col < 28; col++)
        blit_tile(srow, col);
    }
  }
}

void _1942::blit_bgtile_row(short row) {
  row += 32 - 2;  // adjust for top two unused rows and scrolling to zero

  // calculate first pixel line to be displayed from row and
  // scroll register
  int line = (-8 * row + _1942_scroll) & 511;
  char yoffset = (16 - line) & 15;
  
  // if yoffset > 8, then the data is not sufficient for a full
  // row. The remaining bits have to be taken from the next tile row
  int lines2draw = (yoffset > 8) ? (16 - yoffset) : 8;
  
  // determine memory address of first tile to draw
  unsigned short addr = 0x2000 + 32 * (((line - 1) & 511) / 16) + 1;

  // tiles are 16x16, thus render 14 tiles
  for(char col = 0; col < 14; col++) {   
    unsigned short *ptr = rt + 16 * col;

    unsigned char attr = memory[addr + col + 16];
    const unsigned short *colors = _1942_colormap_tiles[_1942_palette][attr & 31];
    unsigned short chr = memory[addr + col] + ((attr & 0x80) <<1);
    const unsigned long *tile = _1942_tilemap[(attr >> 5) &3][chr] + 2 * yoffset;

    // draw up to 8 pixel rows
    char r;
    for(r = 0; r < lines2draw; r++, ptr += (224 -16)) {
      unsigned long pix = *tile++;
      for(char c = 0; c < 8; c++, pix >>= 4) *ptr++ = colors[pix & 7];
      pix = *tile++;
      for(char c = 0; c < 8; c++,pix >>= 4) *ptr++ = colors[pix & 7];
    }

    // more lines from next tile to draw?
    if(r != 8) {
      int next_line = (line - 16) & 511;
      unsigned short next_addr = 0x2000 + 32 * (((line - 17) & 511) / 16) + 1;
      attr = memory[next_addr + col + 16];
      colors = _1942_colormap_tiles[_1942_palette][attr & 31];
      chr = memory[next_addr + col] + ((attr & 0x80) << 1);    
      tile = _1942_tilemap[(attr >> 5) & 3][chr];
      
      for(; r < 8; r++, ptr += (224 - 16)) {
	      unsigned long pix = *tile++;
	      for(char c = 0; c < 8; c++, pix >>= 4) *ptr++ = colors[pix & 7];
	      pix = *tile++;
      	for(char c = 0; c < 8; c++, pix >>= 4) *ptr++ = colors[pix & 7];
      }
    }
  }
}
  
void _1942::blit_tile(short row, char col) {
  // screen is rotated
  unsigned short addr = tileaddr[35 - row][27 - col];    
  unsigned short chr = memory[0x1000 + addr];
  if(chr == 0x30) return; // empty space
  
  unsigned char attr = memory[0x1400 + addr];
  if(attr & 0x80) chr += 256;

  const unsigned short *tile = _1942_charmap[chr];

  // colorprom sb-0.f1 contains 64 color groups
  const unsigned short *colors = _1942_colormap_chars[attr & 63];

  unsigned short *ptr = rt + 8*col;

  // 8 pixel rows per tile
  for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = *tile++;
    // 8 pixel columns per tile
    for(char c = 0; c < 8; c++, pix >>= 2) {
      if(pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void _1942::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = _1942_sprites[sprite[s].code + 256 * (sprite[s].flags & 1)];
  const unsigned short *colors = _1942_colormap_sprites[4 * (sprite[s].color & 15)];

  // create mask for sprites that clip left or right
  // avoid 64 arithmetic
  unsigned long mask[2] = { 0xffffffff, 0xffffffff };
  if(sprite[s].x < 0)        lsl64(mask, -sprite[s].x);
  if(sprite[s].x > 224 - 16) lsr64(mask,  sprite[s].x - (224 - 16));

  // set mask for "left screen half only sprites"
  if(sprite[s].flags & 0x80) {
    if(sprite[s].x >= 113)    return;
    if(sprite[s].x > 113 - 16)  lsr64(mask, sprite[s].x - (113 -16));
  }

  // set mask for "right screen half only sprites"
  if(sprite[s].flags & 0x40) {
    if(sprite[s].x <= 113 - 16) return;
    if(sprite[s].x < 113)     lsl64(mask, 113 - sprite[s].x);
  }

  mask[0] = ~mask[0]; mask[1] = ~mask[1];
  short y_offset = sprite[s].y - 8 * row;

  // check if there are less than 8 lines to be drawn in this row
  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16 + y_offset;

  // check which sprite line to begin with
  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  // if we are not starting to draw with the first line, then
  // skip into the sprite image
  if(y_offset < 0) spr -= 2 * y_offset;  

  // calculate pixel lines to paint  
  unsigned short *ptr = rt + sprite[s].x + 224 * startline;
  
  // 16 pixel rows per sprite
  for(char r = 0; r < lines2draw; r++, ptr += (224 - 16)) {
    // 16 pixel columns per tile

    unsigned long pix = *spr++ | mask[0];
    for(char c = 0; c < 8; c++, pix >>= 4) {
      if((pix & 15) != 15) *ptr = colors[pix & 15];
      ptr++;
    }
    
    pix = *spr++ | mask[1];
    for(char c = 0; c < 8; c++, pix >>= 4) {
      if((pix & 15) != 15) *ptr = colors[pix & 15];
      ptr++;
    }
  }
}

// Render trasposto 90: _1942_full (224x288 portrait) -> frame_buffer landscape
// (strip da 8 colonne native = 8 righe del framebuffer SP, larghezza 288)
void _1942::render_row(short strip_r) {
  if (!_1942_full) return;
  for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
    int fb_y = strip_r * 8 + fb_y_strip;
    int gfb_x = (_1942_NW - 1) - fb_y;          // colonna nativa <- riga landscape
    if (gfb_x < 0 || gfb_x >= _1942_NW) continue;
    unsigned short *dst = frame_buffer + fb_y_strip * _1942_NH;
    const unsigned short *src = _1942_full + gfb_x;
    for (int gfb_y = 0; gfb_y < _1942_NH; gfb_y++)
      dst[gfb_y] = src[gfb_y * _1942_NW];
  }
}

const unsigned short *_1942::logo(void) {
  return _1942_logo;
}

void _1942::lsl64(unsigned long *mask, int pix) {
  if(pix < 8)
    mask[0] <<= 4 * pix;
  else {
    mask[0] = 0;
    mask[1] <<= 4 * (pix - 8);
  }
}
  
void _1942::lsr64(unsigned long *mask, int pix) {
  if(pix < 8)
    mask[1] >>= 4 * pix;
  else {
    mask[1] = 0;
    mask[0] >>= 4 * (pix - 8);
  }
}

#ifdef LED_PIN
void _1942::gameLeds(CRGB *leds) {
  static const CRGB _1942_bg_leds[] =
  {
     CRGB(0x001000), CRGB(0x00FF00), CRGB(0x00FF20), CRGB(0x00FFFF), 
     CRGB(0x0040FF), CRGB(0x0000FF), CRGB(0x000020), CRGB(0x000000)
  };

  static char sub_cnt = 0;
  if(sub_cnt++ == 4) {
    sub_cnt = 0;
      
    static char led = 0;
      
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for(char c = 0; c < (NUM_LEDS + 1) / 2; c++)
      leds[NUM_LEDS - 1 - c] = leds[c] = _1942_bg_leds[(c + led) % (sizeof(_1942_bg_leds) / sizeof(CRGB))];
        
    led = (led + 1) % (sizeof(_1942_bg_leds) / sizeof(CRGB));
  }
}

void _1942::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB));
}
#endif