// ============================================================================
// nes_vs.cpp — motore Nintendo VS. UniSystem. Vedi nes_vs.h.
//
// Trascritto da MAME (nessuna riga inventata):
//   src/devices/video/ppu2c0x.cpp   -> registri, draw_background, draw_tile,
//                                      draw_sprites, sprite-0 hit, loopy
//   src/mame/nintendo/vsnes.cpp     -> mappa CPU, mapper (Namco) 108,
//                                      protezione supxevs, input $4016/$4017
// I riferimenti puntuali sono nei commenti, funzione per funzione.
// ============================================================================
// ⚡ L'IDE Arduino compila tutto a **-Os**. Questo file e' il percorso caldo del
// gioco (il ciclo dei pixel gira 61.440 volte per quadro): a -O2 il compilatore
// srotola il ciclo da 8 pixel e tiene i puntatori nei registri. Stessa mossa
// gia' usata sui file caldi del 68000 in CABINATONE.
#pragma GCC optimize("O2")

#include "nes_vs.h"
#include <string.h>

// --- bit dei registri PPU (ppu2c0x.h, enum) ---------------------------------
#define CTRL0_INC          0x04
#define CTRL0_SPR_SELECT   0x08
#define CTRL0_CHR_SELECT   0x10
#define CTRL0_SPRITE_SIZE  0x20
#define CTRL0_NMI          0x80

#define CTRL1_MONO         0x01
#define CTRL1_BACKGROUND_L8 0x02
#define CTRL1_SPRITES_L8   0x04
#define CTRL1_BACKGROUND   0x08
#define CTRL1_SPRITES      0x10

#define STATUS_8SPRITES    0x20
#define STATUS_SPRITE0_HIT 0x40
#define STATUS_VBLANK      0x80

// maschere di ppu2c0x_device (valori NES di default)
#define VIDEORAM_ADDR_MASK 0x3fff
#define GLOBAL_REFRESH_MASK 0x7fff

// ============================================================================
// Spazio della PPU. Mappa da vsnes_ppu1_map():
//   $0000-$1FFF  CHR ROM, 8 banchi da 1 KB (mapper 108)
//   $2000-$2FFF  4 KB di nametable RAM, mirror a $3000  -> il VS. UniSystem ha
//                la RAM per QUATTRO schermate: niente mirroring orizzontale/
//                verticale da emulare.
//   $3F00-$3FFF  palette RAM (gestita a parte, come in MAME)
// ============================================================================
static inline uint8_t ppu_readbyte(nes_vs_t *n, uint16_t addr) {
  addr &= 0x3fff;
  if (addr < 0x2000)
    return n->chr_fast[((uint32_t)n->chr_bank[(addr >> 10) & 7] << 10) | (addr & 0x3ff)];
  if (addr < 0x3f00)
    return n->ram[NESOFF_VRAM + (addr & 0x0fff)];
  return n->pal_ram[addr & 0x1f];
}

// palette_write() di ppu2c0x.cpp, verbatim: i pen trasparenti sono specchiati.
// In piu' si aggiorna `pal_rgb`, la copia gia' convertita in RGB565 che usa il
// renderer: la conversione si paga QUI (poche decine di volte per quadro)
// invece che a ogni pixel.
static inline void ppu_palette_write(nes_vs_t *n, uint16_t offset, uint8_t data) {
  data &= 0x3f;                       // la palette RAM e' larga 6 bit
  if (offset & 0x3) {
    const uint8_t i = offset & 0x1f;
    n->pal_ram[i] = data;
    n->pal_rgb[i] = n->pal64[data];
  } else {
    if (0 == (offset & 0xf)) {
      n->back_color = data;
      n->back_rgb = n->pal64[data];
    }
    const uint8_t i = offset & 0xf;
    n->pal_ram[i] = n->pal_ram[i + 0x10] = data;
    n->pal_rgb[i] = n->pal_rgb[i + 0x10] = n->pal64[data];
  }
}

static inline void ppu_writebyte(nes_vs_t *n, uint16_t addr, uint8_t data) {
  addr &= 0x3fff;
  if (addr < 0x2000)
    return;                            // CHR e' ROM: le scritture cadono
  if (addr < 0x3f00) {
    n->ram[NESOFF_VRAM + (addr & 0x0fff)] = data;
    return;
  }
  ppu_palette_write(n, addr & 0x1f, data);
}

// ============================================================================
// Rendering di una scanline — ppu2c0x.cpp: draw_tile / draw_background /
// draw_sprites / render_scanline.
// line_priority: bit0 = "qui c'e' gia' uno sprite", bit1 = "qui il BG e' opaco"
// (e' il bit che fa scattare lo sprite-0 hit).
// ============================================================================
// ⚡ Espansione 2bpp con tabella: `exp0[p0] | exp1[p1]` mette i due bit di ogni
// pixel in una word (2 bit per pixel, il piu' a sinistra nei bit 15-14). Toglie
// dal ciclo interno l'estrazione bit a bit E lo scorrimento dei due piani.
// Costruita a runtime in RAM: 1 KB, e non e' un dato da tenere in flash.
static uint16_t nes_exp0[256], nes_exp1[256];
static bool     nes_exp_ready = false;

static void nes_exp_init(void) {
  if (nes_exp_ready) return;
  for (int v = 0; v < 256; v++) {
    uint16_t a = 0, b = 0;
    for (int i = 0; i < 8; i++) {
      const uint16_t bit = (uint16_t)((v >> (7 - i)) & 1);
      a |= (uint16_t)(bit << (14 - 2 * i));   // piano 0 -> bit BASSO del pixel
      b |= (uint16_t)(bit << (15 - 2 * i));   // piano 1 -> bit ALTO
    }
    nes_exp0[v] = a;
    nes_exp1[v] = b;
  }
  nes_exp_ready = true;
}

static void draw_tile(nes_vs_t *n, uint8_t *line_priority, int color_byte,
                      int color_bits, int address, int start_x,
                      uint16_t back_pen, uint16_t **dest) {
  const int color = (color_byte >> color_bits) & 0x03;

  const uint8_t plane0 = ppu_readbyte(n, address & 0x1fff);
  const uint8_t plane1 = ppu_readbyte(n, (address + 8) & 0x1fff);
  uint16_t row = (uint16_t)(nes_exp0[plane0] | nes_exp1[plane1]);

  // ⚡ I quattro colori del tile calcolati UNA volta, non a ogni pixel. Sono
  // costanti per tutta la scanline: `pal_rgb`/`back_rgb` cambiano solo in
  // ppu_palette_write, che parte da cpu_write — e la CPU gira DOPO il
  // rendering della riga (vedi nes_vs_run_frame).
  const uint16_t c4 = (uint16_t)(4 * color);
  const uint16_t pal4[4] = {
    back_pen,
    n->pal_rgb[(c4 + 1) & 0x1f],
    n->pal_rgb[(c4 + 2) & 0x1f],
    n->pal_rgb[(c4 + 3) & 0x1f],
  };

  uint16_t *d = *dest;

  if (start_x >= 0 && start_x <= NES_W - 8) {
    // ⚡ Percorso veloce: tutti e 8 i pixel sono dentro lo schermo, quindi il
    // controllo dei bordi esce dal ciclo. Vale per tutti i tile tranne il primo
    // e l'ultimo di ogni riga.
    uint8_t *lp = line_priority + start_x;
    for (int i = 0; i < 8; i++) {
      const uint8_t pix = (uint8_t)((row >> 14) & 3);
      row = (uint16_t)(row << 2);
      d[i] = pal4[pix];
      // `=` invece di `|=`: line_priority arriva azzerato da render_scanline e
      // ogni colonna e' scritta da UN SOLO tile (start_x cresce di 8 e non
      // torna indietro), quindi il byte finale e' lo stesso.
      lp[i] = pix ? 0x02 : 0x00;
    }
    d += 8;
  } else {
    for (int i = 0; i < 8; i++) {
      const uint8_t pix = (uint8_t)((row >> 14) & 3);
      row = (uint16_t)(row << 2);
      if ((start_x + i) >= 0 && (start_x + i) < NES_W) {
        *d = pal4[pix];
        if (pix)
          line_priority[start_x + i] |= 0x02;
      }
      d++;
    }
  }

  *dest = d;
}

