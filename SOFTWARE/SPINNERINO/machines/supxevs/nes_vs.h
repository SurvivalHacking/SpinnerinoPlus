#ifndef NES_VS_H
#define NES_VS_H
// ============================================================================
// nes_vs — motore Nintendo VS. UniSystem (RP2A03 + PPU RP2C04) per SPINNERINO.
//
// PERCHE' UN MOTORE NES IN QUESTO PROGETTO
// ----------------------------------------
// Il set `supxevs` («Vs. Super Xevious», Namco 1986) NON gira sull'hardware
// Namco a 3x Z80 di `xevious`: e' un NES da sala giochi. Driver di riferimento:
// MAME src/mame/nintendo/vsnes.cpp + src/devices/video/ppu2c0x.cpp, da cui
// questo file e' trascritto (background, sprite, sprite-0 hit, registri loopy).
//
// ⛔ NIENTE DIPENDENZE ARDUINO in questo file e nel .cpp: cosi' lo stesso codice
//    che gira sul P4 si compila anche sul PC e si verifica OFFLINE con
//    _verify_gfx/supxevs_harness (che rende i frame in PNG). E' la regola che ha
//    chiuso Xevious e NeoGeo: si confronta contro la FONTE, non contro una
//    propria copia, e si flasha una volta sola.
//
// MODELLO DI TEMPO: per SCANLINE (non per ciclo). Ogni scanline: prima la PPU
// disegna la riga, poi la CPU gira per ~113,7 cicli. E' la granularita' che
// serve allo split-screen via sprite-0 hit (il gioco polla $2002 in un loop) e
// costa una frazione di un modello ciclo-esatto.
// ============================================================================
#include <stdint.h>
#include "../../cpus/m6502/m6502.h"

#define NES_W                256
#define NES_H                240
#define NES_SCANLINES        262   // NTSC_SCANLINES_PER_FRAME
#define NES_VBLANK_SCANLINE  241   // VBLANK_FIRST_SCANLINE
#define NES_BOTTOM_VISIBLE   239   // BOTTOM_VISIBLE_SCANLINE

// Offset dentro il blocco RAM condiviso (`memory`, RAMSIZE): il motore non
// alloca nulla di suo tranne il framebuffer.
#define NESOFF_RAM    0x0000   // 2 KB  RAM CPU        ($0000-$07FF, mirror x4)
#define NESOFF_VRAM   0x0800   // 4 KB  nametable      (VS = four-screen, niente mirroring)
#define NESOFF_OAM    0x1800   // 256 B sprite RAM
#define NESOFF_NVRAM  0x1900   // 2 KB  RAM tamponata  ($6000-$67FF)
#define NESOFF_END    0x2100   // <= RAMSIZE

// Bit dei pulsanti nell'ordine in cui lo shift register li restituisce
// (macro VS_UNI_JOYSTICK di vsnes.cpp). ⚠️ Non e' la pulsantiera del NES:
// il bit 0x04 e' lo **START** del cabinato («Select 1 (Blue)») e il bit 0x08
// non e' collegato.
#define NESBTN_A       0x01
#define NESBTN_B       0x02
#define NESBTN_START   0x04
#define NESBTN_UP      0x10
#define NESBTN_DOWN    0x20
#define NESBTN_LEFT    0x40
#define NESBTN_RIGHT   0x80

// ⛔ CABLAGGIO "REVERSED" (INPUT_PORTS vsnes_rev, che supxevs usa):
// sul VS. UniSystem il controller di SINISTRA — il giocatore 1 — si legge da
// **$4017**, non da $4016 come sul NES. Il tasto START del giocatore 1 sta
// invece sul bit 0x04 di **$4016**. Da qui i due indici:
#define NESJOY_MOVE    1      // joy[1] -> $4017: direzioni e pulsanti del P1
#define NESJOY_START   0      // joy[0] -> $4016: il tasto START del P1

