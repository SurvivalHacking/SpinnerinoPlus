#include "popeye.h"
#include "emulation/input.h"   // BUTTON_* + Input

// Popeye V3A: first real driver pass.
// Target: boot/run fast enough for hardware test, using the Nintendo TPP2 memory map,
// decrypted program ROM, AY I/O, foreground chars, background bitmap, and first-pass sprites.

#define POPEYE_DSW0_BASE 0x4F  // 1C_1C + Nintendo copyright, DSW1 bit ORed on bit 7
#define POPEYE_DSW1_BASE 0x3D  // 3 lives, medium-hard-ish default, bonus 40000, demo sounds ON, upright

static inline unsigned char bitn(unsigned int v, unsigned char b) { return (v >> b) & 1; }

void popeye::reset() {
    machineBase::reset();

    // V3I: reset deterministico della RAM video Popeye.
    // Sui passaggi menu/attract restavano byte sporchi nel background bitmap RAM:
    // il risultato erano puntini/snow nelle schermate nere e nel bordo del gameplay.
    memset(&memory[MEM_RAMLOW],   0, 0x0400);
    memset(&memory[MEM_DMASRC],   0, 0x0280);
    memset(&memory[MEM_RAMHIGH],  0, 0x0180);
    memset(&memory[MEM_VIDEORAM], 0, 0x0400);
    memset(&memory[MEM_COLORRAM], 0, 0x0400);
    memset(&memory[MEM_BGR],      0, 0x1000);

    ay_addr = 0;
    memset(ay_regs, 0, sizeof(ay_regs));
    dswbit = 0;
    flip_screen = 0;
    field = 0;
    prot0 = prot1 = prot_shift = 0;
    coinFrameCounter = 0;
    coinBackup = 0;
    // CONSOLE_QUADRA_NANO double-buffer + local copy.
    memset(sprite_ram_buf, 0, sizeof(sprite_ram_buf));
    memset(bg_ram_buf, 0, sizeof(bg_ram_buf));
    memset(vram_buf, 0, sizeof(vram_buf));
    memset(cram_buf, 0, sizeof(cram_buf));
    memset(bg_scroll_buf, 0, sizeof(bg_scroll_buf));
    palette_bank_buf[0] = palette_bank_buf[1] = 0;
    dma_write_idx = 0;
    memset(render_sprite_ram, 0, sizeof(render_sprite_ram));
    render_bg_scroll[0] = render_bg_scroll[1] = render_bg_scroll[2] = 0;
    render_palette_bank = 0;
    memset(render_bgr, 0, sizeof(render_bgr));
    memset(render_vram, 0, sizeof(render_vram));
    memset(render_cram, 0, sizeof(render_cram));
    nmi_enabled = 0;
    watchdog_enabled = 0;
    watchdog_counter = 0;
    // Popeye has no separate sound CPU in this driver path; clear the shared AY regs.
    memset(soundregs, 0, sizeof(soundregs));
}

unsigned char popeye::opZ80(unsigned short Addr) {
    if (Addr < 0x8000) return pgm_read_byte(&popeye_rom_main[Addr]);
    return rdZ80(Addr);
}

unsigned char popeye::rdZ80(unsigned short Addr) {
    if (Addr < 0x8000) return pgm_read_byte(&popeye_rom_main[Addr]);

    // TPP2 map: 0x8000-0x87ff is unpopulated on the protected rev-D board.
    if (Addr >= 0x8000 && Addr <= 0x87FF) return 0x7F;

    if (Addr >= 0x8800 && Addr <= 0x8BFF) return memory[MEM_RAMLOW + (Addr & 0x03FF)];
    if (Addr >= 0x8C00 && Addr <= 0x8E7F) return memory[MEM_DMASRC + (Addr - 0x8C00)];
    if (Addr >= 0x8E80 && Addr <= 0x8FFF) return memory[MEM_RAMHIGH + (Addr - 0x8E80)];
    if (Addr >= 0xA000 && Addr <= 0xA3FF) return memory[MEM_VIDEORAM + (Addr & 0x03FF)];
    if (Addr >= 0xA400 && Addr <= 0xA7FF) return memory[MEM_COLORRAM + (Addr & 0x03FF)];

    if (Addr == 0xE000) {
      return ((prot1 << prot_shift) | (prot0 >> (8 - prot_shift))) & 0xFF;
    }
    if (Addr == 0xE001) {
      return 0x00; // hardware protection status: game checks bit 2 clear
    }

    return 0xFF;
}

