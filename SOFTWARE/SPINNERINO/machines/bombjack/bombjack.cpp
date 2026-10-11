// ============================================================================
// bombjack.cpp — Tehkan Bomb Jack (1984), porting FULL33 -> SPINNERINO WIFI.
//
// Core emulazione (rd/wr/out/in/op/run_frame + parsing sprite) IDENTICO a
// FULL33. Differenza unica: il render. FULL33 scriveva strip 224-wide gia'
// "display oriented". Qui i blit scrivono il frame NATIVO completo (224x288)
// in bj_full (PSRAM) — la math dei blit e' invariata, cambia solo la
// destinazione (rt invece di frame_buffer) — e render_row TRASPONE 90 in
// frame_buffer landscape (stride = BJ_NH), come pbaction/1942/pacman.
//
// Incluso dal .ino (TU singola). machines/bombjack/bombjack.cpp e' questo file.
// ============================================================================
#include "config.h"
#include "machines/bombjack/bombjack.h"

#ifdef ENABLE_BOMBJACK
#include "emulation/input.h"

// Boost colori (richiesto utente 2026-06-28): la palette RGB444 del gioco appare
// "smorta" su LCD moderno. Alza saturazione+luminosita' attorno alla luma (stessa
// resa di Road Fighter). Per regolare: BJ_SAT/BJ_VAL. 1.0/1.0 = disattivato.
static inline void bj_boost_rgb(int &r, int &g, int &b) {
  const float BJ_SAT = 1.3f, BJ_VAL = 1.12f;
  float L = 0.299f * r + 0.587f * g + 0.114f * b;
  float nr = (L + (r - L) * BJ_SAT) * BJ_VAL;
  float ng = (L + (g - L) * BJ_SAT) * BJ_VAL;
  float nb = (L + (b - L) * BJ_SAT) * BJ_VAL;
  r = nr < 0 ? 0 : (nr > 255 ? 255 : (int)(nr + 0.5f));
  g = ng < 0 ? 0 : (ng > 255 ? 255 : (int)(ng + 0.5f));
  b = nb < 0 ? 0 : (nb > 255 ? 255 : (int)(nb + 0.5f));
}

void bombjack::reset() {
  machineBase::reset();
  // Frame nativo 224x288 in PSRAM (lazy: solo quando bombjack viene scelto).
  if (!bj_full)
    bj_full = (unsigned short *)ps_malloc(BJ_NW * BJ_NH * sizeof(unsigned short));
  if (!bj_bg)
    bj_bg = (unsigned short *)ps_malloc(BJ_NW * BJ_NH * sizeof(unsigned short));
  bj_bg_cached = -1;   // invalida la cache sfondo a ogni reset/cambio gioco
  m_bg_image = 0;
  m_nmi_on = false;
  m_mmi_skip_audio_cpu = false;
  m_flip = false;
  sound_latch = 0;
  sound_latch_backup = 0;
  ay_address[0] = ay_address[1] = ay_address[2] = 0;
}

unsigned char bombjack::opZ80(unsigned short Addr) {
  if (current_cpu == 0)
    return bombjack_rom_cpu1[Addr];
  else
    return bombjack_rom_cpu2[Addr];
}

