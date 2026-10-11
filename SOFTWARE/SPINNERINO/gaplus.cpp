// ============================================================================
// gaplus.cpp — GAPLUS (Namco 1984, set `gaplus`) per SPINNERINO PLUS, nella
// griglia GIOCHI ARCADE (ultima cella). Dati su SD in /sdcard/gaplus/.
//
// Porting da CABINATONE\gaplus.cpp (03/10/2026, chiuso al primo flash). Vedi
// emulation/gaplus.h per il perche' e' un player AUTONOMO.
//
// ⭐ DUE SORGENTI, ognuna per la sua parte ([[feedback_porta_non_riprogettare]]):
//   · L'EMULAZIONE e' COPIATA PER NUMERO DI RIGA da CABINATONE\gaplus.cpp
//     (bus delle tre 6809, 56XX/58XX, custom #3, stelle, tile, sprite, sintesi
//     del 15XX): li' e' stata verificata contro MAME. Lo script che ha
//     costruito questo file controlla che quelle righe siano proprio quelle.
//   · Il GUSCIO (task, finestra SPI, blit a strisce, pagine di stato, SD,
//     task audio sul core 0, uscita con START 3 s) e' quello di ghouls.cpp,
//     che su QUESTA scheda gira.
//
// ⛔ COSA E' STATO ADATTATO rispetto a CABINATONE (e SOLO questo):
//   1. il display: la' DSI 1024x600 + PPA, qui pannello SPI scritto a strisce.
//      ⭐ Il pannello dello SPINNERINO vuole il quadro NATIVO MAME 288x224
//      (come Xevious, gw=288, e Galaga: riga di pannello = 223 - x verticale,
//      colonna = y verticale). Il render resta quello del CABINATONE (fb
//      VERTICALE 224x288) e la trasposizione la fa SOLO gp_blit().
//   2. i colori: i file della SD sono gli stessi del CABINATONE (RGB565
//      NATIVO, il DSI non vuole il byte-swap); il pannello SPI lo vuole, e lo
//      fa gp_blit() mentre traspone. Una riga, in un posto solo.
//   3. l'audio: audio_pce_* -> ponte laserdisc_audio_begin/write/end, e il
//      VOLUME lo applica il player ([[feedback_ponte_audio_non_applica_volume]]).
//   4. l'input: bt_get_buttons() -> g_input->buttons_get() (stesse maschere).
//   5. il core 6809: la copia RINOMINATA gp6809_* di quello del CABINATONE
//      (cpus/m6809gp/), non quello della griglia — vedi m6809gp.h.
//   6. l'uscita: la' esp_restart(), qui si RIENTRA nella griglia: tutto cio'
//      che si prende si restituisce in gp_free()
//      ([[feedback_init_una_volta_sola_vs_rientro]]).
// ============================================================================
#pragma GCC optimize("O2")

#include <Arduino.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/gaplus.h"

#ifdef ENABLE_GAPLUS

#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"

// ⭐ il core 6809 di CABINATONE, coi nomi rinominati gp6809_* dal suo header:
// qui si scrive m6809_step(), il preprocessore lo fa diventare gp6809_step().
#include "cpus/m6809gp/m6809gp.h"

extern Video video;
extern bool  input_consume_back_to_menu();
extern void  laserdisc_audio_begin(void);
extern void  laserdisc_audio_end(void);
extern void  laserdisc_audio_write(const int16_t *stereo, int frames);
extern bool  input_is_vol_mode_active(void);
extern int   laserdisc_current_volume(void);

// stesse maschere del CABINATONE (input.h: LEFT 01 RIGHT 02 UP 04 DOWN 08
// FIRE 10 START 20 COIN 40 EXTRA 80): il codice del gioco non cambia
#define B_LEFT  BUTTON_LEFT
#define B_RIGHT BUTTON_RIGHT
#define B_UP    BUTTON_UP
#define B_DOWN  BUTTON_DOWN
#define B_FIRE  BUTTON_FIRE
#define B_START BUTTON_START
#define B_COIN  BUTTON_COIN
#define B_EXTRA BUTTON_EXTRA

// ---------------------------------------------------------------- geometria
// ⭐ Dalla FONTE: `set_size(36*8, 28*8)` con visarea piena, ROT90. Il quadro
// verticale e' 224x288 TUTTO visibile, come GALAGA (stesso chip video Namco).
#define GP_W 224
#define GP_H 288
// Sul pannello: quadro nativo 288x224, centrato sull'asse da 320 come Xevious
// (gx = 16), dalla riga TFT_HW_Y come tutti i giochi della griglia.
#define GP_PW     GP_H                       // 288 colonne di pannello
#define GP_PH     GP_W                       // 224 righe di pannello
#define GP_PX0    ((TFT_FB_W - GP_PW) / 2)   // 16
#define LD_FB_H   240
// ⛔ 32 KB come ghouls.cpp: questo task chiama sd_mount(), vorace di stack.
#define GP_STACK  32768
#define GP_DIR    SD_MOUNT_POINT "/gaplus"

// 1.000.000 / 60,606060 = 16.500 us
#define FRAME_US 16500
// 1.536.000 Hz / 60,606060 = 25.344 cicli per CPU per quadro
#define GP_CYC_FRAME 25344
// `set_maximum_quantum(attotime::from_hz(6000))`: 1.536.000/6000 = 256
#define GP_FETTA     256
// 50 us dopo il vblank il 56XX/58XX eseguono il comando: 50 x 1,536 = 77 cicli
#define GP_CYC_IO    77

#define GP_NUM_TILES  512
#define GP_NUM_SPR    384
#define GP_MAX_STARS  250

// DIP di fabbrica (MAME, tutti a 1 = attivi bassi spenti): 1 gettone 1 credito,
// 3 vite, suono dell'attract acceso, bonus 50k/150k/150k, difficolta' standard,
// service (gapluso: DIP SW2:1) spento.
#define GP_DSWA_HIGH 0x0F
#define GP_DSWA_LOW  0x0F
#define GP_DSWB_HIGH 0x0F
#define GP_DSWB_LOW  0x0F
// IN2 del custom #3: cabinato verticale (bit 2) + bit 3 a riposo
#define GP_IN2       0x0F

// ---------------------------------------------------------------- stato
// ⛔⛔⛔ Niente array globali: TUTTO lo stato del gioco sta in una struct
// allocata nel task e restituita all'uscita. I globali di un gioco occupano
// RAM interna anche mentre ne gira un altro.
struct GpIo {                         // Namco 56XX / 58XX (namcoio.cpp)
    uint8_t ram[16];
    int lastcoins, lastbuttons, credits;
    int coins[2], coins_per_cred[2], creds_per_coin[2];
};
struct GpStella { int16_t x, y; uint16_t col; uint8_t set, pad; };
struct GpVoce { uint32_t cnt; };

