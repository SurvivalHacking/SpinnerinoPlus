// ============================================================================
// machines/tempest/avg.cpp — Atari Vector Generator di Tempest + rasterizzatore
//
// FONTE: MAME di oggi, src/devices/video/avgdvg.cpp (avg_device +
// avg_tempest_device). L'AVG vero e' una macchina a stati guidata da una PROM
// (136002-125, che lo zip NON contiene): qui si eseguono gli stessi gestori
// (latch0..3, strobe0..3) nell'ordine che ogni opcode richiede, con la STESSA
// aritmetica: normalizzazione, timer, DAC a 10 bit, scala lineare (scale^0xff)
// e binaria, stack a 4 voci (sp & 3). La semantica degli opcode e' quella del
// MAME storico (avg_generate_vector_list, mame0100), che la PROM implementa.
//
// Uscita: una lista di segmenti gia' in coordinate del framebuffer del gioco
// (fx = 0..TP_GW-1 lungo l'asse lungo, fy = 0..223 lungo le strisce), doppia:
// il core dell'emulazione scrive l'una mentre il render legge l'altra
// ([[feedback_chi_disegna_su_quale_core]]).
//
// ⛔ Il WIP precedente: scala sbagliata ((256-lin)>>bin invece di
// (255-lin)/256 >> bin), intensita' degli SVEC presa come zz invece di
// (lo>>4)&0xe, stack da 16, centro 290/285 in unita' intere.
// ============================================================================
#include <stdint.h>
#include <string.h>

#ifndef TP_GW
#define TP_GW 296            // asse lungo: 3:4 di MAME su 224 di larghezza
#endif
#define TP_GH 224

struct TpSeg { int16_t x0, y0, x1, y1; uint16_t col; };

#define AVG_MAX_SEG  2500
#define AVG_MAX_OPS  6000

// TRE buffer (allocati dal guscio): uno pubblicato (avg_front), uno PRENOTATO
// dal render per tutto il quadro (avg_resv), e quello in costruzione, che non
// e' mai nessuno dei due. Con due soli, se l'emulazione pubblica due volte
// mentre il render sta disegnando (1,5 passate per quadro) scriverebbe nel
// buffer in lettura ([[feedback_chi_disegna_su_quale_core]]: «due non bastano»).
static TpSeg   *avg_seg[3]  = {nullptr, nullptr, nullptr};
static int      avg_nseg[3] = {0, 0, 0};
static volatile int avg_front = 0;           // ultimo quadro completo
static volatile int avg_resv  = 0;           // quello che il render sta leggendo
static int      avg_back  = 1;               // in costruzione

// stato AVG (MAME avg_device)
static int  avg_halt_cmd;                    // ultimo HALT eseguito
static int  avg_ran;                         // c'e' stato almeno un VGGO
static int  avg_scale, avg_bin_scale, avg_color, avg_intensity;

// diagnostica
int avg_last_ops = 0, avg_last_nseg = 0, avg_last_end = 0;   // end: 1 JMP0, 2 HALT, 3 limite
uint32_t avg_n_pass = 0, avg_n_ops = 0;           // conteggi per il banco/overlay

// colorram e flip arrivano dal core
static inline void avg_map(int x16, int y16, int16_t *fx, int16_t *fy);

// --- MAME avg_tempest_device::handler_7 + apply_flipping -------------------
//   x,y beam (16.16), centro ((580-0)/2)<<16, ((570-0)/2)<<16
#define AVG_XC (290 << 16)
#define AVG_YC (285 << 16)

static inline uint16_t avg_rgb565(uint8_t data, int intensity) {
  // colorram attivo BASSO: r = bit1*0xf3 + bit0*0x0c, g = bit3*0xf3, b = bit2*0xf3
  const uint8_t d = (uint8_t)~data;
  int r = ((d >> 1) & 1) * 0xf3 + (d & 1) * 0x0c;
  int g = ((d >> 3) & 1) * 0xf3;
  int b = ((d >> 2) & 1) * 0xf3;
  // pal4bit(intensita'): 0..15 -> 0..255, scala lineare del colore
  const int k = intensity * 17;
  r = r * k / 255; g = g * k / 255; b = b * k / 255;
  uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
  return (uint16_t)((c >> 8) | (c << 8));      // byte-swap per lo SPI
}

static inline uint8_t avg_byte(const uint8_t *vram, const uint8_t *vrom, int a) {
  a &= 0x1fff;
  return a < 0x1000 ? vram[a] : vrom[a - 0x1000];
}

