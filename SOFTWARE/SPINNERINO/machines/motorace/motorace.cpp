// ============================================================================
// SPINNERINO P4 - machines/motorace/motorace.cpp
//
// MotoRace USA (Irem 1983)
// CPU: Z80 @ 3.072MHz main + M6803 @ 894KHz sound + 2x AY-3-8910
// Schermo arcade ROT270 (cabinet portrait), tile 8x8 3bpp, sprite 16x16, 64x32
// scrollable tilemap.
//
// PIPELINE RENDERING (trasposto inline, no buffer intermedio):
//   Il sorgente Galagino V3 originale scrive in framebuffer 224x288 portrait
//   con coords gia' ROT270-applicate. SPINNERINO ha framebuffer 256x224
//   landscape -> serve trasposizione. Versione ottimizzata: scriviamo
//   direttamente nel frame_buffer landscape SPINNERINO senza buffer
//   intermedio, calcolando inline le coords arcade per ogni pixel.
//
// Mapping inline:
//   per render_row(strip_r), strip_r ∈ [0..27]:
//     portrait_x = 223 - (strip_r*8 + sr)   [sr=0..7 strip-local row]
//     fb_x ∈ [0..239]:  portrait_y = fb_x + ARCADE_Y_OFFSET (16)
//     landscape_x_arcade = 263 - portrait_y      -> 8..247
//     landscape_y_arcade = MR_LY(portrait_x)     -> 0..255  (vedi sotto)
//   Scrittura: frame_buffer[sr * FB_W + fb_x] = pixel
//
// EC11: rotation -> BUTTON_LEFT/RIGHT (sterzo), EC11_SW -> BUTTON_FIRE
// (acceleratore). Hold 100 ms, vol mode disabilitato (FIRE in continuo).
// ============================================================================
// ⚡⚡ L'IDE compila a -Os e qui dentro passano il bus di DUE CPU e un ciclo PER
// PIXEL sul fondo (53.760 pixel a quadro): il pragma vale. Nel progetto ce
// l'hanno gia' cps1944, ghouls, outrun, neogeo e m68k_bus; qui mancava.
// ⚠️ E' la stessa dimenticanza che su BATSUGUN (CABINATONE) valeva una decina di
// fps ([[feedback_ottimizzare_contando_accessi]], caso 4).
#pragma GCC optimize("O2")

#include "motorace.h"
#include "../../emulation/input.h"

#ifdef ENABLE_MOTORACE

// ⛔⛔⛔ GEOMETRIA CORRETTA (2026-08-18). Referto dell'utente: «ha i lati come
// fosse stretchato e delle scritte HUD e grafica escono fuori dal display».
// Erano DUE difetti, e la fonte li dice tutti e due:
//     MAME travrusa.cpp: set_raw(18.432_MHz_XTAL/3, 384, 8, 248, 282, 0, 256)
//     -> area visibile GREZZA  X 8..247 (240)  ·  Y 0..255 (256)
// confermato da fuori: lo screenshot MAME di `motorace` e' **256x240**.
//
// 1. **X**: si disegnavano 256 colonne invece di 240, cioe' `lx` da -8 a 247.
//    Le 16 in piu' l'arcade non le manda: la maschera `& 63` le faceva rientrare
//    dall'altro capo della tilemap. -> FB_W 256 -> 240 (+ videoGameWidth()).
// 2. **Y**: si mostravano solo le righe di tilemap 16..239, cioe' **16 tagliate
//    per lato**. A destra ci finiva la colonna dei NOMI DELLE CITTA' (N.Y., CHI.,
//    ST.L., HOU., L.V., L.A.) accanto alla barra del carburante, piu' l'HI-SCORE
//    mozzato: l'«HUD fuori dal display» del referto.
//
// ⭐ Qui, a differenza del CABINATONE (600 px sul lato corto, dove ci stanno
// tutte e 256), il framework da' al gioco **224 righe fisse** (28 strisce da 8,
// in SPINNERINO.ino) e il pannello ne regge al massimo 232: non ci stanno.
// Scelta dell'utente fra tagliare e comprimere: **si comprime**, cosi' non si
// perde niente — ne' i due bordi strada ne' l'HUD. Si scarta una colonna ogni
// otto, perche' 256 -> 224 e' esattamente 8/7.
// ⚠️ Le due macro sono l'una l'inversa dell'altra e vanno cambiate INSIEME: il
// fondo usa la prima, gli sprite la seconda, e se divergono gli sprite scivolano
// rispetto alla strada.
#define MR_LY(px)  (((px) * 8 + 3) / 7)   // colonna del quadro 0..223 -> riga di tilemap 0..255
#define MR_PX(ly)  (((ly) * 7 + 4) / 8)   // riga di tilemap 0..255   -> colonna del quadro 0..223

