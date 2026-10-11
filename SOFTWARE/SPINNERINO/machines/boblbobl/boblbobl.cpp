// ============================================================================
// boblbobl.cpp — Bubble Bobble (boblbobl bootleg) per SPINNERINO WIFI.
//
// FASE 3: 3x Z80 (main + slave + sound[non attivo]), memory map, banking 0xFB40,
// protezione PAL ic43, rendering object/sprite. Audio = FASE 5.
//
// Port fedele da MAME: taito/bublbobl.cpp (maps, machine_config),
// bublbobl_m.cpp (bankswitch, ic43, nmi, semafori), bublbobl_v.cpp (video).
// ============================================================================

#include "config.h"
#include "machines/boblbobl/boblbobl.h"

#ifdef ENABLE_BOBLBOBL

#include "esp_heap_caps.h"

// ROM/gfx/PROM: definizioni incluse UNA sola volta qui (una copia in flash).
#include "machines/boblbobl/boblbobl_rom_main.h"
#include "machines/boblbobl/boblbobl_rom_sub.h"
#include "machines/boblbobl/boblbobl_rom_audio.h"
#include "machines/boblbobl/boblbobl_prom.h"
#include "machines/boblbobl/boblbobl_gfx_decoded.h"

// --- core audio (FASE 5) ---
// fm.h (YM2203) e' GIA' incluso prima nella stessa TU dal blocco condiviso del
// .ino (#if ARKANOID2||BOBLBOBL) -> NON ri-includerlo qui (il #pragma once non
// deduplica con i path Arduino e darebbe "redefinition of _ssg_callbacks").
#include "machines/boblbobl/snd/fmopl.h"       // YM3526 (OPL: musica)
#include "machines/boblbobl/snd/emu2149.h"     // PSG (SSG dello YM2203)

// Istanza globale per i callback C dei chip audio.
boblbobl *g_boblbobl = nullptr;

// ---- callback fm.c (YM2203) ----
// Timer handler NON-NULL: fm.c NON usa il timer interno (INTERNAL_TIMER scatta
// solo se handler==0) -> lo piloto io in gen_audio, cosi' posso trimmare il TEMPO
// musicale (e' il timer YM2203 a scatenare l'IRQ del tick). Qui catturo il periodo.
static void bb_fm_timer(void *p, int c, int count, int clock) {
    if (!g_boblbobl || clock <= 0) return;
    bool on = (count > 0);
    float period = on ? ((float)count / (float)clock) * 32000.0f : 0.0f;   // in campioni
    if (c == 0) { g_boblbobl->ym_tA_on = on; if (on) g_boblbobl->ym_tA_period = period; }
    else        { g_boblbobl->ym_tB_on = on; if (on) g_boblbobl->ym_tB_period = period; }
}
static void bb_fm_irq(void *p, int irq) { if (g_boblbobl) g_boblbobl->sound_irq_ym2203 = irq; }
// SSG (parte PSG dello YM2203) -> emu2149
static void bb_ssg_setclock(void *p, int clock) { if (g_boblbobl && g_boblbobl->psg_chip) PSG_setClock((PSG*)g_boblbobl->psg_chip, (uint32_t)clock); }
static void bb_ssg_write(void *p, int addr, int data) {
    if (!g_boblbobl || !g_boblbobl->psg_chip) return;
    if (addr == 0) g_boblbobl->ssg_latch = (uint8_t)data;                       // register select
    else PSG_writeReg((PSG*)g_boblbobl->psg_chip, g_boblbobl->ssg_latch, (uint32_t)data);
}
static int  bb_ssg_read(void *p) { return (g_boblbobl && g_boblbobl->psg_chip) ? PSG_readReg((PSG*)g_boblbobl->psg_chip, g_boblbobl->ssg_latch) : 0; }
static void bb_ssg_reset(void *p) { if (g_boblbobl && g_boblbobl->psg_chip) PSG_reset((PSG*)g_boblbobl->psg_chip); }
static const ssg_callbacks bb_ssg = { bb_ssg_setclock, bb_ssg_write, bb_ssg_read, bb_ssg_reset };

// ---- callback fmopl.c (YM3526) ----
static void bb_opl_timer(void *p, int timer, int period) { /* OPL timer non pilotato (tick da YM2203) */ }
static void bb_opl_irq(void *p, int irq) { if (g_boblbobl) g_boblbobl->sound_irq_ym3526 = irq; }

// ============================================================
// Init / Reset
// ============================================================
void boblbobl::init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) {
    machineBase::init(input, framebuffer, spritebuffer, memorybuffer);
    // I buffer di lavoro sono allocati lazy in reset() (solo quando il gioco
    // viene selezionato), come gng/phoenix, per non saturare la SRAM al boot.
}

