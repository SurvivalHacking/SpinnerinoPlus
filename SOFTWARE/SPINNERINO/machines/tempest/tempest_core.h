// ============================================================================
// machines/tempest/tempest_core.h — Tempest (Atari 1981), MOTORE senza Arduino
//
// Tutto cio' che e' EMULAZIONE sta qui: mappa di memoria, IRQ, watchdog, i due
// POKEY (registri + RANDOM + ALLPOT + suono), EAROM ER2055. Niente Arduino,
// niente machineBase: lo includono sia `tempest.cpp` (firmware) sia il banco di
// prova sul PC (`_verify_gfx/tempest/mk_tptest.cpp`), cosi' il banco prova IL
// CODICE VERO e non una copia ([[feedback_verify_against_source_not_copy]]).
//
// FONTE: MAME di oggi, `src/mame/atari/tempest.cpp` (set `tempest3`, rev 3,
// quello dello zip), `src/devices/sound/pokey.cpp`, `src/devices/machine/er2055.cpp`.
// Ogni numero qui sotto viene da li', riga citata accanto.
//
// ⛔ Cosa sbagliava il WIP precedente (gioco che non partiva dopo il CREDIT):
//   1. ALLPOT ($60C8/$60D8) e RANDOM ($60CA/$60DA) restituivano 0: il gestore
//      IRQ legge manopola, FIRE e START da ALLPOT, e la protezione del gioco
//      controlla che il RANDOM scorra di un bit per ciclo;
//   2. IRQ a 60 Hz consegnato anche con il flag I alzato: MAME lo da' a
//      CLOCK_3KHZ/12 = 246 Hz, TENUTO alzato finche' il gioco scrive $5000;
//   3. DSW a 0xFF = «Freeze Mode» + 2 crediti minimi: i default MAME sono 0x00.
// ============================================================================
#pragma once
#include <stdint.h>
#include <string.h>
#include <math.h>

// --- dal core 6502 (fake6502.c, incluso prima di questo file) ---------------
extern uint32_t clockticks6502, clockgoal6502;
extern uint8_t  m6502_status;
void reset6502(void);
void irq6502(void);
void exec6502(uint32_t tickcount);

// --- dalla Math Box (mathbox.cpp, port 1:1 di MAME atari/mathbox.cpp) -------
void    mathbox_reset();
void    mathbox_go(uint8_t offset, uint8_t data);
uint8_t mathbox_lo();
uint8_t mathbox_hi();
uint8_t mathbox_status();

// ============================================================================
// Costanti (MAME atari/tempest.cpp)
// ============================================================================
// MASTER_CLOCK 12.096 MHz; CPU = /8; POKEY = /8: CPU e POKEY hanno lo STESSO
// clock, quindi un ciclo di CPU e' un clock di POKEY.
#define TP_CPU_HZ        1512000
// set_periodic_int(irq0_line_assert, CLOCK_3KHZ / 12): 12096000/4096/12 Hz
// = 246,09 Hz = un IRQ ogni 6144 cicli ESATTI.
#define TP_IRQ_PERIOD    6144
// WATCHDOG_TIMER set_time(CLOCK_3KHZ / 256) = 11,5 Hz = 131.072 cicli.
#define TP_WD_PERIOD     131072
// 60 Hz di quadro (vector.set_refresh_hz(60)).
#define TP_CYC_FRAME     (TP_CPU_HZ / 60)        // 25200
// Uscita audio dello SPINNERINO: I2S a 32 kHz ([[spinnerino_audio_i2s_32khz]]).
#define TP_AUD_RATE      32000

// ============================================================================
// POKEY — solo cio' che Tempest usa: 4 canali, AUDCTL, SKCTL (reset dei
// polinomi), POTGO/ALLPOT, RANDOM. Seriale e tastiera: no (Tempest non li usa).
// ============================================================================
#define TPK_POLY17_N  0x1ffff                     // 131071
#define TPK_POLY9_N   0x1ff
// AUDCx
#define TPK_NOTPOLY5  0x80
#define TPK_POLY4     0x40
#define TPK_PURE      0x20
#define TPK_VOLONLY   0x10
// AUDCTL
#define TPK_POLY9     0x80
#define TPK_CH1_HI    0x40
#define TPK_CH3_HI    0x20
#define TPK_CH12_J    0x10
#define TPK_CH34_J    0x08
#define TPK_CH1_FILT  0x04
#define TPK_CH2_FILT  0x02
#define TPK_CLK15     0x01
// SKCTL
#define TPK_SK_RESET  0x03
#define TPK_SK_PADDLE 0x04

struct TpChan {
  uint8_t audf, audc;
  int32_t counter, borrow;
  uint8_t output, filt;
};

struct TpPokey {
  TpChan  ch[4];
  uint8_t audctl, skctl;
  // tempo: clock gia' simulati (assoluti, = cicli di CPU)
  uint64_t t;
  // clock da quando SKCTL ha tolto il reset (puntatori dei polinomi e prescaler)
  uint32_t pclk;
  // pot
  uint8_t  potx[8], allpot;
  uint32_t pot_cnt;              // pot_counter di MAME
  // uscita: volumi attivi (out_raw di MAME, 4 nibble) e filtro RC
  uint16_t out_raw;
  float    out_filt;
  // accumulo del campione a 32 kHz in corso
  float    acc;                  // integrale (somma per clock) del campione
  uint32_t acc_n;                // clock gia' accumulati nel campione
  uint64_t nsamp;                // campioni emessi in tutto
};

