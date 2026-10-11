// fantasy.cpp - Fantasy (Rock-Ola/SNK 1981, set "fantasyu") for GALAGINO
// Stage 2a skeleton. Memory map = "fantasy_map" (snk6502.cpp), shared with
// Nibbler. Differences vs Nibbler implementation:
//   - $2400 VOCE HD38880: protocollo speech_w di MAME + 12 campioni (fantasy_voce.h)
//   - Sound ROM is 8KB linear (fs10 + fs11) instead of Nibbler's 6KB
//   - Logo returns nullptr until fantasy_logo.h is added in a later stage

#include "fantasy.h"
#include "fantasy_rom.h"
#include "fantasy_gfx.h"
#include "fantasy_palette.h"
#include "fantasy_dipswitches.h"
#include "fantasy_logo.h"
#include "../../emulation/input.h"

extern void reset6502();
extern void exec6502(uint32_t tickcount);
extern void irq6502();
extern void irq6502_safe();
extern void nmi6502();
extern uint32_t clockticks6502;
extern uint32_t clockgoal6502;
// SPINNERINO: il glue read6502/write6502/opread6502 (emulation.cpp) chiama
// currentMachine->rd6502/wr6502/op6502 -> niente m6502_current.

void fantasy::reset() {
    // Alloca lazy il buffer nativo trasposto (come scramble/dkong).
    if (!fan_full)
        fan_full = (unsigned short*)ps_malloc(FAN_NW * FAN_NH * sizeof(unsigned short));

    memset(memory, 0, FAN_MEM_TOTAL);
    memset(soundregs, 0, sizeof(soundregs));
    game_started = 0;

    clockticks6502 = 0;
    clockgoal6502 = 0;
    reset6502();

    coin_prev = false;
    scroll_x = 0;
    scroll_y = 0;
    ctrl_reg = 0;

    for (int i = 0; i < FAN_NUM_VOICES; i++) {
        voices[i].base = 0;
        voices[i].offset = 0;
        voices[i].mask = 0xFF;
        voices[i].sample_cur = 0;
        voices[i].sample_step = 0;
        voices[i].mute = true;
        for (int j = 0; j < 16; j++) voices[i].form[j] = 0;
    }
    build_waveform(0, 0xF);
    build_waveform(1, 0xF);
    build_waveform(2, 0xF);
    bomb_active = 0;
    bomb_prev = false;
    sound0_stop_on_rollover = false;

    hd_cmd = 0; hd_byte = 0; hd_addr = 0;
    voce_cmd = 99;                     // ferma una frase rimasta dalla partita prima
}

// La VOCE: `snk6502_sound_device::speech_w()` + `fantasy_table[]` di MAME,
// trascritti dal CABINATONE (fantasy.cpp, confermato sul cabinato). MAME non
// sintetizza l'HD38880: suona 12 campioni scelti dall'indirizzo che il gioco
// manda. Bit 4 CTP e bit 5 CMV alti = un comando; i 4 bit bassi il dato.
#define HD_ADSET 2
#define HD_INT1  4
#define HD_INT2  6
#define HD_SYSPD 8
#define HD_STOP  10
#define HD_START 12
static const unsigned int FAN_TAB_VOCE[16] = {
    0x04000, 0x04297, 0x044b6, 0x04682, 0x04927, 0x04be0, 0x04cc2, 0x04e36,
    0x05000, 0x05163, 0x052c9, 0x053fd, 0, 0, 0, 0
};

void fantasy::voce_w(unsigned char data) {
    if (!((data & 0x10) && (data & 0x20))) return;
    data &= 0x0F;
    switch (hd_cmd) {
    case 0:
        switch (data) {
        case HD_START:
            if (hd_byte == 5 && !voce_attiva && voce_cmd < 0) {
                for (int i = 0; i < 16; i++) {
                    if (FAN_TAB_VOCE[i] && FAN_TAB_VOCE[i] == hd_addr) {
                        voce_cmd = i;
                        break;
                    }
                }
            }
            break;
        case HD_STOP:  voce_cmd = 99; break;
        case HD_SYSPD: case HD_INT1: case HD_INT2: hd_cmd = data; break;
        case HD_ADSET: hd_cmd = data; hd_addr = 0; hd_byte = 0; break;
        default: break;
        }
        break;
    case HD_INT1: case HD_INT2: case HD_SYSPD:
        hd_cmd = 0;
        break;
    case HD_ADSET:
        hd_addr |= (unsigned int)data << (hd_byte++ * 4);
        if (hd_byte == 5) hd_cmd = 0;
        break;
    }
}