// ⛔⛔ 2026-08-29 — I ~92 KB DI RAM INTERNA CHE QUESTO GIOCO NON RESTITUIVA.
// Il conto, sulle allocazioni di `reset()`:
//     indexfb  65.536 (256x256)   vram      8.192   share1   6.144
//     ring      BB_RING x2        fmbuf     4.096   oplbuf   4.096
//     sndram    4.096             palram      512   miscram    512
// Terzo allocatore del progetto dopo PHOENIX (264 KB) e ARKANOID 2 (153 KB), e
// non era nella lista dei casi speciali di `start_machine()` — che ne conteneva
// due ([[feedback_costante_del_primo_caso]]).
//
// ⚠️ Si puo' liberare TUTTO perche' `reset()` qui sopra ha ogni allocazione
// dietro un `if (!ptr)`: azzerando i puntatori si ri-alloca da sola al rientro.
// ⚠️ `ring`/`fmbuf`/`oplbuf` li legge il TASK AUDIO sul core 0 (`audioPop`, via
// `Audio::boblbobl_render_buffer`). Liberarli e' sicuro solo dove lo fa
// `return_to_menu()`: la sua prima riga e' `audio.setMute(true)`, e con il mute
// il task audio riempie di zeri senza chiamare i render della machine
// (`audio.cpp`: `if (muted) { memset; continue; }`). Fra il mute e la
// liberazione c'e' anche `emulation_stop()`, che dura piu' di un campo: la
// finestra e' chiusa da entrambe le parti.
void boblbobl::freeBuffers() {
    void *B[] = { vram, share1, palram, miscram, indexfb, sndram,
                  (void*)ring, (void*)fmbuf, (void*)oplbuf };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++)
        if (B[i]) heap_caps_free(B[i]);   // vale anche per malloc/ps_malloc
    vram = share1 = palram = miscram = indexfb = sndram = nullptr;
    ring = nullptr; fmbuf = nullptr; oplbuf = nullptr;
    ring_head = ring_tail = 0;            // il ring riparte vuoto, non a meta'
}

void boblbobl::reset() {
    machineBase::reset();   // Z80_i8080=0, reset cpu[0..2], clear memory/soundregs

    // --- alloc lazy: DRAM interna (hot path), fallback PSRAM ---
    if (!vram)    { vram    = (uint8_t*)heap_caps_malloc(0x2000, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!vram)    vram    = (uint8_t*)ps_malloc(0x2000); }
    if (!share1)  { share1  = (uint8_t*)heap_caps_malloc(0x1800, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!share1)  share1  = (uint8_t*)ps_malloc(0x1800); }
    if (!palram)  palram  = (uint8_t*)malloc(0x200);
    if (!miscram) miscram = (uint8_t*)malloc(0x200);
    if (!indexfb) { indexfb = (uint8_t*)heap_caps_malloc(256*256, MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!indexfb) indexfb = (uint8_t*)ps_malloc(256*256); }

    if (vram)    memset(vram,    0, 0x2000);
    if (share1)  memset(share1,  0, 0x1800);
    if (palram)  memset(palram,  0, 0x200);
    if (miscram) memset(miscram, 0, 0x200);
    if (indexfb) memset(indexfb, 0, 256*256);
    memset(pal_rgb565, 0, sizeof(pal_rgb565));

    ic43_a = 0; ic43_b = 0; prot_rng = 0x12345678;
    flipscreen = 0; video_enable = 0; sub_running = false;

    // machineBase::reset() ha gia' fatto ResetZ80 su tutti; silenzia gli illegal op.
    cpu[0].TrapBadOps = 0;
    cpu[1].TrapBadOps = 0;
    cpu[2].TrapBadOps = 0;
    current_cpu = 0;

    // --- AUDIO (FASE 5): alloc lazy + init chip ---
    if (!sndram) sndram = (uint8_t*)malloc(0x1000);
    if (!ring)   { ring   = (int16_t*)heap_caps_malloc(BB_RING*sizeof(int16_t), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!ring) ring = (int16_t*)ps_malloc(BB_RING*sizeof(int16_t)); }
    if (!fmbuf)  { fmbuf  = (int32_t*)heap_caps_malloc(1024*sizeof(int32_t), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!fmbuf) fmbuf = (int32_t*)ps_malloc(1024*sizeof(int32_t)); }
    if (!oplbuf) { oplbuf = (int32_t*)heap_caps_malloc(1024*sizeof(int32_t), MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT); if (!oplbuf) oplbuf = (int32_t*)ps_malloc(1024*sizeof(int32_t)); }
    if (sndram) memset(sndram, 0, 0x1000);
    if (ring)   memset(ring, 0, BB_RING*sizeof(int16_t));
    ring_head = ring_tail = 0; snd_accum = 0; bb_last_audio_us = micros();
    sound_reset = false;                       // sound CPU gira dal boot (latch 0xFA03=0)
    main_to_sound = sound_to_main = 0;
    main_to_sound_pending = sound_to_main_pending = false;
    sound_nmi_enabled = false; sound_nmi_prev = false;
    sound_irq_ym2203 = sound_irq_ym3526 = 0; ssg_latch = 0;
    opl_reg = 0; opl_t1_val = opl_t2_val = 0; opl_t1_on = opl_t2_on = false;
    opl_t1_acc = opl_t2_acc = opl_t1_reload = opl_t2_reload = 0;
    ym_tA_on = ym_tB_on = false; ym_tA_period = ym_tB_period = ym_tA_acc = ym_tB_acc = 0;
    sound_init_chips();

    // Come il /RESET hardware: il latch 0xFB40 va a 0 -> banco entry (0^4)&7=4,
    // slave in RESET (bit4=0), video off (bit6=0).
    bankswitch_w(0x00);
    game_started = 0;
}

