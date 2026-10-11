#pragma once
// ============================================================================
// prof.h — STRUMENTO DI MISURA per ottimizzare gli fps di ARKANOID 2.
//
// ⭐⭐ NON e' «debug on-device». E' la forma che
// [[feedback_fps_a_passo_fisso_sono_ciechi]] PRESCRIVE: uno strumento
// PERMANENTE dietro un `#define`, messo UNA volta, da cui leggere la
// ripartizione — cosi' poi si applicano tutti gli interventi insieme in un
// flash solo, invece di provarli uno per uno.
// ⛔ Il primo flash e' lo STRUMENTO, non un tentativo di cura
// ([[feedback_una_modifica_per_flash]], corollario del 2026-08-20: cinque flash
// di ipotesi non avevano trovato niente, tre righe a schermo hanno chiuso in
// tre secondi).
//
// ---------------------------------------------------------------- PERCHE' QUI
// Un contatore di fps DA SOLO non dice niente: il ciclo di questo progetto e' a
// passo fisso (`if (t1 < 16) delay(16 - t1)`), quindi segna 60 finche' non e'
// gia' rotto. Quello che serve e' il **CARICO** (lavoro medio / periodo) e il
// **PICCO**.
//
// ⭐⭐⭐ E qui c'e' una cosa in piu' che sul CABINATONE non serviva: **le due
// meta' girano su DUE TASK DIVERSI**. Il video sta nel `loop()` (core Arduino),
// l'emulazione nel suo task sull'altro core, e si parlano con
// `emulation_notifyGive()`. Prima di ottimizzare qualunque cosa bisogna sapere
// **CHI DEI DUE E' IN RITARDO** — e' il caso 5 di
// [[feedback_ottimizzare_contando_accessi]], dove un conto giusto sul renderer
// non serviva a niente perche' a essere in ritardo era l'altro task.
// Percio' i carichi misurati sono DUE:
//     CAR = carico del ciclo VIDEO      (REN + BLT)
//     CEM = carico del task EMULAZIONE  (run_frame)
// Quello che supera 100 e' il collo di bottiglia. Se nessuno dei due lo supera,
// il gioco non va ottimizzato affatto e le cure candidate si buttano.
//
// ---------------------------------------------------------------- COSA SI LEGGE
// Tre righe nella banda NERA in cima al pannello (32 px che nessun gioco usa:
// il quadro e' largo 256 dentro i 320 del pannello), ridisegnate una volta al
// secondo:
//
//   FPS nn  CAR nnn  PIC nn.n     quadri video/s · carico video · picco (ms)
//   REN nn.n BLT nn.n AUD nnn     disegno · blit SPI · blocchi audio/s (attesi 500)
//   EMU nn.n CEM nnn  EMS nn      run_frame (ms) · carico emulazione · quadri emulati/s
//
// ⛔ IL CONTO DEVE CHIUDERE: **REN + BLT = CAR x 16,667 ms**. Se non torna, il
// numero che si sta guardando puo' essere lo STRUMENTO e non il paziente
// ([[feedback_ottimizzare_contando_accessi]] caso 7: tre volte ho misurato il
// termometro invece del malato).
// ⚠️ EMU sta sull'ALTRO core: **non** si somma a CAR, e' il ramo parallelo.
// ⚠️ AUD atteso **500** (32000 Hz / 64 frame per blocco). Se e' molto sotto, il
// task audio non sta in pari ed e' quella la diagnosi, non gli fps
// ([[feedback_task_audio_indietro_watchdog]]).
// ⚠️ Niente `%f` nelle stringhe ([[esp32_arduino_no_float_printf]]): i decimi si
// fanno a mano con `%lu.%lu`.
//
// ---------------------------------------------------------------- COSTO
// Con `ARK2_PROF 0` tutte le funzioni sono `inline` VUOTE e non esiste nessuna
// variabile: il binario deve tornare **byte per byte** a quello di prima, e va
// verificato compilando due volte e confrontando la riga delle globali — su
// Shinobi due bandiere erano rimaste DENTRO la guardia e a 0 non compilava piu'.
// Acceso, il termometro costa 6 letture di `micros()` per quadro piu' 28
// passaggi sulla banda: dev'essere trascurabile, e il modo di verificarlo e'
// che il conto qui sopra chiuda.
//
// ⭐ Si accende SOLO su ARKANOID 2 (`prof_attiva()` guarda `machineType()`):
// sugli altri 40 giochi non cambia un pixel.
// ============================================================================
#include <stdint.h>

#define ARK2_PROF 0

#if ARK2_PROF

// acceso/spento a ogni quadro secondo la machine corrente
void prof_attiva(bool on);
bool prof_attivo(void);

// il task EMULAZIONE (altro core)
void prof_emu_inizio(void);
void prof_emu_fine(void);

// il ciclo VIDEO
void prof_ren_inizio(void);
void prof_ren_fine(void);
void prof_blt_inizio(void);
void prof_blt_fine(void);

// il task AUDIO: byte effettivamente consegnati all'I2S
void prof_aud_byte(unsigned int n);

// fine quadro video: `lavoro_us` e' il tempo del ciclo PRIMA dell'attesa di
// pacing (dopo, il totale sarebbe sempre 16,7 ms e non direbbe niente)
void prof_quadro(uint32_t lavoro_us);

// disegna la banda dentro la striscia `row` (coordinate di out_strip).
// Serve al percorso LARGO, quello che rispedisce anche il padding.
void prof_banda(unsigned short *strip, int row);

// ⭐ Percorso STRETTO: la banda e' FUORI dalla finestra SPI del gioco, quindi
// si scrive per conto suo — e **solo quando il testo cambia**, cioe' una volta
// al secondo invece di 49 ([[feedback_barra_volume_player_autonomi]]: la banda
// nera non la tocca nessun altro, fra un aggiornamento e l'altro resta com'e').
// `strip` e' un buffer di lavoro da almeno 1792 pixel. Ritorna true se ha
// scritto: in quel caso ha CAMBIATO la finestra SPI e il chiamante deve
// ridarla al gioco.
bool prof_banda_blit(unsigned short *strip);

#else   // ---------------------------------------------------------------- OFF

static inline void prof_attiva(bool on)   { (void)on; }
static inline bool prof_attivo(void)      { return false; }
static inline void prof_emu_inizio(void)  { }
static inline void prof_emu_fine(void)    { }
static inline void prof_ren_inizio(void)  { }
static inline void prof_ren_fine(void)    { }
static inline void prof_blt_inizio(void)  { }
static inline void prof_blt_fine(void)    { }
static inline void prof_aud_byte(unsigned int n) { (void)n; }
static inline void prof_quadro(uint32_t lavoro_us) { (void)lavoro_us; }
static inline void prof_banda(unsigned short *strip, int row) { (void)strip; (void)row; }
static inline bool prof_banda_blit(unsigned short *strip) { (void)strip; return false; }

#endif // ARK2_PROF