void popeye::wrZ80(unsigned short Addr, unsigned char Value) {
    if (Addr >= 0x8800 && Addr <= 0x8BFF) { memory[MEM_RAMLOW + (Addr & 0x03FF)] = Value; return; }
    if (Addr >= 0x8C00 && Addr <= 0x8E7F) { memory[MEM_DMASRC + (Addr - 0x8C00)] = Value; return; }
    if (Addr >= 0x8E80 && Addr <= 0x8FFF) { memory[MEM_RAMHIGH + (Addr - 0x8E80)] = Value; return; }

    if (Addr >= 0xA000 && Addr <= 0xA3FF) {
      if (!game_started) game_started = 1;
      memory[MEM_VIDEORAM + (Addr & 0x03FF)] = Value;
      return;
    }
    if (Addr >= 0xA400 && Addr <= 0xA7FF) { memory[MEM_COLORRAM + (Addr & 0x03FF)] = Value; return; }

    // TPP2 background bitmap RAM: 0xc000-0xdfff. Offset bit 12 selects low/high nibble.
    if (Addr >= 0xC000 && Addr <= 0xDFFF) {
      unsigned short off = Addr & 0x0FFF;
      unsigned char shift = (Addr & 0x1000) ? 4 : 0;
      unsigned char old = memory[MEM_BGR + off];
      memory[MEM_BGR + off] = (old & ~(0x0F << shift)) | ((Value & 0x0F) << shift);
      return;
    }

    if (Addr == 0xE000) { prot_shift = Value & 0x07; return; }
    if (Addr == 0xE001) { prot0 = prot1; prot1 = Value; return; }
}

unsigned char popeye::read_input_p1() {
    unsigned char k = input->buttons_get();
    unsigned char v = 0x00; // active high
    if (k & BUTTON_RIGHT) v |= 0x01;
    if (k & BUTTON_LEFT)  v |= 0x02;
    if (k & BUTTON_UP)    v |= 0x04;
    if (k & BUTTON_DOWN)  v |= 0x08;
    if (k & BUTTON_FIRE)  v |= 0x10;
    return v;
}

unsigned char popeye::read_input_p2() {
    return 0x00; // cocktail/player 2 not wired on this cabinet profile
}

unsigned char popeye::read_input_system() {
    unsigned char k = input->buttons_get();
    unsigned char v = 0x10; // even/odd field bit; toggled in prepare_frame below
    if (field) v = 0x00;

    if ((k & BUTTON_COIN) && !coinBackup) {
      coinFrameCounter = 3;
      coinBackup = 1;
    }
    if (coinFrameCounter > 0) v |= 0x80;       // COIN1 active high
    else if ((k & BUTTON_COIN) == 0) coinBackup = 0;

    if (k & (BUTTON_START | BUTTON_EXTRA)) v |= 0x04; // START1 active high
    return v;
}

unsigned char popeye::read_dsw1() {
    unsigned char v = POPEYE_DSW1_BASE;
    if (input->demoSoundsOff()) v |= 0x40; else v &= ~0x40;
    return v;
}

unsigned char popeye::read_dsw0() {
    unsigned char dsw1bit = (read_dsw1() >> (dswbit & 7)) & 1;
    return (POPEYE_DSW0_BASE & 0x7F) | (dsw1bit ? 0x80 : 0x00);
}

unsigned char popeye::inZ80(unsigned short Port) {
    switch (Port & 0x03) {
      case 0x00: return read_input_p1();
      case 0x01: return read_input_p2();
      case 0x02: return read_input_system();
      case 0x03: {
        unsigned char reg = ay_addr & 0x0F;
        if (reg == 14) return read_dsw0();
        return ay_regs[reg];
      }
    }
    return 0xFF;
}

void popeye::outZ80(unsigned short Port, unsigned char Value) {
    switch (Port & 0x03) {
      case 0x00:
        ay_addr = Value & 0x0F;
        break;
      case 0x01: {
        unsigned char reg = ay_addr & 0x0F;
        ay_regs[reg] = Value;
        if (reg < 14) soundregs[reg] = Value;
        if (reg == 15) {
          flip_screen = Value & 0x01;
          dswbit = (Value >> 1) & 0x07;
        }
        break;
      }
      default:
        break;
    }
}