// Esegue la lista dal pc 0 e riempie il buffer posteriore. Ritorna i cicli
// AVG (unita' MASTER_CLOCK 12,096 MHz) e se e' finita in HALT.
static int avg_build(const uint8_t *vram, const uint8_t *vrom, const uint8_t *colorram,
                     int flip_x, int flip_y, uint32_t *cycles_out) {
  int back = 0;                                 // ne' pubblicato ne' prenotato
  while (back == avg_front || back == avg_resv) back++;
  avg_back = back;
  TpSeg *out = avg_seg[back];
  int nseg = 0;
  int pc = 0, sp = 0;
  int stack[4] = {0, 0, 0, 0};
  int xpos = AVG_XC, ypos = AVG_YC;
  int timer = 0;
  int have_last = 0; int16_t lx = 0, ly = 0;
  uint32_t cyc = 0;
  int end = 3, ops;
  for (ops = 0; ops < AVG_MAX_OPS; ops++) {
    // latch1 (byte alto) + latch0/latch3 (byte basso): m_pc ^ 1
    const uint8_t hi = avg_byte(vram, vrom, pc + 1);
    const uint8_t lo = avg_byte(vram, vrom, pc);
    pc += 2;
    const int op    = hi >> 5;
    const int dvy12 = (hi >> 4) & 1;
    int dvy = (dvy12 << 12) | ((hi & 0xf) << 8);
    int dvx = 0, int_latch = 0;
    const int OP0 = op & 1, OP1 = (op >> 1) & 1, OP2 = (op >> 2) & 1;
    cyc += 8 * 4;                                  // ~4 stati per parola
    if (op == 0) {                                 // VCTR: secondo parola
      dvy |= lo;                                   // latch0
      const uint8_t hi2 = avg_byte(vram, vrom, pc + 1);
      const uint8_t lo2 = avg_byte(vram, vrom, pc);
      pc += 2;
      int_latch = hi2 >> 4;                        // latch3
      dvx = ((int_latch & 1) << 12) | ((hi2 & 0xf) << 8);
      dvx = (dvx & 0x1f00) | lo2;                  // latch2
      cyc += 8 * 4;
    } else if (op == 2) {                          // SVEC: byte basso in latch3
      int_latch = lo >> 4;
      dvx = ((int_latch & 1) << 12) | ((lo & 0xf) << 8);
    } else {
      dvy |= lo;                                   // latch0
    }

    if (op == 0 || op == 2) {
      // strobe0: normalizzazione (MAME handler_4, ramo !OP0)
      int i = 0;
      while ((((dvy ^ (dvy << 1)) & 0x1000) == 0) && (((dvx ^ (dvx << 1)) & 0x1000) == 0) && (i++ < 16)) {
        dvy = (dvy & 0x1000) | ((dvy << 1) & 0x1fff);
        dvx = (dvx & 0x1000) | ((dvx << 1) & 0x1fff);
        timer >>= 1; timer |= 0x4000 | (OP1 << 7);
      }
      if (OP1) timer &= 0xff;
      // strobe1: scala binaria (handler_5, ramo !OP2)
      for (int k = avg_bin_scale; k > 0; k--) { timer >>= 1; timer |= 0x4000 | (OP1 << 7); }
      if (OP1) timer &= 0xff;
      // strobe3 (avg_common_strobe3, ramo !OP0 && !OP2)
      const int cycles = OP1 ? 0x100 - (timer & 0xff) : 0x8000 - timer;
      timer = 0;
      xpos += ((((dvx >> 3) ^ 0x200) - 0x200) * cycles * (avg_scale ^ 0xff)) >> 4;
      ypos -= ((((dvy >> 3) ^ 0x200) - 0x200) * cycles * (avg_scale ^ 0xff)) >> 4;
      cyc += (uint32_t)cycles;
      // tempest handler_7: colore dalla colorram, intensita'
      const int inten = ((int_latch >> 1) == 1) ? avg_intensity : (int_latch & 0xe);
      int x = xpos, y = ypos;
      if (flip_x) x += (AVG_XC - x) << 1;
      if (flip_y) y += (AVG_YC - y) << 1;
      int16_t fx, fy;
      avg_map(y - AVG_YC + AVG_XC, x - AVG_XC + AVG_YC, &fx, &fy);   // scambio x/y di Tempest
      if (inten && have_last && nseg < AVG_MAX_SEG) {
        TpSeg &s = out[nseg++];
        s.x0 = lx; s.y0 = ly; s.x1 = fx; s.y1 = fy;
        s.col = avg_rgb565(colorram[avg_color & 15], inten);
      }
      lx = fx; ly = fy; have_last = 1;
      continue;
    }
    if (op == 1) { end = 2; break; }               // HALT (strobe3: m_halt = OP0)
    if (op == 3) {                                 // STAT / SCAL: strobe2
      if (!dvy12) {                                // tempest handler_6
        if (dvy & 0x800) avg_color = dvy & 0xf;
        else             avg_intensity = (dvy >> 4) & 0xf;
      } else {                                     // avg_common_strobe2
        avg_scale = dvy & 0xff;
        avg_bin_scale = (dvy >> 8) & 7;
      }
      continue;
    }
    if (op == 4) {                                 // CNTR: strobe3 con OP2
      xpos = AVG_XC; ypos = AVG_YC; timer = 0;
      int16_t fx, fy;
      int x = xpos, y = ypos;
      avg_map(y - AVG_YC + AVG_XC, x - AVG_XC + AVG_YC, &fx, &fy);
      lx = fx; ly = fy; have_last = 1;
      continue;
    }
    // op 5 JSRL, 6 RTSL, 7 JMPL (strobe0 push / strobe1 sp / strobe2 pc).
    // ⛔ JMPL NON tocca lo stack (MAME storico: `pc = ...` e basta). La lista di
    // Tempest entra con JSRL in $0004 e prosegue con JMPL: se il JMPL
    // decrementasse sp, il JSRL successivo scriverebbe sopra l'indirizzo di
    // ritorno e la lista non arriverebbe mai al salto a 0 (provato sul banco).
    if (op == 5) { stack[sp & 3] = pc; sp = (sp + 1) & 0xf; }        // JSRL
    if (op == 6) { sp = (sp - 1) & 0xf; }                             // RTSL
    if (OP0) {                                     // avg_common_strobe2, OP2 && OP0
      if (dvy == 0) { end = 1; break; }            // salto a 0 = fine del quadro
      pc = dvy << 1;
    } else {
      pc = stack[sp & 3];
    }
  }
  avg_nseg[back] = nseg;
  avg_last_ops = ops; avg_last_nseg = nseg; avg_last_end = end; avg_n_ops += ops;
  if (cycles_out) *cycles_out = cyc;
  return end;
}

