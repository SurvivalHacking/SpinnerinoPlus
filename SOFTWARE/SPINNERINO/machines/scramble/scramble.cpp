#include "machines/scramble/scramble.h"
#include "emulation/input.h"

static inline bool ppi_port_is_input(unsigned char ctrl, unsigned char port) {
    switch(port & 3) {
        case 0: return (ctrl & 0x10) != 0;
        case 1: return (ctrl & 0x02) != 0;
        case 2: return true;
    }
    return true;
}

unsigned char scramble::ppi_port_read(unsigned char ctrl, unsigned char latch, unsigned char input_value, unsigned char port) {
    if((port & 3) != 2)
        return ppi_port_is_input(ctrl, port) ? input_value : latch;

    unsigned char v = 0;
    v |= (ctrl & 0x08) ? (input_value & 0xF0) : (latch & 0xF0);
    v |= (ctrl & 0x01) ? (input_value & 0x0F) : (latch & 0x0F);
    return v;
}

void scramble::ppi_port_write(unsigned char &ctrl, unsigned char *latch, unsigned char port, unsigned char value) {
    if((port & 3) == 3) {
        if(value & 0x80) {
            ctrl = value;
            latch[0] = 0x00;
            latch[1] = 0x00;
            latch[2] = 0x00;
        } else {
            unsigned char bit = (value >> 1) & 0x07;
            unsigned char mask = 1 << bit;
            if(value & 0x01) latch[2] |= mask;
            else latch[2] &= ~mask;
        }
        return;
    }

    switch(port & 3) {
        case 0: if(!(ctrl & 0x10)) latch[0] = value; break;
        case 1: if(!(ctrl & 0x02)) latch[1] = value; break;
        case 2:
            if(!(ctrl & 0x08)) latch[2] = (latch[2] & 0x0F) | (value & 0xF0);
            if(!(ctrl & 0x01)) latch[2] = (latch[2] & 0xF0) | (value & 0x0F);
            break;
    }
}

void scramble::protection_write(unsigned char value) {
    protection_state = ((protection_state << 4) | (value & 0x0F)) & 0x0FFF;
    unsigned char num1 = (protection_state >> 8) & 0x0F;
    unsigned char num2 = (protection_state >> 4) & 0x0F;
    unsigned char op   = protection_state & 0x0F;

    switch(op) {
        case 0x06:
            protection_result ^= 0x80;
            break;
        case 0x09:
            protection_result = ((num1 < 0x0F) ? (num1 + 1) : 0x0F) << 4;
            break;
        case 0x0B:
            protection_result = ((num2 > num1) ? (num2 - num1) : 0x00) << 4;
            break;
        case 0x0A:
            protection_result = ((num1 > num2) ? (num1 - num2) : 0x00) << 4;
            break;
        default:
            break;
    }
}

unsigned char scramble::opZ80(unsigned short Addr) {
    if(current_cpu == 0) {
        if(Addr < 0x4000) return scramble_rom_main[Addr];
    } else {
        // CPU audio
        if(Addr < 0x3000) return scramble_rom_audio[Addr];
        // Legge anche dalla RAM audio durante il fetch
        if(Addr >= 0x8000 && Addr <= 0x83FF) 
            return snd_ram[Addr & 0x03FF];
    }
    return 0xFF;
}

unsigned char scramble::read_ppi0(unsigned char port) {
    // Keep gameplay buttons on the original stable input path.
    // Only the coin is latched locally, otherwise one long pulse becomes +4 credits.
    unsigned char keymask = input->buttons_get() & ~BUTTON_COIN;
    switch(port & 3) {
        case 0: {
            unsigned char v = 0xFF;
            if(keymask & BUTTON_FIRE)  v &= ~0x08;  // shot
            if(keymask & BUTTON_EXTRA) v &= ~0x02;  // bomb
            if(keymask & BUTTON_RIGHT) v &= ~0x10;
            if(keymask & BUTTON_LEFT)  v &= ~0x20;
            if(coin_read_pulse) {
                v &= ~0x80;
                coin_read_pulse--;
            }
            return v;
        }
        case 1: {
            unsigned char v = SCRAMBLE_DIP_IN1;
            if(keymask & BUTTON_START) v &= ~0x80;
            return v;
        }
        case 2: {
            unsigned char v = SCRAMBLE_DIP_IN2;
            if(keymask & BUTTON_UP)   v &= ~0x10;
            if(keymask & BUTTON_DOWN) v &= ~0x40;
            return v;
        }
        default:
            return 0xFF;
    }
}

