// ============================================================================
// eeprom93c46.cpp — vedi l'header per il protocollo e per il collegamento
// sul CPS-2.
// ============================================================================
#include "eeprom93c46.h"
#include <string.h>

enum {
    EE_ATTESA_START = 0,   // CS alto, si aspetta il bit di start
    EE_COMANDO,            // 2 bit di comando + 6 di indirizzo
    EE_SCRIVI_DATI,        // 16 bit in ingresso
    EE_LEGGI_DATI,         // 16 bit in uscita (piu' uno fittizio davanti)
};

void ee93c46_reset(Eeprom93C46 *e, int vergine)
{
    memset(e, 0, sizeof(*e));
    if (vergine)
        for (int i = 0; i < 64; i++) e->mem[i] = 0xFFFF;
    // a riposo la linea dati sta alta: e' il "sono pronta" del 93C46
    e->dout = 1;
}

static void esegui_comando(Eeprom93C46 *e)
{
    const int op   = (e->shift >> 6) & 3;
    const int addr = e->shift & 0x3F;
    e->addr = (uint8_t)addr;

    switch (op) {
    case 2:                                  // 10 = LEGGI
        e->dati  = e->mem[addr];
        e->nbit  = 0;
        e->stato = EE_LEGGI_DATI;
        // il 93C46 manda uno 0 fittizio prima dei 16 bit veri
        e->dout  = 0;
        break;
    case 1:                                  // 01 = SCRIVI
        e->dati  = 0;
        e->nbit  = 0;
        e->stato = EE_SCRIVI_DATI;
        break;
    case 3:                                  // 11 = CANCELLA
        if (e->wen) e->mem[addr] = 0xFFFF;
        e->stato = EE_ATTESA_START;
        e->dout  = 1;                        // pronta
        break;
    default:                                 // 00 = comando esteso
        switch ((addr >> 4) & 3) {
        case 0: e->wen = 0; e->stato = EE_ATTESA_START; e->dout = 1; break;   // EWDS
        case 3: e->wen = 1; e->stato = EE_ATTESA_START; e->dout = 1; break;   // EWEN
        case 2:                                                               // ERAL
            if (e->wen) for (int i = 0; i < 64; i++) e->mem[i] = 0xFFFF;
            e->stato = EE_ATTESA_START; e->dout = 1;
            break;
        case 1:                                                               // WRAL
            e->dati = 0; e->nbit = 0; e->stato = EE_SCRIVI_DATI;
            break;
        }
        break;
    }
}

void ee93c46_write_lines(Eeprom93C46 *e, int cs, int clk, int di)
{
    cs = cs ? 1 : 0; clk = clk ? 1 : 0; di = di ? 1 : 0;

    if (!cs) {
        // CS basso: fine ciclo, si torna in attesa e la linea dati sta alta
        e->cs = 0; e->clk = clk; e->di = di;
        e->stato = EE_ATTESA_START;
        e->nbit  = 0;
        e->dout  = 1;
        return;
    }

    const int salita = (cs && !e->clk && clk);   // fronte di SALITA del clock
    e->cs = 1; e->di = di;
    const int clk_prec = e->clk;
    e->clk = clk;
    (void)clk_prec;
    if (!salita) return;

    switch (e->stato) {
    case EE_ATTESA_START:
        if (di) {                       // il bit di start e' un 1
            e->stato = EE_COMANDO;
            e->nbit  = 0;
            e->shift = 0;
        }
        break;

    case EE_COMANDO:
        e->shift = (uint16_t)((e->shift << 1) | di);
        if (++e->nbit == 8)             // 2 di comando + 6 di indirizzo
            esegui_comando(e);
        break;

    case EE_SCRIVI_DATI:
        e->dati = (uint16_t)((e->dati << 1) | di);
        if (++e->nbit == 16) {
            if (e->wen) {
                if (((e->shift >> 6) & 3) == 0) {      // era una WRAL
                    for (int i = 0; i < 64; i++) e->mem[i] = e->dati;
                } else {
                    e->mem[e->addr] = e->dati;
                }
            }
            e->stato = EE_ATTESA_START;
            e->dout  = 1;               // pronta
        }
        break;

    case EE_LEGGI_DATI:
        // i 16 bit escono dal piu' significativo, uno per fronte
        e->dout = (uint8_t)((e->dati >> 15) & 1);
        e->dati = (uint16_t)(e->dati << 1);
        if (++e->nbit == 16) {
            // il 93C46 continua con la word seguente se il clock prosegue
            e->addr = (uint8_t)((e->addr + 1) & 0x3F);
            e->dati = e->mem[e->addr];
            e->nbit = 0;
        }
        break;
    }
}