// Bit letti da $4016 oltre al pulsante (VS_COINS_AND_SERVICE): monete e service.
#define NESCOIN_SERV   0x04
#define NESCOIN_1      0x20
#define NESCOIN_2      0x40

// ----------------------------------------------------------------------------
// APU NES (RP2A03). ⭐ NON riscritta: e' quella che gia' suona in
// machines/dkong3/dkong3.cpp (2x RP2A03), trascritta qui con prefisso `nesapu_`.
// Copiata invece di condivisa per non toccare un gioco che funziona; se un
// giorno si corregge un difetto in una delle due, va corretto in ENTRAMBE.
// ----------------------------------------------------------------------------
struct nesapu_t {
  uint8_t  p_ctrl[2];  uint16_t p_timer[2]; bool p_en[2]; int p_cnt[2];
  uint8_t  p_seq[2];   uint8_t  p_env_vol[2]; uint8_t p_env_cnt[2]; bool p_env_rst[2];
  uint8_t  p_len[2];   uint8_t  p_sweep[2]; uint8_t p_sweep_cnt[2]; bool p_sweep_rst[2];
  uint16_t t_timer;    bool t_en; int t_cnt; uint8_t t_seq;
  uint8_t  t_last;     // ultimo livello del triangolo: quando si spegne, il DAC
                       // lo TIENE (come in MAME), non torna a zero
  uint8_t  t_lin_ctrl; uint8_t t_lin; bool t_lin_reload; uint8_t t_len;
  uint8_t  n_ctrl;     uint8_t n_period_idx; bool n_mode; bool n_en; int n_cnt;
  uint16_t n_lfsr;     uint8_t n_env_vol; uint8_t n_env_cnt; bool n_env_rst; uint8_t n_len;
};

// ⭐ LIMITE DEGLI 8 SPRITE PER SCANLINE — 1 = tolto (default), 0 = fedele.
// Perche' e' tolto: in Xevious **l'ombra di ogni oggetto e' uno sprite a se'**,
// quindi appena arrivano le basi il numero di sprite per riga raddoppia e
// sfora. Il gioco allora li alterna a ogni quadro e le ombre LAMPEGGIANO.
// Misurato sull'attract (che le basi ce le ha): 0 righe oltre gli 8 durante il
// titolo, **2,1 -> 4,3 righe per quadro** appena la demo incontra le basi.
// Sul cabinato vero quel lampeggio c'e'; qui pero' e' anche piu' visibile,
// perche' il pannello non mostra tutti i 60 quadri al secondo e il campionamento
// irregolare lo trasforma in sfarfallio. Rimesso a 0 si torna al comportamento
// dell'hardware.
// ⭐ Ammorbidimento orizzontale nel blit: fonde il RETINO A SCACCHIERA con cui
// il gioco disegna le ombre, che su LCD fa sfarfallare il pannello (inversione
// di polarita', ~30 Hz). Vedi il commento in nes_vs_blit_panel_row().
// A 0 si torna alla copia secca (immagine identica al NES, retino compreso).
#ifndef NES_SOFT_DITHER
#define NES_SOFT_DITHER          1
#endif

#ifndef NES_NO_SPRITE_LIMIT      // sovrascrivibile da -D per i confronti offline
#define NES_NO_SPRITE_LIMIT      1
#endif

#define NES_AUDIO_SAMPLES       2048   // ring buffer (potenza di 2)

