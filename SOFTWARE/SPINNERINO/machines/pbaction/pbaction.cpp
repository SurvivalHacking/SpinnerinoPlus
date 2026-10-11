// ============================================================================
// pbaction.cpp ROOT — Tehkan/Tecmo Pinball Action (1985)
// Porting da P4 NEW_multi a sambi 3.5".
//
// Adattamenti P4 → sambi:
//   - ROM da SD → array PROGMEM (160 KB totali in flash)
//   - RAM da heap_caps_malloc → slice del buffer `memory[]` (RAMSIZE 11008)
//   - Rimosso log_ring (non disponibile sambi); Serial.printf periodici leggeri
//   - prepare_frame chiama buttons_get() per long-press reset (regola sambi)
//   - freeRoms() no-op (ROM in flash)
//
// ROOT compilato da Arduino IDE. machines/pbaction/pbaction.cpp e' VUOTO.
// ============================================================================
#include "config.h"
#include "machines/pbaction/pbaction.h"

#ifdef ENABLE_PBACTION
#include "machines/pbaction/pbaction_logo.h"
#include "machines/pbaction/pbaction_rom_main.h"
#include "machines/pbaction/pbaction_rom_audio.h"
#include "machines/pbaction/pbaction_rom_fg.h"
#include "machines/pbaction/pbaction_rom_bg.h"
#include "machines/pbaction/pbaction_rom_sprites.h"

// Memory layout interno (RAMSIZE=11008, definito in machineBase.h)
#define MEM_WORK_BASE   0x0000   // 4 KB ($C000-$CFFF)
#define MEM_VR1_BASE    0x1000   // 1 KB FG videoram ($D000-$D3FF)
#define MEM_CR1_BASE    0x1400   // 1 KB FG colorram ($D400-$D7FF)
#define MEM_VR0_BASE    0x1800   // 1 KB BG videoram ($D800-$DBFF)
#define MEM_CR0_BASE    0x1C00   // 1 KB BG colorram ($DC00-$DFFF)
#define MEM_SPR_BASE    0x2000   // 128 B sprite RAM ($E000-$E07F)
#define MEM_PAL_BASE    0x2080   // 512 B palette RAM ($E400-$E5FF)
#define MEM_AUDIO_BASE  0x2280   // 2 KB audio CPU RAM ($4000-$47FF)

#define PB_FB_W   224     // Display dopo ROT90 CW SW (game 256x224 → display 224x256)
// Geometria nativa portrait (come 1942/pacman): 224 wide x 256 tall.
#define PB_NW     224
#define PB_NH     256

// ============================================================================
// Constructor / init / reset
// ============================================================================
pbaction::pbaction() {
  nmi_enable      = 0;
  flip_screen     = 0;
  scroll          = 0;
  buttons         = 0;
  soundlatch      = 0;
  ctc_vector      = 0xFF;
  ay_addr[0]      = 0;
  ay_addr[1]      = 0;
  ay_addr[2]      = 0;
  last_e604_write = 0;
  frame_count     = 0;
}

const unsigned short *pbaction::logo(void) { return pbaction_logo; }

void pbaction::init(Input *in, unsigned short *fb, sprite_S *sb, unsigned char *mem) {
  machineBase::init(in, fb, sb, mem);
}

void pbaction::reset() {
  machineBase::reset();
  nmi_enable      = 0;
  flip_screen     = 0;
  scroll          = 0;
  buttons         = 0;
  soundlatch      = 0;
  ctc_vector      = 0xFF;
  ay_addr[0]      = 0;
  ay_addr[1]      = 0;
  ay_addr[2]      = 0;
  last_e604_write = 0;
  frame_count     = 0;
  if (!pb_full)
    pb_full = (unsigned short *)ps_malloc(PB_NW * PB_NH * sizeof(unsigned short));
}