static void draw_background(nes_vs_t *n, uint8_t *line_priority) {
  // Da draw_background(): la posizione nella nametable esce tutta dal registro
  // "loopy" refresh_data, che il rendering aggiorna riga per riga.
  const uint8_t  scroll_x_coarse = n->refresh_data & 0x001f;
  const uint8_t  scroll_y_coarse = (n->refresh_data & 0x03e0) >> 5;
  const uint16_t nametable       = n->refresh_data & 0x0c00;
  const uint8_t  scroll_y_fine   = (n->refresh_data & 0x7000) >> 12;

  int x = scroll_x_coarse;
  int tile_index = (nametable | 0x2000) + scroll_y_coarse * 32;

  int start_x = (n->x_fine ^ 0x07) - 7;
  uint16_t *dest = n->fb + n->scanline * NES_W + start_x;

  int tilecount = 0;
  while (tilecount < 34) {
    const int index1 = tile_index + x;
    const int page2 = ppu_readbyte(n, index1);

    // attribute table: un byte ogni 4x4 tile
    const int pos = ((index1 & 0x380) >> 4) | ((index1 & 0x1f) >> 2);
    const int page = (index1 & 0x0c00) >> 10;
    int address = 0x3c0 + pos;
    const int color_byte = ppu_readbyte(n, (((page * 0x400) + address) & 0xfff) + 0x2000);
    const int color_bits = ((index1 & 0x40) >> 4) + (index1 & 0x02);

    if (start_x < NES_W) {
      address = (n->tile_page ? 0x1000 : 0) + (page2 * 16) + scroll_y_fine;
      draw_tile(n, line_priority, color_byte, color_bits, address, start_x,
                n->back_rgb, &dest);
      start_x += 8;

      x++;
      if (x > 31) {
        x = 0;
        tile_index ^= 0x400;
      }
    }
    tilecount++;
  }

  // se gli 8 pixel di sinistra del BG sono spenti, cancellali
  if (!(n->ppu_ctrl1 & CTRL1_BACKGROUND_L8)) {
    dest = n->fb + n->scanline * NES_W;
    for (int i = 0; i < 8; i++) {
      *dest++ = n->back_rgb;
      line_priority[i] ^= 0x02;
    }
  }
}

static void draw_background_pen(nes_vs_t *n) {
  uint16_t *dest = n->fb + n->scanline * NES_W;
  const uint16_t c = n->back_rgb;
  for (int i = 0; i < NES_W; i++)
    *dest++ = c;
}

static inline void draw_sprite_pixel(nes_vs_t *n, int sprite_xpos, int color,
                                     int pixel, uint8_t pixel_data) {
  n->fb[n->scanline * NES_W + sprite_xpos + pixel] =
      n->pal_rgb[((4 * color) | pixel_data) & 0x1f];
}

static void draw_sprites(nes_vs_t *n, uint8_t *line_priority) {
  int sprite_count = 0;
  const uint8_t *spriteram = n->ram + NESOFF_OAM;

  const int size = (n->ppu_ctrl0 & CTRL0_SPRITE_SIZE) ? 16 : 8;
  const int first_pixel = (n->ppu_ctrl1 & CTRL1_SPRITES_L8) ? 0 : 8;

  for (int sprite_index = 0; sprite_index < 0x100; sprite_index += 4) {
    int sprite_ypos = spriteram[sprite_index] + 1;
    int sprite_xpos = spriteram[sprite_index + 3];

    // Il "fudge" di MAME sull'ultimo pixel della scanline: senza, alcuni giochi
    // non vedono mai lo sprite-0 hit (commento originale: Battletoads).
    if ((sprite_index == 0) && (sprite_xpos == 254)) {
      sprite_ypos--;
      if (line_priority[sprite_xpos] & 0x02)
        n->ppu_status |= STATUS_SPRITE0_HIT;
    }

    if ((sprite_ypos + size <= n->scanline) || (sprite_ypos > n->scanline))
      continue;

    int tile = spriteram[sprite_index + 1];
    const int color = (spriteram[sprite_index + 2] & 0x03) + 4;
    const int pri = (spriteram[sprite_index + 2] >> 5) & 1;
    const int flipx = (spriteram[sprite_index + 2] >> 6) & 1;
    const int flipy = (spriteram[sprite_index + 2] >> 7) & 1;

    if (size == 16) {
      if (tile & 1) {          // 8x16 dispari: si disegna l'altra meta'
        tile &= ~0x01;
        tile |= 0x100;
      }
    }

    int sprite_line = n->scanline - sprite_ypos;
    if (flipy)
      sprite_line = (size - 1) - sprite_line;
    if (size == 16 && sprite_line > 7) {
      tile++;
      sprite_line -= 8;
    }

    int index1 = tile * 16;
    if (size == 8)
      index1 += (n->sprite_page == 0) ? 0 : 0x1000;

    uint8_t plane0 = ppu_readbyte(n, (index1 + sprite_line) & 0x1fff);
    uint8_t plane1 = ppu_readbyte(n, (index1 + sprite_line + 8) & 0x1fff);

    if (sprite_count == 8) {
      // Il flag di overflow resta, perche' il gioco puo' leggerlo da $2002.
      n->ppu_status |= STATUS_8SPRITES;
      n->spr_overflow_lines++;
#if !NES_NO_SPRITE_LIMIT
      break;                    // il NES vero ne disegna 8 per riga, non di piu'
#endif
    }
    sprite_count++;

    if (!(n->ppu_ctrl1 & CTRL1_SPRITES))
      continue;                 // gli sprite si contano comunque, non si disegnano

    for (int pixel = 0; pixel < 8; pixel++) {
      uint8_t pixel_data;
      if (flipx) {
        pixel_data = (plane0 & 1) | ((plane1 & 1) << 1);
        plane0 >>= 1;
        plane1 >>= 1;
      } else {
        pixel_data = ((plane0 >> 7) & 1) | (((plane1 >> 7) & 1) << 1);
        plane0 <<= 1;
        plane1 <<= 1;
      }

      if (sprite_xpos + pixel < first_pixel)
        continue;

      if (pixel_data) {
        if ((sprite_xpos + pixel) < NES_W) {
          if (pri) {
            // priorita' bassa: passa solo dove non c'e' gia' BG opaco ne' un altro sprite
            if (!line_priority[sprite_xpos + pixel])
              draw_sprite_pixel(n, sprite_xpos, color, pixel, pixel_data);
            line_priority[sprite_xpos + pixel] |= 0x01;
          } else {
            // priorita' alta: copre il BG, ma non un altro sprite gia' disegnato
            if (!(line_priority[sprite_xpos + pixel] & 0x01)) {
              draw_sprite_pixel(n, sprite_xpos, color, pixel, pixel_data);
              line_priority[sprite_xpos + pixel] |= 0x01;
            }
          }
        }
      }

      // sprite-0 hit: sprite 0 opaco sopra un pixel di BG opaco
      if (sprite_index == 0 && (pixel_data & 0x03) &&
          ((sprite_xpos + pixel) < 255) &&
          (line_priority[sprite_xpos + pixel] & 0x02)) {
        if (!(n->ppu_status & STATUS_SPRITE0_HIT))
          n->spr0_hits++;
        n->ppu_status |= STATUS_SPRITE0_HIT;
      }
    }
  }
}