unsigned char scramble::read_ppi1(unsigned char port) {
    unsigned char input_ab = 0xFF;
    unsigned char input_c = protection_result;
    switch(port & 3) {
        case 0: return ppi_port_read(ppi1_ctrl, ppi1_latch[0], input_ab, 0);
        case 1: return ppi_port_read(ppi1_ctrl, ppi1_latch[1], input_ab, 1);
        case 2: return ppi_port_read(ppi1_ctrl, ppi1_latch[2], input_c, 2);
        default: return 0xFF;
    }
}

void scramble::write_ppi0(unsigned char port, unsigned char value) {
    ppi_port_write(ppi0_ctrl, ppi0_latch, port, value);
}

void scramble::write_ppi1(unsigned char port, unsigned char value) {
    unsigned char oldb = ppi1_latch[1];
    unsigned char oldc = ppi1_latch[2];
    ppi_port_write(ppi1_ctrl, ppi1_latch, port, value);

    switch(port & 3) {
        case 0:
            soundlatch = ppi1_latch[0];
            break;
        case 1: {
            unsigned char trig = ppi1_latch[1] & 0x08;
            unsigned char oldtrig = oldb & 0x08;
            if(oldtrig && !trig) snd_irq_pending = 1;
            snd_irq_last = trig;
            break;
        }
        case 2:
            if((oldc & 0x0F) != (ppi1_latch[2] & 0x0F))
                protection_write(ppi1_latch[2] & 0x0F);
            break;
        case 3:
            // Bit set/reset (value bit7=0) changes port C one bit at a time
            // — this IS how Scramble writes protection data → trigger protection.
            // Control word (value bit7=1) resets latches to 0 → skip protection.
            if(!(value & 0x80)) {
                if((oldc & 0x0F) != (ppi1_latch[2] & 0x0F))
                    protection_write(ppi1_latch[2] & 0x0F);
            }
            break;
    }
}

unsigned char scramble::rdZ80(unsigned short Addr) {
    if(current_cpu == 1) {
        if(Addr < 0x3000)
            return scramble_rom_audio[Addr];
        if(Addr >= 0x8000 && Addr <= 0x83FF)  // Solo 1KB, non fino a 0x8FFF
            return snd_ram[Addr & 0x03FF];
        return 0xFF;
    }

    Addr &= 0xFFFF;
    if(Addr < 0x4000)
        return scramble_rom_main[Addr];

    if(Addr >= 0x4000 && Addr <= 0x47FF)
        return memory[Addr - 0x4000];

    if(Addr >= 0x4800 && Addr <= 0x4FFF)
        return memory[0x0800 + ((Addr - 0x4800) & 0x03FF)];

    if(Addr >= 0x5000 && Addr <= 0x503F)
        return memory[0x0C00 + (Addr - 0x5000)];

    if(Addr >= 0x5040 && Addr <= 0x505F)
        return memory[0x0C40 + (Addr - 0x5040)];

    if(Addr >= 0x5060 && Addr <= 0x507F)
        return memory[0x0C60 + (Addr - 0x5060)];

    if(Addr >= 0x5080 && Addr <= 0x50FF)
        return memory[0x0C80 + (Addr - 0x5080)];

    if(Addr == 0x7000 || Addr == 0x7800)
        return 0x00; // watchdog reset read

    if((Addr >= 0x8100 && Addr <= 0x8103) || (Addr >= 0x8110 && Addr <= 0x8113))
        return read_ppi0(Addr & 0x03);

    if(Addr >= 0x8200 && Addr <= 0x8203)
        return read_ppi1(Addr & 0x03);

    return 0xFF;
}