// ============================================================================
// Z80 memory map (MAME-faithful, ROM da PROGMEM)
// ============================================================================
unsigned char pbaction::rdZ80(unsigned short addr) {
  // Audio CPU (current_cpu==1)
  if (current_cpu == 1) {
    if (addr < 0x2000) return pgm_read_byte(&pbaction_rom_audio[addr]);
    if (addr >= 0x4000 && addr < 0x4800) return memory[MEM_AUDIO_BASE + (addr - 0x4000)];
    if (addr == 0x8000) return soundlatch;
    return 0xFF;
  }
  // Main CPU
  if (addr < 0xA000) return pgm_read_byte(&pbaction_rom_main[addr]);
  if (addr < 0xC000) return 0xFF;     // gap (40 KB ROM, mirror non implementato)
  if (addr < 0xD000) return memory[MEM_WORK_BASE + (addr - 0xC000)];
  if (addr < 0xD400) return memory[MEM_VR1_BASE  + (addr - 0xD000)];
  if (addr < 0xD800) return memory[MEM_CR1_BASE  + (addr - 0xD400)];
  if (addr < 0xDC00) return memory[MEM_VR0_BASE  + (addr - 0xD800)];
  if (addr < 0xE000) return memory[MEM_CR0_BASE  + (addr - 0xDC00)];
  if (addr < 0xE400) return memory[MEM_SPR_BASE  + (addr & 0x7F)];
  if (addr < 0xE600) return memory[MEM_PAL_BASE  + (addr - 0xE400)];

  // I/O reads
  if (addr == 0xE600) {
    // P1 inputs (active HIGH):
    //   bit 0x01 = BUTTON3 (plunger)  → BLE BUTTON_UP
    //   bit 0x08 = BUTTON1 (left flipper)  → BLE BUTTON_LEFT
    //   bit 0x10 = BUTTON2 (right flipper) → BLE BUTTON_RIGHT
    unsigned char v = 0x00;
    if (buttons & BUTTON_LEFT)  v |= 0x08;
    if (buttons & BUTTON_RIGHT) v |= 0x10;
    if (buttons & BUTTON_UP)    v |= 0x01;
    return v;
  }
  if (addr == 0xE601) return 0x00;     // P2 (cocktail) unused
  if (addr == 0xE602) {
    // SYSTEM (active HIGH): bit 0x01 = COIN1, bit 0x04 = START1
    unsigned char v = 0x00;
    if (buttons & BUTTON_COIN)  v |= 0x01;
    if (buttons & BUTTON_START) v |= 0x04;
    return v;
  }
  if (addr == 0xE604) return 0x40;     // DSW1: bit 0x40 = UPRIGHT cabinet
  if (addr == 0xE605) return 0x00;     // DSW2 default
  return 0xFF;
}

void pbaction::wrZ80(unsigned short addr, unsigned char val) {
  // Audio CPU
  if (current_cpu == 1) {
    if (addr >= 0x4000 && addr < 0x4800) { memory[MEM_AUDIO_BASE + (addr - 0x4000)] = val; return; }
    if (addr == 0xFFFF) { /* CTC IRQ ack */ return; }
    return;
  }
  // Main CPU
  if (addr < 0xC000) return;
  if (addr < 0xD000) { memory[MEM_WORK_BASE + (addr - 0xC000)] = val; return; }
  if (addr < 0xD400) { memory[MEM_VR1_BASE  + (addr - 0xD000)] = val; return; }
  if (addr < 0xD800) { memory[MEM_CR1_BASE  + (addr - 0xD400)] = val; return; }
  if (addr < 0xDC00) { memory[MEM_VR0_BASE  + (addr - 0xD800)] = val; return; }
  if (addr < 0xE000) { memory[MEM_CR0_BASE  + (addr - 0xDC00)] = val; return; }
  if (addr < 0xE400) { memory[MEM_SPR_BASE  + (addr & 0x7F)] = val; return; }
  if (addr < 0xE600) { memory[MEM_PAL_BASE  + (addr - 0xE400)] = val; return; }

  switch (addr) {
    case 0xE600: nmi_enable = val & 1; return;
    case 0xE604:
      last_e604_write = val;
      flip_screen = val & 0x01;
      return;
    case 0xE606: scroll = val; return;
    case 0xE800: {
      // Sound command: store soundlatch + simulate CTC ch0 IRQ → audio CPU IM2
      soundlatch = val;
      if (cpu[1].IFF & IFF_1) {
        signed char prev_cpu = current_cpu;
        current_cpu = 1;
        IntZ80(&cpu[1], ctc_vector);
        current_cpu = prev_cpu;
      }
      return;
    }
    default: break;
  }
}