void fantasy::build_waveform(int channel, int mask) {
    int bit0 = 0, bit1 = 0, bit2 = 0, bit3 = 0;
    if ((mask & 1) || (mask & 2))      bit3 = 8;
    else if (mask & 4)                  bit3 = 4;
    else if (mask & 8)                  bit3 = 2;
    if (mask & 4)                       bit2 = 8;
    else if ((mask & 2) || (mask & 8))  bit2 = 4;
    if (mask & 8)                       bit1 = 8;
    else if (mask & 4)                  bit1 = 4;
    else if (mask & 2)                  bit1 = 2;
    bit0 = bit1 >> 1;
    if (bit0 + bit1 + bit2 + bit3 < 16) {
        bit0 <<= 1; bit1 <<= 1; bit2 <<= 1; bit3 <<= 1;
    }
    const int base = (bit0 + bit1 + bit2 + bit3 + 1) / 2;
    for (int i = 0; i < 16; i++) {
        int data = ((i & 1) ? bit0 : 0) + ((i & 2) ? bit1 : 0)
                 + ((i & 4) ? bit2 : 0) + ((i & 8) ? bit3 : 0);
        voices[channel].form[i] = (short)((data - base) * 16);
    }
}

unsigned char fantasy::rd6502(unsigned short Addr) {
    if (Addr < 0x2000) {
        return memory[Addr];
    }

    // I/O area ($2104-$2107 readable). Active HIGH like Nibbler.
    if (Addr >= 0x2104 && Addr <= 0x2107) {
        unsigned short btns = input->buttons_get();
        switch (Addr) {
            case 0x2104: {
                // MAME snk6502_generic_joy8way IN0: solo joystick (no fire!).
                // Fantasy non ha pulsante fire - controlli solo joystick 8-way.
                unsigned char v = 0x00;
                if (btns & BUTTON_DOWN)  v |= 0x10;
                if (btns & BUTTON_UP)    v |= 0x20;
                if (btns & BUTTON_RIGHT) v |= 0x40;
                if (btns & BUTTON_LEFT)  v |= 0x80;
                return v;
            }
            case 0x2105:
                // P2 cocktail input - unused
                return 0x00;
            case 0x2106:
                return FANTASY_DSW;
            case 0x2107: {
                unsigned char v = 0x00;
                if (coin_impulse > 0) v |= 0x02;
                if (btns & BUTTON_START) v |= 0x80;
                return v;
            }
        }
    }

    // ROM: $3000-$BFFF
    if (Addr >= 0x3000 && Addr < 0xC000) {
        return pgm_read_byte_near(fantasy_rom + (Addr - 0x3000));
    }

    // ic14 mirror at $F000-$FFFF (offset 0x5000 in linear ROM buffer)
    if (Addr >= 0xF000) {
        return pgm_read_byte_near(fantasy_rom + 0x5000 + (Addr - 0xF000));
    }

    return 0xFF;
}

void fantasy::wr6502(unsigned short Addr, unsigned char Value) {
    if (Addr < 0x2000) {
        memory[Addr] = Value;
        if (Addr >= 0x0400 && Addr <= 0x0BFF) game_started = 1;
        return;
    }

    if (Addr <= 0x2001) {
        // MC6845 CRTC - ignore
        return;
    }
    if (Addr == 0x2100) {
        // fantasy_sound0_w
        voices[0].base = (Value & 0x07) << 8;
        voices[0].mask = 0xFF;
        sound0_stop_on_rollover = false;
        if (Value & 0x08) {
            voices[0].mute = false;
        } else {
            voices[0].offset = voices[0].base;
            voices[0].mute = true;
        }
        if (Value & 0x10) {
            voices[2].mute = false;
        } else {
            voices[2].offset = 0;
            voices[2].mute = true;
        }
        bool bomb_now = (Value & 0x80) != 0;
        if (bomb_now && !bomb_prev) bomb_active = 6000;
        bomb_prev = bomb_now;
        return;
    }
    if (Addr == 0x2101) {
        // fantasy_sound1_w
        unsigned int new_base = 0x0800 + ((Value & 0x07) << 8);
        voices[1].base = new_base;
        voices[1].mask = 0xFF;
        voices[1].mute = !(Value & 0x08);
        if (voices[1].mute) voices[1].offset = 0;
        return;
    }
    if (Addr == 0x2102) {
        int mask0 = (Value & 0x9) | ((Value & 2) << 1) | ((Value & 4) >> 1);
        build_waveform(0, mask0);
        build_waveform(1, (Value >> 4) & 0x0F);
        return;
    }
    if (Addr == 0x2103) {
        // fantasy_sound2_w
        ctrl_reg = Value;
        voices[2].base = 0x1000 + ((Value & 0x70) << 4);
        voices[2].mask = 0xFF;
        return;
    }
    if (Addr == 0x2200) { scroll_x = Value; return; }
    if (Addr == 0x2300) { scroll_y = Value; return; }
    if (Addr == 0x2400) { voce_w(Value); return; }   // VOCE HD38880
}

