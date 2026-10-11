#include "config.h"
#include "machines/mariobros/mariobros.h"

#ifdef ENABLE_MARIOBROS

#include "emulation/input.h"
#include <math.h>

// ============================================================================
// Mario Bros (Nintendo 1983) — Z80 + I8035 Sound CPU + Z80 DMA
//
// PORTING SPINNERINO WIFI: core emulazione VERBATIM. Render come FULL33/Green
// Beret: pannello commutato in PORTRAIT (video.setLandscapeGame) e righe native
// da 240 px (scalate da 256) scritte DIRETTAMENTE in frame_buffer. NIENTE
// transpose software, NIENTE buffer full-frame PSRAM.
//
// MAME ROT0 → arcade orizzontale 256x224 (single screen, no scroll).
// Rendering: render_row(row) per row=4..31, decode pixel-by-pixel VERBATIM FULL33,
// scaling 256→240 nearest-neighbor (scale_x[] per tile, *15>>4 per sprite).
//
// Tilemap: 32×32, TILEMAP_SCAN_ROWS, VRAM addr = row * 32 + col
// Visible: X=0..255 (32 cols), Y=16..239 (rows 2-29)
// Cropped to 240 pixels wide (X=8..247, centered)
//
// Memory map:
//   0x0000-0x5FFF  ROM (24K)
//   0x6000-0x6FFF  RAM (4K)
//   0x7000-0x73FF  Sprite RAM
//   0x7400-0x77FF  Video RAM (tile codes, NO separate color RAM)
//   0x7C00         IN0 read / sound write
//   0x7C80         IN1 read / sound write
//   0x7D00         Scroll Y write
//   0x7E00         Sound latch 0 write (IRQ to I8035)
//   0x7E80-0x7E87  LS259 mainlatch
//   0x7F00-0x7F07  Sample triggers
//   0x7F80         DSW read
//   0xF000-0xFFFF  ROM (4K high)
// ============================================================================

// ============================================================================
// AUDIO — trascritto dal CABINATONE (mario.cpp, confermato sul cabinato).
// Prima qui l'MCU girava dentro run_frame («6 passi ogni 4 del Z80», legata al
// render) e ogni byte scritto sul DAC veniva suonato come un campione a ritmo
// fisso: ma il M58715 NON scrive il DAC a ritmo costante, quindi ogni tratto in
// cui scriveva piu' piano o piu' in fretta usciva deformato, e la coda si
// svuotava o si riempiva -> il GRACCHIO (es. fra l'intro e l'inizio partita).
// Ora, come sul cabinato e come gia' i tamburi di GYRUSS qui: l'MCU gira sul
// task AUDIO a cicli veri (11 MHz / 15 = 733.333 cicli/s, 22,9 per campione a
// 32 kHz) e il LIVELLO del DAC si legge a ogni campione.
// Dal cabinato anche: la linea EA (bit 5 di P2 invertito) al posto della ROM
// patchata, P2 letto con il bit 4 a massa, T0/T1 e INT riarmati a ogni passo.
// ============================================================================
#define MB_SND_XTAL 11000000
#define MB_SND_DIV  (15 * 32000)
#define MB_GAIN     96          // come il cabinato, prima dei filtri
#define MB_OUT_DIV  48          // cabinato int16 -> livello di valueToBuffer:
                                // (dac-128)*96/48 = *2, lo stesso di prima

// Cicli macchina per opcode MCS-48 (manuale Intel): la tabella del cabinato
// (DONKEY KONG, misurata in dkong_harness/dksnd.c).
static const unsigned char MB_CICLI[256] = {
  1,1,2,2,2,1,1,1,2,2,2,2,2,2,2,2,  // 0_
  1,1,2,2,2,1,2,1,1,1,1,1,1,1,1,1,  // 1_
  1,1,1,2,2,1,2,1,1,1,1,1,1,1,1,1,  // 2_
  1,1,2,1,2,1,2,1,1,2,2,2,2,2,2,2,  // 3_
  1,1,1,2,2,1,2,1,1,1,1,1,1,1,1,1,  // 4_
  1,1,2,2,2,1,2,1,1,1,1,1,1,1,1,1,  // 5_
  1,1,1,1,2,1,1,1,1,1,1,1,1,1,1,1,  // 6_
  1,1,2,1,2,1,2,1,1,1,1,1,1,1,1,1,  // 7_
  2,2,1,2,2,1,2,1,2,2,2,1,2,2,2,2,  // 8_
  2,2,2,2,2,1,2,1,2,2,2,1,2,2,2,2,  // 9_
  1,1,1,2,2,1,1,1,1,1,1,1,1,1,1,1,  // A_
  2,2,2,2,2,1,2,1,2,2,2,2,2,2,2,2,  // B_
  1,1,1,1,2,1,2,1,1,1,1,1,1,1,1,1,  // C_
  1,1,2,2,2,1,1,1,1,1,1,1,1,1,1,1,  // D_
  1,1,1,2,2,1,2,1,2,2,2,2,2,2,2,2,  // E_
  1,1,2,1,2,1,2,1,1,1,1,1,1,1,1,1,  // F_
};