// ============================================================
// Audio: init chip (YM2203 fm.c + YM3526 OPL + PSG SSG)
// ============================================================
void boblbobl::sound_init_chips() {
    g_boblbobl = this;
    if (!psg_chip)    psg_chip = PSG_new(1500000, 32000);   // SSG: clock YM2203/2 (set_clock lo aggiusta)
    if (psg_chip)     PSG_reset((PSG*)psg_chip);
    // TimerHandler = bb_fm_timer (non-NULL): fm.c NON usa il timer interno, lo
    // piloto io in gen_audio (per poter trimmare il tempo musicale, BB_MUSIC_TEMPO).
    if (!ym2203_chip) ym2203_chip = ym2203_init(this, 3000000, 32000, bb_fm_timer, bb_fm_irq, &bb_ssg);
    if (ym2203_chip)  ym2203_reset_chip(ym2203_chip);
    if (!ym3526_chip) ym3526_chip = ym3526_init(3000000, 32000);
    if (ym3526_chip) {
        ym3526_set_irq_handler(ym3526_chip, bb_opl_irq, this);
        ym3526_set_timer_handler(ym3526_chip, bb_opl_timer, this);
        ym3526_reset_chip(ym3526_chip);
    }
}

// ============================================================
// Banking 0xFB40  (bublbobl_m.cpp::bublbobl_bankswitch_w)
//   bit0-2: ROM bank  set_entry((data^4)&7)
//   bit4  : /SBRES slave Z80 reset (0=reset, 1=run)
//   bit6  : /BLACK video enable
//   bit7  : VHINV flip screen
// ============================================================
void boblbobl::bankswitch_w(uint8_t data) {
    rom_bank_ptr = boblbobl_rom_main + 0x10000 + ((data ^ 4) & 7) * 0x4000;

    bool run = (data & 0x10);
    if (run && !sub_running) { ResetZ80(&cpu[1]); cpu[1].TrapBadOps = 0; }  // rilascio /RESET: slave riparte
    sub_running = run;

    video_enable = data & 0x40;
    flipscreen   = data & 0x80;
}

// ============================================================
// Protezione PAL ic43 (bublbobl_m.cpp, port verbatim)
// ============================================================
uint8_t boblbobl::ic43_a_r(uint8_t offset) {
    if (offset == 0) return ic43_a << 4;
    // machine().rand(): xorshift32
    prot_rng ^= prot_rng << 13; prot_rng ^= prot_rng >> 17; prot_rng ^= prot_rng << 5;
    return prot_rng & 0xff;
}

void boblbobl::ic43_a_w(uint8_t offset) {
    int res = 0;
    switch (offset) {
        case 0:
            if (~ic43_a & 8) res ^= 1;
            if (~ic43_a & 1) res ^= 2;
            if (~ic43_a & 1) res ^= 4;
            if (~ic43_a & 2) res ^= 4;
            if (~ic43_a & 4) res ^= 8;
            break;
        case 1:
            if (~ic43_a & 8) res ^= 1;
            if (~ic43_a & 2) res ^= 1;
            if (~ic43_a & 8) res ^= 2;
            if (~ic43_a & 1) res ^= 4;
            if (~ic43_a & 4) res ^= 8;
            break;
        case 2:
            if (~ic43_a & 4) res ^= 1;
            if (~ic43_a & 8) res ^= 2;
            if (~ic43_a & 2) res ^= 4;
            if (~ic43_a & 1) res ^= 8;
            if (~ic43_a & 4) res ^= 8;
            break;
        case 3:
            if (~ic43_a & 2) res ^= 1;
            if (~ic43_a & 4) res ^= 2;
            if (~ic43_a & 8) res ^= 2;
            if (~ic43_a & 8) res ^= 4;
            if (~ic43_a & 1) res ^= 8;
            break;
    }
    ic43_a = res;
}