struct GpStato {
    m6809_state cpu[3];               // 0 main, 1 sub, 2 sub2 (suono)
    int     resto[3];                 // cicli gia' fatti oltre la fetta
    uint8_t mem[0x2000];              // $0000-$07FF videoram, $0800-$1FFF spriteram
    uint8_t snd_ram[0x3C0];           // 15XX $040-$3FF: RAM condivisa vera
    uint8_t soundregs[0x40];          // 15XX $000-$03F: i registri delle voci
    uint8_t cio3[16];                 // custom #3 a $6820
    GpIo    io[2];                    // 0 = 56XX ($6800), 1 = 58XX ($6810)
    uint8_t main_irq_mask, sub_irq_mask, sub2_irq_mask;
    uint8_t subs_reset;               // 1 = sub e sub2 tenute in reset
    uint8_t io_reset;                 // 1 = 56XX/58XX tenuti in reset
    uint8_t snd_enable;               // sound_enable_w del 15XX
    uint8_t keys;                     // pulsanti, campionati UNA volta per quadro
    uint8_t starfield_control[4];
    int     star_count, starfield_framecount;
    GpStella stars[GP_MAX_STARS];
    // l'esplosione: la ARMA l'emulazione, la consuma il task audio
    volatile uint32_t bang_pos;       // 0xFFFFFFFF = ferma
    volatile uint32_t bang_seq;       // ogni scrittura «bang» la fa ripartire

    uint8_t  *rom_main, *rom_sub, *rom_sub2;
    uint16_t *tiles;                  // [512][8]     2 bit/pixel, ruotati
    uint32_t *sprites;                // [384][16][2] 4 bit/pixel, ruotati
    uint8_t  *cmapbin;                // vedi romconv_gaplus.py
    int8_t   *wavetab;                // [8][32]
    int16_t  *bang;
    uint32_t  bang_n;
    uint16_t *fb;                     // 224x288 VERTICALE, colori NATIVI
    size_t    fb_sz;
    int       vol_ultimo;             // barra del volume: ultimo riempimento disegnato
    // audio
    bool      audio_ok;
    int16_t  *aud_out;
    GpVoce    voci[8];
    uint32_t  bang_seq_visto;
    int16_t   rs_prev, rs_cur;
    uint32_t  rs_frac;
};
static GpStato *S = nullptr;          // lo stato del gioco (calloc nel task)

#define rom_main       (S->rom_main)
#define rom_sub        (S->rom_sub)
#define rom_sub2       (S->rom_sub2)
#define tiles          (S->tiles)
#define sprites        (S->sprites)
#define cmapbin        (S->cmapbin)
#define wavetab        (S->wavetab)
#define bang           (S->bang)
#define bang_n         (S->bang_n)
#define fb             (S->fb)
#define fb_sz          (S->fb_sz)
#define vol_ultimo     (S->vol_ultimo)
#define audio_ok       (S->audio_ok)
#define aud_out        (S->aud_out)
#define voci           (S->voci)
#define bang_seq_visto (S->bang_seq_visto)
#define rs_prev        (S->rs_prev)
#define rs_cur         (S->rs_cur)
#define rs_frac        (S->rs_frac)

// ---- player (le sole variabili fuori da S: puntatori e bandiere) -----------
static uint16_t *g_strip = nullptr;
static Input    *g_input = nullptr;
static volatile bool g_stop = false;
static volatile bool g_done = false;
static volatile bool g_aud_running = false;

#define CM_TILE(g)    ((const uint16_t *)cmapbin + 4 * (g))
#define CM_SPR(g)     ((const uint16_t *)(cmapbin + 512) + 8 * (g))
#define CM_TMASK(g)   (cmapbin[1536 + (g)])
#define CM_SMASK(g)   (cmapbin[1600 + (g)])
#define CM_FONDO      (*(const uint16_t *)(cmapbin + 1664))

// ---------------------------------------------------------------- bus 6809
// Trascritto da `m6809_read/write` di Claudio e confrontato con le address_map
// di MAME (cpu1_map, cpu2_map, cpu3_map). L'istanza si riconosce dal puntatore.
static void gp_io_reset(void)
{
    for (int c = 0; c < 2; c++) {
        GpIo *io = &S->io[c];
        memset(io->ram, 0, sizeof(io->ram));
        io->lastcoins = io->lastbuttons = io->credits = 0;
        io->coins[0] = io->coins[1] = 0;
        io->coins_per_cred[0] = io->coins_per_cred[1] = 1;
        io->creds_per_coin[0] = io->creds_per_coin[1] = 1;
    }
}

// customio_3_r di gaplus_m.cpp
static uint8_t gp_cio3_r(uint8_t off)
{
    const uint8_t mode = S->cio3[8];
    switch (off) {
        case 0:  return GP_IN2;
        case 1:  return (mode == 2) ? S->cio3[1] : 0x0F;
        case 2:  return (mode == 2) ? 0x0F : 0x0E;
        case 3:  return (mode == 2) ? S->cio3[3] : 0x01;
        default: return S->cio3[off];
    }
}

static uint8_t gp_15xx_r(uint16_t o)
{
    o &= 0x03FF;
    return (o < 0x40) ? S->soundregs[o] : S->snd_ram[o - 0x40];
}
static void gp_15xx_w(uint16_t o, uint8_t v)
{
    o &= 0x03FF;
    if (o < 0x40) S->soundregs[o] = v;
    else          S->snd_ram[o - 0x40] = v;
}

static uint8_t gp_rd(m6809_state *s, uint16_t a)
{
    if (s == &S->cpu[1]) {                               // sub
        if (a < 0x2000)  return S->mem[a];
        if (a >= 0xA000) return rom_sub[a - 0xA000];
        return 0xFF;
    }
    if (s == &S->cpu[2]) {                               // sub2 (suono)
        if (a < 0x0400)  return gp_15xx_r(a);
        if (a >= 0xE000) return rom_sub2[a - 0xE000];
        return 0xFF;                                     // watchdog
    }
    // main
    if (a < 0x2000)                return S->mem[a];
    if ((a & 0xFC00) == 0x6000)    return gp_15xx_r(a);
    if ((a & 0xFFF0) == 0x6800)    return 0xF0 | S->io[0].ram[a & 0x0F];   // namcoio_1 = 56XX
    if ((a & 0xFFF0) == 0x6810)    return 0xF0 | S->io[1].ram[a & 0x0F];   // namcoio_2 = 58XX
    if ((a & 0xFFF0) == 0x6820)    return gp_cio3_r(a & 0x0F);
    if (a >= 0xA000)               return rom_main[a - 0xA000];
    return 0xFF;                                         // watchdog / vuoto
}

static void gp_sub_reset(void);

static void gp_wr(m6809_state *s, uint16_t a, uint8_t v)
{
    if (s == &S->cpu[1]) {                               // sub
        if (a < 0x2000) { S->mem[a] = v; return; }
        if ((a & 0xF000) == 0x6000) {                    // irq_2_ctrl_w: bit 0
            S->sub_irq_mask = a & 1;
            if (!S->sub_irq_mask) S->cpu[1].irq_pending = 0;
        }
        return;
    }
    if (s == &S->cpu[2]) {                               // sub2 (suono)
        if (a < 0x0400) { gp_15xx_w(a, v); return; }
        if (a >= 0x4000 && a < 0x8000) {                 // irq_3_ctrl_w: !bit 13
            S->sub2_irq_mask = (a & 0x2000) ? 0 : 1;
            if (!S->sub2_irq_mask) S->cpu[2].irq_pending = 0;
        }
        return;                                          // watchdog / ROM
    }
    // main
    if (a < 0x2000) { S->mem[a] = v; return; }
    if ((a & 0xFC00) == 0x6000) { gp_15xx_w(a, v); return; }
    if ((a & 0xFFF0) == 0x6800) { S->io[0].ram[a & 0x0F] = v & 0x0F; return; }
    if ((a & 0xFFF0) == 0x6810) { S->io[1].ram[a & 0x0F] = v & 0x0F; return; }
    if ((a & 0xFFF0) == 0x6820) {                        // customio_3_w
        const uint8_t off = a & 0x0F;
        if (off == 9 && v >= 0x0F) S->bang_seq++;        // m_samples->start(0,0)
        S->cio3[off] = v;
        return;
    }
    if ((a & 0xF000) == 0x7000) {                        // irq_1_ctrl_w: !bit 11
        S->main_irq_mask = (a & 0x0800) ? 0 : 1;
        if (!S->main_irq_mask) S->cpu[0].irq_pending = 0;
        return;
    }
    if ((a & 0xF000) == 0x8000) {                        // sreset_w: !bit 11
        const uint8_t gira = (a & 0x0800) ? 0 : 1;
        if (gira && S->subs_reset) gp_sub_reset();       // linea di reset rilasciata
        S->subs_reset = !gira;
        S->snd_enable = gira;
        return;
    }
    if ((a & 0xF000) == 0x9000) {                        // freset_w: !bit 11
        const uint8_t in_reset = (a & 0x0800) ? 1 : 0;
        if (in_reset && !S->io_reset) gp_io_reset();
        S->io_reset = in_reset;
        return;
    }
    if ((a & 0xF800) == 0xA000) {                        // starfield_control_w
        S->starfield_control[a & 3] = v;
        return;
    }
}