unsigned char bombjack::rdZ80(unsigned short Addr) {
  if (current_cpu == 0) {
    // --- Lettura dalle aree ROM mappate ---
    if (Addr >= 0x0000 && Addr <= 0x7FFF) { return bombjack_rom_cpu1[Addr]; }
    if (Addr >= 0xC000 && Addr <= 0xDFFF) { return bombjack_rom_cpu1[Addr]; }

    // --- Gestione delle aree RAM mappate ---
    if (Addr >= 0x8000 && Addr <= 0x8FFF) { return memory[Addr - 0x8000]; } // 4096 bytes
    if (Addr >= 0x9000 && Addr <= 0x93FF) { return memory[Addr - 0x9000 + 0x1000]; } // 1024 bytes
    if (Addr >= 0x9400 && Addr <= 0x97FF) { return memory[Addr - 0x9400 + 0x1000 + 0x400]; } // 1024 bytes
    if (Addr >= 0x9800 && Addr <= 0x987F) { return memory[Addr - 0x9800 + 0x1000 + 0x400 + 0x400]; } // 128 bytes
    if (Addr >= 0x9A00 && Addr <= 0x9A01) { return memory[Addr - 0x9A00 + 0x1000 + 0x400 + 0x400 + 0x80]; } // 2 bytes

    // --- Gestione delle porte di I/O (Input e DIP Switches) ---
    if ((Addr & 0xFFF8) == 0xB000) { // Controlla l'indirizzo base 0xB000 con mirror
      switch (Addr & 0xB007) { // Maschera per i mirror
      case PORT_P1_IN: {
        unsigned char keymask = input->buttons_get();
        unsigned char retval = 0x00; // Inizia con tutti i bit a 0 (rilasciato)

        // Il gioco si aspetta IP_ACTIVE_HIGH, quindi mettiamo il bit a 1 se premuto
        if (keymask & BUTTON_RIGHT)
          retval |= 0x01;
        if (keymask & BUTTON_LEFT)
          retval |= 0x02;
        if (keymask & BUTTON_UP)
          retval |= 0x04;
        if (keymask & BUTTON_DOWN)
          retval |= 0x08;
        if (keymask & BUTTON_FIRE && game_started) //skip service when fire is pressed
          retval |= 0x10;

        return retval;
      }
      case PORT_P2_IN:
        return 0x00;
      case PORT_SYSTEM_IN: {
        unsigned char keymask1 = input->buttons_get();
        unsigned char retval1 = 0x00;

        if (keymask1 & BUTTON_COIN)
          retval1 |= 0x01;
        if (keymask1 & BUTTON_START)
          retval1 |= 0x04;
        return retval1;
      }
      case PORT_WATCHDOG:
        return 0xFF;
      case PORT_DSW1_IN:
        return BOMBJACK_DIP1 | (input->demoSoundsOff() ? BOMBJACK_DIP_DEMOSOUNDS_OFF : BOMBJACK_DIP_DEMOSOUNDS_ON);
      case PORT_DSW2_IN:
        return BOMBJACK_DIP2;
      }
    }
  }
  else if (current_cpu == 1) {
    Addr &= 0x7fff;   // a15 is unused

    // --- Audio CPU Read Logic ---
    if (Addr <= 0x3FFF) { return bombjack_rom_cpu2[Addr]; }

    if (Addr >= 0x4000 && Addr <= 0x47FF) { return memory[(Addr - 0x4000 + 0x1000 + 0x400 + 0x400 + 0x80 + 2)]; }

    // Sound Latch read (0x6000): l'hardware azzera il latch. Quando il latch e'
    // CAMBIATO (nuovo comando rispetto all'ultimo letto), arma lo skip dell'NMI
    // audio del frame successivo (m_mmi_skip_audio_cpu) cosi' la routine che
    // avvia il nuovo brano non viene interrotta dall'NMI VSYNC. Indispensabile per
    // i jingle morte/cambio livello. VERBATIM FULL33 ("Bomb Jack Dissected").
    if (Addr == 0x6000) {
      unsigned char value = sound_latch;
      if (sound_latch_backup != sound_latch)
        m_mmi_skip_audio_cpu = true;
      sound_latch_backup = sound_latch;
      sound_latch = 0;
      return value;
    }
  }

  return 0xFF;
}

