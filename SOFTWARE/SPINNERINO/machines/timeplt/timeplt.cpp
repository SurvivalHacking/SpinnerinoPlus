#include "machines/timeplt/timeplt.h"
#include "emulation/input.h"

// Time Pilot (Konami 1982) memory map:
//   Main CPU (Z80 @ 3.072 MHz):
//     0x0000-0x5FFF: ROM (24KB)   0xA000-0xA3FF: Color RAM   0xA400-0xA7FF: Video RAM
//     0xA800-0xAFFF: Work RAM     0xB000/0xB400: Sprite RAM bank 0/1
//     0xC000 r: scanline   0xC200 r: DSW2   0xC300 r: IN0   0xC320 r: IN1   0xC360 r: DSW1
//     0xC000 w: sound latch   0xC200 w: watchdog   0xC300 w: LS259 latch
//   Sound CPU (Z80 @ 1.789 MHz) + 2x AY-3-8910.
// Logica 1:1 da FULL33. Render avvolto nel trasposto SPINNERINO: i blit scrivono
// in `rt` (strip nativa di scr_full), render_row traspone 90 in frame_buffer.

#define MEM_COLORRAM  0x0000
#define MEM_VIDEORAM  0x0400
#define MEM_WORKRAM   0x0800
#define MEM_SPRITES0  0x1000
#define MEM_SPRITES1  0x1100

unsigned char timeplt::opZ80(unsigned short Addr) {
  if(current_cpu == 0) {
    if(Addr < 0x6000)
      return timeplt_rom[Addr];
  } else {
    if(Addr < 0x1000)
      return timeplt_snd_rom[Addr];
  }
  return 0xff;
}

unsigned char timeplt::rdZ80(unsigned short Addr) {
  if(current_cpu == 1) {
    // ---- Sound CPU memory map ----
    if(Addr < 0x1000)
      return timeplt_snd_rom[Addr];
    if(Addr < 0x3000)
      return 0xFF;

    if(Addr >= 0x3000 && Addr <= 0x3FFF)
      return snd_ram[Addr & 0x3FF];

    // AY#1 data read (0x4000-0x4FFF mirror)
    if((Addr & 0xF000) == 0x4000) {
      unsigned char reg = ay_addr[0] & 0x0F;
      if(reg == 14) return soundlatch;
      if(reg == 15) {
        // Port B = timer: LS90 bi-quinary, divide-by-5120
        static const unsigned char timeplt_timer[10] = {
          0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xa0, 0xb0, 0xa0, 0xd0
        };
        return timeplt_timer[(snd_icnt / 45) % 10];
      }
      return ay_regs[0][reg];
    }

    // AY#2 data read (0x6000-0x6FFF mirror)
    if((Addr & 0xF000) == 0x6000) {
      unsigned char reg = ay_addr[1] & 0x0F;
      return ay_regs[1][reg];
    }

    return 0xFF;
  }

  // ---- Main CPU memory map ----
  if(Addr < 0x6000)
    return timeplt_rom[Addr];

  if(Addr >= 0xA000 && Addr <= 0xA3FF)
    return memory[MEM_COLORRAM + (Addr & 0x03FF)];

  if(Addr >= 0xA400 && Addr <= 0xA7FF)
    return memory[MEM_VIDEORAM + (Addr & 0x03FF)];

  if(Addr >= 0xA800 && Addr <= 0xAFFF)
    return memory[MEM_WORKRAM + (Addr & 0x07FF)];

  if((Addr & 0xFC00) == 0xB000)
    return memory[MEM_SPRITES0 + (Addr & 0xFF)];

  if((Addr & 0xFC00) == 0xB400)
    return memory[MEM_SPRITES1 + (Addr & 0xFF)];

  if(Addr == 0xC000) {
    multiplexUsed = 1;          // segna il punto di snapshot sprite multiplex
    return scanline_counter;
  }

  if(Addr == 0xC200)
    return TIMEPLT_DSW2 | (input->demoSoundsOff() ? TIMEPILOT_DSW2_DEMO_SOUND_OFF : TIMEPILOT_DSW2_DEMO_SOUND_ON);

  if(Addr == 0xC300) {          // IN0: coin, start (active-LOW)
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0xFF;
    if(keymask & BUTTON_COIN)   retval &= ~0x01;
    if(keymask & BUTTON_START)  retval &= ~0x08;
    return retval;
  }

  if(Addr == 0xC320) {          // IN1: P1 joystick + fire (active-LOW)
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0xFF;
    if(keymask & BUTTON_LEFT)   retval &= ~0x01;
    if(keymask & BUTTON_RIGHT)  retval &= ~0x02;
    if(keymask & BUTTON_UP)     retval &= ~0x04;
    if(keymask & BUTTON_DOWN)   retval &= ~0x08;
    if(keymask & BUTTON_FIRE)   retval &= ~0x10;
    return retval;
  }

  if(Addr == 0xC340)
    return 0xFF;                // IN2: P2 (idle)

  if(Addr == 0xC360)
    return TIMEPLT_DSW1;

  return 0x00;
}

