// ============================================================================
// supercobra.cpp — Super Cobra (Konami 1981), porting -> SPINNERINO WIFI.
//
// Vedi supercobra.h per la strategia (eredita da scramble, riuso massimo).
// Incluso dal .ino (TU singola), dopo scramble.h/scramble.cpp.
// ============================================================================
#include "config.h"
#include "machines/supercobra/supercobra.h"

#ifdef ENABLE_SUPERCOBRA
#include "emulation/input.h"

// ----------------------------------------------------------------------------
// reset: la base alloca scr_full e azzera tutto lo stato comune. Qui aggiungo
// solo l'azzeramento della protezione specifica scobra.
// ----------------------------------------------------------------------------
void supercobra::reset() {
  scramble::reset();
  sc_prot_state  = 0;
  sc_prot_result = 0;
  // Forza il frame-gating dell'emulazione fin dal 1o frame: niente finestra
  // iniziale non sincronizzata (race render<->emulazione = tearing/scie).
  game_started = 1;
}

// ----------------------------------------------------------------------------
// Protezione scobra (PPI1 port C). Tabella diversa da Scramble: oltre a
// 0x6/0x9/0xB usa anche 0xA(=0x00) e 0xF(=num1-num2). Risultato letto a 0xA002
// e (bit7) a 0x9802.
// ----------------------------------------------------------------------------
void supercobra::sc_protection_write(unsigned char value) {
  sc_prot_state = (sc_prot_state << 4) | (value & 0x0F);
  unsigned char num1 = (sc_prot_state >> 8) & 0x0F;
  unsigned char num2 = (sc_prot_state >> 4) & 0x0F;

  switch (sc_prot_state & 0x0F) {
    case 0x6: sc_prot_result ^= 0x80;                                      break; // scrambles
    case 0x9: sc_prot_result = ((num1 + 1 < 0x0F) ? (num1 + 1) : 0x0F) << 4; break; // scramble
    case 0xB: sc_prot_result = ((num2 > num1) ? (num2 - num1) : 0) << 4;   break; // theend
    case 0xA: sc_prot_result = 0x00;                                       break; // theend
    case 0xF: sc_prot_result = ((num1 > num2) ? (num1 - num2) : 0) << 4;   break; // scrambles
  }
}

// ----------------------------------------------------------------------------
// opcode fetch
// ----------------------------------------------------------------------------
unsigned char supercobra::opZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    if (Addr < SC_MAIN_ROM_SIZE) return supercobra_main_rom[Addr];
  } else {
    if (Addr < SC_AUDIO_ROM_SIZE) return supercobra_audio_rom[Addr];
    if (Addr >= 0x8000 && Addr <= 0x83FF) return snd_ram[Addr & 0x03FF];
  }
  // Fuori ROM: 0x00 (NOP) come galagino-main. 0xFF sarebbe RST38 -> se il PC
  // vagasse fuori ROM corromperebbe lo stack (runaway, schermo garbled).
  return 0x00;
}