// ⚠️ ADATTAMENTO: su CABINATONE le tre callback del core passavano da
// m6809_bus.cpp (M6809Bus + m6809_bus_set). Qui il core e' una copia SOLO di
// GAPLUS, quindi le sue callback (gp6809_*, rinominate dal suo header) si
// definiscono qui e vanno dritte al bus del gioco, identico.
extern "C" uint8_t m6809_read(m6809_state *s, uint16_t a)          { return gp_rd(s, a); }
extern "C" void    m6809_write(m6809_state *s, uint16_t a, uint8_t v) { gp_wr(s, a, v); }
extern "C" uint8_t m6809_read_opcode(m6809_state *s, uint16_t a)   { return gp_rd(s, a); }

static void gp_sub_reset(void)
{
    m6809_reset(&S->cpu[1]);
    m6809_reset(&S->cpu[2]);
    S->resto[1] = S->resto[2] = 0;
}

// ---------------------------------------------------------------- chip I/O
// namcoio.cpp: 56XX e 58XX, trascritti (sono identici a quelli di Claudio).
// Porte a 4 bit ATTIVE BASSE. 56XX: 0 COINS, 1 P1, 2 P2, 3 BUTTONS.
// 58XX: 0 DSWA_HIGH, 1 DSWB_LOW, 2 DSWB_HIGH, 3 DSWA_LOW.
static uint8_t gp_io_in(int chip, int port)
{
    if (chip == 0) {
        const uint8_t k = S->keys;
        uint8_t v = 0x0F;
        switch (port) {
            case 0: if (k & B_COIN)  v &= ~0x01; break;          // COIN1
            case 1: if (k & B_UP)    v &= ~0x01;
                    if (k & B_RIGHT) v &= ~0x02;
                    if (k & B_DOWN)  v &= ~0x04;
                    if (k & B_LEFT)  v &= ~0x08; break;
            case 2: break;                                       // P2: cocktail
            case 3: if (k & B_FIRE)  v &= ~0x01;                 // BUTTON1
                    if (k & B_START) v &= ~0x04; break;          // START1
        }
        return v;
    }
    switch (port) {
        case 0: return GP_DSWA_HIGH;
        case 1: return GP_DSWB_LOW;
        case 2: return GP_DSWB_HIGH;
        default: return GP_DSWA_LOW;
    }
}

static void gp_handle_coins(int chip, int swap)
{
    GpIo *c = &S->io[chip];
    int credit_add = 0, credit_sub = 0;

    int val = ~gp_io_in(chip, 0);
    int toggled = val ^ c->lastcoins;
    c->lastcoins = val;
    if (val & toggled & 0x01) {
        c->coins[0]++;
        if (c->coins[0] >= (c->coins_per_cred[0] & 7)) {
            credit_add = c->creds_per_coin[0] - (c->coins_per_cred[0] >> 3);
            c->coins[0] -= c->coins_per_cred[0] & 7;
        } else if (c->coins_per_cred[0] & 8) credit_add = 1;
    }
    if (val & toggled & 0x02) {
        c->coins[1]++;
        if (c->coins[1] >= (c->coins_per_cred[1] & 7)) {
            credit_add = c->creds_per_coin[1] - (c->coins_per_cred[1] >> 3);
            c->coins[1] -= c->coins_per_cred[1] & 7;
        } else if (c->coins_per_cred[1] & 8) credit_add = 1;
    }
    if (val & toggled & 0x08) credit_add = 1;

    val = ~gp_io_in(chip, 3);
    toggled = val ^ c->lastbuttons;
    c->lastbuttons = val;
    if (c->ram[9] == 0) {
        if (val & toggled & 0x04)      { if (c->credits >= 1) credit_sub = 1; }
        else if (val & toggled & 0x08) { if (c->credits >= 2) credit_sub = 2; }
    }
    c->credits += credit_add - credit_sub;

    c->ram[0 ^ swap] = (uint8_t)((c->credits / 10) & 0x0F);
    c->ram[1 ^ swap] = (uint8_t)((c->credits % 10) & 0x0F);
    if (credit_add) c->ram[2 ^ swap] = (uint8_t)(credit_add & 0x0F);
    if (credit_sub) c->ram[3 ^ swap] = (uint8_t)(credit_sub & 0x0F);
    c->ram[4] = (uint8_t)(~gp_io_in(chip, 1) & 0x0F);
    c->ram[5] = (uint8_t)((((val & 0x05) << 1) | (val & toggled & 0x05)) & 0x0F);
    c->ram[6] = (uint8_t)(~gp_io_in(chip, 2) & 0x0F);
    c->ram[7] = (uint8_t)(((val & 0x0A) | ((val & toggled & 0x0A) >> 1)) & 0x0F);
}

static void gp_run_56xx(void)
{
    GpIo *c = &S->io[0];
    switch (c->ram[8]) {
        case 1:
            for (int p = 0; p < 4; p++) c->ram[p] = (uint8_t)(~gp_io_in(0, p) & 0x0F);
            break;
        case 2:
            c->coins_per_cred[0] = c->ram[9];  c->creds_per_coin[0] = c->ram[10];
            c->coins_per_cred[1] = c->ram[11]; c->creds_per_coin[1] = c->ram[12];
            break;
        case 4:
            gp_handle_coins(0, 0);
            break;
        case 8: {
            int sum = 0;
            for (int i = 9; i < 16; i++) sum += c->ram[i];
            c->ram[0] = (uint8_t)((sum >> 4) & 0x0F);
            c->ram[1] = (uint8_t)(sum & 0x0F);
            break;
        }
        case 9:
            for (int p = 0; p < 4; p++) {
                c->ram[2 * p]     = (uint8_t)(~gp_io_in(0, p) & 0x0F);
                c->ram[2 * p + 1] = (uint8_t)(~gp_io_in(0, p) & 0x0F);
            }
            break;
        default: break;
    }
}