// render_scanline() + update_visible_scanline() di ppu2c0x.cpp
static void render_scanline(nes_vs_t *n) {
  uint8_t line_priority[NES_W];
  memset(line_priority, 0, sizeof(line_priority));

  if (n->ppu_ctrl1 & CTRL1_BACKGROUND)
    draw_background(n, line_priority);
  else
    draw_background_pen(n);

  draw_sprites(n, line_priority);
}

// scanline_increment_fine_ycounter(), verbatim (compreso il rollover al 30esimo
// tile verticale, non al 32esimo).
static void scanline_increment_fine_ycounter(nes_vs_t *n) {
  n->refresh_data += 0x1000;
  if (n->refresh_data & 0x8000) {
    const uint16_t tmp = (n->refresh_data & 0x03e0) + 0x20;
    n->refresh_data &= 0x7c1f;
    if (tmp == 0x03c0)
      n->refresh_data ^= 0x0800;
    else
      n->refresh_data |= tmp & 0x03e0;
  }
}

static void update_visible_scanline(nes_vs_t *n) {
  if ((n->ppu_ctrl1 & (CTRL1_BACKGROUND | CTRL1_SPRITES)) && n->first_on_line < 0)
    n->first_on_line = (int16_t)n->scanline;   // prima riga accesa del quadro

  if (n->ppu_ctrl1 & (CTRL1_BACKGROUND | CTRL1_SPRITES)) {
    // inizio scanline: si ricopia dal latch solo lo scroll X grossolano + il
    // bit di overflow orizzontale (0x041f). E' cosi' che funzionano gli split.
    n->refresh_data &= ~0x041f;
    n->refresh_data |= n->refresh_latch & 0x041f;
    render_scanline(n);
    scanline_increment_fine_ycounter(n);
  } else {
    // rendering spento: la riga prende il pen di sfondo
    draw_background_pen(n);
    n->blank_lines++;
  }
}

// ============================================================================
// Registri PPU $2000-$2007 — read()/write() di ppu2c0x.cpp
// ============================================================================
static uint8_t ppu_reg_read(nes_vs_t *n, uint16_t offset) {
  switch (offset & 7) {
    case 2:  // STATUS
      n->data_latch = n->ppu_status | (n->data_latch & 0x1f);
      n->toggle = 0;
      if (n->data_latch & STATUS_VBLANK)
        n->ppu_status &= 0x60;
      break;

    case 4:  // SPRITE DATA
      n->data_latch = n->ram[NESOFF_OAM + n->ppu_oamaddr];
      break;

    case 7:  // DATA
      if (n->videomem_addr >= 0x3f00) {
        n->data_latch = ppu_readbyte(n, n->videomem_addr);
        n->buffered_data = ppu_readbyte(n, n->videomem_addr & 0x2fff);
      } else {
        n->data_latch = n->buffered_data;
        n->buffered_data = ppu_readbyte(n, n->videomem_addr);
      }
      n->videomem_addr += n->addr_inc;
      break;

    default:
      break;
  }
  return n->data_latch;
}

static void ppu_reg_write(nes_vs_t *n, uint16_t offset, uint8_t data) {
  switch (offset & 7) {
    case 0:  // CONTROL0
      n->ppu_ctrl0 = data;
      n->refresh_latch &= 0x73ff;
      n->refresh_latch |= (data & 3) << 10;
      n->tile_page   = (data & CTRL0_CHR_SELECT) >> 2;
      n->sprite_page = (data & CTRL0_SPR_SELECT) >> 1;
      n->addr_inc = (data & CTRL0_INC) ? 32 : 1;
      break;

    case 1:  // CONTROL1
      if (n->scanline <= NES_BOTTOM_VISIBLE &&
          ((n->ppu_ctrl1 ^ data) & (CTRL1_BACKGROUND | CTRL1_SPRITES)))
        n->ctrl1_midframe++;      // accende/spegne il rendering a meta' quadro
      n->ppu_ctrl1 = data;
      break;

    case 3:  // SPRITE ADDRESS
      n->ppu_oamaddr = data;
      break;

    case 4:  // SPRITE DATA — write_to_spriteram_with_increment()
      if (n->scanline > NES_BOTTOM_VISIBLE ||
          !(n->ppu_ctrl1 & (CTRL1_BACKGROUND | CTRL1_SPRITES))) {
        n->ram[NESOFF_OAM + n->ppu_oamaddr] = data;
        n->ppu_oamaddr = (n->ppu_oamaddr + 1) & 0xff;
      } else {
        // Rifiutata perche' siamo dentro il quadro visibile: se succede in
        // massa, gli sprite NON si aggiornano e lampeggiano.
        n->oam_writes_blocked++;
      }
      break;

    case 5:  // SCROLL
      if (n->toggle) {
        n->refresh_latch &= 0x0c1f;
        n->refresh_latch |= (data & 0xf8) << 2;
        n->refresh_latch |= (data & 0x07) << 12;
      } else {
        n->refresh_latch &= (0xffe0 & GLOBAL_REFRESH_MASK);
        n->refresh_latch |= (data & 0xf8) >> 3;
        n->x_fine = data & 7;
      }
      n->toggle = !n->toggle;
      break;

    case 6:  // ADDRESS
      if (n->toggle) {
        n->refresh_latch &= (0xff00 & GLOBAL_REFRESH_MASK);
        n->refresh_latch |= data;
        n->refresh_data = n->refresh_latch;
        n->videomem_addr = n->refresh_latch;
      } else {
        n->refresh_latch &= 0x00ff;
        n->refresh_latch |= (data & (VIDEORAM_ADDR_MASK >> 8)) << 8;
      }
      n->toggle = !n->toggle;
      break;

    case 7:  // DATA
      ppu_writebyte(n, n->videomem_addr & VIDEORAM_ADDR_MASK, data);
      n->videomem_addr += n->addr_inc;
      break;

    default:
      break;
  }
  n->data_latch = data;
}

