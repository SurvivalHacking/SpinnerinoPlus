// ============================================================================
// spaceace.cpp — SPACE ACE (ROM-driven Z80 + LD-V1000 + AY-3-8910) per SPINNERINO.
//
// Porta il motore di CABINATONE\ace_hw.cpp sull'architettura del player DL dello
// SPINNERINO (laserdisc.cpp): video JPEGDEC SOFTWARE 176x240 pre-ruotati, UNICO
// scrittore SPI nel task del player su CPU1, audio in task su CPU0 via il ponte
// laserdisc_audio_*. Il CERVELLO (ROM Z80 che pilota il LD-V1000) e' preso as-is;
// la COLLA video/audio/overlay e' quella di DL.
//
// ⚠️ Z80 SEPARATO: lo SPINNERINO ha gia' il suo Z80 (paginato, in SPINNERINO.ino).
//   Qui usiamo un SECONDO core Z80 (cpus/z80ace/) coi simboli RINOMINATI (Ace*),
//   incluso IN QUESTA unita' di compilazione (spaceace.cpp = .cpp root = TU a se').
//   Nessuna collisione: i .cpp root sono auto-compilati come TU separate.
// ============================================================================
#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "esp_heap_caps.h"
#include "esp_system.h"        // esp_reset_reason(): perche' si e' riavviato
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/laserdisc.h"    // spaceace_play() dichiarata qui accanto a laserdisc_play()
#include "emulation/sd_card.h"
#include "emulation/video.h"

#include <JPEGDEC.h>
#include "ace_rom.h"

// ---- Z80 di ace, RINOMINATO per non collidere col Z80 dello SPINNERINO ----
#define ExecZ80    AceExecZ80
#define RunZ80     AceRunZ80
#define ResetZ80   AceResetZ80
#define IntZ80     AceIntZ80
#define StepZ80    AceStepZ80
#define JumpZ80    AceJumpZ80
#define RdZ80      AceRdZ80
#define WrZ80      AceWrZ80
#define InZ80      AceInZ80
#define OutZ80     AceOutZ80
#define LoopZ80    AceLoopZ80
#define PatchZ80   AcePatchZ80
#define DebugZ80   AceDebugZ80
#define OpZ80_INL  AceOpZ80_INL
extern "C" {
#include "cpus/z80ace/Z80.h"
}

extern Video video;
extern bool  input_consume_back_to_menu();
extern bool  input_is_vol_mode_active(void);
extern void  laserdisc_audio_begin(void);
extern void  laserdisc_audio_end(void);
extern void  laserdisc_audio_write(const int16_t *stereo, int frames);
extern int   laserdisc_current_volume(void);       // audio.volumeSetting (1..30)

// ---------------------------------------------------------------- parametri video (= DL)
#define LD_W          176
#define LD_H          240
#define LD_X_OFF      72
#define LD_FB_H       240
#define LD_IN_MAX     (48*1024)
#define LD_TASK_STACK 32768

#define ACE_FASTBOOT  1            // salta la diagnosi ROM (~31s)
#define ACE_ONE_COIN  1            // patch: 1 moneta = 1 credito
#define ACE_GAIN_SFX  16           // 16 ultimo volume effetti AY coin/start/mosse (x/8) — abbassato da 14 (leggermente piu' bassi)
#define ACE_GAIN_SCENE 10          // 12 ultimo musica/voce scena (x/8) — alzato da 5: >8 = boost >1x (i .pcm sono registrati bassi)

// ---------------------------------------------------------------- globali video/audio (= DL)
static uint16_t *g_strip = nullptr;
static uint8_t  *g_in    = nullptr;    // bitstream JPEG (PSRAM)
static uint8_t  *g_out   = nullptr;    // frame 176x240 RGB565 (PSRAM)
static int16_t  *audmix  = nullptr;    // buffer mix audio (RAM interna)
static JPEGDEC   g_jdec;
static Input    *g_input = nullptr;

static volatile bool g_running = false;   // task audio attivo
static volatile bool g_stop    = false;   // loopTask -> player: esci
static volatile bool g_done    = false;   // player -> loopTask: finito

// ============================================================ stato Z80 / ROM
extern "C" uint8_t *ace_z80mem;
uint8_t            *ace_z80mem = nullptr;
static Z80          cpu;

// ============================================================ LD-V1000
static uint8_t  ldv_out   = 0xFC;
static uint8_t  ldv_stack[16];
static int      ldv_sp    = 0;
static char     ldv_digits[8];
static volatile int ldv_seek = 0;
static volatile int ldv_cur  = 0;      // frame disco (cross-core: ROM scrive, video legge)
static int      ldv_acc   = 0;
static int      ldv_spd_n = 1, ldv_spd_d = 1;
static int      ldv_autostop = 0;
static volatile bool ldv_play = false;

// dip / input (ACTIVE LOW). Ricavati dal ROM disassemblato (vedi CABINATONE\ace_hw.cpp).
static uint8_t sw_a     = 0x21;        // bit0=1 coinage + bit1=0 -> 5 vite
static uint8_t sw_b     = 0xFE;        // bit5=1 -> vite calano (NORMALE)
static uint8_t snd_latch= 0;
static uint8_t joyskill = 0xFF;        // C008 (active-low): UP0/DOWN1/LEFT2/RIGHT3/FIRE4 + skill5/6/7
static uint8_t g_ace_skill = 0x40;     // Captain0x40 / Cadet0x20 / Ace0x80
static volatile uint8_t misc_val    = 0x3F;   // C010: start0, coin2 + strobe6/7
static volatile uint8_t misc_inputs = 0x3F;

// scoreboard catturato dai LED E030-E03F
static volatile uint8_t sb_cred_t = 0, sb_cred_u = 0, sb_lives = 0;
static char sb_score[8] = "000000";

// bit gamepad SPINNERINO (BUTTON_* da emulation/input.h)
#define ABT_UP    BUTTON_UP
#define ABT_DOWN  BUTTON_DOWN
#define ABT_LEFT  BUTTON_LEFT
#define ABT_RIGHT BUTTON_RIGHT
#define ABT_FIRE  BUTTON_FIRE
#define ABT_START BUTTON_START
#define ABT_COIN  BUTTON_COIN

static int ldv_digit_of(uint8_t v) {
    switch (v) {
        case 0x3F: return 0; case 0x0F: return 1; case 0x8F: return 2; case 0x4F: return 3;
        case 0x2F: return 4; case 0xAF: return 5; case 0x6F: return 6; case 0x1F: return 7;
        case 0x9F: return 8; case 0x5F: return 9;
    }
    return -1;
}
static void ldv_add_digit(int d) {
    for (int i = 0; i < 4; i++) ldv_digits[i] = ldv_digits[i + 1];
    ldv_digits[4] = (char)('0' + d); ldv_digits[5] = 0;
}
static void ldv_clear(void) { memset(ldv_digits, 0, sizeof(ldv_digits)); }
static void ldv_push(uint8_t v) { if (ldv_sp < 15) ldv_stack[ldv_sp++] = v; }
static uint8_t ldv_read(void) { if (ldv_sp > 0) return ldv_stack[--ldv_sp]; return ldv_out; }

