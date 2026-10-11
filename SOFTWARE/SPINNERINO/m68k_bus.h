#pragma once
#include <stdint.h>
// ============================================================================
// m68k_bus — smistamento dei callback GLOBALI di Musashi.
//
// PERCHE' ESISTE: come il core Z80 di Marat (vedi z80_bus.h), Musashi dichiara
// m68k_read_memory_*/m68k_write_memory_* come funzioni GLOBALI senza parametro
// di contesto. Finche' c'era un solo gioco 68000 (ghouls) bastava definirle in
// ghouls.cpp. Con un secondo modulo (outrun) che ne vuole di proprie si avrebbe
// **simbolo duplicato in link**: i due .cpp finiscono nello stesso binario anche
// se a runtime ne gira uno solo.
//
// SOLUZIONE: qui stanno le uniche definizioni globali; smistano su puntatori a
// funzione che il modulo attivo registra con m68k_bus_set() prima di far partire
// la CPU. Costo: una chiamata indiretta per accesso in memoria.
//
// NB per chi emula PIU' 68000 nello stesso gioco (outrun ne ha 2: main e sub):
// il core non dice QUALE CPU sta chiedendo. Si usa lo stesso idiom di
// z80_bus/blktiger: una variabile `cpu corrente` che il modulo aggiorna PRIMA di
// ogni slice di m68k_execute, e le sue callback ci si ramificano sopra.
// ============================================================================

typedef unsigned int (*m68k_rd_fn)(unsigned int addr);
typedef void         (*m68k_wr_fn)(unsigned int addr, unsigned int val);
typedef int          (*m68k_iack_fn)(int level);

struct M68kBus {
    m68k_rd_fn   r8, r16, r32;
    m68k_wr_fn   w8, w16, w32;
    m68k_iack_fn int_ack;      // ack dell'interrupt (di solito abbassa la linea)

    // ---- spazio OPCODE separato dallo spazio DATI (per il CPS-2) ------------
    // Il CPS-2 cifra il PROGRAMMA: il 68000 preleva le ISTRUZIONI da una ROM
    // decifrata e legge i DATI da quella cifrata. Musashi lo sa fare
    // (M68K_SEPARATE_READS, ora acceso in m68kconf.h): con questi campi
    // impostati, le letture immediate e PC-relative passano di qui.
    //
    // ⚠️ **Lasciarli a nullptr non cambia NULLA per gli altri giochi**: cadono
    // sulle r8/r16/r32 normali, che e' esattamente cio' che faceva la macro con
    // SEPARATE_READS spento. Verificato sui flag di m68kconf.h: FC,
    // ADDRESS_ERROR, PREFETCH e PMMU sono tutti OFF, quindi
    // `m68ki_read_program_16(A)` si riduce a `m68k_read_memory_16(A)` e basta.
    m68k_rd_fn   p8, p16, p32;   // nullptr = usa lo spazio dati
};

// Registra le callback del modulo attivo. Chiamare PRIMA di m68k_pulse_reset().
// nullptr ripristina gli stub inerti (letture 0xFF, scritture ignorate).
void m68k_bus_set(const M68kBus *bus);