void timeplt::wrZ80(unsigned short Addr, unsigned char Value) {
  if(current_cpu == 1) {
    // ---- Sound CPU writes ----
    if(Addr >= 0x3000 && Addr <= 0x3FFF) {
      snd_ram[Addr & 0x3FF] = Value;
      return;
    }
    // AY#1 data (0x4000-0x4FFF), addr (0x5000-0x5FFF)
    if((Addr & 0xF000) == 0x4000) {
      unsigned char reg = ay_addr[0] & 0x0F;
      ay_regs[0][reg] = Value;
      if(reg < 14) soundregs[reg] = Value;          // -> audio engine (AY#1)
      return;
    }
    if((Addr & 0xF000) == 0x5000) {
      ay_addr[0] = Value & 0x0F;
      return;
    }
    // AY#2 data (0x6000-0x6FFF), addr (0x7000-0x7FFF)
    if((Addr & 0xF000) == 0x6000) {
      unsigned char reg = ay_addr[1] & 0x0F;
      ay_regs[1][reg] = Value;
      if(reg < 14) soundregs[16 + reg] = Value;      // -> audio engine (AY#2)
      return;
    }
    if((Addr & 0xF000) == 0x7000) {
      ay_addr[1] = Value & 0x0F;
      return;
    }
    return;   // filtri RC 0x8000+: ignorati
  }

  // ---- Main CPU writes ----
  if(Addr >= 0xA000 && Addr <= 0xA3FF) {
    memory[MEM_COLORRAM + (Addr & 0x03FF)] = Value;
    return;
  }

  if(Addr >= 0xA400 && Addr <= 0xA7FF) {
    if(!game_started) game_started = 1;
    memory[MEM_VIDEORAM + (Addr & 0x03FF)] = Value;
    return;
  }

  if(Addr >= 0xA800 && Addr <= 0xAFFF) {
    memory[MEM_WORKRAM + (Addr & 0x07FF)] = Value;
    return;
  }

  if((Addr & 0xFC00) == 0xB000) {
    memory[MEM_SPRITES0 + (Addr & 0xFF)] = Value;
    return;
  }

  if((Addr & 0xFC00) == 0xB400) {
    memory[MEM_SPRITES1 + (Addr & 0xFF)] = Value;
    return;
  }

  if(Addr == 0xC000) {
    soundlatch = Value;
    return;
  }

  if(Addr == 0xC200)
    return;   // watchdog

  // LS259 latch 0xC300-0xC30F
  if(Addr >= 0xC300 && Addr <= 0xC30F) {
    unsigned char bit_sel = (Addr >> 1) & 0x07;
    unsigned char bit_val = Value & 1;
    switch(bit_sel) {
      case 0: nmi_enable = bit_val; break;       // NMI enable
      case 1: break;                             // flip screen (ignorato)
      case 2:                                     // sound CPU IRQ (edge 0->1)
        if(bit_val && !snd_irq_last) snd_irq_pending = 1;
        snd_irq_last = bit_val;
        break;
      case 3: break;                             // mute (ignorato)
      case 4: video_enable = bit_val; break;     // video enable
      case 5: break;                             // coin counter 1
      case 6: break;                             // coin counter 2
      case 7: break;
    }
    return;
  }
}

void timeplt::outZ80(unsigned short Port, unsigned char Value) { }
unsigned char timeplt::inZ80(unsigned short Port) { return 0x00; }