// ----------------------------------------------------------------------------
// memory read
// ----------------------------------------------------------------------------
unsigned char supercobra::rdZ80(unsigned short Addr) {
  // --- CPU audio ---
  if (current_cpu == 1) {
    if (Addr < SC_AUDIO_ROM_SIZE) return supercobra_audio_rom[Addr];
    if (Addr >= 0x8000 && Addr <= 0x8FFF) return snd_ram[Addr & 0x03FF];
    return 0xFF;
  }

  // --- CPU main ---
  if (Addr < SC_MAIN_ROM_SIZE)             return supercobra_main_rom[Addr];
  if (Addr >= 0x8000 && Addr <= 0x87FF)    return memory[Addr - 0x8000];
  if (Addr >= 0x8800 && Addr <= 0x8FFF)    return memory[0x0800 + ((Addr - 0x8800) & 0x03FF)]; // VRAM + mirror
  if (Addr >= 0x9000 && Addr <= 0x903F)    return memory[0x0C00 + (Addr - 0x9000)];            // attr
  if (Addr >= 0x9040 && Addr <= 0x905F)    return memory[0x0C40 + (Addr - 0x9040)];            // sprite
  if (Addr >= 0x9060 && Addr <= 0x907F)    return memory[0x0C60 + (Addr - 0x9060)];            // bullet
  if (Addr >= 0x9080 && Addr <= 0x90FF)    return memory[0x0C80 + (Addr - 0x9080)];            // ram2

  switch (Addr) {
    case 0xB000: return 0xFF;  // watchdog

    case 0x9800: {             // PPI0 Port A — IN0
      unsigned char keymask = input->buttons_get();
      unsigned char v = SUPERCOBRA_IN0_VALUE;
      if (coin_read_pulse)       { v &= ~0x80; coin_read_pulse--; } // Coin (1 press = 1 credito)
      if (keymask & BUTTON_LEFT)  v &= ~0x20;
      if (keymask & BUTTON_RIGHT) v &= ~0x10;
      if (keymask & BUTTON_FIRE) { v &= ~0x08; v &= ~0x02; }        // Laser + Bomb
      return v;
    }
    case 0x9801: {             // PPI0 Port B — IN1
      unsigned char keymask = input->buttons_get();
      unsigned char v = SUPERCOBRA_IN1_VALUE;
      if (keymask & BUTTON_START) v &= ~0x80;
      return v;
    }
    case 0x9802: {             // PPI0 Port C — IN2 (+ bit protezione)
      unsigned char keymask = input->buttons_get();
      unsigned char v = ((sc_prot_result >> 7) & 1) ? SUPERCOBRA_IN2_VALUE1 : SUPERCOBRA_IN2_VALUE0;
      if (keymask & BUTTON_DOWN) v &= ~0x40;
      if (keymask & BUTTON_UP)   v &= ~0x10;
      return v;
    }

    case 0xA000:
    case 0xA001: return 0xFF;          // PPI1 A/B
    case 0xA002: return sc_prot_result; // PPI1 C = protezione
  }
  return 0xFF;
}

// ----------------------------------------------------------------------------
// memory write
// ----------------------------------------------------------------------------
void supercobra::wrZ80(unsigned short Addr, unsigned char Value) {
  // --- CPU audio ---
  if (current_cpu == 1) {
    if (Addr >= 0x8000 && Addr <= 0x8FFF) { snd_ram[Addr & 0x03FF] = Value; return; }
    // 0x9000-0x9FFF: filtri sonori HW (non RAM) -> ignora
    return;
  }

  // --- CPU main ---
  if (Addr >= 0x8000 && Addr <= 0x87FF) { memory[Addr - 0x8000] = Value; return; }
  if (Addr >= 0x8800 && Addr <= 0x8FFF) {
    // Prima scrittura VRAM = il gioco sta girando -> abilita il frame-gating
    // dell'emulazione (vedi emulation_frame). SENZA questo, finche' game_started
    // resta 0 l'emulazione gira LIBERA (~250fps) NON sincronizzata col render:
    // race su memory[] = grafica garbled + gioco troppo veloce = comandi persi.
    if (!game_started) game_started = 1;
    memory[0x0800 + ((Addr - 0x8800) & 0x03FF)] = Value;
    return;
  }
  if (Addr >= 0x9000 && Addr <= 0x903F) { memory[0x0C00 + (Addr - 0x9000)] = Value; return; }
  if (Addr >= 0x9040 && Addr <= 0x905F) { memory[0x0C40 + (Addr - 0x9040)] = Value; return; }
  if (Addr >= 0x9060 && Addr <= 0x907F) { memory[0x0C60 + (Addr - 0x9060)] = Value; return; }
  if (Addr >= 0x9080 && Addr <= 0x90FF) { memory[0x0C80 + (Addr - 0x9080)] = Value; return; }

  switch (Addr) {
    case 0xA801:                                            // NMI enable
      nmi_enable = Value & 1;
      if (Value & 1) game_started = 1;                      // backup: gioco in moto -> frame-gate
      return;
    case 0xA802: return;                                    // coin counter
    case 0xA803: background_enable = Value & 1; game_started = 1; return; // sfondo blu
    case 0xA804: stars_enabled = Value & 1; return;         // stelle
    case 0xA806: flip_x = Value & 1; return;
    case 0xA807: flip_y = Value & 1; return;

    case 0xA000: soundlatch = Value; return;                // PPI1 A -> sound latch
    case 0xA001:                                            // PPI1 B -> sound IRQ (rising edge bit3)
      if ((Value & 0x08) && !snd_irq_last) snd_irq_pending = 1;
      snd_irq_last = Value & 0x08;
      return;
    case 0xA002:                                            // PPI1 C -> protezione
      sc_protection_write(Value & 0x0F);
      return;

    case 0x9800: case 0x9801: case 0x9802: case 0x9803:     // PPI0 (write ctrl/port) -> ignora
    case 0xA003:                                            // PPI1 control
    case 0xB000:                                            // watchdog
      return;
  }
}