uint8_t boblbobl::ic43_b_r(uint8_t offset) {
    if (offset == 0) return ic43_b << 4;
    return 0xff;
}

void boblbobl::ic43_b_w(uint8_t offset, uint8_t data) {
    static const int xorval[4] = { 4, 1, 8, 2 };
    ic43_b = (data >> 4) ^ xorval[offset & 3];
}

// ============================================================
// Input IN0 (0xFF02). MAME boblbobl:
//   bit0 P1 LEFT (AL), bit1 P1 RIGHT (AL), bit2 COIN2 (AH), bit3 COIN1 (AH),
//   bit4 BUTTON2=bolla (AL), bit5 BUTTON1=salto (AL), bit6 START1 (AL), bit7 -
// ============================================================
uint8_t boblbobl::read_in0() {
    unsigned char k = input ? input->buttons_get() : 0;
    uint8_t r = 0xF3;                         // attivi-bassi a 1; coin (AH) a 0
    if (k & BUTTON_LEFT)  r &= ~0x01;
    if (k & BUTTON_RIGHT) r &= ~0x02;
    if (k & BUTTON_COIN)  r |=  0x08;         // COIN1 active-high
    if (k & BUTTON_FIRE)  r &= ~0x10;         // BUTTON2 = spara bolla
    if ((k & BUTTON_EXTRA) || (k & BUTTON_UP)) r &= ~0x20;  // BUTTON1 = salto
    if (k & BUTTON_START) r &= ~0x40;
    return r;
}

// ============================================================
// Palette  (RGBx_444 big-endian: byte0=RRRRGGGG, byte1=BBBBxxxx)
// ============================================================
void boblbobl::update_palette(int idx) {
    uint8_t hi = palram[idx*2];
    uint8_t lo = palram[idx*2 + 1];
    uint8_t r8 = ((hi >> 4) & 0x0f) * 17;
    uint8_t g8 = (hi & 0x0f) * 17;
    uint8_t b8 = ((lo >> 4) & 0x0f) * 17;
    uint16_t rgb = ((r8 & 0xf8) << 8) | ((g8 & 0xfc) << 3) | (b8 >> 3);
    pal_rgb565[idx] = (rgb >> 8) | (rgb << 8);   // byte-swap (ESP32 SPI BE)
}

// ============================================================
// Z80 memory  (current_cpu: 0=main, 1=slave/video)
// ============================================================
unsigned char boblbobl::opZ80(unsigned short addr) {
    if (current_cpu == 2) {                                   // SOUND
        if (addr < 0x8000) return boblbobl_rom_audio[addr];
        return rdZ80(addr);
    }
    if (current_cpu == 1) {                                   // SLAVE
        if (addr < 0x8000) return boblbobl_rom_sub[addr];
        return rdZ80(addr);
    }
    if (addr < 0x8000) return boblbobl_rom_main[addr];
    if (addr < 0xc000) return rom_bank_ptr[addr - 0x8000];
    return rdZ80(addr);
}

unsigned char boblbobl::rdZ80(unsigned short addr) {
    if (current_cpu == 2) {                                  // SOUND CPU (sound_map)
        if (addr < 0x8000) return boblbobl_rom_audio[addr];
        if (addr < 0x9000) return sndram[addr & 0x0fff];                                  // 0x8000-0x8fff RAM
        if (addr < 0xa000) return ym2203_chip ? ym2203_read(ym2203_chip, addr & 1) : 0xff; // 0x9000 YM2203
        if (addr < 0xb000) return ym3526_chip ? ym3526_read(ym3526_chip, addr & 1) : 0xff; // 0xa000 YM3526
        if (addr < 0xc000) {                                                              // 0xb000 latch/semafori
            switch (addr & 3) {
                case 0:  main_to_sound_pending = false; return main_to_sound;             // read latch + ack
                case 1:  return 0xfc | (main_to_sound_pending ? 2 : 0) | (sound_to_main_pending ? 1 : 0);
                default: return 0xff;
            }
        }
        return 0xff;                                                                       // 0xe000+ no diag rom
    }
    if (current_cpu == 1) {                                  // SLAVE
        if (addr < 0x8000) return boblbobl_rom_sub[addr];
        if (addr >= 0xe000 && addr <= 0xf7ff) return share1[addr - 0xe000];
        return 0xff;
    }
    // MAIN
    if (addr < 0x8000) return boblbobl_rom_main[addr];
    if (addr < 0xc000) return rom_bank_ptr[addr - 0x8000];
    if (addr < 0xe000) return vram[addr - 0xc000];           // 0xc000-0xdfff (videoram+objectram)
    if (addr < 0xf800) return share1[addr - 0xe000];         // 0xe000-0xf7ff
    if (addr < 0xfa00) return palram[addr - 0xf800];         // 0xf800-0xf9ff
    if (addr < 0xfa80) {                                     // 0xfa00-0xfa7f
        switch (addr & 3) {
            case 0:  sound_to_main_pending = false; return sound_to_main;   // sound->main latch + ack
            case 1:  return 0xfc | (main_to_sound_pending ? 2 : 0) | (sound_to_main_pending ? 1 : 0); // semafori
            default: return 0xff;
        }
    }
    if (addr < 0xfc00) return 0xff;                          // wdog/nmi/bank = write-only
    if (addr < 0xfe00) return miscram[addr - 0xfc00];        // 0xfc00-0xfdff
    if (addr < 0xfe80) return ic43_a_r(addr & 3);            // 0xfe00-0xfe03
    if (addr < 0xff00) return ic43_b_r(addr & 3);            // 0xfe80-0xfe83
    switch (addr & 3) {                                      // 0xff00-0xff03
        case 0:  return dsw0;
        case 1:  return dsw1;
        case 2:  return read_in0();
        default: return 0xff;                               // IN1 (P2/service/tilt) inattivo
    }
}

