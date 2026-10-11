// ============================================================================
// supxevs.cpp — raccordo fra il motore nes_vs e il framework SPINNERINO.
// Vedi supxevs.h per l'inquadramento e nes_vs.h per l'emulazione.
// ============================================================================
#include "config.h"
#include "machines/supxevs/supxevs.h"

#ifdef ENABLE_SUPXEVS

#include "emulation/input.h"
#include <esp_heap_caps.h>

// Le ROM (in flash) e il motore. Una sola TU: questo file e' incluso dallo
// sketch, e a sua volta include nes_vs.cpp — che nelle sottocartelle l'IDE
// non compilerebbe da solo.
#include "machines/supxevs/supxevs_prg.h"
#include "machines/supxevs/supxevs_chr.h"
#include "machines/supxevs/supxevs_pal.h"
#include "machines/supxevs/nes_vs.cpp"

void supxevs::reset() {
  machineBase::reset();          // azzera `memory` (il motore ci tiene RAM, VRAM, OAM, NVRAM)

  // Allocazione pigra in PSRAM, come Xevious e Phoenix: al boot non si paga
  // nulla, si paga solo quando il gioco viene scelto.
  // TRE framebuffer da 120 KB: e' il rimedio allo sfarfallio in gioco — la PPU
  // disegna sul task emulazione mentre il pannello legge dall'altro core, e il
  // blit dura quasi quanto un frame. Vedi il commento in nes_vs.h.
  const size_t FB_BYTES = (size_t)NES_W * NES_H * sizeof(uint16_t);
  if (!nes_fb)  nes_fb  = (uint16_t *)ps_malloc(FB_BYTES);
  if (!nes_fb2) nes_fb2 = (uint16_t *)ps_malloc(FB_BYTES);
  if (!nes_fb3) nes_fb3 = (uint16_t *)ps_malloc(FB_BYTES);
  // ⛔ Le due uscite della media dei quadri NON si allocano piu': la fusione
  // lasciava scie sul terreno che scorre, e soprattutto curava un difetto mal
  // diagnosticato (la rotazione OAM del gioco e' uno SCAMBIO, e con il limite
  // degli 8 sprite tolto quegli sprite li disegniamo comunque tutti).
  // Restano 240 KB di PSRAM liberi. Il codice della fusione resta in nes_vs.cpp
  // per il caso in cui serva davvero, ma inattivo.
  if (!nes_audio)
    nes_audio = (short *)ps_malloc(NES_AUDIO_SAMPLES * sizeof(short));

  if (nes_fb)    memset(nes_fb, 0, FB_BYTES);
  if (nes_fb2)   memset(nes_fb2, 0, FB_BYTES);
  if (nes_fb3)   memset(nes_fb3, 0, FB_BYTES);
  if (nes_audio) memset(nes_audio, 0, NES_AUDIO_SAMPLES * sizeof(short));

  // ⚡ La CHR (32 KB) in RAM INTERNA, non in flash: e' letta ~16.600 volte per
  // quadro dal ciclo piu' interno del rendering, e in flash quegli accessi
  // vengono sfrattati dalla cache dal traffico del framebuffer. Se i 32 KB non
  // ci sono, il motore continua a leggere dalla flash: cambia solo la velocita'.
  if (!chr_ram)
    chr_ram = (uint8_t *)malloc(sizeof(supxevs_chr));

  // Degrado pulito: con due buffer resta un po' di strappo quando il blit
  // sfora il frame, con uno solo si torna allo sfarfallio — ma il gioco parte
  // comunque invece di dare schermo nero.
  nes_vs_init(&nes, supxevs_prg, sizeof(supxevs_prg),
              supxevs_chr, sizeof(supxevs_chr), supxevs_pal,
              memory, nes_fb, nes_fb2, nes_fb3, nes_audio, chr_ram,
              nes_out1, nes_out2);
  nes_vs_reset(&nes);

  prev_coin = prev_start = false;
  last_shown = 0;                  // nes_vs_reset() riazzera nes.frames

  // Il framework usa game_started per il pacing (emulation.cpp): con
  // freeRunEmulation() serve alzato subito, altrimenti il frame gira senza
  // freno e il ring audio a 24 kHz va in overrun.
  game_started = 1;
}

