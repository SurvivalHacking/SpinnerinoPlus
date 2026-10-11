#ifndef _mooncresta_dipswitches_h_
#define _mooncresta_dipswitches_h_

// Moon Cresta DIP switches (from MAME mooncrst input ports)

// IN2 bits 0-1: Coin A
//   00 = 1 Coin / 1 Play
//   01 = 1 Coin / 2 Plays
//   10 = 2 Coins / 1 Play
//   11 = 3 Coins / 1 Play (Free Play on some sets)
#define MOONCRESTA_DIP_COIN_1C1P    0b00000000
#define MOONCRESTA_DIP_COIN_1C2P    0b00000001
#define MOONCRESTA_DIP_COIN_2C1P    0b00000010
#define MOONCRESTA_DIP_COIN_3C1P    0b00000011

// IN2 bit 2: Coin B (0=1C/3C, 1=1C/5C)
#define MOONCRESTA_DIP_COINB_1C3C   0b00000000
#define MOONCRESTA_DIP_COINB_1C5C   0b00000100

// IN2 bit 3: unused (0)

// IN2 bits 4-5: Lives
//   00 = 3 lives
//   01 = 4 lives
//   10 = 5 lives
//   11 = Free Play
#define MOONCRESTA_DIP_LIVES_3      0b00000000
#define MOONCRESTA_DIP_LIVES_4      0b00010000
#define MOONCRESTA_DIP_LIVES_5      0b00100000
#define MOONCRESTA_DIP_LIVES_FREE   0b00110000

// IN2 bits 6-7: Unknown (default 0)

// IN1 bit 6: Cabinet/Upright (game may check this even though MAME marks unknown)
#define MOONCRESTA_DIP_IN1  0x40
// IN2: 1C/1P, 5 lives
#define MOONCRESTA_DIP_IN2  (MOONCRESTA_DIP_COIN_1C1P | MOONCRESTA_DIP_LIVES_5)

#endif