void boblbobl::wrZ80(unsigned short addr, unsigned char val) {
    if (current_cpu == 2) {                                  // SOUND CPU (sound_map)
        if (addr >= 0x8000 && addr < 0x9000) { sndram[addr & 0x0fff] = val; return; }
        if (addr >= 0x9000 && addr < 0xa000) { if (ym2203_chip) ym2203_write(ym2203_chip, addr & 1, val); return; }
        if (addr >= 0xa000 && addr < 0xb000) {
            if (ym3526_chip) ym3526_write(ym3526_chip, addr & 1, val);
            if ((addr & 1) == 0) {
                opl_reg = val;                                  // address latch OPL
            } else {
                if (opl_reg == 0x02) opl_t1_val = val;          // timer1 count
                else if (opl_reg == 0x03) opl_t2_val = val;     // timer2 count
                else if (opl_reg == 0x04 && !(val & 0x80)) {    // control (bit7=IRQ reset: ignora)
                    // bit0/1 = start timer1/2. NB: il driver scrive 0x04=0x00 come
                    // parte dell'ACK del flag (dopo 0x80) — NON e' uno stop reale per
                    // la musica continua. Quindi una volta avviato lascio correre il
                    // timer (lo riavvio su bit=1, NON lo fermo su bit=0).
                    if (val & 0x01) { if (!opl_t1_on) { opl_t1_reload = (256 - opl_t1_val) * 4.0f;  opl_t1_acc = 0; } opl_t1_on = true; }
                    if (val & 0x02) { if (!opl_t2_on) { opl_t2_reload = (256 - opl_t2_val) * 16.0f; opl_t2_acc = 0; } opl_t2_on = true; }
                }
            }
            return;
        }
        if (addr >= 0xb000 && addr < 0xc000) {
            switch (addr & 3) {
                case 0: sound_to_main = val; sound_to_main_pending = true; break;  // sound->main latch
                case 1: sound_nmi_enabled = true;  break;                          // enable sound NMI (in_set)
                case 2: sound_nmi_enabled = false; break;                          // disable sound NMI (in_clear)
            }
            return;
        }
        return;
    }
    if (current_cpu == 1) {                                  // SLAVE
        if (addr >= 0xe000 && addr <= 0xf7ff) share1[addr - 0xe000] = val;
        return;
    }
    // MAIN
    if (addr < 0xc000) return;                               // ROM / banked ROM
    if (addr < 0xe000) { vram[addr - 0xc000] = val; return; }
    if (addr < 0xf800) { share1[addr - 0xe000] = val; return; }
    if (addr < 0xfa00) {                                     // palette 0xf800-0xf9ff
        int off = addr - 0xf800; palram[off] = val; update_palette(off >> 1); return;
    }
    if (addr < 0xfa80) {                                     // 0xfa00-0xfa7f
        switch (addr & 3) {
            case 0:  // main->sound latch: solo set pending. La NMI e' consegnata sul
                     // fronte di salita di (nmi_enabled && pending) in run_frame
                     // (come l'input_merger ALL_HIGH di MAME) -> copre anche il caso
                     // "main scrive mentre NMI disabilitata, sound riabilita dopo".
                main_to_sound = val; main_to_sound_pending = true;
                break;
            case 3:  // sound CPU reset (0xfa03): val!=0 = assert (hold), 0 = release
                if (val) {
                    if (!sound_reset) {
                        ResetZ80(&cpu[2]); cpu[2].TrapBadOps = 0;
                        if (ym2203_chip) ym2203_reset_chip(ym2203_chip);
                        if (ym3526_chip) ym3526_reset_chip(ym3526_chip);
                        if (psg_chip)    PSG_reset((PSG*)psg_chip);
                        sound_irq_ym2203 = sound_irq_ym3526 = 0;
                    }
                    sound_reset = true;
                } else {
                    sound_reset = false;     // release: cpu[2] gia' a PC=0, riparte
                }
                break;
        }
        return;
    }
    if (addr < 0xfb00) return;                               // watchdog 0xfa80 (nop)
    if (addr < 0xfb40) {                                     // 0xfb00-0xfb3f nmitrigger -> slave NMI
        if (sub_running) { char s = current_cpu; current_cpu = 1; IntZ80(&cpu[1], INT_NMI); current_cpu = s; }
        return;
    }
    if (addr < 0xfb80) { bankswitch_w(val); return; }        // 0xfb40-0xfb7f
    if (addr < 0xfc00) return;
    if (addr < 0xfe00) { miscram[addr - 0xfc00] = val; return; }  // 0xfc00-0xfdff
    if (addr < 0xfe80) { ic43_a_w(addr & 3); return; }           // 0xfe00-0xfe03
    if (addr < 0xff00) { ic43_b_w(addr & 3, val); return; }      // 0xfe80-0xfe83
    // 0xff00+ : input (read-only) / nopw -> ignora
}