unsigned char pbaction::opZ80(unsigned short addr) { return rdZ80(addr); }

// I/O ports (audio CPU only):
//   0x00-0x03: Z80 CTC (capture vector, IRQ STUB)
//   0x10-0x11: AY-3-8910 #1 (address / data)
//   0x20-0x21: AY-3-8910 #2
//   0x30-0x31: AY-3-8910 #3
unsigned char pbaction::inZ80(unsigned short port) { (void)port; return 0xFF; }

void pbaction::outZ80(unsigned short port, unsigned char val) {
  if (current_cpu != 1) return;
  int p = port & 0xFF;
  if (p <= 0x03) {
    // Z80 CTC: bit 0 = 0 → vector load (channel 0). Capture base vector.
    if (p == 0 && (val & 1) == 0) ctc_vector = val & 0xF8;
    return;
  }
  int chip = -1;
  if (p == 0x10 || p == 0x11) chip = 0;
  else if (p == 0x20 || p == 0x21) chip = 1;
  else if (p == 0x30 || p == 0x31) chip = 2;
  if (chip < 0) return;
  if ((p & 1) == 0) {
    ay_addr[chip] = val & 0x0F;
  } else {
    if (ay_addr[chip] < 16) soundregs[chip * 16 + ay_addr[chip]] = val;
  }
}

// ============================================================================
// run_frame — Z80 main 4 MHz + audio 3 MHz @ 60 Hz
//
// PBACTION_AUDIO_CPU_OFF: define per disabilitare temporaneamente l'audio Z80.
//   Skip ~5000 step/frame. Audio non genera niente finche' non riabilitato.
// PBACTION_PROFILE: log micros() ogni 60 frame.
//
// Stato 2026-05-10: audio CPU ABILITATO. run_frame ~9.3ms senza audio, atteso
// ~14-15ms con audio. Budget 16.6ms (60 Hz logic) → margine ~1-2ms.
// ============================================================================
//#define PBACTION_AUDIO_CPU_OFF
//#define PBACTION_PROFILE        // disabilitato su SP P4 (serial non disponibile)

void pbaction::run_frame(void) {
#ifdef PBACTION_PROFILE
  uint32_t t_start = micros();
#endif

  buttons = input ? input->buttons_get() : 0;
  game_started = 1;     // pbaction attract ha musica → no attract-mute

#ifndef PBACTION_AUDIO_CPU_OFF
  int audio_acc = 0;
  int ctc_div = 0;
#endif
  for (int i = 0; i < INST_PER_FRAME; i++) {
    current_cpu = 0;
    StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu); StepZ80(cpu);

#ifndef PBACTION_AUDIO_CPU_OFF
    // Rapporti REALI hardware (= PAOLO funzionante): main Z80 4MHz, audio 3MHz →
    // audio al 75% (3/4). I tentativi 2026-05-10 di "velocizzare" (100% + CTC 313)
    // erano un workaround per il render < 60fps; ora l'emulazione e' free-run a
    // 60Hz reali (freeRunEmulation), quindi i rapporti corretti danno il tempo
    // giusto come su PAOLO. Vedi feedback_no_debug_iterations.
    audio_acc += 6 * 3;     // 6 main steps × ratio 3/4
    int n_audio = audio_acc >> 2;
    audio_acc -= n_audio << 2;
    current_cpu = 1;
    for (int k = 0; k < n_audio; k++) StepZ80(&cpu[1]);
    current_cpu = 0;

    // CTC ch1 timer @ ~120Hz (625 iter × 60Hz / 1250 iter): valore di PAOLO.
    if (cpu[1].I == 0x01 && (cpu[1].IFF & IFF_1)
        && cpu[1].PC.W < 0x2000 && frame_count >= 60) {
      ctc_div++;
      if (ctc_div >= 625) {
        ctc_div = 0;
        current_cpu = 1;
        IntZ80(&cpu[1], (ctc_vector + 2) & 0xFF);
        current_cpu = 0;
      }
    }
#endif
  }

  // VBLANK NMI to main (gated by nmi_enable latched at $E600)
  if (nmi_enable) IntZ80(cpu, INT_NMI);