void supxevs::run_frame(void) {
  const unsigned char b = input->buttons_get();

  uint8_t dir = 0;
  if (b & BUTTON_UP)    dir |= NESBTN_UP;
  if (b & BUTTON_DOWN)  dir |= NESBTN_DOWN;
  if (b & BUTTON_LEFT)  dir |= NESBTN_LEFT;
  if (b & BUTTON_RIGHT) dir |= NESBTN_RIGHT;

  // FIRE = zapper (spara in aria), EXTRA = blaster (sgancia la bomba sul
  // bersaglio). Sono i due tasti del cabinato Xevious; sul gamepad BT sono
  // A e B (vedi input.cpp).
  const int a_btn = (b & BUTTON_EXTRA) ? 1 : 0;
  const int b_btn = (b & BUTTON_FIRE)  ? 1 : 0;

  // COIN e START vanno passati come FRONTI: il firmware li genera come impulsi
  // di 80 e 240 ms e il motore li allunga (NES_INPUT_HOLD_FRAMES).
  const bool coin_now  = (b & BUTTON_COIN)  != 0;
  const bool start_now = (b & BUTTON_START) != 0;
  const int coin_edge  = (coin_now  && !prev_coin)  ? 1 : 0;
  const int start_edge = (start_now && !prev_start) ? 1 : 0;
  prev_coin = coin_now;
  prev_start = start_now;

  nes_vs_set_input(&nes, dir, a_btn, b_btn, coin_edge, start_edge);
  nes_vs_run_frame(&nes);
}

// ============================================================================
// ⭐ INDICATORE DI STATO — permanente, nella BANDA NERA sopra il gioco.
//
// Non e' un ciclo di debug: e' UNO strumento messo una volta, che risponde alle
// domande a cui altrimenti si risponde flashando a ripetizione. Serve perche'
// questo motore degrada in silenzio (se un buffer non si alloca, torna al
// comportamento di prima senza dirlo): senza indicatore, «non e' cambiato
// nulla» e «il codice non si e' attivato» sono indistinguibili.
//
// Sta nelle righe 0..23 del pannello, che sono nere perche' il gioco NES e'
// alto 240 su una finestra da 288: **non copre un solo pixel di gioco**.
//   E<n>  quadri EMULATI al secondo (dovrebbe essere 60)
//   R<n>  quadri MOSTRATI dal pannello al secondo
//   B<n>  buffer allocati: 5 = tutto attivo, meno = qualcosa e' degradato
//   M<0|1> media dei quadri ATTIVA (1) o no (0)
//   A<n>  campioni nel ring audio (se va spesso a 0, l'audio si svuota)
// Per toglierlo: SUPXEVS_DIAG a 0.
// ============================================================================
// 0 = spento (difetto chiuso il 2026-08-04). Rimettere a 1 se serve di nuovo
// leggere il passo dell'emulazione, quello del pannello o il livello del ring
// audio: e' UNO strumento gia' pronto, non un ciclo di debug da reinventare.
#define SUPXEVS_DIAG 0

#if SUPXEVS_DIAG
// ⚠️ NIENTE #include di emulation/font8.h qui: lo sketch lo ha gia' incluso
// (SPINNERINO.ino, per boot_msg) e questo file viene compilato DENTRO quella
// stessa unita'. Includendolo di nuovo il `#pragma once` non lo riconosce —
// il percorso arriva una volta con `/` e una con `\` — e si ottiene
// «redefinition of char_to_idx». FONT8 e char_to_idx sono quindi gia' visibili.

static char     diag_txt[32] = "";
static uint32_t diag_t0 = 0;
static uint32_t diag_emu0 = 0, diag_ren = 0, diag_ren0 = 0;

void supxevs::diag_update(void) {
  diag_ren++;
  const uint32_t now = millis();
  if (diag_t0 == 0) { diag_t0 = now; return; }
  if (now - diag_t0 < 1000) return;

  const uint32_t dt = now - diag_t0;
  const uint32_t emu = nes.frames;
  const int fps_emu = (int)((emu - diag_emu0) * 1000 / dt);
  const int fps_ren = (int)((diag_ren - diag_ren0) * 1000 / dt);

  int nbuf = 0;
  if (nes_fb)   nbuf++;
  if (nes_fb2)  nbuf++;
  if (nes_fb3)  nbuf++;
  if (nes_out1) nbuf++;
  if (nes_out2) nbuf++;
  const int media_on = (nes_out1 && nes_out2) ? 1 : 0;

  const int liv = (int)((nes.wptr - nes.rptr) & (NES_AUDIO_SAMPLES - 1));

  snprintf(diag_txt, sizeof(diag_txt), "E%d R%d B%d M%d A%d",
           fps_emu, fps_ren, nbuf, media_on, liv);

  diag_t0 = now;
  diag_emu0 = emu;
  diag_ren0 = diag_ren;
}

