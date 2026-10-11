// ============================================================================
// m68k_bus.cpp — le UNICHE definizioni dei callback globali di Musashi.
// Vedi m68k_bus.h per il perche'. Nessun altro .cpp deve definire
// m68k_read_memory_* / m68k_write_memory_* / m68k_bus_int_ack.
// ============================================================================
// ⚡ qui passano TUTTE le ~35.000 chiamate al bus per frame di ogni gioco
//    68000: l'IDE compila a -Os, questi thunk vogliono -O2.
#pragma GCC optimize("O2")

#include "m68k_bus.h"

extern "C" {
#include "cpus/m68k/m68k.h"
}

// stub inerti: senza un modulo registrato la CPU legge 0xFF invece di
// dereferenziare un puntatore nullo.
static unsigned int stub_r8 (unsigned int a) { (void)a; return 0xFF; }
static unsigned int stub_r16(unsigned int a) { (void)a; return 0xFFFF; }
static unsigned int stub_r32(unsigned int a) { (void)a; return 0xFFFFFFFF; }
static void stub_w(unsigned int a, unsigned int v) { (void)a; (void)v; }
static int  stub_iack(int level) { (void)level; return M68K_INT_ACK_AUTOVECTOR; }

static M68kBus g_bus = { stub_r8, stub_r16, stub_r32,
                         stub_w,  stub_w,   stub_w, stub_iack,
                         nullptr, nullptr,  nullptr };

void m68k_bus_set(const M68kBus *bus)
{
    if (!bus) {
        g_bus = (M68kBus){ stub_r8, stub_r16, stub_r32,
                           stub_w, stub_w, stub_w, stub_iack,
                           nullptr, nullptr, nullptr };
        return;
    }
    g_bus = *bus;
    // un campo lasciato a nullptr sarebbe un crash silenzioso al primo accesso
    if (!g_bus.r8)      g_bus.r8      = stub_r8;
    if (!g_bus.r16)     g_bus.r16     = stub_r16;
    if (!g_bus.r32)     g_bus.r32     = stub_r32;
    if (!g_bus.w8)      g_bus.w8      = stub_w;
    if (!g_bus.w16)     g_bus.w16     = stub_w;
    if (!g_bus.w32)     g_bus.w32     = stub_w;
    if (!g_bus.int_ack) g_bus.int_ack = stub_iack;
    // ⭐ Lo spazio OPCODE, se il modulo non lo dichiara, E' lo spazio dati:
    // e' esattamente il comportamento di prima, quando SEPARATE_READS era
    // spento e la macro rimandava alle letture normali.
    if (!g_bus.p8)      g_bus.p8      = g_bus.r8;
    if (!g_bus.p16)     g_bus.p16     = g_bus.r16;
    if (!g_bus.p32)     g_bus.p32     = g_bus.r32;
}

// ⛔⛔⛔ INDIRIZZO PARI FORZATO SUGLI ACCESSI A 16 E 32 BIT — la correzione
// piu' importante di tutta questa caccia (2026-08-10), e sta QUI apposta: e' il
// punto unico da cui passano TUTTI i giochi a 68000, presenti e futuri.
//
// IL DIFETTO: `m68kconf.h:82` ha `M68K_EMULATE_ADDRESS_ERROR  M68K_OPT_OFF`.
// Su un 68000 vero un accesso a WORD a indirizzo DISPARI e' un ADDRESS ERROR e
// non tocca la memoria; con l'emulazione spenta, invece, l'indirizzo arriva
// intatto ai gestori dei giochi, che fanno tutti la stessa cosa:
//     m[o] = v >> 8;  m[o + 1] = v;
// Le maschere garantiscono `o <= DIMENSIONE-1`, **non** `o+1 <= DIMENSIONE-1`:
// con `o` sull'ultimo byte di una regione si scrive **UN BYTE OLTRE il blocco
// allocato**. Esempi reali: `0x067FFF` -> `workram[0x8000]` (Out Run),
// `0xFFFFFF` -> `mainram[0x10000]` (Ghouls), `0x92FFFF` -> `gfxram[0x30000]`
// (1944 e Ghouls).
//
// ⭐ PERCHE' SPIEGA IL SINTOMO MEGLIO DI OGNI ALTRA IPOTESI: quel byte cade
// nell'INTESTAZIONE del blocco heap successivo. Il panico NON arriva quando si
// scrive, ma alla prima malloc/free che tocca quel blocco — che puo' capitare
// in un task qualunque, anche minuti dopo. Ecco perche' il marcatore di fase
// dava numeri SPARSI (F8, F9, F21, F25 su Out Run; F9, F12 su 1944): fotografava
// dove si trovava l'emulazione in quell'istante, non dov'era il difetto. Ed ecco
// perche' e' intermittente e colpisce tutti e tre i giochi.
//
// ⚠️ Non cambia NIENTE nel funzionamento legittimo: un gioco sano non fa mai
// accessi a word dispari (sull'hardware vero si pianterebbe), e per quelli a 32
// bit il 68000 pretende comunque l'allineamento pari. Si perde solo la
// possibilita' di corrompere l'heap.
#define PARI(a) ((a) & ~1u)

extern "C" {

unsigned int m68k_read_memory_8 (unsigned int a) { return g_bus.r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return g_bus.r16(PARI(a)); }
unsigned int m68k_read_memory_32(unsigned int a) { return g_bus.r32(PARI(a)); }

void m68k_write_memory_8 (unsigned int a, unsigned int v) { g_bus.w8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { g_bus.w16(PARI(a), v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { g_bus.w32(PARI(a), v); }

// ---- spazio OPCODE (M68K_SEPARATE_READS acceso) ---------------------------
// Musashi passa qui gli indirizzi GIA' mascherati con ADDRESS_68K(), quindi si
// inoltra e basta. Con p* non impostati puntano alle r* e il comportamento e'
// identico a prima.
unsigned int m68k_read_immediate_16(unsigned int a)  { return g_bus.p16(PARI(a)); }
unsigned int m68k_read_immediate_32(unsigned int a)  { return g_bus.p32(PARI(a)); }
unsigned int m68k_read_pcrelative_8 (unsigned int a) { return g_bus.p8(a);  }
unsigned int m68k_read_pcrelative_16(unsigned int a) { return g_bus.p16(PARI(a)); }
unsigned int m68k_read_pcrelative_32(unsigned int a) { return g_bus.p32(PARI(a)); }

// Chiamata da m68kconf.h (M68K_INT_ACK_CALLBACK).
int m68k_bus_int_ack(int level) { return g_bus.int_ack(level); }

} // extern "C"
