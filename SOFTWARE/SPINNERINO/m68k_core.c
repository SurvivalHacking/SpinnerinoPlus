// ============================================================================
// m68k_core.c — wrapper ROOT per il core Motorola 68000 (Musashi v4.60).
//
// Stessa convenzione di m6809_core.c: Arduino IDE compila solo i sorgenti nella
// cartella dello sketch, quindi i .c sotto cpus/m68k/ verrebbero ignorati. Qui
// li tiriamo dentro con delle #include, in UNA sola unita' di compilazione.
//
// Perche' anche softfloat: m68kcpu.c include m68kfpu.c (incondizionatamente) e
// m68kcpu.h include softfloat/softfloat.h. Sul 68000 la FPU non esiste e quelle
// funzioni non vengono MAI chiamate — restano li' come codice morto, che il
// linker con -ffunction-sections/--gc-sections butta via.
//
// Le callback di memoria (m68k_read_memory_8/16/32, m68k_write_memory_*) e
// ghouls_int_ack sono definite in ghouls.cpp: Musashi le dichiara GLOBALI e
// senza contesto, quindi -- esattamente come per lo Z80 di Marat (z80_bus.cpp)
// -- puo' esistere UN SOLO gioco a 68000 nel binario. Se un domani ne servisse
// un secondo, va fatto un m68k_bus.cpp che smista su puntatori a funzione.
// ============================================================================
// ⚡ Arduino compila TUTTO a -Os (ottimizza la dimensione, non la velocita'):
//    `compiler.optimization_flags=-Os` in platform.txt. Su un interprete di CPU,
//    che e' un solo switch gigante percorso 31.554 volte per frame, la differenza
//    con -O2 e' sostanziosa. Il pragma vale solo per questa unita' (tutto
//    Musashi), quindi non gonfia il resto del binario.
//    ⚠️ Ne beneficia anche Ghouls: il core 68000 e' lo stesso.
#pragma GCC optimize("O2")

#include "cpus/m68k/m68kcpu.c"
#include "cpus/m68k/m68kops.c"
#include "cpus/m68k/softfloat/softfloat.c"
