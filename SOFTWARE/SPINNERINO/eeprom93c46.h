#pragma once
#include <stdint.h>

// ============================================================================
// EEPROM seriale 93C46 in modo 16 BIT — 64 word da 16 bit, indirizzo a 6 bit.
//
// Serve al CPS-2 (1944): il gioco ci tiene le impostazioni e i punteggi, e se
// non risponde mostra "EEPROM ERROR" e non va avanti.
//
// Collegamento sul CPS-2 (cps2.cpp, porta EEPROMOUT e IN2):
//   scrittura a 0x804040:  bit 12 = DI   bit 13 = CLK   bit 14 = CS
//   lettura   da 0x804020: bit 0  = DO
// tutti attivi ALTI.
//
// Protocollo (quello standard del 93C46): con CS alto, a ogni fronte di SALITA
// del clock entra un bit da DI. La sequenza e' bit di START a 1, poi 2 bit di
// comando e 6 di indirizzo:
//   10 = LEGGI      (poi escono da DO un bit fittizio e 16 bit di dato)
//   01 = SCRIVI     (poi entrano 16 bit di dato)
//   11 = CANCELLA   (mette 0xFFFF)
//   00 = comando esteso, deciso dai due bit ALTI dell'indirizzo:
//        00 EWDS (blocca la scrittura)   01 WRAL (scrive ovunque)
//        10 ERAL (cancella tutto)        11 EWEN (sblocca la scrittura)
// Dopo l'accensione la scrittura e' BLOCCATA finche' non arriva un EWEN.
// ============================================================================

struct Eeprom93C46 {
    uint16_t mem[64];
    uint8_t  cs, clk, di, dout;   // dout = quello che il gioco legge
    uint8_t  wen;                 // scrittura sbloccata (EWEN)
    uint8_t  stato;               // vedi EE_*
    uint8_t  nbit;
    uint16_t shift;               // comando/indirizzo in arrivo
    uint16_t dati;                // dato in arrivo o in uscita
    uint8_t  addr;
};

// Azzera lo stato. `vergine` != 0 riempie la memoria di 0xFFFF, cioe' una
// EEPROM mai scritta: il gioco se ne accorge e ci scrive i suoi valori.
void ee93c46_reset(Eeprom93C46 *e, int vergine);

// Le tre linee, come le scrive il gioco. Il lavoro si fa sul fronte di salita
// del clock; il resto sono solo cambi di livello.
void ee93c46_write_lines(Eeprom93C46 *e, int cs, int clk, int di);

// Il bit che il gioco legge (0 o 1).
static inline int ee93c46_do(const Eeprom93C46 *e) { return e->dout ? 1 : 0; }