// Main Z80 @ 3.072 MHz + Sound Z80 @ 1.789 MHz + 2x AY-3-8910 (FULL33 verbatim)
void timeplt::run_frame(void) {
  multiplexUsed = 0;
  multiplexUsedCopy = 0;
  scanline_counter = 0;

  // Loop = valore di riferimento 1280 (NON INST_PER_FRAME): con meno lo scanline
  // counter non raggiunge i valori alti su cui la main CPU fa busy-wait -> freeze.
  for(int i = 0; i < 1280; i++) {
    current_cpu = 0;
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);

    current_cpu = 1;
    StepZ80(&cpu[1]); snd_icnt++;
    StepZ80(&cpu[1]); snd_icnt++;

    if((i % 5) == 0) scanline_counter++;

    if(snd_irq_pending && (cpu[1].IFF & IFF_1)) {
      IntZ80(&cpu[1], INT_RST38);
      snd_irq_pending = 0;
    }

    // Snapshot sprite RAM alla prima lettura dello scanline counter di questo frame
    if(multiplexUsed && !multiplexUsedCopy) {
      multiplexUsedCopy = 1;
      memcpy(multiplexBank0, &memory[MEM_SPRITES0], 0x100);
      memcpy(multiplexBank1, &memory[MEM_SPRITES1], 0x100);
    }
  }

  if(!multiplexUsed) {
    memset(multiplexBank0, 0, sizeof(multiplexBank0));
    memset(multiplexBank1, 0, sizeof(multiplexBank1));
  }

  if(nmi_enable) {
    current_cpu = 0;
    IntZ80(&cpu[0], INT_NMI);
  }
}

// Estrae sprite da una coppia di banchi sprite RAM nella lista sprite.
// Sprite in orientamento LANDSCAPE (nessuna pre-rotazione nel romconv): la
// rotazione ROT90 e' gestita dentro blit_sprite (trasposizione interna).
// `pass` 0 = snapshot multiplex, 1 = sprite RAM corrente. Ogni passata ha il suo
// stato per-slot (tp_slot_spr/age[pass]) per l'anti-flicker (vedi TP_SPRITE_PERSIST).
void timeplt::extract_sprites(const unsigned char *bank0, const unsigned char *bank1, int pass) {
  sprite_S      *slot_spr = tp_slot_spr[pass];
  unsigned char *slot_age = tp_slot_age[pass];

  int slot = 0;
  for(int offs = 0x3E; offs >= 0x10; offs -= 2, slot++) {
    bool valid_now = false;
    struct sprite_S spr;

    unsigned char sx_raw = bank0[offs];
    unsigned char sy_raw = bank1[offs + 1];
    unsigned char code   = bank0[offs + 1];
    unsigned char attr   = bank1[offs];

    unsigned char color = attr & 0x3F;
    unsigned char flipx = (~attr >> 6) & 1;
    unsigned char flipy = (attr >> 7) & 1;

    int sy = 241 - sy_raw;
    if(sy > -16 && sy < 256) {
      spr.x = (int)sy_raw - 17;
      spr.y = (int)sx_raw + 16;
      spr.code = code;
      spr.color = color;
      spr.flags = (flipy ? 1 : 0) | (flipx ? 2 : 0);
      if((spr.y > -16) && (spr.y < 304) && (spr.x > -16) && (spr.x < 224))
        valid_now = true;
    }

    if(valid_now) {
      // Slot attivo: posizione corrente, azzera l'eta'.
      slot_spr[slot] = spr;
      slot_age[slot] = 0;
      if(active_sprites < 128) sprite[active_sprites++] = spr;
    } else if(slot_age[slot] < TP_SPRITE_PERSIST) {
      // Slot spento questo frame ma valido di recente -> ridisegna l'ultimo
      // (contrasta il blink a frame alterni che a 60Hz farebbe lampeggiare).
      slot_age[slot]++;
      if(active_sprites < 128) sprite[active_sprites++] = slot_spr[slot];
    }
  }
}

void timeplt::prepare_frame(void) {
  active_sprites = 0;

  if(video_enable) {
    // Pass 0: sprite catturati allo snapshot multiplex di meta' frame (FULL33)
    extract_sprites(multiplexBank0, multiplexBank1, 0);
    // Pass 1: stato sprite RAM corrente (fine frame)
    extract_sprites(&memory[MEM_SPRITES0], &memory[MEM_SPRITES1], 1);
  }

  // --- Render NATIVO 224x288 in scr_full (blit via rt = base strip) ---
  if(scr_full) {
    memset(scr_full, 0, SCR_NW * SCR_NH * sizeof(unsigned short));
    if(video_enable) {
      for(short srow = 2; srow < 34; srow++) {
        rt = scr_full + srow * 8 * SCR_NW;
        render_strip(srow);
      }
    }
  }
}

void timeplt::blit_tile(short row, char col) {
  blit_tile_cat(row, col, -1);   // -1 = disegna tutto (nessun filtro categoria)
}