#define FB_W            240 // = videoGameWidth(): finestra X grezza 8..247, esatta
#define ARCADE_Y_OFFSET 16  // shift portrait_y -> fb_x, cioe' lx = 247 - fb_x

// ── Z80 instruction fetch ──
unsigned char motorace::opZ80(unsigned short Addr) {
  if (Addr < 0x8000)
    return pgm_read_byte_near(motorace_rom + Addr);
  return 0xFF;
}

// ── Z80 memory read ──
unsigned char motorace::rdZ80(unsigned short Addr) {
  if (Addr < 0x8000)
    return pgm_read_byte_near(motorace_rom + Addr);

  if (Addr >= 0x8000 && Addr <= 0x8FFF)
    return memory[MR_MEM_VRAM + (Addr - 0x8000)];

  if (Addr >= 0xC800 && Addr <= 0xC9FF)
    return memory[MR_MEM_SPRITES + (Addr - 0xC800)];

  if (Addr == 0xD000) {
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_START) val &= ~0x01;
    if (keymask & BUTTON_COIN)  val &= ~0x08;
    return val;
  }
  if (Addr == 0xD001) {
    unsigned char keymask = input->buttons_get();
    unsigned char val = 0xFF;
    if (keymask & BUTTON_RIGHT) val &= ~0x01;
    if (keymask & BUTTON_LEFT)  val &= ~0x02;
    if (keymask & BUTTON_FIRE)  val &= ~0x20;  // accelerate
    if (keymask & BUTTON_DOWN)  val &= ~0x80;  // brake
    return val;
  }
  if (Addr == 0xD002) return 0xFF;
  if (Addr == 0xD003) return MOTORACE_DSW1;
  if (Addr == 0xD004) return MOTORACE_DSW2;

  if (Addr >= 0xE000 && Addr <= 0xEFFF)
    return memory[MR_MEM_WORKRAM + (Addr - 0xE000)];

  return 0xFF;
}

// ── Z80 memory write ──
void motorace::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x8000 && Addr <= 0x8FFF) {
    memory[MR_MEM_VRAM + (Addr - 0x8000)] = Value;
    return;
  }
  if (Addr == 0x9000) { scroll_x_low  = Value; return; }
  if (Addr == 0xA000) { scroll_x_high = Value; return; }

  if (Addr >= 0xC800 && Addr <= 0xC9FF) {
    memory[MR_MEM_SPRITES + (Addr - 0xC800)] = Value;
    return;
  }

  if (Addr == 0xD000) {
    sound_cmd = Value;
    if ((Value & 0x80) == 0)
      m6803_irq(&snd_cpu);
    return;
  }
  if (Addr == 0xD001) {
    flipscreen = Value & 1;
    if (!game_started && (Value & 0x02)) game_started = 1;
    return;
  }
  if (Addr >= 0xE000 && Addr <= 0xEFFF) {
    memory[MR_MEM_WORKRAM + (Addr - 0xE000)] = Value;
    return;
  }
}

// ============================================================================
// M6803 Sound CPU (Irem M52 sound board)
// ============================================================================
motorace *g_motorace_instance = nullptr;

static uint8_t motorace_snd_read(uint16_t addr)            { return g_motorace_instance->snd_read(addr); }
static void    motorace_snd_write(uint16_t addr, uint8_t v){ g_motorace_instance->snd_write(addr, v); }
static uint8_t motorace_snd_port_read(uint8_t port)        { return g_motorace_instance->snd_port_read(port); }
static void    motorace_snd_port_write(uint8_t port, uint8_t v){ g_motorace_instance->snd_port_write(port, v); }

void motorace::init(Input *inp, unsigned short *fb, sprite_S *sb, unsigned char *mb) {
  machineBase::init(inp, fb, sb, mb);
  g_motorace_instance = this;
}

void motorace::reset() {
  machineBase::reset();
  scroll_x_low = 0;
  scroll_x_high = 0;
  flipscreen = 0;
  sound_cmd = 0;
  snd_port1 = 0;
  snd_port2 = 0;
  memset(ay_addr, 0, sizeof(ay_addr));
  memset(ay_regs, 0, sizeof(ay_regs));

  g_motorace_instance = this;
  m6803_ext_read_fn   = motorace_snd_read;
  m6803_ext_write_fn  = motorace_snd_write;
  m6803_port_read_fn  = motorace_snd_port_read;
  m6803_port_write_fn = motorace_snd_port_write;

  m6803_reset(&snd_cpu);
  snd_cpu.irq_pending = 1;  // MAME: IRQ asserted at reset
}

