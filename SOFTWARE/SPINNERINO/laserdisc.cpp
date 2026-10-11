// ============================================================================
// laserdisc.cpp — DRAGON'S LAIR da microSD.  ⭐ FASE 2c = VIDEO + AUDIO (2026-07-16)
//
// Engine DirkSimple (dl_game.cpp) + player video JPEGDEC + AUDIO da .pcm.
// Attract -> COIN -> START -> gameplay (joystick + FIRE=spada) con musica/voce.
// Ancora SENZA: scelta difficolta' (vite=5 fisse), frecce-aiuto, barra volume.
//
// ARCHITETTURA (lezioni 2026-07-16, 2a/2b confermate sul device):
//  - VIDEO in TASK su CPU1 (core del loopTask/SPI-init; MAI core 0). Unico
//    scrittore SPI. waitDMA() prima di setAddrWindow/fread.
//  - AUDIO in TASK su CPU0 (non tocca il display), legge i .pcm dalla SD e scrive
//    l'I2S con write BLOCCANTE = il CLOCK di riproduzione (il video insegue).
//  - SD letta da ENTRAMBI i task: FATFS e' RIENTRANTE su questo build
//    (CONFIG_FATFS_TIMEOUT_MS) e SDMMC ha DMA proprio separato dal GDMA dello SPI
//    -> nessun conflitto (verificato su sdkconfig P4).
//  - Stato condiviso VIDEO<->AUDIO: SOLO 32-bit atomici (g_seek_seq/ready_seq/
//    seek_frame/seek_still). Ogni task ha il PROPRIO orologio int64 locale.
//  - Decoder SOLO software JPEGDEC. Frame 176x240 pre-ruotati: blit diretto,
//    niente byte-swap (RGB565_BIG_ENDIAN).
//
// ⚠️ NIENTE i2s_set_sample_rates(): l'I2S e' gia' a 32 kHz nativi (= rate dei .pcm)
//    e quell'API legacy al link non-boota. Il ponte (audio.cpp) mette in BYPASS il
//    task audio del firmware mentre giriamo.
// ⛔ NON includere "emulation/audio.h".
// ============================================================================
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/laserdisc.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"

#include "dl_game.h"
#include "lair_framefile.h"
#include "dl_sfx.h"                                 // beep del cabinato (SFX_*)
#include <JPEGDEC.h>

extern Video video;
extern bool  input_consume_back_to_menu();
extern bool  input_is_vol_mode_active(void);       // vol mode (FIRE/COIN 3s), come i giochi
// ponte audio (audio.cpp): il player e' l'unico writer I2S mentre gira.
extern void laserdisc_audio_begin(void);
extern void laserdisc_audio_end(void);
extern void laserdisc_audio_write(const int16_t *stereo, int frames);
extern int  laserdisc_current_volume(void);       // .ino: audio.volumeSetting (1..30)

// ------------------------------------------------------------- parametri
#define LD_W          176
#define LD_H          240
#define LD_X_OFF      72
#define LD_FB_H       240
#define LD_IN_MAX     (48*1024)
#define LD_MAX_FR     8192
#define LD_MAX_CLIPS  256
#define LD_TASK_STACK 32768
#define OC_N          4

static const double DL_LD_FPS   = 23.976;   // il DISCO scandisce il TEMPO
static const double DL_CLIP_FPS = 25.0;     // i .mjpg/.pcm sono a 25 fps
#define DL_CLIP_FPS_I 25
#define DEC_MIN_US    (1000000 / DL_CLIP_FPS_I)          // 40000
#define AUD_PER_FRAME (32000 / DL_CLIP_FPS_I)            // 1280 campioni stereo/frame
#define AUD_BYTES     ((size_t)AUD_PER_FRAME * 2 * sizeof(int16_t))

// -------------------------------------------------------------- globali
static uint16_t *g_strip = nullptr;
static uint8_t  *g_in  = nullptr;
static uint8_t  *g_out = nullptr;
static int16_t  *audchunk = nullptr;        // buffer audio (RAM interna)
static JPEGDEC   g_jdec;

// beep del cabinato: il task GIOCO (dl_cb_play_sound) li arma, il task AUDIO li
// mixa. Cross-core: solo 32-bit atomici (puntatore + int).
static const int16_t * volatile g_sfx = nullptr;
static volatile int g_sfx_pos = 0, g_sfx_len = 0, g_sfx_gain = 2;

struct ClipInfo { char name[16]; uint32_t nfr; uint32_t *pairs; };
static ClipInfo         g_clips[LD_MAX_CLIPS];
static const ClipInfo  *g_clip_ff[LD_MAX_CLIPS];
static int              g_nclip = 0;
static const ClipInfo  *g_cur_clip = nullptr;

struct OpenCache { FILE *f[OC_N]; const ClipInfo *who[OC_N]; uint32_t used[OC_N];
                   uint32_t tick; const char *ext; };