void scramble::wrZ80(unsigned short Addr, unsigned char Value) {
    if(current_cpu == 1) {
        if(Addr >= 0x8000 && Addr <= 0x8FFF) {
            snd_ram[Addr & 0x03FF] = Value;
            return;
        }
        return;
    }

    if(Addr >= 0x4000 && Addr <= 0x47FF) {
        memory[Addr - 0x4000] = Value;
        return;
    }

    if(Addr >= 0x4800 && Addr <= 0x4FFF) {
        if(!game_started && Addr == 0x4800 + 800)
            game_started = 1;
        memory[0x0800 + ((Addr - 0x4800) & 0x03FF)] = Value;
        return;
    }

    if(Addr >= 0x5000 && Addr <= 0x503F) {
        memory[0x0C00 + (Addr - 0x5000)] = Value;
        return;
    }

    if(Addr >= 0x5040 && Addr <= 0x505F) {
        memory[0x0C40 + (Addr - 0x5040)] = Value;
        return;
    }

    if(Addr >= 0x5060 && Addr <= 0x507F) {
        memory[0x0C60 + (Addr - 0x5060)] = Value;
        return;
    }

    if(Addr >= 0x5080 && Addr <= 0x50FF) {
        memory[0x0C80 + (Addr - 0x5080)] = Value;
        return;
    }

    switch(Addr) {
        case 0x6801: nmi_enable = Value & 1; return;
        case 0x6802: return; // coin counter
        case 0x6803: background_enable = Value & 1; return; // sfondo blu (livelli > 1)
        case 0x6804: stars_enabled = Value & 1; return;
        case 0x6806: flip_x = Value & 1; return;
        case 0x6807: flip_y = Value & 1; return;
        default: break;
    }

    if((Addr >= 0x8100 && Addr <= 0x8103) || (Addr >= 0x8110 && Addr <= 0x8113)) {
        write_ppi0(Addr & 0x03, Value);
        return;
    }

    if(Addr >= 0x8200 && Addr <= 0x8203) {
        write_ppi1(Addr & 0x03, Value);
        return;
    }
}

void scramble::outZ80(unsigned short Port, unsigned char Value) {
    if(current_cpu != 1) return;

    switch(Port & 0xFF) {
        case 0x10:
            ay_addr[0] = Value & 0x0F;
            return;
        case 0x20: {
            unsigned char reg = ay_addr[0] & 0x0F;
            ay_regs[0][reg] = Value;
            if(reg < 14) soundregs[reg] = Value;
            return;
        }
        case 0x40:
            ay_addr[1] = Value & 0x0F;
            return;
        case 0x80: {
            unsigned char reg = ay_addr[1] & 0x0F;
            ay_regs[1][reg] = Value;
            if(reg < 14) soundregs[16 + reg] = Value;
            return;
        }
    }
}

unsigned char scramble::scramble_portB_r() {
    static const unsigned char scramble_timer[10] = {
        0x00, 0x10, 0x20, 0x30, 0x40, 0x90, 0xA0, 0xB0, 0xA0, 0xD0
    };
    // MAME: total_cycles / 512. snd_icnt counts instructions (~4.5 cycles each).
    // Tuned to match original Scramble music tempo.
    return scramble_timer[(snd_icnt / 70) % 10];
}

unsigned char scramble::inZ80(unsigned short Port) {
    if(current_cpu != 1) return 0xFF;

    switch(Port & 0xFF) {
        case 0x20: {
            unsigned char reg = ay_addr[0] & 0x0F;
            return ay_regs[0][reg];
        }
        case 0x80: {
            unsigned char reg = ay_addr[1] & 0x0F;
            if(reg == 14) return soundlatch;
            if(reg == 15) return scramble_portB_r();
            return ay_regs[1][reg];
        }
    }
    return 0xFF;
}