uint8_t motorace::snd_read(uint16_t addr) {
  if (addr == 0x0800) return sound_cmd;
  if (addr >= 0xC000)
    return pgm_read_byte_near(motorace_snd_rom + (addr - 0xC000));
  return 0xFF;
}

void motorace::snd_write(uint16_t addr, uint8_t val) {
  if (addr == 0x0800) {
    if ((sound_cmd & 0x80) != 0) snd_cpu.irq_pending = 0;
    return;
  }
  // ADPCM (MSM5205, non emulato)
}

uint8_t motorace::snd_port_read(uint8_t port) {
  if (port == 1) {
    if (snd_port2 & 0x08) {
      unsigned char reg = ay_addr[0] & 0x0F;
      if (reg == 14) return sound_cmd;
      return ay_regs[0][reg];
    }
    if (snd_port2 & 0x10)
      return ay_regs[1][ay_addr[1] & 0x0F];
    return 0xFF;
  }
  if (port == 2) return 0x00;
  return 0xFF;
}

void motorace::snd_port_write(uint8_t port, uint8_t val) {
  if (port == 1) { snd_port1 = val; return; }
  if (port == 2) {
    if ((snd_port2 & 0x01) && !(val & 0x01)) {
      if (snd_port2 & 0x04) {
        if (snd_port2 & 0x08) ay_addr[0] = snd_port1 & 0x0F;
        if (snd_port2 & 0x10) ay_addr[1] = snd_port1 & 0x0F;
      } else {
        if (snd_port2 & 0x08) {
          unsigned char reg = ay_addr[0] & 0x0F;
          ay_regs[0][reg] = snd_port1;
          if (reg < 14) soundregs[reg] = snd_port1;
          // ripartenza dell'inviluppo a OGNI scrittura del registro 13 (anche
          // con lo stesso valore): la conta in soundregs[14], come Moon Patrol
          if (reg == 13) soundregs[14]++;
        }
        if (snd_port2 & 0x10) {
          unsigned char reg = ay_addr[1] & 0x0F;
          ay_regs[1][reg] = snd_port1;
          if (reg < 14) soundregs[16 + reg] = snd_port1;
          if (reg == 13) soundregs[16 + 14]++;   // idem per il secondo AY
        }
      }
      m6803_internal_ram[0x3D] = 1;
    }
    snd_port2 = val;
    return;
  }
}

// ── Z80 + M6803 frame execution ──
void motorace::run_frame(void) {
  current_cpu = 0;
  for (int i = 0; i < INST_PER_FRAME; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    StepZ80(&cpu[0]); StepZ80(&cpu[0]);
    if (i == INST_PER_FRAME - 1)
      IntZ80(&cpu[0], INT_IRQ);
  }

  // M6803 batch: ~14900 cycles per frame (894 KHz / 60 fps)
  static int nmi_cycle_cnt = 0;
  int snd_budget = 14900;
  while (snd_budget > 0) {
    int cyc = m6803_step(&snd_cpu);
    snd_budget -= cyc;
    nmi_cycle_cnt += cyc;
    if (nmi_cycle_cnt >= 224) {
      nmi_cycle_cnt -= 224;
      m6803_nmi(&snd_cpu);
    }
    uint16_t old_tc = snd_cpu.timer_counter;
    snd_cpu.timer_counter += cyc;
    if (snd_cpu.timer_counter < old_tc) snd_cpu.tcsr |= 0x20;
    if (old_tc <= snd_cpu.timer_output_compare &&
        snd_cpu.timer_counter >= snd_cpu.timer_output_compare)
      snd_cpu.tcsr |= 0x40;
  }
}