void mariobros::reset() {
  snd_pronto = false;                  // l'audio tace mentre si riparte
  machineBase::reset();

  // sound_reset() di MAME: latch 0, 1 e 3 a zero; P2 dopo il reset = 0xFF
  // (quindi EA basso: si parte dalla ROM interna finta).
  i8048_reset(&cpu_8035);
  latch0 = latch1 = latch3 = 0;
  int_line = 0;
  latch2 = 0xFF;
  cyc_frac = 0;
  snd_debito = 0;
  dac = 0x80;

  // ---- i filtri della scheda (netlist `nl_mario_dac` e il mixer) ----
  //   · passa-alto a ~20 Hz: toglie la continua del DAC a riposo;
  //   · LM3900 a retroazione multipla: passa-basso a 3.063 Hz, Q 0,354;
  //   · nodo del mixer (C31 22 nF su 6,83 k): passa-basso a un polo a 1.059 Hz.
  const float fs = 32000.0f;
  const float f0 = 3063.0f, Q = 0.354f;
  const float w0 = 2.0f * 3.14159265f * f0 / fs;
  const float al = sinf(w0) / (2.0f * Q), c = cosf(w0);
  const float a0 = 1.0f + al;
  bq_b0 = (1.0f - c) * 0.5f / a0;
  bq_b1 = (1.0f - c) / a0;
  bq_b2 = bq_b0;
  bq_a1 = -2.0f * c / a0;
  bq_a2 = (1.0f - al) / a0;
  bq_z1 = bq_z2 = 0.0f;
  lp_a  = 1.0f - expf(-2.0f * 3.14159265f * 1059.0f / fs);
  lp_y  = 0.0f;
  hp_x = hp_y = 0;

  gfx_bank = 0;
  palette_bank = 0;
  nmi_mask = 0;
  gfx_scroll = 0;
  snd_pronto = true;
}

// ── I8035 Sound CPU interface: `mario_sound_map` / `mario_sound_io_map` ──

void mariobros::wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) {
  // P2 -> soundlatch[2] (e la linea EA). P1 in uscita e il BUS non sono
  // collegati nel driver: il DAC lo scrive solo MOVX (wrI8048_xdm).
  if (port == 2) { latch2 = pos; state->p2_state = pos; }
}

unsigned char mariobros::rdI8048_port(struct i8048_state_S *state, unsigned char port) {
  if (port == 1) return latch1;                            // p1_in = soundlatch[1]
  if (port == 2) return (unsigned char)(latch2 & 0xEF);    // bit 4 a massa
  return state->bus_latch;
}

// tune_r: con P2.7 alto il comando del Z80, altrimenti la ROM esterna a pagine
unsigned char mariobros::rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) {
  if (latch2 & 0x80) return latch0;
  return mariobros_rom_cpu2[((latch2 & 0x0F) << 8) | addr];
}

// dac_w: OGNI scrittura MOVX cambia il livello del DAC
void mariobros::wrI8048_xdm(struct i8048_state_S *state, unsigned char addr, unsigned char data) {
  dac = data;
}

// La ROM interna (NO_DUMP) e' zeri piu' i tre byte di `sound_start()`:
// SEL MB1, JMP 0. La linea EA = bit 5 di P2 INVERTITO: con EA basso i primi
// 2 KB vengono dalla ROM interna finta, con EA alto dalla esterna.
unsigned char mariobros::rdI8048_rom(struct i8048_state_S *state, unsigned short addr) {
  addr &= 0x0FFF;
  const bool ea = !(latch2 & 0x20);
  if (addr < 0x800 && !ea) {
    static const unsigned char BOOT[3] = { 0xF5, 0x04, 0x00 };
    return (addr < 3) ? BOOT[addr] : 0x00;
  }
  return mariobros_rom_cpu2[addr];
}