void scramble::reset() {
    machineBase::reset();
    // Alloca lazy il buffer nativo trasposto (come pacman/dkong).
    if (!scr_full)
        scr_full = (unsigned short*)ps_malloc(SCR_NW * SCR_NH * sizeof(unsigned short));
    memset(snd_ram, 0, sizeof(snd_ram));
    soundlatch = 0;
    snd_irq_pending = 0;
    snd_irq_last = 0;
    snd_icnt = 0;
    memset(ay_addr, 0, sizeof(ay_addr));
    memset(ay_regs, 0, sizeof(ay_regs));
    ppi0_ctrl = 0x9B;
    ppi1_ctrl = 0x88;
    ppi0_latch[0] = ppi0_latch[1] = ppi0_latch[2] = 0xFF;
    ppi1_latch[0] = 0x00;
    ppi1_latch[1] = 0x00;
    ppi1_latch[2] = 0x0F;
    protection_state = 0;
    protection_result = 0;
    nmi_enable = 0;
    stars_enabled = 0;
    background_enable = 0;
    flip_x = 0;
    flip_y = 0;
    bullet_active = 0;
    star_count = 0;
    star_scroll_offset = 0;
    stars_initialized = false;
    coin_pin_prev = false;
    coin_read_pulse = 0;
    frame_counter = 0;
    memset(star_visible, 0, sizeof(star_visible));
}

void scramble::run_frame(void) {
//    if(!game_started) game_started = 1;

    // Freeze controls once per frame so Scramble cannot re-sample the same
    // press several times while the CPUs are stepping.
    // COIN: pin fisico OPPURE gamepad BLE (BUTTON_COIN). Senza l'OR, con
    // BTN_COIN_PIN definito si leggeva solo il pin → coin BT non funzionava.
#ifdef BTN_COIN_PIN
    bool coin_now = !digitalRead(BTN_COIN_PIN) || ((input->buttons_get() & BUTTON_COIN) != 0);
#else
    bool coin_now = (input->buttons_get() & BUTTON_COIN) != 0;
#endif
    if(coin_now && !coin_pin_prev)
        coin_read_pulse = 2;
    coin_pin_prev = coin_now;

    // Scramble clocks are a bit higher than the generic Galaxian-family loop.
    // 5 main CPU steps + 3 sound CPU steps per slice give a much closer pace
    // on ESP32 than the old 4/2 split.
    for(int i = 0; i < INST_PER_FRAME; i++) {
        current_cpu = 0;
        StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);

        current_cpu = 1;
        StepZ80(&cpu[1]); snd_icnt++;
        StepZ80(&cpu[1]); snd_icnt++;
        StepZ80(&cpu[1]); snd_icnt++;

        if(snd_irq_pending && (cpu[1].IFF & IFF_1)) {
            IntZ80(&cpu[1], INT_RST38);
            snd_irq_pending = 0;
        }
    }

    if(nmi_enable) {
        current_cpu = 0;
        IntZ80(&cpu[0], INT_NMI);
    }
}

void scramble::stars_init() {
    static const unsigned char starmap[4] = { 0, 150, 200, 255 };

    unsigned short star_color_lut[64];
    for(int i = 0; i < 64; i++) {
        unsigned char r = starmap[((i >> 4) & 2) | ((i >> 5) & 1)];
        unsigned char g = starmap[((i >> 2) & 2) | ((i >> 3) & 1)];
        unsigned char b = starmap[((i >> 0) & 2) | ((i >> 1) & 1)];
        star_color_lut[i] = rgb_to_swapped565(r, g, b);
    }

    star_count = 0;
    uint32_t shiftreg = 0;
    for(int i = 0; i < 131071 && star_count < SCRAMBLE_MAX_STARS; i++) {
        if((shiftreg & 0x1fe01) == 0x1fe00) {
            int color_idx = (~shiftreg >> 3) & 0x3f;
            int x = (i % 512) / 2;
            int y = i / 512;

            if((y ^ (x >> 3)) & 1) {
                int gx = 255 - y;
                int gy = x + 16;

                if(gx >= 0 && gx < 224 && gy >= 16 && gy < 288) {
                    stars[star_count].x = gx;
                    stars[star_count].y = gy;
                    stars[star_count].color = star_color_lut[color_idx];
                    star_count++;
                }
            }
        }
        shiftreg = (shiftreg >> 1) | ((((shiftreg >> 12) ^ ~shiftreg) & 1) << 16);
    }
    stars_initialized = true;
}

