#ifndef BOOT_MENU_H
#define BOOT_MENU_H

#include <Arduino.h>
#include "input.h"
#include "../machines/machineBase.h"

// ============================================================================
// BootMenu — carousel orizzontale per selezione gioco a boot.
//
// Layout (320x232 utili, ROT270 portrait fisica):
//   y=  0..15   "ARCADE COLLECTION" (titolo, font 8x8)
//   y= 32..127  area logo 224x96 (centrato x=48..271, scroll horizontal)
//   y=144..151  nome gioco (font 8x8)
//   y=168..175  pagination "<  N / TOT  >"
//   y=200..207  hint "SPIN: change   FIRE: start"
//
// Input: EC11 ruota = scroll giochi (loop infinito), FIRE = avvia.
// Animazione scroll fluida (~10 frame), durante l'animazione l'input e'
// ignorato.
// ============================================================================
// --- Voci EXTRA del carosello, in coda ai `count` giochi paddle ---
// L'indice della voce nel carosello e' count + EXTRA_*. Erano confronti
// numerici sparsi (count+1, count+2...) in punti scoordinati tra boot_menu.cpp
// e SPINNERINO.ino: ogni aggiunta era un'occasione di off-by-one.
// Per aggiungere una voce: definisci l'EXTRA_*, alza EXTRA_COUNT e gestiscila
// in BootMenu::machine_name() + BootMenu::draw_entry() + il dispatch in
// SPINNERINO.ino::loop().
// ⭐ L'ORDINE DI QUESTI NUMERI E' L'ORDINE NEL CAROSELLO: per spostare una voce
// basta rinumerare qui, perche' tutto il resto del firmware usa i simboli e non
// i numeri (e' esattamente il motivo per cui esistono).
#define EXTRA_SETTINGS   0        // "IMPOSTAZIONI"
#define EXTRA_TEST       1        // "TEST JOYSTICK E PULSANTI" -> schermata prove
#define EXTRA_WIFI       2        // "FILE MANAGER WIFI" -> file sulla SD dal browser (wifi_fm.cpp)
#define EXTRA_ARCADE     3        // "GIOCHI ARCADE"    -> griglia joystick
#define EXTRA_LASERDISC  4        // "GIOCHI LASERDISC" -> menu laserdisc (da SD)
#define EXTRA_NEOGEO     5        // "GIOCHI NEO GEO"   -> menu NeoGeo (da SD)
#define EXTRA_COUNT      6

class BootMenu {
public:
  void init(Input *input, machineBase **machines, int count,
            unsigned short *out_strip, int start_idx = 0);
  void update();        // gestisce input + tick animazione (chiamare ogni frame)
  void render();        // disegna a video (chiamare ogni frame)
  bool finished() const { return _finished; }
  int  selectedIndex() const { return _current_idx; }
  void attract_resetTimer() { _attract_t0 = millis(); _attract_armed = true; }

private:
  Input         *_input;
  machineBase  **_machines;
  int            _count;
  unsigned short *_out_strip;     // buffer 320x8 condiviso per write strip

  int  _current_idx;              // indice gioco al centro
  int  _next_idx;                 // indice durante transizione
  int  _anim_frame;               // 0 = idle, 1..N = in transizione
  int  _anim_dir;                 // -1 = scroll a sinistra, +1 = a destra
  bool _finished;                 // true quando l'utente ha premuto FIRE

  unsigned char _last_buttons;    // edge detection FIRE
  unsigned long _attract_t0;      // master attract menu timeout origin
  bool          _attract_armed;   // true = attract pending, false = disabled

  // Render helpers
  void draw_strip(int strip_y);   // riempie _out_strip con la strip globale y
  void draw_text(int strip_y, int x, int y_in_strip,
                 const char *text, unsigned short color);
  void draw_logo(int strip_y, int x_offset, const unsigned short *logo);
  void draw_entry(int strip_y, int x_offset, int idx);  // gioco o voce extra
  void draw_settings_card(int strip_y, int x_offset);   // "card" voce IMPOSTAZIONI
  void draw_test_card(int strip_y, int x_offset);       // "card" voce TEST JOYSTICK
  void draw_arcade_card(int strip_y, int x_offset);     // "card" voce GIOCHI ARCADE
  void draw_laserdisc_card(int strip_y, int x_offset);  // "card" voce GIOCHI LASERDISC
  void draw_neogeo_card(int strip_y, int x_offset);     // "card" voce GIOCHI NEO GEO
  void draw_wifi_card(int strip_y, int x_offset);       // "card" voce FILE MANAGER WIFI
  const char *machine_name(int idx);
};