static void gp_run_58xx(void)
{
    GpIo *c = &S->io[1];
    switch (c->ram[8]) {
        case 1:
            for (int p = 0; p < 4; p++) c->ram[4 + p] = (uint8_t)(~gp_io_in(1, p) & 0x0F);
            break;
        case 2:
            c->coins_per_cred[0] = c->ram[9];  c->creds_per_coin[0] = c->ram[10];
            c->coins_per_cred[1] = c->ram[11]; c->creds_per_coin[1] = c->ram[12];
            break;
        case 3:
            gp_handle_coins(1, 2);
            break;
        case 4:
            for (int p = 0; p < 4; p++) {
                c->ram[2 * p]     = (uint8_t)(~gp_io_in(1, p) & 0x0F);
                c->ram[2 * p + 1] = (uint8_t)(~gp_io_in(1, p) & 0x0F);
            }
            break;
        case 5: {                      // bootup check: XOR pilotati da un LFSR
            #define NEXT(n) ((((n) & 1) ? (n) ^ 0x90 : (n)) >> 1)
            int i, n, rng, seed;
            n = (c->ram[9] * 16 + c->ram[10]) & 0x7F;
            seed = 0x22;
            for (i = 0; i < n; i++) seed = NEXT(seed);
            for (i = 1; i < 8; i++) {
                n = 0;
                rng = seed;
                if (rng & 1) n ^= ~c->ram[11];
                rng = NEXT(rng);
                seed = rng;
                if (rng & 1) n ^= ~c->ram[10];
                rng = NEXT(rng);
                if (rng & 1) n ^= ~c->ram[9];
                rng = NEXT(rng);
                if (rng & 1) n ^= ~c->ram[15];
                rng = NEXT(rng);
                if (rng & 1) n ^= ~c->ram[14];
                rng = NEXT(rng);
                if (rng & 1) n ^= ~c->ram[13];
                rng = NEXT(rng);
                if (rng & 1) n ^= ~c->ram[12];
                c->ram[i] = (uint8_t)(~n & 0x0F);
            }
            c->ram[0] = 0x0;
            if (c->ram[9] == 0xF) c->ram[0] = 0xF;     // il «kludge for gaplus» di MAME
            #undef NEXT
            break;
        }
        default: break;
    }
}

// ---------------------------------------------------------------- esecuzione
// Le tre CPU avanzano a fette di 256 cicli, l'una dopo l'altra, col resto della
// fetta riportato alla successiva (l'idiom di `arkanoid.cpp:818`). Sub e sub2
// stanno ferme mentre la loro linea di reset e' tenuta.
static void gp_corri(int c, int fino)
{
    m6809_state *s = &S->cpu[c];
    int fatti = S->resto[c];
    while (fatti < fino) fatti += m6809_step(s);
    S->resto[c] = fatti - fino;
}

static void gp_fetta(int cicli)
{
    gp_corri(0, cicli);
    if (!S->subs_reset) {
        gp_corri(1, cicli);
        gp_corri(2, cicli);
    }
}

static void gp_stelle_vblank(void);

static void gp_run_frame(void)
{
    S->keys = g_input->buttons_get();     // ADATTAMENTO: era bt_get_buttons()

    // vblank_irq (gapluso_state): IRQ a livello, tenuto finche' servito o
    // finche' il gioco non lo spegne dal suo registro.
    if (S->main_irq_mask) m6809_irq(&S->cpu[0]);
    if (!S->subs_reset) {
        if (S->sub_irq_mask)  m6809_irq(&S->cpu[1]);
        if (S->sub2_irq_mask) m6809_irq(&S->cpu[2]);
    }

    // 50 us di CPU, poi i due chip I/O eseguono il comando appena scritto
    gp_fetta(GP_CYC_IO);
    if (!S->io_reset) { gp_run_58xx(); gp_run_56xx(); }

    int fatti = GP_CYC_IO;
    while (fatti < GP_CYC_FRAME) {
        const int n = (GP_CYC_FRAME - fatti < GP_FETTA) ? (GP_CYC_FRAME - fatti) : GP_FETTA;
        gp_fetta(n);
        fatti += n;
    }

    gp_stelle_vblank();
}

// ---------------------------------------------------------------- disegno
// Il quadro di MAME e' 288x224 (x orizzontale) e il gioco e' ROT90: il nostro
// fb e' quello VERTICALE, con **px = 223 - y_mame, py = x_mame**. Grafica e
// coordinate si trasformano UNA volta, qui; il resto e' il driver alla lettera.
static inline void gp_px(int xm, int ym, uint16_t c)
{
    if ((unsigned)xm < 288u && (unsigned)ym < 224u)
        fb[(size_t)xm * GP_W + (223 - ym)] = c;
}

// gaplus_v.cpp: starfield_render()
static void gp_stelle_disegna(void)
{
    if (!(S->starfield_control[0] & 1)) return;
    for (int i = 0; i < S->star_count; i++) {
        const GpStella *st = &S->stars[i];
        if (st->set == 1 && S->starfield_control[2] != 0x85 && (i % 2) == 0) {
            const unsigned v = (unsigned)(S->starfield_framecount + i);
            const int b = ((v >> 3) & 1) ? 1 : 2;
            if ((v >> b) & 1) continue;
        }
        gp_px(st->x, st->y, st->col);
    }
}

// gaplus_v.cpp: screen_vblank() — fronte di discesa, una volta per quadro
static void gp_stelle_vblank(void)
{
    S->starfield_framecount++;
    if (!(S->starfield_control[0] & 1)) return;
    for (int i = 0; i < S->star_count; i++) {
        GpStella *st = &S->stars[i];
        switch (S->starfield_control[st->set + 1]) {
            case 0x87: break;
            case 0x85: case 0x86: st->x += 1; break;
            case 0x06: st->x += 2; break;
            case 0x80: st->x -= 1; break;
            case 0x82: st->x -= 2; break;
            case 0x81: st->x -= 3; break;
            case 0x9f: st->y += 3; break;
            case 0xaf: st->y -= 3; break;
        }
        if (st->x < 16)        st->x += 288 - 32;
        if (st->x >= 288 - 16) st->x -= 288 - 32;
        if (st->y < 0)         st->y += 224;
        if (st->y >= 224)      st->y -= 224;
    }
}

// tilemap 36x28 col `tilemap_scan` di gaplus_v.cpp. Nel fb verticale la colonna
// MAME `col` (0..35) e' la RIGA di tile, la riga MAME `row` (0..27) sta alla
// colonna di tile 27-row.
static void gp_tiles(int categoria)
{
    for (int col = 0; col < 36; col++) {
        for (int row = 0; row < 28; row++) {
            int r = row + 2, c = col - 2, idx;
            if (c & 0x20) idx = r + ((c & 0x1F) << 5);
            else          idx = c + (r << 5);
            const uint8_t attr = S->mem[0x400 + idx];
            if (((attr >> 6) & 1) != categoria) continue;
            const int code = S->mem[idx] + ((attr & 0x80) << 1);
            const int gr = attr & 0x3F;
            const uint16_t *pal = CM_TILE(gr);
            const uint8_t trasp = CM_TMASK(gr);
            const uint16_t *t = tiles + 8 * code;
            uint16_t *p = fb + (size_t)(8 * col) * GP_W + 8 * (27 - row);
            for (int y = 0; y < 8; y++, p += GP_W) {
                unsigned pix = t[y];
                for (int x = 0; x < 8; x++, pix >>= 2) {
                    const int pen = pix & 3;
                    if (!((trasp >> pen) & 1)) p[x] = pal[pen];
                }
            }
        }
    }
}

