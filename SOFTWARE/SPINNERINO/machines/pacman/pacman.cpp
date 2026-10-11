#include "pacman.h"

// Geometria nativa Pac-Man per la trasposizione SPINNERINO (224 largo x 288 alto).
#define PAC_NW 224   // larghezza nativa (colonne) = stride di pac_full
#define PAC_NH 288   // altezza nativa (righe)

void pacman::reset() {
  machineBase::reset();
  // Alloca lazy il buffer nativo intero in PSRAM (come Xevious/Gyruss).
  if (!pac_full)
    pac_full = (unsigned short*)ps_malloc(PAC_NW * PAC_NH * sizeof(unsigned short));
}

unsigned char pacman::opZ80(unsigned short Addr) {
  return pacman_rom[Addr];
}

unsigned char pacman::rdZ80(unsigned short Addr) {
  Addr &= 0x7fff;   // a15 is unused

  if(Addr < 16384)
    return pacman_rom[Addr];

  if((Addr & 0xf000) == 0x4000) {    
    // this includes spriteram 1
    return memory[Addr - 0x4000];
  }   

  if((Addr & 0xf000) == 0x5000) {
    // get a mask of currently pressed keys    
    unsigned char keymask = input->buttons_get();
    
    if(Addr == 0x5080)    // dip switch
      return PACMAN_DIP;
    
    if(Addr == 0x5000) {
      unsigned char retval = 0xff;
      if(keymask & BUTTON_UP)    retval &= ~0x01;
      if(keymask & BUTTON_LEFT)  retval &= ~0x02;
      if(keymask & BUTTON_RIGHT) retval &= ~0x04;
      if(keymask & BUTTON_DOWN)  retval &= ~0x08;
      if(keymask & BUTTON_COIN)  retval &= ~0x20;  
      return retval;
    }
    
    if(Addr == 0x5040) {
      unsigned char retval = 0xff; // 0xef for service
      if(keymask & BUTTON_START)  retval &= ~0x20;  
      return retval;
    }
  }
  return 0xff;
}

void pacman::wrZ80(unsigned short Addr, unsigned char Value) {
  Addr &= 0x7fff;   // a15 is unused
    
  if((Addr & 0xf000) == 0x4000) {
    // writing 85 (U, first char of UP) to the top left corner
    // is an indication that the game has booted up      
    if(Addr == 0x4000 + 985 && Value == 85)
      game_started = 1;
    
    memory[Addr - 0x4000] = Value;
    return;
  }
  
  if((Addr & 0xff00) == 0x5000) {
    // 0x5060 to 0x506f writes through to ram (spriteram2)
    if((Addr & 0xfff0) == 0x5060)
      memory[Addr - 0x4000] = Value;
    
    if(Addr == 0x5000) 
      irq_enable[0] = Value & 1;
    
    if((Addr & 0xffe0) == 0x5040) {
      if(soundregs[Addr - 0x5040] != Value & 0x0f)
	      soundregs[Addr - 0x5040] = Value & 0x0f;
    }    
    return;
  }
} 

void pacman::outZ80(unsigned short Port, unsigned char Value) {
  irq_ptr = Value;
}

void pacman::run_frame(void) {
  for(int i=0;i<INST_PER_FRAME;i++) {
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
  }
      
  if(irq_enable[0])
    IntZ80(cpu, irq_ptr);     
}

void pacman::prepare_frame(void) {
  // Do all the preparations to render a screen.
  /* preprocess sprites */
  active_sprites = 0;
  for(int idx=0;idx<8 && active_sprites<92;idx++) {
    unsigned char *sprite_base_ptr = memory + 2*(7-idx);
    struct sprite_S spr;     
      
    spr.code = sprite_base_ptr[0x0ff0] >> 2;
    spr.color = sprite_base_ptr[0x0ff1] & 63;
    spr.flags = sprite_base_ptr[0x0ff0] & 3;
    
    // adjust sprite position on screen for upright screen
    spr.x = 255 - 16 - sprite_base_ptr[0x1060];
    spr.y = 16 + 256 - sprite_base_ptr[0x1061];

    if((spr.code < 64) &&
       (spr.y > -16) && (spr.y < 288) &&
       (spr.x > -16) && (spr.x < 224)) {      
      
      // save sprite in list of active sprites
      sprite[active_sprites++] = spr;
    }
  }

  // Render nativo a frame INTERO (224x288) in pac_full (orientamento arcade).
  // Le blit_* scrivono via `rt` = base della strip corrente; render_row()
  // trasporra' poi pac_full nel framebuffer 288 di SPINNERINO.
  if (pac_full) {
    memset(pac_full, 0, PAC_NW * PAC_NH * sizeof(unsigned short));
    for (short srow = 0; srow < 36; srow++) {   // 36 strip x 8 = 288 righe
      rt = pac_full + srow * 8 * PAC_NW;
      for (char col = 0; col < 28; col++)
        blit_tile(srow, col);
      for (unsigned char s = 0; s < active_sprites; s++)
        if ((sprite[s].y < 8 * (srow + 1)) && ((sprite[s].y + 16) > 8 * srow))
          blit_sprite(srow, s);
    }
  }
}

