// ============================================================================
// machines/tempest/mathbox.cpp — Atari Math Box per Tempest (e Battlezone/RB)
//
// Port quasi 1:1 di MAME src/mame/atari/mathbox.cpp (Eric Smith, BSD-3-Clause).
// Originale = simulazione function-based (NON emulazione 2901 bit-slice):
// riconosce il "comando" dall'offset di scrittura 0x00..0x1F a 0x6080-0x609F
// e calcola il risultato in C nativo. ~300 righe portabili integralmente.
//
// Hookup CPU 6502 (Tempest):
//   write 0x6080+offs, val  →  mathbox_go(offs, val)
//   read  0x6060            →  mathbox_lo()
//   read  0x6070            →  mathbox_hi()
//   read  0x6040            →  0 (sempre done, non-busy)
//
// I comandi 0x0B/0x12/0x13/0x14 sono moltiplicazioni/proiezioni 3D usate per
// disegnare il tunnel rotante e il movimento dell'astronave attract.
// ============================================================================
#include <Arduino.h>
#include <string.h>
#include <stdint.h>

#ifdef ENABLE_TEMPEST

static int16_t  mb_reg[16];
static int16_t  mb_result;

#define REG0  mb_reg[0x00]
#define REG1  mb_reg[0x01]
#define REG2  mb_reg[0x02]
#define REG3  mb_reg[0x03]
#define REG4  mb_reg[0x04]
#define REG5  mb_reg[0x05]
#define REG6  mb_reg[0x06]
#define REG7  mb_reg[0x07]
#define REG8  mb_reg[0x08]
#define REG9  mb_reg[0x09]
#define REGa  mb_reg[0x0a]
#define REGb  mb_reg[0x0b]
#define REGc  mb_reg[0x0c]
#define REGd  mb_reg[0x0d]
#define REGe  mb_reg[0x0e]
#define REGf  mb_reg[0x0f]

void mathbox_reset() {
  mb_result = 0;
  memset(mb_reg, 0, sizeof(mb_reg));
}

uint8_t mathbox_lo()      { return  (uint8_t)(mb_result & 0xFF); }
uint8_t mathbox_hi()      { return  (uint8_t)((mb_result >> 8) & 0xFF); }
uint8_t mathbox_status()  { return 0x00; /* sempre ready */ }