void popeye::run_frame(void) {
    // POPEYE TPP2 Z80 a 4 MHz @ 60Hz = 66666 cicli/vblank, ~9523 istruzioni.
    // 7 step (8750) era ~92% (l'utente vede fluidita' ok + musica ok ma sprite
    // ancora frantumati = CPU non finisce). 8 step (10000) = ~105% del clock
    // reale = CPU completa update sprite prima del snapshot. Budget liberato
    // dalle ottimizzazioni LUT chars/sprite/bg dovrebbe coprire l'aumento.
    current_cpu = 0;
    // FIX musica troppo lenta nell'attract Popeye (2026-05-30): la melodia
    // dipende dal NMI rate (60/sec standard). Aggiungo un NMI a meta' del
    // frame ogni 6 run_frame -> 70 NMI/sec medio = canzone a 1.17x velocita'.
    static unsigned char extra_nmi_cnt = 0;
    for (int i = 0; i < INST_PER_FRAME / 2; i++) {
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
    }
    // Mid-frame NMI ogni 6 run_frame per +17% music speed
    extra_nmi_cnt++;
    if (extra_nmi_cnt >= 6) extra_nmi_cnt = 0;
    if (extra_nmi_cnt == 0 && (cpu[0].I & 0x01)) {
      IntZ80(&cpu[0], INT_NMI);
    }
    for (int i = 0; i < INST_PER_FRAME / 2; i++) {
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
      StepZ80(&cpu[0]);
    }

    // CONSOLE_QUADRA_NANO: snapshot DMA al vblank con DOUBLE BUFFER cross-core.
    // L'emul task scrive nel buffer NON in uso dal main task, poi swappa l'idx
    // (single-byte volatile = atomic su ESP32). Il main task legge l'altro
    // buffer per tutto il frame display, garantendo coerenza. Risolve il bug
    // "puntini colorati su personaggi in movimento" (race tra half0/half1 del
    // rendering display nel ramo VIDEO_HALF_RATE).
    unsigned char nidx = dma_write_idx ^ 1;
    memcpy(sprite_ram_buf[nidx], &memory[MEM_DMASRC], 0x280);
    // Snapshot ATOMICO di TUTTO memory che il rendering legge.
    // Senza questi, race condition con wrZ80 (emul task) che scrive bg/vram
    // in continuazione -> garbage random sotto sprite in movimento.
    memcpy(bg_ram_buf[nidx],     &memory[MEM_BGR],      0x1000);
    memcpy(vram_buf[nidx],       &memory[MEM_VIDEORAM], 0x400);
    memcpy(cram_buf[nidx],       &memory[MEM_COLORRAM], 0x400);
    bg_scroll_buf[nidx][0] = memory[MEM_DMASRC + 0];
    bg_scroll_buf[nidx][1] = memory[MEM_DMASRC + 1];
    bg_scroll_buf[nidx][2] = memory[MEM_DMASRC + 2];
    palette_bank_buf[nidx] = memory[MEM_DMASRC + 3];
    dma_write_idx = nidx;                 // atomic swap
    // CONSOLE_QUADRA_NANO FIX (puntini sprite in movimento):
    // L'hardware Popeye TPP2 e' interlaced 60i. La CPU calcola sprite Y diversa
    // per ogni field (vedi jtpopeye: obj_addr include DJ[0]^~INITEO). Su CRT
    // i due field si fondono percettivamente. Su LCD progressive vediamo
    // ENTRAMBI i field sovrapposti in ogni frame -> personaggi in movimento
    // = doppia traccia + samples sprite ROM da indirizzi diversi = puntini.
    // Forziamo field=0 sempre: la CPU vede solo un field -> singola traccia.
    // field ^= 1;     // disabilitato per display progressive (era: alternanza CRT)
    field = 0;

    // CONSOLE_QUADRA_NANO FIX (2026-05-28) — NMI enable via Z80 I register.
    // MAME `tnx1_state::refresh_w()` ricava nmi_enabled = bit 8 dell'address
    // refresh = bit 0 del registro I del Z80 (`LD I, A` set/clear). Il gioco
    // Popeye TPP2 disabilita NMI durante le routine critiche di update sprite
    // per evitare interruzioni mid-aggiornamento. Il nostro Z80 (Galagino fake)
    // NON ha refresh callback, ma espone `cpu[0].I` direttamente via struct.
    // Leggi bit 0 di I per replicare MAME nmi_enabled gate -> NMI solo quando
    // il gioco lo permette -> sprite update completi -> niente "puntini in
    // movimento" su Olive Oyl/Wimpy.
    nmi_enabled = (cpu[0].I & 0x01);
    if (nmi_enabled) {
      IntZ80(&cpu[0], INT_NMI);
    }
    if (coinFrameCounter > 0) coinFrameCounter--;
}

void popeye::prepare_frame(void) {
    // CONSOLE_QUADRA_NANO race-free snapshot: copia LOCAL del buffer in scrittura
    // dell'emul task. Con 2 buf alternati e 2 run_frame/frame display, l'emul
    // sovrascriverebbe il buf attivo a meta' rendering -> puntini colorati.
    // Soluzione: il main fa memcpy in prepare_frame (~1ms, atomico vs emul che
    // impiega ~10ms per swap). Poi il rendering legge dai render_*[] locali,
    // safe da sovrascritte concorrenti.
    unsigned char w = dma_write_idx;   // single-byte atomic read
    memcpy(render_sprite_ram, sprite_ram_buf[w], 0x280);
    // Copy DA DOUBLE BUFFER (race-free) NON da memory[] direttamente.
    memcpy(render_bgr,        bg_ram_buf[w],     0x1000);
    memcpy(render_vram,       vram_buf[w],       0x400);
    memcpy(render_cram,       cram_buf[w],       0x400);
    render_bg_scroll[0] = bg_scroll_buf[w][0];
    render_bg_scroll[1] = bg_scroll_buf[w][1];
    render_bg_scroll[2] = bg_scroll_buf[w][2];
    render_palette_bank = palette_bank_buf[w];
}