// Pubblica il buffer appena costruito (scambio atomico dell'indice).
static inline void avg_publish(void) { avg_front = avg_back; }
// Il render prenota l'ultimo quadro completo all'inizio del suo quadro.
static inline void avg_reserve(void) { avg_resv = avg_front; }

// ============================================================================
// Rasterizzatore a strisce. Il render chiede la striscia `row` (8 righe di fy,
// TP_GW pixel ciascuna). Ogni segmento si disegna solo nella sua parte.
// ============================================================================
// n/d arrotondato al piu' vicino (meta' lontano da zero), d != 0
static inline int avg_rdiv(int32_t n, int32_t d) {
  if (d < 0) { n = -n; d = -d; }
  return (int)(n >= 0 ? (n + d / 2) / d : (n - d / 2) / d);
}

static inline void avg_plot(uint16_t *strip, int ytop, int x, int y, uint16_t c) {
  if ((unsigned)x >= (unsigned)TP_GW) return;
  const int sy = y - ytop;
  if ((unsigned)sy >= 8u) return;
  strip[sy * TP_GW + x] = c;
}

static void avg_draw_strip(uint16_t *strip, int row) {
  const int fr = avg_resv;
  const TpSeg *s = avg_seg[fr];
  const int n = avg_nseg[fr];
  if (!s) return;
  const int ytop = row * 8, ybot = ytop + 7;
  for (int i = 0; i < n; i++) {
    int x0 = s[i].x0, y0 = s[i].y0, x1 = s[i].x1, y1 = s[i].y1;
    if ((y0 < ytop && y1 < ytop) || (y0 > ybot && y1 > ybot)) continue;
    const uint16_t c = s[i].col;
    int dx = x1 - x0, dy = y1 - y0;
    int adx = dx < 0 ? -dx : dx, ady = dy < 0 ? -dy : dy;
    if (adx == 0 && ady == 0) { avg_plot(strip, ytop, x0, y0, c); continue; }
    if (ady >= adx) {
      // asse maggiore y: una x per riga, solo le righe della striscia
      int ya = y0 < y1 ? y0 : y1, yb = y0 < y1 ? y1 : y0;
      if (ya < ytop) ya = ytop;
      if (yb > ybot) yb = ybot;
      for (int y = ya; y <= yb; y++)
        avg_plot(strip, ytop, x0 + avg_rdiv((int32_t)(y - y0) * dx, dy), y, c);
    } else {
      // asse maggiore x: si limita la x all'intervallo che cade nella striscia
      int xa = x0 < x1 ? x0 : x1, xb = x0 < x1 ? x1 : x0;
      if (dy != 0) {
        // x dove y = ytop-1 e y = ybot+1 (margine di un pixel)
        int32_t t0 = (int32_t)(ytop - 1 - y0) * dx / dy + x0;
        int32_t t1 = (int32_t)(ybot + 1 - y0) * dx / dy + x0;
        int32_t lo = t0 < t1 ? t0 : t1, hi = t0 < t1 ? t1 : t0;
        if (lo > xa) xa = (int)lo;
        if (hi < xb) xb = (int)hi;
      }
      if (xa < 0) xa = 0;
      if (xb > TP_GW - 1) xb = TP_GW - 1;
      for (int x = xa; x <= xb; x++) {
        int y = y0 + avg_rdiv((int32_t)(x - x0) * dy, dx);
        if (y < ytop || y > ybot) continue;
        strip[(y - ytop) * TP_GW + x] = c;
      }
    }
  }
}