// ============================================================================
// Mapper (Namco) 108 — vs108_rom_banking_w() di vsnes.cpp, verbatim.
// $8000 sceglie il registro, $8001 gli da' il valore. I due banchi PRG alti
// ($C000/$E000) restano fissi sugli ultimi 16 KB (init_prg_banking).
// ============================================================================
static void mapper108_write(nes_vs_t *n, uint16_t addr, uint8_t data) {
  switch (addr & 0x6001) {
    case 0x0000:  // $8000
      n->reg108 = data & 0x07;
      break;

    case 0x0001:  // $8001
      switch (n->reg108) {
        case 0: case 1: {          // 2 KB di CHR
          const int start = n->reg108 * 2;
          for (int i = 0; i < 2; i++)
            n->chr_bank[start + i] = (uint8_t)(((data & (n->chr_chunks - 1)) + i) & (n->chr_chunks - 1));
          break;
        }
        case 2: case 3: case 4: case 5:   // 1 KB di CHR
          n->chr_bank[n->reg108 + 2] = (uint8_t)(data & (n->chr_chunks - 1));
          break;
        case 6: case 7: {                 // 8 KB di PRG
          const uint8_t slot = (uint8_t)((n->reg108 - 6) & 3);
          n->prg_bank[slot] = (uint8_t)(data & (n->prg_chunks - 1));
          n->prg_ptr[slot] = n->prg + ((uint32_t)n->prg_bank[slot] << 13);
          break;
        }
      }
      break;

    default:
      break;
  }
}

// ============================================================================
// Protezione di Vs. Super Xevious — supxevs_prot_1..4_r() di vsnes.cpp.
// Quattro indirizzi nel buco $4020-$5FFF: il gioco li interroga a coppie e si
// pianta se le risposte non tornano.
// ============================================================================
static inline int supxevs_prot(nes_vs_t *n, uint16_t addr, uint8_t *out) {
  switch (addr) {
    case 0x54ff: n->prot_index ^= 1;                    *out = 0x05; return 1;
    case 0x5678: *out = n->prot_index ? 0x01 : 0x00;                 return 1;
    case 0x578f: *out = n->prot_index ? 0x89 : 0xd1;                 return 1;
    case 0x5567: *out = n->prot_index ? 0x37 : 0x3e;                 return 1;
  }
  return 0;
}

// ============================================================================
// Ingressi — vsnes_in0_r/in1_r/in0_w() di vsnes.cpp.
// $4016: bit0 = shift del joystick 1, piu' monete e i 2 dip bassi.
// $4017: bit0 = shift del joystick 2, piu' gli altri 6 dip.
// ============================================================================
static uint8_t in0_read(nes_vs_t *n) {
  if (n->in_strobe & 1)
    n->in_latch[0] = n->joy[0];

  uint8_t ret = n->in_latch[0] & 1;
  n->in_latch[0] >>= 1;

  ret |= n->coins;                 // monete e service
  ret |= (n->dsw & 3) << 3;        // i 2 dip bassi
  return ret;
}

static uint8_t in1_read(nes_vs_t *n) {
  if (n->in_strobe & 1)
    n->in_latch[1] = n->joy[1];

  uint8_t ret = n->in_latch[1] & 1;
  n->in_latch[1] >>= 1;

  ret |= n->dsw & ~3;              // gli altri 6 dip
  return ret;
}

static void in0_write(nes_vs_t *n, uint8_t data) {
  // il fronte 1->0 ricarica i latch dei due joystick
  if (n->in_strobe & ~data & 1) {
    n->in_latch[0] = n->joy[0];
    n->in_latch[1] = n->joy[1];
  }
  n->in_strobe = data;
}

// ============================================================================
// APU NES — trascritta da machines/dkong3/dkong3.cpp (dove suona gia' sul
// device con due RP2A03). Qui ne serve UNA sola. Tabelle e mixer non lineare
// sono gli stessi, cambiano solo i nomi.
// ⚠️ Le due copie vanno tenute allineate: un difetto corretto qui va corretto
//    anche in dkong3.cpp e viceversa.
// ============================================================================
static const uint8_t NESAPU_LENGTH_TABLE[32] = {
  10,254,20,2,40,4,80,6,160,8,60,10,14,12,26,14,
  12,16,24,18,48,20,96,22,192,24,72,26,16,28,32,30
};
static const uint8_t NESAPU_DUTY[4][8] = {
  {0,1,0,0,0,0,0,0},{0,1,1,0,0,0,0,0},{0,1,1,1,1,0,0,0},{1,0,0,1,1,1,1,1}
};
static const uint8_t NESAPU_TRI_SEQ[32] = {
  15,14,13,12,11,10,9,8,7,6,5,4,3,2,1,0, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15
};
static const uint16_t NESAPU_NOISE_PERIOD[16] = {
  4,8,16,32,64,96,128,160,202,254,380,508,762,1016,2034,4068
};

// Mixer NON-LINEARE del NES (nesdev wiki, come in dkong3): due LUT.
#define NESAPU_LUT_SCALE 2048
static int  nesapu_pulse_lut[31];
static int  nesapu_tnd_lut[203];
static bool nesapu_lut_ready = false;
static void nesapu_init_lut(void) {
  if (nesapu_lut_ready) return;
  nesapu_pulse_lut[0] = 0;
  for (int i = 1; i < 31; i++)
    nesapu_pulse_lut[i] = (int)(95.52 / (8128.0 / i + 100.0) * NESAPU_LUT_SCALE + 0.5);
  nesapu_tnd_lut[0] = 0;
  for (int i = 1; i < 203; i++)
    nesapu_tnd_lut[i] = (int)(163.67 / (24329.0 / i + 100.0) * NESAPU_LUT_SCALE + 0.5);
  nesapu_lut_ready = true;
}

static void nesapu_write(nesapu_t &a, uint16_t addr, uint8_t val) {
  switch (addr) {
    case 0x4000: a.p_ctrl[0] = val; break;
    case 0x4001: a.p_sweep[0] = val; a.p_sweep_rst[0] = true; break;
    case 0x4002: a.p_timer[0] = (a.p_timer[0] & 0x700) | val; break;
    case 0x4003: a.p_timer[0] = (a.p_timer[0] & 0x0FF) | ((val & 7) << 8);
                 a.p_seq[0] = 0; a.p_cnt[0] = 0; a.p_env_rst[0] = true;
                 if (a.p_en[0]) a.p_len[0] = NESAPU_LENGTH_TABLE[val >> 3]; break;
    case 0x4004: a.p_ctrl[1] = val; break;
    case 0x4005: a.p_sweep[1] = val; a.p_sweep_rst[1] = true; break;
    case 0x4006: a.p_timer[1] = (a.p_timer[1] & 0x700) | val; break;
    case 0x4007: a.p_timer[1] = (a.p_timer[1] & 0x0FF) | ((val & 7) << 8);
                 a.p_seq[1] = 0; a.p_cnt[1] = 0; a.p_env_rst[1] = true;
                 if (a.p_en[1]) a.p_len[1] = NESAPU_LENGTH_TABLE[val >> 3]; break;
    case 0x4008: a.t_lin_ctrl = val; break;
    case 0x400A: a.t_timer = (a.t_timer & 0x700) | val; break;
    case 0x400B: a.t_timer = (a.t_timer & 0x0FF) | ((val & 7) << 8);
                 a.t_cnt = 0; a.t_lin_reload = true;
                 if (a.t_en) a.t_len = NESAPU_LENGTH_TABLE[val >> 3]; break;
    case 0x400C: a.n_ctrl = val; break;
    case 0x400E: a.n_period_idx = val & 0x0F; a.n_mode = (val >> 7) & 1; break;
    case 0x400F: a.n_env_rst = true;
                 if (a.n_en) a.n_len = NESAPU_LENGTH_TABLE[val >> 3]; break;
    case 0x4015:
      if (!((val >> 0) & 1)) a.p_len[0] = 0;
      if (!((val >> 1) & 1)) a.p_len[1] = 0;
      if (!((val >> 2) & 1)) a.t_len = 0;
      if (!((val >> 3) & 1)) a.n_len = 0;
      a.p_en[0] = (val >> 0) & 1; a.p_en[1] = (val >> 1) & 1;
      a.t_en = (val >> 2) & 1;    a.n_en = (val >> 3) & 1;
      break;
    default: break;
  }
}

