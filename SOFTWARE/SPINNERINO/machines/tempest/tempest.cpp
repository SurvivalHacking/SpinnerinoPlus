// ============================================================================
// machines/tempest/tempest.cpp — Tempest (Atari 1981), GUSCIO SPINNERINO
//
// Il motore (tempest_core.h + avg.cpp) e' lo stesso file che gira nel banco di
// prova sul PC: li' si e' visto partire attract, gettone, «RATE YOURSELF» e la
// partita, e si e' misurato il suono. Questo file aggiunge solo l'ambiente.
// ============================================================================
#include "tempest.h"

#ifdef ENABLE_TEMPEST
#include "esp_heap_caps.h"
#include "tempest_logo.h"
#include "tempest_rom_cpu.h"       // 0x9000-0xDFFF, 20 KB (CONVERTITORI/romconv_tempest.py)
#include "tempest_vector_rom.h"    // 0x3000-0x3FFF,  4 KB
#include "tempest_core.h"
#include "avg.cpp"

extern volatile uint8_t ec11_dial_counter;

// Allocazione: prima la RAM interna, poi la PSRAM (mai fallire per 1 KB).
static void *tp_alloc(size_t n, bool interna) {
  void *p = interna ? heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) : nullptr;
  if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  return p;
}

static uint8_t *tp_tables = nullptr;

void Tempest::init(Input *in, unsigned short *fb, sprite_S *sb, unsigned char *mem) {
  machineBase::init(in, fb, sb, mem);
}

// ⭐ Ordine delle allocazioni: PRIMA l'audio (ring letto 32.000 volte al
// secondo dal task audio), poi lo stato emulato e le tabelle, per ultimi i
// segmenti vettoriali in PSRAM ([[feedback_audio_muto_e_ram_interna]]).
void Tempest::reset() {
  machineBase::reset();
  if (!ring)      ring      = (int16_t *)tp_alloc(TP_RING * sizeof(int16_t), true);
  if (!tp)        tp        = (TpState *)tp_alloc(sizeof(TpState), true);
  if (!tp_tables) tp_tables = (uint8_t *)tp_alloc(TPK_TABLES_BYTES, true);
  for (int i = 0; i < 3; i++)
    if (!avg_seg[i]) avg_seg[i] = (TpSeg *)tp_alloc(AVG_MAX_SEG * sizeof(TpSeg), false);
  ring_r = ring_w = 0;
  if (!tp || !tp_tables || !ring || !avg_seg[0] || !avg_seg[1] || !avg_seg[2]) return;

  tp_pokey_tables_init(tp_tables);
  tp_rom  = tempest_rom_cpu;
  tp_vrom = tempest_vector_rom;
  tp_emit = tp_mix_emit;
  for (int i = 0; i < 3; i++) avg_nseg[i] = 0;
  avg_front = 0; avg_resv = 0; avg_back = 1; avg_ran = 0;

  // ingressi a riposo + DIP di fabbrica MAME (tempest.cpp INPUT_PORTS):
  //   DSW1 0x00 = 1 gettone 1 partita, nessun bonus · DSW2 0x00 = 3 vite,
  //   bonus a 20000, inglese, 1 credito minimo · IN2 bit0..2 = 0x07
  //   (Medium1 + rating 1,3,5,7,9) · IN1 bit 4 = 1 (mobile verticale)
  tp->in0 = 0x3f; tp->dsw1 = 0x00; tp->dsw2 = 0x00;
  tp->in2_dsw = 0x07; tp->in1_cab = 0xf0; tp->buttons = 0x03; tp->in2_hi = 0xe0;
  tp->knob = 0;
  dial_init = false; knob_acc = 0;
  fire_prev = false; fire_pulse = 0;
  tp_power_on();
}

// Restituisce tutto cio' che reset() sa ri-allocare (vedi machineBase.h).
// ⚠️ I puntatori del motore (tp, tpk_c17, tpk_p9) tornano a nullptr insieme
// ai blocchi: nessuno deve conservarne un pezzo
// ([[feedback_init_una_volta_sola_vs_rientro]], corollario Metal Slug 2).
void Tempest::freeBuffers() {
  if (ring)      { heap_caps_free(ring);      ring = nullptr; }
  if (tp)        { heap_caps_free(tp);        tp = nullptr; }
  if (tp_tables) { heap_caps_free(tp_tables); tp_tables = nullptr; tpk_c17 = nullptr; tpk_p9 = nullptr; }
  for (int i = 0; i < 3; i++)
    if (avg_seg[i]) { heap_caps_free(avg_seg[i]); avg_seg[i] = nullptr; avg_nseg[i] = 0; }
  ring_r = ring_w = 0;
}

const unsigned short *Tempest::logo(void) { return tempest_logo; }

// rd6502 NON inline: op6502 la richiama invece di duplicarne il corpo.
// ⛔⛔ 10/10: TOLTO `IRAM_ATTR` dalle tre funzioni (fix dei NEO GEO rallentati
// con TEMPEST nel binario, lo stesso del progetto NUOVO SPINNERINO PLUS DAVIDE).
// Con IRAM_ATTR le tre funzioni, con tp_rd/tp_wr inline dentro, prendevano
// +1.284 byte di `.iram0.text`, cioe' RAM interna tolta a TUTTI i giochi. Il
// 6502 di Tempest (25.200 cicli/quadro) sta comodo in flash con la cache, come
// quello di Nibbler ([[feedback_audio_muto_e_ram_interna]]).
__attribute__((noinline)) unsigned char Tempest::rd6502(unsigned short Addr) { return tp_rd(Addr); }
// Fetch degli opcode: il programma sta tutto in ROM $9000-$DFFF -> lettura diretta.
unsigned char Tempest::op6502(unsigned short Addr) {
  if ((unsigned short)(Addr - 0x9000) < 0x5000) return tp_rom[Addr - 0x9000];
  return Tempest::rd6502(Addr);
}
void Tempest::wr6502(unsigned short Addr, unsigned char Value) { tp_wr(Addr, Value); }