#ifdef PBACTION_PROFILE
  uint32_t dt = micros() - t_start;
  if ((frame_count % 60) == 0) {
    Serial.printf("[PBACTION] f=%u run=%luus mPC=%04X aPC=%04X aIFF=%02X aI=%02X ctcV=%02X latch=%02X\n",
                  frame_count, (unsigned long)dt,
                  cpu[0].PC.W, cpu[1].PC.W, cpu[1].IFF, cpu[1].I,
                  ctc_vector, soundlatch);
    Serial.printf("[PBACTION] AY0 R=%02X %02X %02X %02X %02X %02X %02X %02X V=%02X %02X %02X "
                  "AY1V=%02X %02X %02X AY2V=%02X %02X %02X\n",
                  soundregs[0],soundregs[1],soundregs[2],soundregs[3],
                  soundregs[4],soundregs[5],soundregs[6],soundregs[7],
                  soundregs[8],soundregs[9],soundregs[10],
                  soundregs[16+8],soundregs[16+9],soundregs[16+10],
                  soundregs[32+8],soundregs[32+9],soundregs[32+10]);
  }
#endif
  frame_count++;
}

// ============================================================================
// Palette LUT (cache 256 colori RGB565 BE, ricalcolato 1 volta/frame in
// prepare_frame). Sostituisce conversione xBGR_444→RGB565 fatta per pixel
// (era ~170K conversioni/frame × 60 fps = 10M conversioni/sec → bottleneck).
// Ora: 256 conversioni/frame × 60 = 15K conversioni/sec.
// ============================================================================
static unsigned short pbaction_pal_lut[256] = {0};

static inline unsigned short pbaction_pal_to_rgb565(uint8_t lo, uint8_t hi) {
  uint16_t val = lo | ((uint16_t)hi << 8);
  uint8_t b4 = (val >> 8) & 0x0F;
  uint8_t g4 = (val >> 4) & 0x0F;
  uint8_t r4 =  val       & 0x0F;
  uint16_t r5 = (r4 << 1) | (r4 >> 3);
  uint16_t g6 = (g4 << 2) | (g4 >> 2);
  uint16_t b5 = (b4 << 1) | (b4 >> 3);
  uint16_t rgb = (r5 << 11) | (g6 << 5) | b5;
  return (unsigned short)((rgb >> 8) | (rgb << 8));
}

// ============================================================================
// prepare_frame — parse spriteram (32 sprite × 4 byte = 128 byte)
// byte 0: bit 7 = "32x32 size" flag, bit 0..6 = sprite code (high bit included)
// byte 1: bit 7 flipY, bit 6 flipX, bit 0..3 color
// byte 2: Y position raw (sy = 241 - byte2 per 16x16, 225 - byte2 per 32x32)
// byte 3: X position raw (sx = byte3 - scroll, & 0xFF)
// 32x32 sprite occupies 2 spriteram slots — skip the second.
// ============================================================================
void pbaction::prepare_frame(void) {
  // Long-press reset timer driver
  buttons = input ? input->buttons_get() : 0;
  active_sprites = 0;

  // Pre-computa palette LUT 256 colori (1 volta/frame). Risparmio enorme
  // rispetto a fare la conversione xBGR_444→RGB565 per ogni pixel renderizzato.
  unsigned char *pal_ram = memory + MEM_PAL_BASE;
  for (int i = 0; i < 256; i++) {
    pbaction_pal_lut[i] = pbaction_pal_to_rgb565(pal_ram[i * 2], pal_ram[i * 2 + 1]);
  }

  unsigned char *sram = memory + MEM_SPR_BASE;

  for (int offs = 0; offs <= 124; offs += 4) {
    if (offs > 0 && (sram[offs - 4] & 0x80)) continue;

    uint8_t b0 = sram[offs + 0];
    uint8_t b1 = sram[offs + 1];
    uint8_t b2 = sram[offs + 2];
    uint8_t b3 = sram[offs + 3];

    bool size32 = (b0 & 0x80) != 0;
    int  code   = b0;
    int  color  = b1 & 0x0F;
    bool flipx  = (b1 & 0x40) != 0;
    bool flipy  = (b1 & 0x80) != 0;
    int  sx_src = (b3 - scroll) & 0xFF;
    int  sy_src = size32 ? (225 - b2) : (241 - b2);

    if (active_sprites < 64) {
      sprite_S &spr = sprite[active_sprites++];
      spr.code      = code;
      spr.color     = color;
      spr.is_32x32  = size32 ? 1 : 0;
      spr.flip_x    = flipx ? 1 : 0;
      spr.flip_y    = flipy ? 1 : 0;
      spr.x         = sx_src;     // MAME source X
      spr.y         = sy_src;     // MAME source Y
    }
  }

  // --- Render trasposto: disegna l'intero frame nativo 224x256 in pb_full ---
  if (pb_full) {
    for (short srow = 0; srow < PB_NH / 8; srow++) {   // 32 strip native
      rt = pb_full + srow * 8 * PB_NW;
      render_native_row(srow);
    }
  }
}