void timeplt::blit_tile_cat(short row, char col, signed char cat_filter) {
  if((row < 2) || (row >= 34))
    return;

  unsigned short addr = tileaddr[row][col];
  unsigned char vram_val = memory[MEM_VIDEORAM + addr];
  unsigned char cram_val = memory[MEM_COLORRAM + addr];

  unsigned char category = (cram_val & 0x10) >> 4;
  if(cat_filter >= 0 && category != (unsigned char)cat_filter)
    return;

  unsigned short tile_code = vram_val + ((cram_val & 0x20) << 3);
  unsigned char color = cram_val & 0x1F;
  // 2026-10-03 - FIX SPARO DOPPIO (stesso fix di CABINATONE, confermato sul
  // cabinato). MAME: TILE_FLIPYX(attr >> 6) = bit6 flipX, bit7 flipY nel quadro
  // ORIZZONTALE; il convertitore RUOTA i caratteri di 90, quindi qui bit6 = flip
  // VERTICALE e bit7 = flip ORIZZONTALE. I proiettili del giocatore sono
  // CARATTERI a coppie (bit7 acceso a sinistra perche' i puntini combacino):
  // con gli assi invertiti uscivano su piu' colonne in volo verticale.
  unsigned char flip_x = (cram_val >> 7) & 1;
  unsigned char flip_y = (cram_val >> 6) & 1;

  const unsigned short *tile = timeplt_tilemap[tile_code];
  const unsigned short *colors = timeplt_char_colormap[color];

  unsigned short *ptr = rt + 8 * col;

  for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
    int src_r = flip_y ? (7 - r) : r;
    unsigned short pix = tile[src_r];
    if(flip_x) {
      for(char c = 0; c < 8; c++) {
        unsigned short p = (pix >> ((7 - c) * 2)) & 3;
        ptr[c] = colors[p];
      }
      ptr += 8;
    } else {
      for(char c = 0; c < 8; c++, pix >>= 2) {
        *ptr = colors[pix & 3];
        ptr++;
      }
    }
  }
}

// Sprite trasposti (dati sprite landscape). ROM row -> screen X (15-r), ROM col
// -> screen Y (forward). Preserva il tiling multi-sprite (nuvole, ecc.).
void timeplt::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr_data = timeplt_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = timeplt_sprite_colormap[sprite[s].color];

  int spr_x = sprite[s].x;
  int spr_y = sprite[s].y;
  int row_start = 8 * row;

  int y_begin = (spr_y > row_start) ? spr_y : row_start;
  int y_end   = ((spr_y + 15) < (row_start + 7)) ? (spr_y + 15) : (row_start + 7);
  if(y_begin > y_end) return;

  for(int r = 0; r < 16; r++) {
    int screen_x = spr_x + 15 - r;
    if(screen_x < 0 || screen_x >= 224) continue;
    unsigned long row_data = spr_data[r];

    for(int screen_y = y_begin; screen_y <= y_end; screen_y++) {
      int c = screen_y - spr_y;
      unsigned char px = (row_data >> (c * 2)) & 3;
      if(px)
        rt[(screen_y - row_start) * 224 + screen_x] = colors[px];
    }
  }
}

// render_strip: corpo dell'ex render_row (tile cat0 -> sprite -> tile cat1)
// verso rt (strip nativa di scr_full) invece di frame_buffer.
void timeplt::render_strip(short row) {
  if(row <= 1 || row >= 34) return;

  // Ordine MAME: tile(cat 0) -> sprite -> tile(cat 1). Le tile cat 1 stanno
  // SOPRA gli sprite (es. testo "(c) KONAMI 1982").
  for(char col = 0; col < 28; col++)
    blit_tile_cat(row, col, 0);

  for(unsigned char s = 0; s < active_sprites; s++) {
    if((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
      blit_sprite(row, s);
  }

  for(char col = 0; col < 28; col++)
    blit_tile_cat(row, col, 1);
}

// Render trasposto 90: scr_full (224x288, riempito in prepare_frame) ->
// frame_buffer (stride SCR_NH), come scramble/mooncresta.
void timeplt::render_row(short strip_r) {
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

const unsigned short *timeplt::logo(void) {
  return timeplt_logo;
}

#ifdef LED_PIN
void timeplt::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if(sub_cnt++ == 32) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for(char c = 0; c < NUM_LEDS; c++)
      leds[c] = (c == il) ? LED_CYAN : LED_WHITE;
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void timeplt::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