// ⛔⛔ IL RATE E' 32 kHz, NON 24. `audio.cpp` apre l'I2S con
// `I2S_STD_CLK_DEFAULT_CONFIG(32000)` e col driver nuovo la
// `i2s_set_sample_rates(..., 24000)` piu' sotto **non ha effetto** — lo dice il
// commento a audio.cpp:211. Copiando i 400 campioni/quadro da dkong3 producevo
// 24.000 campioni al secondo mentre il device ne consumava 32.000: **un quarto
// dell'audio era vuoto**, riempito dal ramo di ripiego di
// supxevs_render_buffer — cioe' un buco a 60 Hz, che si sente come GRACCHIO — e
// per giunta tutto suonava un terzo piu' acuto.
// 536 e non 533,33: deve restare divisibile per 4, perche' il frame sequencer
// fa un passo ogni NES_SAMPLES_PER_FRAME/4 = 134. Il piccolo surplus (32.160
// contro 32.000) lo assorbe il `break` su ring pieno: meglio un campione di
// troppo che un buco.
// ⚠️ Lo stesso errore e' in machines/dkong3/dkong3.cpp (400 campioni): li' pero'
//    l'I2S viene messo a 11.765 Hz per DKONG/DKONGJR/MARIOBROS, non per DK3 —
//    da verificare separatamente.
#define NES_SAMPLES_PER_FRAME    536   // 536 x 60 Hz ≈ 32 kHz
#define NES_CPU_TICKS_PER_SAMPLE  56   // 1.789.773 / 32.000 = 55,9

struct nes_vs_t {
  m6502_t cpu;

  // --- ROM (in flash) ---
  const uint8_t  *prg;         // PRG, multipla di 8 KB
  uint16_t        prg_chunks;  // numero di banchi da 8 KB
  const uint8_t  *chr;         // CHR, multipla di 1 KB
  uint16_t        chr_chunks;  // numero di banchi da 1 KB
  const uint16_t *pal64;       // 64 colori RGB565 byte-swapped (PROM 2C04)

  // --- RAM (dentro il blocco condiviso) ---
  uint8_t *ram;                // base del blocco: vedi NESOFF_*

  // ⛔⛔ DOPPIO BUFFER — non e' un lusso, e' il rimedio allo SFARFALLIO IN GIOCO
  // (2026-08-04, segnalato sul device). Qui la PPU disegna dentro run_frame(),
  // cioe' sul **task emulazione (core 1)**, mentre il pannello legge dal core 0:
  // con un buffer solo il pannello mostra meta' frame nuovo e meta' vecchio.
  // Negli altri giochi del progetto non succede perche' disegnano in
  // prepare_frame(), che gira sullo stesso core del blit — ma un NES deve
  // disegnare riga per riga mentre la CPU gira, quindi qui serve lo scambio.
  //   fb      = quello in cui la PPU sta disegnando ADESSO
  //   fb_show = ultimo frame COMPLETO, l'unico che il pannello deve leggere
  // `volatile` perche' il puntatore viene letto dall'altro core (stessa
  // trappola di spinnerino_volume_multicore_race).
  // Sono TRE e non due per un motivo misurato: il blit del pannello sposta
  // 240x288x2 = 138 KB via SPI, cioe' ~14 ms a 80 MHz, praticamente quanto un
  // frame. Con due soli buffer, appena il blit sfora i 16,7 ms la PPU
  // ricomincia a scrivere proprio sul buffer che il pannello sta leggendo e lo
  // strappo torna. Col terzo ce n'e' sempre uno libero.
  //   fb        = quello in cui la PPU disegna ADESSO
  //   fb_show   = ultimo frame COMPLETO
  //   fb_locked = quello che il pannello sta leggendo in questo momento
  //               (prenotato da nes_vs_lock_frame all'inizio del quadro)
  uint16_t *fb;
  uint16_t *volatile fb_show;
  uint16_t *volatile fb_locked;
  uint16_t *fb_a, *fb_b, *fb_c;   // fb_b/fb_c possono essere NULL (si degrada)

  // ⭐⭐ MEDIA DI DUE QUADRI (frame blending) — la cura per le OMBRE.
  // Il gioco disegna le ombre **a quadri alterni**, di proposito, per fingere la
  // semi-trasparenza: a 60 Hz su un televisore l'occhio fonde i due quadri.
  // Il pannello dello SPINNERINO pero' non riceve tutti i 60 quadri (il blit da
  // 138 KB via SPI dura ~14 ms), quindi l'alternanza si vede come sfarfallio.
  // Rimedio documentato (NESdev: «the only proper way to scale a 60FPS game into
  // 30FPS would be to average frames»): il pannello non riceve piu' il quadro
  // grezzo ma la MEDIA fra quello appena finito e il precedente — cosi' l'ombra
  // c'e' sempre, a meta' intensita', com'era nelle intenzioni del gioco.
  //   fb_prev  = quadro precedente
  //   fb_out_a/b = le due uscite mediate, alternate per non riscrivere quella
  //                che il pannello sta leggendo
  // Se questi buffer non ci sono, il motore mostra il quadro grezzo come prima.
  uint16_t *fb_prev;
  uint16_t *fb_out_a, *fb_out_b;