static void ldv_write(uint8_t v) {
    if (ldv_out & 0x80) {
        ldv_out &= 0x7F;
        int d = ldv_digit_of(v);
        if (d >= 0) { ldv_add_digit(d); return; }
        switch (v) {
            case 0xF7: ldv_seek = atoi(ldv_digits); ldv_cur = ldv_seek; ldv_play = false;
                       ldv_acc = 0; ldv_spd_n = 1; ldv_spd_d = 1; ldv_autostop = 0;
                       ldv_out = 0xD0; ldv_clear(); break;
            case 0xFD: ldv_play = true; ldv_spd_n = 1; ldv_spd_d = 1; ldv_out = 0x64; break;
            case 0xF3: ldv_autostop = atoi(ldv_digits); ldv_clear(); ldv_play = true;
                       ldv_spd_n = 1; ldv_spd_d = 1; ldv_out = 0x54; break;
            case 0xA3: if (!ldv_play) { ldv_play = true; ldv_out = 0x2E; } ldv_spd_n = 1; ldv_spd_d = 1; break;
            case 0xA1: ldv_spd_n = 1; ldv_spd_d = 4; break;
            case 0xA2: ldv_spd_n = 1; ldv_spd_d = 2; break;
            case 0xA4: ldv_spd_n = 2; ldv_spd_d = 1; break;
            case 0xA5: ldv_spd_n = 3; ldv_spd_d = 1; break;
            case 0xA6: ldv_spd_n = 4; ldv_spd_d = 1; break;
            case 0xA7: ldv_spd_n = 5; ldv_spd_d = 1; break;
            case 0xF9: ldv_out = 0x7C; break;
            case 0xA0: case 0xFB: case 0xFE: ldv_play = false; ldv_out = 0x65; break;
            case 0xC2: { char s[8]; snprintf(s, sizeof(s), "%05d", ldv_cur);
                         ldv_push(s[4]); ldv_push(s[3]); ldv_push(s[2]); ldv_push(s[1]); ldv_push(s[0]); break; }
            case 0xBF: ldv_clear(); break;
            case 0xFF: ldv_out |= 0x80; break;
            default: break;
        }
    } else {
        if (v == 0xFF) ldv_out |= 0x80; else ldv_out &= 0x7F;
    }
}

// ============================================================ ace.meta + mappa frame
static int ace_seg_frames = 1000, ace_nframes = 0, ace_ld_start = 2;
static bool ace_load_meta(void) {
    FILE *f = fopen(SD_MOUNT_POINT "/ace/ace.meta", "r");
    if (!f) return false;
    int sf = 0, nf = 0, ls = 2;
    int n = fscanf(f, "%d %d %d", &sf, &nf, &ls);
    fclose(f);
    if (n != 3 || sf <= 0 || nf <= 0) return false;
    ace_seg_frames = sf; ace_nframes = nf; ace_ld_start = ls;
    return true;
}
static inline int ace_ld_to_mjpg(int ld_frame) {
    int d = ld_frame - ace_ld_start; return (d < 0) ? 0 : d;
}
static inline int64_t ace_frame_to_sample(int mjpg_frame) {
    if (mjpg_frame < 0) mjpg_frame = 0;
    return (int64_t)mjpg_frame * 4004 / 3;
}
static inline int64_t ace_seg_start_sample(int seg) {
    return (int64_t)seg * ace_seg_frames * 4004 / 3;
}
static int ace_seg_of_sample(int64_t p) {
    int seg = (int)(p * 3 / (4004LL * ace_seg_frames));
    while (seg > 0 && ace_seg_start_sample(seg) > p) seg--;
    while (ace_seg_start_sample(seg + 1) <= p)       seg++;
    return seg;
}

// ============================================================ AUDIO SCENA (.pcm) — cache LRU
#define ACE_AC_N 4
typedef struct { int seg; FILE *fp; long off; uint32_t lru; } ace_aslot_t;
static ace_aslot_t ace_a[ACE_AC_N];
static uint32_t    ace_a_tick = 0;
static bool        ace_apcm_none[64];

static ace_aslot_t *ace_apcm_get(int seg) {
    if (seg < 0 || seg >= 64 || ace_apcm_none[seg]) return nullptr;
    for (int i = 0; i < ACE_AC_N; i++)
        if (ace_a[i].fp && ace_a[i].seg == seg) { ace_a[i].lru = ++ace_a_tick; return &ace_a[i]; }
    int vi = 0;
    for (int i = 0; i < ACE_AC_N; i++) {
        if (!ace_a[i].fp) { vi = i; break; }
        if (ace_a[i].lru < ace_a[vi].lru) vi = i;
    }
    ace_aslot_t *s = &ace_a[vi];
    if (s->fp) { fclose(s->fp); s->fp = nullptr; }
    char p[64]; snprintf(p, sizeof(p), SD_MOUNT_POINT "/ace/seg%03d.pcm", seg);
    FILE *f = fopen(p, "rb");
    if (!f) { ace_apcm_none[seg] = true; return nullptr; }
    s->fp = f; s->seg = seg; s->off = 0; s->lru = ++ace_a_tick;
    return s;
}
static void ace_apcm_read(int64_t pos, int16_t *dst, int n) {
    int done = 0;
    while (done < n) {
        int64_t p = pos + done;
        int seg = ace_seg_of_sample(p);
        int64_t s0 = ace_seg_start_sample(seg), s1 = ace_seg_start_sample(seg + 1);
        int chunk = (int)(s1 - p);
        if (chunk > n - done || chunk <= 0) chunk = n - done;
        ace_aslot_t *as = ace_apcm_get(seg);
        if (!as) { memset(&dst[2 * done], 0, (size_t)chunk * 4); done += chunk; continue; }
        long off = (long)((p - s0) * 4);
        if (off != as->off) { fseek(as->fp, off, SEEK_SET); as->off = off; }
        size_t got = fread(&dst[2 * done], 4, (size_t)chunk, as->fp);
        as->off += (long)got * 4;
        if (got < (size_t)chunk) memset(&dst[2 * (done + got)], 0, (size_t)(chunk - got) * 4);
        done += chunk;
    }
}
#define ACE_APF_N 4096
static int16_t *ace_apf     = nullptr;
static int64_t  ace_apf_pos = -1;
static int      ace_apf_len = 0;
static void ace_apcm_fetch(int64_t pos, int16_t *dst, int n) {
    if (!ace_apf) {
        ace_apf = (int16_t *)heap_caps_malloc((size_t)ACE_APF_N * 4, MALLOC_CAP_INTERNAL);
        if (!ace_apf) { memset(dst, 0, (size_t)n * 4); return; }
    }
    if (ace_apf_pos < 0 || pos < ace_apf_pos || pos + n > ace_apf_pos + ace_apf_len) {
        ace_apcm_read(pos, ace_apf, ACE_APF_N);
        ace_apf_pos = pos; ace_apf_len = ACE_APF_N;
    }
    int off = (int)(pos - ace_apf_pos);
    memcpy(dst, &ace_apf[2 * off], (size_t)n * 4);
}

// ============================================================ AY-3-8910 (effetti)
static uint8_t ay_reg[16];
static float   ay_phase[3] = {0, 0, 0};
static float   ay_noise_ph = 0;
static uint32_t ay_lfsr = 1;
static uint8_t ay_noise_bit = 0;
static float   ay_env_ph = 0;
static int     ay_env_pos = 15, ay_env_dir = -1;
static bool    ay_env_stop = false;
static const int16_t AY_VOL[16] = {
    0, 55, 78, 111, 158, 224, 316, 447, 632, 894, 1265, 1789, 2530, 3578, 5060, 7155 };