void scramble::prepare_frame(void) {
    if(!stars_initialized) stars_init();

    // Calcola il lampeggio delle stelle UNA VOLTA per frame
    frame_counter++;
    
    for(int i = 0; i < star_count; i++) {
        // Periodi molto variabili: da 1 secondo a 5 secondi (60-300 frame)
        int period = 60 + ((i * 131) % 240);
        // La stella è visibile per una frazione variabile del periodo
        int duty = 10 + ((i * 97) % 40);  // tra 10 e 50 frame accesa
        
        star_visible[i] = ((frame_counter + i) % period) < duty;
    }


    // ==== CORRETTO 2026-08-28: era il difetto delle STRISCE COLORATE ========
    // Qui c'era, senza graffe:
    //     if(stars_enabled)
    //         // star_scroll_offset = (star_scroll_offset + 1) % 288;
    //
    //     active_sprites = 0;
    // Il corpo dell'if era COMMENTATO, quindi a diventarne il corpo era
    // l'istruzione DOPO: la lista degli sprite si azzerava SOLO con le stelle
    // accese. Super Cobra le spegne appena comincia la partita -> la lista
    // cresceva di 8 voci per quadro fino al tetto di 92 e li' si CONGELAVA
    // (col contatore a 92 il for qui sotto non gira piu'): 92 sprite VECCHI
    // ridisegnati per sempre sopra il gioco. Sono le strisce colorate che
    // comparivano dopo qualche secondo di partita, su Super Cobra e Scramble.
    //
    // Non era lo schema di famiglia: galaxian.cpp:148 ha le graffe e
    // mooncresta.cpp:142 ha un'istruzione vera. E' un refuso nato commentando
    // quella riga. Provato rimettendolo dentro l'harness del porting
    // CABINATONE (mk_scobratest.py bugspin): finche' le stelle sono accese le
    // due versioni danno 0 pixel diversi su 57.344, dal quadro in cui si
    // spengono divergono (380, 662, 586 pixel) e non tornano piu'.
    //
    // ⚠️ Lo scroll delle stelle resta SPENTO, com'era: se un giorno lo si
    //    riaccende, si riaccende INSIEME al suo if — e con le graffe.
    // if(stars_enabled)
    //     star_scroll_offset = (star_scroll_offset + 1) % 288;
    // =======================================================================

    active_sprites = 0;
    for(int idx = 7; idx >= 0 && active_sprites < 92; idx--) {
        struct sprite_S spr;
        unsigned char *base = memory + 0x0C40 + idx * 4;

        spr.code  = base[1] & 0x3F;
        spr.flags = (base[1] >> 6) & 3;
        spr.color = base[2] & 7;
        spr.x = base[0] - 16;
        spr.y = base[3] + 16;

        if(base[3] && (spr.y > -16) && (spr.y < 288) && (spr.x > -16) && (spr.x < 224))
            sprite[active_sprites++] = spr;
    }

    bullet_active = 0;
    for(int idx = 0; idx < 8; idx++) {
        unsigned char *bbase = memory + 0x0C60 + idx * 4;
        bullet_x[idx] = bbase[1] - 16;
        bullet_y[idx] = 266 - bbase[3];
        if(bbase[1] && bbase[3] && bullet_x[idx] >= 0 && bullet_x[idx] < 224 &&
           bullet_y[idx] > -4 && bullet_y[idx] < 288)
            bullet_active |= (1 << idx);
    }

    // --- Render NATIVO 224x288 in scr_full (blit via rt = base strip) ---
    // render_row() trasporra' poi scr_full nel framebuffer (stride SCR_NH).
    if (scr_full) {
        memset(scr_full, 0, SCR_NW * SCR_NH * sizeof(unsigned short));
        for (short srow = 2; srow < 34; srow++) {
            rt = scr_full + srow * 8 * SCR_NW;
            render_strip(srow);
        }
    }
}

