// ============================================================================
// dkong3.cpp — Donkey Kong 3 (Nintendo 1983), porting -> SPINNERINO WIFI.
// Z80 main + 2x RP2A03 (NES APU). Video TRASPOSTO (dkongjr); APU/memory-map da
// galagino dkong3.cpp. snd_ram + dk3_samples in PSRAM (build DRAM stretta).
// ============================================================================
#include "config.h"
#include "machines/dkong3/dkong3.h"

#ifdef ENABLE_DKONG3
#include "emulation/input.h"

// ---------------------------------------------------------------------------
// Tabelle APU NES
// ---------------------------------------------------------------------------
static const uint8_t DK3_LENGTH_TABLE[32] = {
  10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
  12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
};
static const uint8_t DK3_DUTY[4][8] = {
  {0,1,0,0,0,0,0,0},{0,1,1,0,0,0,0,0},{0,1,1,1,1,0,0,0},{1,0,0,1,1,1,1,1}
};
static const uint8_t DK3_TRI_SEQ[32] = {
  15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const uint16_t DK3_NOISE_PERIOD[16] = {
  4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
};

// Mixer NON-LINEARE NES (come MAME/NESdev): due LUT pre-calcolate. Evita il
// clipping della somma lineare (= distorsione). pulse_out + tnd_out, 0..~0.76.
// Scalato SCALE per restare in interi. Vedi https://www.nesdev.org/wiki/APU_Mixer
// NB: provato ±512 (zona lineare valueToBuffer) per il clipping -> NON ha risolto
// la distorsione dei jingle (difetto indipendente dal livello = nell'APU, non
// clipping output). Ripristinato 2048. Vedi memoria per la prossima sessione.
#define DK3_LUT_SCALE 2048
static int  dk3_pulse_lut[31];
static int  dk3_tnd_lut[203];
static bool dk3_lut_ready = false;
static void dk3_init_lut(void) {
  if (dk3_lut_ready) return;
  dk3_pulse_lut[0] = 0;
  for (int n = 1; n < 31; n++)
    dk3_pulse_lut[n] = (int)(95.52 / (8128.0 / n + 100.0) * DK3_LUT_SCALE + 0.5);
  dk3_tnd_lut[0] = 0;
  for (int n = 1; n < 203; n++)
    dk3_tnd_lut[n] = (int)(163.67 / (24329.0 / n + 100.0) * DK3_LUT_SCALE + 0.5);
  dk3_lut_ready = true;
}

// Avanza i canali e ritorna l'output (UNIPOLARE) di una APU via mixer non-lineare.
static int dk3_apu_sample(dk3_apu_t *a, int ticks) {
  int p_out[2] = {0, 0};
  for (int ch = 0; ch < 2; ch++) {
    uint16_t cur_per = a->p_timer[ch] & 0x7FF;
    int period = (int)(cur_per + 1) * 2;
    if (period >= 4) {
      a->p_cnt[ch] -= ticks;
      while (a->p_cnt[ch] <= 0) { a->p_cnt[ch] += period; a->p_seq[ch] = (a->p_seq[ch] + 1) & 7; }
    }
    if (a->p_en[ch] && a->p_len[ch] && cur_per >= 8) {
      bool muted = false;
      uint8_t sw_shift = a->p_sweep[ch] & 7;
      if (sw_shift && !((a->p_sweep[ch] >> 3) & 1) && (cur_per + (cur_per >> sw_shift) > 0x7FF)) muted = true;
      if (!muted) {
        uint8_t vol = (a->p_ctrl[ch] & 0x10) ? (a->p_ctrl[ch] & 0x0F) : a->p_env_vol[ch];
        uint8_t duty = (a->p_ctrl[ch] >> 6) & 3;
        if (DK3_DUTY[duty][a->p_seq[ch]]) p_out[ch] = vol;   // livello 0..15
      }
    }
  }
  int tri_out = 0;
  // Il sequencer del triangolo avanza SOLO con len & lin attivi (come NES/galagino);
  // altrimenti si ferma sul valore corrente.
  if (a->t_en && a->t_len && a->t_lin) {
    int period = (int)(a->t_timer & 0x7FF) + 1;
    if (period >= 2) {
      a->t_cnt -= ticks;
      while (a->t_cnt <= 0) { a->t_cnt += period; a->t_seq = (a->t_seq + 1) & 31; }
    }
    tri_out = DK3_TRI_SEQ[a->t_seq];   // 0..15
  }
  int noise_out = 0;
  {
    int period = DK3_NOISE_PERIOD[a->n_period_idx & 0x0F];
    a->n_cnt -= ticks;
    while (a->n_cnt <= 0) {
      a->n_cnt += period;
      uint16_t bit = (a->n_lfsr & 1) ^ ((a->n_mode ? (a->n_lfsr >> 6) : (a->n_lfsr >> 1)) & 1);
      a->n_lfsr = (a->n_lfsr >> 1) | (bit << 14);
    }
    if (a->n_en && a->n_len && !(a->n_lfsr & 1)) {   // NES: silenziato quando bit0 LFSR = 1
      uint8_t nvol = (a->n_ctrl & 0x10) ? (a->n_ctrl & 0x0F) : a->n_env_vol;
      noise_out = nvol;   // 0..15
    }
  }
  return dk3_pulse_lut[p_out[0] + p_out[1]] + dk3_tnd_lut[3 * tri_out + 2 * noise_out];
}

// ---------------------------------------------------------------------------
// Callback memoria RP2A03 #0 / #1 (registri APU -> apu[0]/apu[1])
// ---------------------------------------------------------------------------
static void dk3_apu_write(dk3_apu_t &a, uint16_t addr, uint8_t val) {
  switch (addr) {
    case 0x4000: a.p_ctrl[0] = val; break;
    case 0x4001: a.p_sweep[0] = val; a.p_sweep_rst[0] = true; break;
    case 0x4002: a.p_timer[0] = (a.p_timer[0] & 0x700) | val; break;
    case 0x4003: a.p_timer[0] = (a.p_timer[0] & 0x0FF) | ((val & 7) << 8);
                 a.p_seq[0] = 0; a.p_cnt[0] = 0; a.p_env_rst[0] = true;
                 if (a.p_en[0]) a.p_len[0] = DK3_LENGTH_TABLE[val >> 3]; break;
    case 0x4004: a.p_ctrl[1] = val; break;
    case 0x4005: a.p_sweep[1] = val; a.p_sweep_rst[1] = true; break;
    case 0x4006: a.p_timer[1] = (a.p_timer[1] & 0x700) | val; break;
    case 0x4007: a.p_timer[1] = (a.p_timer[1] & 0x0FF) | ((val & 7) << 8);
                 a.p_seq[1] = 0; a.p_cnt[1] = 0; a.p_env_rst[1] = true;
                 if (a.p_en[1]) a.p_len[1] = DK3_LENGTH_TABLE[val >> 3]; break;
    case 0x4008: a.t_lin_ctrl = val; break;
    case 0x400A: a.t_timer = (a.t_timer & 0x700) | val; break;
    case 0x400B: a.t_timer = (a.t_timer & 0x0FF) | ((val & 7) << 8);
                 a.t_cnt = 0; a.t_lin_reload = true;
                 if (a.t_en) a.t_len = DK3_LENGTH_TABLE[val >> 3]; break;
    case 0x400C: a.n_ctrl = val; break;
    case 0x400E: a.n_period_idx = val & 0x0F; a.n_mode = (val >> 7) & 1; break;
    case 0x400F: a.n_env_rst = true;
                 if (a.n_en) a.n_len = DK3_LENGTH_TABLE[val >> 3]; break;
    case 0x4015:
      if (!((val >> 0) & 1)) a.p_len[0] = 0;
      if (!((val >> 1) & 1)) a.p_len[1] = 0;
      if (!((val >> 2) & 1)) a.t_len = 0;
      if (!((val >> 3) & 1)) a.n_len = 0;
      a.p_en[0] = (val >> 0) & 1; a.p_en[1] = (val >> 1) & 1;
      a.t_en = (val >> 2) & 1;    a.n_en = (val >> 3) & 1;
      break;
    default: break;
  }
}

static uint8_t dk3_apu_status(dk3_apu_t &a) {
  return (a.p_len[0] ? 0x01 : 0) | (a.p_len[1] ? 0x02 : 0) |
         (a.t_len ? 0x04 : 0) | (a.n_len ? 0x08 : 0);
}

uint8_t dkong3::snd0_read(m6502_t *cpu, uint16_t addr) {
  if (addr >= 0x8000) return dkong3_rom_sound_a[addr & 0x1FFF];
  dkong3 *s = (dkong3*)cpu->user;
  if (addr < 0x0800) return s->snd_ram[0][addr & 0x07FF];
  if (addr >= 0x4000 && addr <= 0x4013) return s->apu_shadow[0][addr - 0x4000]; // readback ($4xxx ~ RAM)
  if (addr == 0x4016) return s->sound_latch[0];
  if (addr == 0x4017) return s->sound_latch[1];
  if (addr == 0x4015) return dk3_apu_status(s->apu[0]);
  return 0;
}
void dkong3::snd0_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  dkong3 *s = (dkong3*)cpu->user;
  if (addr < 0x0800) { s->snd_ram[0][addr] = val; return; }
  if (addr >= 0x4000 && addr <= 0x4013) s->apu_shadow[0][addr - 0x4000] = val;  // readback
  dk3_apu_write(s->apu[0], addr, val);
}
uint8_t dkong3::snd1_read(m6502_t *cpu, uint16_t addr) {
  if (addr >= 0x8000) return dkong3_rom_sound_b[addr & 0x1FFF];
  dkong3 *s = (dkong3*)cpu->user;
  if (addr < 0x0800) return s->snd_ram[1][addr & 0x07FF];
  if (addr >= 0x4000 && addr <= 0x4013) return s->apu_shadow[1][addr - 0x4000]; // readback ($4xxx ~ RAM)
  if (addr == 0x4016) return s->sound_latch[2];
  if (addr == 0x4017) return 0xFF;
  if (addr == 0x4015) return dk3_apu_status(s->apu[1]);
  return 0;
}
void dkong3::snd1_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  dkong3 *s = (dkong3*)cpu->user;
  if (addr < 0x0800) { s->snd_ram[1][addr] = val; return; }
  if (addr >= 0x4000 && addr <= 0x4013) s->apu_shadow[1][addr - 0x4000] = val;  // readback
  dk3_apu_write(s->apu[1], addr, val);
}