static void ay_write(uint8_t r, uint8_t v) {
    if (r > 15) return;
    ay_reg[r] = v;
    if (r == 13) { ay_env_ph = 0; ay_env_stop = false;
                   ay_env_dir = (v & 0x04) ? +1 : -1; ay_env_pos = (v & 0x04) ? 0 : 15; }
}
static void ay_render(int16_t *out, int n) {
    const float Fs = 32000.0f, CK = 2000000.0f;
    for (int i = 0; i < n; i++) {
        float nper = (float)(ay_reg[6] & 0x1F); if (nper < 1) nper = 1;
        ay_noise_ph += (CK / (16.0f * nper)) / Fs;
        while (ay_noise_ph >= 1.0f) { ay_noise_ph -= 1.0f;
            ay_lfsr = (ay_lfsr >> 1) ^ ((ay_lfsr & 1) ? 0x12000u : 0u); ay_noise_bit = ay_lfsr & 1; }
        float eper = (float)(((ay_reg[12] << 8) | ay_reg[11])); if (eper < 1) eper = 1;
        ay_env_ph += (CK / (256.0f * eper)) / Fs;
        while (ay_env_ph >= 1.0f) { ay_env_ph -= 1.0f;
            if (!ay_env_stop) { ay_env_pos += ay_env_dir;
                if (ay_env_pos < 0 || ay_env_pos > 15) {
                    uint8_t sh = ay_reg[13] & 0x0F;
                    if (!(sh & 0x08)) { ay_env_pos = 0; ay_env_stop = true; }
                    else if (sh & 0x01) { ay_env_pos = (sh & 0x04) ? 15 : 0; ay_env_stop = true; }
                    else if (sh & 0x02) { ay_env_dir = -ay_env_dir; ay_env_pos += ay_env_dir; }
                    else ay_env_pos = (ay_env_dir > 0) ? 0 : 15;
                } } }
        int env_v = ay_env_pos & 0x0F;
        int32_t mix = 0;
        for (int ch = 0; ch < 3; ch++) {
            int period = ((ay_reg[2 * ch + 1] & 0x0F) << 8) | ay_reg[2 * ch]; if (period < 1) period = 1;
            ay_phase[ch] += (CK / (16.0f * period)) / Fs;
            while (ay_phase[ch] >= 1.0f) ay_phase[ch] -= 1.0f;
            uint8_t tone = ay_phase[ch] < 0.5f ? 1 : 0;
            uint8_t ten  = !((ay_reg[7] >> ch) & 1);
            uint8_t nen  = !((ay_reg[7] >> (ch + 3)) & 1);
            uint8_t on   = (tone | !ten) & (ay_noise_bit | !nen);
            uint8_t vr   = ay_reg[8 + ch];
            int vol = (vr & 0x10) ? env_v : (vr & 0x0F);
            if (on) mix += AY_VOL[vol];
        }
        if (mix > 32767) mix = 32767;
        out[2 * i] = out[2 * i + 1] = (int16_t)mix;
    }
}

// ============================================================ callback memoria/IO Z80 (rinominate)
extern "C" byte RdZ80(word Addr) {
    if (Addr < 0xC000) return ace_z80mem[Addr];
    switch (Addr) {
        case 0xC008: return joyskill;
        case 0xC010: return misc_val;
        case 0xC020: return ldv_read();
        default:     return ace_z80mem[Addr];
    }
}
extern "C" void WrZ80(word Addr, byte V) {
    if (Addr < 0xA000) return;
    if (Addr < 0xB000) { ace_z80mem[Addr] = V; return; }
    switch (Addr) {
        case 0xE000: ay_write(snd_latch, V); break;
        case 0xE010: snd_latch = V;
                     if (V == 0x0E) ace_z80mem[0xC000] = sw_a;
                     else if (V == 0x0F) ace_z80mem[0xC000] = sw_b;
                     break;
        case 0xE020: ldv_write(V); break;
        case 0xE038: case 0xE039: case 0xE03A:
        case 0xE03B: case 0xE03C: case 0xE03D:
            if (V != 0xCC) { uint8_t d = V & 0x0F;
                             sb_score[Addr - 0xE038] = (d <= 9) ? (char)('0' + d) : ' '; }
            break;
        case 0xE036: if ((V & 0x0F) <= 9) sb_cred_t = V & 0x0F; break;
        case 0xE037: if ((V & 0x0F) <= 9) sb_cred_u = V & 0x0F; break;
        case 0xE03E: if (V != 0xCC && (V & 0x0F) <= 9) sb_lives = V & 0x0F; break;
        default: break;
    }
}
extern "C" byte InZ80(word P)            { (void)P; return 0xFF; }
extern "C" void OutZ80(word P, byte V)   { (void)P; (void)V; }
extern "C" word LoopZ80(Z80 *R)          { (void)R; return INT_NONE; }
extern "C" void PatchZ80(Z80 *R)         { (void)R; }
extern "C" unsigned char OpZ80_INL(unsigned short Addr) { return ace_z80mem[Addr]; }

// ============================================================ input SPINNERINO -> C008/C010
static void ace_read_inputs(void) {
    uint8_t b = g_input->buttons_get();

    // in vol-mode il firmware regola il volume: le direzioni/COIN NON vanno al ROM
    if (input_is_vol_mode_active()) b &= ~(ABT_UP | ABT_DOWN | ABT_LEFT | ABT_RIGHT | ABT_COIN);

    // anti-flicker COIN/START (routine credito ROM conta 3 frame consecutivi)
    static int coin_hold = 0, start_hold = 0;
    const int STRETCH = 8;
    if (b & ABT_COIN)  coin_hold  = STRETCH; else if (coin_hold  > 0) coin_hold--;
    if (b & ABT_START) start_hold = STRETCH; else if (start_hold > 0) start_hold--;

    uint8_t js = 0xFF;
    if (b & ABT_UP)    js &= ~0x01;
    if (b & ABT_DOWN)  js &= ~0x02;
    if (b & ABT_LEFT)  js &= ~0x04;
    if (b & ABT_RIGHT) js &= ~0x08;
    if (b & ABT_FIRE)  js &= ~0x10;
    if (start_hold > 0) js &= ~g_ace_skill;   // START -> skill scelta = avvia
    joyskill = js;

    uint8_t mi = 0x3F;
    if (coin_hold  > 0) mi &= ~0x04;          // coin1
    if (start_hold > 0) mi &= ~0x01;          // START1
    misc_inputs = mi;
}

// ============================================================ un CAMPO (60Hz): strobe + slice
#define FIELD_CYCLES  66666
#define IRQ_CYCLES    131072
#define STATUS_START  2108
#define STATUS_END    2212
#define CMD_START     2428
#define CMD_END       2528
static uint32_t irq_acc = 0;

static void ace_run_field(void) {
    int done = 0;
    while (done < FIELD_CYCLES) {
        uint8_t m = misc_inputs & 0x3F;
        if (done >= STATUS_START && done < STATUS_END) m |= 0x40;
        if (done >= CMD_START    && done < CMD_END)    m |= 0x80;
        misc_val = m;
        ExecZ80(&cpu, 32);
        done += 32;
        irq_acc += 32;
        if (irq_acc >= IRQ_CYCLES) { irq_acc -= IRQ_CYCLES;
            if (cpu.IFF & IFF_1) IntZ80(&cpu, INT_RST38); }
    }
    if (ldv_play) {
        ldv_acc += (23976 * ldv_spd_n) / ldv_spd_d;
        while (ldv_acc >= 60000) { ldv_acc -= 60000; ldv_cur++; }
    }
    if ((ldv_out & 0x7F) == 0x54 && ldv_cur >= ldv_autostop) {
        ldv_play = false; ldv_out = (uint8_t)((ldv_out & 0x80) | 0x65); ldv_autostop = 0;
    }
}