unsigned char boblbobl::inZ80(unsigned short Port) { return 0xff; }
void          boblbobl::outZ80(unsigned short Port, unsigned char Value) { }

// ============================================================
// Frame: interleave main+slave, IRQ0 vblank a entrambe (irq0_line_hold)
// ============================================================
void boblbobl::run_frame(void) {
    (void)input->buttons_get();   // long-press safety (uscita al menu)
    g_boblbobl = this;            // i callback C dei chip usano questo puntatore

    // Interleave FINE: il sound CPU polla in tempo reale i flag dei timer dei chip
    // (OPL 0xA000.bit7 + YM2203 0x9000.bit0-1). Avanzando i chip a piccoli passi
    // PRIMA dello stepping del sound, l'ISR vede flag aggiornati (sincronizzazione).
    const int INTERLEAVE = 16;
    const int main_per = BB_MAIN_ITER_PER_FRAME / INTERLEAVE;
    const int sub_per  = BB_SUB_ITER_PER_FRAME  / INTERLEAVE;
    const int snd_per  = BB_SND_ITER_PER_FRAME  / INTERLEAVE;
    const int vblank_slice = INTERLEAVE * 6 / 8;   // ~6/8 del frame

    // Campioni audio per il TEMPO REALE trascorso (32000/s): tempo musicale
    // corretto e ring bilanciato a prescindere dal rate di run_frame (che con il
    // carico FM non e' esattamente 60 Hz). 32000/1e6 = 32 campioni per ms (in /1000).
    uint32_t now = micros();
    uint32_t elapsed = now - bb_last_audio_us;
    bb_last_audio_us = now;
    if (elapsed > 32000) elapsed = 32000;          // clamp (primo frame / hiccup) -> max 1024 campioni
    snd_accum += elapsed * 32;
    int frame_samples = (int)(snd_accum / 1000);
    snd_accum %= 1000;

    for (int slice = 0; slice < INTERLEAVE; slice++) {
        current_cpu = 0;
        for (int i = 0; i < main_per; i++) {
            StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]); StepZ80(&cpu[0]);
        }
        if (sub_running) {
            current_cpu = 1;
            for (int i = 0; i < sub_per; i++) {
                StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]); StepZ80(&cpu[1]);
            }
        }

        // 1) Avanza i chip (timer + flag di status) per la quota di campioni di
        //    questa slice, PRIMA di far girare il sound CPU.
        int s0 = (int)((long)frame_samples * slice       / INTERLEAVE);
        int s1 = (int)((long)frame_samples * (slice + 1)  / INTERLEAVE);
        if (s1 > s0) gen_audio(s1 - s0);

        if (!sound_reset) {
            current_cpu = 2;
            // 2) NMI (comandi dal main) sul fronte di salita di (nmi_enabled && pending)
            bool nmi_line = sound_nmi_enabled && main_to_sound_pending;
            if (nmi_line && !sound_nmi_prev) IntZ80(&cpu[2], INT_NMI);
            sound_nmi_prev = nmi_line;
            // 3+4) step sound + consegna IRQ del timer (YM2203/OPL). StepZ80 NON
            //   onora cpu[2].IRequest, quindi controllo la linea IRQ DOPO OGNI gruppo
            //   di 4 step: cosi' catturo la breve finestra EI del main loop (l'IRQ
            //   viene preso appena il sound CPU riabilita gli interrupt).
            for (int i = 0; i < snd_per; i++) {
                StepZ80(&cpu[2]); StepZ80(&cpu[2]); StepZ80(&cpu[2]); StepZ80(&cpu[2]);
                if ((sound_irq_ym2203 || sound_irq_ym3526) && (cpu[2].IFF & IFF_1))
                    IntZ80(&cpu[2], INT_IRQ);
            }
        }

        // VBLANK rising-edge: IRQ0 a main e slave (irq0_line_hold)
        if (slice == vblank_slice) {
            current_cpu = 0;
            if (cpu[0].IFF & IFF_1) IntZ80(&cpu[0], INT_IRQ);
            if (sub_running) {
                current_cpu = 1;
                if (cpu[1].IFF & IFF_1) IntZ80(&cpu[1], INT_IRQ);
            }
        }
    }
    current_cpu = 0;
    if (!game_started) game_started = 1;
}