void scramble::blit_tile(short row, char col) {
    unsigned short addr = tileaddr[row][col];
    if(row < 2 || row >= 34) return;

    const unsigned short *tile = scramble_tilemap[memory[0x0800 + addr]];
    int c = memory[0x0C00 + 2 * (addr & 31) + 1] & 7;
    const unsigned short *colors = scramble_colormap[c];

    unsigned short *ptr = rt + 8 * col;
    for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
        unsigned short pix = tile[r];
        for(char x = 0; x < 8; x++, pix >>= 2) {
            if(pix & 3) *ptr = colors[pix & 3];
            ptr++;
        }
    }
}

void scramble::blit_tile_scroll(short row, signed char col, unsigned char scroll) {
    if(row < 2 || row >= 34) return;

    unsigned short addr;
    unsigned short mask = 0xFFFF;
    int sub = scroll & 0x07;

    if(col >= 0) {
        addr = tileaddr[row][col];
        addr = (addr + ((scroll & ~7) << 2)) & 1023;
        if(sub && col == 27)
            mask = 0xFFFF >> (2 * sub);
    } else {
        addr = tileaddr[row][0];
        addr = (addr + 32 + ((scroll & ~7) << 2)) & 1023;
        mask = 0xFFFF << (2 * (8 - sub));
    }

    const unsigned short *tile = scramble_tilemap[memory[0x0800 + addr]];
    int c = memory[0x0C00 + 2 * (addr & 31) + 1] & 7;
    const unsigned short *colors = scramble_colormap[c];
    unsigned short *ptr = rt + 8 * col + sub;

    for(char r = 0; r < 8; r++, ptr += (224 - 8)) {
        unsigned short pix = tile[r] & mask;
        for(char x = 0; x < 8; x++, pix >>= 2) {
            if(pix & 3) *ptr = colors[pix & 3];
            ptr++;
        }
    }
}

void scramble::blit_sprite(short row, unsigned char s) {
    const unsigned long *spr = scramble_spritemap[sprite[s].flags & 3][sprite[s].code];
    const unsigned short *colors = scramble_colormap[sprite[s].color];

    unsigned long mask = 0xFFFFFFFF;
    if(sprite[s].x < 0) mask <<= -2 * sprite[s].x;
    if(sprite[s].x > 224 - 16) mask >>= 2 * (sprite[s].x - (224 - 16));

    short y_offset = sprite[s].y - 8 * row;
    unsigned char lines2draw = 8;
    if(y_offset < -8) lines2draw = 16 + y_offset;

    unsigned short startline = 0;
    if(y_offset > 0) {
        startline = y_offset;
        lines2draw = 8 - y_offset;
    }

    if(y_offset < 0) spr -= y_offset;

    unsigned short *ptr = rt + sprite[s].x + 224 * startline;
    for(char r = 0; r < lines2draw; r++, ptr += (224 - 16)) {
        unsigned long pix = *spr++ & mask;
        for(char c = 0; c < 16; c++, pix >>= 2) {
            if(pix & 3) *ptr = colors[pix & 3];
            ptr++;
        }
    }
}