static uint8_t nesapu_status(nesapu_t &a) {
  return (a.p_len[0] ? 0x01 : 0) | (a.p_len[1] ? 0x02 : 0) |
         (a.t_len ? 0x04 : 0) | (a.n_len ? 0x08 : 0);
}

static int nesapu_sample(nesapu_t *a, int ticks) {
  int p_out[2] = {0, 0};
  for (int ch = 0; ch < 2; ch++) {
    uint16_t cur_per = a->p_timer[ch] & 0x7FF;
    int period = (int)(cur_per + 1) * 2;
    if (period >= 4) {
      a->p_cnt[ch] -= ticks;
      while (a->p_cnt[ch] <= 0) { a->p_cnt[ch] += period; a->p_seq[ch] = (a->p_seq[ch] + 1) & 7; }
    }
    if (a->p_en[ch] && a->p_len[ch] && cur_per >= 8) {
      bool muted = false;
      uint8_t sw_shift = a->p_sweep[ch] & 7;
      if (sw_shift && !((a->p_sweep[ch] >> 3) & 1) && (cur_per + (cur_per >> sw_shift) > 0x7FF)) muted = true;
      if (!muted) {
        uint8_t vol = (a->p_ctrl[ch] & 0x10) ? (a->p_ctrl[ch] & 0x0F) : a->p_env_vol[ch];
        uint8_t duty = (a->p_ctrl[ch] >> 6) & 3;
        if (NESAPU_DUTY[duty][a->p_seq[ch]]) p_out[ch] = vol;
      }
    }
  }
  // ⛔ IL TRIANGOLO NON VA A ZERO QUANDO SI SPEGNE: **tiene l'ultimo livello**.
  // In MAME (nes_apu.cpp, apu_triangle) tutti i `return` anticipati NON toccano
  // `chan->output` — a differenza di quadre e rumore, che azzerano. E' il DAC
  // vero: si congela, non si azzera.
  // Qui invece `tri_out` ripartiva da 0 a ogni gate, con un salto che vale fino
  // a 523 su un fondo scala di 1317 (il 40%): un TOCK secco a ogni fine nota.
  // E il driver di questo gioco il triangolo lo gatea di continuo — nella ROM
  // c'e' `LDA $50 / AND #$FA / STA $4015`, che spegne proprio il bit 2.
  if (a->t_en && a->t_len && a->t_lin) {
    int period = (int)(a->t_timer & 0x7FF) + 1;
    if (period >= 2) {
      a->t_cnt -= ticks;
      while (a->t_cnt <= 0) { a->t_cnt += period; a->t_seq = (a->t_seq + 1) & 31; }
    }
    a->t_last = NESAPU_TRI_SEQ[a->t_seq];
  }
  const int tri_out = a->t_last;
  int noise_out = 0;
  {
    int period = NESAPU_NOISE_PERIOD[a->n_period_idx & 0x0F];
    a->n_cnt -= ticks;
    while (a->n_cnt <= 0) {
      a->n_cnt += period;
      uint16_t bit = (a->n_lfsr & 1) ^ ((a->n_mode ? (a->n_lfsr >> 6) : (a->n_lfsr >> 1)) & 1);
      a->n_lfsr = (a->n_lfsr >> 1) | (bit << 14);
    }
    if (a->n_en && a->n_len && !(a->n_lfsr & 1)) {
      uint8_t nvol = (a->n_ctrl & 0x10) ? (a->n_ctrl & 0x0F) : a->n_env_vol;
      noise_out = nvol;
    }
  }
  return nesapu_pulse_lut[p_out[0] + p_out[1]] + nesapu_tnd_lut[3 * tri_out + 2 * noise_out];
}

// Frame sequencer: UN passo per chiamata. Inviluppi e contatore lineare a
// 240 Hz, lunghezze e sweep a 120 Hz (passi 1 e 3).
// ⛔ Prima i quattro passi giravano tutti insieme UNA volta per quadro, e solo
// DOPO si generavano i 400 campioni: ogni nota partiva quindi da un inviluppo
// gia' decaduto di quattro passi, e il suono ne usciva sporco. Ora il passo e'
// interlacciato con la sintesi (uno ogni 100 campioni = 240 Hz veri).
// ⚠️ Lo stesso difetto sta in machines/dkong3/dkong3.cpp, da cui questa APU e'
//    stata portata: se un giorno si sistema DK3, il riferimento e' qui.
static void nesapu_frame_step(nesapu_t &a, int step) {
  {
    const bool clock_length = (step == 1 || step == 3);

    for (int ch = 0; ch < 2; ch++) {
      if (a.p_env_rst[ch]) {
        a.p_env_rst[ch] = false; a.p_env_vol[ch] = 15; a.p_env_cnt[ch] = a.p_ctrl[ch] & 0x0F;
      } else if (!(a.p_ctrl[ch] & 0x10)) {
        uint8_t per = a.p_ctrl[ch] & 0x0F;
        if (a.p_env_cnt[ch] > 0) a.p_env_cnt[ch]--;
        else {
          a.p_env_cnt[ch] = per;
          if (a.p_env_vol[ch] > 0) a.p_env_vol[ch]--;
          else if (a.p_ctrl[ch] & 0x20) a.p_env_vol[ch] = 15;
        }
      }
    }
    if (a.n_env_rst) {
      a.n_env_rst = false; a.n_env_vol = 15; a.n_env_cnt = a.n_ctrl & 0x0F;
    } else if (!(a.n_ctrl & 0x10)) {
      uint8_t per = a.n_ctrl & 0x0F;
      if (a.n_env_cnt > 0) a.n_env_cnt--;
      else {
        a.n_env_cnt = per;
        if (a.n_env_vol > 0) a.n_env_vol--;
        else if (a.n_ctrl & 0x20) a.n_env_vol = 15;
      }
    }
    if (a.t_lin_reload) a.t_lin = a.t_lin_ctrl & 0x7F;
    else if (a.t_lin > 0) a.t_lin--;
    if (!(a.t_lin_ctrl & 0x80)) a.t_lin_reload = false;

    if (clock_length) {
      for (int ch = 0; ch < 2; ch++) {
        if (!(a.p_ctrl[ch] & 0x20) && a.p_len[ch] > 0) a.p_len[ch]--;
        uint8_t sw = a.p_sweep[ch], sw_per = (sw >> 4) & 7;
        if (a.p_sweep_rst[ch]) { a.p_sweep_cnt[ch] = sw_per; a.p_sweep_rst[ch] = false; }
        else if (a.p_sweep_cnt[ch] > 0) a.p_sweep_cnt[ch]--;
        else {
          a.p_sweep_cnt[ch] = sw_per;
          const bool sw_en = (sw >> 7) & 1, sw_neg = (sw >> 3) & 1;
          const uint8_t sw_sh = sw & 7;
          uint16_t cur = a.p_timer[ch] & 0x7FF;
          if (sw_en && sw_sh > 0 && cur >= 8) {
            uint16_t delta = cur >> sw_sh;
            uint16_t target = sw_neg ? (uint16_t)(cur - delta - (ch == 0 ? 1 : 0)) : (uint16_t)(cur + delta);
            if (target <= 0x7FF) a.p_timer[ch] = target;
          }
        }
      }
      if (!(a.t_lin_ctrl & 0x80) && a.t_len > 0) a.t_len--;
      if (!(a.n_ctrl & 0x20) && a.n_len > 0) a.n_len--;
    }
  }
}