// ============================================================
// Audio: generazione (core emulazione) -> ring buffer
//   Mix MAME: YM2203 0.25, YM3526 0.50, SSG ~0.25. Pesi/gain DA CALIBRARE A
//   ORECCHIO (dipendono dai range relativi dei 3 core + dal codec ES8311).
// ============================================================
void boblbobl::gen_audio(int n) {
    if (n <= 0 || !ring) return;
    if (n > 1024) n = 1024;                              // sicurezza (size fmbuf/oplbuf)
    FMSAMPLE  *fmb[2]  = { (FMSAMPLE*)fmbuf,  (FMSAMPLE*)fmbuf  };
    OPLSAMPLE *oplb[2] = { (OPLSAMPLE*)oplbuf, (OPLSAMPLE*)oplbuf };
    if (ym2203_chip) ym2203_update_one(ym2203_chip, fmb,  n); else memset(fmbuf,  0, n*sizeof(int32_t));
    if (ym3526_chip) ym3526_update_one(ym3526_chip, oplb, n); else memset(oplbuf, 0, n*sizeof(int32_t));

    // Pilota i timer OPL (fmopl.c VGMPlay non li auto-temporizza): ad ogni overflow
    // chiama ym3526_timer_over -> setta lo status (bit7) -> sblocca il poll del
    // sound CPU (tick musicale). rate = (clock/72)/sample_rate TimerBase units/campione.
    const float OPL_TB_PER_SAMPLE = (3000000.0f / 72.0f) / 32000.0f / BB_MUSIC_TEMPO;  // /trim = piu' lento
    if (ym3526_chip && opl_t1_on && opl_t1_reload > 0) {
        opl_t1_acc += n * OPL_TB_PER_SAMPLE;
        while (opl_t1_acc >= opl_t1_reload) { opl_t1_acc -= opl_t1_reload; ym3526_timer_over(ym3526_chip, 0); }
    }
    if (ym3526_chip && opl_t2_on && opl_t2_reload > 0) {
        opl_t2_acc += n * OPL_TB_PER_SAMPLE;
        while (opl_t2_acc >= opl_t2_reload) { opl_t2_acc -= opl_t2_reload; ym3526_timer_over(ym3526_chip, 1); }
    }

    // Timer YM2203 (TICK MUSICALE) pilotati a mano, con trim BB_MUSIC_TEMPO (>1 = piu' lento).
    if (ym2203_chip && ym_tA_on && ym_tA_period > 0) {
        ym_tA_acc += n;
        float pa = ym_tA_period * BB_MUSIC_TEMPO;
        while (ym_tA_acc >= pa) { ym_tA_acc -= pa; ym2203_timer_over(ym2203_chip, 0); }
    }
    if (ym2203_chip && ym_tB_on && ym_tB_period > 0) {
        ym_tB_acc += n;
        float pb = ym_tB_period * BB_MUSIC_TEMPO;
        while (ym_tB_acc >= pb) { ym_tB_acc -= pb; ym2203_timer_over(ym2203_chip, 1); }
    }

    PSG *psg = (PSG*)psg_chip;
    uint32_t tail = ring_tail;
    uint32_t head = ring_head;
    for (int i = 0; i < n; i++) {
        int ssg = psg ? PSG_calc(psg) : 0;
        // Pesi relativi iniziali (tarare): OPL doppio del FM (route .50 vs .25).
        int32_t s = (fmbuf[i] >> 1) + oplbuf[i] + (ssg << 1);
        s >>= 1;                                          // gain master (tarare)
        if (s >  32767) s =  32767;
        if (s < -32768) s = -32768;
        uint32_t nt = (tail + 1) & (BB_RING - 1);
        if (nt == head) break;                            // ring pieno: evita overrun
        ring[tail] = (int16_t)s;
        tail = nt;
    }
    ring_tail = tail;
}

// Estrazione dal ring (chiamato dal task audio, core 0). SPSC lock-free.
int boblbobl::audioPop(int16_t *out, int n) {
    uint32_t head = ring_head;
    uint32_t tail = ring_tail;
    int got = 0;
    while (got < n && head != tail) {
        out[got++] = ring[head];
        head = (head + 1) & (BB_RING - 1);
    }
    ring_head = head;
    return got;
}