unsigned short popeye::rgb_from_prom(unsigned char prom, bool invert, bool background) {
    unsigned char c = invert ? ((~prom) & 0xFF) : prom;
    int r, g, b;
    if (background) {
      r = 0x2E * ((c >> 0) & 1) + 0x50 * ((c >> 1) & 1) + 0x81 * ((c >> 2) & 1);
      g = 0x2E * ((c >> 3) & 1) + 0x50 * ((c >> 4) & 1) + 0x81 * ((c >> 5) & 1);
      b = 0x55 * ((c >> 6) & 1) + 0xAA * ((c >> 7) & 1);
    } else {
      r = 0x21 * ((c >> 0) & 1) + 0x47 * ((c >> 1) & 1) + 0x97 * ((c >> 2) & 1);
      g = 0x21 * ((c >> 3) & 1) + 0x47 * ((c >> 4) & 1) + 0x97 * ((c >> 5) & 1);
      b = 0x55 * ((c >> 6) & 1) + 0xAA * ((c >> 7) & 1);
    }
    if (r > 255) r = 255; if (g > 255) g = 255; if (b > 255) b = 255;
    unsigned short rgb = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xF8) >> 3);
    return (rgb >> 8) | (rgb << 8);
}

unsigned short popeye::bg_color(unsigned char pen) {
    unsigned char pb = render_palette_bank;
    unsigned char bank = (pb & 0x08) ? 16 : 0;
    unsigned char prom = pgm_read_byte(&popeye_prom_4a[bank + (pen & 0x0F)]);
    return rgb_from_prom(prom, true, true);
}

unsigned short popeye::char_color(unsigned char color, unsigned char pen) {
    if (!pen) return 0x0000;
    unsigned char pb = render_palette_bank;
    unsigned char bank = (pb & 0x08) ? 16 : 0;
    unsigned char prom = pgm_read_byte(&popeye_prom_3a[bank + (color & 0x0F)]);
    return rgb_from_prom(prom, true, false);
}

unsigned short popeye::sprite_color(unsigned char color, unsigned char pen) {
    if (!pen) return 0x0000;
    unsigned char pb = render_palette_bank;
    // Indirizzo PROM colore sprite (5a/5b, 256 byte): 32 entry per palette bank
    // = 8 color x 4 pen. MAME popeye_v.cpp: color_prom = base + 32*(pb&7), poi
    // color*4 + pen. FBNeo d_popeye.cpp: (pb&7)<<3 | color, poi *4 + pen.
    // => indirizzo a 8 bit pieno, SENZA alcun "address tie".
    // BUG FIX (personaggi bianchi dal livello 2): la vecchia formula
    //   idx = (raw & 0x3F) | ((raw & 0x20) << 1)
    // scartava i bit 1-2 del palette bank (raw bit6/7) e duplicava il bit0.
    // Al livello 1 pb&7==0 quindi non cambiava nulla; dai livelli successivi
    // pb&7!=0 -> indice errato -> PROM=0x00 -> invertito a 0xFF = BIANCO.
    // Il remap (color & 0x08)<<1 esiste solo per Sky Skipper, non per Popeye.
    unsigned int raw = ((pb & 0x07) << 5) | ((color & 0x07) << 2) | (pen & 0x03);
    unsigned int idx = raw & 0xFF;        // raw e' gia' 0..255, indirizzo diretto
    unsigned char lo = pgm_read_byte(&popeye_prom_5b[idx]) & 0x0F;
    unsigned char hi = pgm_read_byte(&popeye_prom_5a[idx]) & 0x0F;
    unsigned char prom = lo | (hi << 4);
    return rgb_from_prom(prom, true, false);
}

unsigned char popeye::char_pen(unsigned char code, unsigned char x, unsigned char y) {
    // V3E: TPP2 5N character ROM has an empty first half in this ROM dump.
    // MAME effectively continues the second 0x800 bytes over the first half; in
    // our packed popeye_rom_video[] the raw 5N data is appended at 0x8000, so
    // the usable character glyphs start at 0x8800. Reading 0x8000 left the real
    // foreground/text layer blank and made the title/game texts look broken.
    unsigned int off = 0x8800 + (((unsigned int)code & 0xFF) * 8) + (y & 7);
    unsigned char b = pgm_read_byte(&popeye_rom_video[off]);
    return (b >> (7 - (x & 7))) & 1;
}

unsigned char popeye::sprite_pen(unsigned short code, unsigned char x, unsigned char y) {
    code &= 0x1FF;
    unsigned int row = 15 - (y & 0x0F);
    unsigned int base = ((unsigned int)code * 16) + row;
    unsigned char bit;
    unsigned char p0, p1;
    if (x < 8) {
      bit = 7 - x;
      p0 = (pgm_read_byte(&popeye_rom_video[0x2000 + base]) >> bit) & 1;
      p1 = (pgm_read_byte(&popeye_rom_video[0x6000 + base]) >> bit) & 1;
    } else {
      bit = 15 - x;
      p0 = (pgm_read_byte(&popeye_rom_video[0x0000 + base]) >> bit) & 1;
      p1 = (pgm_read_byte(&popeye_rom_video[0x4000 + base]) >> bit) & 1;
    }
    return p0 | (p1 << 1);
}