static OpenCache g_vid_oc, g_aud_oc;

static volatile uint32_t g_seek_seq   = 0;
static volatile uint32_t g_ready_seq  = 0xFFFFFFFFu;
static volatile int      g_seek_frame = 0;
static volatile bool     g_seek_still = false;
static volatile bool     g_running = false;    // task audio attivo
static volatile bool     g_stop = false;       // loopTask -> video: esci
static volatile bool     g_done = false;       // video -> loopTask: finito

static int64_t g_boot_us = 0, g_clock_us = 0;  // SOLO task video/gioco
static bool    g_clock_pending = false;
static int     g_credits = 0;
static int     dl_vff = -1, dl_nfr = 0, dl_local = 0;
static Input  *g_input = nullptr;
static TaskHandle_t g_aud_task = nullptr;

static uint32_t vs_myseq = 0xFFFFFFFFu;
static int64_t  vs_open_us = 0, vs_last_dec = 0;
static int      vs_shown = -1;

// ------------------------------------------------------- overlay ruotato
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

// ---------------------------------------------------------- finestra / testo
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
static void ld_fatal(const char *m) { ld_screen("ERRORE", m, 0x00F8); }

// -------------------------------------------------- barra volume (banda nera alta)
// Il video e' letterboxed: 176px centrati nella finestra da 320 -> bande nere ai
// window cols 0..71 (BORDO ALTO del pannello) e 248..319 (basso). ld_px_vis copre
// solo l'area video (cols 72..247), quindi la barra la scriviamo DIRETTAMENTE in
// g_strip dentro ld_blit, nella banda alta = il piu' in alto possibile sul display.
//   lunghezza = window rows (orizzontale come si vede), centrata; spessore = window
//   cols vicino a 0. ⚠️ se comparisse in BASSO, e' un flip: usare (319-col).
#define VOLBAR_LEN   112
#define VOLBAR_ROW0  ((240 - VOLBAR_LEN) / 2)   // 64: centrata orizzontalmente
#define VOLBAR_COL0  3                          // vicino al bordo alto (window col 0): +3px su
#define VOLBAR_THK   16
#define VOLBAR_RED   10
static int g_volbar_fill = -1;                  // -1 = off; 0..VOLBAR_LEN = riempimento