unsigned char fantasy::op6502(unsigned short Addr) {
    return rd6502(Addr);
}

void fantasy::run_frame() {
    clockticks6502 = 0;
    clockgoal6502 = 0;

    int visible_cycles = (FAN_CYCLES_PER_FRAME * 224) / 262;
    int vblank_cycles  = FAN_CYCLES_PER_FRAME - visible_cycles;

    exec6502(visible_cycles);
    irq6502_safe();
    exec6502(vblank_cycles);

    unsigned short btns = input->buttons_get();
    if ((btns & BUTTON_COIN) && !coin_prev) {
        nmi6502();
        coin_impulse = 2;
    }
    coin_prev = (btns & BUTTON_COIN) != 0;
    if (coin_impulse > 0) coin_impulse--;
}

void fantasy::prepare_frame() {
    active_sprites = 0;
    // --- Render NATIVO 224x256 in fan_full (blit via rt = base strip) ---
    if (fan_full) {
        memset(fan_full, 0, FAN_NW * FAN_NH * sizeof(unsigned short));
        for (short srow = 0; srow < FAN_ROWS; srow++) {
            rt = fan_full + srow * 8 * FAN_NW;
            render_strip(srow);
        }
    }
}

// render_strip: corpo dell'ex render_row (bg+fg tile) verso rt invece di
// frame_buffer. fan_full e' gia' azzerato in prepare_frame.
void fantasy::render_strip(short row) {
    if (row < 0 || row >= FAN_ROWS) return;
    for (char col = 0; col < FAN_COLS; col++) blit_bg_tile(row, col);
    for (char col = 0; col < FAN_COLS; col++) blit_fg_tile(row, col);
}

// Render trasposto 90: fan_full (224x256) -> frame_buffer (stride FAN_NH),
// come scramble/dkong.
void fantasy::render_row(short strip_r) {
    if (!fan_full) return;
    for (int fy = 0; fy < 8; fy++) {
        int fb_y = strip_r * 8 + fy;               // 0..223 (asse corto display)
        int col  = (FAN_NW - 1) - fb_y;            // colonna nativa 223..0
        if (col < 0 || col >= FAN_NW) continue;
        unsigned short *dst       = frame_buffer + fy * FAN_NH;
        const unsigned short *src = fan_full + col;
        for (int y = 0; y < FAN_NH; y++)
            dst[y] = src[y * FAN_NW];
    }
}