// gaplus_v.cpp: draw_sprites(), alla lettera. Gli sprite sono memorizzati
// ruotati (rot[y][x] = mame[15-x][y]), quindi il pixel MAME (colonna i, riga j)
// e' rot[i][15-j]: un nibble per pixel, x=0 nel nibble basso.
static void gp_sprite_1(int code, int color, int flipx, int flipy, int sx, int sy)
{
    code %= GP_NUM_SPR;                                  // gfx_element::get_data
    const uint32_t *spr = sprites + (size_t)code * 32;
    const uint16_t *pal = CM_SPR(color);
    const uint8_t trasp = CM_SMASK(color);
    for (int i = 0; i < 16; i++) {
        const int xm = sx + i;
        if ((unsigned)xm >= 288u) continue;
        const int sc = flipx ? 15 - i : i;               // colonna sorgente
        const uint32_t w0 = spr[2 * sc], w1 = spr[2 * sc + 1];
        uint16_t *riga = fb + (size_t)xm * GP_W;
        for (int j = 0; j < 16; j++) {
            const int ym = sy + j;
            if ((unsigned)ym >= 224u) continue;
            const int sr = flipy ? 15 - j : j;           // riga sorgente
            const int k = 15 - sr;                       // colonna ruotata
            const int pen = (k < 8 ? (w0 >> (4 * k)) : (w1 >> (4 * (k - 8)))) & 7;
            if (!((trasp >> pen) & 1)) riga[223 - ym] = pal[pen];
        }
    }
}

static void gp_sprites(void)
{
    static const uint8_t gfx_offs[2][2] = { { 0, 1 }, { 2, 3 } };
    const uint8_t *spriteram   = S->mem + 0x0800 + 0x780;
    const uint8_t *spriteram_2 = spriteram + 0x800;
    const uint8_t *spriteram_3 = spriteram_2 + 0x800;
    // ⚠️ flip_screen (spriteram[0x1f7f-0x800] & 1) lo accende solo il cocktail:
    // il banco conta se compare mai.
    for (int offs = 0; offs < 0x80; offs += 2) {
        if (spriteram_3[offs + 1] & 2) continue;
        const int sprite = spriteram[offs] | ((spriteram_3[offs] & 0x40) << 2);
        const int color  = spriteram[offs + 1] & 0x3F;
        const int sx     = spriteram_2[offs + 1] + 0x100 * (spriteram_3[offs + 1] & 1) - 71;
        int       sy     = 256 - spriteram_2[offs] - 8;
        const int flipx  = spriteram_3[offs] & 1;
        const int flipy  = (spriteram_3[offs] >> 1) & 1;
        const int sizex  = (spriteram_3[offs] >> 3) & 1;
        const int sizey  = (spriteram_3[offs] >> 5) & 1;
        const int dup    = spriteram_3[offs] & 0x80;
        sy -= 16 * sizey;
        sy = (sy & 0xFF) - 32;
        for (int y = 0; y <= sizey; y++)
            for (int x = 0; x <= sizex; x++)
                gp_sprite_1(sprite + (dup ? 0 : gfx_offs[y ^ (sizey * flipy)][x ^ (sizex * flipx)]),
                            color, flipx, flipy, sx + 16 * x, sy + 16 * y);
    }
}

// screen_update(): fondo (pen 0), stelle, caratteri bassi, sprite, caratteri alti
static void gp_render(void)
{
    const uint32_t f2 = (uint32_t)CM_FONDO | ((uint32_t)CM_FONDO << 16);
    uint32_t *q = (uint32_t *)fb;
    for (size_t i = 0; i < (size_t)GP_W * GP_H / 2; i++) q[i] = f2;
    gp_stelle_disegna();
    gp_tiles(0);
    gp_sprites();
    gp_tiles(1);
}

// ============================================================ blit sul pannello
// ⛔ QUI NON SI COPIA DA CABINATONE (la' c'erano PPA e lcd_draw_bitmap).
// ⭐⭐ GEOMETRIA — quella di GALAGA e XEVIOUS su questa scheda. Il pannello
// vuole il quadro NATIVO 288x224 del gioco: riga di pannello r = 223 - px,
// colonna di pannello c = py, dove (px,py) e' il pixel del fb VERTICALE del
// CABINATONE (px orizzontale 0..223, py verticale 0..287). E' la mappa di
// galaga.cpp di questo progetto: `fb_y = 223 - arcade_x`, `fb_x = arcade_y`.
// ⭐ I colori: la tavolozza della SD e' RGB565 NATIVO (quella del CABINATONE),
// questo pannello SPI lo vuole BYTE-SWAPPED — si gira qui, in un posto solo.
// ⭐ ORDINE DEI CICLI = ACCESSI ALLA MEMORIA ([[feedback_ottimizzare_contando_accessi]]):
// il ciclo esterno percorre `fb` riga per riga (in ORDINE) e dentro si
// riempiono tutte e otto le righe della striscia, come gh_blit().
static void gp_blit(void)
{
    for (int r0 = 0; r0 < GP_PH; r0 += 8) {
        const int px0 = (GP_PH - 1) - r0;      // px della prima riga della striscia
        const uint16_t *src = fb + px0;         // fb[py * 224 + px0], py = 0
        uint16_t *dst = g_strip;
        for (int c = 0; c < GP_PW; c++) {
            for (int i = 0; i < 8; i++) {
                const uint16_t v = src[-i];      // px0 - i
                dst[i * GP_PW] = (uint16_t)((v >> 8) | (v << 8));
            }
            src += GP_W;                        // riga successiva di fb
            dst += 1;                           // colonna successiva del pannello
        }
        video.write(g_strip, GP_PW * 8);        // finestra STRETTA: 288, non 320
    }
    // ⚠️ video.write() e' ASINCRONA: si drena qui, una volta per quadro
    video.waitDMA();
}

// 2 = tutta la finestra del player (320x240): pagine di stato e bande nere.
// 1 = SOLO le 288 colonne x 224 righe del gioco.
// 0 = si restituisce la finestra al menu.
static void gp_window(int quale)
{
    video.waitDMA();
    if (quale == 2)      video.setAddrWindow(TFT_HW_X, 0, TFT_FB_W, LD_FB_H);
    else if (quale == 1) video.setAddrWindow(TFT_HW_X + GP_PX0, TFT_HW_Y, GP_PW, GP_PH);
    else                 video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// Dipinge di nero TUTTA la finestra del player, una volta sola, prima di
// stringere la finestra sul gioco: le bande non si rispediscono piu'.
static void gp_bande_nere(void)
{
    gp_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    video.waitDMA();
}

// ⛔⛔ La barra del volume va nella BANDA NERA, in pixel di PANNELLO, con la
// funzione condivisa dai player autonomi ([[feedback_barra_volume_player_autonomi]]).
// Sta nelle colonne 0..15: con la finestra a GP_PX0 = 16 e' esattamente la
// banda a sinistra del gioco, che nessun altro tocca. Si ridisegna SOLO quando
// cambia, quindi non lampeggia ([[feedback_vol_bar_no_flicker]]).
static void gp_vol_bar(void)
{
    const bool on = input_is_vol_mode_active();
    const int  f  = on ? laserdisc_current_volume() : -1;
    if (f == vol_ultimo) return;
    vol_ultimo = f;
    videoVolBarTop(g_strip, on, on ? f : 30);
    gp_window(1);                         // ridai la finestra STRETTA al gioco
}

// ---------------------------------------------------------------- pagine
// Testo scalato nella strip ruotata: copiata da gh_line di ghouls.cpp.
static void gp_line(int sy, const char *s, int y_p, int scale, uint16_t col)
{
    int slen = (int)strlen(s);
    int x0 = (240 - slen * 8 * scale) / 2; if (x0 < 2) x0 = 2;
    for (int i = 0; i < slen; i++) {
        int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++) {
                if (!(row & (0x80 >> c))) continue;
                for (int dr = 0; dr < scale; dr++) {
                    int x_l = y_p + r * scale + dr;
                    if (x_l < 0 || x_l >= TFT_FB_W) continue;
                    for (int dc = 0; dc < scale; dc++) {
                        int y_l = 239 - (x0 + (i * 8 + c) * scale + dc), y_in = y_l - sy;
                        if (y_in < 0 || y_in >= 8) continue;
                        g_strip[y_in * TFT_FB_W + x_l] = col;
                    }
                }
            }
        }
    }
}

