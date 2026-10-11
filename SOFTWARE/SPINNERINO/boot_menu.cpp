// ============================================================================
// boot_menu.cpp — BootMenu carousel con ROTAZIONE SOFTWARE (90 antiorario)
//
// Il display lavora in MAC=0x60 landscape (320x232) ma l'utente lo vede in
// portrait fisico. Il menu e' progettato in COORDINATE LOGICHE PORTRAIT
// 240x320 e ogni pixel viene mappato in landscape via:
//
//   x_landscape = y_portrait
//   y_landscape = 239 - x_portrait        (visibili: y_l in [0..231])
//
// Cosi' l'utente vede il menu DIRITTO sul display portrait fisico.
// Lo scroll orizzontale (x_p change) appare orizzontale anche all'utente.
//
// Layout PORTRAIT 240x320 (area utile: x_p in [8..239] = 232 visibili):
//   y_p=  20.. 27   "ARCADE COLLECTION" (font 8x8, ambra)
//   y_p=  60..155   logo 224x96 centrato (x_p = 8..231)
//   y_p= 175..182   nome gioco (bianco)
//   y_p= 200..207   pagination "<  N / TOT  >" (ciano)
//   y_p= 290..297   hint "SPIN: CHANGE   FIRE: START" (ciano)
// ============================================================================
#include "emulation/boot_menu.h"
#include "emulation/video.h"
#include "emulation/sd_card.h"
#include "config.h"

#include <dirent.h>          // LaserdiscMenu: scansione clip sulla SD
#include <sys/stat.h>
#include <strings.h>         // strcasecmp
#include "machines/arkanoid/arkanoid_logo.h"
#ifdef ENABLE_TEMPEST
#include "machines/tempest/tempest_logo.h"
#endif

#include "esp_heap_caps.h"        // heap_caps_get_free_size (riga IRAM del menu LD)
#include "esp_system.h"           // esp_reset_reason() (perche' si e' riavviato)
#include "emulation/font8.h"      // FONT8 + char_to_idx (condivisi con laserdisc.cpp)

// --- Asset nuovo menu "GIOCHI ARCADE" (griglia joystick) ---
#include "giochi_header.h"        // banner 232x96 (header griglia)
#include "giochi_card_logo.h"     // banner 224x96 (card nel carosello)
#include "laserdisc_card_logo.h"  // banner 224x96 (card "GIOCHI LASERDISC")
#include "neogeo_card_logo.h"     // banner 224x96 (card "GIOCHI NEO GEO")
#include "dl_grid_logo.h"         // 94x62 cover Dragon's Lair (voce del menu)
#include "ace_grid_logo.h"        // 94x62 cover Space Ace    (voce del menu)
#include "dl2_grid_logo.h"        // 94x62 cover Dragon's Lair II (voce del menu)
#include "_1944_grid_logo.h"      // 72x48 — 1944: The Loop Master (CPS-2)
#include "ghouls_grid_logo.h"     // 72x48 — Ghouls'n Ghosts (CPS-1)
#include "outrun_grid_logo.h"     // 72x48 — Out Run (Sega Out Run, due 68000)
#include "gaplus_grid_logo.h"     // 72x48 — Gaplus (Namco 1984, tre 6809)
#include "xevious_grid_logo.h"    // 72x48
#include "gyruss_grid_logo.h"     // 72x48
#include "pacman_grid_logo.h"     // 72x48
#include "_1942_grid_logo.h"      // 72x48
#include "pbaction_grid_logo.h"   // 72x48
#include "bombjack_grid_logo.h"   // 72x48
#include "dkongjr_grid_logo.h"    // 72x48
#include "frogger_grid_logo.h"    // 72x48
#include "greenberet_grid_logo.h" // 72x48
#include "ladybug_grid_logo.h"    // 72x48
#include "moonpatrol_grid_logo.h" // 72x48
#include "mariobros_grid_logo.h"  // 72x48
#include "mspacman_grid_logo.h"   // 72x48
#include "commando_grid_logo.h"   // 72x48
#include "dkong_grid_logo.h"      // 72x48
#include "brubber_grid_logo.h"    // 72x48
#include "scramble_grid_logo.h"   // 72x48
#include "popeye_grid_logo.h"     // 72x48
#include "fantasy_grid_logo.h"    // 72x48
#include "nibbler_grid_logo.h"    // 72x48
#include "mooncresta_grid_logo.h" // 72x48
#include "timeplt_grid_logo.h"    // 72x48
#include "gng_grid_logo.h"        // 72x48
#include "boblbobl_grid_logo.h"   // 72x48
#include "supercobra_grid_logo.h" // 72x48
#include "dkong3_grid_logo.h"     // 72x48
#include "supxevs_grid_logo.h"    // 72x48

extern Video video;

// Coordinate logiche portrait
#define MENU_W   240          // larghezza canvas portrait
#define MENU_H   320          // altezza canvas portrait
#define LOGO_W   224
#define LOGO_H   96
#define LOGO_X     16         // partenza x_p del logo: 12 = centro matematico, +4px a destra (finestra pannello non simmetrica)
#define LOGO_Y    60          // partenza y_p del logo
#define ANIM_FRAMES   10

#define Y_TITLE   20
#define Y_NAME   175
#define Y_PAGIN  200
#define Y_HINT   290

// Le voci EXTRA_* del carosello sono in emulation/boot_menu.h: le usa anche il
// dispatch in SPINNERINO.ino::loop().

// Mappa coords portrait -> coords landscape strip-relative.
// Ritorna true se il pixel cade nella strip landscape corrente.
// strip[] e' il buffer 320x8 landscape (TFT_FB_W * 8).
static inline void put_pixel_p(unsigned short *strip, int strip_y_l,
                               int x_p, int y_p, unsigned short color) {
  int x_l = y_p;
  int y_l = 239 - x_p;
  if (x_l < 0 || x_l >= TFT_FB_W) return;
  if (y_l < 0 || y_l >= TFT_FB_H) return;     // fuori area visibile
  int y_in = y_l - strip_y_l;
  if (y_in < 0 || y_in >= 8) return;          // non in questa strip
  strip[y_in * TFT_FB_W + x_l] = color;
}

// Font 8x8 + char_to_idx: ora in emulation/font8.h, condivisi con il player
// laserdisc (laserdisc.cpp), che li usa in landscape sul frame video.

static int text_width(const char *s) { return (int)strlen(s) * 8; }

// Disegna un char 8x8 a coords portrait (x_p, y_p) con rotazione software.
static void blit_char_rot(unsigned short *strip, int strip_y_l,
                          int x_p, int y_p, char ch, unsigned short color) {
  int idx = char_to_idx(ch);
  for (int r = 0; r < 8; r++) {
    unsigned char row = pgm_read_byte(&FONT8[idx][r]);
    int y_p_pixel = y_p + r;
    int x_l_for_this_row = y_p_pixel;
    if (x_l_for_this_row < 0 || x_l_for_this_row >= TFT_FB_W) continue;
    for (int c = 0; c < 8; c++) {
      if (row & (0x80 >> c)) {
        int x_p_pixel = x_p + c;
        int y_l = 239 - x_p_pixel;
        if (y_l < 0 || y_l >= TFT_FB_H) continue;
        int y_in = y_l - strip_y_l;
        if (y_in < 0 || y_in >= 8) continue;
        strip[y_in * TFT_FB_W + x_l_for_this_row] = color;
      }
    }
  }
}

// ----------------------------------------------------------------------------
// BootMenu
// ----------------------------------------------------------------------------
void BootMenu::init(Input *input, machineBase **machines, int count,
                    unsigned short *out_strip, int start_idx) {
  _input        = input;
  _machines     = machines;
  _count        = count;
  _out_strip    = out_strip;
  if (count > 0) {
    _current_idx = ((start_idx % count) + count) % count;
  } else {
    _current_idx = 0;
  }
  _next_idx     = _current_idx;
  _anim_frame   = 0;
  _anim_dir     = 0;
  _finished     = false;
  _last_buttons = 0xFF;
  _attract_t0   = millis();
  _attract_armed = true;
}

void BootMenu::update() {
  if (_finished || _count <= 0) return;
  unsigned char b = _input->buttons_get();

  if (_anim_frame > 0) {
    _anim_frame++;
    if (_anim_frame > ANIM_FRAMES) {
      _current_idx = _next_idx;
      _anim_frame  = 0;
      _anim_dir    = 0;
    }
    _last_buttons = b;
    return;
  }

  if ((b & BUTTON_FIRE) && !(_last_buttons & BUTTON_FIRE)) {
    _finished = true;
    _attract_armed = false;
    _last_buttons = b;
    return;
  }
  // Slot totali = giochi paddle + voci extra (vedi EXTRA_* in cima al file)
  int slots = _count + EXTRA_COUNT;
  if ((b & BUTTON_RIGHT) && !(_last_buttons & BUTTON_RIGHT)) {
    _anim_dir   = -1;
    _next_idx   = (_current_idx + 1) % slots;
    _anim_frame = 1;
    _attract_t0 = millis();        // user input → reset attract timer
  } else if ((b & BUTTON_LEFT) && !(_last_buttons & BUTTON_LEFT)) {
    _anim_dir   = +1;
    _next_idx   = (_current_idx - 1 + slots) % slots;
    _anim_frame = 1;
    _attract_t0 = millis();
  }
  _last_buttons = b;

#ifdef MASTER_ATTRACT_MENU_TIMEOUT
  // Master attract: dopo X ms di idle nel menu, autostart del gioco corrente.
  // Stesso pattern di FULL33/menu.cpp (Menu::handle()). NON su voce IMPOSTAZIONI.
  if (_attract_armed && _current_idx < _count &&
      (millis() - _attract_t0) > MASTER_ATTRACT_MENU_TIMEOUT) {
    _finished = true;
    _attract_armed = false;
  }
#endif
}