// Sintesi di un frame di audio nel ring buffer (400 sample a 24 kHz), con lo
// stesso 4x oversampling + DC-blocker + LPF di dkong3.
static void nesapu_render_frame(nes_vs_t *n) {
  if (!n->samples) return;
  const int OS = 4;
  // 56 cicli di CPU per campione (32 kHz), spezzati in 4 sotto-passi: la somma
  // fa 56 esatti. Erano due moltiplicazioni e due divisioni per sotto-passo.
  static const int ticks_os[4] = { 14, 14, 14, 14 };

  for (int i = 0; i < NES_SAMPLES_PER_FRAME; i++) {
    // ⚡ Un passo di frame sequencer ogni 100 campioni = i 240 Hz veri,
    // interlacciati con la sintesi invece che tutti in blocco a inizio quadro.
    if ((i % (NES_SAMPLES_PER_FRAME / 4)) == 0)
      nesapu_frame_step(n->apu, i / (NES_SAMPLES_PER_FRAME / 4));

    uint16_t next = (uint16_t)((n->wptr + 1) & (NES_AUDIO_SAMPLES - 1));
    if (next == n->rptr) break;                 // buffer pieno: il consumatore e' indietro
    int acc = 0;
    for (int k = 0; k < OS; k++)
      acc += nesapu_sample(&n->apu, ticks_os[k]);

    int m = acc / OS;
    // ⛔ Il filtro che toglie la componente continua era `dc += ac >> 7`, e con
    // segnali di questa ampiezza **non convergeva**: lo shift arrotonda verso
    // il basso, quindi ogni scarto fra 0 e 127 dava 0 e il filtro si fermava
    // lasciando un offset. Peggio, fuori da quella banda si muoveva di 1 LSB
    // per campione — una rampa, non un'esponenziale: dopo ogni gradino restava
    // un "woomp" lungo. Ora l'accumulatore e' in virgola fissa 8.8: converge a
    // media zero davvero, stesso costo.
    const int ac = m - (n->dc_q8 >> 8);
    n->dc_q8 += (((m << 8) - n->dc_q8) >> 7);
    n->lpf = (ac * 3 + n->lpf) >> 2;
    int val = n->lpf;
    if (val >  2000) val =  2000;
    if (val < -2000) val = -2000;
    n->samples[n->wptr] = (short)val;
    n->wptr = next;
  }
}

// Media di due colori RGB565 (in ordine NORMALE, non byte-swapped): azzera il
// bit basso di ogni canale e somma la parte comune, senza mai traboccare da un
// canale all'altro.
static inline uint16_t nes_avg565(uint16_t a, uint16_t b) {
  return (uint16_t)((((a ^ b) & 0xF7DE) >> 1) + (a & b));
}

// ⛔ Qui c'era `nes_blend_frame()`, la media di due quadri: era il tentativo di
// curare le ombre fondendo i quadri consecutivi. Tolta il 2026-08-04 — non era
// quella la causa (girava sul task emulazione, muoveva 360 KB di PSRAM per
// quadro e teneva l'emulazione a 47 Hz invece di 60, lasciando anche le scie
// sul terreno che scorre). La cura vera e' l'ammorbidimento orizzontale nel
// blit, qui sotto.

void nes_vs_lock_frame(nes_vs_t *n) {
  n->fb_locked = n->fb_show;      // prenota: da qui la PPU non lo riusa
}

void nes_vs_blit_panel_row(nes_vs_t *n, int row, uint16_t *dst, int dst_w,
                           int x_crop, int y_off) {
  // ⛔ Si legge il buffer PRENOTATO, mai `fb`: quello lo sta riempiendo la PPU
  // sull'altro core. La prenotazione vale per tutto il quadro, quindi le 36
  // bande vengono tutte dallo stesso frame: niente strappi fra una banda e
  // l'altra. (Se prepare_frame non fosse passata, si ripiega su fb_show.)
  const uint16_t *src = n->fb_locked ? n->fb_locked : n->fb_show;
  if (!src || !dst) return;

  for (int line = 0; line < 8; line++) {
    const int panel_y = row * 8 + line;
    const int nes_y = panel_y - y_off;
    if (nes_y < 0 || nes_y >= NES_H) continue;
    const uint16_t *s = src + (size_t)nes_y * NES_W + x_crop;
    uint16_t *d = dst + (size_t)line * dst_w;
#if NES_SOFT_DITHER
    // ⭐⭐ AMMORBIDIMENTO ORIZZONTALE — la cura per le OMBRE CHE LAMPEGGIANO.
    //
    // Il gioco disegna le ombre delle torri **a scacchiera** (un pixel nero, uno
    // di cielo): e' il modo con cui il NES finge la semi-trasparenza. Su un
    // televisore a tubo quel motivo si fonde in un blu scuro uniforme — era
    // pensato per il segnale composito. Su un LCD nitido la scacchiera resta, ed
    // e' il caso peggiore per l'**inversione di polarita' del pannello**: ne
    // esce uno sfarfallio a meta' del frame rate (~30 Hz).
    // ⛔ Che il difetto sia del PANNELLO e non nostro e' provato: sulla
    // schermata ferma il quadro e' identico **pixel per pixel** per decine di
    // quadri (0 diversi, misurato dall'harness) e l'utente lo vede lampeggiare
    // lo stesso. Riscrivere gli stessi valori non puo' cambiare l'immagine.
    // Qui si rifa' quello che faceva il composito: la media di ogni pixel col
    // suo vicino di sinistra. Il retino diventa una tinta piena, i bordi netti
    // restano netti (una transizione grande resta grande).
    // Misura offline (mk_confronto_retino.py, zona delle torri): transizioni
    // orizzontali **5010 -> 2582**.
    // Costa ~1 ms per quadro sul core del video, che ha margine (il blit SPI
    // occupa 13,8 ms su 16,7, ma e' il DMA a lavorare, non la CPU).
    // Per toglierlo: NES_SOFT_DITHER a 0 in nes_vs.h.
    uint16_t prec = s[0];
    for (int x = 0; x < dst_w; x++) {
      const uint16_t cur = s[x];
      // i valori sono byte-swapped (formato del pannello): si raddrizzano, si
      // mediano, e si riswappa una volta sola.
      const uint16_t a = (uint16_t)((prec >> 8) | (prec << 8));
      const uint16_t b = (uint16_t)((cur  >> 8) | (cur  << 8));
      const uint16_t m = nes_avg565(a, b);
      d[x] = (uint16_t)((m >> 8) | (m << 8));
      prec = cur;
    }
#else
    memcpy(d, s, (size_t)dst_w * sizeof(uint16_t));
#endif
  }
}

