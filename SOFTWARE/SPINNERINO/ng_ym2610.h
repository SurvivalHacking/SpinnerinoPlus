#pragma once
// ============================================================================
// ng_ym2610.h — quello che serve al core YM2610 portato da gngeo.
//
// Raccoglie in un solo posto cio' che nel sorgente originale arrivava da
// `config.h`, `mvs.h`, `SDL.h`, `timer.h`, `adpcm_cache.h` e `2610intf.h`.
// ⛔ `config.h` NON si puo' includere: in root del CABINATONE quel nome e' gia'
//    preso dalle opzioni del PC ENGINE. Stessa trappola gia' vista con pd4990a.
//
// Il core e' generato da `ROM CONVERTER PY/port_ym2610.py`: le uniche
// differenze rispetto a gngeo sono elencate li'.
// ============================================================================
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---- i tipi che mvs.h prendeva da SDL --------------------------------------
typedef int8_t   s8;
typedef int16_t  s16;
typedef int32_t  s32;
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
// il core usa anche i nomi SDL in qualche punto (adpcma_out, play_buffer)
typedef int8_t   Sint8;
typedef int16_t  Sint16;
typedef int32_t  Sint32;
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

// in gngeo e' un macro VUOTO (2610intf.h:10): la richiesta di aggiornamento
// serviva solo alla versione a streaming continuo, qui i campioni si generano
// una volta per frame.
#define YM2610UpdateRequest()

// Dove YM2610Update_stream() scrive i campioni (stereo intercalato L,R).
// Lo fa puntare la sezione sonora di neogeo.cpp prima di ogni chiamata.
extern Uint16 *play_buffer;

#define ALIGN_DATA                    // in gngeo era un attributo per il GP2X
#ifndef INLINE
#define INLINE static __inline__
#endif
#define SOUND_SAMPLES 512

#define Limit(val, max, min)                    \
{                                               \
        if (val > max) val = max;               \
        else if (val < min) val = min;          \
}

// ---- orologio del chip ------------------------------------------------------
// gngeo: `#define FM_GET_TIME_NOW() timer_get_time()`, con `timer_count` in
// SECONDI (double) fatto avanzare da my_timer() una volta per fetta di Z80.
// Serve solo al flag BUSY, che si legge poche centinaia di volte per frame:
// il double qui non pesa (⚠️ sul P4 NON c'e' la virgola mobile a 64 bit in
// hardware, quindi nel ciclo dei campioni non deve entrarci mai).
extern double ng_timer_count;
double ng_timer_get_time(void);
#define FM_GET_TIME_NOW() ng_timer_get_time()

// il temporizzatore di gngeo (timer.c), portato: 3 posti, nessuna allocazione
typedef struct ng_timer_struct {
    double time;
    int    param;
    u32    del_it;
    void (*func)(int param);
} ng_timer_struct;
ng_timer_struct *ng_insert_timer(double duration, int param, void (*func)(int));
void ng_del_timer(ng_timer_struct *ts);
void ng_free_all_timer(void);
void ng_my_timer(void);               // una chiamata per fetta di Z80
void ng_timer_reset(int nb_interlace);

// ---- ADPCM: cache dei campioni dalla microSD -------------------------------
// Quasi tutti i giochi tengono il `v.bin` intero in PSRAM: li' `active` resta 0
// e le macro ADPCM_READ_A/B del core si riducono a un accesso diretto al
// puntatore, come nel sorgente di gngeo.
// ⭐ Metal Slug 3 no: 16 MB di campioni PIU' 9 di programma non lascerebbero
// spazio alla cache degli sprite. Li' `active` vale 1 e i byte arrivano da
// `adpcm_cache_read()`, che neogeo.cpp appoggia sulla stessa `gfx_cache` degli
// sprite. Il costo e' minimo: l'ADPCM-A legge in sequenza, un banco da 4 KB
// dura circa mezzo secondo per canale.
typedef struct { int active; } adpcm_cache_t;
extern adpcm_cache_t adpcm_cacheA;      // definite in ng_ym2610.c
extern adpcm_cache_t adpcm_cacheB;
u8 adpcm_cache_read(adpcm_cache_t *c, u32 addr);   // definita in neogeo.cpp

// ---- memoria: la fornisce neogeo.cpp (PSRAM/interna, mai statica) ----------
void *ng_snd_alloc(size_t n);        // PSRAM va bene
void *ng_snd_alloc_caldo(size_t n);  // path caldo: prova prima la RAM interna
void  ng_snd_free(void *p);
int   ng_ym2610_alloc(void);          // definite dentro ng_ym2610.c
void  ng_ym2610_free(void);

// ---- il chip ---------------------------------------------------------------
typedef s16 FMSAMPLE;
typedef s32 FMSAMPLE_MIX;
#define TIMER_SH 16

typedef void (*FM_TIMERHANDLER)(int channel, int count, double stepTime);
typedef void (*FM_IRQHANDLER)(int irq);

void YM2610Init(int baseclock, int rate,
                void *pcmroma, int pcmsizea,
                void *pcmromb, int pcmsizeb,
                FM_TIMERHANDLER TimerHandler,
                FM_IRQHANDLER IRQHandler);
void YM2610ChangeSamplerate(int rate);
void YM2610Reset(void);
int  YM2610Write(int addr, u8 value);
u8   YM2610Read(int addr);
int  YM2610TimerOver(int channel);
void YM2610Update(int *p);
void YM2610Update_stream(int length);

// il core scrive qui: due canali da 16384 campioni, allocati a runtime
extern s16 *mixing_buffer[2];

#ifdef __cplusplus
}
#endif