static const uint16_t C_YELLOW = 0xE0FF;   // byte-swapped, come ghouls.cpp
static const uint16_t C_CYAN   = 0xFF07;
static const uint16_t C_GREY   = 0xEF7B;
static const uint16_t C_RED    = 0x00F8;

static void gp_page(const char *l[5], const uint16_t c[5])
{
    static const int Y[5] = { 150, 172, 194, 216, 250 };
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        gp_line(sy, "GAPLUS", 96, 3, C_YELLOW);
        for (int k = 0; k < 5; k++)
            if (l[k] && l[k][0]) gp_line(sy, l[k], Y[k], 1, c[k]);
        video.write(g_strip, TFT_FB_W * 8);
    }
    // subito dopo una pagina si legge quasi sempre dalla SD: si drena il DMA
    video.waitDMA();
}

static void gp_msg(const char *a, const char *b, uint16_t col)
{
    const char *l[5] = { "", a, b, "", "START 3s ESCI" };
    const uint16_t c[5] = { C_GREY, col, C_GREY, C_GREY, C_GREY };
    gp_page(l, c);
}

// Schermata di CARICAMENTO: nome del gioco e una riga, come gli altri player.
static void gp_caricamento(void)
{
    const char *l[5] = { "", "", "CARICAMENTO DA SD", "", "" };
    const uint16_t c[5] = { C_GREY, C_GREY, C_CYAN, C_GREY, C_GREY };
    gp_page(l, c);
}

// ---------------------------------------------------------------- uscita
static bool gp_want_exit(uint8_t b)
{
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & BUTTON_START) {
        uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}

// Al posto di gp_fatal() del CABINATONE (che faceva esp_restart): la pagina
// d'errore resta finche' l'utente non esce, poi si torna alla griglia.
static void gp_errore(const char *a, const char *b)
{
    while (!g_stop) {
        gp_msg(a, b, C_RED);
        if (gp_want_exit(g_input->buttons_get())) break;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
}

// ---------------------------------------------------------------- SD
// Al posto di sd_read_exact/sd_read_rom del CABINATONE: stessa regola, la
// dimensione deve essere ESATTA ([[fantasy_cabinatone_port]]).
static long gp_taglia(const char *nome)
{
    char p[64];
    snprintf(p, sizeof p, GP_DIR "/%s", nome);
    FILE *f = fopen(p, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fclose(f);
    return n;
}

static bool gp_leggi(const char *nome, void *dst, size_t n)
{
    char p[64];
    snprintf(p, sizeof p, GP_DIR "/%s", nome);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    size_t fatti = 0;
    unsigned pezzi = 0;
    while (fatti < n) {
        size_t q = n - fatti; if (q > 32768) q = 32768;
        if (fread((uint8_t *)dst + fatti, 1, q, f) != q) { fclose(f); return false; }
        fatti += q;
        if ((++pezzi & 7) == 0) vTaskDelay(1);   // respiro anti-watchdog
    }
    // la dimensione deve essere ESATTA: un byte in piu' e' un file sbagliato
    const bool fine = (fgetc(f) == EOF);
    fclose(f);
    return fine;
}

// ---------------------------------------------------------------- memoria
// ⭐⭐ IL CONTO DELLA RAM INTERNA ([[feedback_audio_muto_e_ram_interna]]).
// Ghouls ha misurato sul device 156-183 KB di interna libera a gioco avviato.
// GAPLUS ne chiede:
//     stack di questo task (prima di tutto)  32.768
//     stack del task audio                    4.096
//     GpStato (CPU, RAM, I/O, stelle)       ~12.000
//     tiles + cmap + wave                    10.116
//     aud_out                                 1.024
//                                          -------
//                                          ~60.000
// poi, SOLO se avanza oltre la riserva: i tre programmi (57.344) e fb
// (129.024); altrimenti PSRAM. Sprite (48 KB) ed esplosione (62 KB) in PSRAM.
// La riserva (48 KB, come ghouls) tiene posto agli stack e al Bluetooth.
#define GP_RISERVA_INT (48 * 1024)

static void *gp_calloc_caldo(size_t n)
{
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + GP_RISERVA_INT) {
        void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
        if (p) return p;
    }
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static void *gp_allineata_calda(size_t n)
{
    const size_t na = (n + 127) & ~(size_t)127;
    void *p = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= na + GP_RISERVA_INT)
        p = heap_caps_aligned_alloc(128, na, MALLOC_CAP_INTERNAL);
    if (!p) p = heap_caps_aligned_alloc(128, na, MALLOC_CAP_SPIRAM);
    if (p) memset(p, 0, na);
    return p;
}

// I dati della SD, nell'ordine e nelle memorie del CABINATONE (gp_load_all).
static bool gp_load_all(char *err, size_t errlen)
{
    struct F { const char *nome; size_t n; void **dst; bool calda; };
    const F FILES[] = {
        { "tiles.bin",   GP_NUM_TILES * 8 * 2,    (void **)&tiles,   true  },
        { "cmap.bin",    1668,                    (void **)&cmapbin, true  },
        { "wave.bin",    256,                     (void **)&wavetab, true  },
        { "sprites.bin", GP_NUM_SPR * 16 * 2 * 4, (void **)&sprites, false },
    };
    for (size_t i = 0; i < sizeof(FILES) / sizeof(FILES[0]); i++) {
        *FILES[i].dst = FILES[i].calda ? gp_calloc_caldo(FILES[i].n)
                                       : heap_caps_calloc(1, FILES[i].n, MALLOC_CAP_SPIRAM);
        if (!*FILES[i].dst) { snprintf(err, errlen, "MEMORIA ESAURITA"); return false; }
        if (!gp_leggi(FILES[i].nome, *FILES[i].dst, FILES[i].n)) {
            snprintf(err, errlen, "MANCA/ERRATO %s", FILES[i].nome);
            return false;
        }
    }

    // stelle: conteggio + 250 voci da 8 byte
    uint8_t *st = (uint8_t *)heap_caps_malloc(2 + 8 * GP_MAX_STARS, MALLOC_CAP_SPIRAM);
    if (!st) { snprintf(err, errlen, "MEMORIA ESAURITA"); return false; }
    if (!gp_leggi("stars.bin", st, 2 + 8 * GP_MAX_STARS)) {
        heap_caps_free(st);
        snprintf(err, errlen, "MANCA/ERRATO stars.bin");
        return false;
    }
    int n = st[0] | (st[1] << 8);
    if (n > GP_MAX_STARS) n = GP_MAX_STARS;
    S->star_count = n;
    for (int i = 0; i < n; i++) {
        const uint8_t *e = st + 2 + 8 * i;
        S->stars[i].x   = (int16_t)(e[0] | (e[1] << 8));
        S->stars[i].y   = (int16_t)(e[2] | (e[3] << 8));
        S->stars[i].col = (uint16_t)(e[4] | (e[5] << 8));
        S->stars[i].set = e[6];
    }
    heap_caps_free(st);

    // l'esplosione ha una lunghezza che dipende dal campione: si legge intera
    const long nb = gp_taglia("bang.bin");
    if (nb < 2) { snprintf(err, errlen, "MANCA/ERRATO bang.bin"); return false; }
    bang = (int16_t *)heap_caps_malloc((size_t)nb, MALLOC_CAP_SPIRAM);
    if (!bang) { snprintf(err, errlen, "MEMORIA ESAURITA"); return false; }
    if (!gp_leggi("bang.bin", bang, (size_t)nb)) { snprintf(err, errlen, "MANCA/ERRATO bang.bin"); return false; }
    bang_n = (uint32_t)(nb / 2);
    return true;
}

// ⭐ I tre programmi in RAM interna se c'e' posto (si leggono a ogni fetch),
// altrimenti in PSRAM: si caricano DOPO il framebuffer, come sul CABINATONE.
static bool gp_load_rom(char *err, size_t errlen)
{
    struct F { const char *nome; size_t n; uint8_t **dst; };
    const F FILES[] = {
        { "main.bin", 0x6000, &rom_main },
        { "sub.bin",  0x6000, &rom_sub  },
        { "sub2.bin", 0x2000, &rom_sub2 },
    };
    for (size_t i = 0; i < 3; i++) {
        *FILES[i].dst = (uint8_t *)gp_calloc_caldo(FILES[i].n);
        if (!*FILES[i].dst) { snprintf(err, errlen, "MEMORIA ESAURITA"); return false; }
        if (!gp_leggi(FILES[i].nome, *FILES[i].dst, FILES[i].n)) {
            snprintf(err, errlen, "MANCA/ERRATO %s", FILES[i].nome);
            return false;
        }
    }
    return true;
}

// ⛔⛔ Si RIENTRA nella griglia: tutto cio' che si e' preso si restituisce, e
// i puntatori si azzerano (S per ultimo: gli altri sono macro su S->).
static void gp_free(void)
{
    if (!S) return;
    void *B[] = { rom_main, rom_sub, rom_sub2, tiles, sprites, cmapbin,
                  wavetab, bang, fb, aud_out };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++)
        if (B[i]) heap_caps_free(B[i]);
    heap_caps_free(S);
    S = nullptr;
}
// ============================================================ audio
// ⭐ Namco 15XX trascritto da `sound/namco.cpp`: clock 24.000 Hz, contatore a 15
// bit di frazione (a 24 kHz `m_f_fracbits` vale 15 rispetto all'uscita), 32
// campioni per forma d'onda, voce = forma(-8..7) x volume(0..15). Si sintetizza
// a **24 kHz**, come il chip, e si ricampiona 3:4 verso i 32 kHz dell'I2S; il
// campione dell'esplosione sta gia' a 24 kHz ([[feedback_adattare_il_ritmo_non_le_costanti]]).
// LIVELLI nel rapporto di MAME: il 15XX esce come somma/1024 (MIX_RES =
// 128 x 8 voci) con route 1.0, i samples come campione/32768 con route 0.80.
// In int16 cio' fa **voce x 32** e **bang x 0,8**; GP_GAIN_NUM/DEN abbassa i
// due INSIEME per stare in famiglia con gli altri giochi senza toccare il
// rapporto (il banco misura il picco).
#define GP_SYNTH_RATE 24000
#define GP_OUT_FRAMES 256
// ⭐ MISURATO col banco (`mk_gptest.py audio`): a 1/2 la partita usciva a picco
// 15.556, il DOPPIO degli altri giochi (Phoenix 7.686, Galaxian 6.240, Lady Bug
// 7.348). 1/4 la riporta in famiglia; il rapporto 15XX/esplosione resta MAME.
// ⭐ SPINNERINO: 3/4 invece di 1/4. Sul CABINATONE 1/4 dava picco 7.778, in
// famiglia con quei giochi; qui i giochi della griglia arrivano a fondo
// scala e Ghouls e' stato alzato per questo ([[feedback_ponte_audio_non_applica_volume]]).
// 3/4 = picco 7.778 x 3 = ~23.300, sotto il clamp: il rapporto 15XX/esplosione
// di MAME resta quello. ⚠️ Stima dal picco MISURATO sul CABINATONE, non
// rimisurata qui: se l'utente lo sente forte o piano, e' QUESTO il numero.
#define GP_GAIN_NUM   3
#define GP_GAIN_DEN   4