// ----------------------------------------------------------------------------
// Blit (destinazione rt = strip nativa di scr_full, come scramble). Le gfx sono
// quelle di supercobra; la colormap NON e' pre-swappata, quindi si applica lo
// swap d'indice ((pix&2)>>1)|((pix&1)<<1) come galagino-main.
// ----------------------------------------------------------------------------
void supercobra::blit_tile(short row, char col) {
  if (row < 2 || row >= 34) return;

  unsigned short addr = tileaddr[row][col];
  const unsigned short *tile = supercobra_tilemap[memory[0x0800 + addr]];
  int c = memory[0x0C00 + 2 * (addr & 31) + 1] & 7;
  const unsigned short *colors = supercobra_colormap[c];

  unsigned short *ptr = rt + 8 * col;
  for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = tile[r];
    for (char x = 0; x < 8; x++, pix >>= 2) {
      if (pix & 3) *ptr = colors[((pix & 2) >> 1) | ((pix & 1) << 1)];
      ptr++;
    }
  }
}

void supercobra::blit_tile_scroll(short row, signed char col, unsigned char scroll) {
  if (row < 2 || row >= 34) return;

  unsigned short addr;
  unsigned short mask = 0xFFFF;
  int sub = scroll & 0x07;

  if (col >= 0) {
    addr = tileaddr[row][col];
    addr = (addr + ((scroll & ~7) << 2)) & 1023;
    if (sub && col == 27) mask = 0xFFFF >> (2 * sub);
  } else {
    addr = tileaddr[row][0];
    addr = (addr + 32 + ((scroll & ~7) << 2)) & 1023;
    mask = 0xFFFF << (2 * (8 - sub));
  }

  const unsigned short *tile = supercobra_tilemap[memory[0x0800 + addr]];
  int c = memory[0x0C00 + 2 * (addr & 31) + 1] & 7;
  const unsigned short *colors = supercobra_colormap[c];
  unsigned short *ptr = rt + 8 * col + sub;

  for (char r = 0; r < 8; r++, ptr += (224 - 8)) {
    unsigned short pix = tile[r] & mask;
    for (char x = 0; x < 8; x++, pix >>= 2) {
      if (pix & 3) *ptr = colors[((pix & 2) >> 1) | ((pix & 1) << 1)];
      ptr++;
    }
  }
}

void supercobra::blit_sprite(short row, unsigned char s) {
  const unsigned long *spr = supercobra_spritemap[sprite[s].flags & 3][sprite[s].code];
  const unsigned short *colors = supercobra_colormap[sprite[s].color];

  unsigned long mask = 0xFFFFFFFF;
  if (sprite[s].x < 0)        mask <<= -2 * sprite[s].x;
  if (sprite[s].x > 224 - 16) mask >>= 2 * (sprite[s].x - (224 - 16));

  short y_offset = sprite[s].y - 8 * row;
  unsigned char lines2draw = 8;
  if (y_offset < -8) lines2draw = 16 + y_offset;

  unsigned short startline = 0;
  if (y_offset > 0) { startline = y_offset; lines2draw = 8 - y_offset; }

  if (y_offset < 0) spr -= y_offset;

  unsigned short *ptr = rt + sprite[s].x + 224 * startline;
  for (char r = 0; r < lines2draw; r++, ptr += (224 - 16)) {
    unsigned long pix = *spr++ & mask;
    for (char c = 0; c < 16; c++, pix >>= 2) {
      if (pix & 3) *ptr = colors[((pix & 2) >> 1) | ((pix & 1) << 1)];
      ptr++;
    }
  }
}

const unsigned short *supercobra::logo(void) {
  return supercobra_logo;
}

#endif // ENABLE_SUPERCOBRA