// ── Sprite preparation (identico al sorgente, scrive in sprite[]) ──
void motorace::prepare_frame(void) {
  active_sprites = 0;
  for (int offs = 0x1FF; offs >= 0; offs -= 4) {
    unsigned char *spr_base = &memory[MR_MEM_SPRITES + offs - 3];

    unsigned char sy_raw = spr_base[0];
    unsigned char attr   = spr_base[1];
    unsigned char code   = spr_base[2];
    unsigned char sx_raw = spr_base[3];

    if (sy_raw == 0 && sx_raw == 0) continue;

    struct sprite_S spr;
    spr.code  = code;
    spr.color = attr & 0x0F;
    bool flipx = (attr & 0x40) != 0;
    bool flipy = (attr & 0x80) != 0;

    int sx = ((sx_raw + 8) & 0xFF) - 8;
    int sy = 240 - sy_raw;
    if (sy > 191) continue;

    // ⛔ `spr.x` NON e' piu' una colonna del quadro: e' la **Y GREZZA** (0..255).
    // La colonna la ricava blit_sprite_t con MR_PX(), riga per riga, cosi' gli
    // sprite subiscono la STESSA compressione 256->224 del fondo. Tenendo qui la
    // colonna gia' calcolata, uno sprite alto 16 righe grezze coprirebbe 16
    // colonne invece di 14 e scivolerebbe rispetto alla strada.
    spr.x = sy;
    spr.y = 263 - sx - 15;
    spr.flags = (flipy ? 1 : 0) | (flipx ? 2 : 0);

    if ((spr.y > -16) && (spr.y < 288) &&
        (spr.x > -16) && (spr.x < 256)) {
      sprite[active_sprites++] = spr;
    }
    if (active_sprites >= 124) break;
  }
}

// ============================================================================
// Render BG scroll trasposto inline (zero buffer intermedio).
// Per ogni strip_r ∈ [0..27], copre 8 valori di portrait_x (= 8 fb_y) e
// tutti FB_W=240 fb_x. Ottimizzazione: per ogni fb_y_strip (= sr) lx_arcade
// resta costante, quindi scroll_x e tile_col cambiano solo in funzione di fb_x.
// ============================================================================
void motorace::blit_scroll_strip_t(short strip_r) {
  int scroll = scroll_x_low + (scroll_x_high << 8);

  for (int sr = 0; sr < 8; sr++) {
    int portrait_x = 223 - (strip_r * 8 + sr);
    if (portrait_x < 0 || portrait_x >= 224) continue;

    // landscape_y arcade fissato per la riga corrente.
    // ⛔ Era `portrait_x + 16`, cioe' solo 16..239: tagliava 16 righe di tilemap
    // per lato, e a destra ci stava la colonna delle citta' dell'HUD.
    // Ora le 256 righe vere entrano tutte nelle 224 colonne disponibili.
    int ly = MR_LY(portrait_x);     // 0..255
    int tile_row = (ly >> 3) & 31;
    int rom_row_base = ly & 7;      // 0..7 (riga dentro al tile)

    bool scroll_active = (ly < 192);

    unsigned short *fb_dst = frame_buffer + sr * FB_W;

    // ⚡⚡ SI AVANZA PER TILE, NON PER PIXEL. Prima ogni pixel rifaceva la
    // ricerca completa del tile — due letture di VRAM, decodifica di
    // codice/palette/flip e la lettura della riga di grafica — ma tutta quella
    // roba cambia solo ogni OTTO pixel, perche' `lx_scrolled` scende di uno per
    // ogni fb_x. Contato per quadro ([[feedback_ottimizzare_contando_accessi]]:
    // prima di toccare l'architettura si contano gli accessi):
    //   letture di VRAM        107.520 -> ~13.900   (-87%)
    //   letture della tilemap   53.760 -> ~6.950    (-87%)
    //   letture della colormap  53.760 -> 53.760    (una per pixel, inevitabile)
    // ⭐ La riscrittura e' **provabilmente equivalente**, non «sembra uguale»:
    // confrontata con la vecchia su 39.936 casi (6 VRAM casuali x 256 righe x 26
    // valori di scroll, tutte e quattro le combinazioni di flip e tutte le fasi
    // di `lxs & 7`) -> **zero pixel diversi**. Se cambiasse un pixel non sarebbe
    // un'ottimizzazione, sarebbe un difetto.
    const unsigned char *vram_row = &memory[MR_MEM_VRAM + tile_row * 128];
    const int lxs0 = 247 + (scroll_active ? scroll : 0);   // lx_scrolled a fb_x = 0

    int fb_x = 0;
    while (fb_x < FB_W) {
      const int lxs = lxs0 - fb_x;              // scende di 1 a ogni fb_x
      const int tile_col = (lxs >> 3) & 63;

      const unsigned char code_lo = vram_row[2 * tile_col];
      const unsigned char attr    = vram_row[2 * tile_col + 1];

      const int tile_code = code_lo + ((attr & 0xC0) << 2);
      const int flip_x = (attr >> 5) & 1;
      const int flip_y = (attr >> 4) & 1;
      const int rom_row = flip_y ? (7 - rom_row_base) : rom_row_base;

      const unsigned long d = pgm_read_dword_near(&motorace_tilemap[tile_code][rom_row]);
      const unsigned short *colors = motorace_char_cmap[attr & 0x0F];

      // pixel che restano dentro a QUESTO tile andando avanti con fb_x (cioe'
      // indietro con lxs): sono (lxs & 7) + 1, tagliati al bordo del quadro.
      const int base = lxs & 7;
      int n = base + 1;
      if (fb_x + n > FB_W) n = FB_W - fb_x;

      // il flip si decide una volta per tile, non a ogni pixel
      if (flip_x) {
        for (int k = 0; k < n; k++)
          fb_dst[fb_x + k] = pgm_read_word_near(&colors[(d >> ((7 - base + k) * 4)) & 7]);
      } else {
        for (int k = 0; k < n; k++)
          fb_dst[fb_x + k] = pgm_read_word_near(&colors[(d >> ((base - k) * 4)) & 7]);
      }
      fb_x += n;
    }
  }
}