// Nome gioco per machineType (condiviso BootMenu + SettingsMenu).
static const char *name_for_type(signed char t) {
  switch (t) {
#ifdef ENABLE_ARKANOID
    case MCH_ARKANOID: return "ARKANOID";
#endif
#ifdef ENABLE_TEMPEST
    case MCH_TEMPEST:  return "TEMPEST";
#endif
#ifdef ENABLE_ARKANOID2
    case MCH_ARKANOID2: return "ARKANOID 2 WIP";
#endif
#ifdef ENABLE_GIGAS
    case MCH_GIGAS:     return "GIGAS";
#endif
#ifdef ENABLE_GIGAS2
    case MCH_GIGAS2:    return "GIGAS MARK II";
#endif
#ifdef ENABLE_GOINDOL
    case MCH_GOINDOL:   return "GOINDOL";
#endif
#ifdef ENABLE_SBRKOUT
    case MCH_SBRKOUT:   return "SUPER BREAKOUT";
#endif
#ifdef ENABLE_SPACE
    case MCH_SPACE:    return "SPACE INVADERS";
#endif
#ifdef ENABLE_GALAGA
    case MCH_GALAGA:   return "GALAGA";
#endif
#ifdef ENABLE_MOTORACE
    case MCH_MOTORACE: return "MOTORACE USA";
#endif
#ifdef ENABLE_PHOENIX
    case MCH_PHOENIX:  return "PHOENIX";
#endif
#ifdef ENABLE_GALAXIAN
    case MCH_GALAXIAN: return "GALAXIAN";
#endif
#ifdef ENABLE_BOMBBEE
    case MCH_BOMBBEE:  return "BOMB BEE";
#endif
#ifdef ENABLE_ROADFIGHTER
    case MCH_ROADFIGHTER: return "ROAD FIGHTER";
#endif
#ifdef ENABLE_PANG
    case MCH_PANG:     return "PANG";
#endif
#ifdef ENABLE_SUPERCOBRA
    case MCH_SUPERCOBRA: return "SUPER COBRA";
#endif
#ifdef ENABLE_DKONG3
    case MCH_DKONG3: return "DONKEY KONG 3";
#endif
    default:           return "GAME";
  }
}

const char *BootMenu::machine_name(int idx) {
  if (!_machines || idx < 0) return "?";
  if (idx == _count + EXTRA_SETTINGS)  return "IMPOSTAZIONI";
  if (idx == _count + EXTRA_ARCADE)    return "GIOCHI ARCADE";
  if (idx == _count + EXTRA_LASERDISC) return "GIOCHI LASERDISC";
  if (idx == _count + EXTRA_NEOGEO)    return "GIOCHI NEO GEO";
  if (idx == _count + EXTRA_TEST)      return "TEST JOYSTICK";
  if (idx == _count + EXTRA_WIFI)      return "FILE MANAGER WIFI";
  if (idx >= _count + EXTRA_COUNT)     return "?";
  return name_for_type(_machines[idx]->machineType());
}

// Wrapper compatibili con l'interfaccia originale (non piu' usate per il
// rendering portrait, le ridichiariamo per soddisfare il .h).
void BootMenu::draw_text(int strip_y_global, int x, int y_global,
                         const char *text, unsigned short color) {
  for (int i = 0; text[i]; i++) {
    blit_char_rot(_out_strip, strip_y_global, x + i * 8, y_global, text[i], color);
  }
}

// Disegna porzione del logo 224x96 con rotazione software.
// x_offset = x_p del logo nel canvas portrait (puo' essere negativo per slide).
void BootMenu::draw_logo(int strip_y_global, int x_offset,
                         const unsigned short *logo) {
  // Determina quali colonne del canvas portrait cadono nella strip landscape.
  // Ogni col portrait x_p diventa row landscape y_l = 239 - x_p.
  // Ci interessano x_p tali che y_l in [strip_y..strip_y+8).
  // => 239 - x_p in [strip_y..strip_y+8)  =>  x_p in (231-strip_y..239-strip_y].
  int x_p_min = 232 - strip_y_global;             // inclusivo
  int x_p_max = 239 - strip_y_global;             // inclusivo
  if (x_p_min < 0)        x_p_min = 0;
  if (x_p_max >= MENU_W)  x_p_max = MENU_W - 1;

  // Range del logo in coords portrait
  int logo_x_min = x_offset;
  int logo_x_max = x_offset + LOGO_W - 1;

  for (int x_p = x_p_min; x_p <= x_p_max; x_p++) {
    if (x_p < logo_x_min || x_p > logo_x_max) continue;
    int logo_col = x_p - x_offset;                // 0..223
    int y_l = 239 - x_p;
    int y_in = y_l - strip_y_global;
    if (y_in < 0 || y_in >= 8) continue;
    unsigned short *dst_row = _out_strip + y_in * TFT_FB_W;
    // Per ogni y_p della colonna: y_p in [LOGO_Y..LOGO_Y+LOGO_H)
    // x_l = y_p
    for (int row = 0; row < LOGO_H; row++) {
      int y_p_pixel = LOGO_Y + row;
      int x_l = y_p_pixel;
      if (x_l < 0 || x_l >= TFT_FB_W) continue;
      // Pixel sorgente: logo[row * LOGO_W + logo_col]
      dst_row[x_l] = logo[row * LOGO_W + logo_col];
    }
  }
}

// Disegna la entry idx (logo di gioco o card di una voce extra) a x_offset.
// Usata sia per la entry corrente sia per quella entrante durante lo slide.
void BootMenu::draw_entry(int strip_y_global, int x_offset, int idx) {
  if (idx < _count) {
    const unsigned short *lg = _machines[idx]->logo();
    if (lg) draw_logo(strip_y_global, x_offset, lg);
  } else if (idx == _count + EXTRA_SETTINGS) {
    draw_settings_card(strip_y_global, x_offset);
  } else if (idx == _count + EXTRA_ARCADE) {
    draw_arcade_card(strip_y_global, x_offset);
  } else if (idx == _count + EXTRA_LASERDISC) {
    draw_laserdisc_card(strip_y_global, x_offset);
  } else if (idx == _count + EXTRA_NEOGEO) {
    draw_neogeo_card(strip_y_global, x_offset);
  } else if (idx == _count + EXTRA_TEST) {
    draw_test_card(strip_y_global, x_offset);
  } else if (idx == _count + EXTRA_WIFI) {
    draw_wifi_card(strip_y_global, x_offset);
  }
}

void BootMenu::draw_strip(int strip_y_global) {
  // Sfondo nero
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  // Posizione X portrait del logo (per scroll orizzontale durante animazione)
  int x_current = LOGO_X;
  int x_next    = 0;
  bool anim_active = (_anim_frame > 0);
  if (anim_active) {
    int dx = (MENU_W * _anim_frame) / ANIM_FRAMES;
    x_current = LOGO_X + _anim_dir * dx;
    x_next    = x_current - _anim_dir * MENU_W;
  }

  draw_entry(strip_y_global, x_current, _current_idx);
  if (anim_active) draw_entry(strip_y_global, x_next, _next_idx);

  unsigned short C_WHITE = 0xFFFF;
  unsigned short C_CYAN  = 0xFF07;
  unsigned short C_AMBER = 0x60FE;

  // Title centrato in area utile portrait [8..239] (= 232 px utili)
  const char *title = "ARCADE COLLECTION";
  int title_x = 8 + (232 - text_width(title)) / 2;
  draw_text(strip_y_global, title_x, Y_TITLE, title, C_AMBER);

  if (!anim_active) {
    const char *name = machine_name(_current_idx);
    int name_x = 8 + (232 - text_width(name)) / 2;
    draw_text(strip_y_global, name_x, Y_NAME, name, C_WHITE);

    char pag[16];
    snprintf(pag, sizeof(pag), "<  %d / %d  >", _current_idx + 1, _count + EXTRA_COUNT);
    int pag_x = 8 + (232 - text_width(pag)) / 2;
    draw_text(strip_y_global, pag_x, Y_PAGIN, pag, C_CYAN);
  }

  const char *hint = "SPIN: CHANGE   FIRE: START";
  int hint_x = 8 + (232 - text_width(hint)) / 2;
  draw_text(strip_y_global, hint_x, Y_HINT, hint, C_CYAN);
}

// Render: strip landscape 320x8, 29 strip per coprire 232 row (TFT_FB_H).
void BootMenu::render() {
  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}