void popeye::render_original_line(unsigned short *line, int y) {
    // ROLLBACK fix MAME-accurate: il sorgente Game_pack_2 aveva una formula
    // sua (custom shift su rovi&0x80 + row_addr su rovi>>1) calibrata per il
    // suo wrZ80 layout. Cambiarla a MAME-accurate ha peggiorato lo sfondo.
    // Ritorno alla formula originale + ottimizzazioni (bg LUT, no recalc rovi).
    const unsigned char *bgs = render_bg_scroll;
    unsigned int rovi = (flip_screen ? ((y / 2) ^ 0xFF) : (y / 2)) + bgs[1];

    if (!(rovi & 0x100)) {
      unsigned short bg0 = bg_color(0);
      for (int x = 0; x < ORIG_W; x++) line[x] = bg0;
    } else {
      unsigned short bg_lut[16];
      for (int p = 0; p < 16; p++) bg_lut[p] = bg_color(p);

      unsigned char shift = (rovi & 0x80) ? 4 : 0;
      unsigned int row_addr = ((rovi >> 1) & 0x3F) << 6;
      for (int x = 0; x < ORIG_W; x++) {
        unsigned char roh = 0x38 + (x / 2) + bgs[0];
        unsigned int addr = row_addr | ((roh >> 2) & 0x3F);
        // Local bg snapshot (race-free, no concurrent CPU writes).
        unsigned char pen = (render_bgr[addr & 0x0FFF] >> shift) & 0x0F;
        line[x] = bg_lut[pen];
      }
    }

    draw_sprites_line(line, y);
    draw_chars_line(line, y);
}

void popeye::draw_sprites_line(unsigned short *line, int y) {
    // V3K: sprite line-buffer hardware emulation.
    // Popeye non disegna gli sprite direttamente sul background. Prima copia, per
    // ogni scanline, gli attributi in una piccola line buffer da 64 posizioni
    // indicizzata con X>>2; se due sprite condividono quella posizione, l'ultima
    // entry DMA sovrascrive la precedente. Poi disegna la line buffer in una
    // bitmap sprite separata. Il pen 0 dello sprite NON e' trasparente rispetto
    // agli altri sprite: cancella il pixel sprite precedente. Diventa trasparente
    // solo nel copy finale verso background/foreground.
    struct attr_t {
      int sx;
      int row;
      unsigned char color;
      unsigned short code;
      unsigned char flipx;
      unsigned char flipy;
    } attrs[64];

    memset(attrs, 0, sizeof(attrs));
    // Local snapshot owned by main task (race-free).
    const unsigned char *sr = render_sprite_ram;

    for (int offs = 4; offs < 0x280; offs += 4) {
      // FBNeo fix: skip slot DMA con X=0 (ghost sprite invalid).
      if (sr[offs] == 0) continue;
      int sy = 0x200 - (sr[offs + 1] * 2);
      int row = y - sy;
      if (flip_screen) {
        sy ^= 0x1FF;
        row = sy - y;
      }

      if (row >= 0 && row < 16) {
        attr_t *a = &attrs[sr[offs] >> 2];
        a->sx = sr[offs] * 2;
        a->row = row;
        a->code = (((sr[offs + 2] & 0x7F) +
                   ((sr[offs + 3] & 0x10) << 3) +
                   ((sr[offs + 3] & 0x04) << 6)) ^ 0x1FF) & 0x1FF;
        a->color = sr[offs + 3] & 0x07;
        a->flipx = (sr[offs + 2] & 0x80) ? 0x0F : 0x00;
        a->flipy = (sr[offs + 3] & 0x08) ? 0x0F : 0x00;
      }
    }

    static unsigned short sprline[ORIG_W];
    memset(sprline, 0, sizeof(sprline));

    for (int i = 0; i < 64; i++) {
      attr_t *a = &attrs[i];
      if (a->color == 0) continue;

      unsigned char src_y = (a->row ^ a->flipy) & 0x0F;
      // MiSTer Y LSB override DISABILITATO (test - potrebbe interferire).

      // PERF: pre-read 4 byte ROM (p0/p1 left/right) UNA volta per sprite-row.
      // Prima: 2 pgm_read per pixel × 16 = 32 pgm_read per sprite-row.
      // Dopo: 4 pgm_read totali, poi unpacking inline.
      unsigned int row = 15 - (src_y & 0x0F);
      unsigned int base = ((unsigned int)(a->code & 0x1FF) * 16) + row;
      // NO XOR, solo bit-swap orizzontale.
      unsigned char p0l = pgm_read_byte(&popeye_rom_video[0x2000 + base]);
      unsigned char p1l = pgm_read_byte(&popeye_rom_video[0x6000 + base]);
      unsigned char p0r = pgm_read_byte(&popeye_rom_video[0x0000 + base]);
      unsigned char p1r = pgm_read_byte(&popeye_rom_video[0x4000 + base]);

      // PERF: pre-LUT sprite_color per i 4 pen possibili dello sprite color.
      // Prima: fino a 16 chiamate sprite_color per sprite-row.
      // Dopo: 3 chiamate (pen 0 e' sempre 0 = cancellazione).
      unsigned short color_lut[4];
      color_lut[0] = 0;
      color_lut[1] = sprite_color(a->color, 1);
      color_lut[2] = sprite_color(a->color, 2);
      color_lut[3] = sprite_color(a->color, 3);

      for (int x = 0; x < 16; x++) {
        int px = a->sx + x - 6;
        if (px < 0 || px >= ORIG_W) continue;

        int outx = flip_screen ? (px ^ 0x1FF) : px;
        unsigned char src_x = (x ^ a->flipx) & 0x0F;
        unsigned char pen;
        // FIX colore POPEYE (2026-05-30): plane 0 e plane 1 erano swappati
        // nel pen. pen=2 con color=1 pb=0 -> PROM=0x50 -> R=255 G=184 B=170 =
        // ROSA CARNE (faccia Popeye corretta). pen=1 stesso input -> PROM=0x7F
        // -> R=0 G=0 B=170 = BLU. Con swap p0<->p1 nel bit basso/alto del pen,
        // faccia Popeye e cuori escono coi colori giusti. Bit-swap orizz LSB
        // mantenuto. Confermato su GALAGONE 3.5 sambi.
        if (src_x < 8) {
          unsigned char bit = src_x;
          pen = ((p1l >> bit) & 1) | (((p0l >> bit) & 1) << 1);
        } else {
          unsigned char bit = src_x - 8;
          pen = ((p1r >> bit) & 1) | (((p0r >> bit) & 1) << 1);
        }
        // pen 0 cancella sprite precedente nella sprline (color_lut[0]=0).
        sprline[outx] = color_lut[pen];
      }
    }

    for (int x = 0; x < ORIG_W; x++) {
      if (sprline[x]) line[x] = sprline[x];
    }
}