void bombjack::wrZ80(unsigned short Addr, unsigned char Value) {
  if (current_cpu == 0) {
    // Trigger game_started: prima scrittura "S" di "SIDE-ONE" (offset ~0x140).
    if (Addr == 0x9144 && Value == 83) {
      if (game_started == 0)
        game_started = 1;
    }
    // --- Scrittura nelle aree RAM mappate ---
    if (Addr >= 0x8000 && Addr <= 0x8FFF) {
      memory[Addr - 0x8000] = Value;
      return;
    }

    if (Addr >= 0x9000 && Addr <= 0x93FF) {
      memory[Addr - 0x9000 + 0x1000] = Value;
      return;
    }

    if (Addr >= 0x9400 && Addr <= 0x97FF) {
      memory[Addr - 0x9400 + 0x1000 + 0x400] = Value;
      return;
    }

    if (Addr >= 0x9800 && Addr <= 0x987F) {
      memory[Addr - 0x9800 + 0x1000 + 0x400 + 0x400] = Value;
      return;
    }

    if (Addr >= 0x9A00 && Addr <= 0x9A01) {
      memory[Addr - 0x9A00 + 0x1000 + 0x400 + 0x400 + 0x80] = Value;
      return;
    }

    if (Addr >= 0x9C00 && Addr <= 0x9CFF) {
      palette[Addr - 0x9C00] = Value;
      uint8_t b_p, b_d;
      if (Addr & 1) {
        b_p = palette[Addr - 0x9C01];
        b_d = Value;
      }
      else {
        b_p = Value;
        b_d = palette[Addr - 0x9C00 + 1];
      }

      // RGB444 -> 8 bit per canale (nibble replicato), poi boost colori.
      int rn = b_p & 0x0F, gn = (b_p >> 4) & 0x0F, bn = b_d & 0x0F;
      int r = rn | (rn << 4), g = gn | (gn << 4), b = bn | (bn << 4);
      bj_boost_rgb(r, g, b);   // colori piu' vivaci (smorti -> carichi), come Road Fighter
      uint16_t c = ((r & 248) << 8) | ((g & 252) << 3) | (b >> 3);

      int i = (Addr - 0x9C00) / 2;
      bombjack_palette[i] = (c >> 8) | (c << 8);
      return;
    }

    if (Addr == 0x9E00) {
      m_bg_image = Value;
      return;
    }

    if (Addr == 0xB000) {
      m_nmi_on = (Value & 0x01);
      return;
    }

    if (Addr == 0xB003) {
      return;
    }

    if (Addr == 0xB004) {
      m_flip = (Value & 0x01);
      return;
    }

    if (Addr == 0xB800) {
      sound_latch = Value;
      return;
    }

    return;
  }
  else if (current_cpu == 1) {
    Addr &= 0x7fff;   // a15 is unused

    if (Addr >= 0x4000 && Addr <= 0x47ff) {
      memory[(Addr - 0x4000 + 0x1000 + 0x400 + 0x400 + 0x80 + 2)] = Value;
      return;
    }
  }
}

void bombjack::outZ80(unsigned short Port, unsigned char Value) {
  int chip_id = -1;
  if ((Port & 0xFF) <= 0x01)
    chip_id = 0;
  else if ((Port & 0xFF) >= 0x10 && (Port & 0xFF) <= 0x11)
    chip_id = 1;
  else if ((Port & 0xFF) >= 0x80 && (Port & 0xFF) <= 0x81)
    chip_id = 2;
  else
    return;

  if ((Port & 1) == 0) { // Scrittura all'indirizzo del registro
    ay_address[chip_id] = Value & 0x0F;
  }
  else { // Scrittura ai dati del registro
    soundregs[(chip_id * 16) + ay_address[chip_id]] = Value;
  }
}

unsigned char bombjack::inZ80(unsigned short Port) {
  if ((Port & 0xFF) == 0x01)
    return soundregs[ay_address[0]];
  else if ((Port & 0xFF) == 0x11)
    return soundregs[(1 * 16) + ay_address[1]];
  else if ((Port & 0xFF) == 0x81)
    return soundregs[(2 * 16) + ay_address[2]];
  else
    return 0xff;
}

// ===== MANOPOLA VELOCITA' GIOCO (frazionaria) =====
// nframes = BJ_SPEED_NUM/BJ_SPEED_DEN frame di gioco per chiamata. Accelera
// gioco E musica insieme (entrambi guidati da run_frame). 10/10 = 1.0x.
#define BJ_SPEED_NUM 10
#define BJ_SPEED_DEN 10

// ===== RAPPORTO CPU AUDIO (step per iterazione) =====
// Clock reale hardware = FULL33 (sorgente funzionante): audio Z80 3 step per 4
// del main (rapporto 3:4). FULL33 fa esattamente `StepZ80(&cpu[1]) x3` per iter.
// Il vecchio overclock a 6.0 (BJ_AUDIO_NUM=60) era un workaround per il render
// < 60fps con emulazione gated; ma sovra-clockare l'audio CPU SBALLA il timing
// del music player -> note a frequenze sbagliate + tempo distorto. Ora bombjack
// e' free-run a 60Hz reali (freeRunEmulation), quindi il rapporto REALE 3.0 da'
// tempo E frequenze corrette come FULL33. NON sovra-cloccare. Vedi
// feedback_no_debug_iterations e spinnerino_wifi_freerun_emulation.
#define BJ_AUDIO_NUM 30   // 30/10 = 3.0 step audio/iter = FULL33
#define BJ_AUDIO_DEN 10