static int16_t gp_synth_sample(void)
{
    int32_t wsg = 0;
    // ⚠️ `snd_enable` e i registri li scrive l'emulazione sull'ALTRO core:
    // si leggono volatile, o il compilatore li congela per tutto il blocco.
    if (*(volatile uint8_t *)&S->snd_enable) {
        const volatile uint8_t *r = S->soundregs;
        for (int ch = 0; ch < 8; ch++) {
            const int vol = r[ch * 8 + 3] & 0x0F;
            const uint32_t freq = (uint32_t)r[ch * 8 + 4] | ((uint32_t)r[ch * 8 + 5] << 8) |
                                  ((uint32_t)(r[ch * 8 + 6] & 0x0F) << 16);
            if (vol) {
                const int8_t *w = wavetab + 32 * ((r[ch * 8 + 6] >> 4) & 7);
                wsg += w[(voci[ch].cnt >> 15) & 0x1F] * vol;
                voci[ch].cnt += freq;                       // come namco_update_one:
            }                                               // solo le voci accese avanzano
        }
    }
    int32_t value = wsg * 32;

    // il campione riparte a ogni scrittura «bang» (samples->start(0,0))
    const uint32_t seq = S->bang_seq;
    if (seq != bang_seq_visto) { bang_seq_visto = seq; S->bang_pos = 0; }
    uint32_t pos = S->bang_pos;
    if (bang && pos < bang_n) {
        value += (int32_t)bang[pos] * 4 / 5;
        S->bang_pos = pos + 1;
    }

    value = value * GP_GAIN_NUM / GP_GAIN_DEN;
    if (value >  32767) value =  32767;
    if (value < -32768) value = -32768;
    return (int16_t)value;
}

// Ricampionatore lineare 24 -> 32 kHz, rapporto ESATTO 3/4 (quello di GALAXIAN).
#define RS_STEP ((uint32_t)((GP_SYNTH_RATE << 16) / 32000))   // 49152