// MAME snk6502.cpp: gfx1 region 8KB, standard charlayout 512 chars 2bpp,
// plane 0 at offset 0x0000 (fs10), plane 1 at offset 0x1000 (fs11).
// char_bank: MAME flipscreen_w fa "bank = (~data & 0x08) >> 3" -> bit 3 NEGATO!
//   ctrl_reg bit 3 = 0 -> char_bank = 1 (chars 256-511)
//   ctrl_reg bit 3 = 1 -> char_bank = 0 (chars 0-255)
// Flipscreen: ctrl_reg bit 7 -> flippa entrambi gli assi del game world.
//
// SCROLL: MAME snk6502_v.cpp fa m_bg_tilemap->set_scrollx/y(0, data) sul
// BG (FG e' statico). Tilemap 32x32 = 256x256 virtuale, wrap & 0xFF.
// Lo scroll deve essere applicato in coordinate game pixel PRIMA di
// risalire al tile effettivo: il livello 3 di Fantasy scrolla la citta',
// e il livello 1 usa fine scroll per le nuvole (senza fix appariranno
// scattanti a step di 8 px).
void fantasy::blit_bg_tile(short row, char col) {
    bool flip = (ctrl_reg & 0x80) != 0;
    int base_game_x_tile, base_game_y_tile;
    if (flip) {
        base_game_x_tile = 31 - row;
        base_game_y_tile = col;
    } else {
        base_game_x_tile = row;
        base_game_y_tile = 27 - col;
    }

    // MAME snk6502_palette: pen 0 di ogni BG bank e' SOSTITUITO col backcolor
    // globale -> palette[0x20 + 4*(ctrl_reg & 7)].
    unsigned short bg_color0 = pgm_read_word_near(
        &fantasy_palette[32 + (ctrl_reg & 0x07) * 4]);
    int char_bank_or = (ctrl_reg & 0x08) ? 0 : 0x100;   // INVERTED char_bank

    // Cache: il lookup VRAM/CRAM e gfx fetch cambiano solo quando l'effective
    // tile (offs) o la riga del tile (pix_y) cambiano. Nel caso scroll=0 e' un
    // tile fisso per display tile, quindi performance ~= versione originale.
    int  last_offs  = -1;
    int  last_pix_y = -1;
    int  code       = 0;
    unsigned char bank = 0;
    unsigned char b0 = 0, b1 = 0;

    unsigned short *fb = rt + col * 8;
    for (int dy = 0; dy < 8; dy++, fb += (FAN_FB_STRIDE - 8)) {
        int gpx = flip ? (7 - dy) : dy;
        int gx_pix = (base_game_x_tile * 8 + gpx + scroll_x) & 0xFF;
        int eff_tile_x = gx_pix >> 3;
        int pix_x = gx_pix & 7;
        int shift = 7 - pix_x;
        for (int dx = 0; dx < 8; dx++, fb++) {
            int gpy = flip ? dx : (7 - dx);
            int gy_pix = (base_game_y_tile * 8 + gpy + scroll_y) & 0xFF;
            int eff_tile_y = gy_pix >> 3;
            int pix_y = gy_pix & 7;
            int offs = eff_tile_y * 32 + eff_tile_x;

            if (offs != last_offs) {
                code = memory[FAN_MEM_VRAM + offs] | char_bank_or;
                unsigned char attr = memory[FAN_MEM_CRAM + offs];
                bank = (attr >> 3) & 0x07;
                last_offs = offs;
                last_pix_y = -1;
            }
            if (pix_y != last_pix_y) {
                // Bitplanes SWAPPED come per Nibbler: bp0 (LSB pen) da offset
                // 0x1000 (fs11), bp1 (MSB) da offset 0 (fs10). Verifica empirica
                // utente: senza swap il mare diventa verde invece che blu.
                // Il converter palette / decode appare identico a MAME ma in
                // pratica swap e' l'unica configurazione che da' mare blu.
                // EFFETTO COLLATERALE: pen 1<->2 invertiti -> uccelli liv.3
                // rossi (devono essere verdi) e muri liv.2 invisibili.
                // Bug aperto: cercare la causa reale (PROM inversion?).
                b0 = pgm_read_byte_near(fantasy_gfx + 0x1000 + code * 8 + pix_y);
                b1 = pgm_read_byte_near(fantasy_gfx + code * 8 + pix_y);
                last_pix_y = pix_y;
            }
            unsigned char p = ((b0 >> shift) & 1) | (((b1 >> shift) & 1) << 1);
            *fb = (p == 0)
                ? bg_color0
                : pgm_read_word_near(&fantasy_palette[32 + bank * 4 + p]);
        }
    }
}

void fantasy::blit_fg_tile(short row, char col) {
    int game_x, game_y;
    if (ctrl_reg & 0x80) {
        // flipscreen
        game_x = 31 - row;
        game_y = col;
    } else {
        game_x = row;
        game_y = 27 - col;
    }
    int offs = game_y * 32 + game_x;
    unsigned char code = memory[FAN_MEM_VRAM2 + offs];
    unsigned char attr = memory[FAN_MEM_CRAM + offs];
    unsigned char bank = attr & 0x07;

    // FG layout: charlayout_memory MAME ha planes a 0 e 0x800. Per coerenza
    // con il SWAP gia' applicato al BG (necessario per mare blu) si applica
    // SWAP anche qui: bp0 (LSB pen) da offset 0x800, bp1 (MSB) da offset 0.
    // Senza swap su FG: Tom (mongolfiera/personaggio) e i nemici (uccelli liv.3,
    // palle cannone, ecc.) avevano pen 1<->2 invertiti -> uccelli rossi invece
    // di verdi e parti FG mancanti perche' colorate come backcolor.
    // Stesso pattern di Nibbler (machines/nibbler/nibbler.cpp blit_fg_tile).
    const unsigned char *bp0 = memory + FAN_MEM_CGRAM + 0x800 + code * 8;
    const unsigned char *bp1 = memory + FAN_MEM_CGRAM + code * 8;

    const unsigned short *pal = &fantasy_palette[bank * 4];

    bool flip = (ctrl_reg & 0x80) != 0;
    unsigned short *fb = rt + col * 8;
    for (int dy = 0; dy < 8; dy++, fb += (FAN_FB_STRIDE - 8)) {
        int gpx = flip ? (7 - dy) : dy;
        for (int dx = 0; dx < 8; dx++, fb++) {
            int gpy = flip ? dx : (7 - dx);
            unsigned char p = ((bp0[gpy] >> (7 - gpx)) & 1)
                            | (((bp1[gpy] >> (7 - gpx)) & 1) << 1);
            if (p == 0) continue;
            *fb = pgm_read_word_near(&pal[p]);
        }
    }
}

const unsigned short *fantasy::logo() {
    return fantasy_logo;   // stage 2a: blank black placeholder (replace in 2c)
}