// Render trasposto 90: pb_full (224x256 portrait) -> frame_buffer landscape
// (256 wide). Stesso schema di 1942/pacman.
void pbaction::render_row(short strip_r) {
  if (!pb_full) return;
  for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
    int fb_y = strip_r * 8 + fb_y_strip;
    int gfb_x = (PB_NW - 1) - fb_y;
    if (gfb_x < 0 || gfb_x >= PB_NW) continue;
    unsigned short *dst = frame_buffer + fb_y_strip * PB_NH;
    const unsigned short *src = pb_full + gfb_x;
    for (int gfb_y = 0; gfb_y < PB_NH; gfb_y++)
      dst[gfb_y] = src[gfb_y * PB_NW];
  }
}

// ============================================================================
// render_row — pipeline MAME-faithful: bg → sprites → fg
// Cabinet ROT90 CW SW: (mame_x, mame_y) → display (dx, dy):
//   mame_x = dy, mame_y = 239 - dx
// ============================================================================
// Render NATIVO di una strip portrait (8 righe x 224) dentro rt (= pb_full+offset).
void pbaction::render_native_row(short row) {
  if (!rt) return;

  unsigned short *base = rt;
  // Clear strip via memset (piu' veloce di for-loop con scrittura halfword)
  memset(base, 0, PB_FB_W * 8 * sizeof(unsigned short));

  if (row >= 32) return;           // letterbox 32 px in fondo (288 - 256)

  constexpr int BG_PLANE  = 0x4000;
  constexpr int FG_PLANE  = 0x2000;
  constexpr int SPR_PLANE = 0x2000;

  const bool fs = flip_screen != 0;
  unsigned char *vr0 = memory + MEM_VR0_BASE;
  unsigned char *cr0 = memory + MEM_CR0_BASE;
  unsigned char *vr1 = memory + MEM_VR1_BASE;
  unsigned char *cr1 = memory + MEM_CR1_BASE;
  // pal LUT pre-computata in prepare_frame: pbaction_pal_lut[256]

  // ── PASS 1: BG layer (4bpp, 32×32 tilemap, palette base 128) ──
  // Tile cache: per ogni dy_local fisso, sub_x/gx_sh/mbg sono fissi. Iteriamo
  // 28 tile orizzontali; per ogni tile pre-carichiamo gli 8 byte plane × 4
  // (32 byte sequenziali per cache flash hit) invece di leggerli per pixel.
  for (int dy_local = 0; dy_local < 8; dy_local++) {
    int dy = (int)row * 8 + dy_local;
    if (dy >= 256) continue;

    int mame_x = fs ? (255 - dy) : dy;
    int tile_x = mame_x >> 3;
    int sub_x  = mame_x & 7;
    int gx_sh = fs ? (7 - sub_x) : sub_x;
    int mbg   = 7 - gx_sh;

    unsigned short *dst = base + dy_local * PB_FB_W;

    // Itera 28 tile orizzontali
    for (int t = 0; t < 28; t++) {
      // tile_y derivato da gruppo di 8 dx contigui:
      //   !fs: dx in [t*8 .. t*8+7] → mame_y in [239-t*8-7 .. 239-t*8] → tile_y = 29-t
      //   fs:  dx in [t*8 .. t*8+7] → mame_y in [16+t*8 .. 16+t*8+7]   → tile_y = 2+t
      int tile_y = fs ? (2 + t) : (29 - t);
      int tile_idx = tile_y * 32 + tile_x;
      uint8_t v = vr0[tile_idx];
      uint8_t a = cr0[tile_idx];
      int code   = v + 0x10 * (a & 0x70);
      int color  = a & 0x07;
      bool tflipy = (a & 0x80) != 0;

      // Pre-carica gli 8 byte plane × 4 plane per il tile (32 byte sequenziali)
      int byte_base = code * 8;
      uint8_t pb[4][8];
      for (int g = 0; g < 8; g++) {
        pb[0][g] = pgm_read_byte(&pbaction_rom_bg[byte_base + g + 0 * BG_PLANE]);
        pb[1][g] = pgm_read_byte(&pbaction_rom_bg[byte_base + g + 1 * BG_PLANE]);
        pb[2][g] = pgm_read_byte(&pbaction_rom_bg[byte_base + g + 2 * BG_PLANE]);
        pb[3][g] = pgm_read_byte(&pbaction_rom_bg[byte_base + g + 3 * BG_PLANE]);
      }
      int pal_base = 128 + color * 16;
      bool flip_v = tflipy ^ fs;

      // Loop pixel: 8 dx all'interno del tile.
      // sub_y in funzione di i: !fs → 7-i, fs → i.
      //   gy = flip_v ? (7-sub_y) : sub_y
      //   !fs flip_v=tflipy: gy = tflipy ? i : (7-i)
      //   fs:  flip_v=!tflipy: gy = !tflipy ? (7-i) : i
      for (int i = 0; i < 8; i++) {
        int dx = t * 8 + i;
        if (dx >= PB_FB_W) break;
        int sub_y = fs ? i : (7 - i);
        int gy = flip_v ? (7 - sub_y) : sub_y;
        int p = ((pb[3][gy] >> mbg) & 1)
              | (((pb[2][gy] >> mbg) & 1) << 1)
              | (((pb[1][gy] >> mbg) & 1) << 2)
              | (((pb[0][gy] >> mbg) & 1) << 3);
        dst[dx] = pbaction_pal_lut[(pal_base + p) & 0xFF];
      }
    }
  }

  // ── PASS 2: Sprites (16x16 layout1 + 32x32 layout2, 3bpp, base 0) ──
  int band_y_min = (int)row * 8;
  int band_y_max = band_y_min + 7;

  for (int s = 0; s < active_sprites; s++) {
    sprite_S &spr = sprite[s];
    int size = spr.is_32x32 ? 32 : 16;

    int spr_y_min = spr.x;
    int spr_y_max = spr.x + size - 1;
    if (spr_y_max < band_y_min || spr_y_min > band_y_max) continue;

    int spr_x_min = 239 - spr.y - (size - 1);
    int spr_x_max = 239 - spr.y;
    if (spr_x_max < 0 || spr_x_min >= PB_FB_W) continue;

    int dy_lo = spr_y_min < band_y_min ? band_y_min : spr_y_min;
    int dy_hi = spr_y_max > band_y_max ? band_y_max : spr_y_max;
    int dx_lo = spr_x_min < 0 ? 0 : spr_x_min;
    int dx_hi = spr_x_max >= PB_FB_W ? (PB_FB_W - 1) : spr_x_max;

    for (int dy = dy_lo; dy <= dy_hi; dy++) {
      int dy_local = dy - band_y_min;
      int sprite_local_x = dy - spr.x;
      unsigned short *dst = base + dy_local * PB_FB_W;

      for (int dx = dx_lo; dx <= dx_hi; dx++) {
        int sprite_local_y = (239 - spr.y) - dx;

        int rx = spr.flip_x ? (size - 1 - sprite_local_x) : sprite_local_x;
        int ry = spr.flip_y ? (size - 1 - sprite_local_y) : sprite_local_y;

        int bit_pos;
        int sprite_byte_base;
        if (size == 16) {
          int y_off = (ry & 7) * 8 + (ry >> 3) * 128;
          int x_off = (rx & 7) + (rx >> 3) * 64;
          bit_pos = y_off + x_off;
          sprite_byte_base = (spr.code & 0x7F) * 32;
        } else {
          int y_off = (ry & 7) * 8 + ((ry >> 3) & 1) * 128 + ((ry >> 4) & 1) * 512;
          int x_off = (rx & 7) + ((rx >> 3) & 1) * 64 + ((rx >> 4) & 1) * 256;
          bit_pos = y_off + x_off;
          sprite_byte_base = 0x1000 + (spr.code & 0x1F) * 128;
        }
        int byte_in_plane = sprite_byte_base + (bit_pos >> 3);
        int bit_in_byte   = bit_pos & 7;

        uint8_t s0 = pgm_read_byte(&pbaction_rom_sprites[byte_in_plane + 0 * SPR_PLANE]);
        uint8_t s1 = pgm_read_byte(&pbaction_rom_sprites[byte_in_plane + 1 * SPR_PLANE]);
        uint8_t s2 = pgm_read_byte(&pbaction_rom_sprites[byte_in_plane + 2 * SPR_PLANE]);
        int mb = 7 - bit_in_byte;
        int sp = ((s2 >> mb) & 1)
               | (((s1 >> mb) & 1) << 1)
               | (((s0 >> mb) & 1) << 2);
        if (sp == 0) continue;
        int spal = spr.color * 8 + sp;
        dst[dx] = pbaction_pal_lut[spal & 0xFF];
      }
    }
  }

  // ── PASS 3: FG layer overlay (3bpp, palette base 0, transparent on 0) ──
  // Tile cache pattern stesso del BG ma con 3 plane invece di 4 e flip_x
  // gestito a livello tile (FFX puo' variare per tile, fsx non e' fisso a riga).
  for (int dy_local = 0; dy_local < 8; dy_local++) {
    int dy = (int)row * 8 + dy_local;
    if (dy >= 256) continue;

    int mame_x = fs ? (255 - dy) : dy;
    int tile_x = mame_x >> 3;
    int sub_x  = mame_x & 7;

    unsigned short *dst = base + dy_local * PB_FB_W;

    for (int t = 0; t < 28; t++) {
      int tile_y = fs ? (2 + t) : (29 - t);
      int tile_idx = tile_y * 32 + tile_x;
      uint8_t fv = vr1[tile_idx];
      uint8_t fa = cr1[tile_idx];
      int fcode  = fv + 0x10 * (fa & 0x30);
      int fcolor = fa & 0x0F;
      bool ffy = (fa & 0x80) != 0;
      bool ffx = (fa & 0x40) != 0;
      int fsx = (ffx ^ fs) ? sub_x : (7 - sub_x);

      int fbyte_base = fcode * 8;
      uint8_t fp_byte[3][8];
      for (int g = 0; g < 8; g++) {
        fp_byte[0][g] = pgm_read_byte(&pbaction_rom_fg[fbyte_base + g + 0 * FG_PLANE]);
        fp_byte[1][g] = pgm_read_byte(&pbaction_rom_fg[fbyte_base + g + 1 * FG_PLANE]);
        fp_byte[2][g] = pgm_read_byte(&pbaction_rom_fg[fbyte_base + g + 2 * FG_PLANE]);
      }
      int fpal_base = fcolor * 8;
      bool flip_v = ffy ^ fs;

      for (int i = 0; i < 8; i++) {
        int dx = t * 8 + i;
        if (dx >= PB_FB_W) break;
        int sub_y = fs ? i : (7 - i);
        int fgy = flip_v ? (7 - sub_y) : sub_y;
        int fp = ((fp_byte[2][fgy] >> fsx) & 1)
               | (((fp_byte[1][fgy] >> fsx) & 1) << 1)
               | (((fp_byte[0][fgy] >> fsx) & 1) << 2);
        if (fp != 0) {
          dst[dx] = pbaction_pal_lut[(fpal_base + fp) & 0xFF];
        }
      }
    }
  }
}

#endif // ENABLE_PBACTION