  // ⚡ Puntatori gia' risolti ai 4 banchi PRG e alla CHR: nel percorso caldo si
  // legge da qui, senza rifare il conto dell'indirizzo a ogni accesso.
  // `chr_fast` punta alla copia in RAM interna se c'e' stata (32 KB), altrimenti
  // alla ROM in flash: il codice e' lo stesso nei due casi.
  const uint8_t *prg_ptr[4];
  const uint8_t *chr_fast;

  // --- mapper (Namco) 108 ---
  uint8_t prg_bank[4];         // 4 slot da 8 KB: $8000 $A000 $C000 $E000
  uint8_t chr_bank[8];         // 8 slot da 1 KB: $0000..$1C00
  uint8_t reg108;              // registro selezionato da $8000
  uint8_t prot_index;          // protezione Vs. Super Xevious

  // --- PPU ---
  uint8_t  ppu_ctrl0, ppu_ctrl1, ppu_status, ppu_oamaddr;
  uint16_t refresh_data, refresh_latch, videomem_addr;
  uint8_t  x_fine, toggle, data_latch, buffered_data, addr_inc;
  uint8_t  tile_page, sprite_page, back_color;
  uint8_t  pal_ram[32];
  // ⭐ COLORI GIA' PRONTI. Il renderer faceva, PER OGNI PIXEL, due letture:
  // pal_ram[...] e poi pal64[...] — e pal64 sta in FLASH. Sono ~65.000 accessi
  // alla flash per quadro solo per i colori. Le SCRITTURE alla palette sono
  // invece poche decine per quadro: conviene convertire li' e tenere qui il
  // risultato in RAM (stessa mossa che ha risolto gli fps del NeoGeo).
  uint16_t pal_rgb[32];
  uint16_t back_rgb;
  int      scanline;

  // --- input / VS ---
  uint8_t in_latch[2];         // shift register dei due joystick
  uint8_t in_strobe;
  uint8_t joy[2];              // stato pulsanti corrente (NESBTN_*)
  uint8_t coins;               // NESCOIN_* (letti da $4016)
  uint8_t dsw;                 // 8 dip: i 2 bassi escono da $4016, gli altri da $4017
  uint8_t coin_latch;          // ultimo valore scritto al contatore monete

  // Allungamento di COIN e START. Il firmware genera impulsi brevissimi (la
  // sequenza automatica in input.cpp tiene COIN 80 ms = 5 frame), mentre la ROM
  // campiona quei segnali con comodo. Tenere il bit premuto per qualche frame
  // in piu' e' quello che fa un giocatore vero, e non tocca l'input globale.
  uint8_t coin_hold, start_hold;

  // --- audio ---
  nesapu_t apu;
  short   *samples;            // ring buffer (PSRAM sul device)
  // ⛔ `volatile`: chi SCRIVE e' il task emulazione (core 1), chi LEGGE e' il
  // task audio (core 0). Senza, il compilatore — a maggior ragione ora che il
  // file e' a -O2 — puo' tenere gli indici nei registri e far vedere all'altro
  // core un buffer fermo: si sentirebbe come un vuoto, cioe' un gracchio.
  // Stessa trappola di spinnerino_volume_multicore_race.
  volatile uint16_t rptr, wptr;
  int      dc_q8, lpf;         // DC-blocker in virgola fissa 8.8 + filtro