// ============================================================================
// Agganci dal bus (tempest_core.h): AVGGO $4800 e AVGRST $5800
// ============================================================================
// Mappa coordinate vettoriali (16.16, area visibile 0..580 x 0..570, gia'
// scambiate da handler_7) -> framebuffer. Lo schermo MAME e' ROT270 e 3:4.
// L'orientamento si VERIFICA col titolo di riferimento (mk_tptest).
#ifndef TP_ORIENT
#define TP_ORIENT 0
#endif
static inline void avg_map(int x16, int y16, int16_t *fx, int16_t *fy) {
  int px = x16 >> 16, py = y16 >> 16;
  int ox, oy;                                  // coordinate di cio' che vede il giocatore
#if TP_ORIENT == 0
  ox = py;        oy = 580 - px;               // ROT270: swap_xy + flip_y
#elif TP_ORIENT == 1
  ox = 570 - py;  oy = px;
#elif TP_ORIENT == 2
  ox = py;        oy = px;
#else
  ox = 570 - py;  oy = 580 - px;
#endif
  const int ux = ox * TP_GH / 571;             // 0..223, sinistra -> destra
  const int uy = oy * TP_GW / 581;             // 0..TP_GW-1, alto -> basso
  *fy = (int16_t)(TP_GH - 1 - ux);             // galaga: fb_y = 223 - arcade_x
  *fx = (int16_t)uy;                           //         fb_x = arcade_y
}

void avg_reset_hook(void) {
  // vgrst + vg_set_halt(1)
  avg_scale = 0; avg_bin_scale = 0; avg_color = 0;
  tp->avg_halt = 1;
  tp->avg_evt = ~(uint64_t)0;
}

// Una PASSATA dell'AVG: la lista si legge dal pc 0 fino al salto a 0 o a un
// HALT, e dura il suo tempo di disegno (cicli MASTER_CLOCK, 8 per ciclo CPU).
// ⭐ Tempest fa girare la lista in tondo e, quando vuole un quadro nuovo,
// scrive un HALT in $2000: l'AVG vero lo trova AL GIRO DOPO, si ferma, e solo
// allora l'IRQ ($D7C9: BIT $0C00 / BVC) da' AVGRST + AVGGO. Leggere la lista
// una volta sola al GO non vede mai quell'HALT e il gioco smette di disegnare.

static void avg_start_pass(void) {
  avg_n_pass++;
  uint32_t cyc = 0;
  const int end = avg_build(tp->vram, tp_vrom, tp->colorram, tp->flip_x, tp->flip_y, &cyc);
  // MAME: al salto a 0 clear_list + vg_flush (il quadro si pubblica sempre);
  // al GO dopo un HALT il quadro si cambia solo se ha piu' di 10 vettori.
  if (end != 2 || avg_last_nseg > 10) avg_publish();
  tp->avg_pass_halt = (end == 2);
  uint32_t dur = cyc / 8;
  if (dur < 256) dur = 256;                      // passata minima (lista vuota)
  tp->avg_evt = tp_now() + dur;
}

void avg_event(void) {
  if (tp->avg_pass_halt) { tp->avg_halt = 1; tp->avg_evt = ~(uint64_t)0; return; }
  avg_start_pass();                              // JMPL 0: si ricomincia da capo
}

void avg_go_hook(void) {
  // go_w: vggo (pc=0, sp=0), vg_set_halt(0) e la macchina riparte
  avg_ran = 1;
  tp->avg_halt = 0;
  avg_start_pass();
}