// tabelle comuni ai due chip (riempite da tp_pokey_tables_init)
// ⚠️ Le due tabelle grandi stanno in UN blocco allocato dal guscio a runtime,
// non in .bss: ogni byte globale di un gioco pesa su TUTTI gli altri anche
// quando Tempest non gira ([[feedback_pce_lento_globali_soglia_vram]]).
#define TPK_C17_BYTES     16384                  // 131071 bit, arrotondato
#define TPK_TABLES_BYTES  (TPK_C17_BYTES + TPK_POLY9_N * 2)
static uint8_t  *tpk_c17  = nullptr;   // bit 0 di ogni stato del poly17: 131071 bit (16 KB)
static uint16_t *tpk_p9   = nullptr;   // 511 stati del poly9
static uint8_t   tpk_p4[15], tpk_p5[31];
static float     tpk_g[16];            // conduttanza di un canale al volume v (1/r_chan[v])
// RC del Tempest: set_output_rc(RES_K(10), CAP_U(0.015), 5.0)
#define TPK_RPULL   10000.0f
#define TPK_CAP     0.015e-6f

// --- polinomi: STESSE formule di MAME pokey.cpp (poly_init_4_5 / _9_17) ------
static void tp_pokey_tables_init(uint8_t *c17_buf /* TPK_TABLES_BYTES */) {
  tpk_p9 = (uint16_t *)(c17_buf + TPK_C17_BYTES);
  // poly4/5: lfsr = (lfsr<<1) | (~((lfsr>>2) ^ (lfsr>>xorbit)) & 1)
  for (int size = 4; size <= 5; size++) {
    int mask = (1 << size) - 1; uint32_t lfsr = 0; int xorbit = size - 1;
    uint8_t *dst = (size == 4) ? tpk_p4 : tpk_p5;
    for (int i = 0; i < mask; i++) {
      lfsr = (lfsr << 1) | (~((lfsr >> 2) ^ (lfsr >> xorbit)) & 1);
      dst[i] = (uint8_t)(lfsr & mask);
    }
  }
  { // poly9: in = bit0 ^ bit5; lfsr = (lfsr>>1) | (in<<8), partendo da 0x1ff
    uint32_t lfsr = 0x1ff;
    for (int i = 0; i < TPK_POLY9_N; i++) {
      uint32_t in = (lfsr & 1) ^ ((lfsr >> 5) & 1);
      lfsr = (lfsr >> 1) | (in << 8);
      tpk_p9[i] = (uint16_t)lfsr;
    }
  }
  // poly17: MAME tiene 131071 parole a 32 bit (512 KB). Qui basta il bit 0 di
  // ogni stato, perche' il registro scorre a DESTRA di uno per clock: i bit
  // 8..15 dello stato p sono i bit 0 degli otto stati precedenti (vedi
  // tpk_random17). Il banco di prova lo confronta con la tabella di MAME su
  // TUTTI i 131071 indici.
  tpk_c17 = c17_buf;
  memset(tpk_c17, 0, (TPK_POLY17_N + 7) / 8);
  {
    uint32_t lfsr = 0x1ffff;           // s_0 = tutti uno
    // c[i] = bit 0 di s_i, i = 0..N-1 (s_0 compreso)
    for (uint32_t i = 0; i < TPK_POLY17_N; i++) {
      if (lfsr & 1) tpk_c17[i >> 3] |= (uint8_t)(1 << (i & 7));
      uint32_t in8 = ((lfsr >> 8) ^ (lfsr >> 13)) & 1;
      uint32_t in  = lfsr & 1;
      lfsr = lfsr >> 1;
      lfsr = (lfsr & 0xff7f) | (in8 << 7);
      lfsr = (in << 16) | lfsr;
    }
  }
  // volumi: MAME vol_init(), resistenze {90000, 26500, 8050, 3400}, r_off 8e6
  const float res[4] = {90000.0f, 26500.0f, 8050.0f, 3400.0f};
  for (int j = 0; j < 16; j++) {
    float g = 1.0f / 1e12f;
    for (int i = 0; i < 4; i++) g += (j & (1 << i)) ? 1.0f / res[i] : 1.0f / 8e6f;
    tpk_g[j] = g;                      // 1 / r_chan[j]
  }
}

static inline uint32_t tpk_c17_bit(int32_t i) {
  i %= (int32_t)TPK_POLY17_N; if (i < 0) i += TPK_POLY17_N;
  return (tpk_c17[i >> 3] >> (i & 7)) & 1;
}
// MAME: m_poly17[p] = s_{p+1}. (s_{p+1} >> 8) & 0xff = bit c_{p-8} .. c_{p-1}.
static inline uint8_t tpk_random17(uint32_t p) {
  uint8_t r = 0;
  for (int k = 0; k < 8; k++) r |= (uint8_t)(tpk_c17_bit((int32_t)p - 8 + k) << k);
  return r;
}
// m_poly17[p] & 1 = bit 0 di s_{p+1} = c_{p+1}
static inline uint8_t tpk_noise17(uint32_t p) { return (uint8_t)tpk_c17_bit((int32_t)p + 1); }

