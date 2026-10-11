// Fantasy DIP switches - active HIGH @ $2106 (snk6502_generic_joy8way + fantasyu)
// bit 0=Cabinet (1=Upright/0=Cocktail), bit 1-3=Coinage, bit 4-5=Lives,
// bit 6=Coinage Bonus, bit 7=Allow_Continue (0=Yes per fantasyu).
// Default 0x01: Upright + 1C/1C + 3 lives + Continue=Yes.
// CRITICAL: bit 0=1 (Upright) altrimenti la mongolfiera vola da sola.
#ifndef FANTASY_DSW
#define FANTASY_DSW 0x01
#endif