void popeye::draw_chars_line(unsigned short *line, int y) {
    int ty = y >> 4;
    if (ty < 0 || ty >= 32) return;
    int py16 = y & 0x0F;
    unsigned char py = py16 >> 1; // chars are 8x8 scaled 2x

    // PERF: pre-LUT char_color per i 16 color possibili (pen sempre 1).
    // Da 512 pgm_read sprintf scanline a 16. Big saving.
    unsigned short char_lut[16];
    for (int c = 0; c < 16; c++) char_lut[c] = char_color(c, 1);

    for (int tx = 0; tx < 32; tx++) {
      // V3F: Popeye foreground RAM is physically scanned in the opposite X order
      // from the framebuffer used by this portrait renderer. V3E selected the
      // correct 5N character ROM half, but every text row still appeared reversed
      // (EROCS TSEB, flipped HUD names, etc.). Keep the glyph bit order unchanged
      // and mirror only the tile column lookup.
      int src_tx = flip_screen ? tx : (31 - tx);
      unsigned int idx = (ty * 32 + src_tx) & 0x03FF;
      // Local snapshot (race-free).
      unsigned char code = render_vram[idx];
      unsigned char color = render_cram[idx] & 0x0F;

      // PERF: pre-read del glyph byte 1 sola volta per char-row (era 16 read).
      unsigned int glyph_off = 0x8800 + (((unsigned int)code & 0xFF) * 8) + (py & 7);
      unsigned char glyph = pgm_read_byte(&popeye_rom_video[glyph_off]);

      int x0 = tx << 4;
      for (int px16 = 0; px16 < 16; px16++) {
        unsigned char px = px16 >> 1;
        unsigned char pen = (glyph >> (7 - (px & 7))) & 1;
        if (pen) {
          // V3H: keep background/sprites in the V3F/V3E orientation, but mirror
          // only the foreground/text pixels. V3G mirrored the whole final frame:
          // that made the text readable, but also mirrored the playfield and
          // inverted the controls. This applies the same X correction only to
          // the Popeye text/HUD layer.
          int outx = (ORIG_W - 1) - (x0 + px16);
          if (outx >= 0 && outx < ORIG_W) line[outx] = char_lut[color];
        }
      }
    }
}

// ============================================================================
// V9A — Attract screen overlay
// ============================================================================
//
// We detect the attract "star + face" screen and paint four pre-rendered
// images on top of the rendered frame buffer: the face inside the star, the
// POPEYE logo, the "Nintendo" caption, and the "King Features Syndicate, Inc."
// caption. The "©1982" green text to the left of the captions is left to the
// CPU emulation (which renders it correctly).
//
// Detection: read VRAM tiles in the rows known (from diagnostic dump V8F)
// to contain the star+face artwork. If the expected pattern is present, we
// activate the overlay; otherwise we leave the original rendering alone. The
// signature tiles checked are:
//   - VRAM row 6, col 15-16 = 0x70, 0x71  (top of star outline)
//   - VRAM row 13, col 12-13 = 0x94, 0x95 (mid of face)
//   - VRAM row 19, col 12-13 = 0xC3, 0xC4 (bottom of star)
// All three must match to consider the screen as "attract with star".
//
bool popeye::is_attract_with_star() {
    if (render_vram[6 * 32 + 15] != 0x70) return false;
    if (render_vram[6 * 32 + 16] != 0x71) return false;
    if (render_vram[13 * 32 + 12] != 0x94) return false;
    if (render_vram[19 * 32 + 12] != 0xC3) return false;
    return true;
}