// Esegue `cicli` cicli macchina dell'MCU, col resto risommato.
void mariobros::snd_run(int cicli) {
  i8048_state_S *s = &cpu_8035;
  int budget = cicli - snd_debito;
  while (budget > 0) {
    // ⛔ INT e' di LIVELLO: il core lo cancella quando lo serve, e la routine
    // della morte lo interroga con JNI -> va RIARMATO a ogni passo.
    s->notINT = !int_line;
    const unsigned char l3 = latch3;
    s->T0 = (l3 & 1) != 0;
    s->T1 = (l3 & 2) != 0;
    const unsigned char op = rdI8048_rom(s, s->PC);
    const bool era_int = s->inInterrupt;
    i8048_step(s);
    int cyc = MB_CICLI[op];
    if (s->inInterrupt && !era_int) cyc += 2;          // la CALL implicita
    budget -= cyc;
  }
  snd_debito = -budget;
}

// Un campione a 32 kHz, al livello di valueToBuffer. Gira sul task AUDIO.
short mariobros::audio_sample(void) {
  if (!snd_pronto) return 0;
  cyc_frac += MB_SND_XTAL;
  const int c = (int)(cyc_frac / MB_SND_DIV);
  cyc_frac -= (unsigned int)c * MB_SND_DIV;
  snd_run(c);

  // Il DAC e' a 8 bit SENZA SEGNO: si centra su 128.
  const float x = (float)(((int)dac - 128) * MB_GAIN);
  const float y = bq_b0 * x + bq_z1;
  bq_z1 = bq_b1 * x - bq_a1 * y + bq_z2;
  bq_z2 = bq_b2 * x - bq_a2 * y;
  lp_y += lp_a * (y - lp_y);
  const int xi = (int)lp_y;
  const int h = xi - hp_x + (int)(((long long)hp_y * 4079) >> 12);
  hp_x = xi;
  hp_y = h;
  int v = h / MB_OUT_DIV;
  if (v >  700) v =  700;              // sotto il tetto pulito di valueToBuffer
  if (v < -700) v = -700;
  return (short)v;
}

// ── Z80 I/O ports (DMA chip at port 0x00) ──

void mariobros::outZ80(unsigned short Port, unsigned char Value) {
  // Z80 DMA at port 0x00 — accept writes silently
  (void)Port; (void)Value;
}

unsigned char mariobros::inZ80(unsigned short Port) {
  unsigned char port = Port & 0xFF;
  if (port == 0x00)
    return 0x01;  // DMA ready
  return 0xFF;
}

// ── Z80 Main CPU ──

unsigned char mariobros::opZ80(unsigned short Addr) {
  if (Addr < 0x6000)
    return mariobros_rom_cpu1[Addr];
  if (Addr >= 0xF000)
    return mariobros_rom_cpu1_high[Addr - 0xF000];
  return 0xFF;
}

unsigned char mariobros::rdZ80(unsigned short Addr) {
  if (Addr < 0x6000)
    return mariobros_rom_cpu1[Addr];

  if (Addr >= 0xF000)
    return mariobros_rom_cpu1_high[Addr - 0xF000];

  // RAM + Sprite + Video: 0x6000-0x77FF
  if (Addr >= 0x6000 && Addr < 0x7800)
    return memory[Addr - 0x6000];

  // IN0: 0x7C00
  if (Addr == 0x7C00) {
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0x00;
    if (keymask & BUTTON_RIGHT) retval |= 0x01;
    if (keymask & BUTTON_LEFT)  retval |= 0x02;
    if (keymask & BUTTON_UP)    retval |= 0x04;
    if (keymask & BUTTON_DOWN)  retval |= 0x08;
    if (keymask & BUTTON_FIRE)  retval |= 0x10;
    if (keymask & BUTTON_START) retval |= 0x20;
    return retval;
  }

  // IN1: 0x7C80
  if (Addr == 0x7C80) {
    unsigned char keymask = input->buttons_get();
    unsigned char retval = 0x00;
    if (keymask & BUTTON_COIN) retval |= 0x20;
    return retval;
  }

  // DSW: 0x7F80
  if (Addr == 0x7F80)
    return MARIOBROS_DIP;

  return 0xFF;
}