  // --- diagnostica (usata dall'harness offline) ---
  // ⚠️ `volatile`: lo incrementa il task emulazione (core 1) e lo LEGGE IN
  // POLLING il render (core 0) per aspettare un quadro NUOVO prima di
  // prenotarlo — vedi supxevs::prepare_frame(). Senza volatile il compilatore
  // (il file e' a -O2) issa la lettura fuori dal ciclo di attesa e il ciclo non
  // termina piu'. Stessa trappola di rptr/wptr qui sopra.
  volatile uint32_t frames;
  uint32_t nmi_count;
  uint32_t spr0_hits;
  // Righe in cui sono stati scartati sprite perche' oltre gli 8 per scanline:
  // e' la misura dello SFARFALLIO NATIVO del NES (il gioco alterna gli sprite
  // apposta, e quel lampeggio si vede anche sull'hardware vero).
  uint32_t spr_overflow_lines;
  uint32_t spr_dropped;
  // Righe visibili disegnate col rendering SPENTO (diventano colore di sfondo)
  // e volte in cui $2001 cambia a meta' quadro: sono le due firme di uno
  // schermo che lampeggia tutto insieme.
  uint32_t blank_lines;
  uint32_t ctrl1_midframe;
  uint32_t oam_writes_blocked;   // scritture OAM rifiutate perche' in rendering
  // Prima riga visibile disegnata col rendering ACCESO, frame per frame: se
  // varia da un quadro all'altro, il bordo alto balla = sfarfallio.
  int16_t  first_on_line;        // -1 = nessuna riga accesa in questo frame
  int16_t  first_on_min, first_on_max;
  uint32_t first_on_changes;
};

#define NES_INPUT_HOLD_FRAMES  12

#ifdef __cplusplus
extern "C" {
#endif

void nes_vs_init(nes_vs_t *n,
                 const uint8_t *prg, uint32_t prg_len,
                 const uint8_t *chr, uint32_t chr_len,
                 const uint16_t *pal64,
                 uint8_t *ram_block,
                 uint16_t *fb, uint16_t *fb2, uint16_t *fb3,
                 short *audio_ring,
                 uint8_t *chr_ram_copy,    // 32 KB in RAM interna, oppure NULL
                 uint16_t *fb_out_a,       // uscite per la media di due quadri
                 uint16_t *fb_out_b);      // (NULL = niente media, quadro grezzo)

// Da chiamare UNA volta all'inizio del quadro (da prepare_frame): prenota il
// frame completo piu' recente, cosi' tutte le 36 bande del pannello vengono
// dallo STESSO quadro e la PPU non lo tocca finche' il blit non e' finito.
void nes_vs_lock_frame(nes_vs_t *n);

// Drena il ring buffer audio: ritorna quanti sample ha copiato (<= n).
int nes_vs_audio_pop(nes_vs_t *n, short *out, int n_max);

// Compone una banda di 8 righe della finestra portrait (240 px per riga)
// prendendola dal framebuffer NES: taglia `x_crop` colonne per lato e mette il
// gioco `y_off` righe piu' in basso. Le righe fuori dal gioco NON vengono
// toccate (il chiamante le ha gia' azzerate).
// ⚠️ Sta qui, e non nella classe, apposta: cosi' l'harness offline verifica
//    l'inquadratura VERA del pannello invece di una copia riscritta.
void nes_vs_blit_panel_row(nes_vs_t *n, int row, uint16_t *dst, int dst_w,
                           int x_crop, int y_off);
void nes_vs_reset(nes_vs_t *n);
void nes_vs_run_frame(nes_vs_t *n);

// Ingressi del frame: `dir` e' una combinazione di NESBTN_UP/DOWN/LEFT/RIGHT,
// `a` e `b` i due pulsanti, `coin` e `start` sono FRONTI (bastano 1 frame:
// li allunga il motore). Da chiamare prima di nes_vs_run_frame().
void nes_vs_set_input(nes_vs_t *n, uint8_t dir, int a, int b, int coin, int start);

#ifdef __cplusplus
}
#endif

#endif // NES_VS_H