// Decode a single row of an RLE-encoded overlay image, writing up to
// (dst_max_x - dst_x) pixels into dst starting at dst[dst_x]. Pixels with
// value 0x0000 are treated as transparent and skipped (keeps the existing
// frame buffer content).
//
// The RLE format matches popeye_logo.h / menu_logo() in menu.cpp:
//   src[0]    = marker
//   src[1..]  = stream of either
//                - single pixel (value != marker)
//                - or three-element run (marker, count-1, color)
//
// V9B: previous version had a bug where, when the FIRST run of a row was
// processed during the "skip to target row" phase as a straddling run, the
// run was correctly drawn but the internal out_x cursor wasn't advanced. All
// subsequent pixels (singles or runs) of the same row would then be drawn
// from out_x=0 again, overwriting the straddling-run output and shifting the
// whole image left. Fix: use a single position variable (rel = pixel offset
// from start of target row) for BOTH single pixels and runs; drop out_x.
//
void popeye::rle_decode_to(const unsigned short *src, int target_row,
                           int img_w, unsigned short *dst, int dst_x,
                           int dst_max_x) {
    unsigned short marker = pgm_read_word(&src[0]);
    int data_idx = 1;
    int pixel_idx = 0;
    int target_start = target_row * img_w;
    int target_end   = target_start + img_w;

    // Skip ahead to target_row's first pixel without writing anything.
    // We exit when either pixel_idx reaches target_start, or we hit a run
    // that straddles target_start (in which case we leave the loop with
    // pixel_idx < target_start so the main loop processes the run with
    // proper clipping).
    while (pixel_idx < target_start) {
        unsigned short v = pgm_read_word(&src[data_idx]);
        if (v != marker) {
            pixel_idx++;
            data_idx++;
        } else {
            unsigned short run_len = pgm_read_word(&src[data_idx + 1]) + 1;
            if (pixel_idx + run_len <= target_start) {
                pixel_idx += run_len;
                data_idx += 3;
            } else {
                // Straddling run — main loop handles it.
                break;
            }
        }
    }

    // Write pixels of target row. Single position variable `pixel_idx` drives
    // everything; the relative position inside the row is `pixel_idx - target_start`.
    while (pixel_idx < target_end) {
        unsigned short v = pgm_read_word(&src[data_idx]);
        if (v != marker) {
            // Single pixel.
            if (pixel_idx >= target_start) {
                int rel = pixel_idx - target_start;
                int dx = dst_x + rel;
                if (v != 0x0000 && dx >= 0 && dx < dst_max_x) {
                    dst[dx] = v;
                }
            }
            pixel_idx++;
            data_idx++;
        } else {
            // Run.
            unsigned short run_len   = pgm_read_word(&src[data_idx + 1]) + 1;
            unsigned short run_color = pgm_read_word(&src[data_idx + 2]);
            int run_start = pixel_idx;
            int run_end   = pixel_idx + run_len;
            // How many pixels of this run fall inside our target row.
            int draw_start = (run_start > target_start) ? run_start : target_start;
            int draw_end   = (run_end   < target_end)   ? run_end   : target_end;
            int draw_count = draw_end - draw_start;
            if (draw_count > 0 && run_color != 0x0000) {
                int rel = draw_start - target_start;
                for (int i = 0; i < draw_count; i++) {
                    int dx = dst_x + rel + i;
                    if (dx >= 0 && dx < dst_max_x) dst[dx] = run_color;
                }
            }
            pixel_idx = run_end;
            data_idx += 3;
        }
    }
}

// Overlay the four pre-rendered images on top of the current frame_buffer
// 8-line band. `row` is 0..35 (band index), the frame buffer holds 240×8
// pixels at positions [row*8 .. row*8+7] of the 240×288 logical screen.
//
// Layout V9J (final attract overlay - full blackout below HUD):
//   popeye_face_overlay    170×145 at (35, 30)
//   popeye_logo_overlay    190×46  at (25, 175)
//   popeye_nintendo_overlay 120×16 at (60, 232)    -- composite "©1982 Nintendo"
//   popeye_king_features_overlay 218×16 at (11, 257) -- composite "©1982 King..."
//
// V9J: the WHOLE attract area below the HUD (y >= ATTRACT_BLACKOUT_Y0) is
// blacked out before drawing the four overlay images. This makes the original
// CPU-rendered attract sprites/chars completely invisible, leaving only our
// images on screen. The "©1982" green text is now embedded in the Nintendo
// and King composite images (no longer relying on CPU rendering for it).
//
static const int ATTRACT_BLACKOUT_Y0 = 18; // below HUD, above attract content
static const int ATTRACT_BLACKOUT_Y1 = 288; // bottom of frame