int nes_vs_audio_pop(nes_vs_t *n, short *out, int n_max) {
  int got = 0;
  while (got < n_max && n->rptr != n->wptr) {
    out[got++] = n->samples[n->rptr];
    n->rptr = (uint16_t)((n->rptr + 1) & (NES_AUDIO_SAMPLES - 1));
  }
  return got;
}

// ============================================================================
// Mappa della CPU — vsnes_cpu1_map() di vsnes.cpp
// ============================================================================
static uint8_t cpu_read(m6502_t *cpu, uint16_t addr) {
  nes_vs_t *n = (nes_vs_t *)cpu->user;

  // ⚡ IL RAMO PIU' FREQUENTE PER PRIMO. Da $8000 in su arrivano ~20.000 delle
  // ~23.000 letture per quadro (e' li' che stanno codice e tabelle del gioco):
  // prima pagavano cinque confronti per arrivare in fondo alla catena.
  if (addr >= 0x8000)
    return n->prg_ptr[(addr >> 13) & 3][addr & 0x1fff];

  if (addr < 0x2000)
    return n->ram[NESOFF_RAM + (addr & 0x07ff)];       // 2 KB, mirror x4
  if (addr < 0x4000)
    return ppu_reg_read(n, addr & 7);
  if (addr < 0x4020) {
    if (addr == 0x4016) return in0_read(n);
    if (addr == 0x4017) return in1_read(n);
    if (addr == 0x4015) return nesapu_status(n->apu);
    return 0x00;
  }
  if (addr < 0x6000) {
    uint8_t v;
    if (supxevs_prot(n, addr, &v)) return v;
    return n->coin_latch;                              // vsnes_coin_counter_r
  }
  return n->ram[NESOFF_NVRAM + (addr & 0x07ff)];       // RAM tamponata, mirror
}

static void cpu_write(m6502_t *cpu, uint16_t addr, uint8_t val) {
  nes_vs_t *n = (nes_vs_t *)cpu->user;

  if (addr < 0x2000) {
    n->ram[NESOFF_RAM + (addr & 0x07ff)] = val;
    return;
  }
  if (addr < 0x4000) {
    ppu_reg_write(n, addr & 7, val);
    return;
  }
  if (addr < 0x4020) {
    if (addr == 0x4014) {
      // spriteram_dma(): 256 byte verso la OAM passando dal registro $2004,
      // quindi con lo stesso incremento e le stesse regole di scrittura.
      const uint16_t base = (uint16_t)val << 8;
      for (int i = 0; i < 256; i++)
        ppu_reg_write(n, 4, cpu_read(cpu, base + i));
      return;
    }
    if (addr == 0x4016) {
      in0_write(n, val);
      return;
    }
    // ⚠️ $4017 sul NES sarebbe il frame counter dell'APU, ma sul VS quella
    // linea e' un INGRESSO (vsnes_cpu1_map non la mappa in scrittura): il
    // sequencer resta quindi sui 4 passi di default.
    nesapu_write(n->apu, addr, val);
    return;
  }
  if (addr < 0x6000) {
    n->coin_latch = val;                               // vsnes_coin_counter_w
    return;
  }
  if (addr < 0x8000) {
    n->ram[NESOFF_NVRAM + (addr & 0x07ff)] = val;
    return;
  }
  mapper108_write(n, addr, val);
}

// ============================================================================
// API
// ============================================================================
void nes_vs_init(nes_vs_t *n,
                 const uint8_t *prg, uint32_t prg_len,
                 const uint8_t *chr, uint32_t chr_len,
                 const uint16_t *pal64,
                 uint8_t *ram_block,
                 uint16_t *fb, uint16_t *fb2, uint16_t *fb3,
                 short *audio_ring,
                 uint8_t *chr_ram_copy,
                 uint16_t *fb_out_a, uint16_t *fb_out_b) {
  memset(n, 0, sizeof(*n));
  nesapu_init_lut();
  nes_exp_init();
  n->prg = prg;
  n->prg_chunks = (uint16_t)(prg_len / 0x2000);
  n->chr = chr;
  n->chr_chunks = (uint16_t)(chr_len / 0x400);
  // ⚡ La CHR viene letta ~16.600 volte per quadro dal ciclo piu' interno del
  // rendering. In flash quegli accessi competono con i 377 KB per quadro del
  // framebuffer e vengono sfrattati dalla cache a ogni giro. Se il chiamante ci
  // ha passato 32 KB di RAM interna, ci si copia dentro; altrimenti si continua
  // a leggere dalla flash e cambia solo la velocita'.
  if (chr_ram_copy) {
    memcpy(chr_ram_copy, chr, chr_len);
    n->chr_fast = chr_ram_copy;
  } else {
    n->chr_fast = chr;
  }
  n->pal64 = pal64;
  n->ram = ram_block;
  // Doppio buffer: si disegna in fb_a e si mostra fb_b (e viceversa). Se il
  // secondo buffer non c'e' (allocazione fallita) si torna al buffer singolo:
  // si rivede lo sfarfallio, ma il gioco resta giocabile invece di dare
  // schermo nero.
  n->fb_a = fb;
  n->fb_b = fb2;
  n->fb_c = fb3;
  n->fb_out_a = fb_out_a;
  n->fb_out_b = fb_out_b;
  // Con la media attiva servono due buffer di DISEGNO (corrente e precedente):
  // sono fb_a e fb_b. fb_c resta la riserva per il percorso senza media.
  n->fb = fb;
  n->fb_prev = fb2 ? fb2 : fb;
  // ⛔ Il pannello guarda i buffer di DISEGNO: la media dei quadri e' stata
  // tolta (lasciava scie sul terreno che scorre, ed era comunque la cura di un
  // difetto mal diagnosticato — vedi il commento in nes_vs_run_frame).
  n->fb_show = fb;
  n->fb_locked = fb;
  n->samples = audio_ring;

  n->cpu.read  = cpu_read;
  n->cpu.write = cpu_write;
  n->cpu.user  = n;
}

