#ifndef COMMANDO_DIPSWITCHES_H
#define COMMANDO_DIPSWITCHES_H

// Commando DIPs (MAME factory defaults).  rdZ80 returns ~DIP_X, so these are
// the BIT-INVERTED values of the port read expected by the ROM.
//
// DSW1 default port read = 0x03 | 0x0C | 0x30 | 0xC0 = 0xFF
//   → Starting Area Forest 1, 3 lives, Coin_B 1C/1C, Coin_A 1C/1C
// DSW2 default port read = 0x07 | 0x08 | 0x10 | 0x00 | 0x00 = 0x1F
//   → Bonus 10K 50K+, Demo Sounds ON, Normal difficulty, Flip OFF, Upright

#define COMMANDO_DIP_A  0x00   // ~0x00 = 0xFF read
#define COMMANDO_DIP_B  0xE0   // ~0xE0 = 0x1F read

#endif