// draw a single 8x8 tile
void pacman::blit_tile(short row, char col) {
  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile = tileRom(addr);
  const unsigned short *colors = colorRom(memory[0x400 + addr] & 63);
  unsigned short *ptr = rt + 8 * col;

  // 8 pixel rows per tile
  for(char r=0;r<8;r++,ptr+=(224-8)) {
    unsigned short pix = *tile++;
    // 8 pixel columns per tile
    for(char c=0;c<8;c++,pix>>=2) {
      if(pix & 3) *ptr = colors[pix&3];
      ptr++;
    }
  }
}

void pacman::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = spriteRom(sprite[s].flags & 3, sprite[s].code);
  const unsigned short *colors = colorRom(sprite[s].color & 63);

  // create mask for sprites that clip left or right
  unsigned long mask = 0xffffffff;
  if(sprite[s].x < 0)      mask <<= -2*sprite[s].x;
  if(sprite[s].x > 224-16) mask >>= (2*(sprite[s].x-(224-16)));		

  short y_offset = sprite[s].y - 8*row;

  // check if there are less than 8 lines to be drawn in this row
  unsigned char lines2draw = 8;
  if(y_offset < -8) lines2draw = 16+y_offset;

  // check which sprite line to begin with
  unsigned short startline = 0;
  if(y_offset > 0) {
    startline = y_offset;
    lines2draw = 8 - y_offset;
  }

  // if we are not starting to draw with the first line, then
  // skip into the sprite image
  if(y_offset < 0)
    spr -= y_offset;  

  // calculate pixel lines to paint
  unsigned short *ptr = rt + sprite[s].x + 224*startline;
  
  // 16 pixel rows per sprite
  for(char r=0;r<lines2draw;r++,ptr+=(224-16)) {
    unsigned long pix = *spr++ & mask;
    // 16 pixel columns per tile
    for(char c=0;c<16;c++,pix>>=2) {
      unsigned short col = colors[pix&3];
      if(col) *ptr = col;
      ptr++;
    }
  }
}

// Trasposizione del frame nativo (pac_full, 224x288) nel framebuffer SPINNERINO.
// Stride framebuffer = videoGameWidth() = 288 (PAC_NH). Pannello ruotato 90:
//   fb_x = gfb_y (riga nativa -> asse lungo 288)
//   fb_y = 223 - gfb_x (colonna nativa, reversed)
void pacman::render_row(short strip_r) {
  if (!pac_full) return;
  for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
    int fb_y  = strip_r * 8 + fb_y_strip;
    int gfb_x = 223 - fb_y;                 // colonna nativa (reversed)
    if (gfb_x < 0 || gfb_x >= PAC_NW) continue;
    unsigned short *dst       = frame_buffer + fb_y_strip * PAC_NH;  // stride 288
    const unsigned short *src = pac_full + gfb_x;   // colonna gfb_x, passo PAC_NW
    for (int gfb_y = 0; gfb_y < PAC_NH; gfb_y++)
      dst[gfb_y] = src[gfb_y * PAC_NW];     // fb_x = gfb_y (tutti i 288)
  }
}

const unsigned short *pacman::tileRom(unsigned short addr) {
  return pacman_tilemap[memory[addr]];
}

const unsigned short *pacman::colorRom(unsigned short addr) {
  return pacman_colormap[addr];
}

const unsigned long *pacman::spriteRom(unsigned char flags, unsigned char code) {
  return pacman_sprites[flags][code];
}

const signed char *pacman::waveRom(unsigned char value) {
  return pacman_wavetable[value]; 
}

const unsigned short *pacman::logo(void) {
  return pacman_logo;
}

#ifdef LED_PIN
void pacman::gameLeds(CRGB *leds) {
  // pacman: yellow on blue "knight rider" ...
  static char sub_cnt = 0;
  if(sub_cnt++ == 4) {
    sub_cnt = 0;
      
    // and also do the marquee LEDs
    static char led = 0;
      
    char il = (led<NUM_LEDS)?led:((2*NUM_LEDS-2)-led);
    for(char c=0;c<NUM_LEDS;c++) {
      if(c == il) leds[c] = LED_YELLOW;
      else        leds[c] = LED_BLUE;
    }
    led = (led + 1) % (2*NUM_LEDS-2);      
  }    
}

void pacman::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB));
}
#endif