void bombjack::run_frame(void) {
  static unsigned int speed_acc = 0;
  speed_acc += BJ_SPEED_NUM;
  int nframes = 0;
  while (speed_acc >= BJ_SPEED_DEN) { speed_acc -= BJ_SPEED_DEN; nframes++; }

  for (int f = 0; f < nframes; f++) {
    int audio_acc = 0;
    for (int i = 0; i < INST_PER_FRAME; i++) {
      current_cpu = 0; StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
      // CPU audio: BJ_AUDIO_NUM/BJ_AUDIO_DEN step per iterazione (default 6.0).
      audio_acc += BJ_AUDIO_NUM;
      int na = audio_acc / BJ_AUDIO_DEN;
      audio_acc -= na * BJ_AUDIO_DEN;
      current_cpu = 1;
      for (int k = 0; k < na; k++) StepZ80(&cpu[1]);
    }

    // NMI per la CPU Principale (GATED da m_nmi_on, scritto a 0xB000)
    if (m_nmi_on) {
      current_cpu = 0;
      IntZ80(&cpu[0], INT_NMI);
    }

    // NMI per la CPU Audio: VSYNC 60Hz (hardware). SALTATO per UN frame quando
    // e' appena arrivato un NUOVO comando sonoro (m_mmi_skip_audio_cpu, armato in
    // rdZ80 0x6000): da' alla routine di avvio del nuovo brano il tempo di
    // completare senza essere interrotta. Indispensabile per i jingle morte/cambio
    // livello (VERBATIM FULL33). E' un evento raro (1 per comando) -> NON altera il
    // tempo della musica di fondo (la rimozione precedente era un errore).
    if (m_mmi_skip_audio_cpu) {
      m_mmi_skip_audio_cpu = false;
    } else {
      current_cpu = 1;
      IntZ80(&cpu[1], INT_NMI);
    }
  }
}