void mariobros::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x6000 && Addr < 0x7800) {
    memory[Addr - 0x6000] = Value;
    return;
  }

  // 0x7C00/0x7C80 WRITE = walk sample triggers (discrete analog, ignored)
  if (Addr == 0x7C00 || Addr == 0x7C80) return;
  if (Addr == 0x7D00) { gfx_scroll = Value; return; }

  // Sound latch 0 (command to sound CPU, read via tune_r / rdI8048_xdm)
  // NO interrupt here — sound CPU polls this via MOVX. INT only from 0x7F00+0.
  if (Addr == 0x7E00) {
    latch0 = Value;
    return;
  }

  // LS259 mainlatch: 0x7E80-0x7E87
  if (Addr >= 0x7E80 && Addr <= 0x7E87) {
    unsigned char bit_val = Value & 1;
    switch (Addr & 7) {
      case 0: gfx_bank = bit_val; break;
      case 1: break;
      case 2: break;  // flip screen
      case 3: palette_bank = bit_val; break;
      case 4: nmi_mask = bit_val; break;
      case 5:                                    // DMA trigger
        if (bit_val) {
          // Sprite DMA: copy from 0x6900 to 0x7000
          // Mario Bros has 1K sprite RAM (256 entries), not 96 like DKong
          // DMA: 512 bytes (128 entries) during title screen for Luigi,
          // 384 bytes (96 entries) during gameplay
          {
            int dma_len = (memory[0x05] == 0) ? 0x200 : 0x180;
            memcpy(memory + 0x1000, memory + 0x900, dma_len);
          }
        }
        break;
      case 6: break;
      case 7: break;
    }
    return;
  }

  // Sample triggers (LS259 addressable latch)
  // MAME: offset 0 = death (INT to sound CPU)
  //        offset 1-2 = coin/ice (latch[3] → T0/T1)
  //        offset 3-6 = crab/turtle/fly/coin (latch[1] → port P1 bits)
  //        offset 7 = skid (discrete analog, ignored)
  if (Addr >= 0x7F00 && Addr <= 0x7F07) {
    unsigned char bit_val = Value & 1;
    switch (Addr & 7) {
      // Solo i LATCH: l'MCU li applica lei a ogni passo (snd_run, task audio).
      case 0: int_line = bit_val; break;                  // death -> INT (di livello)
      case 1: case 2: {                                   // coin, ice -> T0, T1
        const unsigned char m = (unsigned char)(1 << ((Addr & 7) - 1));
        latch3 = (unsigned char)((latch3 & ~m) | (bit_val ? m : 0));
        break;
      }
      case 3: case 4: case 5: case 6: {                   // crab, turtle, fly, coin -> P1
        const unsigned char m = (unsigned char)(1 << ((Addr & 7) - 3));
        latch1 = (unsigned char)((latch1 & ~m) | (bit_val ? m : 0));
        break;
      }
      case 7: break;  // skid (discrete analog)
    }
    return;
  }
}

// ── Frame execution ──

void mariobros::run_frame(void) {
  current_cpu = 0;
  game_started = 1;

  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);
    // La CPU sonora NON gira piu' qui: vive sul task audio (audio_sample).
  }

  if (nmi_mask)
    IntZ80(cpu, INT_NMI);
}

// ============================================================================
// prepare_frame: SOLO parsing sprite (1x/frame), VERBATIM FULL33 (bande 4..31,
// spr.y = 240 - sp[0] + 8 + 16). + buttons_get() per il long-press SPINNERINO.
// ============================================================================
void mariobros::prepare_frame(void) {
  // Driver long-press reset (regola SPINNERINO: chiamare buttons_get ogni frame).
  if (input) input->buttons_get();

  active_sprites = 0;

  for (int idx = 0; idx < 128 && active_sprites < 64; idx++) {
    unsigned char *sp = memory + 0x1000 + 4 * idx;

    if (sp[0] == 0) continue;
    if (sp[2] == 0) continue;  // skip sprites with no tile (invisible markers)

    // MAME draw_sprites (no-flip): x = sp[3] - 8, y = 240 - sp[0] + 8
    struct sprite_S spr;
    spr.y = 240 - sp[0] + 8 + 16;  // +16 for visible area offset (bands start at row 4)
    spr.x = sp[3] - 8;
    spr.code = sp[2];
    spr.color = sp[1] & 0x0F;
    spr.flags = ((sp[1] & 0x80) ? 1 : 0) |
                ((sp[1] & 0x40) ? 2 : 0);

    if ((spr.x > -16) && (spr.x < 256) && (spr.y > -16) && (spr.y < 288))
      sprite[active_sprites++] = spr;
  }
}