void nes_vs_reset(nes_vs_t *n) {
  // init_prg_banking(): i quattro slot partono dagli ULTIMI 32 KB.
  for (int i = 0; i < 4; i++) {
    n->prg_bank[i] = (uint8_t)(n->prg_chunks - 4 + i);
    n->prg_ptr[i] = n->prg + ((uint32_t)n->prg_bank[i] << 13);
  }
  // machine_start(): v_set_videorom_bank(0, 8, 0) -> primi 8 KB di CHR
  for (int i = 0; i < 8; i++)
    n->chr_bank[i] = (uint8_t)i;

  n->reg108 = 0;
  n->prot_index = 0;

  n->ppu_ctrl0 = n->ppu_ctrl1 = n->ppu_status = n->ppu_oamaddr = 0;
  n->refresh_data = n->refresh_latch = n->videomem_addr = 0;
  n->x_fine = n->toggle = n->data_latch = n->buffered_data = 0;
  n->tile_page = n->sprite_page = n->back_color = 0;
  n->addr_inc = 1;
  n->scanline = 0;
  memset(n->pal_ram, 0, sizeof(n->pal_ram));
  // la copia gia' convertita deve ripartire coerente con la palette azzerata
  n->back_rgb = n->pal64[0];
  for (int i = 0; i < 32; i++) n->pal_rgb[i] = n->pal64[0];

  n->in_latch[0] = n->in_latch[1] = 0;
  n->in_strobe = 0;
  n->coin_latch = 0;
  n->joy[0] = n->joy[1] = 0;
  n->coins = 0;
  n->coin_hold = n->start_hold = 0;

  // Si riparte sempre dagli stessi buffer, cosi' un riavvio a caldo del gioco
  // e' identico al primo avvio.
  n->fb = n->fb_a;
  n->fb_prev = n->fb_b ? n->fb_b : n->fb_a;
  n->fb_show = n->fb_a;
  n->fb_locked = n->fb_a;

  memset(&n->apu, 0, sizeof(n->apu));
  n->apu.n_lfsr = 1;                 // l'LFSR del rumore non parte mai da 0
  n->rptr = n->wptr = 0;
  n->dc_q8 = n->lpf = 0;

  n->frames = n->nmi_count = n->spr0_hits = 0;

  m6502_reset(&n->cpu);
}

void nes_vs_set_input(nes_vs_t *n, uint8_t dir, int a, int b, int coin, int start) {
  // Direzioni e pulsanti stanno sul controller SINISTRO, che il cablaggio
  // "reversed" del VS fa leggere da $4017 (vedi NESJOY_MOVE in nes_vs.h).
  uint8_t j = (uint8_t)(dir & (NESBTN_UP | NESBTN_DOWN | NESBTN_LEFT | NESBTN_RIGHT));
  if (a) j |= NESBTN_A;
  if (b) j |= NESBTN_B;
  n->joy[NESJOY_MOVE] = j;

  if (coin)  n->coin_hold  = NES_INPUT_HOLD_FRAMES;
  if (start) n->start_hold = NES_INPUT_HOLD_FRAMES;

  n->coins = n->coin_hold ? NESCOIN_1 : 0;
  n->joy[NESJOY_START] = n->start_hold ? NESBTN_START : 0;
}

// Un frame = 262 scanline. Per ognuna: la PPU disegna la riga, poi la CPU gira
// per 341/3 cicli (1,7897 MHz contro 5,3693 MHz di pixel clock). L'ordine
// conta: rendendo PRIMA, quando la CPU gira ha gia' a disposizione lo
// sprite-0 hit di quella riga — che e' esattamente il segnale con cui i giochi
// NES fanno lo split dello schermo.
void nes_vs_run_frame(nes_vs_t *n) {
  int cyc_frac = 0;
  const int16_t first_on_prec = n->first_on_line;
  n->first_on_line = -1;

  for (n->scanline = 0; n->scanline < NES_SCANLINES; n->scanline++) {
    if (n->scanline <= NES_BOTTOM_VISIBLE)
      update_visible_scanline(n);

    cyc_frac += 341;
    int budget = cyc_frac / 3;
    cyc_frac -= budget * 3;
    while (budget > 0)
      budget -= m6502_step(&n->cpu);

    // Eventi di FINE scanline (in MAME sono dopo lo scanline++).
    const int next = n->scanline + 1;
    if (next == NES_VBLANK_SCANLINE) {
      n->ppu_status |= STATUS_VBLANK;
      if (n->ppu_ctrl0 & CTRL0_NMI) {
        n->cpu.nmi = 1;
        n->nmi_count++;
      }
    }
    if (next == NES_SCANLINES - 1)      // riga di pre-render
      n->ppu_status &= ~(STATUS_VBLANK | STATUS_SPRITE0_HIT | STATUS_8SPRITES);
  }

  // Fine frame: se il rendering e' acceso si ricopia tutto il latch (v = t).
  if (n->ppu_ctrl1 & (CTRL1_BACKGROUND | CTRL1_SPRITES))
    n->refresh_data = n->refresh_latch;

  // ⛔⛔ QUI NON SI FONDE PIU' NIENTE. La media dei quadri stava in questo punto,
  // cioe' sul **task emulazione**, e muoveva 360 KB di PSRAM per quadro: con
  // l'indicatore sul device si e' misurato **E47 R60**, cioe' l'emulazione a 47
  // Hz mentre il pannello ne mostrava 60 — e a 47 quadri il ring audio riceve
  // 25.000 campioni al secondo invece di 32.000, quindi si svuota (era la lettura
  // `A24`). La fusione e' stata spostata nel BLIT, sul core del video, che di
  // margine ne aveva: vedi nes_vs_blit_panel_row().
  //
  // ⛔ SCAMBIO DEI BUFFER: il quadro appena finito diventa quello mostrato e la
  // PPU riparte su uno LIBERO — ne' quello appena pubblicato ne' quello
  // prenotato dal blit.
  if (n->fb_b) {
    n->fb_show = n->fb;
    uint16_t *cand[3] = { n->fb_a, n->fb_b, n->fb_c };
    for (int i = 0; i < 3; i++) {
      if (!cand[i] || cand[i] == n->fb_show || cand[i] == n->fb_locked)
        continue;
      n->fb = cand[i];
      break;
    }
    // Se nessuno e' libero (terzo buffer non allocato e blit in corso) si
    // continua sul buffer attuale: si torna al comportamento di prima, non si
    // rompe nulla.
  }

  // Audio: 400 campioni nel ring buffer, col frame sequencer interlacciato
  // dentro (non piu' quattro passi in blocco prima della sintesi).
  nesapu_render_frame(n);

  // Statistiche sulla prima riga accesa (diagnostica dello sfarfallio).
  if (n->frames == 0) {
    n->first_on_min = n->first_on_max = n->first_on_line;
  } else {
    if (n->first_on_line < n->first_on_min) n->first_on_min = n->first_on_line;
    if (n->first_on_line > n->first_on_max) n->first_on_max = n->first_on_line;
    if (n->first_on_line != first_on_prec)  n->first_on_changes++;
  }

  if (n->coin_hold)  n->coin_hold--;
  if (n->start_hold) n->start_hold--;

  n->frames++;
}