// ============================================================================
// GamesGridMenu — nuovo menu "GIOCHI ARCADE" per i giochi joystick portati da
// GALAGONE (Xevious, Gyruss, ...). Si apre dalla voce "GIOCHI ARCADE" del
// carosello BootMenu.
//
// Layout PORTRAIT 240x320:
//   y_p=  0.. 95  banner "GIOCHI ARCADE" (giochi_header.h, 232x96)
//   y_p=104..301  griglia 3x3 di loghi 72x48 (celle 77x66), scroll verticale
//   y_p=308..315  hint "FIRE GIOCA  START 3s ESCI" (25 char: la versione lunga
//                 con i ":" e "INDIETRO" e' 248 px e NON entra nei 232 utili)
//
// Controlli: D-pad BT / EC11 = sposta cursore (LEFT/RIGHT lineare, UP/DOWN per
// riga). FIRE = avvia il gioco selezionato. START tenuto 3s = torna al
// carosello (gestito nel .ino via input_consume_back_to_menu()).
// ============================================================================
class GamesGridMenu {
public:
  void init(Input *input, unsigned short *out_strip);
  void update();
  void render();
  bool finished() const { return _finished; }   // FIRE su un gioco
  void clearFinished() { _finished = false; }    // gioco non ancora portato: resta
  int  selectedIndex() const { return _sel; }    // indice gioco joystick scelto
  int  count() const;

private:
  Input          *_input;
  unsigned short *_out_strip;
  int  _sel;
  int  _scroll;          // prima riga visibile (scroll verticale)
  bool _finished;
  unsigned char _last_buttons;

  void draw_strip(int strip_y);
  void draw_header(int strip_y);
  void draw_cell_logo(int strip_y, int x_p0, int y_p0, int w, int h,
                      const unsigned short *logo);
  void draw_frame(int strip_y, int x0, int y0, int w, int h, unsigned short col);
};

// ============================================================================
// SettingsMenu — schermata IMPOSTAZIONI: lista di tutti i giochi con slider
// per la sensibilita' EC11 (livello 1..10, 5 = fabbrica). Si apre dalla voce
// "IMPOSTAZIONI" del carosello BootMenu.
//
// Controlli:
//   SPIN (EC11)  : in navigazione scorre i giochi; in modifica cambia il livello
//   FIRE         : entra/esce dalla modalita' modifica del gioco selezionato
//   START        : salva ed esce (torna al menu giochi)
//
// Il livello vive in machines[i]->ec11SensLevel (applicato live); il salvataggio
// su NVS lo fa il chiamante (.ino) quando finished() diventa true.
// ============================================================================
class SettingsMenu {
public:
  void init(Input *input, machineBase **machines, int count,
            unsigned short *out_strip);
  void update();
  void render();
  bool finished() const { return _finished; }

private:
  Input         *_input;
  machineBase  **_machines;
  int            _count;
  unsigned short *_out_strip;

  int  _sel;            // riga/gioco selezionato (0.._count-1)
  bool _edit;           // false = naviga lista, true = modifica livello
  int  _scroll;         // prima riga visibile (per liste lunghe)
  bool _finished;       // true = salva ed esci
  unsigned char _last_buttons;

  void draw_strip(int strip_y);
};

