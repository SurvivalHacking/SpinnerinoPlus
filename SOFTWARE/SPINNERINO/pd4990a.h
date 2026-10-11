#ifndef PD4990A_H
#define PD4990A_H
// ============================================================================
// pd4990a.h — calendario/orologio seriale NEC uPD4990A del NEO GEO.
//
// PORTATO DA gngeo TALE E QUALE (la logica non e' stata toccata): il BIOS ne
// legge i bit uno a uno e senza risposte coerenti si ferma su "CALENDAR ERROR".
// Verificato sul PC nell'harness _verify_gfx/neogeo_harness.
//
// ── COSA E' STATO ADATTATO (e perche') ──────────────────────────────────────
// L'originale include "memory.h" (un header di gngeo che qui non esiste) e
// pretende i tipi Uint8/Uint32 di SDL. Nell'harness c'erano tre stub (SDL.h,
// state.h, config.h); nella root dello sketch NON si possono usare:
//   · "config.h" nella root e' GIA' un altro file (le opzioni di traccia del
//     PC Engine) e includerlo qui darebbe il file sbagliato;
//   · "memory.h"/"state.h" non esistono e su alcuni toolchain <memory.h> di
//     sistema verrebbe risolto a sorpresa.
// Quindi: niente include esterni, i due tipi che servono sono definiti qui.
// ⚠️ Arduino compila SOLO i sorgenti nella cartella dello sketch: per questo
// pd4990a.c/.h stanno nella ROOT e non sotto _verify_gfx/.
// ============================================================================
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// I nomi SDL li usa il corpo di pd4990a.c: si tengono per non toccare il codice
// verificato (l'unica differenza con lo stub dell'harness e' da dove vengono).
typedef uint8_t  Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;

struct pd4990a_s
{
  int seconds;
  int minutes;
  int hours;
  int days;
  int month;
  int year;
  int weekday;
};

void pd4990a_init(void);
void pd4990a_addretrace(void);
int  read_4990_testbit(void);
int  read_4990_databit(void);
void write_4990_control_w(Uint32 address, Uint32 data);
void pd4990a_increment_day(void);
void pd4990a_increment_month(void);

#ifdef __cplusplus
}
#endif

#endif // PD4990A_H
