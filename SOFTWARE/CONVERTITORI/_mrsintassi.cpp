/* generato da mk_mrsintassi.py - non modificare a mano */
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#define MR_MEM_VRAM 0x0000
#define MR_LY(px)  (((px) * 8 + 3) / 7)   // colonna del quadro 0..223 -> riga di tilemap 0..255
#define MR_PX(ly)  (((ly) * 7 + 4) / 8)   // riga di tilemap 0..255   -> colonna del quadro 0..223
#define FB_W            240 // = videoGameWidth(): finestra X grezza 8..247, esatta
#define ARCADE_Y_OFFSET 16  // shift portrait_y -> fb_x, cioe' lx = 247 - fb_x

/* PROGMEM e le pgm_read_* su ESP32 non fanno niente: qui nemmeno esistono */
#define pgm_read_dword_near(p) (*(p))
#define pgm_read_word_near(p)  (*(p))

static const unsigned long  motorace_tilemap[1024][8] = {{0}};
static const unsigned long  motorace_spritemap[4][256][32] = {{{0}}};
static const unsigned short motorace_char_cmap[16][8] = {{0}};
static const unsigned short motorace_spr_cmap[16][8] = {{0}};

struct sprite_S { unsigned short code; unsigned char color, flags; short x, y; };

/* il guscio della classe: solo cio' che i tre metodi toccano davvero */
struct motorace {
    unsigned char  *memory;
    unsigned short *frame_buffer;
    sprite_S       *sprite;
    unsigned char   active_sprites;
    unsigned char   scroll_x_low, scroll_x_high;
    void blit_scroll_strip_t(short strip_r);
    void blit_sprite_t(short strip_r, unsigned char s);
    void render_row(short row);
};

void motorace::blit_scroll_strip_t(short strip_r) {
  int scroll = scroll_x_low + (scroll_x_high << 8);

  for (int sr = 0; sr < 8; sr++) {
    int portrait_x = 223 - (strip_r * 8 + sr);
    if (portrait_x < 0 || portrait_x >= 224) continue;

    // landscape_y arcade fissato per la riga corrente.
    // ⛔ Era `portrait_x + 16`, cioe' solo 16..239: tagliava 16 righe di tilemap
    // per lato, e a destra ci stava la colonna delle citta' dell'HUD.
    // Ora le 256 righe vere entrano tutte nelle 224 colonne disponibili.
    int ly = MR_LY(portrait_x);     // 0..255
    int tile_row = (ly >> 3) & 31;
    int rom_row_base = ly & 7;      // 0..7 (riga dentro al tile)

    bool scroll_active = (ly < 192);

    unsigned short *fb_dst = frame_buffer + sr * FB_W;

    // ⚡⚡ SI AVANZA PER TILE, NON PER PIXEL. Prima ogni pixel rifaceva la
    // ricerca completa del tile — due letture di VRAM, decodifica di
    // codice/palette/flip e la lettura della riga di grafica — ma tutta quella
    // roba cambia solo ogni OTTO pixel, perche' `lx_scrolled` scende di uno per
    // ogni fb_x. Contato per quadro ([[feedback_ottimizzare_contando_accessi]]:
    // prima di toccare l'architettura si contano gli accessi):
    //   letture di VRAM        107.520 -> ~13.900   (-87%)
    //   letture della tilemap   53.760 -> ~6.950    (-87%)
    //   letture della colormap  53.760 -> 53.760    (una per pixel, inevitabile)
    // ⭐ La riscrittura e' **provabilmente equivalente**, non «sembra uguale»:
    // confrontata con la vecchia su 39.936 casi (6 VRAM casuali x 256 righe x 26
    // valori di scroll, tutte e quattro le combinazioni di flip e tutte le fasi
    // di `lxs & 7`) -> **zero pixel diversi**. Se cambiasse un pixel non sarebbe
    // un'ottimizzazione, sarebbe un difetto.
    const unsigned char *vram_row = &memory[MR_MEM_VRAM + tile_row * 128];
    const int lxs0 = 247 + (scroll_active ? scroll : 0);   // lx_scrolled a fb_x = 0

    int fb_x = 0;
    while (fb_x < FB_W) {
      const int lxs = lxs0 - fb_x;              // scende di 1 a ogni fb_x
      const int tile_col = (lxs >> 3) & 63;

      const unsigned char code_lo = vram_row[2 * tile_col];
      const unsigned char attr    = vram_row[2 * tile_col + 1];

      const int tile_code = code_lo + ((attr & 0xC0) << 2);
      const int flip_x = (attr >> 5) & 1;
      const int flip_y = (attr >> 4) & 1;
      const int rom_row = flip_y ? (7 - rom_row_base) : rom_row_base;

      const unsigned long d = pgm_read_dword_near(&motorace_tilemap[tile_code][rom_row]);
      const unsigned short *colors = motorace_char_cmap[attr & 0x0F];

      // pixel che restano dentro a QUESTO tile andando avanti con fb_x (cioe'
      // indietro con lxs): sono (lxs & 7) + 1, tagliati al bordo del quadro.
      const int base = lxs & 7;
      int n = base + 1;
      if (fb_x + n > FB_W) n = FB_W - fb_x;

      // il flip si decide una volta per tile, non a ogni pixel
      if (flip_x) {
        for (int k = 0; k < n; k++)
          fb_dst[fb_x + k] = pgm_read_word_near(&colors[(d >> ((7 - base + k) * 4)) & 7]);
      } else {
        for (int k = 0; k < n; k++)
          fb_dst[fb_x + k] = pgm_read_word_near(&colors[(d >> ((base - k) * 4)) & 7]);
      }
      fb_x += n;
    }
  }
}