// ============================================================================
// TestMenu — schermata "TEST JOYSTICK E PULSANTI". Si apre dalla voce omonima
// del carosello BootMenu (cornice ROSSA, per distinguerla dalle altre).
//
// A cosa serve: verificare che ogni comando arrivi davvero al firmware, senza
// dover entrare in un gioco e indovinare. Ogni elemento si ACCENDE quando il
// comando corrispondente e' attivo:
//   - le 4 direzioni del joystick, disposte a croce come sul mobile;
//   - i 4 pulsanti: A (fire), B (extra), START, COIN;
//   - l'encoder EC11: barra 0..255 del paddle, contatore del dial e il verso
//     dell'ultimo scatto. Il pulsante dell'encoder e' cablato su FIRE, quindi
//     premendolo si accende "A": e' scritto a schermo per non farlo sembrare
//     un difetto.
// ⚠️ Legge gli stessi ingressi dei giochi (`buttons_get()` e le variabili
// dell'ISR dell'EC11): se qui non si accende, non funziona nemmeno nei giochi.
//
// Uscita: START tenuto 3 s (come la griglia GIOCHI ARCADE), gestita dal .ino
// con input_consume_back_to_menu().
// ============================================================================
class TestMenu {
public:
  void init(Input *input, unsigned short *out_strip);
  void update();
  void render();

private:
  Input          *_input;
  unsigned short *_out_strip;
  unsigned char   _btn;        // maschera BUTTON_* del frame corrente
  int             _paddle;     // ec11_paddle_x  (0..255)
  int             _dial;       // ec11_dial_counter (0..255, free running)
  int             _verso;      // -1 / 0 / +1: verso dell'ultimo scatto EC11
  int             _dial_prec;  // per ricavare il verso
  unsigned long   _verso_t0;   // quando si e' visto l'ultimo scatto

  void draw_strip(int strip_y);
};

// ============================================================================
// LaserdiscMenu — menu "GIOCHI LASERDISC" (Dragon's Lair, Space Ace). Si apre
// dalla voce "GIOCHI LASERDISC" del carosello BootMenu.
//
// A differenza dei giochi arcade, questi NON hanno la ROM in flash: video e
// audio stanno sulla microSD (vedi SD SPINNERINO\LEGGIMI.txt). Il menu monta
// la SD alla init() e mostra a schermo il motivo se fallisce — sul P4 il Serial
// Monitor non funziona, quindi la diagnostica deve essere visibile a video.
//
// Layout PORTRAIT 240x320 (GRIGLIA 2 COLONNE dal 2026-07-22, era una riga sola):
//   y_p=  0.. 95  banner (laserdisc_card_logo.h, 224x96)
//   y_p=104..263  le voci = cover 94x62 (<gioco>_grid_logo.h) in celle 99x80
//                 disposte a griglia 2 colonne, riempite riga per riga:
//                   [0 DL ][1 ACE]
//                   [2 DL2]
//                 stesso sistema della griglia "GIOCHI ARCADE": cornice ambra
//                 sul selezionato. Il titolo e' dentro la cover -> niente testo.
//                 Gioco non avviabile = logo DIMMATO (dim565_swapped).
//                 Con la 4a voce la griglia diventa un 2x2 pieno senza toccare
//                 nulla: le colonne sono fisse, le righe crescono.
//   y_p=268..275  "VIDEO MANCANTI" / "NO SD" (solo per la voce SELEZIONATA:
//                 con piu' di 2 giochi le scritte si sovrapponevano)
//   y_p=286..293  stato SD
//   y_p=308..315  hint "FIRE GIOCA  START 3s ESCI" (identica a GIOCHI ARCADE)
//
// Controlli: SPIN/LEFT/RIGHT = voce precedente/successiva, UP/DOWN = riga sopra/
// sotto (stessa colonna), FIRE = avvia, START 3s = torna al carosello (gestito
// nel .ino via input_consume_back_to_menu()).
// ============================================================================
#define LD_GAME_DL   0        // Dragon's Lair       (DirkSimple, laserdisc.cpp)
#define LD_GAME_ACE  1        // Space Ace           (ROM-driven Z80, spaceace.cpp)
#define LD_GAME_DL2  2        // Dragon's Lair II    (ROM-driven 8088, dl2.cpp)
#define LD_GAME_N    3

class LaserdiscMenu {
public:
  void init(Input *input, unsigned short *out_strip);
  void update();
  void render();
  bool finished() const { return _finished; }    // FIRE su un gioco disponibile
  void clearFinished() { _finished = false; }
  int  selectedIndex() const { return _sel; }    // LD_GAME_*

private:
  Input          *_input;
  unsigned short *_out_strip;
  int  _sel;
  bool _finished;
  unsigned char _last_buttons;

  bool _sd_ok;                  // esito del mount (fatto una volta alla init)
  bool _avail[LD_GAME_N];       // gioco presente sulla SD?
  int  _nclips[LD_GAME_N];      // n. clip trovati (diagnostica a schermo)