// Card della voce "IMPOSTAZIONI" nel carosello: cornice + testo nella logo-box,
// scorre con x_offset come un logo durante l'animazione.
void BootMenu::draw_settings_card(int strip_y_global, int x_offset) {
  unsigned short C_AMBER = 0x60FE;
  unsigned short C_WHITE = 0xFFFF;

  int x0 = x_offset + 12;
  int x1 = x_offset + LOGO_W - 13;
  int y0 = LOGO_Y + 6;
  int y1 = LOGO_Y + LOGO_H - 7;
  for (int x_p = x0; x_p <= x1; x_p++) {
    put_pixel_p(_out_strip, strip_y_global, x_p, y0, C_AMBER);
    put_pixel_p(_out_strip, strip_y_global, x_p, y1, C_AMBER);
  }
  for (int y_p = y0; y_p <= y1; y_p++) {
    put_pixel_p(_out_strip, strip_y_global, x0, y_p, C_AMBER);
    put_pixel_p(_out_strip, strip_y_global, x1, y_p, C_AMBER);
  }

  const char *l1 = "IMPOSTAZIONI";
  const char *l2 = "SENSIBILITA";
  const char *l3 = "SPINNER";
  int l1x = x_offset + (LOGO_W - text_width(l1)) / 2;
  int l2x = x_offset + (LOGO_W - text_width(l2)) / 2;
  int l3x = x_offset + (LOGO_W - text_width(l3)) / 2;
  for (int i = 0; l1[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l1x + i * 8, LOGO_Y + 24, l1[i], C_WHITE);
  for (int i = 0; l2[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l2x + i * 8, LOGO_Y + 42, l2[i], C_AMBER);
  for (int i = 0; l3[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l3x + i * 8, LOGO_Y + 60, l3[i], C_AMBER);
}

// Card della voce "TEST JOYSTICK E PULSANTI": stessa forma della card
// IMPOSTAZIONI, ma con la cornice VERDE e a due pixel — e' una voce di
// servizio/diagnostica e deve distinguersi a colpo d'occhio dalle altre del
// carosello (che hanno la cornice ambra a un pixel).
void BootMenu::draw_test_card(int strip_y_global, int x_offset) {
  const unsigned short C_VERDE = 0xE007;    // byte-swapped di 0x07E0
  const unsigned short C_WHITE = 0xFFFF;

  int x0 = x_offset + 12;
  int x1 = x_offset + LOGO_W - 13;
  int y0 = LOGO_Y + 6;
  int y1 = LOGO_Y + LOGO_H - 7;
  // cornice a DUE pixel: piu' marcata di quella ambra delle impostazioni
  for (int t = 0; t < 2; t++) {
    for (int x_p = x0 + t; x_p <= x1 - t; x_p++) {
      put_pixel_p(_out_strip, strip_y_global, x_p, y0 + t, C_VERDE);
      put_pixel_p(_out_strip, strip_y_global, x_p, y1 - t, C_VERDE);
    }
    for (int y_p = y0 + t; y_p <= y1 - t; y_p++) {
      put_pixel_p(_out_strip, strip_y_global, x0 + t, y_p, C_VERDE);
      put_pixel_p(_out_strip, strip_y_global, x1 - t, y_p, C_VERDE);
    }
  }

  const char *l1 = "TEST";
  const char *l2 = "JOYSTICK";
  const char *l3 = "E PULSANTI";
  int l1x = x_offset + (LOGO_W - text_width(l1)) / 2;
  int l2x = x_offset + (LOGO_W - text_width(l2)) / 2;
  int l3x = x_offset + (LOGO_W - text_width(l3)) / 2;
  for (int i = 0; l1[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l1x + i * 8, LOGO_Y + 24, l1[i], C_WHITE);
  for (int i = 0; l2[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l2x + i * 8, LOGO_Y + 42, l2[i], C_VERDE);
  for (int i = 0; l3[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l3x + i * 8, LOGO_Y + 60, l3[i], C_VERDE);
}

// Card della voce "FILE MANAGER WIFI": stessa forma della card TEST, cornice
// CIANO a due pixel. La schermata vera e' in wifi_fm.cpp.
void BootMenu::draw_wifi_card(int strip_y_global, int x_offset) {
  const unsigned short C_CIANO = 0xFF07;    // byte-swapped di 0x07FF
  const unsigned short C_WHITE = 0xFFFF;

  int x0 = x_offset + 12;
  int x1 = x_offset + LOGO_W - 13;
  int y0 = LOGO_Y + 6;
  int y1 = LOGO_Y + LOGO_H - 7;
  for (int t = 0; t < 2; t++) {
    for (int x_p = x0 + t; x_p <= x1 - t; x_p++) {
      put_pixel_p(_out_strip, strip_y_global, x_p, y0 + t, C_CIANO);
      put_pixel_p(_out_strip, strip_y_global, x_p, y1 - t, C_CIANO);
    }
    for (int y_p = y0 + t; y_p <= y1 - t; y_p++) {
      put_pixel_p(_out_strip, strip_y_global, x0 + t, y_p, C_CIANO);
      put_pixel_p(_out_strip, strip_y_global, x1 - t, y_p, C_CIANO);
    }
  }

  const char *l1 = "FILE MANAGER";
  const char *l2 = "WIFI";
  const char *l3 = "FILE SU SD DAL PC";
  int l1x = x_offset + (LOGO_W - text_width(l1)) / 2;
  int l2x = x_offset + (LOGO_W - text_width(l2)) / 2;
  int l3x = x_offset + (LOGO_W - text_width(l3)) / 2;
  for (int i = 0; l1[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l1x + i * 8, LOGO_Y + 24, l1[i], C_WHITE);
  for (int i = 0; l2[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l2x + i * 8, LOGO_Y + 42, l2[i], C_CIANO);
  for (int i = 0; l3[i]; i++)
    blit_char_rot(_out_strip, strip_y_global, l3x + i * 8, LOGO_Y + 60, l3[i], C_CIANO);
}

// Card della voce "GIOCHI ARCADE" nel carosello: usa il logo banner 224x96
// (giochi_card_logo) al posto delle scritte. Si disegna esattamente come un
// logo di gioco (stesso slot 224x96, scorre con x_offset).
void BootMenu::draw_arcade_card(int strip_y_global, int x_offset) {
  draw_logo(strip_y_global, x_offset, giochi_card_logo);
}

// Card della voce "GIOCHI LASERDISC" nel carosello: banner 224x96 come sopra.
void BootMenu::draw_laserdisc_card(int strip_y_global, int x_offset) {
  draw_logo(strip_y_global, x_offset, laserdisc_card_logo);
}

// Card della voce "GIOCHI NEO GEO" nel carosello: banner 224x96 come sopra,
// generato da PY\convert_neogeo_card.py con la stessa regola stretch-fit della
// card "GIOCHI ARCADE".
void BootMenu::draw_neogeo_card(int strip_y_global, int x_offset) {
  draw_logo(strip_y_global, x_offset, neogeo_card_logo);
}

// ============================================================================
// GamesGridMenu — menu griglia 3x3 dei giochi joystick (GIOCHI ARCADE).
// ============================================================================
// Tabella delle voci: per ora i loghi piccoli (raw 72x48). L'associazione al
// gioco reale in machines[] avviene nel .ino (grid_to_machine), perche' i
// porting (Xevious/Gyruss) vengono aggiunti nelle fasi successive.
struct GridEntry { const unsigned short *logo; };
static const GridEntry GRID_ENTRIES[] = {
  { xevious_grid_logo },
  { supxevs_grid_logo },   // accanto a Xevious (stessa riga della griglia)
  { gyruss_grid_logo },
  { pacman_grid_logo },
  { _1942_grid_logo },
  { _1944_grid_logo },     // 1944: The Loop Master (CPS-2) — player AUTONOMO
  { pbaction_grid_logo },
  { bombjack_grid_logo },
  { dkongjr_grid_logo },
  { frogger_grid_logo },
  { greenberet_grid_logo },
  { ladybug_grid_logo },
  { moonpatrol_grid_logo },
  { mariobros_grid_logo },
  { mspacman_grid_logo },
  { commando_grid_logo },
  { dkong_grid_logo },
  { brubber_grid_logo },
  { scramble_grid_logo },
  { popeye_grid_logo },
  { fantasy_grid_logo },
  { nibbler_grid_logo },
  { mooncresta_grid_logo },
  { timeplt_grid_logo },
  { gng_grid_logo },
  { ghouls_grid_logo },    // Ghouls'n Ghosts (CPS-1) — player AUTONOMO
  { boblbobl_grid_logo },
  { supercobra_grid_logo },
  { dkong3_grid_logo },
  // ⭐ Out Run IN FONDO di proposito: GRID_ENTRIES, il `switch` di
  // grid_to_machine() e joystick_machines[] sono TRE ELENCHI PARALLELI, e
  // infilare una voce in mezzo fa slittare tutto cio' che segue (spostando
  // Ghouls alla cella 25 sono stati rinumerati tutti e 27 i `case`). In fondo
  // non si tocca nulla. E' un player AUTONOMO: nessuna voce nel `switch`.
  { outrun_grid_logo },    // Out Run (Sega 1986) — player AUTONOMO
  // ⭐ Gaplus IN FONDO, dopo Out Run, per lo stesso motivo: player AUTONOMO,
  // nessuna voce nel `switch` di grid_to_machine(), indice GRID_IDX_GAPLUS.
  { gaplus_grid_logo },    // Gaplus (Namco 1984) — player AUTONOMO, dati su SD
};
static const int GRID_COUNT = sizeof(GRID_ENTRIES) / sizeof(GRID_ENTRIES[0]);

// Geometria griglia (coords portrait)
#define G_COLS    3
#define G_ROWS    3              // righe visibili (scroll oltre)
#define GCELL_W   77
#define GCELL_H   66
#define GRID_X0    8
#define GRID_Y0  104
#define GLOGO_W   72
#define GLOGO_H   48

int GamesGridMenu::count() const { return GRID_COUNT; }

void GamesGridMenu::init(Input *input, unsigned short *out_strip) {
  _input        = input;
  _out_strip    = out_strip;
  _sel          = 0;
  _scroll       = 0;
  _finished     = false;
  _last_buttons = 0xFF;
}

void GamesGridMenu::update() {
  unsigned char b = _input->buttons_get();

  if ((b & BUTTON_FIRE) && !(_last_buttons & BUTTON_FIRE)) {
    _finished = true;
    _last_buttons = b;
    return;
  }
  if ((b & BUTTON_RIGHT) && !(_last_buttons & BUTTON_RIGHT)) {
    _sel = (_sel + 1) % GRID_COUNT;
  } else if ((b & BUTTON_LEFT) && !(_last_buttons & BUTTON_LEFT)) {
    _sel = (_sel - 1 + GRID_COUNT) % GRID_COUNT;
  } else if ((b & BUTTON_DOWN) && !(_last_buttons & BUTTON_DOWN)) {
    int n = _sel + G_COLS; if (n < GRID_COUNT) _sel = n;
  } else if ((b & BUTTON_UP) && !(_last_buttons & BUTTON_UP)) {
    int n = _sel - G_COLS; if (n >= 0) _sel = n;
  }

  // Scroll verticale per tenere la riga selezionata visibile.
  int row = _sel / G_COLS;
  if (row < _scroll)               _scroll = row;
  if (row > _scroll + G_ROWS - 1)  _scroll = row - G_ROWS + 1;

  _last_buttons = b;
}

// Banner header 232x96 (giochi_header) a y_p 0..95, x_p 8..239.
void GamesGridMenu::draw_header(int strip_y) {
  int x_p_min = 232 - strip_y;  if (x_p_min < 13)  x_p_min = 13;   // +5px a destra (era 8)
  int x_p_max = 239 - strip_y;  if (x_p_max > 239) x_p_max = 239;
  for (int x_p = x_p_min; x_p <= x_p_max; x_p++) {
    int hx = x_p - 13;
    if (hx < 0 || hx >= GIOCHI_HEADER_W) continue;
    int y_l = 239 - x_p;
    int y_in = y_l - strip_y;
    if (y_in < 0 || y_in >= 8) continue;
    unsigned short *dst = _out_strip + y_in * TFT_FB_W;
    for (int hy = 0; hy < GIOCHI_HEADER_H; hy++) {
      int x_l = hy + 2;                   // x_l = y_p = hy, +2px in basso
      if (x_l >= TFT_FB_W) break;
      dst[x_l] = giochi_header[hy * GIOCHI_HEADER_W + hx];
    }
  }
}

// Logo piccolo raw (w x h) a (x_p0, y_p0), row-major portrait.
// Dimezza la luminosita' di un pixel RGB565 GIA' BYTE-SWAPPED: va prima
// disfatto lo swap, altrimenti si shiftano i bit sbagliati e il colore esplode.
// Usato per i giochi non avviabili (video mancanti) -> logo "spento".
static inline unsigned short dim565_swapped(unsigned short s) {
  unsigned short v = (unsigned short)((s >> 8) | (s << 8));
  unsigned short r = ((v >> 11) & 0x1F) >> 1;
  unsigned short g = ((v >> 5)  & 0x3F) >> 1;
  unsigned short b = ( v        & 0x1F) >> 1;
  v = (unsigned short)((r << 11) | (g << 5) | b);
  return (unsigned short)((v >> 8) | (v << 8));
}

// --- Disegno loghi/cornici, CONDIVISO tra la griglia "GIOCHI ARCADE" e il
// menu "GIOCHI LASERDISC". Stesso sistema, MISURE DIVERSE: arcade 72x48 in
// cella 77x66, laserdisc 94x62 in cella 99x80 (+30%). w/h sono parametri.
// Erano metodi di GamesGridMenu: estratti qui per non duplicarli. I metodi
// della classe restano come wrapper, cosi' boot_menu.h non cambia.
static void draw_logo_box(unsigned short *out_strip, int strip_y, int x_p0,
                          int y_p0, int w, int h, const unsigned short *logo,
                          bool dim = false) {
  int x_p_min = 232 - strip_y;  if (x_p_min < x_p0)         x_p_min = x_p0;
  int x_p_max = 239 - strip_y;  if (x_p_max > x_p0 + w - 1) x_p_max = x_p0 + w - 1;
  for (int x_p = x_p_min; x_p <= x_p_max; x_p++) {
    int px = x_p - x_p0;
    if (px < 0 || px >= w) continue;
    int y_l = 239 - x_p;
    int y_in = y_l - strip_y;
    if (y_in < 0 || y_in >= 8) continue;
    unsigned short *dst = out_strip + y_in * TFT_FB_W;
    for (int py = 0; py < h; py++) {
      int x_l = y_p0 + py;
      if (x_l < 0 || x_l >= TFT_FB_W) continue;
      unsigned short v = logo[py * w + px];
      dst[x_l] = dim ? dim565_swapped(v) : v;
    }
  }
}

static void draw_box_frame(unsigned short *out_strip, int strip_y, int x0, int y0,
                           int w, int h, unsigned short col) {
  int x1 = x0 + w - 1, y1 = y0 + h - 1;
  for (int x = x0; x <= x1; x++) {
    put_pixel_p(out_strip, strip_y, x, y0, col);
    put_pixel_p(out_strip, strip_y, x, y1, col);
  }
  for (int y = y0; y <= y1; y++) {
    put_pixel_p(out_strip, strip_y, x0, y, col);
    put_pixel_p(out_strip, strip_y, x1, y, col);
  }
}

void GamesGridMenu::draw_cell_logo(int strip_y, int x_p0, int y_p0, int w, int h,
                                   const unsigned short *logo) {
  draw_logo_box(_out_strip, strip_y, x_p0, y_p0, w, h, logo);
}

void GamesGridMenu::draw_frame(int strip_y, int x0, int y0, int w, int h,
                               unsigned short col) {
  draw_box_frame(_out_strip, strip_y, x0, y0, w, h, col);
}

void GamesGridMenu::draw_strip(int strip_y) {
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  draw_header(strip_y);

  for (int vr = 0; vr < G_ROWS; vr++) {
    int gridRow = _scroll + vr;
    for (int c = 0; c < G_COLS; c++) {
      int idx = gridRow * G_COLS + c;
      if (idx < 0 || idx >= GRID_COUNT) continue;
      int cell_x = GRID_X0 + c * GCELL_W;
      int cell_y = GRID_Y0 + vr * GCELL_H;
      int lx = cell_x + (GCELL_W - GLOGO_W) / 2;
      int ly = cell_y + (GCELL_H - GLOGO_H) / 2;
      draw_cell_logo(strip_y, lx, ly, GLOGO_W, GLOGO_H, GRID_ENTRIES[idx].logo);
      if (idx == _sel)
        draw_frame(strip_y, cell_x + 1, cell_y + 1, GCELL_W - 2, GCELL_H - 2, 0x60FE);
    }
  }

  // Hint in basso
  unsigned short C_CYAN = 0xFF07;
  const char *hint = "FIRE GIOCA  START 3s ESCI";
  int hx = 8 + (232 - text_width(hint)) / 2;
  if (hx < 8) hx = 8;
  for (int i = 0; hint[i]; i++)
    blit_char_rot(_out_strip, strip_y, hx + i * 8, 308, hint[i], C_CYAN);
}

void GamesGridMenu::render() {
  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}

// ============================================================================
// SettingsMenu — lista giochi + slider sensibilita' EC11 (livello 1..10).
// ============================================================================
void SettingsMenu::init(Input *input, machineBase **machines, int count,
                        unsigned short *out_strip) {
  _input     = input;
  _machines  = machines;
  _count     = count;
  _out_strip = out_strip;
  _sel       = 0;
  _edit      = false;
  _scroll    = 0;
  _finished  = false;
  _last_buttons = 0xFF;
}

void SettingsMenu::update() {
  if (_finished || _count <= 0) return;
  unsigned char b = _input->buttons_get();

  // START = salva ed esci (gestito dal chiamante quando finished()).
  if ((b & BUTTON_START) && !(_last_buttons & BUTTON_START)) {
    _finished = true;
    _last_buttons = b;
    return;
  }

  // FIRE = entra/esce dalla modifica del gioco selezionato.
  if ((b & BUTTON_FIRE) && !(_last_buttons & BUTTON_FIRE)) {
    _edit = !_edit;
  }

  // SPIN: in modifica cambia il livello, altrimenti scorre la lista.
  if ((b & BUTTON_RIGHT) && !(_last_buttons & BUTTON_RIGHT)) {
    if (_edit) {
      if (_machines[_sel]->ec11SensLevel < 10) _machines[_sel]->ec11SensLevel++;
    } else {
      _sel = (_sel + 1) % _count;
    }
  } else if ((b & BUTTON_LEFT) && !(_last_buttons & BUTTON_LEFT)) {
    if (_edit) {
      if (_machines[_sel]->ec11SensLevel > 1) _machines[_sel]->ec11SensLevel--;
    } else {
      _sel = (_sel - 1 + _count) % _count;
    }
  }

  // Scroll per tenere la riga selezionata visibile.
  const int VISIBLE = 11;
  if (_sel < _scroll)             _scroll = _sel;
  if (_sel >= _scroll + VISIBLE)  _scroll = _sel - VISIBLE + 1;

  _last_buttons = b;
}

void SettingsMenu::draw_strip(int strip_y) {
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  unsigned short C_WHITE = 0xFFFF, C_CYAN = 0xFF07, C_AMBER = 0x60FE,
                 C_GREEN = 0xE007, C_GREY = 0x0842;

  const char *title = "SENSIBILITA SPINNER";
  int tx = 8 + (232 - text_width(title)) / 2;
  for (int i = 0; title[i]; i++)
    blit_char_rot(_out_strip, strip_y, tx + i * 8, 12, title[i], C_AMBER);

  const int ROW0 = 38, ROW_H = 22, VISIBLE = 11;
  const int CURSOR_X = 8, NAME_X = 20, BAR_X = 120, BAR_CELLS = 10, CELL_W = 8;
  const int NUM_X = BAR_X + BAR_CELLS * CELL_W + 6;   // 206 (entro x_p<=239)

  for (int r = 0; r < VISIBLE; r++) {
    int gi = _scroll + r;
    if (gi >= _count) break;
    int y_p = ROW0 + r * ROW_H;
    bool selected = (gi == _sel);
    bool editing  = selected && _edit;
    int  level    = _machines[gi]->ec11SensLevel;
    unsigned short name_col = selected ? C_WHITE : C_CYAN;

    if (selected)
      blit_char_rot(_out_strip, strip_y, CURSOR_X, y_p, '>', C_AMBER);

    char nm[13];
    const char *full = name_for_type(_machines[gi]->machineType());
    int n = 0; for (; full[n] && n < 12; n++) nm[n] = full[n]; nm[n] = 0;
    for (int i = 0; nm[i]; i++)
      blit_char_rot(_out_strip, strip_y, NAME_X + i * 8, y_p, nm[i], name_col);

    for (int c = 0; c < BAR_CELLS; c++) {
      bool fill = (c < level);
      unsigned short cell_col = fill ? (editing ? C_AMBER : C_GREEN) : C_GREY;
      int cx = BAR_X + c * CELL_W;
      for (int yy = 0; yy < 7; yy++)
        for (int xx = 0; xx < CELL_W - 1; xx++)
          put_pixel_p(_out_strip, strip_y, cx + xx, y_p + yy, cell_col);
    }

    char num[4]; snprintf(num, sizeof(num), "%d", level);
    unsigned short num_col = editing ? C_AMBER : name_col;
    for (int i = 0; num[i]; i++)
      blit_char_rot(_out_strip, strip_y, NUM_X + i * 8, y_p, num[i], num_col);
  }

  const char *hint = _edit ? "SPIN: VALORE   FIRE: OK"
                           : "SPIN: SCEGLI   FIRE: MODIFICA";
  int hx = 8 + (232 - text_width(hint)) / 2;
  for (int i = 0; hint[i]; i++)
    blit_char_rot(_out_strip, strip_y, hx + i * 8, 290, hint[i], C_CYAN);
  const char *hint2 = "START: SALVA ED ESCI";
  int h2x = 8 + (232 - text_width(hint2)) / 2;
  for (int i = 0; hint2[i]; i++)
    blit_char_rot(_out_strip, strip_y, h2x + i * 8, 306, hint2[i], C_CYAN);
}

void SettingsMenu::render() {
  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}

// ============================================================================
// TestMenu — schermata "TEST JOYSTICK E PULSANTI" (vedi emulation/boot_menu.h).
// ============================================================================
// ⚠️ Legge ESATTAMENTE gli stessi ingressi dei giochi: `buttons_get()` per
// joystick e pulsanti, e le due variabili che riempie l'ISR dell'encoder in
// input.cpp. Se un comando non si accende qui, non funziona nemmeno in partita —
// e' il senso della schermata.
extern volatile int16_t ec11_paddle_x;      // input.cpp: 0..255, clamped
extern volatile uint8_t ec11_dial_counter;  // input.cpp: free-running, wrappa
extern bool input_is_vol_mode_active(void);

// riquadro a coordinate PORTRAIT: cornice sempre, interno pieno se `acceso`
static void box_p(unsigned short *strip, int sy, int x0, int y0, int w, int h,
                  unsigned short bordo, unsigned short dentro, bool acceso) {
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) {
      const bool cornice = (x == 0 || x == w - 1 || y == 0 || y == h - 1);
      if (cornice)      put_pixel_p(strip, sy, x0 + x, y0 + y, bordo);
      else if (acceso)  put_pixel_p(strip, sy, x0 + x, y0 + y, dentro);
    }
}

// scritta centrata dentro un riquadro
static void testo_c(unsigned short *strip, int sy, int x0, int w, int y_p,
                    const char *s, unsigned short col) {
  int x = x0 + (w - text_width(s)) / 2;
  for (int i = 0; s[i]; i++)
    blit_char_rot(strip, sy, x + i * 8, y_p, s[i], col);
}

void TestMenu::init(Input *input, unsigned short *out_strip) {
  _input = input;
  _out_strip = out_strip;
  _btn = 0;
  _paddle = ec11_paddle_x;
  _dial = ec11_dial_counter;
  _dial_prec = _dial;
  _verso = 0;
  _verso_t0 = 0;
}

void TestMenu::update() {
  _btn = _input->buttons_get();
  _paddle = ec11_paddle_x;
  _dial = ec11_dial_counter;
  // verso dell'ultimo scatto: differenza con SEGNO sul contatore a 8 bit, cosi'
  // il wrap 255->0 non viene scambiato per un giro all'indietro.
  const int d = (int)(int8_t)((uint8_t)_dial - (uint8_t)_dial_prec);
  if (d != 0) {
    _verso = (d > 0) ? +1 : -1;
    _verso_t0 = millis();
    _dial_prec = _dial;
  } else if (_verso && (millis() - _verso_t0) > 400) {
    _verso = 0;                      // fermo da un po': si spegne l'indicazione
  }
}

void TestMenu::draw_strip(int strip_y) {
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  const unsigned short C_WHITE = 0xFFFF, C_AMBER = 0x60FE,
                       C_VERDE = 0xE007, C_GRIGIO = 0x0842, C_CIANO = 0xFF07;

  // ⚠️⚠️ AREA VISIBILE: x_p va da **8** a 239, non da 0. In put_pixel_p() la
  // coordinata del pannello e' `y_l = 239 - x_p` e viene scartata se
  // `y_l >= TFT_FB_H` (232): quindi le prime OTTO colonne cadono fuori dallo
  // schermo per via dell'offset hardware (TFT_HW_Y = 8). Centrare su 0..239
  // faceva sembrare tutto spostato a sinistra e mangiava il bordo dei riquadri.
  // ⭐ E' la stessa base che usa gia' il menu IMPOSTAZIONI (`8 + (232 - w)/2`).
  // ⚠️ X0 = 14, non 8: gli 8 sono il minimo TECNICO (sotto non si vede nulla),
  // ma sul pannello vero il contenuto restava ancora spostato a sinistra, quindi
  // si tengono 6 px in piu' di margine (tarati a occhio sul device, in due
  // passate). La larghezza cala di altrettanto, cosi' il bordo destro resta sul
  // pixel 239.
  const int X0 = 14, XW = 226;             // origine e larghezza UTILI
  const int CX = X0 + XW / 2;              // centro = 127

  // ---- titolo
  testo_c(_out_strip, strip_y, X0, XW, 8,  "TEST JOYSTICK", C_VERDE);
  testo_c(_out_strip, strip_y, X0, XW, 22, "E PULSANTI",    C_VERDE);

  // ---- le 4 direzioni, disposte a CROCE come sul mobile
  testo_c(_out_strip, strip_y, X0, XW, 44, "DIREZIONI", C_AMBER);
  struct { int x, y, w, h; unsigned char m; const char *et; } D[4] = {
    { CX - 20,  60, 40, 26, BUTTON_UP,    "SU"  },
    { CX - 68,  90, 40, 26, BUTTON_LEFT,  "SX"  },
    { CX + 28,  90, 40, 26, BUTTON_RIGHT, "DX"  },
    { CX - 20, 120, 40, 26, BUTTON_DOWN,  "GIU" },
  };
  for (int i = 0; i < 4; i++) {
    const bool on = (_btn & D[i].m) != 0;
    box_p(_out_strip, strip_y, D[i].x, D[i].y, D[i].w, D[i].h,
          on ? C_VERDE : C_GRIGIO, C_VERDE, on);
    testo_c(_out_strip, strip_y, D[i].x, D[i].w, D[i].y + 9, D[i].et,
            on ? 0x0000 : C_CIANO);
  }

  // ---- i 4 pulsanti. La fila si CENTRA nell'area utile invece di partire dal
  // bordo: cosi' spostando X0/XW non serve rifare i conti a mano (e non si
  // rischia di sforare a destra, che e' quello che succedeva prima).
  testo_c(_out_strip, strip_y, X0, XW, 158, "PULSANTI", C_AMBER);
  const int PW = 52, PG = 6;
  const int PTOT = 4 * PW + 3 * PG;              // 226 su 228 disponibili
  const int PX0  = X0 + (XW - PTOT) / 2;
  struct { unsigned char m; const char *et; } P[4] = {
    { BUTTON_FIRE,  "A"    },
    { BUTTON_EXTRA, "B"    },
    { BUTTON_START, "STA"  },
    { BUTTON_COIN,  "COIN" },
  };
  for (int i = 0; i < 4; i++) {
    const int px = PX0 + i * (PW + PG);
    const bool on = (_btn & P[i].m) != 0;
    box_p(_out_strip, strip_y, px, 174, PW, 28,
          on ? C_VERDE : C_GRIGIO, C_VERDE, on);
    testo_c(_out_strip, strip_y, px, PW, 184, P[i].et, on ? 0x0000 : C_CIANO);
  }

  // ---- encoder EC11
  testo_c(_out_strip, strip_y, X0, XW, 214, "SPINNER EC11", C_AMBER);
  // barra del paddle, 0..255 su 200 px, centrata nell'area utile
  const int BW = 200, BH = 16;
  const int BX = X0 + (XW - BW) / 2, BY = 230;
  int pieni = _paddle * (BW - 2) / 255;
  if (pieni < 0) pieni = 0; else if (pieni > BW - 2) pieni = BW - 2;
  box_p(_out_strip, strip_y, BX, BY, BW, BH, C_WHITE, 0, false);
  for (int y = 1; y < BH - 1; y++)
    for (int x = 0; x < pieni; x++)
      put_pixel_p(_out_strip, strip_y, BX + 1 + x, BY + y, C_VERDE);

  char riga[24];
  snprintf(riga, sizeof riga, "PAD %3d  DIAL %3d", _paddle, _dial);
  testo_c(_out_strip, strip_y, X0, XW, 254, riga, C_WHITE);

  // verso dell'ultimo scatto: si spegne dopo 400 ms di fermo
  const char *v = (_verso > 0) ? "GIRA  >>>" : (_verso < 0 ? "<<<  GIRA" : "FERMO");
  testo_c(_out_strip, strip_y, X0, XW, 270, v, _verso ? C_VERDE : C_GRIGIO);

  // ⚠️ Il pulsante dell'encoder e' cablato su FIRE: premendolo si accende "A".
  // Scritto qui perche' altrimenti sembra che il tasto dell'EC11 non funzioni.
  testo_c(_out_strip, strip_y, X0, XW, 288, "SW ENCODER = A", C_CIANO);

  // ---- piede: modo volume (se attivo) e uscita
  if (input_is_vol_mode_active())
    testo_c(_out_strip, strip_y, X0, XW, 302, "MODO VOLUME: SPIN FERMO", C_AMBER);
  else
    testo_c(_out_strip, strip_y, X0, XW, 302, "START 3s ESCI", C_GRIGIO);
}

void TestMenu::render() {
  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}

// ============================================================================
// LaserdiscMenu — menu "GIOCHI LASERDISC" (Dragon's Lair, Space Ace,
//                 Dragon's Lair II).
// ============================================================================
// Questi giochi NON hanno la ROM in flash: video (.mjpg+.idx) e audio (.pcm)
// stanno sulla microSD. Il menu monta la SD e mostra a video l'esito: sul P4 il
// Serial Monitor non funziona, quindi la diagnostica DEVE essere a schermo.

// Voci = loghi, come la griglia "GIOCHI ARCADE" ma PIU' GRANDI del 30%
// (richiesta utente 2026-07-18): logo 94x62 (era 72x48) dentro una cella 99x80
// (era 77x66), cornice ambra sul selezionato. Qui NON valgono piu'
// GCELL_*/GLOGO_* della griglia arcade: quelli restano 77x66 / 72x48.
// I .h dei loghi vanno rigenerati in coppia con questi define, altrimenti si
// legge fuori array: convert_laserdisc_game_logos.py (GRID_LOGO_W/H = 94/62).
//
// ⭐ 2026-07-22: con l'arrivo di DRAGON'S LAIR II le voci sono 3 e NON stanno
// piu' su una riga sola: 3*99 = 297 px contro i 232 utili (x_p 8..239) = si
// leggeva fuori array. Scelta utente: GRIGLIA A 2 COLONNE con i loghi GRANDI
// (94x62 invariati), riempita riga per riga:
//     [0 DL ][1 ACE]
//     [2 DL2]
// Le prime due celle restano ESATTAMENTE dove erano (LD_ROW_X0 21 con 2
// colonne = la vecchia formula a 2 giochi): DL e Space Ace non si spostano di
// un pixel. Con la 4a voce la griglia si chiude in un 2x2 senza toccare nulla.
#define LD_CELL_W     99      // 94 + 5 (stesso margine relativo di GCELL_W)
#define LD_CELL_H     80      // 62 + 18 (idem GCELL_H)
#define LD_LOGO_W     94      // = GRID_LOGO_W del converter
#define LD_LOGO_H     62      // = GRID_LOGO_H del converter
#define LD_COLS        2      // colonne della griglia (2*99 = 198 <= 232 utili)
#define LD_ROWS       ((LD_GAME_N + LD_COLS - 1) / LD_COLS)   // 3 giochi -> 2 righe
#define LD_ROW_Y     104      // y_p della PRIMA riga di celle (banner finisce a 95)
#define LD_ROW_X0     21      // 8 + (224 - LD_COLS*LD_CELL_W)/2 -> centrate
// Le celle ora finiscono a LD_ROW_Y + LD_ROWS*LD_CELL_H = 104 + 160 = 264:
// tutto cio' che sta sotto e' stato spostato in giu' di conseguenza.
#define LD_Y_WHY     268      // y_p "VIDEO MANCANTI" (sotto le celle)
#define LD_Y_STATUS  286      // y_p riga stato SD
// Riga diagnostica del menu LASER DISC: "IRAM 100K MAX 47K RST PANIC".
// 0 = nascosta (default: menu pulito). Metti 1 quando serve capire un riavvio
// o la RAM: sul P4 non c'e' Serial, questo e' l'UNICO canale — ed e' cio' che
// ha identificato lo stack overflow del player (RST PANIC) in un flash.
// ⚠️ RIMESSO A 0 il 2026-08-29, a difetto CHIUSO: il menu LASER DISC torna
// pulito. La riga IRAM/RST ha fatto il suo lavoro (ha identificato lo stack
// overflow del player in un flash) e resta a un carattere di distanza.
#define LD_DEBUG_HUD 0   // 0 = menu pulito (metti 1 per la riga IRAM/RST in debug)
// 300, non piu' 268: quella y ora e' di "VIDEO MANCANTI" (griglia a 2 righe).
// E' l'ultima riga libera prima della hint (308): con LD_DEBUG_HUD 1 le due
// scritte si toccano ma non si sovrappongono (carattere alto 8).
#define LD_Y_HEAP    300      // y_p riga RAM interna libera (misura, non stima)

struct LdGame {
  const char *name;
  const char *dir;            // cartella sulla SD
  const char *year;
  const unsigned short *logo; // 94x62 RGB565 byte-swapped (convert_laserdisc_game_logos.py)
  // File indispensabile OLTRE ai .mjpg perche' il gioco sia avviabile, nullptr
  // se bastano i clip. Prima era un caso speciale scritto a mano per Space Ace
  // dentro probe_sd(); con il terzo gioco (DL2 vuole dl2.meta per la mappa
  // frame-laserdisc -> clip) diventava il secondo "if" hardcodato -> promosso
  // a campo della tabella.
  const char *meta;
  const char *tag;            // etichetta breve MAIUSCOLA per la riga stato SD
};
static const LdGame LD_GAMES[LD_GAME_N] = {
  // Dragon's Lair: la lista dei clip e' in flash (lair_framefile.h) -> niente meta
  { "DRAGON'S LAIR",    SD_MOUNT_POINT "/dl",  "1983", dl_grid_logo,  nullptr,    "DL"  },
  { "SPACE ACE",        SD_MOUNT_POINT "/ace", "1984", ace_grid_logo, "ace.meta", "ACE" },
  { "DRAGON'S LAIR II", SD_MOUNT_POINT "/dl2", "1991", dl2_grid_logo, "dl2.meta", "DL2" },
};

// Conta i .mjpg in una cartella (0 se assente). Serve sia per sapere se il
// gioco e' installato, sia per mostrare a schermo quanti clip ci sono.
static int ld_count_mjpg(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) return 0;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != nullptr) {
    size_t l = strlen(e->d_name);
    if (l > 5 && strcasecmp(e->d_name + l - 5, ".mjpg") == 0) n++;
  }
  closedir(d);
  return n;
}

void LaserdiscMenu::probe_sd() {
  for (int i = 0; i < LD_GAME_N; i++) { _avail[i] = false; _nclips[i] = 0; }

  _sd_ok = sd_mount();
  if (!_sd_ok) return;

  for (int i = 0; i < LD_GAME_N; i++) {
    _nclips[i] = ld_count_mjpg(LD_GAMES[i].dir);
    _avail[i]  = (_nclips[i] > 0);

    // I clip da soli non bastano se il gioco ha un file di mappa:
    //   Space Ace  -> segmentato (seg000.mjpg + ace.meta): con solo intro.mjpg
    //                 avremmo l'attract ma non il gioco.
    //   DL2        -> dl2.meta e' la mappa frame-laserdisc -> clip, senza la
    //                 quale il player non sa quale .mjpg aprire.
    if (_avail[i] && LD_GAMES[i].meta) {
      struct stat st;
      char p[128];
      snprintf(p, sizeof(p), "%s/%s", LD_GAMES[i].dir, LD_GAMES[i].meta);
      if (stat(p, &st) != 0) _avail[i] = false;
    }
  }
}

void LaserdiscMenu::init(Input *input, unsigned short *out_strip) {
  _input        = input;
  _out_strip    = out_strip;
  _sel          = 0;
  _finished     = false;
  _last_buttons = 0xFF;
  _heap_free_k  = 0;            // valorizzati a ogni render()
  _heap_big_k   = 0;

  probe_sd();

  // parti dal primo gioco disponibile, se c'e'
  for (int i = 0; i < LD_GAME_N; i++)
    if (_avail[i]) { _sel = i; break; }
}

void LaserdiscMenu::update() {
  unsigned char b = _input->buttons_get();

  // FIRE avvia solo se il gioco c'e' davvero sulla SD
  if ((b & BUTTON_FIRE) && !(_last_buttons & BUTTON_FIRE)) {
    if (_avail[_sel]) _finished = true;
    _last_buttons = b;
    return;
  }
  // Griglia a 2 colonne: LEFT/RIGHT (e lo SPIN) scorrono le voci una a una in
  // ordine di lettura, UP/DOWN saltano di una riga restando nella colonna.
  // Prima erano tutti equivalenti (unica riga): con 2 righe UP/DOWN devono
  // muoversi in verticale, o si perde il modo di raggiungere la riga sotto
  // senza attraversare tutta la lista.
  if ((b & BUTTON_RIGHT) && !(_last_buttons & BUTTON_RIGHT)) {
    _sel = (_sel + 1) % LD_GAME_N;
  } else if ((b & BUTTON_LEFT) && !(_last_buttons & BUTTON_LEFT)) {
    _sel = (_sel - 1 + LD_GAME_N) % LD_GAME_N;
  } else if ((b & BUTTON_DOWN) && !(_last_buttons & BUTTON_DOWN)) {
    // giu' di una riga; se sotto non c'e' nulla, torna in cima alla colonna
    int n = _sel + LD_COLS;
    _sel = (n < LD_GAME_N) ? n : (_sel % LD_COLS);
  } else if ((b & BUTTON_UP) && !(_last_buttons & BUTTON_UP)) {
    // su di una riga; se sopra non c'e' nulla, va all'ULTIMA voce della colonna
    int n = _sel - LD_COLS;
    if (n < 0) {
      n = _sel % LD_COLS;
      while (n + LD_COLS < LD_GAME_N) n += LD_COLS;
    }
    _sel = n;
  }
  _last_buttons = b;
}

void LaserdiscMenu::draw_strip(int strip_y) {
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  const unsigned short C_WHITE = 0xFFFF;
  const unsigned short C_CYAN  = 0xFF07;
  const unsigned short C_AMBER = 0x60FE;
  const unsigned short C_GREY  = 0xEF7B;   // voce non disponibile
  const unsigned short C_RED   = 0x00F8;

  // banner 224x96 in cima (x_p 17.., y_p 2..97) -> +5px a destra, +2px in basso
  {
    int x_p_min = 232 - strip_y;  if (x_p_min < 17)  x_p_min = 17;
    int x_p_max = 239 - strip_y;  if (x_p_max > 239) x_p_max = 239;
    for (int x_p = x_p_min; x_p <= x_p_max; x_p++) {
      int hx = x_p - 17;
      if (hx < 0 || hx >= LASERDISC_CARD_W) continue;
      int y_in = (239 - x_p) - strip_y;
      if (y_in < 0 || y_in >= 8) continue;
      unsigned short *dst = _out_strip + y_in * TFT_FB_W;
      for (int hy = 0; hy < LASERDISC_CARD_H; hy++) {
        if (hy + 2 >= TFT_FB_W) break;
        dst[hy + 2] = laserdisc_card_logo[hy * LASERDISC_CARD_W + hx];
      }
    }
  }

  // voci: loghi affiancati, come le celle della griglia "GIOCHI ARCADE".
  // Il titolo del gioco e' DENTRO il logo (sono le cover), quindi niente testo.
  for (int i = 0; i < LD_GAME_N; i++) {
    int cell_x = LD_ROW_X0 + (i % LD_COLS) * LD_CELL_W;
    int cell_y = LD_ROW_Y  + (i / LD_COLS) * LD_CELL_H;
    int lx = cell_x + (LD_CELL_W - LD_LOGO_W) / 2;
    int ly = cell_y + (LD_CELL_H - LD_LOGO_H) / 2;

    // logo dimmato se il gioco non e' avviabile (era il grigio del testo)
    draw_logo_box(_out_strip, strip_y, lx, ly, LD_LOGO_W, LD_LOGO_H,
                  LD_GAMES[i].logo, !_avail[i]);

    if (i == _sel)
      draw_box_frame(_out_strip, strip_y, cell_x + 1, cell_y + 1,
                     LD_CELL_W - 2, LD_CELL_H - 2,
                     _avail[i] ? C_AMBER : C_GREY);
  }

  // Motivo per cui il gioco non e' avviabile: senza Serial e' l'unico canale.
  // ⚠️ Solo per la voce SELEZIONATA, e su UNA riga fissa sotto la griglia:
  // prima veniva scritto per OGNI gioco mancante alla stessa y, quindi con piu'
  // di un gioco assente le scritte si sovrapponevano (con 2 voci si notava a
  // malapena, con 3 no).
  if (!_avail[_sel]) {
    const char *why = _sd_ok ? "VIDEO MANCANTI" : "NO SD";
    int wx = 8 + (232 - text_width(why)) / 2;
    if (wx < 8) wx = 8;
    for (int k = 0; why[k]; k++)
      blit_char_rot(_out_strip, strip_y, wx + k * 8, LD_Y_WHY, why[k], C_GREY);
  }

  // stato SD
  {
    char st[40];
    if (!_sd_ok) {
      snprintf(st, sizeof(st), "%s", sd_status_str());
    } else {
      // Clip del gioco SELEZIONATO, non piu' sempre quelli di Dragon's Lair:
      // con 3 giochi la riga doveva dire qualcosa sulla voce che si sta
      // guardando. tag in MAIUSCOLO: il font del menu non ha le minuscole.
      snprintf(st, sizeof(st), "SD OK  %s:%d CLIP",
               LD_GAMES[_sel].tag, _nclips[_sel]);
    }
    int x = 8 + (232 - text_width(st)) / 2;
    if (x < 8) x = 8;                  // come la hint: mai fuori dal bordo
    for (int k = 0; st[k]; k++)
      blit_char_rot(_out_strip, strip_y, x + k * 8, LD_Y_STATUS, st[k],
                    _sd_ok ? C_CYAN : C_RED);
  }

#if LD_DEBUG_HUD
  // RAM INTERNA LIBERA + motivo dell'ultimo reset.
  // Il "98%/99% di memoria dinamica" del report Arduino e' calcolato contro
  // esp32p4.upload.maximum_data_size=327680 (boards.txt), un valore copiato
  // dall'ESP32 classico che NON e' la memoria del P4: e' inaffidabile.
  // FREE = heap interno totale libero; MAX = blocco contiguo piu' grande
  // (quello che conta per stack dei task e buffer DMA, che NON possono stare
  // in PSRAM). Sul P4 non c'e' Serial -> a schermo.
  // I valori arrivano da render(): qui si LEGGONO soltanto (vedi boot_menu.h).
  {
    // Motivo dell'ULTIMO reset: senza Serial e' l'unico modo per sapere se il
    // dispositivo si e' riavviato per watchdog, panic o brownout.
    //   WDT   = task watchdog: un loop non ha ceduto la CPU
    //   PANIC = crash (puntatore nullo, stack overflow...)
    //   BROWN = brownout: caduta di tensione (picco SD+display+decode)
    const char *rr;
    switch (esp_reset_reason()) {
      case ESP_RST_TASK_WDT: rr = "WDT";   break;
      case ESP_RST_INT_WDT:  rr = "IWDT";  break;
      case ESP_RST_WDT:      rr = "HWDT";  break;
      case ESP_RST_PANIC:    rr = "PANIC"; break;
      case ESP_RST_BROWNOUT: rr = "BROWN"; break;
      case ESP_RST_POWERON:  rr = "POWER"; break;
      case ESP_RST_SW:       rr = "SW";    break;
      default:               rr = "?";     break;
    }
    char hm[40];
    snprintf(hm, sizeof(hm), "IRAM %uK MAX %uK RST %s",
             _heap_free_k, _heap_big_k, rr);
    int hx2 = 8 + (232 - text_width(hm)) / 2;
    if (hx2 < 8) hx2 = 8;
    for (int k = 0; hm[k]; k++)
      blit_char_rot(_out_strip, strip_y, hx2 + k * 8, LD_Y_HEAP, hm[k], C_AMBER);
  }
#endif  // LD_DEBUG_HUD

  // Identica alla hint del menu GIOCHI ARCADE (draw_strip di GamesGridMenu):
  // stessa stringa, stesso clamp, stessa y. La versione lunga
  // ("FIRE: GIOCA  START 3s: INDIETRO") e' 31 char = 248 px e NON entra nei
  // 232 px utili: hx finiva a 0 e le ultime lettere uscivano dal display.
  const char *hint = "FIRE GIOCA  START 3s ESCI";   // 25 char * 8 = 200 px
  int hx = 8 + (232 - text_width(hint)) / 2;
  if (hx < 8) hx = 8;
  for (int k = 0; hint[k]; k++)
    blit_char_rot(_out_strip, strip_y, hx + k * 8, 308, hint[k], C_CYAN);
}

// ============================================================================
// NeoGeoMenu — menu "GIOCHI NEO GEO" (Metal Slug, Metal Slug 2).
// ============================================================================
// Gemello di LaserdiscMenu: stesse celle, stesse cornici, stessa hint. Le due
// differenze sono dichiarate nell'intestazione della classe:
//   1. i dati del gioco sono su SD (26,28 MB: in flash non ci starebbero);
//   2. anche la COVER e' su SD (/neogeo/<gioco>.scr), come su CABINATONE.
// Le misure sono le stesse del menu laserdisc, riusate di proposito: LD_CELL_*
// e LD_LOGO_* sono gia' definite sopra e valgono per tutti e due i menu.
#define NG_ROW_Y     104      // y_p della prima riga di celle (il banner finisce a 95)
#define NG_ROW_X0     21      // 8 + (224 - 2*LD_CELL_W)/2  -> due colonne centrate
#define NG_Y_WHY     268      // motivo per cui la voce non e' avviabile
#define NG_Y_STATUS  286      // riga stato SD

struct NgGame {
  const char *name;
  const char *set;            // nome del set = cartella sulla SD e riga di
                              // NG_GAMES dentro neogeo.cpp (si passa a
                              // neogeo_play: mai un indice, vedi boot_menu.h)
  const char *dir;            // cartella dei dati sulla SD
  const char *scr;            // cover .scr sulla SD (RGB565 LE + header 6 byte)
  // Un file che DEVE esserci perche' il gioco sia avviabile. Si controlla il
  // piu' grande (gli sprite): se c'e' quello, la copia sulla SD e' andata a
  // termine. Controllare solo la cartella direbbe "c'e'" anche a meta' copia.
  const char *probe;
  // Secondo file obbligatorio, oppure nullptr. ⛔ Serve a Metal Slug 2: senza
  // `sram.bin` il gioco CARICA, parte e poi si pianta per sempre sulla schermata
  // "ORIGINAL SNK BOARDS ONLY" (protezione SNK: la backup RAM vergine le fa
  // credere che sia il primo avvio). Un guasto che non si spiega da solo e'
  // peggio di un guasto dichiarato: qui diventa "DATI MANCANTI" nel menu.
  const char *probe2;
  const char *tag;            // etichetta breve per la riga di stato
};
static const NgGame NG_GAMES[NG_GAME_N] = {
  { "METAL SLUG", "mslug",
    SD_MOUNT_POINT "/neogeo/mslug", SD_MOUNT_POINT "/neogeo/mslug.scr",
    "c.ctile", nullptr, "MSLUG" },
  // ⚠️ Metal Slug 2 e' 43,31 MB: i 32 MB di sprite NON entrano in PSRAM e
  // restano sulla SD, letti a banchi da gfx_cache (`spr_cache` in neogeo.cpp).
  // Il file di prova resta `c.ctile` — non viene caricato in memoria, ma deve
  // esserci lo stesso, e anzi e' proprio quello che la cache legge in corsa.
  { "METAL SLUG 2", "mslug2",
    SD_MOUNT_POINT "/neogeo/mslug2", SD_MOUNT_POINT "/neogeo/mslug2.scr",
    "c.ctile", "sram.bin", "MSLUG2" },
  // View Point: 11,26 MB, tutto in PSRAM (dal CABINATONE, stessi file SD).
  { "VIEW POINT", "viewpoin",
    SD_MOUNT_POINT "/neogeo/viewpoin", SD_MOUNT_POINT "/neogeo/viewpoin.scr",
    "c.ctile", nullptr, "VIEWPOIN" },
  // Pulstar: 39,30 MB, stessa forma di Metal Slug 2 — i 28 MB di sprite restano
  // sulla SD (gfx_cache). ⛔ Come Metal Slug 2 fa lo «slot check»: senza
  // `sram.bin` resta per sempre sul WARNING «ORIGINAL SNK BOARDS ONLY»
  // (provato nel banco offline senza watchdog, come gira qui).
  { "PULSTAR", "pulstar",
    SD_MOUNT_POINT "/neogeo/pulstar", SD_MOUNT_POINT "/neogeo/pulstar.scr",
    "c.ctile", "sram.bin", "PULSTAR" },
};

const char *NeoGeoMenu::selectedSet() const { return NG_GAMES[_sel].set; }

// Il BIOS e' COMUNE a tutti i giochi NEO GEO: senza, nessuno parte.
static const char *NG_BIOS_DIR = SD_MOUNT_POINT "/neogeo/bios";
static const char *NG_BIOS_FILES[] = { "sp.bin", "sfix.bin", "lo.bin", "sm1.bin" };

static bool ng_file_exists(const char *dir, const char *name) {
  struct stat st;
  char p[160];
  snprintf(p, sizeof(p), "%s/%s", dir, name);
  return stat(p, &st) == 0 && st.st_size > 0;
}

// Carica un .scr e restituisce un buffer RGB565 GIA' BYTE-SWAPPATO per il
// pannello SPI. Accetta solo la dimensione attesa: una cover di misura diversa
// verrebbe letta fuori dalla cella (il .scr porta le sue w/h, ma il disegno
// usa LD_LOGO_W/H, quindi le due DEVONO coincidere).
static unsigned short *ng_load_scr(const char *path, int w, int h) {
  FILE *f = fopen(path, "rb");
  if (!f) return nullptr;
  unsigned short hdr[3];
  if (fread(hdr, sizeof(hdr), 1, f) != 1) { fclose(f); return nullptr; }
  if (hdr[0] != 0x5343 || hdr[1] != (unsigned short)w || hdr[2] != (unsigned short)h) {
    fclose(f);
    return nullptr;
  }
  const size_t npx = (size_t)w * h;
  unsigned short *buf =
      (unsigned short *)heap_caps_malloc(npx * 2, MALLOC_CAP_SPIRAM);
  if (!buf) buf = (unsigned short *)malloc(npx * 2);
  if (!buf) { fclose(f); return nullptr; }
  if (fread(buf, 2, npx, f) != npx) { free(buf); fclose(f); return nullptr; }
  fclose(f);
  // little-endian sul file -> byte-swapped per lo SPI, una volta sola qui
  for (size_t i = 0; i < npx; i++)
    buf[i] = (unsigned short)((buf[i] << 8) | (buf[i] >> 8));
  return buf;
}

void NeoGeoMenu::free_covers() {
  for (int i = 0; i < NG_GAME_N; i++) {
    if (_cover[i]) { heap_caps_free(_cover[i]); _cover[i] = nullptr; }
  }
}

void NeoGeoMenu::probe_sd() {
  for (int i = 0; i < NG_GAME_N; i++) _avail[i] = false;
  _bios_ok = false;

  _sd_ok = sd_mount();
  if (!_sd_ok) return;

  _bios_ok = true;
  for (unsigned i = 0; i < sizeof(NG_BIOS_FILES) / sizeof(NG_BIOS_FILES[0]); i++)
    if (!ng_file_exists(NG_BIOS_DIR, NG_BIOS_FILES[i])) { _bios_ok = false; break; }

  for (int i = 0; i < NG_GAME_N; i++) {
    // I dati del gioco senza BIOS non bastano: il 68000 parte dentro il BIOS
    // (PC = 0x00C00402), quindi un gioco senza BIOS non e' avviabile.
    _avail[i] = _bios_ok && ng_file_exists(NG_GAMES[i].dir, NG_GAMES[i].probe) &&
                (!NG_GAMES[i].probe2 ||
                 ng_file_exists(NG_GAMES[i].dir, NG_GAMES[i].probe2));
    if (!_cover[i]) _cover[i] = ng_load_scr(NG_GAMES[i].scr, LD_LOGO_W, LD_LOGO_H);
  }
}

void NeoGeoMenu::init(Input *input, unsigned short *out_strip) {
  _input        = input;
  _out_strip    = out_strip;
  _sel          = 0;
  _finished     = false;
  _last_buttons = 0xFF;
  // ⚠️ init() viene richiamata a ogni rientro nel menu: le cover si liberano
  // prima di ricaricarle, altrimenti ogni giro perde 11,6 KB di PSRAM.
  free_covers();

  probe_sd();

  for (int i = 0; i < NG_GAME_N; i++)
    if (_avail[i]) { _sel = i; break; }
}

void NeoGeoMenu::update() {
  unsigned char b = _input->buttons_get();

  if ((b & BUTTON_FIRE) && !(_last_buttons & BUTTON_FIRE)) {
    if (_avail[_sel]) _finished = true;
    _last_buttons = b;
    return;
  }
  if ((b & BUTTON_RIGHT) && !(_last_buttons & BUTTON_RIGHT)) {
    _sel = (_sel + 1) % NG_GAME_N;
  } else if ((b & BUTTON_LEFT) && !(_last_buttons & BUTTON_LEFT)) {
    _sel = (_sel - 1 + NG_GAME_N) % NG_GAME_N;
  } else if ((b & BUTTON_DOWN) && !(_last_buttons & BUTTON_DOWN)) {
    int n = _sel + LD_COLS;
    _sel = (n < NG_GAME_N) ? n : (_sel % LD_COLS);
  } else if ((b & BUTTON_UP) && !(_last_buttons & BUTTON_UP)) {
    int n = _sel - LD_COLS;
    if (n < 0) {
      n = _sel % LD_COLS;
      while (n + LD_COLS < NG_GAME_N) n += LD_COLS;
    }
    _sel = n;
  }
  _last_buttons = b;
}

void NeoGeoMenu::draw_strip(int strip_y) {
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));

  const unsigned short C_WHITE = 0xFFFF;
  const unsigned short C_CYAN  = 0xFF07;
  const unsigned short C_AMBER = 0x60FE;
  const unsigned short C_GREY  = 0xEF7B;
  const unsigned short C_RED   = 0x00F8;

  // banner 224x96 in cima, stessa collocazione del menu laserdisc
  {
    int x_p_min = 232 - strip_y;  if (x_p_min < 17)  x_p_min = 17;
    int x_p_max = 239 - strip_y;  if (x_p_max > 239) x_p_max = 239;
    for (int x_p = x_p_min; x_p <= x_p_max; x_p++) {
      int hx = x_p - 17;
      if (hx < 0 || hx >= NEOGEO_CARD_W) continue;
      int y_in = (239 - x_p) - strip_y;
      if (y_in < 0 || y_in >= 8) continue;
      unsigned short *dst = _out_strip + y_in * TFT_FB_W;
      for (int hy = 0; hy < NEOGEO_CARD_H; hy++) {
        if (hy + 2 >= TFT_FB_W) break;
        dst[hy + 2] = neogeo_card_logo[hy * NEOGEO_CARD_W + hx];
      }
    }
  }

  // voci: cover 94x62 caricate da SD, celle e cornici come il menu laserdisc
  for (int i = 0; i < NG_GAME_N; i++) {
    int cell_x = NG_ROW_X0 + (i % LD_COLS) * LD_CELL_W;
    int cell_y = NG_ROW_Y  + (i / LD_COLS) * LD_CELL_H;
    int lx = cell_x + (LD_CELL_W - LD_LOGO_W) / 2;
    int ly = cell_y + (LD_CELL_H - LD_LOGO_H) / 2;

    if (_cover[i]) {
      draw_logo_box(_out_strip, strip_y, lx, ly, LD_LOGO_W, LD_LOGO_H,
                    _cover[i], !_avail[i]);
    } else {
      // Nessuna cover sulla SD: il nome a testo, cosi' la voce resta leggibile
      // invece di essere una cella nera.
      const char *nm = NG_GAMES[i].name;
      int nx = cell_x + (LD_CELL_W - text_width(nm)) / 2;
      if (nx < 8) nx = 8;
      for (int k = 0; nm[k]; k++)
        blit_char_rot(_out_strip, strip_y, nx + k * 8, cell_y + LD_CELL_H / 2 - 4,
                      nm[k], _avail[i] ? C_WHITE : C_GREY);
    }

    if (i == _sel)
      draw_box_frame(_out_strip, strip_y, cell_x + 1, cell_y + 1,
                     LD_CELL_W - 2, LD_CELL_H - 2,
                     _avail[i] ? C_AMBER : C_GREY);
  }

  // Perche' la voce selezionata non e' avviabile: senza Serial e' l'unico
  // canale, e i due motivi si curano in modo diverso (copiare il gioco oppure
  // copiare il BIOS), quindi vanno distinti.
  if (!_avail[_sel]) {
    const char *why = !_sd_ok    ? "NO SD"
                    : !_bios_ok  ? "BIOS MANCANTE"
                                 : "DATI MANCANTI";
    int wx = 8 + (232 - text_width(why)) / 2;
    if (wx < 8) wx = 8;
    for (int k = 0; why[k]; k++)
      blit_char_rot(_out_strip, strip_y, wx + k * 8, NG_Y_WHY, why[k], C_GREY);
  }

  // stato SD
  {
    char st[40];
    if (!_sd_ok) snprintf(st, sizeof(st), "%s", sd_status_str());
    else         snprintf(st, sizeof(st), "SD OK  %s %s", NG_GAMES[_sel].tag,
                          _avail[_sel] ? "PRONTO" : "ASSENTE");
    int x = 8 + (232 - text_width(st)) / 2;
    if (x < 8) x = 8;
    for (int k = 0; st[k]; k++)
      blit_char_rot(_out_strip, strip_y, x + k * 8, NG_Y_STATUS, st[k],
                    _sd_ok ? C_CYAN : C_RED);
  }

  const char *hint = "FIRE GIOCA  START 3s ESCI";
  int hx = 8 + (232 - text_width(hint)) / 2;
  if (hx < 8) hx = 8;
  for (int k = 0; hint[k]; k++)
    blit_char_rot(_out_strip, strip_y, hx + k * 8, 308, hint[k], C_CYAN);
}

void NeoGeoMenu::render() {
  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}

void LaserdiscMenu::render() {
#if LD_DEBUG_HUD
  // Campiona la RAM interna UNA VOLTA per frame, PRIMA delle strip.
  // ⚠️ heap_caps_get_largest_free_block() prende il lock dell'heap e scorre
  // tutti i blocchi liberi: dentro draw_strip() (29 chiamate a frame) inchioda
  // il menu, e con esso FIRE e il long-press di START.
  _heap_free_k = (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024);
  _heap_big_k  = (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024);
#endif

  int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
  }
}
