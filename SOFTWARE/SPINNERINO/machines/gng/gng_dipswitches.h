#ifndef GNG_DIPSWITCHES_H
#define GNG_DIPSWITCHES_H

// Ghosts'n Goblins DIP switches (from MAME gng.cpp INPUT_PORTS_START).
// Active-low: bit = 1 means switch OFF. MAME's default state uses all
// switches in OFF position except "Demo Sounds" which defaults to ON (0x00).
//
// DSW1 @ 0x3003:
//   0x0F: Coinage          default 0x0F = 1C_1C
//   0x10: Coinage affects  default 0x10 = Coin A
//   0x20: Demo Sounds      default 0x00 = ON
//   0x40: Service Mode     default 0x40 = OFF
//   0x80: Flip Screen      default 0x80 = OFF
//
// DSW2 @ 0x3004:
//   0x03: Lives            default 0x03 = 3
//   0x04: Cabinet          default 0x00 = Upright  (0x04 = Cocktail!)
//   0x18: Bonus Life       default 0x18 = 20K / 70K / every 70K
//   0x60: Difficulty       default 0x60 = Normal
//   0x80: unused           default 0x80

#define GNG_DSW1  0xDF   // MAME default: 1C1C, demo-sounds ON, upright, no flip
#define GNG_DSW2  0xFB   // MAME default: 3 lives, UPRIGHT, 20K/70K, normal

#endif