// ⭐ Ricampionatore 24 -> 32 kHz del CABINATONE + VOLUME del player. Il ponte
// laserdisc_audio_write() non lo applica: lo fa ogni player nel suo mix
// ([[feedback_ponte_audio_non_applica_volume]]). E' un'ATTENUAZIONE: a volume
// massimo il fattore e' 30/30 e il livello resta quello misurato.
static void gp_audio_block(void)
{
    const int vs  = laserdisc_current_volume();      // 1..30, 1 = massimo
    const int vol = (vs >= 30) ? 0 : (31 - vs);      // 0..30, 30 = barra piena
    for (int i = 0; i < GP_OUT_FRAMES; i++) {
        const int32_t d = (int32_t)rs_cur - (int32_t)rs_prev;
        const int16_t s = (int16_t)((int32_t)rs_prev + ((d * (int32_t)(rs_frac >> 8)) >> 8));
        const int16_t sv = (int16_t)(((int32_t)s * vol) / 30);
        aud_out[2 * i] = aud_out[2 * i + 1] = sv;
        rs_frac += RS_STEP;
        while (rs_frac >= 0x10000) {
            rs_frac -= 0x10000;
            rs_prev = rs_cur;
            rs_cur  = gp_synth_sample();
        }
    }
    // ⭐ La write BLOCCA: e' l'orologio ([[feedback_task_audio_indietro_watchdog]]).
    laserdisc_audio_write(aud_out, GP_OUT_FRAMES);
}

static void gp_audio_init(void)
{
    // ⛔⛔⛔ L'audio PRIMA del framebuffer ([[feedback_audio_muto_e_ram_interna]]).
    const size_t n = GP_OUT_FRAMES * 2 * sizeof(int16_t);
    aud_out = (int16_t *)gp_calloc_caldo(n);
    audio_ok = (aud_out != nullptr);
    if (!audio_ok) return;
    memset(voci, 0, sizeof(voci));
    rs_prev = rs_cur = 0; rs_frac = 0;
    bang_seq_visto = S->bang_seq;
}

// Task audio sul CORE 0 (il gioco sta sul core 1), come ghouls.cpp.
static void gp_audio_task(void *arg)
{
    (void)arg;
    laserdisc_audio_begin();
    uint32_t respiro = 0;
    while (!g_stop) {
        gp_audio_block();
        // ⛔⛔ RESPIRO OBBLIGATORIO: se il task resta indietro la write non
        // blocca piu' e senza questo l'idle del core 0 non gira -> watchdog.
        if ((++respiro & 15) == 0) vTaskDelay(1);
    }
    // ⛔ senza laserdisc_audio_end() gli altri giochi restano MUTI
    laserdisc_audio_end();
    g_aud_running = false;
    vTaskDelete(nullptr);
}

// Stato all'accensione, come `machine_reset()` + reset dei device di MAME.
static void gp_reset(void)
{
    S->main_irq_mask = S->sub_irq_mask = S->sub2_irq_mask = 0;
    S->subs_reset = 0;                // nessuno tiene la linea di reset all'avvio
    S->snd_enable = 1;                // «start with sound enabled» (namco.cpp)
    S->io_reset   = 0;
    S->bang_pos   = 0xFFFFFFFF;
    S->starfield_framecount = 0;
    gp_io_reset();
    // (su CABINATONE qui c'era m6809_bus_set(&GP_BUS): vedi le callback sopra)
    for (int c = 0; c < 3; c++) { m6809_reset(&S->cpu[c]); S->resto[c] = 0; }
}

// ==================================================================== task
static void gp_task(void *arg)
{
    (void)arg;
    char err[48] = { 0 };
    gp_window(2);

    if (!(sd_is_mounted() ? true : sd_mount())) {
        gp_errore("SD NON MONTATA", "");
        goto uscita;
    }
    gp_caricamento();

    // ⛔⛔ PRIMA DI TUTTO `S`: `fb`, `audio_ok`... sono macro su `S->...`.
    S = (GpStato *)gp_calloc_caldo(sizeof(GpStato));
    if (!S) { gp_errore("RAM ESAURITA", ""); goto uscita; }
    fb_sz = ((size_t)GP_W * GP_H * 2 + 127) & ~(size_t)127;
    vol_ultimo = -2;                  // la barra non e' mai stata disegnata
    S->bang_pos = 0xFFFFFFFF;         // l'esplosione e' ferma

    if (!gp_load_all(err, sizeof(err))) { gp_errore(err, "SERVE /GAPLUS SU SD"); goto uscita; }

    // ⛔⛔⛔ Audio e STACK del suo task PRIMA del framebuffer e delle ROM, che
    // provano la RAM interna ([[feedback_stack_task_prenotato_per_primo]]).
    // La create si CONTROLLA. Finche' gp_reset() non accende il 15XX
    // (`snd_enable`, a 0 dalla calloc) il task produce silenzio.
    gp_audio_init();
    if (audio_ok) {
        g_aud_running = true;
        if (xTaskCreatePinnedToCore(gp_audio_task, "gp_audio", 4096,
                                    nullptr, 6, nullptr, 0) != pdPASS) {
            g_aud_running = false;
            audio_ok = false;
        }
    }
    if (!audio_ok) {
        // rete di sicurezza permanente: due secondi di avviso, poi si gioca muti
        gp_msg("AUDIO KO", "RAM INTERNA FINITA?", C_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    fb = (uint16_t *)gp_allineata_calda(fb_sz);
    if (!fb) { gp_errore("MEMORIA VIDEO ESAURITA", ""); goto uscita; }

    if (!gp_load_rom(err, sizeof(err))) { gp_errore(err, "SERVE /GAPLUS SU SD"); goto uscita; }
    gp_reset();

    gp_bande_nere();
    gp_window(1);

    {
    uint32_t fcount = 0;
    // ⭐⭐ L'ACCUMULATORE del CABINATONE: emulazione a tempo VERO, non «un
    // quadro per disegno» ([[feedback_multi_step_emulation]]). Se il blit SPI
    // rallenta il disegno, il gioco resta alla sua velocita'.
    int64_t emul_acc = 0;
    int64_t t_prec = esp_timer_get_time();

    while (!g_stop) {
        const int64_t t0 = esp_timer_get_time();
        int64_t dt = t0 - t_prec;
        t_prec = t0;
        if (dt > 500000) dt = FRAME_US;
        emul_acc += dt;
        int passi = (int)(emul_acc / FRAME_US);
        emul_acc -= (int64_t)passi * FRAME_US;
        if (passi < 1) passi = 1;
        if (passi > 6) passi = 6;
        for (int p = 0; p < passi; p++) gp_run_frame();

        gp_render();
        gp_blit();
        gp_vol_bar();

        fcount++;
        if (gp_want_exit(g_input->buttons_get())) break;

        const int64_t due = t0 + FRAME_US;
        int64_t left = due - esp_timer_get_time();
        if (left > 2000) vTaskDelay(pdMS_TO_TICKS((left - 1000) / 1000));
        while (esp_timer_get_time() < due) { }
        // ⚠️ se il quadro e' arrivato in ritardo non si dorme MAI: senza questo
        // respiro il loop del menu (che passa il Bluetooth) non gira (ghouls).
        if ((fcount & 7) == 0) vTaskDelay(1);
    }
    }

uscita:
    // ferma il task audio PRIMA di liberare la memoria che sta usando
    g_stop = true;
    for (int i = 0; i < 200 && g_aud_running; i++) vTaskDelay(pdMS_TO_TICKS(10));
    gp_free();
    // pulisci a nero l'intera finestra e torna alla finestra del menu
    gp_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    gp_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- entry
bool gaplus_play(Input *input, unsigned short *out_strip)
{
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;
    g_aud_running = false;

    if (xTaskCreatePinnedToCore(gp_task, "gaplus", GP_STACK,
                                nullptr, 2, nullptr, 1) != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

#else  // !ENABLE_GAPLUS

bool gaplus_play(Input *input, unsigned short *out_strip)
{
    (void)input; (void)out_strip;
    return false;
}

#endif // ENABLE_GAPLUS