static inline float tpk_v0(uint16_t raw, float *k_out);
static void tp_pokey_reset(TpPokey *pk) {
  memset(pk, 0, sizeof(*pk));
  // device_reset di MAME: AUDF/AUDC/AUDCTL 0, SKCTL 0 (polinomi fermi),
  // contatori a 0; m_out_filter parte da 0.
  for (int i = 0; i < 4; i++) { pk->ch[i].filt = (i < 2) ? 1 : 0; }
  // il filtro parte dal livello di SILENZIO (tutti i volumi a 0), non da 0:
  // altrimenti all'avvio c'e' un gradino = un colpo nell'altoparlante
  float k; pk->out_filt = tpk_v0(0, &k);
}

// ---------------------------------------------------------------------------
// Uscita: out_raw -> tensione (RC_LOWPASS di MAME sound_stream_update)
//   rTot = 1/sum(g[v]) ; V0 = rTot/(rTot+Rpull) ; mult per clock =
//   1 - exp(-(rTot+Rpull)/(C*Rpull*rTot) * Tclk)
// ---------------------------------------------------------------------------
static inline float tpk_v0(uint16_t raw, float *k_out) {
  float g = tpk_g[raw & 15] + tpk_g[(raw >> 4) & 15] + tpk_g[(raw >> 8) & 15] + tpk_g[(raw >> 12) & 15];
  float rtot = 1.0f / g;
  // costante di tempo per clock: (rTot+Rp)/(C*Rp*rTot) * Tclk = (1/Rp + g)/C * Tclk
  *k_out = ((1.0f / TPK_RPULL) + g) / TPK_CAP * (1.0f / (float)TP_CPU_HZ);
  return rtot / (rtot + TPK_RPULL);
}

// Avanza il filtro RC di n clock a out_raw costante, accumulando l'integrale.
static inline void tpk_filter_run(TpPokey *pk, uint32_t n) {
  if (!n) return;
  float k; const float v0 = tpk_v0(pk->out_raw, &k);
  const float d  = expf(-k);                 // (1 - mult) per clock
  const float dn = expf(-k * (float)n);
  const float e  = pk->out_filt - v0;
  // somma_{j=1..n} out_j = n*v0 + e * d*(1-d^n)/(1-d)
  float s = (1.0f - d) > 1e-9f ? e * d * (1.0f - dn) / (1.0f - d) : e * (float)n;
  pk->acc      += (float)n * v0 + s;
  pk->out_filt  = v0 + e * dn;
}

// Ricalcolo di out_raw: identico a MAME (m_old_raw_inval)
static inline void tpk_update_raw(TpPokey *pk) {
  uint16_t sum = 0;
  for (int c = 0; c < 4; c++) {
    const TpChan &q = pk->ch[c];
    if ((q.output ^ q.filt) || (q.audc & TPK_VOLONLY)) sum |= (uint16_t)((q.audc & 15) << (c * 4));
  }
  pk->out_raw = sum;
}

static inline void tpk_inc_chan(TpChan &q, int cycles) {
  q.counter = (q.counter + 1) & 0xff;
  if (q.counter == 0 && q.borrow == 0) q.borrow = cycles;
}
static inline int tpk_check_borrow(TpChan &q) {
  if (q.borrow > 0) { q.borrow--; return q.borrow == 0; }
  return 0;
}
static inline void tpk_reset_chan(TpChan &q) { q.counter = q.audf ^ 0xff; q.borrow = 0; }

// process_channel di MAME. I puntatori dei polinomi valgono pclk mod periodo
// (`++m_p4 == 0xf -> 0` ecc., incrementati a ogni clock fuori dal reset).
static inline void tpk_process(TpPokey *pk, int c) {
  TpChan &q = pk->ch[c];
  const uint32_t pc = pk->pclk;
  if ((q.audc & TPK_NOTPOLY5) || (tpk_p5[pc % 31] & 1)) {
    if (q.audc & TPK_PURE)            q.output ^= 1;
    else if (q.audc & TPK_POLY4)      q.output = tpk_p4[pc % 15] & 1;
    else if (pk->audctl & TPK_POLY9)  q.output = tpk_p9[pc % TPK_POLY9_N] & 1;
    else                              q.output = tpk_noise17(pc % TPK_POLY17_N);
  }
}