// -------------------------------------------------------------- blit
static void ld_blit(void) {
  const uint16_t *img = (const uint16_t *)g_out;
  for (int s = 0; s < LD_FB_H / 8; s++) {
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int i = 0; i < 8; i++) {
      int src_y = s * 8 + i; if (src_y < 0 || src_y >= LD_H) continue;
      memcpy(g_strip + i * TFT_FB_W + LD_X_OFF,
             img + (size_t)src_y * LD_W, (size_t)LD_W * sizeof(uint16_t));
    }
    // barra volume nella banda nera alta (se vol mode attivo)
    if (g_volbar_fill >= 0) {
      for (int i = 0; i < 8; i++) {
        int pos = (s * 8 + i) - VOLBAR_ROW0;            // posizione fisica lungo window rows
        if (pos < 0 || pos >= VOLBAR_LEN) continue;
        int asx = VOLBAR_LEN - 1 - pos;                 // ⭐ INVERTITO: riempimento sx->dx come i giochi
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

// ----------------------------------------------------- decode JPEGDEC -> g_out
static int dl_jpeg_draw(JPEGDRAW *p) {
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

// ------------------------------------------------------- indici .idx / cache
static int dl_load_clip_idx(const char *name) {
  for (int i = 0; i < g_nclip; i++)
    if (strncmp(g_clips[i].name, name, 16) == 0) return i;
  if (g_nclip >= LD_MAX_CLIPS) return -1;
  char path[80];
  snprintf(path, sizeof(path), SD_MOUNT_POINT "/dl/%s.idx", name);
  FILE *f = fopen(path, "rb"); if (!f) return -1;
  uint32_t nfr = 0;
  if (fread(&nfr, 4, 1, f) != 1 || nfr == 0 || nfr > LD_MAX_FR) { fclose(f); return -1; }
  uint32_t *pairs = (uint32_t *)heap_caps_malloc((size_t)nfr * 8, MALLOC_CAP_SPIRAM);
  if (!pairs || fread(pairs, 8, nfr, f) != nfr) {
    if (pairs) heap_caps_free(pairs); fclose(f); return -1;
  }
  fclose(f);
  ClipInfo *c = &g_clips[g_nclip];
  strncpy(c->name, name, 15); c->name[15] = 0; c->nfr = nfr; c->pairs = pairs;
  return g_nclip++;
}
static bool dl_load_all_idx(void) {
  for (int ff = 0; ff < DL_FRAMEFILE_N; ff++) {
    int idx = dl_load_clip_idx(DL_FRAMEFILE[ff].clip);
    g_clip_ff[ff] = (idx >= 0) ? &g_clips[idx] : nullptr;
  }
  return g_nclip > 0;
}
// Cache LRU di handle (una per estensione). g_vid_oc usata SOLO dal task video,
// g_aud_oc SOLO dal task audio -> nessuna race sulla stessa struttura.
static FILE *oc_get(OpenCache *oc, const ClipInfo *c) {
  if (!c) return nullptr;
  oc->tick++;
  for (int i = 0; i < OC_N; i++)
    if (oc->who[i] == c) { oc->used[i] = oc->tick; return oc->f[i]; }
  int lru = 0;
  for (int i = 1; i < OC_N; i++) if (oc->used[i] < oc->used[lru]) lru = i;
  if (oc->f[lru]) fclose(oc->f[lru]);
  char path[80];
  snprintf(path, sizeof(path), SD_MOUNT_POINT "/dl/%s%s", c->name, oc->ext);
  oc->f[lru]   = fopen(path, "rb");
  oc->who[lru] = oc->f[lru] ? c : nullptr;
  oc->used[lru] = oc->tick;
  return oc->f[lru];
}
static void oc_close_all(OpenCache *oc) {
  for (int i = 0; i < OC_N; i++) {
    if (oc->f[i]) fclose(oc->f[i]);
    oc->f[i] = nullptr; oc->who[i] = nullptr; oc->used[i] = 0;
  }
}

// --------------------------------------------------------- mappa tempo->frame
static inline int dl_frame_at(int64_t open_us) {
  if (g_seek_still) return g_seek_frame;
  return g_seek_frame + (int)((double)(esp_timer_get_time() - open_us) / 1e6 * DL_LD_FPS);
}
static inline int dl_local_frame(int ff, int ld) {
  int lld = ld - DL_FRAMEFILE[ff].ld_start; if (lld < 0) lld = 0;
  return (int)(lld * (DL_CLIP_FPS / DL_LD_FPS) + 0.5);
}

// -------------------------------------------------------- scoreboard minimo
static void dl_draw_scoreboard(uint16_t *fb) {
  char line[40];
  if (dl_game_in_attract()) {
    if (dl_game_gameover()) snprintf(line, sizeof(line), "GAME OVER  CREDIT %02d", g_credits);
    else                    snprintf(line, sizeof(line), "CREDIT %02d  PRESS START", g_credits);
  } else {
    snprintf(line, sizeof(line), "%06d  DIRK %d  CR %02d",
             dl_game_score(), dl_game_lives(), g_credits);
  }
  ld_text_vis(fb, 4, 3, line, 0xFFFF);
}

// -------------------------------------------------------- barra volume
// La barra e' disegnata nella BANDA NERA ALTA da ld_blit (vedi sopra). Qui si
// calcola solo il riempimento dal volume corrente (audio.volumeSetting via
// laserdisc_current_volume()); la REGOLAZIONE la fa buttons_get() del firmware.
static void dl_update_volbar(void) {
  if (!input_is_vol_mode_active()) { g_volbar_fill = -1; return; }
  int f = (VOLBAR_LEN * (31 - laserdisc_current_volume())) / 30;
  if (f < 0) f = 0; else if (f > VOLBAR_LEN) f = VOLBAR_LEN;
  g_volbar_fill = f;
}

// -------------------------------------------------- AIUTO frecce / spada
// Geometria copiata dal MAIN (matematica pura sul frame gia' ruotato). Solo il
// CONTORNO (riempire coprirebbe il video). Centrato in basso al video.
static bool g_hints_on = false;     // lo setta dl_select_difficulty (ON solo in FACILE)
#define HINT_R   20
#define HINT_CX  120                // 240/2 (LD_VIS_W)
#define HINT_CY  152                // 176-24 (LD_VIS_H - 24)
static inline bool dl_arrow_in(int x, int y, int R, int dx, int dy) {
  const double inv = (dx && dy) ? 0.7071067811865476 : 1.0;
  double t = (x * dx + y * dy) * inv, s = (x * dy - y * dx) * inv;
  if (t >= -R && t <= 0.0)       return (s > -0.25 * R && s < 0.25 * R);
  if (t > 0.0 && t <= (double)R) { double hw = (R - t) * 0.95; return (s > -hw && s < hw); }
  return false;
}
static inline bool dl_sword_in(int x, int y, int R) {
  if (x >= -3       && x <= 3       && y >= -R    && y <= R - 9) return true;
  if (x >= -(R - 8) && x <= (R - 8) && y >=  2    && y <= 7)     return true;
  if (x >= -6       && x <= 6       && y >= R - 9 && y <= R - 3) return true;
  return false;
}
static void dl_arrow_outline(uint16_t *b, int cx, int cy, int R, int dx, int dy, uint16_t col) {
  for (int y = -R; y <= R; y++)
    for (int x = -R; x <= R; x++) {
      if (!dl_arrow_in(x, y, R, dx, dy)) continue;
      bool bd = false;
      for (int k = 1; k <= 2 && !bd; k++)
        bd = !dl_arrow_in(x - k, y, R, dx, dy) || !dl_arrow_in(x + k, y, R, dx, dy) ||
             !dl_arrow_in(x, y - k, R, dx, dy) || !dl_arrow_in(x, y + k, R, dx, dy);
      if (bd) ld_px_vis(b, cx + x, cy + y, col);
    }
}
static void dl_sword_outline(uint16_t *b, int cx, int cy, int R, uint16_t col) {
  for (int y = -R; y <= R; y++)
    for (int x = -R; x <= R; x++) {
      if (!dl_sword_in(x, y, R)) continue;
      bool bd = false;
      for (int k = 1; k <= 2 && !bd; k++)
        bd = !dl_sword_in(x - k, y, R) || !dl_sword_in(x + k, y, R) ||
             !dl_sword_in(x, y - k, R) || !dl_sword_in(x, y + k, R);
      if (bd) ld_px_vis(b, cx + x, cy + y, col);
    }
}
static void dl_draw_hint(uint16_t *fb) {
  if (!g_hints_on) return;
  int h = dl_game_move_hint();
  if (h == 0) return;
  if (h == IN_ACTION) { dl_sword_outline(fb, HINT_CX, HINT_CY, HINT_R, 0xFFFF); return; }
  int dx = 0, dy = 0;
  switch (h) {
    case IN_UP:   dy = -1; break;                 case IN_DOWN:  dy = 1; break;
    case IN_LEFT: dx = -1; break;                 case IN_RIGHT: dx = 1; break;
    case IN_UPLEFT:   dx = -1; dy = -1; break;    case IN_UPRIGHT:   dx = 1; dy = -1; break;
    case IN_DOWNLEFT: dx = -1; dy =  1; break;    case IN_DOWNRIGHT: dx = 1; dy =  1; break;
    default: return;
  }
  dl_arrow_outline(fb, HINT_CX, HINT_CY, HINT_R, dx, dy, 0xFFFF);
}

// ------------------------------------------------- decode + mostra (task VIDEO)
static void dl_show(void) {
  video.waitDMA();                          // reap DMA SPI del video prima della SD
  FILE *fmj = oc_get(&g_vid_oc, g_cur_clip);
  if (!fmj || !g_cur_clip || dl_local < 0 || dl_local >= (int)g_cur_clip->nfr) return;
  uint32_t off = g_cur_clip->pairs[2 * dl_local], len = g_cur_clip->pairs[2 * dl_local + 1];
  if (len == 0 || len > LD_IN_MAX) return;
  if (fseek(fmj, (long)off, SEEK_SET) != 0) return;
  if (fread(g_in, 1, len, fmj) != len) return;
  int ok = 0;
  if (g_jdec.openRAM(g_in, (int)len, dl_jpeg_draw)) {
    g_jdec.setPixelType(RGB565_BIG_ENDIAN);   // solarizzato? -> RGB565_LITTLE_ENDIAN
    ok = g_jdec.decode(0, 0, 0);
    g_jdec.close();
  }
  if (!ok) return;
  dl_draw_scoreboard((uint16_t *)g_out);      // score nell'area video (in alto)
  dl_draw_hint((uint16_t *)g_out);            // frecce / spada di aiuto
  dl_update_volbar();                         // fill barra (la disegna ld_blit nella banda alta)
  ld_blit();
}

// -------------------------------------------- CALLBACK del motore (dl_game.cpp)
static void dl_seek(int32_t ms, bool still) {
  int N = (int)((ms + 6297.0) / 1000.0 * DL_LD_FPS + 0.5);
  if (N < 0) N = 0;
  g_seek_frame = N; g_seek_still = still;
  g_clock_pending = true;
  g_seek_seq = g_seek_seq + 1;
}
extern "C" void dl_cb_start_clip(int32_t start_ms) { dl_seek(start_ms, false); }
extern "C" void dl_cb_show_still(int32_t start_ms) { dl_seek(start_ms, true);  }
extern "C" void dl_cb_play_sound(int snd) {
  if      (snd == SND_ACCEPT) { g_sfx = SFX_ACCEPT; g_sfx_len = SFX_ACCEPT_LEN; g_sfx_gain = 1; }  // mosse abbassate (era 2)
  else if (snd == SND_REJECT) { g_sfx = SFX_BUZZ;   g_sfx_len = SFX_BUZZ_LEN;   g_sfx_gain = 1; }  // mosse abbassate (era 2)
  else                        { g_sfx = SFX_CREDIT; g_sfx_len = SFX_CREDIT_LEN; g_sfx_gain = 1; }
  g_sfx_pos = 0;
}

// -------------------------------------------------------------- input
static void build_inputs(unsigned char b, dl_inputs_t *in) {
  static uint16_t prev_dir = 0;
  static bool prev_coin = false, prev_start = false;
  uint16_t held = 0;
  if (b & BUTTON_UP)    held |= (1 << IN_UP);
  if (b & BUTTON_DOWN)  held |= (1 << IN_DOWN);
  if (b & BUTTON_LEFT)  held |= (1 << IN_LEFT);
  if (b & BUTTON_RIGHT) held |= (1 << IN_RIGHT);
  if (b & BUTTON_FIRE)  held |= (1 << IN_ACTION);
  in->coinslot = 0;
  // ⚠️ in vol mode NON contare la moneta: tenere COIN 3s apre il vol mode, e il
  // fronte di rilascio farebbe scattare un credito fantasma. prev_vm copre il
  // frame in cui si esce dal vol mode.
  static bool prev_vm = false;
  bool vm = input_is_vol_mode_active();
  bool coin = (b & BUTTON_COIN) != 0;
  if (!vm && !prev_vm && !coin && prev_coin && g_credits < 99) { g_credits++; in->coinslot = 1; }
  prev_coin = coin; prev_vm = vm;
  if ((held & (1 << IN_UP))   && (held & (1 << IN_LEFT)))  held |= (1 << IN_UPLEFT);
  if ((held & (1 << IN_UP))   && (held & (1 << IN_RIGHT))) held |= (1 << IN_UPRIGHT);
  if ((held & (1 << IN_DOWN)) && (held & (1 << IN_LEFT)))  held |= (1 << IN_DOWNLEFT);
  if ((held & (1 << IN_DOWN)) && (held & (1 << IN_RIGHT))) held |= (1 << IN_DOWNRIGHT);
  uint16_t pressed = held & ~prev_dir;
  prev_dir = held;
  bool start = (b & BUTTON_START) != 0;
  if (start && !prev_start) {
    if (dl_game_in_attract()) { if (g_credits > 0) { g_credits--; pressed |= (1 << IN_START); } }
    else                      { pressed |= (1 << IN_START); }
  }
  prev_start = start;
  in->held    = held | (start ? (1 << IN_START) : 0);
  in->pressed = pressed;
}

// --------------------------------------------------------- passo video
static bool dl_open_clip_ff(int ff) {
  g_cur_clip = (ff >= 0 && ff < DL_FRAMEFILE_N) ? g_clip_ff[ff] : nullptr;
  dl_vff = ff;
  dl_nfr = g_cur_clip ? (int)g_cur_clip->nfr : 0;
  return g_cur_clip != nullptr;
}
static void dl_video_reset_state(void) {
  vs_myseq = 0xFFFFFFFFu; vs_open_us = 0; vs_last_dec = 0; vs_shown = -1;
}
static void dl_video_step(void) {
  uint32_t seq = g_seek_seq;
  if (seq != vs_myseq) {
    vs_myseq = seq;
    int ff = dl_framefile_find(g_seek_frame); if (ff < 0) ff = 0;
    dl_open_clip_ff(ff);
    int local = dl_local_frame(ff, g_seek_frame);
    if (g_cur_clip && local >= 0 && local < dl_nfr) { dl_local = local; dl_show(); }
    vs_open_us = esp_timer_get_time();
    vs_shown = local;
    g_ready_seq = vs_myseq;
  }
  int ld = dl_frame_at(vs_open_us);
  int ff = dl_framefile_find(ld); if (ff < 0) ff = 0;
  if (ff != dl_vff) { dl_open_clip_ff(ff); vs_shown = -1; }
  int local = dl_local_frame(ff, ld);
  int64_t now = esp_timer_get_time();
  if (g_cur_clip && local != vs_shown && local >= 0 && local < dl_nfr &&
      (now - vs_last_dec) >= DEC_MIN_US) {
    vs_shown = local; dl_local = local; dl_show(); vs_last_dec = now;
  }
}

// ------------------------------------------------- TASK AUDIO (CPU0)
// Legge i .pcm al frame del disco corrente e li scrive sull'I2S (BLOCCANTE =
// clock). Volume come i giochi (1=max, >=30=mute). Orologio LOCALE (open_us).
static void audio_write_mix(int frames) {
  // 1) mixa il beep del cabinato (mono -> entrambi i canali; scena a meta' scala).
  // SFX_SHIFT abbassa gli effetti: col boost ×4 sul totale erano troppo forti.
  // Alzare SFX_SHIFT per abbassarli ancora (1 = meta', 2 = un quarto).
  #define SFX_SHIFT 1
  if (g_sfx && g_sfx_pos < g_sfx_len) {
    const int16_t *s = g_sfx; int pos = g_sfx_pos, len = g_sfx_len, gain = g_sfx_gain;
    for (int i = 0; i < frames && pos < len; i++) {
      int t = ((int)s[pos++] * gain) >> SFX_SHIFT;
      int l = (audchunk[2 * i] >> 1) + t, r = (audchunk[2 * i + 1] >> 1) + t;
      if (l > 32767) l = 32767; else if (l < -32768) l = -32768;
      if (r > 32767) r = 32767; else if (r < -32768) r = -32768;
      audchunk[2 * i] = (int16_t)l; audchunk[2 * i + 1] = (int16_t)r;
    }
    g_sfx_pos = pos;
  }
  // 2) volume (scala dei giochi: 1=max, >=30=mute) + BOOST + write BLOCCANTE = clock.
  // AUD_BOOST: i .pcm hanno RMS ~5% (misurato); i giochi al max clampano a fondo
  // scala (valueToBuffer: value*128/vol). Qui ~4x a barra piena (con clamp sui
  // picchi, come i giochi). Se distorce ("gracchia") abbassare a 3; se ancora
  // basso alzare a 5-6.
  #define AUD_BOOST 4
  // LD_TRIM_PCT: attenuazione GENERALE del player (utente 2026-07-18).
  // 72 = -28% sull'originale. Storia della taratura, in sessione:
  //   100 (originale) -> 70 ("-30%") -> 60 (fix gracchio) -> 72 (+20% richiesto).
  // MISURATO sui .pcm veri (dls00..dls05, script measure_pcm.py):
  //   RMS sorgente 3.3..4.9% FS, picchi 52..70% FS (crest factor ~23 dB).
  // Con TRIM 72 il gain a barra piena e' 2.88x -> RMS in uscita ~9-14% FS,
  // allineato a Space Ace (che con ACE_TRIM_PCT 50 sta sullo stesso livello).
  #define LD_TRIM_PCT 72
  // LD_LIM_T: soglia del SOFT-LIMITER. Il gracchio NON era volume alto in se':
  // era l'hard clamp a +-32767. Coi picchi al 70% FS e gain 2.4x si arriva a
  // ~168% fondo scala -> il clamp tagliava la cima dell'onda (distorsione
  // squadrata, udibile come "gracchio" sulle voci forti). Sotto soglia il
  // segnale passa INTATTO; sopra viene compresso con continuita' fino a 32767,
  // cosi' i picchi restano forti ma non squadrati. I .pcm sorgente sono puliti
  // (misurato: zero run di campioni saturi), quindi il difetto era tutto qui.
  #define LD_LIM_T 24000
  int vs = laserdisc_current_volume();
  int vol = (vs >= 30) ? 0 : (31 - vs);           // 0..30 (30 = barra piena)
  if (vol <= 0) memset(audchunk, 0, (size_t)frames * 2 * sizeof(int16_t));
  else {
    // fondo scala in INGRESSO al limiter col gain corrente: scala col volume,
    // cosi' a barra bassa il limiter e' inerte (nessun campione supera la soglia).
    int in_max = (32767 * vol * AUD_BOOST * LD_TRIM_PCT) / (30 * 100);
    if (in_max <= LD_LIM_T) in_max = LD_LIM_T + 1;   // evita divisione per zero
    for (int i = 0; i < frames * 2; i++) {
      int v = ((int)audchunk[i] * vol * AUD_BOOST * LD_TRIM_PCT) / (30 * 100);
      int a = (v < 0) ? -v : v;
      if (a > LD_LIM_T) {                            // compressione dei soli picchi
        a = LD_LIM_T + ((a - LD_LIM_T) * (32767 - LD_LIM_T)) / (in_max - LD_LIM_T);
        if (a > 32767) a = 32767;
        v = (v < 0) ? -a : a;
      }
      audchunk[i] = (int16_t)v;
    }
  }
  laserdisc_audio_write(audchunk, frames);
}
static void dl_audio_task(void *arg) {
  uint32_t myseq = 0xFFFFFFFFu; bool started = false; int64_t open_us = 0;
  while (g_running) {
    uint32_t seq = g_seek_seq;
    if (seq != myseq) { myseq = seq; started = false; }
    if (g_seek_still || g_ready_seq != myseq) {   // still o video non ancora pronto -> silenzio
      memset(audchunk, 0, AUD_BYTES); audio_write_mix(AUD_PER_FRAME); continue;
    }
    if (!started) { open_us = esp_timer_get_time(); started = true; }
    int ld = g_seek_frame + (int)((double)(esp_timer_get_time() - open_us) / 1e6 * DL_LD_FPS);
    int ff = dl_framefile_find(ld); if (ff < 0) ff = 0;
    const ClipInfo *c = (ff < DL_FRAMEFILE_N) ? g_clip_ff[ff] : nullptr;
    FILE *fp = oc_get(&g_aud_oc, c);
    int local = dl_local_frame(ff, ld);
    if (fp && c && local >= 0 && local < (int)c->nfr) {
      fseek(fp, (long)local * AUD_PER_FRAME * 2 * (long)sizeof(int16_t), SEEK_SET);
      size_t rd = fread(audchunk, sizeof(int16_t), (size_t)AUD_PER_FRAME * 2, fp);
      if (rd < (size_t)AUD_PER_FRAME * 2)
        memset((uint8_t *)audchunk + rd * sizeof(int16_t), 0, AUD_BYTES - rd * sizeof(int16_t));
      audio_write_mix(AUD_PER_FRAME);
    } else {
      memset(audchunk, 0, AUD_BYTES); audio_write_mix(AUD_PER_FRAME);
    }
  }
  g_aud_task = nullptr;
  vTaskDelete(nullptr);
}

// ------------------------------------------------ uscita: START tenuto 3s
static bool ld_want_exit(unsigned char b) {
  static uint32_t t0 = 0;
  if (input_consume_back_to_menu()) { t0 = 0; return true; }
  if (b & BUTTON_START) {
    uint32_t now = millis();
    if (t0 == 0)                 t0 = now ? now : 1;
    else if (now - t0 >= 3000) { t0 = 0; return true; }
  } else t0 = 0;
  return false;
}

// ---------------------------------------------------- init player (una volta)
static bool ld_init(void) {
  if (!g_in)  g_in  = (uint8_t *)heap_caps_malloc(LD_IN_MAX,               MALLOC_CAP_SPIRAM);
  if (!g_out) g_out = (uint8_t *)heap_caps_malloc((size_t)LD_W * LD_H * 2, MALLOC_CAP_SPIRAM);
  if (!audchunk) audchunk = (int16_t *)heap_caps_malloc(AUD_BYTES, MALLOC_CAP_INTERNAL);
  if (!g_in || !g_out || !audchunk) { ld_fatal("ALLOC BUFFER"); return false; }
  video.waitDMA();
  if (!sd_is_mounted() && !sd_mount()) { ld_fatal("SD NON MONTATA"); return false; }
  memset(&g_vid_oc, 0, sizeof(g_vid_oc)); g_vid_oc.ext = ".mjpg";
  memset(&g_aud_oc, 0, sizeof(g_aud_oc)); g_aud_oc.ext = ".pcm";
  if (DL_FRAMEFILE_N > LD_MAX_CLIPS) { ld_fatal("FRAMEFILE TROPPO GRANDE"); return false; }
  if (!dl_load_all_idx()) { ld_fatal("MANCANO I CLIP IN /DL"); return false; }
  return true;
}

// --------------------------------------------------- scelta difficolta'
// Testo SCALATO (ogni pixel font -> blocco scale x scale) nella strip corrente,
// stessa mappa ruotata di ld_screen (x_l = y_p, y_l = 239 - x_p). Centrato in x_p.
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
// Schermata scelta difficolta': titolo GIALLO grande + opzioni.
static void dl_diff_screen(void) {
  for (int sy = 0; sy < LD_FB_H; sy += 8) {
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    ld_line_strip(sy, "DRAGONS LAIR",       92,  2, 0xE0FF);   // titolo giallo (scala 2)
    ld_line_strip(sy, "SCEGLI DIFFICOLTA",  138, 1, 0xFF07);   // ciano
    ld_line_strip(sy, "SU = FACILE",        166, 1, 0xFFFF);
    ld_line_strip(sy, "GIU/FIRE = NORMALE", 184, 1, 0xFFFF);
    ld_line_strip(sy, "START 3s = ESCI",    208, 1, 0xEF7B);   // grigio
    video.write(g_strip, TFT_FB_W * 8);
  }
}

// SU = FACILE (aiuti ON + 10 vite) · GIU/FIRE = NORMALE (aiuti OFF + 5 vite).
// START 3s = esci al menu. Ritorna true se si gioca, false se si esce.
static bool dl_select_difficulty(Input *input) {
  bool released = false;    // FIRE lancia DL: ignora finche' non e' rilasciato una volta
  for (;;) {
    unsigned char b = input->buttons_get();
    if (!(b & (BUTTON_FIRE | BUTTON_START | BUTTON_UP | BUTTON_DOWN))) released = true;
    dl_diff_screen();
    if (ld_want_exit(b)) return false;                 // START 3s -> menu
    if (released && (b & BUTTON_UP)) { g_hints_on = true;  dl_game_set_lives(10); break; }
    if (released && (b & (BUTTON_DOWN | BUTTON_FIRE | BUTTON_START))) {
      g_hints_on = false; dl_game_set_lives(5); break;
    }
    vTaskDelay(pdMS_TO_TICKS(30));
  }
  // CONFERMA: modalita' scelta in VERDE, grande e centrata (come il sorgente).
  {
    const char *mode = g_hints_on ? "FACILE" : "NORMALE";
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
      memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
      ld_line_strip(sy, "MODALITA", 96,  2, 0xFFFF);   // header bianco
      ld_line_strip(sy, mode,       140, 3, 0xE007);   // modalita' VERDE (scala 3)
      video.write(g_strip, TFT_FB_W * 8);
    }
    vTaskDelay(pdMS_TO_TICKS(1200));
  }
  // debounce: attendi il rilascio (FIRE/START non devono trascinare nel gioco)
  while (input->buttons_get() & (BUTTON_UP | BUTTON_DOWN | BUTTON_FIRE | BUTTON_START))
    vTaskDelay(pdMS_TO_TICKS(20));
  return true;
}

// ------------------------------------------------------ corpo del player (VIDEO)
static void ld_play_task(void *arg) {
  ld_window(true);
  if (!ld_init()) { vTaskDelay(pdMS_TO_TICKS(1500)); goto done; }

  // Scelta difficolta' (setta vite + aiuti). START 3s qui = esci al menu.
  if (dl_select_difficulty(g_input)) {
    g_credits = 0;
    g_seek_seq = 0; g_ready_seq = 0xFFFFFFFFu; g_clock_pending = false;
    g_boot_us = esp_timer_get_time(); g_clock_us = g_boot_us;
    dl_video_reset_state();
    dl_game_reset();               // vite/aiuti gia' settati da dl_select_difficulty

    // AUDIO: bypassa il task audio del firmware + avvia il nostro (CPU0).
    laserdisc_audio_begin();
    g_running = true;
    xTaskCreatePinnedToCore(dl_audio_task, "dl_aud", 16384, nullptr, 6, &g_aud_task, 0);

    while (!g_stop) {
      unsigned char b = g_input->buttons_get();
      dl_inputs_t in; build_inputs(b, &in);
      if (ld_want_exit(b)) break;
      int64_t now = esp_timer_get_time();
      if (g_clock_pending && g_ready_seq == g_seek_seq) { g_clock_us = now; g_clock_pending = false; }
      uint32_t clip_ms  = g_clock_pending ? 0u : (uint32_t)((now - g_clock_us) / 1000);
      uint32_t total_ms = (uint32_t)((now - g_boot_us) / 1000);
      dl_game_tick(total_ms, clip_ms, &in);
      dl_video_step();
      vTaskDelay(pdMS_TO_TICKS(8));
    }

    g_running = false;                      // ferma il task audio
    for (int i = 0; i < 100 && g_aud_task; i++) vTaskDelay(pdMS_TO_TICKS(10));
    laserdisc_audio_end();
  }

  oc_close_all(&g_vid_oc); oc_close_all(&g_aud_oc);
  for (int i = 0; i < g_nclip; i++)
    if (g_clips[i].pairs) { heap_caps_free(g_clips[i].pairs); g_clips[i].pairs = nullptr; }
  g_nclip = 0; g_cur_clip = nullptr;
  memset(g_clip_ff, 0, sizeof(g_clip_ff));

done:
#if LIBERA_BUFFER_ALL_USCITA
  // DRAGON'S LAIR restituisce i suoi 5 KB di RAM INTERNA (`audchunk`, il buffer
  // di mix audio) — `g_in`/`g_out` restano perche' sono in PSRAM, che non e' la
  // risorsa contesa. E' l'ultimo pezzo del giro fatto su tutti i giochi (il
  // conto e' in `config.h`): il piu' piccolo, ma tenerlo fuori vorrebbe dire
  // lasciare in piedi l'eccezione che questo lavoro serviva a togliere.
  // ⚠️ Il task audio e' gia' fermo qui sopra (`g_running = false` + attesa di
  // `g_aud_task`), e `ld_init()` rialloca dietro un `if (!audchunk)`.
  if (audchunk) { heap_caps_free(audchunk); audchunk = nullptr; }
#endif
  // Pulisci a NERO l'intera finestra del player (240 righe): la finestra del menu
  // ne copre solo 232 (offset TFT_HW_Y=8), quindi le 8 righe di differenza (y=0..7)
  // restavano coi resti del video -> striscia verticale sul bordo che disturbava i
  // giochi. Qui la finestra e' ancora quella piena (ld_window(true) di inizio).
  memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
  for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
  ld_window(false);
  g_done = true;
  vTaskDelete(nullptr);
}

// ============================================================================
bool laserdisc_play(Input *input, unsigned short *out_strip) {
  g_strip = (uint16_t *)out_strip;
  g_input = input;
  g_stop  = false;
  g_done  = false;

  if (xTaskCreatePinnedToCore(ld_play_task, "dl_play", LD_TASK_STACK, nullptr,
                              2, nullptr, 1) != pdPASS) {
    return false;
  }
  while (!g_done) {
    input->updateBluetooth();
    vTaskDelay(pdMS_TO_TICKS(8));
  }
  return true;
}