// ============================================================================
// Sprite trasposto: rendering nella strip se lo sprite la tocca.
// ⚠ `sprite[].x` e' la Y GREZZA (0..255): le 16 righe dello sprite finiscono
// nelle colonne MR_PX(x)..MR_PX(x+15), che dopo la compressione sono 14, non 16.
// Strip portrait_x range: [216-strip_r*8 .. 223-strip_r*8] (8 valori, sr-mapped)
// ============================================================================
void motorace::blit_sprite_t(short strip_r, unsigned char s) {
  const int spr_rawY = sprite[s].x;   // ⛔ Y GREZZA (0..255), non una colonna
  const int spr_y    = sprite[s].y;

  int orientation = sprite[s].flags & 3;
  const unsigned long  *spr_data = motorace_spritemap[orientation][sprite[s].code];
  const unsigned short *colors   = motorace_spr_cmap[sprite[s].color];

  // ⭐ Si scorrono le 16 righe e si scarta quella che non cade in questa
  // striscia. Prima si calcolavano r_min/r_max in forma chiusa, ma con la
  // compressione la corrispondenza riga->colonna non e' piu' una traslazione.
  // ⚡ `MR_PX` e' monotona (verificato su tutti i 256 valori), quindi `sr` puo'
  // solo CALARE al crescere di r: appena esce di sotto si puo' uscire dal ciclo
  // invece di provare le righe rimanenti a vuoto.
  for (int r = 0; r < 16; r++) {
    const int rawY = spr_rawY + r;
    if ((unsigned)rawY > 255) continue;             // fuori dalla tilemap
    const int portrait_x = MR_PX(rawY);             // stessa compressione del fondo
    const int sr = 223 - portrait_x - strip_r * 8;
    if (sr < 0) break;                              // ...e le prossime pure
    if (sr > 7) continue;                           // non ancora in questa striscia

    unsigned long row_lo = pgm_read_dword_near(&spr_data[r * 2]);
    unsigned long row_hi = pgm_read_dword_near(&spr_data[r * 2 + 1]);

    unsigned short *fb_dst = frame_buffer + sr * FB_W;

    for (int c = 0; c < 16; c++) {
      int portrait_y = spr_y + 15 - c;
      int fb_x = portrait_y - ARCADE_Y_OFFSET;
      if (fb_x < 0 || fb_x >= FB_W) continue;

      unsigned char px = (c < 8)
                       ? ((row_lo >> (c * 4)) & 7)
                       : ((row_hi >> ((c - 8) * 4)) & 7);

      if (px) {
        fb_dst[fb_x] = pgm_read_word_near(&colors[px]);
      }
    }
  }
}

void motorace::render_row(short strip_r) {
  if (strip_r < 0 || strip_r >= 28) return;

  // BG scroll (riempie tutta la strip FB_W x 8)
  blit_scroll_strip_t(strip_r);

  // Sprite: filtraggio veloce per strip portrait_x range.
  // ⚠️ `sprite[].x` e' la Y GREZZA: la prima colonna e' MR_PX(x), e dopo la
  // compressione lo sprite ne copre 14, non 16. Si tiene comunque il +15 (una
  // colonna in piu' del necessario): un candidato di troppo lo scarta
  // blit_sprite_t, uno di meno sarebbe uno sprite che sparisce.
  int strip_pX_lo = 216 - strip_r * 8;
  int strip_pX_hi = strip_pX_lo + 7;
  for (unsigned char s = 0; s < active_sprites; s++) {
    int px0 = MR_PX(sprite[s].x);
    if (((px0 + 15) >= strip_pX_lo) && (px0 <= strip_pX_hi)) {
      blit_sprite_t(strip_r, s);
    }
  }
}

const unsigned short *motorace::logo(void) {
  return motorace_logo;
}

#endif // ENABLE_MOTORACE
