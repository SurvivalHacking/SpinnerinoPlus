// Nibbler DIP switches (read at $2106) - ACTIVE HIGH (MAME snk6502.cpp)
// bit 0-1: Lives (00=3, 01=4, 10=5, 11=6)
// bit 2:   Difficulty (0=Easy, 1=Hard)
// bit 3:   Cabinet (0=Upright, 1=Cocktail)
// bit 4:   Service Mode (0=Off, 1=On)  ← 1 = ENTRA IN TEST, NON in gioco!
// bit 5:   Free Play (0=Off, 1=On)
// bit 6-7: Coinage (00=1C/1Cr, 01=1C/1Cr 2/3, 10=2C/1Cr, 11=2C/1Cr 4/3)
// Default: 3 lives, Easy, Upright, Service OFF, Free Play OFF, 1 Coin / 1 Credit
#define NIBBLER_DSW  0x00