// ============================================================ TASK AUDIO (CPU0)
static void ace_audio_task(void *arg) {
    (void)arg;
    laserdisc_audio_begin();
    static int16_t buf[256 * 2];      // AY -> mix in-place
    static int16_t sc[256 * 2];       // scena
    int64_t pos = 0;
    while (g_running) {
        ay_render(buf, 256);
        int64_t want = ace_frame_to_sample(ace_ld_to_mjpg((int)ldv_cur));
        if (!ldv_play) { memset(sc, 0, sizeof(sc)); pos = want; }
        else {
            int64_t d = pos - want; if (d < 0) d = -d;
            if (d > 3200) pos = want;
            ace_apcm_fetch(pos, sc, 256);
            pos += 256;
        }
        // mix scena+effetti, poi VOLUME come i giochi/DL (1=max, >=30=mute)
        // ACE_TRIM_PCT: attenuazione GENERALE del player (utente 2026-07-18).
        // 50, NON 72 come DL: MISURATO (measure_pcm.py sui seg000..seg005) che
        // i .pcm di Space Ace hanno RMS 16..20% FS contro i 3.3..4.9% di Dragon's
        // Lair, cioe' sono ~5 dB piu' caldi. Con lo stesso trim Space Ace suonava
        // molto piu' forte di DL ("troppo alto"). Con 50 il gain a barra piena e'
        // 0.63x -> RMS in uscita ~10-12% FS = stesso livello percepito di DL.
        // Storia: 100 (originale) -> 70 ("-30%") -> 42 (fix gracchio) -> 50 (+20%).
        #define ACE_TRIM_PCT 50
        // Soft-limiter identico a LD_LIM_T in laserdisc.cpp: qui i .pcm arrivano
        // gia' a 89..100% FS di picco e sommando l'AY (ACE_GAIN_SFX) il mix puo'
        // sfondare -> senza limiter tornerebbe l'hard clamp squadrato = gracchio.
        #define ACE_LIM_T 24000
        int vs = laserdisc_current_volume();
        int vol = (vs >= 30) ? 0 : (31 - vs);        // 0..30
        // fondo scala in ingresso al limiter: mix a fondo scala su ENTRAMBE le
        // sorgenti, con il gain corrente. Scala col volume -> a barra bassa inerte.
        int32_t in_max = (int32_t)(((32767 * (ACE_GAIN_SCENE + ACE_GAIN_SFX)) >> 3)
                                   * vol * ACE_TRIM_PCT / (30 * 100));
        if (in_max <= ACE_LIM_T) in_max = ACE_LIM_T + 1;
        for (int i = 0; i < 256 * 2; i++) {
            int32_t v = ((int32_t)sc[i] * ACE_GAIN_SCENE + (int32_t)buf[i] * ACE_GAIN_SFX) >> 3;
            v = (v * vol * ACE_TRIM_PCT) / (30 * 100);
            int32_t a = (v < 0) ? -v : v;
            if (a > ACE_LIM_T) {                     // compressione dei soli picchi
                a = ACE_LIM_T + ((a - ACE_LIM_T) * (32767 - ACE_LIM_T)) / (in_max - ACE_LIM_T);
                if (a > 32767) a = 32767;
                v = (v < 0) ? -a : a;
            }
            buf[i] = (int16_t)v;
        }
        laserdisc_audio_write(buf, 256);            // bloccante = clock 32kHz
    }
    laserdisc_audio_end();
    vTaskDelete(nullptr);
}

// ============================================================ VIDEO 176x240 (JPEGDEC SW) — come DL
// segmenti /ace/segNNN.mjpg (+ .idx), cache LRU 4 slot.
#define ACE_OC_N 4
typedef struct { int seg; FILE *fp; uint32_t *idx; uint32_t nfr; uint32_t lru; } ace_vslot_t;
static ace_vslot_t ace_v[ACE_OC_N];
static uint32_t    ace_v_tick = 0;

static ace_vslot_t *ace_get_segment(int seg) {
    for (int i = 0; i < ACE_OC_N; i++)
        if (ace_v[i].fp && ace_v[i].seg == seg) { ace_v[i].lru = ++ace_v_tick; return &ace_v[i]; }
    int vi = 0;
    for (int i = 0; i < ACE_OC_N; i++) {
        if (!ace_v[i].fp) { vi = i; break; }
        if (ace_v[i].lru < ace_v[vi].lru) vi = i;
    }
    ace_vslot_t *s = &ace_v[vi];
    if (s->fp)  { fclose(s->fp); s->fp = nullptr; }
    if (s->idx) { heap_caps_free(s->idx); s->idx = nullptr; }
    char p[64];
    snprintf(p, sizeof(p), SD_MOUNT_POINT "/ace/seg%03d.idx", seg);
    FILE *fi = fopen(p, "rb");
    if (!fi) return nullptr;
    uint32_t nfr = 0;
    if (fread(&nfr, 4, 1, fi) != 1 || nfr == 0 || nfr > 2000) { fclose(fi); return nullptr; }
    s->idx = (uint32_t *)heap_caps_malloc((size_t)nfr * 8, MALLOC_CAP_SPIRAM);
    if (!s->idx || fread(s->idx, 8, nfr, fi) != nfr) {
        if (s->idx) { heap_caps_free(s->idx); s->idx = nullptr; }
        fclose(fi); return nullptr;
    }
    fclose(fi);
    snprintf(p, sizeof(p), SD_MOUNT_POINT "/ace/seg%03d.mjpg", seg);
    s->fp = fopen(p, "rb");
    if (!s->fp) { heap_caps_free(s->idx); s->idx = nullptr; return nullptr; }
    s->seg = seg; s->nfr = nfr; s->lru = ++ace_v_tick;
    return s;
}
static void ace_close_segments(void) {
    for (int i = 0; i < ACE_OC_N; i++) {
        if (ace_v[i].fp)  fclose(ace_v[i].fp);
        if (ace_v[i].idx) heap_caps_free(ace_v[i].idx);
        ace_v[i].fp = nullptr; ace_v[i].idx = nullptr; ace_v[i].lru = 0; ace_v[i].seg = -1;
    }
    for (int i = 0; i < ACE_AC_N; i++) {
        if (ace_a[i].fp) fclose(ace_a[i].fp);
        ace_a[i].fp = nullptr;
    }
}

// ---- overlay ruotato (come DL: il frame 176x240 e' pre-ruotato) ----
static inline void ld_px_vis(uint16_t *img, int X, int Y, uint16_t col) {
    if ((unsigned)X >= 240u || (unsigned)Y >= 176u) return;
    img[(size_t)(239 - X) * LD_W + Y] = col;
}
static void ld_text_vis(uint16_t *img, int x, int y, const char *s, uint16_t col) {
    for (int i = 0; s[i]; i++) {
        int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++)
                if (row & (0x80 >> c)) ld_px_vis(img, x + i * 8 + c, y + r, col);
        }
    }
}
static void ld_window(bool full) {
    video.waitDMA();
    if (full) video.setAddrWindow(TFT_HW_X, 0,        TFT_FB_W, LD_FB_H);
    else      video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}