// UN clock, trascrizione di MAME step_one_clock() per la parte suono (senza
// seriale/tastiera). Ritorna true se out_raw puo' essere cambiato.
static inline bool tpk_step_one(TpPokey *pk) {
  bool inval = false;
  const uint8_t ctl = pk->audctl;
  if (pk->skctl & TPK_SK_RESET) {
    pk->pclk++;
    const bool t28  = (pk->pclk % 28) == 0;     // CLK_28: ++cnt >= DIV_64(28)
    const bool t114 = (pk->pclk % 114) == 0;    // CLK_114: ++cnt >= DIV_15(114)
    const bool base = (ctl & TPK_CLK15) ? t114 : t28;
    if (ctl & TPK_CH1_HI) tpk_inc_chan(pk->ch[0], (ctl & TPK_CH12_J) ? 7 : 4);
    if (!(ctl & TPK_CH1_HI) && base) tpk_inc_chan(pk->ch[0], 1);
    if (ctl & TPK_CH3_HI) tpk_inc_chan(pk->ch[2], (ctl & TPK_CH34_J) ? 7 : 4);
    if (!(ctl & TPK_CH3_HI) && base) tpk_inc_chan(pk->ch[2], 1);
    if (base) {
      if (!(ctl & TPK_CH12_J)) tpk_inc_chan(pk->ch[1], 1);
      if (!(ctl & TPK_CH34_J)) tpk_inc_chan(pk->ch[3], 1);
    }
    if ((t114 || (pk->skctl & TPK_SK_PADDLE)) && pk->pot_cnt < 228) {
      pk->pot_cnt++;
      for (int p = 0; p < 8; p++)
        if (pk->potx[p] < pk->pot_cnt || pk->pot_cnt == 228) pk->allpot |= (uint8_t)(1 << p);
    }
  }
  if (tpk_check_borrow(pk->ch[2])) {
    if (ctl & TPK_CH34_J) tpk_inc_chan(pk->ch[3], 1); else tpk_reset_chan(pk->ch[2]);
    tpk_process(pk, 2);
    if (ctl & TPK_CH1_FILT) pk->ch[0].filt = pk->ch[0].output; else pk->ch[0].filt = 1;
    inval = true;
  }
  if (tpk_check_borrow(pk->ch[3])) {
    if (ctl & TPK_CH34_J) tpk_reset_chan(pk->ch[2]);
    tpk_reset_chan(pk->ch[3]);
    tpk_process(pk, 3);
    if (ctl & TPK_CH2_FILT) pk->ch[1].filt = pk->ch[1].output; else pk->ch[1].filt = 1;
    inval = true;
  }
  if (tpk_check_borrow(pk->ch[0])) {
    if (ctl & TPK_CH12_J) tpk_inc_chan(pk->ch[1], 1); else tpk_reset_chan(pk->ch[0]);
    tpk_process(pk, 0);
    inval = true;
  }
  if (tpk_check_borrow(pk->ch[1])) {
    if (ctl & TPK_CH12_J) tpk_reset_chan(pk->ch[0]);
    tpk_reset_chan(pk->ch[1]);
    tpk_process(pk, 1);
    inval = true;
  }
  return inval;
}

// Quanti clock mancano al prossimo clock in cui step_one PUO' cambiare qualcosa.
// Fuori da quei clock lo stato non cambia: avanzano solo pclk (puntatori) e il
// filtro RC — ed e' questo il salto che rende il POKEY sostenibile sul P4
// (MAME fa 1,5 milioni di passi al secondo per chip; qui ~64 mila).
static inline uint32_t tpk_quiet_clocks(const TpPokey *pk) {
#ifdef TPK_NO_SKIP
  return 0;
#endif
  // borrow in corso o canali a 1,79 MHz: ogni clock conta
  for (int c = 0; c < 4; c++) if (pk->ch[c].borrow > 0) return 0;
  if (pk->audctl & (TPK_CH1_HI | TPK_CH3_HI)) return 0;
  if (!(pk->skctl & TPK_SK_RESET)) return 0xFFFFFFFFu;     // tutto fermo
  // prossimo clock "speciale": base tick (28 o 114) o tick del pot (114, o
  // ogni clock in modalita' SK_PADDLE se il conteggio e' ancora in corso)
  if ((pk->skctl & TPK_SK_PADDLE) && pk->pot_cnt < 228) return 0;
  uint32_t n28  = 28  - (pk->pclk % 28);      // clock fino a pclk%28==0
  uint32_t n114 = 114 - (pk->pclk % 114);
  uint32_t nb = (pk->audctl & TPK_CLK15) ? n114 : n28;
  uint32_t np = (pk->pot_cnt < 228) ? n114 : 0xFFFFFFFFu;
  uint32_t n = nb < np ? nb : np;
  return n - 1;                                // clock "vuoti" prima di quello speciale
}

// Emette i campioni a 32 kHz completati. Un campione copre 47,25 clock
// (1512000/32000 = 189/4): le finestre sono 47/47/47/48 in sequenza esatta.
static inline uint32_t tpk_win(uint64_t n) {
  // clock di fine del campione n (esclusivo) = floor((n+1)*189/4)
  return (uint32_t)(((n + 1) * 189) / 4 - (n * 189) / 4);
}

typedef void (*tpk_emit_fn)(int chip, uint64_t sample_index, float v);