// ============================================================
// Rendering object/sprite  (bublbobl_v.cpp::screen_update_bublbobl)
//   videoram+objectram = vram[0x2000]. objectram @ vram+0x1d00.
//   Composizione completa in prepare_frame() -> indexfb (256x256, indici palette).
// ============================================================
void boblbobl::draw_tile(int code, int color, int flipx, int flipy, int x, int y) {
    const uint8_t *tile = boblbobl_gfx_decoded + (code & 0x3fff) * 64;
    int cbase = color * 16;
    for (int ty = 0; ty < 8; ty++) {
        int py = y + ty;
        if ((unsigned)py >= 256) continue;
        int sty = flipy ? (7 - ty) : ty;
        const uint8_t *trow = tile + sty * 8;
        uint8_t *drow = indexfb + py * 256;
        for (int tx = 0; tx < 8; tx++) {
            int px = x + tx;
            if ((unsigned)px >= 256) continue;
            uint8_t pixel = trow[flipx ? (7 - tx) : tx];
            if (pixel == 15) continue;          // pen 15 = trasparente
            drow[px] = cbase + pixel;
        }
    }
}

void boblbobl::prepare_frame(void) {
    if (!indexfb) return;
    memset(indexfb, 255, 256*256);              // bitmap.fill(255) = pen di sfondo
    if (!video_enable) return;

    const uint8_t *prom      = boblbobl_prom;
    const uint8_t *objectram = vram + 0x1d00;   // 0xdd00-0xdfff
    int sx = 0;
    for (int offs = 0; offs < 0x300; offs += 4) {
        // skip sprite vuoti (dword == 0)
        if (objectram[offs] == 0 && objectram[offs+1] == 0 &&
            objectram[offs+2] == 0 && objectram[offs+3] == 0) continue;

        int gfx_num  = objectram[offs + 1];
        int gfx_attr = objectram[offs + 3];
        const uint8_t *prom_line = prom + 0x80 + ((gfx_num & 0xe0) >> 1);

        int gfx_offs = (gfx_num & 0x1f) * 0x80;
        if ((gfx_num & 0xa0) == 0xa0) gfx_offs |= 0x1000;

        int sy = -(int)objectram[offs + 0];

        for (int yc = 0; yc < 32; yc++) {
            if (prom_line[yc/2] & 0x08) continue;           // NEXT (riga saltata)
            if (!(prom_line[yc/2] & 0x04)) {                // nuova colonna
                sx = objectram[offs + 2];
                if (gfx_attr & 0x40) sx -= 256;
            }
            for (int xc = 0; xc < 2; xc++) {
                int goffs = gfx_offs + xc*0x40 + (yc & 7)*0x02 + (prom_line[yc/2] & 0x03)*0x10;
                int code  = vram[goffs] + 256*(vram[goffs+1] & 0x03) + 1024*(gfx_attr & 0x0f);
                int color = (vram[goffs+1] & 0x3c) >> 2;
                int flipx = vram[goffs+1] & 0x40;
                int flipy = vram[goffs+1] & 0x80;
                int x = sx + xc*8;
                int y = (sy + yc*8) & 0xff;
                if (flipscreen) { x = 248 - x; y = 248 - y; flipx = !flipx; flipy = !flipy; }
                draw_tile(code, color, flipx, flipy, x, y);
            }
        }
        sx += 16;
    }
}

void boblbobl::render_row(short row) {
    // Pannello portrait, 36 righe da 8px. Gioco visibile y 16..239 (224px) reso
    // ai pannello-row 2..29 (1:1, come GnG). X gioco 8..247 -> 0..239 (crop 16px).
    if (!indexfb) return;
    if (row < 2 || row > 29) return;            // fuori area gioco: resta nero
    // Scala orizzontale 256 -> 240 (nearest, accumulatore 16.16): mostra TUTTA la
    // larghezza nativa del playfield (niente crop dei bordi), con un lieve
    // schiacciamento ~6% (pannello largo solo 240). Verticale 1:1 (224px).
    const uint32_t xstep = (256u << 16) / 240u;   // ~1.0667 px sorgente per px output
    for (int dy = 0; dy < 8; dy++) {
        int gy = row*8 + dy;                    // 16..239
        const uint8_t *src = indexfb + gy*256;
        unsigned short *dst = frame_buffer + dy*240;
        uint32_t xacc = 0;
        for (int sx = 0; sx < 240; sx++) {
            dst[sx] = pal_rgb565[src[xacc >> 16]];
            xacc += xstep;
        }
    }
}

#endif  // ENABLE_BOBLBOBL