static void ld_screen(const char *l1, const char *l2, uint16_t col) {
    const char *L[2] = { l1, l2 }; const int Y[2] = { 150, 175 };
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        for (int k = 0; k < 2; k++) {
            if (!L[k]) continue;
            int n = (int)strlen(L[k]), x0 = (240 - n * 8) / 2; if (x0 < 4) x0 = 4;
            for (int i = 0; i < n; i++) {
                int idx = char_to_idx(L[k][i]);
                for (int r = 0; r < 8; r++) {
                    unsigned char row = pgm_read_byte(&FONT8[idx][r]);
                    int x_l = Y[k] + r; if (x_l < 0 || x_l >= TFT_FB_W) continue;
                    for (int c = 0; c < 8; c++) {
                        if (!(row & (0x80 >> c))) continue;
                        int y_l = 239 - (x0 + i * 8 + c), y_in = y_l - sy;
                        if (y_in < 0 || y_in >= 8) continue;
                        g_strip[y_in * TFT_FB_W + x_l] = col;
                    }
                }
            }
        }
        video.write(g_strip, TFT_FB_W * 8);
    }
}

// -------------------------------------------------- barra volume (IDENTICA a DL)
// Video letterboxed: 176px centrati in 320 -> bande nere ai window cols 0..71 (alto)
// e 248..319 (basso). ld_px_vis copre solo l'area video (72..247), quindi la barra si
// scrive DIRETTA in g_strip dentro ld_blit, nella banda ALTA (window col vicino a 0).
#define VOLBAR_LEN   112
#define VOLBAR_ROW0  ((240 - VOLBAR_LEN) / 2)   // 64: centrata orizzontalmente
#define VOLBAR_COL0  3                          // vicino al bordo alto
#define VOLBAR_THK   16
#define VOLBAR_RED   10
static int g_volbar_fill = -1;                  // -1 = off; 0..VOLBAR_LEN

// ---- blit del frame 176x240 nel FB 320 (letterbox, X_OFF 72) + barra volume ----
static void ld_blit(void) {
    const uint16_t *img = (const uint16_t *)g_out;
    for (int s = 0; s < LD_FB_H / 8; s++) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        for (int i = 0; i < 8; i++) {
            int src_y = s * 8 + i; if (src_y < 0 || src_y >= LD_H) continue;
            memcpy(g_strip + i * TFT_FB_W + LD_X_OFF,
                   img + (size_t)src_y * LD_W, (size_t)LD_W * sizeof(uint16_t));
        }
        // barra volume nella banda nera alta (se vol mode attivo) — identica a DL
        if (g_volbar_fill >= 0) {
            for (int i = 0; i < 8; i++) {
                int pos = (s * 8 + i) - VOLBAR_ROW0;
                if (pos < 0 || pos >= VOLBAR_LEN) continue;
                int asx = VOLBAR_LEN - 1 - pos;            // riempimento sx->dx come i giochi
                for (int c = 0; c < VOLBAR_THK; c++) {
                    uint16_t cc;
                    if (asx == 0 || asx == VOLBAR_LEN - 1 || c == 0 || c == VOLBAR_THK - 1) cc = 0xFFFF;
                    else if (asx < g_volbar_fill && asx >= VOLBAR_LEN - VOLBAR_RED)         cc = 0x00F8;
                    else if (asx < g_volbar_fill)                                          cc = 0xE007;
                    else                                                                    cc = 0x0000;
                    g_strip[i * TFT_FB_W + VOLBAR_COL0 + c] = cc;
                }
            }
        }
        video.write(g_strip, TFT_FB_W * 8);
    }
}

// riempimento barra dal volume corrente (la REGOLAZIONE la fa buttons_get del firmware,
// come DL). laserdisc_current_volume() = 1..30 (1 = max).
static void ace_update_volbar(void) {
    if (!input_is_vol_mode_active()) { g_volbar_fill = -1; return; }
    int f = (VOLBAR_LEN * (31 - laserdisc_current_volume())) / 30;
    if (f < 0) f = 0; else if (f > VOLBAR_LEN) f = VOLBAR_LEN;
    g_volbar_fill = f;
}

// ---- decode JPEGDEC -> g_out ----
static int ace_jpeg_draw(JPEGDRAW *p) {
    uint16_t *dst = (uint16_t *)g_out;
    for (int row = 0; row < p->iHeight; row++) {
        int y = p->y + row; if ((unsigned)y >= (unsigned)LD_H) continue;
        int x0 = p->x; if (x0 < 0 || x0 >= LD_W) continue;
        int w = p->iWidth; if (x0 + w > LD_W) w = LD_W - x0;
        if (w > 0) memcpy(dst + (size_t)y * LD_W + x0,
                          p->pPixels + (size_t)row * p->iWidth, (size_t)w * 2);
    }
    return 1;
}

static void ace_draw_scoreboard(uint16_t *fb) {
    char line[40];
    snprintf(line, sizeof(line), "%s  ACE %d  CR %02d",
             sb_score, sb_lives, sb_cred_t * 10 + sb_cred_u);
    ld_text_vis(fb, 4, 3, line, 0xFFFF);
}

static void ace_show_frame(int mjpg_frame) {
    if (mjpg_frame < 0) mjpg_frame = 0;
    if (ace_nframes > 0 && mjpg_frame >= ace_nframes) mjpg_frame = ace_nframes - 1;
    int seg = mjpg_frame / ace_seg_frames, local = mjpg_frame % ace_seg_frames;
    video.waitDMA();                                   // reap SPI DMA prima della SD
    ace_vslot_t *sl = ace_get_segment(seg);
    if (!sl || (uint32_t)local >= sl->nfr) return;
    uint32_t off = sl->idx[2 * local], len = sl->idx[2 * local + 1];
    if (!len || len > LD_IN_MAX) return;
    if (fseek(sl->fp, (long)off, SEEK_SET) != 0) return;
    if (fread(g_in, 1, len, sl->fp) != len) return;
    int ok = 0;
    if (g_jdec.openRAM(g_in, (int)len, ace_jpeg_draw)) {
        g_jdec.setPixelType(RGB565_BIG_ENDIAN);
        ok = g_jdec.decode(0, 0, 0);
        g_jdec.close();
    }
    if (!ok) return;
    ace_draw_scoreboard((uint16_t *)g_out);
    ace_update_volbar();                    // calcola il fill; la barra la disegna ld_blit
    ld_blit();
}

// ============================================================ SCELTA DIFFICOLTA'
static bool ld_want_exit(uint8_t b) {
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & ABT_START) {
        uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}