  // RAM interna libera, campionata UNA VOLTA per frame in render().
  // ⚠️ heap_caps_get_largest_free_block() prende il lock dell'heap e scorre
  // TUTTI i blocchi liberi: chiamarla dentro draw_strip() (29 volte a frame)
  // inchioda il menu -> FIRE e long-press non vengono piu' rilevati.
  unsigned _heap_free_k;
  unsigned _heap_big_k;

  void probe_sd();              // monta la SD e popola _avail/_nclips
  void draw_strip(int strip_y);
};

// ============================================================================
// NeoGeoMenu — menu "GIOCHI NEO GEO". Si apre dalla voce omonima del carosello,
// subito a destra di "GIOCHI LASERDISC".
//
// Come i laserdisc, questi giochi NON stanno in flash: ROM, BIOS e sprite gia'
// convertiti stanno sulla microSD in /neogeo/ (prodotti da romconv_neogeo.py).
// Metal Slug da solo sono 26,78 MB + 512 KB di BIOS (Metal Slug 2 ne fa 43,31):
// in flash non ci starebbe nemmeno un decimo.
//
// ⭐ E anche la COVER viene dalla SD (/neogeo/<gioco>.scr), come su CABINATONE
// e a differenza di tutti gli altri menu di questo firmware, che hanno i loghi
// compilati dentro: cosi' aggiungere un gioco NEO GEO non costa flash.
// Il .scr e' RGB565 LITTLE-ENDIAN (formato comune ai due progetti); il pannello
// SPI vuole i byte scambiati, quindi lo swap si fa una volta al caricamento.
//
// Layout PORTRAIT 240x320 — identico a LaserdiscMenu, cosi' i tre menu di
// categoria si somigliano:
//   y_p=  0.. 95  banner "GIOCHI NEO GEO" (neogeo_card_logo.h, 224x96)
//   y_p=104..183  le voci = cover 94x62 da SD in celle 99x80, 2 colonne
//   y_p=268..275  motivo per cui una voce non e' avviabile
//   y_p=286..293  stato SD
//   y_p=308..315  hint "FIRE GIOCA  START 3s ESCI"
//
// Controlli: SPIN/LEFT/RIGHT = voce prec./succ., UP/DOWN = riga, FIRE = avvia,
// START 3s = torna al carosello (gestito nel .ino).
// ============================================================================
#define NG_GAME_MSLUG   0     // Metal Slug   (set `mslug`,  NGH-201) — 26,78 MB
#define NG_GAME_MSLUG2  1     // Metal Slug 2 (set `mslug2`, NGH-241) — 43,31 MB
#define NG_GAME_VIEWPOIN 2    // View Point   (set `viewpoin`, NGH-051) — 11,26 MB
#define NG_GAME_PULSTAR 3     // Pulstar      (set `pulstar`,  NGH-089) — 39,30 MB
#define NG_GAME_N       4     // ⚠️ la griglia 2x2 ne regge 4: il 5° vuole una 3a riga

class NeoGeoMenu {
public:
  void init(Input *input, unsigned short *out_strip);
  void update();
  void render();
  bool finished() const { return _finished; }    // FIRE su un gioco disponibile
  void clearFinished() { _finished = false; }
  int  selectedIndex() const { return _sel; }    // NG_GAME_*
  // Nome del set della voce selezionata ("mslug", "mslug2"): e' cio' che si
  // passa a neogeo_play(). ⭐ Si passa il NOME e non l'indice perche' la tabella
  // di questo menu e la NG_GAMES dell'emulatore sono due elenchi distinti: un
  // indice li accoppierebbe in silenzio, e al primo riordino partirebbe il gioco
  // sbagliato senza un solo errore di compilazione.
  const char *selectedSet() const;

private:
  Input          *_input;
  unsigned short *_out_strip;
  int  _sel;
  bool _finished;
  unsigned char _last_buttons;

  bool _sd_ok;                       // esito del mount (una volta, alla init)
  bool _avail[NG_GAME_N];            // dati del gioco presenti sulla SD?
  bool _bios_ok;                     // /neogeo/bios/sp.bin ecc. presenti
  // Cover caricate da SD, gia' byte-swappate per il pannello. nullptr se il
  // file manca: in quel caso la cella resta nera e resta il nome a testo.
  unsigned short *_cover[NG_GAME_N];

  void probe_sd();
  void free_covers();
  void draw_strip(int strip_y);
};

#endif // BOOT_MENU_H