void popeye::overlay_attract_row(short row) {
    int band_y0 = row * 8;
    int band_y1 = band_y0 + 8; // exclusive

    // V9J: full blackout of attract area (y >= ATTRACT_BLACKOUT_Y0).
    // For each TFT scanline in the band that falls in the attract area,
    // zero the entire frame buffer row.
    for (int ly = 0; ly < 8; ly++) {
        int tft_y = band_y0 + ly;
        if (tft_y >= ATTRACT_BLACKOUT_Y0 && tft_y < ATTRACT_BLACKOUT_Y1) {
            unsigned short *dst = frame_buffer + ly * SRC_W;
            for (int x = 0; x < SRC_W; x++) dst[x] = 0x0000;
        }
    }

    // Now draw the four overlay images on top.
    struct OverlaySpec {
        const unsigned short *data;
        int img_w, img_h;
        int dst_x, dst_y;
    };
    static const OverlaySpec specs[] = {
        { popeye_face_overlay,          POPEYE_FACE_OVERLAY_WIDTH,
                                        POPEYE_FACE_OVERLAY_HEIGHT,           35,  30 },
        { popeye_logo_overlay,          POPEYE_LOGO_OVERLAY_WIDTH,
                                        POPEYE_LOGO_OVERLAY_HEIGHT,           25, 175 },
        { popeye_nintendo_overlay,      POPEYE_NINTENDO_OVERLAY_WIDTH,
                                        POPEYE_NINTENDO_OVERLAY_HEIGHT,       60, 232 },
        { popeye_king_features_overlay, POPEYE_KING_FEATURES_OVERLAY_WIDTH,
                                        POPEYE_KING_FEATURES_OVERLAY_HEIGHT,  11, 257 },
    };
    const int num_specs = sizeof(specs) / sizeof(specs[0]);

    for (int s = 0; s < num_specs; s++) {
        const OverlaySpec *o = &specs[s];
        int img_y0 = o->dst_y;
        int img_y1 = o->dst_y + o->img_h;
        // Does this image overlap the current band?
        if (img_y1 <= band_y0) continue; // image above band
        if (img_y0 >= band_y1) continue; // image below band
        // For each TFT scanline of the band that overlaps the image,
        // RLE-decode and write the overlay image pixels (the blackout was
        // already done at the start of this function for the whole attract
        // area, so we don't need per-image blackouts anymore).
        for (int ly = 0; ly < 8; ly++) {
            int tft_y = band_y0 + ly;
            if (tft_y < img_y0 || tft_y >= img_y1) continue;
            int img_row = tft_y - img_y0;
            unsigned short *dst = frame_buffer + ly * SRC_W;
            rle_decode_to(o->data, img_row, o->img_w, dst, o->dst_x, SRC_W);
        }
    }
}

void popeye::render_row(short row) {
    if (row < 0 || row >= 36) return;
    // CONSOLE_QUADRA_NANO 240x240: il framework landscape mostra solo le tile
    // row 1..30 (src_y 8..247). Le row 0 e 31..35 vengono saltate dal write
    // del display. Skippare il rendering qui risparmia ~17% di tempo per frame
    // (6 row su 36) - budget che riallochiamo agli step Z80 aumentati.
    if (row == 0 || row >= 31) return;
    static unsigned short line[ORIG_W];

    for (int ly = 0; ly < 8; ly++) {
      int src_y = row * 8 + ly;
      // CONSOLE_QUADRA_NANO 240x240: il framework landscape (video.cpp) mostra
      // src_y in [8..247] saltando i primi 8 px. Per evitare di perdere l'HUD
      // top del raster originale, comprimiamo l'intero raster visibile 448 px
      // in 240 px display, compensando l'offset iniziale.
      // Mapping: src_y=8 -> oy=ORIG_VISIBLE_Y (top raster), src_y=247 -> oy~480 (bottom).
      int visible_y = src_y - 8;
      if (visible_y < 0)   visible_y = 0;
      if (visible_y > 239) visible_y = 239;
      int oy = ORIG_VISIBLE_Y + ((visible_y * ORIG_VISIBLE_H) / 240);
      if (oy < 0) oy = 0; if (oy >= 512) oy = 511;
      render_original_line(line, oy);

      unsigned short *dst = frame_buffer + ly * SRC_W;
      for (int sx = 0; sx < SRC_W; sx++) {
        // V3H: no final full-frame mirror. The playfield/sprites/control
        // orientation must stay as in the working V3F/V3E path. Only the text
        // layer is mirrored in draw_chars_line().
        int ox = (sx * ORIG_W) / SRC_W;
        dst[sx] = line[ox];
      }
    }

    // V9A — Overlay the four attract-screen images on top of the rendered
    // band, but only if we detected the attract "star + face" screen. The
    // detection runs once per band; it's a cheap VRAM check (4 byte reads).
    // OVERLAY V9A DISABILITATO (2026-05-30): l'utente preferisce la grafica
    // ROM originale (testa Popeye con pipa+fumo). I 4 overlay (face, logo,
    // nintendo, king_features) restano nel codice ma non vengono applicati.
    // if (is_attract_with_star()) {
    //   overlay_attract_row(row);
    // }
}