int dkong3::audioPop(int16_t *out, int n) {
  int got = 0;
  while (got < n && dk3_rptr != dk3_wptr) {
    out[got++] = dk3_samples[dk3_rptr];
    dk3_rptr = (dk3_rptr + 1) & (DK3_SAMPLES - 1);
  }
  return got;
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
void dkong3::reset() {
  machineBase::reset();
  dk3_init_lut();   // LUT mixer non-lineare NES (una volta)
  general_ram = memory + 0x0000;
  sprite_ram  = memory + 0x1000;
  video_ram   = memory + 0x1400;
  memset(memory, 0, 0x1800);

  palette_bank = 0; gfx_bank = 0; flip_screen = 0;
  sound_latch[0] = sound_latch[1] = sound_latch[2] = 0;
  coin_latch = 0; coin_prev = false;

  // Alloc lazy in PSRAM (build DRAM stretta).
  if (!dk3_full)     dk3_full     = (unsigned short*)ps_malloc(DK3_NW * DK3_NH * sizeof(unsigned short));
  if (!snd_ram[0])   snd_ram[0]   = (unsigned char*) ps_malloc(2048);
  if (!snd_ram[1])   snd_ram[1]   = (unsigned char*) ps_malloc(2048);
  if (!dk3_samples)  dk3_samples  = (short*)         ps_malloc(DK3_SAMPLES * sizeof(short));
  if (snd_ram[0])  memset(snd_ram[0], 0, 2048);
  if (snd_ram[1])  memset(snd_ram[1], 0, 2048);
  if (dk3_samples) memset(dk3_samples, 0, DK3_SAMPLES * sizeof(short));
  dk3_rptr = dk3_wptr = 0;

  memset(&apu[0], 0, sizeof(dk3_apu_t)); apu[0].n_lfsr = 1;
  memset(&apu[1], 0, sizeof(dk3_apu_t)); apu[1].n_lfsr = 1;
  memset(apu_shadow, 0, sizeof(apu_shadow));   // readback registri APU

  memset(&snd_cpu[0], 0, sizeof(m6502_t));
  snd_cpu[0].read = snd0_read; snd_cpu[0].write = snd0_write; snd_cpu[0].user = this;
  m6502_reset(&snd_cpu[0]);
  memset(&snd_cpu[1], 0, sizeof(m6502_t));
  snd_cpu[1].read = snd1_read; snd_cpu[1].write = snd1_write; snd_cpu[1].user = this;
  m6502_reset(&snd_cpu[1]);
}

// ---------------------------------------------------------------------------
// Z80 main CPU
// ---------------------------------------------------------------------------
unsigned char dkong3::opZ80(unsigned short Addr) {
  return dkong3_rom_cpu[Addr];
}

unsigned char dkong3::rdZ80(unsigned short Addr) {
  if (Addr < 0x6000)                    return dkong3_rom_cpu[Addr];
  if (Addr >= 0x8000 && Addr <= 0x9FFF) return dkong3_rom_cpu[Addr];
  if (Addr >= 0x6000 && Addr <= 0x6FFF) return general_ram[Addr - 0x6000];
  if (Addr >= 0x7000 && Addr <= 0x73FF) return sprite_ram[Addr - 0x7000];
  if (Addr >= 0x7400 && Addr <= 0x77FF) return video_ram[Addr - 0x7400];

  if (Addr >= 0x7C00 && Addr <= 0x7DFF) {
    unsigned char keymask = input->buttons_get();
    unsigned char retval  = 0x00;
    switch (Addr) {
      case 0x7C00:
        if (keymask & BUTTON_RIGHT) retval |= 0x01;
        if (keymask & BUTTON_LEFT)  retval |= 0x02;
        if (keymask & BUTTON_UP)    retval |= 0x04;
        if (keymask & BUTTON_DOWN)  retval |= 0x08;
        if (keymask & BUTTON_FIRE)  retval |= 0x10;
        if (keymask & BUTTON_START) retval |= 0x20;
        return retval;
      case 0x7C80: {
        bool coin_now = (keymask & BUTTON_COIN) != 0;
        if (coin_now && !coin_prev) coin_latch = 20;
        coin_prev = coin_now;
        retval = (coin_latch > 0) ? 0x20 : 0;
        if (coin_latch) coin_latch--;
        return retval;
      }
      case 0x7D00: game_started = 1; return DK3_DSW2;
      case 0x7D80: return DK3_DSW1;
    }
  }
  return 0xFF;
}

void dkong3::wrZ80(unsigned short Addr, unsigned char Value) {
  if (Addr >= 0x6000 && Addr <= 0x6FFF) { general_ram[Addr - 0x6000] = Value; return; }
  if (Addr >= 0x7000 && Addr <= 0x73FF) { sprite_ram [Addr - 0x7000] = Value; return; }
  if (Addr >= 0x7400 && Addr <= 0x77FF) { video_ram  [Addr - 0x7400] = Value; return; }

  if (Addr == 0x7C00) { sound_latch[0] = Value; return; }
  if (Addr == 0x7C80) { sound_latch[1] = Value; return; }
  if (Addr == 0x7D00) { sound_latch[2] = Value; return; }
  if (Addr == 0x7D80) { return; }

  if (Addr >= 0x7E80 && Addr <= 0x7E87) {
    switch (Addr) {
      case 0x7E81: gfx_bank     = (~Value) & 0x01; break;
      case 0x7E82: flip_screen  = Value & 1;        break;
      case 0x7E84: irq_enable[0] = Value & 1;       break;
      case 0x7E85: if (Value & 1) memcpy(sprite_ram, general_ram + 0x0900, 512); break;
      case 0x7E86: palette_bank = (Value & 1) ? (palette_bank | 1) : (palette_bank & ~1); break;
      case 0x7E87: palette_bank = (Value & 1) ? (palette_bank | 2) : (palette_bank & ~2); break;
    }
  }
}

// ---------------------------------------------------------------------------
// Frame: Z80 + NMI + APU NES (2x RP2A03) + sintesi 400 sample/frame @24kHz
// ---------------------------------------------------------------------------
void dkong3::run_frame(void) {
  current_cpu = 0;
  for (int i = 0; i < 1666; i++) {
    StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
  }
  if (irq_enable[0]) IntZ80(&cpu[0], INT_NMI);

  // Frame sequencer NES (4 step: envelope/lin 240Hz, length/sweep 120Hz step 1&3)
  for (int step = 0; step < 4; step++) {
    bool clock_length = (step == 1 || step == 3);
    for (int ai = 0; ai < 2; ai++) {
      dk3_apu_t &a = apu[ai];
      for (int ch = 0; ch < 2; ch++) {
        if (a.p_env_rst[ch]) { a.p_env_rst[ch] = false; a.p_env_vol[ch] = 15; a.p_env_cnt[ch] = a.p_ctrl[ch] & 0x0F; }
        else if (!(a.p_ctrl[ch] & 0x10)) {
          uint8_t per = a.p_ctrl[ch] & 0x0F;
          if (a.p_env_cnt[ch] > 0) a.p_env_cnt[ch]--;
          else { a.p_env_cnt[ch] = per; if (a.p_env_vol[ch] > 0) a.p_env_vol[ch]--; else if (a.p_ctrl[ch] & 0x20) a.p_env_vol[ch] = 15; }
        }
      }
      if (a.n_env_rst) { a.n_env_rst = false; a.n_env_vol = 15; a.n_env_cnt = a.n_ctrl & 0x0F; }
      else if (!(a.n_ctrl & 0x10)) {
        uint8_t per = a.n_ctrl & 0x0F;
        if (a.n_env_cnt > 0) a.n_env_cnt--;
        else { a.n_env_cnt = per; if (a.n_env_vol > 0) a.n_env_vol--; else if (a.n_ctrl & 0x20) a.n_env_vol = 15; }
      }
      if (a.t_lin_reload) a.t_lin = a.t_lin_ctrl & 0x7F;
      else if (a.t_lin > 0) a.t_lin--;
      if (!(a.t_lin_ctrl & 0x80)) a.t_lin_reload = false;

      if (clock_length) {
        for (int ch = 0; ch < 2; ch++) {
          if (!(a.p_ctrl[ch] & 0x20) && a.p_len[ch] > 0) a.p_len[ch]--;
          uint8_t sw = a.p_sweep[ch]; uint8_t sw_per = (sw >> 4) & 7;
          if (a.p_sweep_rst[ch]) { a.p_sweep_cnt[ch] = sw_per; a.p_sweep_rst[ch] = false; }
          else if (a.p_sweep_cnt[ch] > 0) a.p_sweep_cnt[ch]--;
          else {
            a.p_sweep_cnt[ch] = sw_per;
            bool sw_en = (sw >> 7) & 1; bool sw_neg = (sw >> 3) & 1; uint8_t sw_sh = sw & 7;
            uint16_t cur = a.p_timer[ch] & 0x7FF;
            if (sw_en && sw_sh > 0 && cur >= 8) {
              uint16_t delta = cur >> sw_sh;
              uint16_t target = sw_neg ? cur - delta - (ch == 0 ? 1 : 0) : cur + delta;
              if (target <= 0x7FF) a.p_timer[ch] = target;
            }
          }
        }
        if (!(a.t_lin_ctrl & 0x80) && a.t_len > 0) a.t_len--;
        if (!(a.n_ctrl & 0x20) && a.n_len > 0) a.n_len--;
      }
    }
  }

  snd_cpu[0].nmi = 1; snd_cpu[1].nmi = 1;
  m6502_exec(&snd_cpu[0], 12000);
  m6502_exec(&snd_cpu[1], 12000);

  static const int SAMPLES_PER_FRAME = 400;
  static const int CPU_TICKS = 75;
  static const int OS = 4;   // oversampling (anti-alias, vedi sotto)
  static int dk3_dc  = 0;    // stato DC-blocker (AC coupling)
  static int dk3_lpf = 0;    // stato LPF anti-alias
  for (int i = 0; i < SAMPLES_PER_FRAME; i++) {
    uint16_t next = (dk3_wptr + 1) & (DK3_SAMPLES - 1);
    if (next == dk3_rptr) break;
    // 4x OVERSAMPLING (box filter): i 75 cicli/sample sono ripartiti in 4
    // sotto-step e si media il mixer. MAME sovracampiona alla freq CPU piena
    // per evitare l'aliasing delle onde quadre/noise ad alta freq (es. jingle di
    // morte). 4x e' un compromesso CPU/qualita' molto migliore del campionamento
    // puntuale. Mixer NON-LINEARE NES (unipolare) sommato sulle due RP2A03.
    int acc = 0;
    for (int k = 0; k < OS; k++) {
      int t = (CPU_TICKS * (k + 1)) / OS - (CPU_TICKS * k) / OS;   // somma = 75
      acc += dk3_apu_sample(&apu[0], t) + dk3_apu_sample(&apu[1], t);
    }
    int m = acc / OS;
    // DC-blocker (high-pass): rimuove l'offset -> resta la forma d'onda AC.
    int ac = m - dk3_dc;
    dk3_dc += ac >> 7;
    // LPF leggero residuo.
    dk3_lpf = (ac * 3 + dk3_lpf) >> 2;
    int val = dk3_lpf;
    if (val >  2000) val =  2000;
    if (val < -2000) val = -2000;
    dk3_samples[dk3_wptr] = (short)val;
    dk3_wptr = next;
  }
}

// ---------------------------------------------------------------------------
// Video — render TRASPOSTO (come dkongjr)
// ---------------------------------------------------------------------------
void dkong3::prepare_frame(void) {
  if (input) input->buttons_get();

  active_sprites = 0;
  for (int idx = 0; idx < 92 && active_sprites < 92; idx++) {
    unsigned char *b = sprite_ram + 4 * idx;
    if (b[0] == 0) continue;
    struct sprite_S spr;
    spr.x = 224 + 8 - b[0];
    spr.y = 224 - 24 - b[3];
    unsigned char attr = b[2];
    spr.code  = (b[1] & 0x7F) | ((attr & 0x40) << 1);
    spr.color = attr & 0x3F;
    spr.flags = ((b[1] & 0x80) ? 1 : 0) | ((attr & 0x80) ? 2 : 0);
    if (flip_screen) { spr.x = 224 - 16 - spr.x; spr.y = 224 - 16 - spr.y; spr.flags ^= 1; }
    if (spr.y > -16 && spr.y < 288 && spr.x > -16 && spr.x < 224)
      sprite[active_sprites++] = spr;
  }

  if (dk3_full) {
    memset(dk3_full, 0, DK3_NW * DK3_NH * sizeof(unsigned short));
    for (short srow = 0; srow < DK3_NH / 8; srow++) {
      rt = dk3_full + srow * 8 * DK3_NW;
      render_strip(srow);
    }
  }
}

void dkong3::render_strip(short srow) {
  for (char col = 0; col < 28; col++)
    blit_tile(srow, col);
  for (unsigned char s = 0; s < active_sprites; s++)
    if ((sprite[s].y < 8 * (srow + 1)) && ((sprite[s].y + 16) > 8 * srow))
      blit_sprite(srow, s);
}

void dkong3::blit_tile(short row, char col) {
  if (row < 2 || row >= 34) return;
  unsigned short addr = tileaddr[row][col];
  if (video_ram[addr] == 0x10) return;

  unsigned char  base_code  = video_ram[addr];
  unsigned short final_code = base_code + (256 * gfx_bank);
  const unsigned short *tile = dkong3_tilemap[final_code];

  unsigned short color_idx = (addr % 32) + 32 * ((addr / 32) / 4);
  unsigned char  base_pal  = pgm_read_byte(&color_codes_prom[color_idx]) & 0x0F;
  unsigned short final_pal  = base_pal + (palette_bank * 16);
  const unsigned short *colors = dkong3_colormap_sprite[final_pal];

  unsigned short *ptr = rt + 8 * col;
  for (char r = 0; r < 8; r++, ptr += (DK3_NW - 8)) {
    unsigned short pix = *tile++;
    for (char c = 0; c < 8; c++, pix >>= 2)
      *ptr++ = colors[pix & 3];
  }
}

void dkong3::blit_sprite(short row, unsigned char s) {
  const unsigned long  *spr    = dkong3_sprites[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = dkong3_colormap_sprite[sprite[s].color];

  unsigned long mask = 0xFFFFFFFF;
  if (sprite[s].x < 0)        mask <<= (-sprite[s].x * 2);
  if (sprite[s].x > 224 - 16) mask >>= ((sprite[s].x - (224 - 16)) * 2);

  short y_off = sprite[s].y - 8 * row;
  unsigned char lines = 8;
  unsigned short startline = 0;
  if (y_off < -8) lines = 16 + y_off;
  if (y_off > 0)  { startline = y_off; lines = 8 - y_off; }
  if (y_off < 0)  spr += (-y_off);

  unsigned short *ptr = rt + sprite[s].x + DK3_NW * startline;
  for (char r = 0; r < lines; r++, ptr += (DK3_NW - 16)) {
    unsigned long pix = *spr++ & mask;
    for (char c = 0; c < 16; c++, pix >>= 2) {
      if (pix & 3) *ptr = colors[pix & 3];
      ptr++;
    }
  }
}

void dkong3::render_row(short strip_r) {
  if (!dk3_full) return;
  for (int fy = 0; fy < 8; fy++) {
    int fb_y = strip_r * 8 + fy;
    int col  = (DK3_NW - 1) - fb_y;
    if (col < 0 || col >= DK3_NW) continue;
    unsigned short *dst       = frame_buffer + fy * DK3_NH;
    const unsigned short *src = dk3_full + col;
    for (int y = 0; y < DK3_NH; y++)
      dst[y] = src[y * DK3_NW];
  }
}

const unsigned short *dkong3::logo(void) {
  return dkong3_logo;
}

#endif // ENABLE_DKONG3