void mathbox_go(uint8_t offset, uint8_t data) {
  int32_t mb_temp;
  int16_t mb_q;
  int     msb;

  switch (offset) {
    case 0x00: mb_result = REG0 = (REG0 & 0xff00) | data;        break;
    case 0x01: mb_result = REG0 = (REG0 & 0x00ff) | (data << 8); break;
    case 0x02: mb_result = REG1 = (REG1 & 0xff00) | data;        break;
    case 0x03: mb_result = REG1 = (REG1 & 0x00ff) | (data << 8); break;
    case 0x04: mb_result = REG2 = (REG2 & 0xff00) | data;        break;
    case 0x05: mb_result = REG2 = (REG2 & 0x00ff) | (data << 8); break;
    case 0x06: mb_result = REG3 = (REG3 & 0xff00) | data;        break;
    case 0x07: mb_result = REG3 = (REG3 & 0x00ff) | (data << 8); break;
    case 0x08: mb_result = REG4 = (REG4 & 0xff00) | data;        break;
    case 0x09: mb_result = REG4 = (REG4 & 0x00ff) | (data << 8); break;

    case 0x0a: mb_result = REG5 = (REG5 & 0xff00) | data;        break;
    // 0x0b: load REG5 high + esegui sequenza moltiplicazioni proiezione
    case 0x0c: mb_result = REG6 = data; break;

    case 0x15: mb_result = REG7 = (REG7 & 0xff00) | data;        break;
    case 0x16: mb_result = REG7 = (REG7 & 0x00ff) | (data << 8); break;

    case 0x1a: mb_result = REG8 = (REG8 & 0xff00) | data;        break;
    case 0x1b: mb_result = REG8 = (REG8 & 0x00ff) | (data << 8); break;

    case 0x0d: mb_result = REGa = (REGa & 0xff00) | data;        break;
    case 0x0e: mb_result = REGa = (REGa & 0x00ff) | (data << 8); break;
    case 0x0f: mb_result = REGb = (REGb & 0xff00) | data;        break;
    case 0x10: mb_result = REGb = (REGb & 0x00ff) | (data << 8); break;

    case 0x17: mb_result = REG7; break;
    case 0x19: mb_result = REG8; break;
    case 0x18: mb_result = REG9; break;

    case 0x0b: {
      REG5 = (REG5 & 0x00ff) | (data << 8);
      REGf = (int16_t)0xffff;
      REG2 = REG2;  // (no-op kept for clarity)
      REG4 = REG4 - REG2;
      REG5 = REG5 - REG3;

    step_048:
      mb_temp = ((int32_t) REG0) * ((int32_t) REG4);
      REGc = mb_temp >> 16;
      REGe = mb_temp & 0xffff;

      mb_temp = ((int32_t) -REG1) * ((int32_t) REG5);
      REG7 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG7 += REGc;

      // rounding
      REGe = (REGe >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      mb_q = REGc + REGe;
      if (mb_q < 0)
        REG7++;

      mb_result = REG7;

      if (REGf < 0)
        break;

      REG7 += REG2;

      // fallthrough -> command 0x12
    }
    /* FALLTHROUGH */
    case 0x12: {
      mb_temp = ((int32_t) REG1) * ((int32_t) REG4);
      REGc = mb_temp >> 16;
      REG9 = mb_temp & 0xffff;

      mb_temp = ((int32_t) REG0) * ((int32_t) REG5);
      REG8 = mb_temp >> 16;
      mb_q = mb_temp & 0xffff;

      REG8 += REGc;

      // rounding
      REG9 = (REG9 >> 1) & 0x7fff;
      REGc = (mb_q >> 1) & 0x7fff;
      REG9 += REGc;
      if (REG9 < 0)
        REG8++;
      REG9 <<= 1;

      mb_result = REG8;

      if (REGf < 0)
        break;

      REG8 += REG3;
      REG9 &= 0xff00;

      // fallthrough -> command 0x13
    }
    /* FALLTHROUGH */
    case 0x13:
      REGc = REG9;
      mb_q = REG8;
      goto step_0bf;

    case 0x14:
      REGc = REGa;
      mb_q = REGb;

    step_0bf:
      REGe = REG7 ^ mb_q;
      REGd = mb_q;
      if (mb_q >= 0)
        mb_q = REGc;
      else {
        REGd = - mb_q - 1;
        mb_q = - REGc - 1;
        if ((mb_q < 0) && ((mb_q + 1) < 0))
          REGd++;
        mb_q++;
      }

      // step 0c9: REGc = abs(REG7)
      if (REG7 >= 0) REGc = REG7;
      else           REGc = -REG7;

      REGf = REG6;
      do {
        REGd -= REGc;
        msb = ((mb_q & 0x8000) != 0);
        mb_q <<= 1;
        if (REGd >= 0) mb_q++;
        else           REGd += REGc;
        REGd <<= 1;
        REGd += msb;
      } while (--REGf >= 0);

      if (REGe >= 0) mb_result = mb_q;
      else           mb_result = - mb_q;
      break;

    case 0x11:
      REG5 = (REG5 & 0x00ff) | (data << 8);
      REGf = 0x0000;  // do everything in one step
      goto step_048;

    case 0x1c:
      // window test
      REG5 = (REG5 & 0x00ff) | (data << 8);
      do {
        REGe = (REG4 + REG7) >> 1;
        REGf = (REG5 + REG8) >> 1;
        if ((REGb < REGe) && (REGf < REGe) && ((REGe + REGf) >= 0)) {
          REG7 = REGe; REG8 = REGf;
        } else {
          REG4 = REGe; REG5 = REGf;
        }
      } while (--REG6 >= 0);
      mb_result = REG8;
      break;

    case 0x1d:
      REG3 = (REG3 & 0x00ff) | (data << 8);
      REG2 -= REG0;
      if (REG2 < 0) REG2 = -REG2;
      REG3 -= REG1;
      if (REG3 < 0) REG3 = -REG3;
      // fallthrough -> 0x1e
      /* FALLTHROUGH */
    case 0x1e:
      // result = max(REG2, REG3) + 3/8 * min(REG2, REG3)
      if (REG3 >= REG2) { REGc = REG2; REGd = REG3; }
      else              { REGd = REG2; REGc = REG3; }
      REGc >>= 2;
      REGd += REGc;
      REGc >>= 1;
      mb_result = REGd = (REGc + REGd);
      break;

    case 0x1f:
      // signature/selftest — non implementato
      break;
  }
}

#endif // ENABLE_TEMPEST