// Avanza il chip fino al tempo assoluto `t_end` (in cicli CPU).
static void tpk_run(TpPokey *pk, int chip, uint64_t t_end, tpk_emit_fn emit) {
  while (pk->t < t_end) {
    uint32_t left = (uint32_t)(t_end - pk->t);
    uint32_t win  = tpk_win(pk->nsamp) - pk->acc_n;       // clock al bordo del campione
    if (left > win) left = win;
    uint32_t quiet = tpk_quiet_clocks(pk);
    if (quiet > 0) {
      uint32_t n = quiet < left ? quiet : left;
      tpk_filter_run(pk, n);
      if (pk->skctl & TPK_SK_RESET) pk->pclk += n;
      pk->t += n; pk->acc_n += n;
    } else {
      if (tpk_step_one(pk)) tpk_update_raw(pk);
      tpk_filter_run(pk, 1);
      pk->t += 1; pk->acc_n += 1;
    }
    if (pk->acc_n >= tpk_win(pk->nsamp)) {
      if (emit) emit(chip, pk->nsamp, pk->acc / (float)pk->acc_n);
      pk->nsamp++; pk->acc = 0; pk->acc_n = 0;
    }
  }
}

// ============================================================================
// Stato della macchina
// ============================================================================
struct TpWrite { uint64_t t; uint8_t chip, reg, val; };

struct TpState {
  uint8_t  ram[0x800];
  uint8_t  vram[0x1000];
  uint8_t  colorram[16];         // scritti dalla CPU, letti dall'AVG (avg.cpp)
  uint64_t cyc_base;             // cicli assoluti = cyc_base + clockticks6502
  uint64_t next_irq;
  uint64_t wd_last;
  uint8_t  irq_line;
  uint8_t  player_select;        // $60E0 bit 2 (FLIP)
  uint8_t  flip_x, flip_y;       // $4000 bit 3/4
  // ingressi (li scrive il guscio a ogni quadro)
  uint8_t  in0;                  // $0C00 senza bit 6/7: coin attivi bassi, ecc.
  uint8_t  knob;                 // 4 bit, contatore libero (IPT_DIAL)
  uint8_t  buttons;              // porta BUTTONSP1: bit0 SUPERZAP, bit1 FIRE (attivi BASSI)
  uint8_t  in2_hi;               // bit 5 START1, bit 6 START2, bit 7 (attivi bassi)
  uint8_t  dsw1, dsw2;           // $0D00, $0E00
  uint8_t  in2_dsw;              // IN2 bit 0..2 (difficolta' / rating)
  uint8_t  in1_cab;              // IN1 bit 4..7 (0x10 = upright)
  // AVG
  uint8_t  avg_halt;              // 1 dopo AVGRST (vg_set_halt(1))
  uint64_t avg_evt;               // fine della passata in corso dell'AVG (~0 = nessuna)
  uint8_t  avg_pass_halt;         // la passata in corso finisce in HALT
  uint32_t avg_go_count;
  // EAROM ER2055 (MAME er2055.cpp)
  uint8_t  earom[64], er_ctl, er_addr, er_data;
  // POKEY
  TpPokey  pk[2];
  // contatori diagnostici (banco di prova / overlay)
  uint32_t n_irq, n_wd_reset, n_random;
  // mix audio dei due POKEY (tp_mix_*)
  float    mix[1024];            // indicizzato per numero di campione & 1023
  uint64_t mix_next;             // primo campione non ancora consegnato
  float    hp_x, hp_y;           // passa-alto
};

static TpState *tp = nullptr;
static const uint8_t *tp_rom  = nullptr;   // 0x9000-0xDFFF (20 KB)
static const uint8_t *tp_vrom = nullptr;   // 0x3000-0x3FFF (4 KB)
static tpk_emit_fn tp_emit = nullptr;      // dove vanno i campioni POKEY

// ---------------------------------------------------------------------------
// Mix dei due POKEY. MAME: pokey1/pokey2 add_route(ALL_OUTPUTS, "mono", 0.5).
// L'uscita RC_LOWPASS e' una tensione 0..1 con il SILENZIO in alto (~0,98):
// va centrata, altrimenti la continua si mangia la dinamica
// ([[feedback_dac_unipolare_va_centrato]]). Passa-alto a 20 Hz, poi segno
// invertito (piu' volume = tensione piu' bassa) e guadagno sulla scala di
// valueToBuffer (picco ~1100 misurato sul banco in partita).
// ---------------------------------------------------------------------------
#define TP_MIX_GAIN   1700.0f
#define TP_MIX_CLAMP  1200
static void tp_mix_emit(int chip, uint64_t idx, float v) {
  (void)chip;
  tp->mix[idx & 1023] += 0.5f * v;
}
// Consegna i campioni che ENTRAMBI i chip hanno gia' prodotto.
static int tp_mix_drain(int16_t *out, int max) {
  const uint64_t ready = tp->pk[0].nsamp < tp->pk[1].nsamp ? tp->pk[0].nsamp : tp->pk[1].nsamp;
  const float k = 0.99608f;                  // exp(-2*pi*20/32000)
  int n = 0;
  while (tp->mix_next < ready && n < max) {
    float *p = &tp->mix[tp->mix_next & 1023];
    const float x = *p; *p = 0.0f;
    tp->hp_y = k * (tp->hp_y + x - tp->hp_x); tp->hp_x = x;
    int s = (int)(-tp->hp_y * TP_MIX_GAIN);
    if (s >  TP_MIX_CLAMP) s =  TP_MIX_CLAMP;
    if (s < -TP_MIX_CLAMP) s = -TP_MIX_CLAMP;
    out[n++] = (int16_t)s;
    tp->mix_next++;
  }
  return n;
}

