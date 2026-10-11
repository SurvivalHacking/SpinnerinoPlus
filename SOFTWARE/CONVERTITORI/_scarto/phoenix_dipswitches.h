#ifndef PHOENIX_DIPSWITCHES_H
#define PHOENIX_DIPSWITCHES_H
// Default DSW0 MAME ufficiale (verificato src/mame/phoenix/phoenix.cpp):
//   bit 0,1 = lives    : 0x00 = 3 lives
//   bit 2,3 = bonus    : 0x00 = 3K/30K
//   bit 4   = coinage  : 0x00 = 1C/1C  (0x10 = 2C/1C)
//   bit 5   = unknown  : 0x20 = Off    (0x00 = On — causa drift!)
//   bit 6   = unknown  : 0x40 = Off    (0x00 = On — causa drift!)
//   bit 7   = VBLANK   : dinamico (gestito in rdZ80)
// Default ufficiale MAME = 0x60 (lives=3, bonus 3K/30K, 1C/1C, both DIPs Off).
// CRITICO: vecchio valore 0x10 metteva i 2 DIP unknown su "On", attivando
// un comportamento non documentato che causava DRIFT della nave a sinistra.
#define PHOENIX_DSW0  0x60
#endif