// ============================================================================
// render_row — VERBATIM FULL33. Pannello in PORTRAIT (video.setLandscapeGame):
// le righe native da 240 px (scalate da 256) vanno dritte in frame_buffer.
// Bande row=4..31 (28 bande = 224 px gioco), come Green Beret. Scaling 256->240
// nearest-neighbor: tile via tabella scale_x[], sprite via *15>>4.
// ============================================================================
void mariobros::render_row(short row) {
  // 28 visible bands: rows 4-31 (like Green Beret)
  if (row < 4 || row >= 32) return;

  // CYD: 240px display. Game is 256px → scale down (nearest neighbor 256→240).
  #define MARIO_FB_W  240

  // Precomputed scale table: display px (0-239) → game px (0-255)
  // Avoids expensive integer division per pixel
  static const unsigned char scale_x[240] = {
     0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,15,
    17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,
    34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,
    51,52,53,54,55,56,57,58,59,60,61,62,63,64,65,66,
    68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,
    85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,
   102,103,104,105,106,107,108,109,110,111,112,113,114,115,116,117,
   119,120,121,122,123,124,125,126,127,128,129,130,131,132,133,134,
   136,137,138,139,140,141,142,143,144,145,146,147,148,149,150,151,
   153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,168,
   170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,
   187,188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,
   204,205,206,207,208,209,210,211,212,213,214,215,216,217,218,219,
   221,222,223,224,225,226,227,228,229,230,231,232,233,234,235,236,
   238,239,240,241,242,243,244,245,246,247,248,249,250,251,252,253
  };

  int game_row_base = (row - 4);  // 0-27

  // === TILES === (scaled 256→240)
  for (int line = 0; line < 8; line++) {
    int game_y = (game_row_base * 8 + line + 16 + gfx_scroll + 17) & 0xFF;
    int tile_row = game_y >> 3;
    int tile_r = 7 - (game_y & 7);

    unsigned short *ptr = frame_buffer + line * MARIO_FB_W;

    for (int px = 0; px < MARIO_FB_W; px++) {
      int game_x = scale_x[px];  // fast lookup instead of division
      int tile_col = game_x >> 3;
      int tile_c = 7 - (game_x & 7);

      int tile_idx = tile_row * 32 + tile_col;
      unsigned char tile_code = memory[0x1400 + tile_idx];

      unsigned short full_code = tile_code + (gfx_bank ? 256 : 0);
      if (full_code >= 512) full_code = 0;

      const unsigned short *colors = mariobros_colormap[palette_bank][tile_code >> 5];

      unsigned short tile_row_data = mariobros_tilemap[full_code][tile_r];
      unsigned char px_val = (tile_row_data >> (tile_c * 2)) & 3;

      ptr[(MARIO_FB_W - 1) - px] = colors[px_val];
    }
  }

  // === SPRITES === (scaled 256→240)
  for (int s = 0; s < active_sprites; s++) {
    short spr_x = sprite[s].x;
    short spr_y = sprite[s].y;

    if ((spr_y >= 8 * (row + 1)) || ((spr_y + 16) <= 8 * row))
      continue;

    int code = sprite[s].code;
    if (code >= 256) continue;

    const unsigned short *colors = mariobros_colormap_sprite[palette_bank][sprite[s].color & 0x0F];
    int variant = sprite[s].flags & 3;
    const unsigned long *spr_gfx = mariobros_sprites[variant][code];

    int band_top = 8 * row;

    for (int sub = 0; sub < 8; sub++) {
      int screen_y = band_top + sub;
      if (screen_y < spr_y || screen_y >= spr_y + 16) continue;

      int dy = screen_y - spr_y;
      int row_idx = dy * 2;
      unsigned long spr_left  = pgm_read_dword(&spr_gfx[row_idx]);
      unsigned long spr_right = pgm_read_dword(&spr_gfx[row_idx + 1]);

      for (int dx = 0; dx < 16; dx++) {
        int game_screen_x = spr_x + dx;
        int screen_x = (game_screen_x * 15) >> 4;  // fast scale: *240/256 = *15/16
        if (screen_x < 0 || screen_x >= MARIO_FB_W) continue;

        unsigned char px_val;
        if (dx < 8)
          px_val = (spr_left >> ((7 - dx) * 3)) & 7;
        else
          px_val = (spr_right >> ((15 - dx) * 3)) & 7;

        if (px_val) {
          frame_buffer[(7 - sub) * MARIO_FB_W + ((MARIO_FB_W - 1) - screen_x)] = colors[px_val];
        }
      }
    }
  }
}

// ── LEDs ──
#ifdef LED_PIN
void mariobros::gameLeds(CRGB *leds) {
  static char sub_cnt = 0;
  if (sub_cnt++ == 4) {
    sub_cnt = 0;
    static char led = 0;
    char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
    for (char c = 0; c < NUM_LEDS; c++) {
      if (c == il) leds[c] = LED_RED;
      else         leds[c] = LED_BLACK;
    }
    led = (led + 1) % (2 * NUM_LEDS - 2);
  }
}

void mariobros::menuLeds(CRGB *leds) {
  memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif // LED_PIN

#endif // ENABLE_MARIOBROS