// avg.cpp: il guscio lo chiama a ogni AVGGO/AVGRST
void avg_go_hook(void);
void avg_reset_hook(void);
void avg_event(void);          // fine di una passata dell'AVG (vedi avg.cpp)

static inline uint64_t tp_now(void) { return tp->cyc_base + clockticks6502; }

// ---------------------------------------------------------------------------
// EAROM (MAME er2055.cpp). Tempest: earom_write -> set_address(off&0x3f) +
// set_data; earom_control_w: set_control(BIT3, 1, !BIT2, BIT1); set_clk(BIT0)
// ---------------------------------------------------------------------------
#define ER_CK  0x01
#define ER_C1  0x02
#define ER_C2  0x04
#define ER_CS1 0x08
#define ER_CS2 0x10
static void tp_er_update_state(void) {
  switch (tp->er_ctl & (ER_C1 | ER_C2)) {
    case 0:      tp->earom[tp->er_addr] &= tp->er_data; break;   // write (AND)
    case ER_C2:  tp->earom[tp->er_addr] = 0xff;         break;   // erase
  }
}
static void tp_er_set_control(uint8_t cs1, uint8_t cs2, uint8_t c1, uint8_t c2) {
  uint8_t old = tp->er_ctl;
  uint8_t s = old & ER_CK;
  s |= c1 ? ER_C1 : 0; s |= c2 ? ER_C2 : 0; s |= cs1 ? ER_CS1 : 0; s |= cs2 ? ER_CS2 : 0;
  tp->er_ctl = s;
  if ((s & (ER_CS1 | ER_CS2)) != (ER_CS1 | ER_CS2) || s == old) return;
  tp_er_update_state();
}
static void tp_er_set_clk(int state) {
  uint8_t old = tp->er_ctl;
  if (state) tp->er_ctl |= ER_CK; else tp->er_ctl &= ~ER_CK;
  if ((tp->er_ctl & (ER_CS1 | ER_CS2)) == (ER_CS1 | ER_CS2) && tp->er_ctl != old && !state) {
    if ((tp->er_ctl & ER_C1) == ER_C1) tp->er_data = tp->earom[tp->er_addr];
    tp_er_update_state();
  }
}

// ---------------------------------------------------------------------------
// POKEY: lettura/scrittura (MAME pokey_device::read / write_internal)
// ---------------------------------------------------------------------------
static inline void tp_pokey_sync(int c) { tpk_run(&tp->pk[c], c, tp_now(), tp_emit); }

// input_port_1_bit_r / input_port_2_bit_r: (in & (1<<off)) ? 0 : 228
static inline uint8_t tp_in1(void) {
  return (uint8_t)((tp->knob & 0x0f) | (tp->in1_cab & 0xf0));
}
static inline uint8_t tp_in2(void) {
  // PORT_BIT(0x18, ACTIVE_HIGH, CUSTOM) tempest_buttons_r: i due bit della porta
  // BUTTONS (attivi BASSI) finiscono in 3..4 cosi' come sono.
  return (uint8_t)((tp->in2_dsw & 0x07) | ((tp->buttons & 0x03) << 3) | (tp->in2_hi & 0xe0));
}

static void tp_pokey_potgo(int c) {
  TpPokey *pk = &tp->pk[c];
  pk->allpot = 0; pk->pot_cnt = 0;
  const uint8_t in = c ? tp_in2() : tp_in1();
  for (int p = 0; p < 8; p++) {
    int r = (in & (1 << p)) ? 0 : 228;
    if (r == 0) pk->allpot |= (uint8_t)(1 << p);
    pk->potx[p] = (uint8_t)r;
  }
}

static uint8_t tp_pokey_read(int c, int off) {
  tp_pokey_sync(c);
  TpPokey *pk = &tp->pk[c];
  switch (off & 15) {
    case 0x00: case 0x01: case 0x02: case 0x03:
    case 0x04: case 0x05: case 0x06: case 0x07: {
      int p = off & 7;
      return (pk->allpot & (1 << p)) ? pk->potx[p] : (uint8_t)pk->pot_cnt;
    }
    case 0x08:   // ALLPOT
      if ((pk->skctl & TPK_SK_RESET) == 0) return pk->allpot;
      return (uint8_t)(pk->allpot ^ 0xff);
    case 0x0a:   // RANDOM
      tp->n_random++;
      if (pk->audctl & TPK_POLY9) return (uint8_t)(tpk_p9[pk->pclk % TPK_POLY9_N] & 0xff);
      return tpk_random17(pk->pclk % TPK_POLY17_N);
    case 0x0e:   // IRQST (attivo basso, nessun IRQ abilitato da Tempest)
      return 0xff;
    case 0x0f:   // SKSTAT (attivo basso)
      return 0xff;
    default:
      return 0xff;
  }
}