// Disegna una banda di 8 righe del testo (row 0..2 = pixel 0..23).
static void diag_draw_row(unsigned short *dst, int row) {
  const int y0 = row * 8;
  if (y0 != 8) return;                 // il testo sta sulle righe 8..15
  for (int i = 0; diag_txt[i] && i < 30; i++) {
    const int idx = char_to_idx(diag_txt[i]);
    const int x = i * 8;
    for (int r = 0; r < 8; r++) {
      const unsigned char bits = pgm_read_byte(&FONT8[idx][r]);
      unsigned short *line = dst + r * 240 + x;
      for (int c = 0; c < 8; c++)
        if (bits & (0x80 >> c))
          line[c] = 0xFFFF;            // bianco
    }
  }
}
#endif // SUPXEVS_DIAG

// Il quadro e' gia' stato disegnato dentro run_frame (la PPU lavora per
// scanline). Qui si PRENOTA il frame completo piu' recente: il .ino chiama
// prepare_frame() una volta prima delle 36 bande, quindi da questo momento
// tutto il blit legge lo stesso quadro e la PPU non lo tocca.
void supxevs::prepare_frame(void) {
  // ⭐⭐ ACCOPPIAMENTO 1:1 fra quadro EMULATO e quadro MOSTRATO.
  //
  // Misurato sul device: `E60 R62`. L'emulazione produce 60 quadri al secondo
  // (free-run a 16667 us sul core 1), il render ne disegna 62 (il suo ciclo dura
  // ~16,1 ms: 13,8 di SPI a 80 MHz piu' il resto). I due non erano agganciati:
  // `emulation_notifyGive()` viene chiamata ma il ramo free-run di
  // emulation_frame() non la aspetta.
  // Conseguenze, entrambe visibili come «le ombre sfarfallano»:
  //   1. due volte al secondo il pannello ridipinge lo STESSO quadro;
  //   2. soprattutto, la fase del blit rispetto al quadro emulato SCORRE di
  //      continuo (battimento di 2 Hz), e con essa il punto in cui la
  //      riscrittura incontra il refresh interno del pannello — che non ha il
  //      segnale TE (config.h non definisce nessun pin TE, quindi non c'e' modo
  //      di sincronizzarsi con lui).
  // Che il difetto sia QUI e non nel disegno e' provato offline: su 70 quadri
  // consecutivi di gioco vero l'immagine emulata e' stabile (pixel che
  // alternano: ~60 su 61.440, cioe' 0,1%), e le ombre non spariscono mai —
  // togliendo il limite degli 8 sprite lo scambio OAM del gioco non nasconde
  // piu' nulla.
  //
  // Rimedio: si aspetta che ci sia un quadro NUOVO. R scende a 60 esatti,
  // nessun quadro viene mostrato due volte e il blit parte sempre allo stesso
  // punto del ciclo. L'attesa media e' ~0,5 ms (la differenza fra 16,1 e 16,7),
  // quindi non toglie margine al render.
  // Il tetto di 20 ms e' una rete di sicurezza: se l'emulazione non gira (gioco
  // non ancora partito, task non creato) il render prosegue col quadro vecchio
  // invece di piantarsi.
  {
    const uint32_t t0 = micros();
    while (nes.frames == last_shown && (uint32_t)(micros() - t0) < 20000)
      taskYIELD();                      // cede il core 0 a BT/audio mentre aspetta
    last_shown = nes.frames;
  }

  nes_vs_lock_frame(&nes);
#if SUPXEVS_DIAG
  diag_update();                        // una volta per quadro, non per banda
#endif
}

// row 0..35 nella finestra portrait 240x288. Le righe fuori dal gioco restano
// nere: il loop in SPINNERINO.ino azzera frame_buffer prima di ogni chiamata.
void supxevs::render_row(short row) {
  // La composizione vera sta nel motore (nes_vs_blit_panel_row): e' lo stesso
  // codice che l'harness offline usa per rendere l'inquadratura del pannello,
  // quindi il ritaglio si verifica sul PC e non sul device.
  nes_vs_blit_panel_row(&nes, row, (uint16_t *)frame_buffer, 240,
                        SUPXEVS_X_CROP, SUPXEVS_Y_OFF);
#if SUPXEVS_DIAG
  if (row < 3)
    diag_draw_row((unsigned short *)frame_buffer, row);
#endif
}

int supxevs::audioPop(int16_t *out, int n) {
  return nes_vs_audio_pop(&nes, out, n);
}

#endif // ENABLE_SUPXEVS