// render_strip: corpo dell'ex render_row (stars + tile + sprite + bullet) verso
// rt (strip nativa di scr_full) invece di frame_buffer.
void scramble::render_strip(short row) {
    if(row <= 1 || row >= 34) return;

    // Sfondo blu (porta 0x6803): nelle sezioni dopo il 1o livello l'hardware
    // accende un backdrop blu su cui i tile disegnano solo i pixel non-zero.
    // Fedele a galagino-main: memset(... ,8, ...) -> ogni short = 0x0808 (blu).
    // Qui riempie la strip corrente di scr_full puntata da rt (224*8 short).
    if(background_enable)
        memset(rt, 8, SCR_NW * 8 * sizeof(unsigned short));

    if(stars_enabled && stars_initialized) {
        int row_top = 8 * row;
        int row_bot = row_top + 8;
        
        for(int i = 0; i < star_count; i++) {
            // Usa il valore precalcolato in prepare_frame()
            if(!star_visible[i]) continue;
            
            int sy = stars[i].y;
            if(sy < 16) sy += 272;
            if(sy >= row_top && sy < row_bot) {
                int sx = stars[i].x;
                if(sx >= 0 && sx < 224) {
                    int fb_idx = (sy - row_top) * 224 + sx;
                    // Disegna su nero, oppure sopra il backdrop blu se attivo
                    // (come galagino-main, dove le stelle vanno sopra lo sfondo).
                    if(rt[fb_idx] == 0x0000 || background_enable)
                        rt[fb_idx] = stars[i].color;
                }
            }
        }
    }
    unsigned char scroll = memory[0x0C00 + 2 * (row - 2)];
    if(scroll == 0) {
        for(char col = 0; col < 28; col++)
            blit_tile(row, col);
    } else {
        if(scroll & 7)
            blit_tile_scroll(row, -1, scroll);
        for(char col = 0; col < 28; col++)
            blit_tile_scroll(row, col, scroll);
    }

    for(unsigned char s = 0; s < active_sprites; s++) {
        if((sprite[s].y < 8 * (row + 1)) && ((sprite[s].y + 16) > 8 * row))
            blit_sprite(row, s);
    }

    if(bullet_active) {
        short row_top = 8 * row;
        short row_bot = row_top + 8;
        for(int b = 0; b < 8; b++) {
            if(!(bullet_active & (1 << b))) continue;
            short bx = bullet_x[b];
            short by = bullet_y[b];
            if(bx < 0 || bx >= 224) continue;
            if(by + 2 <= row_top || by >= row_bot) continue;

            // Sparo: piccolo quadrato 2x2
            unsigned short color = (b == 7) ? 0xE0FF : 0xFFFF;
            for(int py = 0; py < 2; py++) {
                short sy = by + py;
                if(sy < row_top || sy >= row_bot) continue;
                int line = (sy - row_top) * 224;
                rt[line + bx] = color;
                if(bx + 1 < 224) rt[line + bx + 1] = color;
            }
        }
    }
}

// Render trasposto 90: scr_full (224x288, riempito in prepare_frame) ->
// frame_buffer (stride SCR_NH), come pacman/dkong. Ruota il framebuffer
// galagino-portrait sul pannello SPINNERINO -> Scramble appare orizzontale.
void scramble::render_row(short strip_r) {
    if (!scr_full) return;
    for (int fy = 0; fy < 8; fy++) {
        int fb_y  = strip_r * 8 + fy;              // 0..223 (asse corto display)
        int col   = (SCR_NW - 1) - fb_y;           // colonna nativa 223..0
        if (col < 0 || col >= SCR_NW) continue;
        unsigned short *dst       = frame_buffer + fy * SCR_NH;
        const unsigned short *src = scr_full + col;
        for (int y = 0; y < SCR_NH; y++)
            dst[y] = src[y * SCR_NW];
    }
}

const unsigned short *scramble::logo(void) {
    return Scramble_logo;
}

#ifdef LED_PIN
void scramble::gameLeds(CRGB *leds) {
    static char sub_cnt = 0;
    if(sub_cnt++ == 32) {
        sub_cnt = 0;
        static char led = 0;
        char il = (led < NUM_LEDS) ? led : ((2 * NUM_LEDS - 2) - led);
        for(char c = 0; c < NUM_LEDS; c++)
            leds[c] = (c == il) ? LED_YELLOW : LED_RED;
        led = (led + 1) % (2 * NUM_LEDS - 2);
    }
}

void scramble::menuLeds(CRGB *leds) {
    memcpy(leds, menu_leds, NUM_LEDS * sizeof(CRGB));
}
#endif