static void tp_pokey_write(int c, int off, uint8_t data) {
  tp_pokey_sync(c);
  TpPokey *pk = &tp->pk[c];
  switch (off & 15) {
    case 0x00: pk->ch[0].audf = data; break;
    case 0x01: pk->ch[0].audc = data; tpk_update_raw(pk); break;
    case 0x02: pk->ch[1].audf = data; break;
    case 0x03: pk->ch[1].audc = data; tpk_update_raw(pk); break;
    case 0x04: pk->ch[2].audf = data; break;
    case 0x05: pk->ch[2].audc = data; tpk_update_raw(pk); break;
    case 0x06: pk->ch[3].audf = data; break;
    case 0x07: pk->ch[3].audc = data; tpk_update_raw(pk); break;
    case 0x08: if (data != pk->audctl) { pk->audctl = data; tpk_update_raw(pk); } break;
    case 0x09:   // STIMER
      for (int i = 0; i < 4; i++) {
        tpk_reset_chan(pk->ch[i]); pk->ch[i].output = 0; pk->ch[i].filt = (i < 2) ? 1 : 0;
      }
      tpk_update_raw(pk);
      break;
    case 0x0b:   // POTGO
      if (pk->skctl & TPK_SK_RESET) tp_pokey_potgo(c);
      break;
    case 0x0f:   // SKCTL
      if (data == pk->skctl) break;
      pk->skctl = data;
      if (!(data & TPK_SK_RESET)) pk->pclk = 0;   // p4/p5/p9/p17 e prescaler a 0
      tpk_update_raw(pk);
      break;
    default: break;                               // SEROUT, IRQEN, SKREST: non usati
  }
}

// ============================================================================
// Bus della CPU (MAME tempest_state::main_map)
// ============================================================================
static inline uint8_t tp_rd(uint16_t a) {
  if (a < 0x0800) return tp->ram[a];
  if (a >= 0x9000 && a <= 0xdfff) return tp_rom[a - 0x9000];
  if (a >= 0xf800) return tp_rom[(a - 0xf800) + 0x4800];   // ROM_RELOAD 0xd800 a 0xf800
  if (a >= 0x2000 && a <= 0x2fff) return tp->vram[a - 0x2000];
  if (a >= 0x3000 && a <= 0x3fff) return tp_vrom[a - 0x3000];
  switch (a) {
    case 0x0c00: {
      // IN0: bit 6 = AVG done_r (alto = fermo), bit 7 = clock_r:
      // (total_cycles & 0x100) ? 1 : 0
      uint8_t v = tp->in0 & 0x3f;
      if (tp_now() >= tp->avg_evt) avg_event();
      if (tp->avg_halt) v |= 0x40;
      if (tp_now() & 0x100) v |= 0x80;
      return v;
    }
    case 0x0d00: return tp->dsw1;
    case 0x0e00: return tp->dsw2;
    case 0x6040: return mathbox_status();
    case 0x6050: return tp->er_data;                // earom_read: m_earom->data()
    case 0x6060: return mathbox_lo();
    case 0x6070: return mathbox_hi();
  }
  if (a >= 0x60c0 && a <= 0x60cf) return tp_pokey_read(0, a & 15);
  if (a >= 0x60d0 && a <= 0x60df) return tp_pokey_read(1, a & 15);
  return 0;                                         // non mappato
}

static inline void tp_wr(uint16_t a, uint8_t v) {
  if (a < 0x0800) { tp->ram[a] = v; return; }
  if (a >= 0x2000 && a <= 0x2fff) { tp->vram[a - 0x2000] = v; return; }
  if (a >= 0x0800 && a <= 0x080f) { tp->colorram[a & 15] = v; return; }
  if (a >= 0x6080 && a <= 0x609f) { mathbox_go((uint8_t)(a & 0x1f), v); return; }
  if (a >= 0x6000 && a <= 0x603f) { tp->er_addr = a & 0x3f; tp->er_data = v; return; }
  if (a >= 0x60c0 && a <= 0x60cf) { tp_pokey_write(0, a & 15, v); return; }
  if (a >= 0x60d0 && a <= 0x60df) { tp_pokey_write(1, a & 15, v); return; }
  switch (a) {
    case 0x4000:                                    // tempest_coin_w
      tp->flip_x = (v & 0x08) ? 1 : 0;
      tp->flip_y = (v & 0x10) ? 1 : 0;
      return;
    case 0x4800: tp->avg_go_count++; avg_go_hook(); return;     // avg go_w
    case 0x5000:                                    // wdclr_w: IRQ CLEAR + watchdog
      tp->irq_line = 0; tp->wd_last = tp_now(); return;
    case 0x5800: avg_reset_hook(); return;          // avg reset_w
    case 0x6040:                                    // earom_control_w
      // CK = EDB0, C1 = /EDB2, C2 = EDB1, CS1 = EDB3, /CS2 = GND
      tp_er_set_control((v >> 3) & 1, 1, !((v >> 2) & 1), (v >> 1) & 1);
      tp_er_set_clk(v & 1);
      return;
    case 0x60e0: tp->player_select = v & 0x04; return;          // tempest_led_w
  }
}