// ============================================================================
// prepare_frame: parsing sprite (VERBATIM FULL33) + render NATIVO completo in
// bj_full (224x288). Heavy work 1 volta/frame; render_row poi solo traspone.
// ============================================================================
void bombjack::prepare_frame(void) {
  // Driver long-press reset (regola SPINNERINO: chiamare buttons_get ogni frame).
  if (input) input->buttons_get();

  uint8_t large_sprite_rev = (memory[0 + 0x1000 + 0x400 + 0x400 + 0x80] > memory[1 + 0x1000 + 0x400 + 0x400 + 0x80]) ? 1 : 0;
  uint8_t large_sprite_start_idx = memory[large_sprite_rev + 0x1000 + 0x400 + 0x400 + 0x80];
  uint8_t large_sprite_end_idx = memory[(large_sprite_rev + 0x1000 + 0x400 + 0x400 + 0x80) ^ 1];
  active_sprites = 0;

  // Itera sui 32 slot degli sprite hardware, in ordine inverso (da 31 a 0)
  for (int idx = 31; idx >= 0; idx--) {
    if (active_sprites >= 32)
      break;

    unsigned char *sprite_base_ptr = (memory + 0x1000 + 0x400 + 0x400) + 4 * (31 - idx);
    uint8_t code_low = sprite_base_ptr[0];

    if (code_low == 0 && sprite_base_ptr[2] == 0 && sprite_base_ptr[3] == 0)
      continue;

    struct sprite_S spr;

    spr.is_32x32 = (((31 - idx) / 2) > large_sprite_start_idx) && (((31 - idx) / 2) <= large_sprite_end_idx);

    if (spr.is_32x32) {
      if (((31 - idx) % 2) != 0)
        continue;
      spr.code = code_low & 0x3F;
    }
    else {
      spr.code = code_low;
    }

    spr.color_block = (sprite_base_ptr[1] & 0x0F) << 3;
    spr.flip_x = (sprite_base_ptr[1] & 0x40) != 0;
    spr.flip_y = (sprite_base_ptr[1] & 0x80) != 0;

    spr.x = sprite_base_ptr[2] - 16;
    spr.y = sprite_base_ptr[3];

    if (spr.is_32x32)
      spr.x -= 1;

    if ((spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
      sprite[active_sprites++] = spr;
  }

  // --- Render NATIVO 224x288 con CACHE dello sfondo ---
  // Lo sfondo (blit_tile_bg, per-pixel con divisioni) e' il costo dominante ma
  // cambia solo a cambio schermo. Lo ricalcoliamo in bj_bg SOLO quando m_bg_image
  // cambia; ogni frame restauriamo bj_full da bj_bg (memcpy veloce) e sovrapponiamo
  // fg + sprite. Cosi' il render resta sotto i 16ms -> 60 fps reali (gioco+musica
  // a velocita' piena, dato che emulazione e render sono 1:1, vedi emulation.cpp).
  if (bj_full && bj_bg) {
    if (bj_bg_cached != (int)m_bg_image) {
      for (short srow = 0; srow < BJ_NH / 8; srow++) {
        rt = bj_bg + srow * 8 * BJ_NW;
        blit_tile_bg(srow);
      }
      bj_bg_cached = (int)m_bg_image;
    }
    memcpy(bj_full, bj_bg, BJ_NW * BJ_NH * sizeof(unsigned short));

    for (short srow = 0; srow < BJ_NH / 8; srow++) {
      rt = bj_full + srow * 8 * BJ_NW;
      for (char col = 0; col < 28; col++)
        blit_tile_fg(srow, col);
      for (unsigned char s = 0; s < active_sprites; s++)
        blit_sprite(srow, s);
    }
  }
}

// blit_tile_bg: VERBATIM FULL33, output in rt (strip nativa) invece di frame_buffer.
void bombjack::blit_tile_bg(short logical_row) {
  // 1. Controlla se il background e' visibile.
  if ((m_bg_image & 0x10) == 0) {
    memset(rt, 0, BJ_NW * 8 * 2);
    return;
  }

  uint16_t img_base_addr = (m_bg_image & 7) * 0x0200;

  const int visual_x_offset = 16;
  const int visual_y_offset = 24;

  for (int y_in_strip = 0; y_in_strip < 8; y_in_strip++) {
    for (int x_in_strip = 0; x_in_strip < BJ_NW; x_in_strip++) {
      int dest_x = x_in_strip;
      int dest_y = (logical_row * 8) + y_in_strip;

      int shifted_dest_x = dest_x - visual_x_offset;
      int shifted_dest_y = dest_y - visual_y_offset;

      int source_x = shifted_dest_y;
      int source_y = 223 - shifted_dest_x;

      if (source_x < 0 || source_x >= 224 || source_y < 0 || source_y >= 288) {
        rt[(y_in_strip * BJ_NW) + x_in_strip] = 0;
        continue;
      }

      int map_col = source_x / 16;
      int map_row = source_y / 16;

      int pixel_x_in_tile = source_x % 16;
      int pixel_y_in_tile = source_y % 16;

      uint16_t map_addr = img_base_addr + (map_row * 16) + map_col;

      uint8_t tile_code = bombjack_bg_maps[map_addr];
      uint8_t attr = bombjack_bg_maps[map_addr + 0x100];

      uint8_t color_block = (attr & 0x0F) << 3;
      bool flip_y = (attr & 0x80) != 0;

      int final_pixel_y = flip_y ? (15 - pixel_y_in_tile) : pixel_y_in_tile;
      int final_pixel_x = pixel_x_in_tile;

      const unsigned long *tile_gfx = bombjack_bg_tiles[tile_code];

      unsigned long packed_data;
      uint8_t pen;

      if (final_pixel_x < 8) {
        packed_data = tile_gfx[final_pixel_y * 2];
        pen = (packed_data >> (3 * (7 - final_pixel_x))) & 0x07;
      }
      else {
        packed_data = tile_gfx[final_pixel_y * 2 + 1];
        pen = (packed_data >> (3 * (7 - (final_pixel_x - 8)))) & 0x07;
      }

      if (pen == 0) {
        rt[(y_in_strip * BJ_NW) + x_in_strip] = 0;
      }
      else {
        rt[(y_in_strip * BJ_NW) + x_in_strip] = bombjack_palette[color_block | pen];
      }
    }
  }
}

// blit_tile_fg: VERBATIM FULL33, output in rt.
void bombjack::blit_tile_fg(short row, char col) {
  if ((row < 2) || (row >= 34))
    return;

  unsigned short tilemap_index = tileaddr[row][col];

  uint8_t chr = memory[tilemap_index + 0x1000];
  uint8_t clr = memory[tilemap_index + 0x1000 + 0x400];

  uint16_t tile_id = chr | ((clr & 0x10) << 4);
  uint8_t color_block = (clr & 0x0F) << 3;

  const unsigned long *tile_gfx = bombjack_fg_tiles[tile_id];
  unsigned short *ptr = rt + (col * 8);

  for (char r = 0; r < 8; r++) {
    unsigned long packed_pixels = *tile_gfx++;

    for (char c = 0; c < 8; c++) {
      uint8_t pen = (packed_pixels >> (3 * (7 - c))) & 0x07;

      if (pen != 0)
        ptr[c] = bombjack_palette[color_block | pen];
    }
    ptr += BJ_NW;
  }
}

// blit_sprite: VERBATIM FULL33, output in rt.
void bombjack::blit_sprite(short row, unsigned char s_idx) {
  if ((row < 2) || (row >= 34))
    return;

  if (s_idx >= active_sprites)
    return;

  struct sprite_S *s = &sprite[s_idx];
  int size = s->is_32x32 ? 32 : 16;

  const unsigned long *sprite_gfx_data;
  if (s->is_32x32) {
    if (s->code >= 64)
      return;

    sprite_gfx_data = bombjack_sprites_32x32[s->code];
  }
  else {
    if (s->code >= 256)
      return;

    sprite_gfx_data = bombjack_sprites_16x16[s->code];
  }

  int strip_start_y = (row - 2) * 8;
  int strip_end_y = strip_start_y + 8;

  int sprite_start_y = s->y;
  int sprite_end_y = sprite_start_y + size;

  if (sprite_end_y <= strip_start_y || sprite_start_y >= strip_end_y)
    return;

  for (int y_on_screen = strip_start_y; y_on_screen < strip_end_y; y_on_screen++) {
    for (int x_on_screen = 0; x_on_screen < BJ_NW; x_on_screen++) {
      int px_in_sprite_x = x_on_screen - s->x;
      int px_in_sprite_y = y_on_screen - s->y;

      if (px_in_sprite_x < 0 || px_in_sprite_x >= size || px_in_sprite_y < 0 || px_in_sprite_y >= size)
        continue;

      int sx = px_in_sprite_y;
      int sy = (size - 1) - px_in_sprite_x;

      if (s->flip_x)
        sx = (size - 1) - sx;

      if (s->flip_y)
        sy = (size - 1) - sy;

      int pixel_index = sy * size + sx;
      int long_index = pixel_index / 8;
      int bit_offset_in_long = pixel_index % 8;

      unsigned long packed_pixels = sprite_gfx_data[long_index];
      uint8_t pen = (packed_pixels >> (3 * (7 - bit_offset_in_long))) & 0x07;

      if (pen != 0) {
        uint16_t color = bombjack_palette[s->color_block | pen];
        int y_in_strip = y_on_screen - strip_start_y;
        rt[y_in_strip * BJ_NW + x_on_screen] = color;
      }
    }
  }
}

// ============================================================================
// render_row: TRASPONE 90 bj_full (224x288 nativo) -> frame_buffer landscape
// (stride = BJ_NH = 288). Stesso schema di pbaction/1942/pacman.
// ============================================================================
void bombjack::render_row(short strip_r) {
  if (!bj_full) return;
  for (int fb_y_strip = 0; fb_y_strip < 8; fb_y_strip++) {
    int fb_y = strip_r * 8 + fb_y_strip;       // 0..223 (asse corto display)
    int col = (BJ_NW - 1) - fb_y;              // colonna nativa 223..0
    if (col < 0 || col >= BJ_NW) continue;
    unsigned short *dst = frame_buffer + fb_y_strip * BJ_NH;
    const unsigned short *src = bj_full + col;
    for (int y = 0; y < BJ_NH; y++)
      dst[y] = src[y * BJ_NW];
  }
}

const unsigned short *bombjack::logo(void) {
  return bombjack_logo;
}

#endif // ENABLE_BOMBJACK