int Tempest::audioPop(int16_t *out, int n) {
  int got = 0;
  if (!ring) return 0;
  uint16_t r = ring_r;
  const uint16_t w = ring_w;
  while (got < n && r != w) { out[got++] = ring[r]; r = (r + 1) & (TP_RING - 1); }
  ring_r = r;
  return got;
}

void Tempest::run_frame() {
  if (!tp || !tp_tables) return;

  // ---- ingressi (una volta per quadro, mai dentro rd6502) ----
  const unsigned char b = input->buttons_get();
  tp->in0 = 0x3f;                                    // COIN3/2/1, TILT, TEST, STEP a riposo
  if (b & BUTTON_COIN)  tp->in0 &= ~0x04;            // COIN1
  tp->in2_hi = 0xe0;
  if (b & BUTTON_START) tp->in2_hi &= ~0x20;         // START1
  tp->buttons = 0x03;                                // porta BUTTONSP1, attiva bassa
  // BUTTON1 = FUOCO, SENZA AUTOFIRE (richiesta utente 10/10). Il gioco vero
  // spara a raffica col tasto tenuto: misurato sul banco, un colpo ogni 2-3
  // quadri fino agli 8 posti di $02D3-$02DA. Il gioco guarda il tasto circa
  // ogni 2,2 quadri, quindi un impulso FISSO non va: 1 quadro a volte non lo
  // vede, 3 quadri a volte fanno 2 colpi. Si tiene premuto dal fronte finche'
  // non si accende un posto colpo (sotto, dopo tp_run_frame) o per TP_FIRE_MAX
  // quadri: fuori partita (RATE YOURSELF, iniziali) basta come pressione.
  {
    const bool fire = (b & BUTTON_FIRE) != 0;
    if (fire && !fire_prev) fire_pulse = TP_FIRE_MAX;
    fire_prev = fire;
    if (fire_pulse) tp->buttons &= ~0x02;
  }
  // SUPERZAPPER sul tasto B (BUTTON_EXTRA). ⛔ La sequenza di START di
  // input.cpp FABBRICA un EXTRA insieme a START per 240 ms: senza questo filtro
  // ogni partita bruciava il superzapper alla partenza
  // ([[feedback_tasto_esiste_ma_non_arriva]], corollario 4).
  if ((b & BUTTON_EXTRA) && !(b & BUTTON_START)) tp->buttons &= ~0x01;

  // ---- manopola: contatore libero a 4 bit (IPT_DIAL, 72 passi per giro) ----
  // ec11_dial_counter avanza di 6 per scatto (livello 5): 3/5 -> 3,6 passi
  // per scatto, ~72 per un giro di un EC11 da 20 scatti. Max 7 passi per
  // quadro: il gioco fa la differenza a 4 bit fra due IRQ e oltre 7 la legge
  // al contrario; il resto si recupera nei quadri dopo.
  const uint8_t cur = ec11_dial_counter;
  if (!dial_init) { dial_last = cur; dial_init = true; }
  knob_acc += (int8_t)(cur - dial_last) * 3;
  dial_last = cur;
  int steps = 0;
  while (knob_acc >= 5 && steps < 7)  { knob_acc -= 5; steps++; }
  while (knob_acc <= -5 && steps > -7) { knob_acc += 5; steps--; }
  tp->knob = (uint8_t)((tp->knob + steps) & 0x0f);

  // ---- un quadro di macchina: 25.200 cicli, IRQ, AVG, POKEY ----
  // Posti colpo del giocatore $02D3-$02DA (0 = libero): se uno si accende in
  // questo quadro l'impulso del FUOCO ha fatto il suo colpo e si rilascia.
  uint8_t colpi_prima = 0;
  if (fire_pulse)
    for (int k = 0; k < 8; k++) if (tp->ram[0x2d3 + k]) colpi_prima |= 1u << k;
  tp_run_frame();
  if (fire_pulse) {
    bool partito = false;
    for (int k = 0; k < 8; k++)
      if (tp->ram[0x2d3 + k] && !(colpi_prima & (1u << k))) partito = true;
    fire_pulse = partito ? 0 : (uint8_t)(fire_pulse - 1);
  }

  // ---- audio -> ring (se pieno si scarta il nuovo: il task audio e' indietro) ----
  if (ring) {
    int16_t tmp[600];
    const int n = tp_mix_drain(tmp, 600);
    uint16_t w = ring_w;
    for (int i = 0; i < n; i++) {
      const uint16_t nx = (w + 1) & (TP_RING - 1);
      if (nx == ring_r) break;
      ring[w] = tmp[i]; w = nx;
    }
    ring_w = w;
  }

  if (!game_started) game_started = 1;
}

// Il render prenota l'ultimo quadro vettoriale completo: tutte le 28 strisce
// vengono dallo STESSO quadro anche se l'emulazione (altro core) ne pubblica
// di nuovi intanto.
void Tempest::prepare_frame() { avg_reserve(); }

void Tempest::render_row(short row) {
  // frame_buffer gia' azzerato dal ciclo di render (hasOpaqueBG() == false)
  avg_draw_strip(frame_buffer, row);
}

#endif // ENABLE_TEMPEST