// ============================================================================
// Accensione / reset
// ============================================================================
static void tp_machine_reset(void) {
  // RAM e EAROM NON si toccano: il reset del watchdog sull'hardware vero non
  // azzera la memoria ([[feedback_watchdog_fa_parte_dell_avvio]]).
  tp->irq_line = 0;
  tp->player_select = 0; tp->flip_x = tp->flip_y = 0;
  tp->avg_halt = 1; tp->avg_evt = ~(uint64_t)0;
  tp->next_irq = tp_now() + TP_IRQ_PERIOD;
  tp->wd_last  = tp_now();
  mathbox_reset();
  avg_reset_hook();
  reset6502();
}

static void tp_power_on(void) {
  memset(tp->ram, 0, sizeof(tp->ram));
  memset(tp->vram, 0, sizeof(tp->vram));
  memset(tp->colorram, 0, sizeof(tp->colorram));
  memset(tp->earom, 0xff, sizeof(tp->earom));        // er2055 nvram_default: tutto 0xff
  tp->er_ctl = tp->er_addr = tp->er_data = 0;
  tp->cyc_base = 0;
  clockticks6502 = 0; clockgoal6502 = 0;
  tp_pokey_reset(&tp->pk[0]);
  tp_pokey_reset(&tp->pk[1]);
  tp->n_irq = tp->n_wd_reset = tp->n_random = 0;
  tp->avg_go_count = 0;
  memset(tp->mix, 0, sizeof(tp->mix));
  tp->mix_next = 0;
  { float k; tp->hp_x = 0.5f * 2.0f * tpk_v0(0, &k); }   // passa-alto gia' a regime sul silenzio
  tp->hp_y = 0.0f;
  tp_machine_reset();
}

// ============================================================================
// Un quadro di emulazione (25200 cicli). IRQ: irq0_line_ASSERT ogni 6144 cicli,
// linea TENUTA alta finche' $5000 non la abbassa, consegnata SOLO a flag I
// basso ([[fake6502_irq_ignores_iflag]], [[feedback_irq_di_livello_non_si_scarta]]).
// Fette di 32 cicli: e' il ritardo massimo con cui l'IRQ viene preso dopo un CLI.
// ============================================================================
#define TP_SLICE 32
static void tp_run_frame(void) {
  // riporta clockticks6502 vicino a zero (uint32: 47 minuti a 1,5 MHz)
  tp->cyc_base += clockticks6502;
  clockgoal6502 = 0;                 // (vedi sotto: l'obiettivo si ancora a ogni fetta)
  clockticks6502 = 0;
  const uint64_t t_end = tp->cyc_base + TP_CYC_FRAME;
  while (tp_now() < t_end) {
    const uint64_t now = tp_now();
#ifdef TP_DEBUG_LOOP
    { static uint64_t it = 0, lastnow = 0; static int same = 0;
      if (now == lastnow) { if (++same > 1000) { printf("STALLO now=%llu evt=%llu irq=%llu wd=%llu PC=%04X st=%02X\n",
        (unsigned long long)now,(unsigned long long)tp->avg_evt,(unsigned long long)tp->next_irq,(unsigned long long)tp->wd_last, m6502_pc, m6502_status); exit(1);} }
      else same = 0; lastnow = now; it++; }
#endif
    if (now >= tp->next_irq) { tp->irq_line = 1; tp->next_irq += TP_IRQ_PERIOD; }
    if (now >= tp->avg_evt) avg_event();
    if (now - tp->wd_last >= TP_WD_PERIOD) {         // watchdog scaduto: reset
      tp->n_wd_reset++;
      tp_machine_reset();
      continue;
    }
    if (tp->irq_line && !(m6502_status & 0x04)) {
      irq6502(); clockticks6502 += 7; tp->n_irq++;
    }
    uint64_t lim = t_end - now;
    uint64_t to_irq = (tp->next_irq > now) ? tp->next_irq - now : 1;
    uint32_t n = TP_SLICE;
    if (n > lim) n = (uint32_t)lim;
    if (n > to_irq) n = (uint32_t)to_irq;
    if (tp->avg_evt > now && tp->avg_evt - now < n) n = (uint32_t)(tp->avg_evt - now);
    if (n == 0) n = 1;
    // ⛔ L'obiettivo si ancora al conteggio ATTUALE prima di ogni fetta.
    // exec6502() fa `clockgoal6502 += n` e gira finche' i tick non lo
    // raggiungono: l'ultima istruzione lo SUPERA (fino a 6 cicli) e l'IRQ ne
    // aggiunge 7 da fuori. Con `clockgoal6502 -= clockticks6502` a inizio
    // quadro, uno sforo rendeva la sottrazione negativa su un uint32 ->
    // obiettivo di ~4 miliardi di cicli -> CPU ferma per un'ora. Trovato dal
    // banco al quadro 2506 dell'attract: sul device sarebbe stato un blocco
    // "a caso dopo qualche minuto".
    clockgoal6502 = clockticks6502;
    exec6502(n);
  }
  // porta i due POKEY a fine quadro (emette i campioni completati)
  tp_pokey_sync(0);
  tp_pokey_sync(1);
}