void motorace::blit_sprite_t(short strip_r, unsigned char s) {
  const int spr_rawY = sprite[s].x;   // ⛔ Y GREZZA (0..255), non una colonna
  const int spr_y    = sprite[s].y;

  int orientation = sprite[s].flags & 3;
  const unsigned long  *spr_data = motorace_spritemap[orientation][sprite[s].code];
  const unsigned short *colors   = motorace_spr_cmap[sprite[s].color];

  // ⭐ Si scorrono le 16 righe e si scarta quella che non cade in questa
  // striscia. Prima si calcolavano r_min/r_max in forma chiusa, ma con la
  // compressione la corrispondenza riga->colonna non e' piu' una traslazione.
  // ⚡ `MR_PX` e' monotona (verificato su tutti i 256 valori), quindi `sr` puo'
  // solo CALARE al crescere di r: appena esce di sotto si puo' uscire dal ciclo
  // invece di provare le righe rimanenti a vuoto.
  for (int r = 0; r < 16; r++) {
    const int rawY = spr_rawY + r;
    if ((unsigned)rawY > 255) continue;             // fuori dalla tilemap
    const int portrait_x = MR_PX(rawY);             // stessa compressione del fondo
    const int sr = 223 - portrait_x - strip_r * 8;
    if (sr < 0) break;                              // ...e le prossime pure
    if (sr > 7) continue;                           // non ancora in questa striscia

    unsigned long row_lo = pgm_read_dword_near(&spr_data[r * 2]);
    unsigned long row_hi = pgm_read_dword_near(&spr_data[r * 2 + 1]);

    unsigned short *fb_dst = frame_buffer + sr * FB_W;

    for (int c = 0; c < 16; c++) {
      int portrait_y = spr_y + 15 - c;
      int fb_x = portrait_y - ARCADE_Y_OFFSET;
      if (fb_x < 0 || fb_x >= FB_W) continue;

      unsigned char px = (c < 8)
                       ? ((row_lo >> (c * 4)) & 7)
                       : ((row_hi >> ((c - 8) * 4)) & 7);

      if (px) {
        fb_dst[fb_x] = pgm_read_word_near(&colors[px]);
      }
    }
  }
}

void motorace::render_row(short strip_r) {
  if (strip_r < 0 || strip_r >= 28) return;

  // BG scroll (riempie tutta la strip FB_W x 8)
  blit_scroll_strip_t(strip_r);

  // Sprite: filtraggio veloce per strip portrait_x range.
  // ⚠️ `sprite[].x` e' la Y GREZZA: la prima colonna e' MR_PX(x), e dopo la
  // compressione lo sprite ne copre 14, non 16. Si tiene comunque il +15 (una
  // colonna in piu' del necessario): un candidato di troppo lo scarta
  // blit_sprite_t, uno di meno sarebbe uno sprite che sparisce.
  int strip_pX_lo = 216 - strip_r * 8;
  int strip_pX_hi = strip_pX_lo + 7;
  for (unsigned char s = 0; s < active_sprites; s++) {
    int px0 = MR_PX(sprite[s].x);
    if (((px0 + 15) >= strip_pX_lo) && (px0 <= strip_pX_hi)) {
      blit_sprite_t(strip_r, s);
    }
  }
}

int main(void)
{
    static unsigned char  mem[0x2200];
    static unsigned short fb[8 * FB_W];
    static sprite_S       spr[128];
    motorace m;
    m.memory = mem; m.frame_buffer = fb; m.sprite = spr;
    m.active_sprites = 0; m.scroll_x_low = 0; m.scroll_x_high = 0;
    for (int s = 0; s < 28; s++) m.render_row(s);
    printf("OK: i tre blitter di motorace.cpp compilano e girano.\n");
    printf("    FB_W = %d, MR_LY(0)=%d MR_LY(223)=%d, MR_PX(0)=%d MR_PX(255)=%d\n",
           FB_W, MR_LY(0), MR_LY(223), MR_PX(0), MR_PX(255));
    if (MR_LY(223) != 255 || MR_PX(255) != 223) {
        printf("    ERRORE: le due macro non coprono tutto l'intervallo!\n");
        return 1;
    }
    return 0;
}