// Testo SCALATO nella strip (mappa ruotata come ld_screen). IDENTICA a DL
// (dl_select_difficulty in laserdisc.cpp): x_l = y_p + r*scale, y_l = 239 - x_p.
static void ld_line_strip(int sy, const char *s, int y_p, int scale, uint16_t col) {
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
// Schermata scelta difficolta': IMPAGINAZIONE, SCALE e COLORI IDENTICI a DL.
// (DL: titolo scala2 giallo 0xE0FF, "SCEGLI DIFFICOLTA" scala1 ciano 0xFF07, voci
//  scala1 bianche, "START 3s = ESCI" scala1 grigio 0xEF7B; conferma "MODALITA" scala2
//  bianco + modalita' scala3 verde 0xE007.) Solo il titolo cambia in "SPACE ACE".
static void ace_diff_screen(void) {
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        ld_line_strip(sy, "SPACE ACE",         92,  2, 0xE0FF);
        ld_line_strip(sy, "SCEGLI DIFFICOLTA", 138, 1, 0xFF07);
        ld_line_strip(sy, "SU = FACILE",        166, 1, 0xFFFF);
        ld_line_strip(sy, "GIU/FIRE = NORMALE", 184, 1, 0xFFFF);
        ld_line_strip(sy, "START 3s = ESCI",    208, 1, 0xEF7B);
        video.write(g_strip, TFT_FB_W * 8);
    }
}
// SU = FACILE (vite infinite + skill Cadet) · GIU/FIRE = NORMALE (5 vite + Captain).
// Ritorna true se si gioca, false se si esce (START 3s). Stesso flusso di DL.
static bool ace_select_difficulty(void) {
    bool released = false;
    for (;;) {
        uint8_t b = g_input->buttons_get();
        if (!(b & (ABT_FIRE | ABT_START | ABT_UP | ABT_DOWN))) released = true;
        ace_diff_screen();
        if (ld_want_exit(b)) return false;                       // START 3s -> menu
        if (released && (b & ABT_UP)) {                          // FACILE
            g_ace_skill = 0x20; sw_a = 0x21; sw_b = 0xDE; break;
        }
        if (released && (b & (ABT_DOWN | ABT_FIRE))) {           // NORMALE
            g_ace_skill = 0x40; sw_a = 0x21; sw_b = 0xFE; break;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    // CONFERMA: modalita' in VERDE, grande e centrata (identico a DL).
    {
        const char *mode = (g_ace_skill == 0x20) ? "FACILE" : "NORMALE";
        for (int sy = 0; sy < LD_FB_H; sy += 8) {
            memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
            ld_line_strip(sy, "MODALITA", 96,  2, 0xFFFF);
            ld_line_strip(sy, mode,       140, 3, 0xE007);
            video.write(g_strip, TFT_FB_W * 8);
        }
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    while (g_input->buttons_get() & (ABT_UP | ABT_DOWN | ABT_FIRE | ABT_START))
        vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

// ============================================================ init player (una volta)
// ⛔⛔⛔ IL DIFETTO «ALLOC BUFFER» (referto 2026-08-28: *«su SPACE ACE dopo aver
// giocato piu' giochi mi da errore rosso ALLOC BUFFER»*), e le due cose che ne
// sono uscite.
//
// 1) **QUALE dei quattro buffer** non si allocava non lo diceva nessuno, e i
//    quattro non si somigliano per niente: due stanno in PSRAM (48 KB e 82 KB,
//    dove lo spazio non manca mai) e due in RAM INTERNA — `audmix`, che e' 1 KB,
//    e **`ace_z80mem`, che sono 64 KB CONTIGUI**, la richiesta interna piu'
//    grossa di tutto il progetto. Un guasto senza voce si manifesta a valle e
//    fa cercare nel posto sbagliato ([[feedback_stack_task_prenotato_per_primo]]:
//    li' un «Z80 ERROR» del BIOS accusava l'emulazione mentre la causa era una
//    `xTaskCreate` fallita in silenzio). Adesso il messaggio dice il NOME del
//    buffer e i DUE numeri che distinguono le due cause possibili:
//      INT = RAM interna libera in tutto · MAX = blocco CONTIGUO piu' grande
//    Se INT e' grande e MAX piccolo e' **frammentazione**; se sono piccoli
//    tutt'e due e' **esaurimento**. ⚠️ `heap_caps_get_free_size()` da' il
//    TOTALE, non il blocco piu' grande: da solo non distingue i due casi.
//
// 2) **Perche' «dopo aver giocato piu' giochi»**: i giochi di questo progetto
//    allocano i loro buffer alla prima partita e **non li liberano piu'**
//    (start_machine libera esplicitamente solo PANG, «l'unico allocatore
//    enorme»). Ogni gioco provato si porta via un pezzo di RAM interna, e la
//    stessa dinamica e' gia' documentata in `emulation.cpp`: lo stack del task
//    emulazione fu reso STATICO proprio perche' «dopo molte rotazioni la heap
//    interna si FRAMMENTA e l'alloc non trova piu' 16 KB CONTIGUI». Qui di
//    contigui ne servono **64**.
//
// ⭐ La cura sta DOVE IL DIFETTO SI MANIFESTA, non dove nasce: i due buffer
//    interni ripiegano in PSRAM invece di far fallire il gioco. Curarlo alla
//    nascita vorrebbe dire liberare i buffer di ogni gioco all'uscita — venti
//    file sani da toccare ([[feedback_reset_software_non_spegne_la_sd]]: si
//    sceglie il raggio d'azione minimo, e lo smontaggio preventivo resta una
//    proposta a se').
// ⛔ IL RIPIEGO ERA NON SILENZIOSO — lo diceva a schermo per un secondo e mezzo
//    — ma dal 2026-08-30 quell'avviso e' NASCOSTO su richiesta dell'utente:
//    `ACE_AVVISO_RIPIEGO 0`, piu' avanti in `ace_init()`, dove sta scritto per
//    intero cosa si perde. ⚠️ Da qui in poi vale il contrario di quello che
//    diceva questa riga: **se SPACE ACE andasse a scatti o si riavviasse, non
//    c'e' piu' niente a schermo che lo spieghi** — si rimette
//    `ACE_AVVISO_RIPIEGO 1` PRIMA di cercare altrove.
// ⚠️ Prezzo dichiarato: il Z80 di Space Ace macina **66.666 cicli per campo**
//    (~4 milioni al secondo) e in PSRAM ogni accesso passa dalla cache. Non e'
//    stato misurato: e' un ripiego, non la strada normale.
// ⚠️ SPENTO il 2026-08-29, a difetto CHIUSO e confermato dall'utente.
// Metterlo a 1 rimette la schermata «RST xxx / INT nnnK MAX nnnK» all'avvio di
// SPACE ACE quando il reset precedente e' stato ANOMALO (watchdog, panic,
// brownout). Serviva a cacciare il riavvio nell'attract: davanti a «si riavvia»
// ci sono quattro cause possibili e distinguerle a tentativi costa flash su
// flash, mentre la CPU se lo ricorda da sola ([[feedback_task_audio_indietro_watchdog]]).
// Se un domani SPACE ACE tornasse a riavviarsi, questa e' la prima riga da
// riaccendere: dice la CAUSA e lo stato della RAM interna nello stesso momento.
#define ACE_MOSTRA_RESET_ANOMALO 0

static bool ace_init(void) {
#if ACE_MOSTRA_RESET_ANOMALO
    {
        const esp_reset_reason_t rr = esp_reset_reason();
        if (rr != ESP_RST_POWERON && rr != ESP_RST_SW && rr != ESP_RST_UNKNOWN) {
            const char *nome = (rr == ESP_RST_TASK_WDT) ? "RST WDT"
                             : (rr == ESP_RST_INT_WDT)  ? "RST IWDT"
                             : (rr == ESP_RST_WDT)      ? "RST HWDT"
                             : (rr == ESP_RST_PANIC)    ? "RST PANIC"
                             : (rr == ESP_RST_BROWNOUT) ? "RST BROWNOUT" : "RST ?";
            char n0[32];
            snprintf(n0, sizeof n0, "INT %uK MAX %uK",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
            ld_screen(nome, n0, 0xE0FF);
            vTaskDelay(pdMS_TO_TICKS(2500));
        }
    }
#endif  // ACE_MOSTRA_RESET_ANOMALO

    // ⚠️ `ace_z80mem` sono 64 KB e servono TUTTI: `RdZ80` ha un
    // `default: return ace_z80mem[Addr]` che arriva fino a 0xFFFF. Non si puo'
    // ridurre la richiesta a 0xC000 «tanto sopra non ci va nessuno».
    if (!g_in)   g_in   = (uint8_t *)heap_caps_malloc(LD_IN_MAX,               MALLOC_CAP_SPIRAM);
    if (!g_out)  g_out  = (uint8_t *)heap_caps_malloc((size_t)LD_W * LD_H * 2, MALLOC_CAP_SPIRAM);

    int ripiego = 0;                       // bit0 = audmix, bit1 = ace_z80mem
    if (!audmix) {
        audmix = (int16_t *)heap_caps_malloc(256 * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL);
        if (!audmix) { audmix = (int16_t *)heap_caps_malloc(256 * 2 * sizeof(int16_t), MALLOC_CAP_SPIRAM); if (audmix) ripiego |= 1; }
    }
    if (!ace_z80mem) {
        // ⛔⛔⛔ LO STACK DI UN TASK SI PRENOTA PER PRIMO, e qui non lo era.
        // Subito dopo questa funzione `ace_play_task` crea DUE task —
        // `ace_aud` (8 KB) e `ace_rom` (16 KB) — e uno stack FreeRTOS e'
        // l'unica cosa che **non ha un ripiego in PSRAM**. Prendendo prima i
        // 64 KB di `ace_z80mem`, gli stack raccoglievano gli avanzi: e' esatta-
        // mente il difetto di [[feedback_stack_task_prenotato_per_primo]]
        // («in una gara per la RAM interna lo stack e' il concorrente che va
        // servito per primo, non quello che raccoglie gli avanzi»).
        // ✅ Percio' l'interna si prende SOLO se ne resta abbastanza per loro.
        // Se non basta, `ace_z80mem` va in PSRAM — dove funziona — e i task
        // trovano il posto che serve.
        const size_t Z80_SZ = 0x10000;                 // 64 KB
        const size_t RISERVA = 40 * 1024;              // 8+16 di stack + margine
        const size_t libera  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
        const size_t blocco  = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
        if (blocco >= Z80_SZ && libera >= Z80_SZ + RISERVA)
            ace_z80mem = (uint8_t *)heap_caps_malloc(Z80_SZ, MALLOC_CAP_INTERNAL);
        if (!ace_z80mem) { ace_z80mem = (uint8_t *)heap_caps_malloc(Z80_SZ, MALLOC_CAP_SPIRAM); if (ace_z80mem) ripiego |= 2; }
    }

    // I due numeri si prendono UNA volta: `heap_caps_get_largest_free_block()`
    // prende il lock dell'heap e scorre tutti i blocchi liberi
    // ([[feedback_no_costly_calls_in_render]]). Qui siamo all'avvio del gioco,
    // non in un ciclo di disegno.
    char numeri[32];
    snprintf(numeri, sizeof numeri, "INT %uK MAX %uK",
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));

    const char *manca = !g_in       ? "MANCA IN 48K"
                      : !g_out      ? "MANCA OUT 82K"
                      : !audmix     ? "MANCA AUDMIX 1K"
                      : !ace_z80mem ? "MANCA Z80 64K" : nullptr;
    if (manca) { ld_screen(manca, numeri, 0x00F8); return false; }

    // ------------------------------------------------------------------------
    // AVVISO DI RIPIEGO — nascosto il 2026-08-30 su richiesta dell'utente.
    //
    // ⛔⛔ NON e' debug e NON e' un errore: e' il referto che dice che
    // `ace_z80mem` (64 KB) non ha trovato posto in RAM INTERNA e sta girando in
    // PSRAM. Il difetto che lo fa comparire **resta aperto**: misurato
    // `INT 131K MAX 61K`, cioe' memoria libera ce n'e' (131 KB) ma il blocco
    // CONTIGUO piu' grande e' 61 KB e ne servono 64 — mancano 3 KB su 64. E'
    // **frammentazione**, non esaurimento (le due si distinguono proprio da quei
    // due numeri, ed e' per questo che ce ne sono due).
    // ⚠️ E l'utente ha misurato che compare **solo dopo aver giocato ad altro**,
    // mai a freddo: quindi i buchi si formano DURANTE la sessione — `freeBuffers()`
    // restituisce la memoria ma non la COMPATTA.
    //
    // ⚠️⚠️ IL PREZZO DI NASCONDERLO, dichiarato: il ripiego torna **silenzioso**,
    // e col Z80 in PSRAM riparte la catena gia' pagata
    // ([[spaceace_alloc_buffer_cinque_giri]] giro 4: Z80 lento -> `ace_rom_task`
    // fuori budget -> IDLE affamato -> task watchdog -> riavvio). Se un domani
    // SPACE ACE tornasse a riavviarsi nell'attract, **questa e' la prima riga da
    // riaccendere**, prima di cercare altrove.
    // ⭐ L'informazione resta comunque raggiungibile senza toccare questo file:
    // `LD_DEBUG_HUD 1` in boot_menu.cpp mostra `IRAM nnnK MAX nnnK` nel menu
    // LASER DISC.
    // ⭐ Gli avvisi di ERRORE VERO (buffer mancante, SD, ace.meta, task KO) NON
    // sono toccati: quelli devono restare, o il guasto torna muto
    // ([[feedback_audio_muto_e_ram_interna]] punto 5).
    // ------------------------------------------------------------------------
#define ACE_AVVISO_RIPIEGO 0
#if ACE_AVVISO_RIPIEGO
    if (ripiego) {
        ld_screen((ripiego & 2) ? "Z80 64K IN PSRAM" : "AUDMIX IN PSRAM", numeri, 0xE0FF);
        vTaskDelay(pdMS_TO_TICKS(1500));
    }
#else
    (void)ripiego;
#endif
    video.waitDMA();
    if (!sd_is_mounted() && !sd_mount()) { ld_screen("ERRORE", "SD NON MONTATA", 0x00F8); return false; }
    for (int i = 0; i < ACE_OC_N; i++) { ace_v[i].fp = nullptr; ace_v[i].idx = nullptr; ace_v[i].seg = -1; }
    for (int i = 0; i < ACE_AC_N; i++) { ace_a[i].fp = nullptr; ace_a[i].seg = -1; }
    memset(ace_apcm_none, 0, sizeof(ace_apcm_none));
    if (!ace_load_meta()) { ld_screen("ERRORE", "MANCA /ace/ace.meta", 0x00F8); return false; }
    return true;
}

// ROM: carica + patch fastboot/coinage + reset Z80
static void ace_rom_boot(void) {
    memset(ace_z80mem, 0, 0x10000);
    memcpy(ace_z80mem, ACE_ROM, ACE_ROM_SIZE);
#if ACE_FASTBOOT
    ace_z80mem[0x121B] = 0x00; ace_z80mem[0x123A] = 0x00;
#endif
#if ACE_ONE_COIN
    ace_z80mem[0x1A50] = 0x00;
#endif
    ResetZ80(&cpu);
    cpu.IRequest = INT_NONE; cpu.IAutoReset = 1; cpu.TrapBadOps = 0; cpu.Trace = 0;
    irq_acc = 0; ldv_cur = 0; ldv_seek = 0; ldv_acc = 0; ldv_play = false; ldv_out = 0xFC; ldv_sp = 0;
    ldv_clear();
}

// ============================================================ TASK ROM (core0)
// Il ROM (emulazione Z80 pesante: 66666 cicli/campo) gira SEPARATO dal video, a 60Hz
// PRECISO (pacing next_us, come il CABINATONE). Cosi' il disco (ldv_cur) avanza in
// tempo reale e video/audio non rallentano quando il decode JPEG e' lento. Il video
// (CPU1) legge ldv_cur (volatile, cross-core). Uscita = combo del firmware (START 3s).
static TaskHandle_t g_rom_task = nullptr;
static void ace_rom_task(void *arg) {
    (void)arg;
    int64_t next_us = esp_timer_get_time();
    while (!g_stop) {
        ace_read_inputs();
        ace_run_field();
        if (input_consume_back_to_menu()) { g_stop = true; break; }
        next_us += 16667;                           // 60 campi/s
        int64_t now = esp_timer_get_time();
        if (next_us > now) vTaskDelay(pdMS_TO_TICKS((int)((next_us - now) / 1000) + 1));
        // ⛔⛔⛔ QUI C'ERA `taskYIELD()`, ED E' IL RIAVVIO NELL'ATTRACT.
        // `taskYIELD()` cede la CPU ai soli task di priorita' UGUALE O
        // SUPERIORE: questo task e' a **5**, il task IDLE e' a **0**, quindi
        // IDLE0 non gira MAI. Finche' `ace_run_field()` sta nei 16,667 ms si
        // passa dal ramo sopra (`vTaskDelay`) e non si vede niente; appena
        // sfora — e sfora se `ace_z80mem` finisce in PSRAM, perche' sono 66.666
        // cicli Z80 per campo con ogni accesso fuori chip — si cade QUI a ogni
        // giro, il task resta al 100% su core 0, IDLE0 non e' piu' schedulato e
        // dopo 5 s scatta il **TASK WATCHDOG**: il dispositivo si riavvia.
        // E' esattamente la catena di [[feedback_task_audio_indietro_watchdog]]:
        // «il task gira al 100% a priorita' alta sul core 0, l'idle non e' mai
        // schedulato -> TASK WATCHDOG dopo 5 s -> LA SCHEDA SI RIAVVIA».
        //
        // ⭐⭐ E la cura era gia' SCRITTA in questo progetto, con queste parole,
        // in `neogeo.cpp:2782`: «⚠️ Respiro per il watchdog: vTaskDelay(1), non
        // taskYIELD() (che NON alimenta il task IDLE)». Anche `emulation.cpp` ha
        // il suo do-while «per garantire SEMPRE almeno un tick anche se
        // run_frame sfora», e `audio_task` ha la sua rete di sicurezza. Questo
        // player era rimasto indietro.
        // ⚠️ Un tick perso su un giro GIA' in ritardo non cambia il ritmo: si
        // paga solo quando si e' gia' fuori budget.
        else { next_us = now; vTaskDelay(1); }
    }
    g_rom_task = nullptr;
    vTaskDelete(nullptr);
}

// ============================================================ corpo player (VIDEO, CPU1 = unico writer SPI)
static void ace_play_task(void *arg) {
    (void)arg;
    ld_window(true);
    if (!ace_init()) { vTaskDelay(pdMS_TO_TICKS(1500)); goto done; }

    if (ace_select_difficulty()) {
        ace_rom_boot();
        ace_load_meta();

        g_running = true;
        // ⛔⛔ UN `xTaskCreate*` CHE PUO' FALLIRE VA SEMPRE CONTROLLATO. Questi
        // due non lo erano: senza `ace_rom` il disco non avanza mai e il gioco
        // resta appeso nell'attract, senza `ace_aud` sparisce il suono — e in
        // nessuno dei due casi qualcuno lo dice. Un guasto senza voce si
        // manifesta a valle e nomina la cosa sbagliata
        // ([[feedback_stack_task_prenotato_per_primo]], punto 2: la stessa
        // rottura, col controllo, diventa «TASK KO IRAM 25K» — causa e numero).
        // ⚠️ Gli stack dei task stanno in RAM INTERNA e NON ripiegano in PSRAM:
        // se qui si fallisce, la memoria non c'e' e basta.
        const bool ok_aud = (xTaskCreatePinnedToCore(ace_audio_task, "ace_aud", 8192,
                                 nullptr, 6, nullptr, 0) == pdPASS);
        const bool ok_rom = (xTaskCreatePinnedToCore(ace_rom_task, "ace_rom", 16384,
                                 nullptr, 5, &g_rom_task, 0) == pdPASS);
        if (!ok_aud || !ok_rom) {
            char n2[32];
            snprintf(n2, sizeof n2, "INT %uK MAX %uK",
                     (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                     (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
            ld_screen(ok_rom ? "TASK AUDIO KO" : "TASK ROM KO", n2, 0x00F8);
            vTaskDelay(pdMS_TO_TICKS(2500));
            g_running = false;
            g_stop = true;
        }

        // VIDEO: SOLO decode+blit, guidato da ldv_cur (che il ROM avanza a 60Hz su core0).
        int shown = -1;
        while (!g_stop) {
            int mf = ace_ld_to_mjpg((int)ldv_cur);
            if (mf != shown) { ace_show_frame(mf); shown = mf; }
            else              vTaskDelay(pdMS_TO_TICKS(4));   // nessun frame nuovo -> cedi
        }

        for (int i = 0; i < 100 && g_rom_task; i++) vTaskDelay(pdMS_TO_TICKS(10));  // ferma ROM
        g_running = false;
        vTaskDelay(pdMS_TO_TICKS(60));                                              // ferma audio
    }

    ace_close_segments();

done:
#if LIBERA_BUFFER_ALL_USCITA
    // ⭐⭐⭐ SPACE ACE RESTITUISCE I SUOI 81 KB DI RAM INTERNA.
    //     ace_z80mem  65.536      ace_apf  16.384      audmix  1.024
    // Il commento lungo sopra `ace_init()` diceva: *«curarlo alla nascita
    // vorrebbe dire liberare i buffer di ogni gioco all'uscita — venti file sani
    // da toccare»*, e sceglieva invece il ripiego in PSRAM. Il referto dell'utente
    // — *«ogni volta che si apre un gioco e si chiude si accumula buffer»* — dice
    // che la cura alla nascita e' quella giusta, e i file da toccare erano
    // CINQUE, non venti (il conto e' in `config.h`). Questo player e' il caso
    // curioso: era la VITTIMA del difetto (non trovava piu' 64 KB contigui e si
    // riavviava nell'attract) ed era anche uno dei colpevoli, perche' quei 64 KB
    // se li teneva a sua volta per il gioco dopo.
    //
    // ⚠️ Il posto e' `done:`, cioe' l'uscita di TUTTE le strade: quella normale
    // (dove i due task, ROM e audio, sono gia' fermi qualche riga sopra) e il
    // `goto done` di `ace_init()` fallita, dove i task non sono mai partiti ma
    // qualche buffer puo' essere gia' allocato.
    // ⚠️ Tutte e tre le allocazioni sono dietro un `if (!ptr)`: azzerando i
    // puntatori si rifanno da sole al rientro nel player.
    if (ace_z80mem) { heap_caps_free(ace_z80mem); ace_z80mem = nullptr; }
    if (ace_apf)    { heap_caps_free(ace_apf);    ace_apf    = nullptr; }
    if (audmix)     { heap_caps_free(audmix);     audmix     = nullptr; }
    ace_apf_pos = -1; ace_apf_len = 0;   // il prefetch riparte vuoto, non a meta'
#endif
    // pulisci a nero l'intera finestra player e torna alla finestra menu
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    ld_window(false);
    g_done = true;
    vTaskDelete(nullptr);
}

// ============================================================ entry
bool spaceace_play(Input *input, unsigned short *out_strip) {
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;
    if (xTaskCreatePinnedToCore(ace_play_task, "ace_play", LD_TASK_STACK, nullptr, 2, nullptr, 1) != pdPASS)
        return false;
    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

// il core Z80 di ace vive IN QUESTA TU (simboli Ace* per non collidere col Z80 SPINNERINO).
// extern "C" = linkage C, come le dichiarazioni in Z80.h; `register` neutralizzato
// (rimosso in C++17, il core Fayzullin lo usa come storage-class).
#define register
extern "C" {
#include "cpus/z80ace/Z80ace.c"
}
#undef register
