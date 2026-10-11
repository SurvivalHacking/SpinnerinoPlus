/*
 * ============================================================================
 * SPINNERINO PLUS
 * by Marco Prunca and Davide Gatti SH
 *
 * V1.0 - 09/26 - First Release
 * V1.1 - 10/26 - Added WIFI File manager (C6 V1.1 needed), added gapless, fixed: Moon Patrol, Time Pilot, Race USA
 * V1.2 - 10/26 - Increased file transfer speed (C6 V1.2 needed)
 * V1.3 - 10/26 - Fixed Fantasy, Super Mario. Added View Point and Pulstar
 *
 * ============================================================================
 *
 * Arduino IDE settings (esp32 core >= 3.3.8):
 *   Board:                 ESP32P4 Dev Module
 *   Flash Size:            32MB
 *   PSRAM:                 Enabled
 *   Partition Scheme:      SPINNERINO 30MB (usare il file .bat per creare la partizione custom)
 *   CPU Frequency:         360 MHz
 *   USB Mode:              Hardware CDC and JTAG
 *   USB CDC On Boot:       Enabled
 *   Upload Speed:          921600
 * ----------------------------------------------------------------------------
 *
 * Pinout (header 40-pin Waveshare ESP32-P4 WIFI6):
 *   ST7789 SPI:  MOSI=21  SCLK=20  CS=22  DC=23  RST=26  BL=27 (PWM)
 *   ES8311 I2C:  SDA=7    SCL=8    addr=0x18
 *   ES8311 I2S:  MCLK=13  SCLK=12  LRCK=10  DOUT=9  PA_EN=53
 *   EC11:        A=32     B=33     SW(FIRE)=48
 *   Buttons:     START=47
 * ----------------------------------------------------------------------------
 *   COMANDI:
 *
 *   Gioca:            ruota l'encoder EC11 = paddle/spinner; SW EC11 = FIRE;
 *                     COIN inserisce credito; START avvia.
 *
 *   REGOLA VOLUME:    tieni premuto FIRE per ~3 secondi
 *                     -> compare la barra volume verde. Poi RUOTA l'encoder:
 *                     orario (destra) = alza, antiorario (sinistra) = abbassa.
 *                     RILASCIA FIRE per confermare e salvare (NVS). Durante la
 *                     regolazione il paddle resta fermo.
 *
 *   REGOLA VOLUME (GAMEPAD, giochi della griglia GIOCHI ARCADE):
 *                     tieni premuto COIN sul gamepad BT per ~3 secondi
 *                     -> compare la barra volume. Poi usa il D-pad: SU = alza,
 *                     GIU = abbassa. RILASCIA COIN per confermare e salvare.
 *                     (Per questi giochi si usa COIN e non FIRE, perche' FIRE e'
 *                     tenuto premuto durante il gioco per lo sparo continuo.)
 *
 *   ESCI DAL GIOCO:   tieni premuto START per ~3 secondi -> torna al menu giochi.
 *
 *   IMPOSTAZIONI:     nel menu giochi, dopo l'ultimo gioco c'e' la voce
 *                     IMPOSTAZIONI per regolare la sensibilita' dell'encoder
 *                     (spinner) per ogni gioco.
 * ============================================================================
 */

#include <Arduino.h>
#include "esp_task_wdt.h"
#include "esp_system.h"          // esp_reset_reason(): causa dell'ultimo riavvio
#include "config.h"
#include "machines/machineBase.h"
#include "emulation/audio.h"
#include "emulation/video.h"
#include "emulation/input.h"
#include "emulation/menu.h"
#include "esp_heap_caps.h"        // boot_msg(): heap libero se una malloc fallisce
#include "emulation/font8.h"      // boot_msg(): FONT8 + char_to_idx
#include "emulation/boot_menu.h"
#include "emulation/wifi_fm.h"      // FILE MANAGER WIFI (schermata + file su SD)
#include "emulation/laserdisc.h"   // player video LASERDISC (fase 2)
#include "emulation/cps1944.h"     // 1944 (CPS-2): player autonomo nella griglia
#include "emulation/ghouls.h"      // Ghouls'n Ghosts (CPS-1): player autonomo
#include "emulation/outrun.h"      // Out Run (Sega 1986): player autonomo
#include "emulation/gaplus.h"      // Gaplus (Namco 1984): player autonomo, dati su SD
#include "emulation/neogeo.h"      // NEO GEO / Metal Slug 1-2: player autonomo (da SD)
#include <JPEGDEC.h>               // decoder JPEG SOFTWARE per LASERDISC (l'HW P4 crasha)

// ⛔ NIENTE SET_LOOP_TASK_STACK_SIZE(16*1024) qui.
// Ci avevo provato per dare stack al player laserdisc (jpeg_decoder_process
// sforava gli 8 KB di default -> RST PANIC): risultato = il loopTask non
// partiva piu' e la scheda NON BOOTAVA (schermo bianco, nemmeno il jingle).
// E comunque era sbagliato: avrebbe fatto pagare 16 KB di RAM interna a TUTTO
// il progetto, per sempre, anche giocando a Pac-Man.
// Il player si crea il PROPRIO task con lo stack che gli serve, e solo mentre
// gira: vedi laserdisc.cpp (LD_TASK_STACK).
#include "emulation/emulation.h"
#include "prof.h"        // strumento di misura (ARKANOID 2): vedi prof.h

// CPU cores (Z80 sempre, M6502 solo se Tempest attivo, M6803 per MotoRace)
#include "cpus/z80/Z80.c"
#if defined(ENABLE_TEMPEST) || defined(ENABLE_SBRKOUT)
#include "cpus/m6502/fake6502.c"
#endif
#if defined(ENABLE_DKONG3) || defined(ENABLE_SUPXEVS)
#include "cpus/m6502/m6502.c"   // RP2A03 per-istanza: suono DK3 e CPU di Vs. Super Xevious
#endif
#if defined(ENABLE_MOTORACE) || defined(ENABLE_MOONPATROL)
#include "cpus/m6803/m6803.c"   // M6803 sound CPU (MotoRace, Moon Patrol)
#endif
#if defined(ENABLE_GYRUSS) || defined(ENABLE_ROADFIGHTER) || defined(ENABLE_GNG)
#include "cpus/m6809/m6809.c"   // M6809 sub-CPU (Gyruss) / main CPU KONAMI-1 (Road Fighter) / main CPU (GnG)
#endif
#if defined(ENABLE_GYRUSS) || defined(ENABLE_DKONGJR) || defined(ENABLE_MARIOBROS)
#include "cpus/i8048/i8048.c"   // i8039/i8048 MCU (drums Gyruss, sound DK Jr / Mario Bros)
#endif

#ifdef ENABLE_ARKANOID
#include "machines/arkanoid/arkanoid.h"
#include "machines/arkanoid/arkanoid.cpp"
#endif
#ifdef ENABLE_TEMPEST
#include "machines/tempest/tempest.h"
#include "machines/tempest/mathbox.cpp"
#include "machines/tempest/tempest.cpp"   // include tempest_core.h e avg.cpp
#endif
// YM2203 FM emulator (Burczynski/Satoh, MAME fm.c via vgmplay-legacy).
// Condiviso da Arkanoid 2 (approx), Bubble Bobble e Goindol (audio FEDELE). Una
// sola copia in flash. fm.h dichiara le API, fm.c definisce il core (BUILD_YM2203=1).
#if defined(ENABLE_ARKANOID2) || defined(ENABLE_BOBLBOBL) || defined(ENABLE_GOINDOL)
#include "machines/arkanoid2/ym2203/fm.h"
#include "machines/arkanoid2/ym2203/fm.c"
#endif
#ifdef ENABLE_ARKANOID2
#include "machines/arkanoid2/arkanoid2.h"
#include "machines/arkanoid2/arkanoid2.cpp"
#endif
#ifdef ENABLE_GIGAS
#include "machines/gigas/gigas.h"
#include "machines/gigas/gigas.cpp"
#endif
#ifdef ENABLE_GIGAS2
#include "machines/gigas2/gigas2.h"
#include "machines/gigas2/gigas2.cpp"
#endif
#ifdef ENABLE_GOINDOL
#include "machines/goindol/goindol.h"
#include "machines/goindol/goindol.cpp"
#endif
#ifdef ENABLE_SBRKOUT
#include "machines/sbrkout/sbrkout.h"
#include "machines/sbrkout/sbrkout.cpp"
#endif
#ifdef ENABLE_SPACE
#include "machines/spaceinvaders/spaceinvaders.h"
#include "machines/spaceinvaders/spaceinvaders.cpp"
#endif
#ifdef ENABLE_GALAGA
#include "machines/galaga/galaga.h"
#include "machines/galaga/galaga.cpp"
#endif
#ifdef ENABLE_MOTORACE
#include "machines/motorace/motorace.h"
#include "machines/motorace/motorace.cpp"
#endif
#ifdef ENABLE_PHOENIX
#include "machines/phoenix/phoenix.h"
#include "machines/phoenix/phoenix.cpp"
#endif
#ifdef ENABLE_GALAXIAN
#include "machines/galaxian/galaxian.h"
#include "machines/galaxian/galaxian.cpp"
#endif
#ifdef ENABLE_BOMBBEE
#include "machines/bombbee/bombbee.h"
#include "machines/bombbee/bombbee.cpp"
#endif
#ifdef ENABLE_ROADFIGHTER
#include "machines/roadfighter/roadfighter.h"
#include "machines/roadfighter/roadfighter.cpp"
#endif
#ifdef ENABLE_PANG
// PANG header-only con asset PROGMEM (~6.4MB): inclusi SOLO qui (una TU).
// audio.cpp NON include pang.h -> usa il bridge libero pang_audio_fill (sotto).
#include "machines/pang/pang.h"
#include "machines/pang/pang.cpp"
#endif
// --- Giochi JOYSTICK (menu griglia GIOCHI ARCADE) ---
#ifdef ENABLE_XEVIOUS
#include "machines/xevious/xevious.h"
#include "machines/xevious/xevious.cpp"
#endif
#ifdef ENABLE_GYRUSS
#include "machines/gyruss/gyruss.h"
#include "machines/gyruss/gyruss.cpp"
#endif
#ifdef ENABLE_PACMAN
#include "machines/pacman/pacman.h"
#include "machines/pacman/pacman.cpp"
#endif
#ifdef ENABLE_MSPACMAN
#include "machines/mspacman/mspacman.h"
#include "machines/mspacman/mspacman.cpp"
#endif
#ifdef ENABLE_COMMANDO
#include "machines/commando/commando.h"
#include "machines/commando/commando.cpp"
#endif
#ifdef ENABLE_BRUBBER
#include "machines/brubber/brubber.h"
#include "machines/brubber/brubber.cpp"
#endif
// scramble e' anche la classe base di supercobra: compilare la base quando uno
// dei due e' attivo (come dkong per dkongjr).
#if defined(ENABLE_SCRAMBLE) || defined(ENABLE_SUPERCOBRA)
#include "machines/scramble/scramble.h"
#include "machines/scramble/scramble.cpp"
#endif
#ifdef ENABLE_SUPERCOBRA
#include "machines/supercobra/supercobra.h"
#include "machines/supercobra/supercobra.cpp"
#endif
#ifdef ENABLE_DKONG3
#include "machines/dkong3/dkong3.h"
#include "machines/dkong3/dkong3.cpp"
#endif
#ifdef ENABLE_POPEYE
#include "machines/popeye/popeye.h"
#include "machines/popeye/popeye.cpp"
#endif
#ifdef ENABLE_FANTASY
#include "machines/fantasy/fantasy.h"
#include "machines/fantasy/fantasy.cpp"
#endif
#ifdef ENABLE_NIBBLER
#include "machines/nibbler/nibbler.h"
#include "machines/nibbler/nibbler.cpp"
#endif
#ifdef ENABLE_MOONCRESTA
#include "machines/mooncresta/mooncresta.h"
#include "machines/mooncresta/mooncresta.cpp"
#endif
#ifdef ENABLE_TIMEPLT
#include "machines/timeplt/timeplt.h"
#include "machines/timeplt/timeplt.cpp"
#endif
#ifdef ENABLE_GNG
#include "machines/gng/gng.h"
#include "machines/gng/gng.cpp"
#endif
#ifdef ENABLE_BOBLBOBL
#include "machines/boblbobl/boblbobl.h"
#include "machines/boblbobl/boblbobl.cpp"
// NB: i core OPL (fmopl.c) + PSG (emu2149.c) sono compilati nella TU separata
// boblbobl_snd.cpp (root), per non collidere con fm.c (stesse macro Burczynski).
#endif
#ifdef ENABLE_1942
#include "machines/1942/1942.h"
#include "machines/1942/1942.cpp"
#endif
#ifdef ENABLE_PBACTION
#include "machines/pbaction/pbaction.h"
#include "machines/pbaction/pbaction.cpp"
#endif
#ifdef ENABLE_BOMBJACK
#include "machines/bombjack/bombjack.h"
#include "machines/bombjack/bombjack.cpp"
#endif
// --- Giochi JOYSTICK portati da FULL33 (griglia GIOCHI ARCADE) ---
#if defined(ENABLE_DKONGJR) || defined(ENABLE_DKONG)
// dkongjr eredita dalla classe base dkong: compilare ENTRAMBE.
#include "machines/dkong/dkong.h"
#include "machines/dkong/dkong.cpp"
#endif
#ifdef ENABLE_DKONGJR
#include "machines/dkongjr/dkongjr.h"
#include "machines/dkongjr/dkongjr.cpp"
#endif
#ifdef ENABLE_FROGGER
#include "machines/frogger/frogger.h"
#include "machines/frogger/frogger.cpp"
#endif
#ifdef ENABLE_GREENBERET
#include "machines/greenberet/greenberet.h"
#include "machines/greenberet/greenberet.cpp"
#endif
#ifdef ENABLE_LADYBUG
#include "machines/ladybug/ladybug.h"
#include "machines/ladybug/ladybug.cpp"
#endif
#ifdef ENABLE_MOONPATROL
#include "machines/moonpatrol/moonpatrol.h"
#include "machines/moonpatrol/moonpatrol.cpp"
#endif
#ifdef ENABLE_MARIOBROS
#include "machines/mariobros/mariobros.h"
#include "machines/mariobros/mariobros.cpp"
#endif
#ifdef ENABLE_SUPXEVS
// Vs. Super Xevious: Nintendo VS. UniSystem (NES da sala giochi), NON l'hardware
// Namco di Xevious. supxevs.cpp tira dentro anche nes_vs.cpp e le sue ROM: una
// sola unita' di compilazione, come per gli altri giochi in sottocartella.
#include "machines/supxevs/supxevs.h"
#include "machines/supxevs/supxevs.cpp"
#endif

#include <Preferences.h>
static Preferences volPrefs;

// Splash screen 240x320 portrait (PROGMEM, ~153 KB)
#include "splash_logo.h"

// ============================================================================
// Globali richiesti dal framework
// ============================================================================
unsigned short *frame_buffer  = nullptr;
sprite_S       *sprite_buffer = nullptr;
unsigned char  *memory        = nullptr;
machineBase    *currentMachine = nullptr;

Audio audio;
Video video;

// ⚠️ Accessor per il player laserdisc (laserdisc.cpp). DEVE stare qui, dopo
// `Audio audio;`, e il player NON deve includere emulation/audio.h: quell'header
// tira dentro <driver/i2s.h> (legacy) E le machine con le loro ROM (galaga.h ->
// galaga_rom1/2/3.h, dkong.h, mariobros.h). Includerlo in un secondo .cpp
// duplica quei dati e la scheda NON BOOTA PIU' (schermo bianco, nessun jingle).
// Costato una serata: il player legge il volume solo da qui.
int laserdisc_current_volume(void) { return audio.volumeSetting; }
Input input;
Menu  menu;
BootMenu bootMenu;
SettingsMenu settingsMenu;
TestMenu     testMenu;          // voce "TEST JOYSTICK E PULSANTI" del carosello
WifiFileMenu wifiFm;            // voce "FILE MANAGER WIFI" del carosello (wifi_fm.cpp)
GamesGridMenu gamesGrid;   // nuovo menu "GIOCHI ARCADE" (griglia joystick)
LaserdiscMenu laserdiscMenu; // nuovo menu "GIOCHI LASERDISC" (video da microSD)
NeoGeoMenu    neogeoMenu;    // menu "GIOCHI NEO GEO" (ROM e cover da microSD)

// Lista macchine (multi-machine via menu carousel)
#ifdef ENABLE_ARKANOID
Arkanoid arkanoid;
#endif
#ifdef ENABLE_TEMPEST
Tempest  tempest;
#endif
#ifdef ENABLE_ARKANOID2
Arkanoid2 arkanoid2;
#endif
#ifdef ENABLE_GIGAS
Gigas gigas;
#endif
#ifdef ENABLE_GIGAS2
Gigas2 gigas2;
#endif
#ifdef ENABLE_GOINDOL
Goindol goindol;
#endif
#ifdef ENABLE_SBRKOUT
Sbrkout sbrkout;
#endif
#ifdef ENABLE_SPACE
SpaceInvaders spaceinvaders;
#endif
#ifdef ENABLE_GALAGA
galaga galagaInst;
#endif
#ifdef ENABLE_MOTORACE
motorace motoraceInst;
#endif
#ifdef ENABLE_PHOENIX
Phoenix phoenix;
#endif
#ifdef ENABLE_GALAXIAN
galaxian galaxianInst;
#endif
#ifdef ENABLE_BOMBBEE
BombBee bombbee;
#endif
#ifdef ENABLE_ROADFIGHTER
Roadfighter roadfighter;
#endif
#ifdef ENABLE_PANG
pang pangInst;
// Bridge audio: audio.cpp (Audio::pang_render_buffer) chiama questa funzione,
// definita qui dove pang.h e' incluso (gli asset non vengono duplicati in audio.cpp).
void pang_audio_fill(int16_t *out, int frames) { pangInst.renderAudio(out, frames); }
#endif
#ifdef ENABLE_XEVIOUS
xevious xeviousInst;   // gioco joystick (griglia GIOCHI ARCADE, non nel carosello)
#endif
#ifdef ENABLE_GYRUSS
gyruss gyrussInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_PACMAN
pacman pacmanInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_1942
_1942 _1942Inst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_PBACTION
pbaction pbactionInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_BOMBJACK
bombjack bombjackInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_DKONGJR
dkongjr dkongjrInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_FROGGER
frogger froggerInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_GREENBERET
greenberet greenberetInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_LADYBUG
ladybug ladybugInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_MOONPATROL
moonpatrol moonpatrolInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_MARIOBROS
mariobros mariobrosInst;   // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_MSPACMAN
mspacman mspacmanInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_COMMANDO
commando commandoInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_DKONG
dkong dkongInst;           // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_BRUBBER
brubber brubberInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_SCRAMBLE
scramble scrambleInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_SUPERCOBRA
supercobra supercobraInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_DKONG3
dkong3 dkong3Inst;         // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_SUPXEVS
supxevs supxevsInst;       // gioco joystick (griglia GIOCHI ARCADE) — NES VS. UniSystem
#endif
#ifdef ENABLE_POPEYE
popeye popeyeInst;         // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_FANTASY
fantasy fantasyInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_NIBBLER
nibbler nibblerInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_MOONCRESTA
mooncresta mooncrestaInst; // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_TIMEPLT
timeplt timepltInst;       // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_GNG
gng gngInst;               // gioco joystick (griglia GIOCHI ARCADE)
#endif
#ifdef ENABLE_BOBLBOBL
boblbobl boblboblInst;     // gioco joystick (griglia GIOCHI ARCADE)
#endif
// Ordine di visualizzazione (boot menu + menu impostazioni spinner), richiesto
// utente 2026-06-06: Arkanoid, Arkanoid 2, Gigas, Gigas 2, Goindol, Super Breakout,
// Space Invaders, Galaga, MotoRace USA, Phoenix, Galaxian.
// (Goindol aggiunto 2026-06-29 dopo Gigas 2: famiglia SunA, dial/spinner.)
// Entrambi i menu (BootMenu/SettingsMenu in boot_menu.cpp) indicizzano questo
// array per posizione, quindi quest'ordine e' l'unica sorgente di verita'.
// (Tempest spostato il 08/10 tra Super Breakout e Space Invaders.)
machineBase *machines[] = {
#ifdef ENABLE_ARKANOID
  &arkanoid,
#endif
#ifdef ENABLE_ARKANOID2
  &arkanoid2,
#endif
#ifdef ENABLE_GIGAS
  &gigas,
#endif
#ifdef ENABLE_GIGAS2
  &gigas2,
#endif
#ifdef ENABLE_GOINDOL
  &goindol,
#endif
#ifdef ENABLE_SBRKOUT
  &sbrkout,
#endif
#ifdef ENABLE_TEMPEST
  &tempest,              // utente 08/10: tra Super Breakout e Space Invaders
#endif
#ifdef ENABLE_SPACE
  &spaceinvaders,
#endif
#ifdef ENABLE_GALAGA
  &galagaInst,
#endif
#ifdef ENABLE_MOTORACE
  &motoraceInst,
#endif
#ifdef ENABLE_PHOENIX
  &phoenix,
#endif
#ifdef ENABLE_GALAXIAN
  &galaxianInst,
#endif
#ifdef ENABLE_BOMBBEE
  &bombbee,
#endif
#ifdef ENABLE_ROADFIGHTER
  &roadfighter,
#endif
#ifdef ENABLE_PANG
  &pangInst,
#endif
};
const int machinesCount = sizeof(machines) / sizeof(machines[0]);

// Giochi JOYSTICK lanciati dalla griglia GIOCHI ARCADE (NON nel carosello paddle
// machines[]). L'ordine deve combaciare con GRID_ENTRIES[] in boot_menu.cpp:
//   indice 0 = Xevious, 1 = Vs. Super Xevious, 2 = Gyruss, ...
machineBase *joystick_machines[] = {
#ifdef ENABLE_XEVIOUS
  &xeviousInst,
#endif
#ifdef ENABLE_SUPXEVS
  &supxevsInst,          // subito dopo Xevious: nella griglia gli sta accanto
#endif
#ifdef ENABLE_GYRUSS
  &gyrussInst,
#endif
#ifdef ENABLE_PACMAN
  &pacmanInst,
#endif
#ifdef ENABLE_1942
  &_1942Inst,
#endif
#ifdef ENABLE_PBACTION
  &pbactionInst,
#endif
#ifdef ENABLE_BOMBJACK
  &bombjackInst,
#endif
#ifdef ENABLE_DKONGJR
  &dkongjrInst,
#endif
#ifdef ENABLE_FROGGER
  &froggerInst,
#endif
#ifdef ENABLE_GREENBERET
  &greenberetInst,
#endif
#ifdef ENABLE_LADYBUG
  &ladybugInst,
#endif
#ifdef ENABLE_MOONPATROL
  &moonpatrolInst,
#endif
#ifdef ENABLE_MARIOBROS
  &mariobrosInst,
#endif
#ifdef ENABLE_MSPACMAN
  &mspacmanInst,
#endif
#ifdef ENABLE_COMMANDO
  &commandoInst,
#endif
#ifdef ENABLE_DKONG
  &dkongInst,
#endif
#ifdef ENABLE_BRUBBER
  &brubberInst,
#endif
#ifdef ENABLE_SCRAMBLE
  &scrambleInst,
#endif
#ifdef ENABLE_POPEYE
  &popeyeInst,
#endif
#ifdef ENABLE_FANTASY
  &fantasyInst,
#endif
#ifdef ENABLE_NIBBLER
  &nibblerInst,
#endif
#ifdef ENABLE_MOONCRESTA
  &mooncrestaInst,
#endif
#ifdef ENABLE_TIMEPLT
  &timepltInst,
#endif
#ifdef ENABLE_GNG
  &gngInst,
#endif
#ifdef ENABLE_BOBLBOBL
  &boblboblInst,
#endif
#ifdef ENABLE_SUPERCOBRA
  &supercobraInst,
#endif
#ifdef ENABLE_DKONG3
  &dkong3Inst,
#endif
};
const int joystickCount = sizeof(joystick_machines) / sizeof(joystick_machines[0]);

static bool in_menu = true;     // true finche' il menu boot non chiude
static bool in_settings = false; // true mentre il menu IMPOSTAZIONI e' aperto
static bool in_test     = false; // true mentre la schermata TEST JOYSTICK e' aperta
static bool in_wifi     = false; // true mentre la schermata FILE MANAGER WIFI e' aperta
static bool in_grid = false;    // true mentre la griglia GIOCHI ARCADE e' aperta
static bool in_laserdisc = false; // true mentre il menu GIOCHI LASERDISC e' aperto
static bool in_neogeo    = false; // true mentre il menu GIOCHI NEO GEO e' aperto
// true se il gioco in corso e' stato avviato dalla griglia GIOCHI ARCADE
// GAMEPAD: l'hold START 3s deve tornare alla griglia, non al carosello.
static bool launched_from_grid = false;

// Indice di 1944 nella griglia GIOCHI ARCADE = ULTIMA cella. Deve combaciare con
// l'ultima voce di GRID_ENTRIES[] in boot_menu.cpp; sta in fondo apposta, perche'
// infilarlo in mezzo sposterebbe i tre elenchi paralleli (GRID_ENTRIES, il
// `switch` di grid_to_machine e joystick_machines[]).
#define GRID_IDX_1944  5
// Indice di Ghouls'n Ghosts = ULTIMA cella, subito dopo 1944. Stesso motivo:
// e' un player AUTONOMO, non un machineBase, e va IN FONDO. L'indice 28 e' stato
// VERIFICATO contando le voci di GRID_ENTRIES[] con uno script, non assunto.
#define GRID_IDX_GHOULS 25
// Indice di Out Run = ULTIMA cella della griglia. Stesso motivo di 1944 e
// Ghouls: e' un player AUTONOMO, non un machineBase, e va IN FONDO cosi' non
// slitta nessuno dei tre elenchi paralleli. L'indice 29 e' stato VERIFICATO
// contando le voci di GRID_ENTRIES[] con uno script, non assunto.
#define GRID_IDX_OUTRUN 29
// Indice di Gaplus = ULTIMA cella, subito dopo Out Run: player AUTONOMO,
// stesso motivo. Il 30 e' VERIFICATO contando le voci di GRID_ENTRIES[]
// con uno script (e grid_to_machine non ha un `case 30`).
#define GRID_IDX_GAPLUS 30

// Mappa l'indice della griglia GIOCHI ARCADE (0=Xevious, 1=Vs. Super Xevious,
// 2=Gyruss, ...) al gioco in joystick_machines[]. Ritorna -1 se il gioco non e'
// ancora portato (FIRE sulla cella non lancia nulla).
// ⚠️ L'ordine dei `case` DEVE seguire GRID_ENTRIES[] in boot_menu.cpp e quello
// di joystick_machines[] qui sopra: sono tre elenchi paralleli. Spostando una
// voce vanno spostati tutti e tre, `#ifdef` compreso — il `#ifdef` protegge la
// POSIZIONE, non il gioco che ci sta scritto accanto.
static int grid_to_machine(int grid_idx) {
  switch (grid_idx) {
#ifdef ENABLE_XEVIOUS
    case 0: return 0;   // Xevious -> joystick_machines[0]
#endif
#ifdef ENABLE_SUPXEVS
    case 1: return 1;   // Vs. Super Xevious -> joystick_machines[1]
#endif
#ifdef ENABLE_GYRUSS
    case 2: return 2;   // Gyruss -> joystick_machines[2]
#endif
#ifdef ENABLE_PACMAN
    case 3: return 3;   // Pac-Man -> joystick_machines[3]
#endif
#ifdef ENABLE_1942
    case 4: return 4;   // 1942 -> joystick_machines[4]
#endif
#ifdef ENABLE_PBACTION
    case 6: return 5;   // Pinball Action -> joystick_machines[5]
#endif
#ifdef ENABLE_BOMBJACK
    case 7: return 6;   // Bomb Jack -> joystick_machines[6]
#endif
#ifdef ENABLE_DKONGJR
    case 8: return 7;   // Donkey Kong Jr -> joystick_machines[7]
#endif
#ifdef ENABLE_FROGGER
    case 9: return 8;   // Frogger -> joystick_machines[8]
#endif
#ifdef ENABLE_GREENBERET
    case 10: return 9;   // Green Beret -> joystick_machines[9]
#endif
#ifdef ENABLE_LADYBUG
    case 11: return 10; // Lady Bug -> joystick_machines[10]
#endif
#ifdef ENABLE_MOONPATROL
    case 12: return 11; // Moon Patrol -> joystick_machines[11]
#endif
#ifdef ENABLE_MARIOBROS
    case 13: return 12; // Mario Bros -> joystick_machines[12]
#endif
#ifdef ENABLE_MSPACMAN
    case 14: return 13; // Ms. Pac-Man -> joystick_machines[13]
#endif
#ifdef ENABLE_COMMANDO
    case 15: return 14; // Commando -> joystick_machines[14]
#endif
#ifdef ENABLE_DKONG
    case 16: return 15; // Donkey Kong -> joystick_machines[15]
#endif
#ifdef ENABLE_BRUBBER
    case 17: return 16; // Burnin' Rubber -> joystick_machines[16]
#endif
#ifdef ENABLE_SCRAMBLE
    case 18: return 17; // Scramble -> joystick_machines[17]
#endif
#ifdef ENABLE_POPEYE
    case 19: return 18; // Popeye -> joystick_machines[18]
#endif
#ifdef ENABLE_FANTASY
    case 20: return 19; // Fantasy -> joystick_machines[19]
#endif
#ifdef ENABLE_NIBBLER
    case 21: return 20; // Nibbler -> joystick_machines[20]
#endif
#ifdef ENABLE_MOONCRESTA
    case 22: return 21; // Moon Cresta -> joystick_machines[21]
#endif
#ifdef ENABLE_TIMEPLT
    case 23: return 22; // Time Pilot -> joystick_machines[22]
#endif
#ifdef ENABLE_GNG
    case 24: return 23; // Ghosts'n Goblins -> joystick_machines[23]
#endif
#ifdef ENABLE_BOBLBOBL
    case 26: return 24; // Bubble Bobble -> joystick_machines[24]
#endif
#ifdef ENABLE_SUPERCOBRA
    case 27: return 25; // Super Cobra -> joystick_machines[25]
#endif
#ifdef ENABLE_DKONG3
    case 28: return 26; // Donkey Kong 3 -> joystick_machines[26]
#endif
    default: return -1;
  }
}

// Helper: salva la sensibilita' EC11 di tutti i giochi su NVS (per machineType).
static void save_ec11_sens() {
  Preferences sp;
  sp.begin("ec11sens", false);
  for (int i = 0; i < machinesCount; i++) {
    char k[8]; snprintf(k, sizeof(k), "t%d", (int)machines[i]->machineType());
    sp.putUChar(k, (uint8_t)machines[i]->ec11SensLevel);
  }
  sp.end();
}

// Strip buffer 320x8 condiviso tra game render e BootMenu.
// DEVE essere dichiarato prima di setup() per essere visibile in scope.
unsigned short out_strip[TFT_FB_W * 8];

// ---------------------------------------------------------------------------
// boot_msg(): scrive una riga a schermo DURANTE setup(). Serve perche' sul P4
// il Serial non c'e': senza, un fallimento nel boot (es. la malloc qui sotto
// che finisce in `while(1)`) da' solo SCHERMO BIANCO e nessun indizio.
// Richiede video.begin() gia' fatto.
// ---------------------------------------------------------------------------
static void boot_msg(const char *msg, unsigned short col = 0xFFFF) {
  // I checkpoint normali (BOOT 1..5, bianchi) sono NASCOSTI: passano invisibili.
  // Restano visibili SOLO i messaggi d'errore (col != bianco, es. ALLOC FAIL rosso),
  // che sul P4 senza Serial sono l'unico modo per sapere dove il boot si e' fermato.
  if (col == 0xFFFF) return;
  int n = (int)strlen(msg); if (n > TFT_FB_W / 8) n = TFT_FB_W / 8;
  int x = (TFT_FB_W - n * 8) / 2; if (x < 0) x = 0;
  for (int s = 0; s < TFT_FB_H / 8; s++) {
    memset(out_strip, 0, sizeof(out_strip));
    if (s == (TFT_FB_H / 8) / 2)
      for (int i = 0; i < n; i++) {
        int idx = char_to_idx(msg[i]);
        for (int r = 0; r < 8; r++) {
          unsigned char row = pgm_read_byte(&FONT8[idx][r]);
          for (int c = 0; c < 8; c++)
            if ((row & (0x80 >> c)) && x + i * 8 + c < TFT_FB_W)
              out_strip[r * TFT_FB_W + x + i * 8 + c] = col;
        }
      }
    video.write(out_strip, TFT_FB_W * 8);
  }
}

// Hash del contenuto di gioco dell'ultimo frame renderizzato (FNV-1a sui pixel
// del frame_buffer). Usato dall'attract per rilevare uno schermo CONGELATO
// (gioco impiantato, es. GIGAS bloccato nel self-test) e ruotare subito al
// gioco successivo invece di aspettare l'intero timeout di 5 minuti.
// Pubblicato da render_one_frame(), letto dal loop().
static volatile uint32_t g_render_hash = 0;

// Fingerprint PER-BANDA dell'ultimo frame: una sub-hash FNV per ogni striscia
// renderizzata (28 strisce nel path normale, 36 nel path portrait). Serve
// all'anti-freeze TOLLERANTE: pretendere l'hash GLOBALE identico era fragile,
// perche' un singolo pixel che lampeggia (es. blink/contatore nel self-test di
// Gigas) azzerava il timer ad ogni frame e il rilevatore non scattava MAI -> il
// gioco impiantato restava su per i 5 minuti pieni del game-timeout. Confrontando
// le bande contiamo QUANTE strisce cambiano: se ne cambiano poche (<= soglia) lo
// schermo e' praticamente fermo anche con un piccolo elemento animato.
#define ATTRACT_MAX_BANDS   40   // >= max righe renderizzate (36 portrait)
#define ATTRACT_STUCK_BANDS  3   // <= 3 bande cambiate = schermo "fermo"
static volatile uint32_t g_band_hash[ATTRACT_MAX_BANDS] = {0};
static volatile uint8_t  g_band_count = 0;

// ============================================================================
// Splash screen: blit immagine 240x320 portrait, attendi N ms o skip su FIRE.
// Presuppone che video.setOrientation(true) sia gia' stata chiamata.
// ============================================================================
static void show_splash(unsigned long duration_ms) {
  // Splash in LANDSCAPE 320x232 (pre-ruotata 90 anti-orario in Python).
  // SPLASH_W=320, SPLASH_H=232. Pipeline: 29 strip da 8 righe x 320 col.
  for (int s = 0; s < SPLASH_H / 8; s++) {
    memcpy(out_strip,
           splash_logo + s * 8 * SPLASH_W,
           SPLASH_W * 8 * sizeof(unsigned short));
    video.write(out_strip, SPLASH_W * 8);
  }

  // Mute dell'emulazione durante lo splash (il jingle ha priorita' sul mute):
  // dopo la melodia resta silenzio pulito, e il menu boot e' silenzioso.
  audio.setMute(true);
  // Jingle d'avvio (suonato dall'audio_task via transmit(), si auto-ferma).
  audio.startSplashMelody();

  // Attesa con skip su FIRE
  unsigned long t0 = millis();
  while (millis() - t0 < duration_ms) {
    if (input.buttons_get() & BUTTON_FIRE) break;
    delay(50);
  }

  audio.stopMelody();   // ferma il jingle se ancora in corso (skip FIRE)
}

// ============================================================================
// Audio task: gira sull'altro core e tiene il DMA I2S sempre pieno.
// IMPORTANTE: con buffer DMA = 1536 sample (48 ms) e frame 60 fps (16.67 ms),
// se transmit() viene chiamata solo a fine frame il DMA si svuota -> click
// periodici udibili come distorsione. Galagino chiama transmit() 6 volte per
// frame; qui usiamo un task dedicato che lo fa ogni 2 ms.
// ============================================================================
static void audio_task(void *param) {
  Audio *aud = (Audio*)param;
  for (;;) {
    aud->transmit();
    // pdMS_TO_TICKS arrotonda a 1 tick minimo (con tickrate 100 Hz default
    // 2/portTICK_PERIOD_MS = 0 → busy loop → starve IDLE → TWDT 5s freeze).
    // Phoenix col rendering audio leggero scatena il bug; altri giochi
    // mascheravano perche' transmit() durava di piu'.
    vTaskDelay(pdMS_TO_TICKS(2));
    if (pdMS_TO_TICKS(2) == 0) vTaskDelay(1);   // safety net se tickrate insolito
  }
}

// ============================================================================
// Callback Input -> Audio/Video
// ============================================================================
static void onVolumeUpDown(bool up, bool down)     { audio.volumeUpDown(up, down); }
static void onBrightnessUpDown(bool up, bool down) { /* opt: video.setBrightness */ }
static void onDoReset()         { if (currentMachine) currentMachine->reset(); }
static void onDoAttractReset()  { /* opt */ }

// ============================================================================
// setup / loop
// ============================================================================
void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println(F("\n[SPINNERINO SH] boot - Marco Prunca & Davide Gatti"));

  setCpuFrequencyMhz(360);   // ESP32-P4 default max

  // ⚠️ video.begin() PRIMA di tutto: cosi' un errore nel boot si VEDE.
  // Prima stava dopo le malloc/init e un fallimento qui dava schermo bianco
  // muto (il messaggio andava sul Serial, che sul P4 non esiste).
  video.begin();
  boot_msg("BOOT 1 VIDEO OK");

  // Framebuffer: 256 px wide × 8 row × 2 byte (1 strip per render)
  frame_buffer  = (unsigned short*)malloc(320 * 8 * 2);  // 320 max (Xevious usa 288)
  sprite_buffer = (sprite_S*)malloc(128 * sizeof(sprite_S));
  memory        = (unsigned char*)malloc(RAMSIZE);

  if (!frame_buffer || !sprite_buffer || !memory) {
    Serial.println(F("ALLOC FAIL"));
    // Dice QUALE e quanto heap interno resta: RAMSIZE puo' arrivare a 22 KB e
    // serve un blocco CONTIGUO (misurato: 47 KB liberi, ma frammentati).
    char m[40];
    snprintf(m, sizeof(m), "ALLOC FAIL %c%c%c FREE %uK MAX %uK",
             frame_buffer ? '-' : 'F', sprite_buffer ? '-' : 'S', memory ? '-' : 'M',
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned)(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL) / 1024));
    boot_msg(m, 0x00F8);
    while (1) delay(1000);
  }
  boot_msg("BOOT 2 MALLOC OK");

  currentMachine = machines[0];

  for (int i = 0; i < machinesCount; i++)
    machines[i]->init(&input, frame_buffer, sprite_buffer, memory);

  // Init anche dei giochi joystick della griglia GIOCHI ARCADE.
  for (int i = 0; i < joystickCount; i++)
    joystick_machines[i]->init(&input, frame_buffer, sprite_buffer, memory);

  // Sensibilita' EC11 per-gioco da NVS (chiave per machineType, default livello 5).
  {
    Preferences sp;
    sp.begin("ec11sens", true);
    for (int i = 0; i < machinesCount; i++) {
      char k[8]; snprintf(k, sizeof(k), "t%d", (int)machines[i]->machineType());
      uint8_t lv = sp.getUChar(k, 5);
      if (lv >= 1 && lv <= 10) machines[i]->ec11SensLevel = lv;
    }
    sp.end();
  }

  boot_msg("BOOT 3 MACHINES OK");
  audio.init();
  audio.start(currentMachine);
  boot_msg("BOOT 4 AUDIO OK");

  input.init();

  // Volume da NVS (con default)
  volPrefs.begin("arkanoid", true);
  uint8_t savedVol = volPrefs.getUChar("volume", 5);
  volPrefs.end();
  if (savedVol >= 1 && savedVol <= 30)
    audio.volumeSetting = savedVol;

  input.onVolumeUpDown(onVolumeUpDown);
  input.onBrightnessUpDown(onBrightnessUpDown);
  input.onDoReset(onDoReset);
  input.onDoAttractReset(onDoAttractReset);
  input.onVolumeCommit([]() {
    volPrefs.begin("arkanoid", false);
    volPrefs.putUChar("volume", (uint8_t)audio.volumeSetting);
    volPrefs.end();
    Serial.printf("[VOL] saved %d to NVS\n", audio.volumeSetting);
  });

  menu.init(&input, machines, machinesCount, frame_buffer);
  bootMenu.init(&input, machines, machinesCount, out_strip);

  // Audio task su CORE 0 (insieme ad Arduino main+video).
  // Phoenix e' polling-only: Z80 polla VBLANK su 0x7800 a 5.5MHz, audio sullo
  // stesso core dell'emulazione lo starva (game bloccato HUD INSERT COIN).
  // Separazione fisica: audio core 0, emulation core 1.
  // NOTA (fix bisect 2026-05-09): la slowness di Arkanoid 2/Galaga/Motorace
  // NON era causata dal pin audio_task su core 0, ma dalla saturazione SRAM
  // interna in Phoenix::init() (~268 KB allocate al boot per TUTTE le machine
  // → stack FreeRTOS in PSRAM → 5x lenti). Risolto in phoenix.cpp spostando
  // l'allocazione da init() a reset() (lazy, solo quando Phoenix selezionato).
  // Stack 8KB per YM2203 update_one().
  xTaskCreatePinnedToCore(audio_task, "aud", 8192, &audio, 5, NULL, 0);

  boot_msg("BOOT 5 TASK OK");
  Serial.println(F("[BOOT] video.begin done"));

  // Splash + menu restano in LANDSCAPE come il game (per ora).
  // L'immagine splash_logo.h e' stata pre-ruotata 90 anti-orario in Python
  // per apparire diritta sul display portrait fisico (vedi convert_splash.py).
  show_splash(4300);                  // splash, copre il jingle (~3.8s), FIRE per skip
  Serial.println(F("[BOOT] splash done -> boot menu"));
}

// ============================================================================
// TEST_PATTERN: bypassa Arkanoid e disegna un pattern noto per verificare
// display, orientamento, ordine RGB/BGR, mappatura pixel.
// Commenta la riga sotto per tornare al render normale.
// ============================================================================
//#define TEST_PATTERN

#ifdef TEST_PATTERN
// Colori RGB565 byte-swapped per ESP32 SPI BE
static const unsigned short C_BLACK   = 0x0000;
static const unsigned short C_WHITE   = 0xFFFF;
static const unsigned short C_RED     = 0x00F8;  // RGB565: F800 -> swap
static const unsigned short C_GREEN   = 0xE007;  // 07E0 -> swap
static const unsigned short C_BLUE    = 0x1F00;  // 001F -> swap
static const unsigned short C_YELLOW  = 0xE0FF;  // FFE0
static const unsigned short C_CYAN    = 0xFF07;  // 07FF
static const unsigned short C_MAGENTA = 0x1FF8;  // F81F

static void draw_test_pattern_strip(int strip_row) {
  // strip_row 0..27, copre 8 righe pixel (y = strip_row*8 .. strip_row*8+7)
  // Layout 256x224:
  //   row 0..1   (y 0-15)   : barre di colore (R,G,B,W,Y,C,M,K)
  //   row 2..3   (y 16-31)  : sfumatura R/G/B grayscale
  //   row 4..25  (y 32-207) : scacchiera 8x8 con cornice 1px bianca
  //   row 26..27 (y 208-223): scritta "ARKANOID TEST" semplice -> usiamo barre

  for (int y = 0; y < 8; y++) {
    unsigned short *line = frame_buffer + y * 256;
    int gy = strip_row * 8 + y;

    // Cornice bianca esterna 1px
    bool border = (gy == 0) || (gy == 223);

    if (strip_row < 2) {
      // Barre colore (8 barre x 32 px = 256)
      for (int x = 0; x < 256; x++) {
        unsigned short c;
        switch (x / 32) {
          case 0: c = C_RED; break;
          case 1: c = C_GREEN; break;
          case 2: c = C_BLUE; break;
          case 3: c = C_WHITE; break;
          case 4: c = C_YELLOW; break;
          case 5: c = C_CYAN; break;
          case 6: c = C_MAGENTA; break;
          default: c = C_BLACK; break;
        }
        if (border || x == 0 || x == 255) c = C_WHITE;
        line[x] = c;
      }
    } else if (strip_row < 4) {
      // Sfumatura grayscale (256 step)
      for (int x = 0; x < 256; x++) {
        uint8_t g = (uint8_t)x;
        uint16_t rgb = ((g & 0xF8) << 8) | ((g & 0xFC) << 3) | (g >> 3);
        unsigned short bs = (rgb >> 8) | (rgb << 8);
        if (border || x == 0 || x == 255) bs = C_WHITE;
        line[x] = bs;
      }
    } else {
      // Scacchiera 16x16 + cornice
      for (int x = 0; x < 256; x++) {
        bool side = (x == 0) || (x == 255);
        bool checker = ((x / 16) ^ (gy / 16)) & 1;
        line[x] = (border || side) ? C_WHITE : (checker ? C_BLACK : C_BLUE);
      }
    }
  }
}

static void render_one_frame() {
  for (int row = 0; row < 28; row++) {
    draw_test_pattern_strip(row);
    video.write(frame_buffer, 256 * 8);
  }
}
#else
// ============================================================================
// Render loop: window FULL 320x240 con padding nero, game 256x224 centrato.
// Indipendente da offset HW del pannello ST7789.
// ============================================================================
// (out_strip dichiarato sopra, vicino alle altre globali)

// Forward declarations per overlay volume (definizioni piu' avanti)
extern bool input_is_vol_mode_active(void);
static void draw_volume_overlay(unsigned short *strip_buf, int row, int vol_setting);
// Per il path PORTRAIT (giochi landscape: Popeye/Mario Bros/Moon Patrol/Green
// Beret) la barra NON viene disegnata sul gioco ma nel PADDING NERO in cima al
// pannello (via video.blitTopPadding), cosi' sta a filo del bordo come Gyruss e
// gli altri giochi del path normale. Questa funzione riempie un buffer 240x16.
static void fill_volume_bar_portrait(unsigned short *buf, int vol_setting);

// ============================================================================
// ⚡⚡ ARKANOID 2 — BLIT A FINESTRA STRETTA (2026-08-28). `ARK2_BLIT_STRETTO 0`
// la spegne, cosi' il referto puo' dire QUALE cura ha pagato.
//
// Il referto diceva: FPS 49 · CAR 121 · REN 6,7 · **BLT 13,4** · EMU 17,5.
// Due terzi del ramo video sono il BLIT, e non e' calcolo: e' BANDA. Il
// pannello e' SPI a 80 MHz e la finestra e' 320x232 = **148.480 byte per
// quadro = 14,85 ms di bus**, ma il quadro di gioco e' 256x224 = 114.688:
// i 64 pixel di padding NERO per riga (e le 8 righe in fondo) sono il
// **22,8% del traffico**, rispedito identico 49 volte al secondo.
//
// ⭐⭐ La cura NON e' inventata: e' lo schema che su questo stesso progetto
// usano gia' i player autonomi (1944, Ghouls, Out Run) — finestra SPI sulle
// sole colonne del gioco, e le bande scritte a parte solo quando cambiano.
// Lo dice il commento di `videoVolBarTop()` in emulation/video.h: «le bande
// nere non si rispediscono ad ogni quadro, vale il 30% del traffico»
// ([[feedback_porta_non_riprogettare]]).
//
// ⭐ E toglie anche una COPIA: nel percorso largo ogni pixel si copia due volte
// (frame_buffer -> out_strip con i due memset del padding, poi out_strip ->
// dma_buffer). Con la finestra stretta `frame_buffer` va dritto al blit.
//
// ⛔ RAGGIO D'AZIONE: **solo ARKANOID 2**. La guardia e' `ark2_stretto()`: per
// tutti gli altri giochi il percorso resta quello di prima, riga per riga.
// ============================================================================
#define ARK2_BLIT_STRETTO 1

#if ARK2_BLIT_STRETTO
static bool s_a2_finestra = false;   // la finestra SPI e' gia' quella stretta
static bool s_a2_pulito   = false;   // le bande sono gia' state annerite

static bool ark2_stretto(void) {
#ifdef ENABLE_ARKANOID2
  return currentMachine && currentMachine->machineType() == MCH_ARKANOID2;
#else
  return false;
#endif
}

// Con la finestra stretta le due bande e le 8 righe in fondo non si riscrivono
// piu': quello che c'e' resta li'. Percio' si annerisce TUTTO il pannello una
// volta sola, entrando.
static void ark2_pulisci_pannello(void) {
  video.waitDMA();
  video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
  memset(out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));
  for (int i = 0; i < TFT_FB_H / 8; i++) video.write(out_strip, TFT_FB_W * 8);
  video.waitDMA();
}

// ⚠️ La finestra si cambia solo a DMA ferma: una transazione in volo
// scriverebbe nel posto sbagliato.
static void ark2_finestra_gioco(int gx, int gw) {
  video.waitDMA();
  video.setAddrWindow(TFT_HW_X + gx, TFT_HW_Y, gw, 224);
  s_a2_finestra = true;
}

// ⛔⛔⛔ IL RIPRISTINO — e' la meta' che al primo giro avevo dimenticato di
// portare (2026-08-28, referto dell'utente: «uscendo col long press non torna al
// boot e perde il sincronismo»). Chi restringe la finestra DEVE anche ridarla
// larga: il menu di avvio scrive strisce da TFT_FB_W=320 e, dentro una finestra
// da 256, l'immagine si AVVOLGE — sembra che il gioco non sia uscito.
// ⭐ Non e' una regola nuova di questo progetto: i player autonomi lo fanno gia'
// (`c1944_window(0)` in cps1944.cpp, all'uscita). Avevo portato il restringimento
// e non il ripristino ([[feedback_porta_non_riprogettare]]: si trascrive il
// MECCANISMO intero, non il pezzo che serve a far vedere il guadagno).
// Idempotente: se la finestra non e' nostra non tocca niente.
static void ark2_finestra_ripristina(void) {
  if (!s_a2_finestra && !s_a2_pulito) return;
  video.waitDMA();
  video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
  s_a2_finestra = false;
  s_a2_pulito   = false;
}
#endif // ARK2_BLIT_STRETTO

// DEBUG_BORDER disabilitato (era confondente)
//#define DEBUG_BORDER

static inline void apply_border_to_strip(int strip_y_global) {
  for (int r = 0; r < 8; r++) {
    unsigned short *line = out_strip + r * TFT_FB_W;
    int gy = strip_y_global + r;
    if (gy < 4 || gy >= TFT_FB_H - 4) {
      for (int x = 0; x < TFT_FB_W; x++) line[x] = 0x1F00; // BLU
    } else {
      for (int x = 0; x < 4; x++) line[x] = 0x00F8;        // ROSSO sx
      for (int x = TFT_FB_W - 4; x < TFT_FB_W; x++) line[x] = 0xE007; // VERDE dx
    }
  }
}

static void render_one_frame() {
  // ⭐ Lo strumento si accende SOLO su ARKANOID 2: sugli altri 40 giochi non
  // cambia un pixel, e con ARK2_PROF a 0 queste righe non esistono affatto
  // ([[feedback_una_modifica_per_flash]]: il raggio d'azione conta piu' del
  // guadagno atteso).
#ifdef ENABLE_ARKANOID2
  prof_attiva(currentMachine->machineType() == MCH_ARKANOID2);
#else
  prof_attiva(false);
#endif

  // ===== Gioco LANDSCAPE (Green Beret) — pannello in PORTRAIT come FULL33 =====
  // Il pannello e' gia' stato commutato in portrait da start_machine
  // (video.setLandscapeGame(true)). Qui rendiamo ESATTAMENTE come FULL33:
  // 36 righe native da 240 px scritte dritte nella finestra 240x288. Niente
  // strisce trasposte, niente out_strip/padding. render_row (versione FULL33)
  // disegna le righe 4..31 (224 px gioco) centrate, le altre restano nere.
  if (currentMachine->usesPortraitPanel()) {
    currentMachine->prepare_frame();
    // videoFlipY() -> inverte l'ordine delle bande (flipRows, come FULL33
    // src.ino: row = 35-row). E' COSI' che FULL33 raddrizza i giochi landscape
    // ribaltati verticalmente (Mario Bros), senza toccare il MADCTL.
    bool flip_rows = currentMachine->videoFlipY();
    // Hash del contenuto come nel path normale: l'attract (rilevatore schermo
    // congelato nel loop) si arma al primo tasto e legge g_render_hash; se non lo
    // aggiornassimo, vedrebbe l'hash fermo e tornerebbe al menu dopo pochi secondi.
    uint32_t fhash = 2166136261u;              // FNV-1a offset basis
    for (int row = 0; row < 36; row++) {
      memset(frame_buffer, 0, 240 * 8 * sizeof(unsigned short));
      currentMachine->render_row(flip_rows ? (35 - row) : row);
      const unsigned short *gp = frame_buffer;
      int n = 240 * 8;
      uint32_t bh = 2166136261u;
      for (int i = 0; i < n; i += 3)
        bh = (bh ^ gp[i]) * 16777619u;         // FNV-1a per-banda
      g_band_hash[row] = bh;                    // fingerprint per-striscia (anti-freeze)
      fhash = (fhash ^ bh) * 16777619u;         // hash globale = combinazione bande
      video.write(frame_buffer, 240 * 8);
    }
    g_band_count  = 36;
    g_render_hash = fhash;

    // Barra volume (vol mode da COIN, gamepad): disegnata nel PADDING NERO in
    // cima al pannello, SOPRA la finestra di gioco, cosi' e' alla stessa altezza
    // assoluta della barra dei giochi normali (Gyruss). Quando il vol mode si
    // chiude, ripuliamo il padding una volta sola (altrimenti la barra resta
    // disegnata finche' non si esce dal gioco).
    {
      static unsigned short s_volbar[240 * 16];
      static bool s_volbar_shown = false;
      if (input_is_vol_mode_active()) {
        fill_volume_bar_portrait(s_volbar, audio.volumeSetting);
        video.blitTopPadding(s_volbar, 16);
        s_volbar_shown = true;
      } else if (s_volbar_shown) {
        memset(s_volbar, 0, sizeof(s_volbar));   // padding nero -> cancella barra
        video.blitTopPadding(s_volbar, 16);
        s_volbar_shown = false;
      }
    }

    emulation_notifyGive();
    return;
  }

  prof_ren_inizio();
  currentMachine->prepare_frame();
  prof_ren_fine();

#if ARK2_BLIT_STRETTO
  const bool stretto = ark2_stretto();
  if (!stretto) ark2_finestra_ripristina();
#else
  const bool stretto = false;
#endif

  // Riga di padding nero (8 righe) per top/bottom
  static bool padding_inited = false;
  if (!padding_inited) {
    memset(out_strip, 0, sizeof(out_strip));
    padding_inited = true;
  }

  // 1) Top padding: scritto in chunks di max 8 righe (out_strip e' 8 righe)
  if (TFT_Y_OFFSET > 0 && !stretto) {
    memset(out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));
    int rem = TFT_Y_OFFSET;
    while (rem > 0) {
      int chunk = (rem > 8) ? 8 : rem;
      prof_blt_inizio();
      video.write(out_strip, TFT_FB_W * chunk);
      prof_blt_fine();
      rem -= chunk;
    }
  }

  // Larghezza area gioco (asse lungo): 256 default, 288 per Xevious. Centrata in
  // TFT_FB_W=320. render_row scrive con stride = gw.
  const int gw = currentMachine->videoGameWidth();
  const int gx = (TFT_FB_W - gw) / 2;        // offset centratura (32 a 256, 16 a 288)

#if ARK2_BLIT_STRETTO
  if (stretto) {
    if (!s_a2_pulito) { ark2_pulisci_pannello(); s_a2_pulito = true; s_a2_finestra = false; }
    if (!s_a2_finestra) ark2_finestra_gioco(gx, gw);
  }
#endif

  // 2) Game: 28 strip di 8 righe ciascuna, ognuna con padding laterale
  uint32_t fhash = 2166136261u;              // FNV-1a offset basis
  for (int row = 0; row < 28; row++) {
    // ⭐ REN = disegno + impronta + composizione della striscia; BLT = la sola
    // scrittura SPI. Il conto DEVE chiudere: REN + BLT = CAR x 16,667 ms. Se
    // non torna, il numero che si sta guardando puo' essere lo STRUMENTO
    // ([[feedback_ottimizzare_contando_accessi]] caso 7).
    prof_ren_inizio();
    if (!currentMachine->hasOpaqueBG())
      memset(frame_buffer, 0, gw * 8 * 2);
    currentMachine->render_row(row);

    // Accumula l'hash del contenuto di gioco (solo pixel del frame_buffer, non
    // il padding/overlay): serve all'attract per rilevare uno schermo fermo.
    // Campionamento ogni 3 px = robusto e a costo trascurabile sul P4.
    // Sub-hash PER-BANDA (g_band_hash[row]) per l'anti-freeze tollerante.
    {
      const unsigned short *gp = frame_buffer;
      int n = gw * 8;
      uint32_t bh = 2166136261u;
      for (int i = 0; i < n; i += 3)
        bh = (bh ^ gp[i]) * 16777619u;        // FNV-1a per-banda
      g_band_hash[row] = bh;
      fhash = (fhash ^ bh) * 16777619u;        // hash globale = combinazione bande
    }

    // ⭐ Percorso STRETTO: `frame_buffer` va dritto al blit — niente
    // composizione, niente memset del padding, niente seconda copia.
    if (stretto) {
      prof_ren_fine();
      prof_blt_inizio();
      video.write(frame_buffer, gw * 8);
      prof_blt_fine();
      continue;
    }

    // Compone strip 320x8: padding sx + game gw + padding dx
    for (int r = 0; r < 8; r++) {
      unsigned short *dst = out_strip + r * TFT_FB_W;
      // padding sinistro
      memset(dst, 0, gx * sizeof(unsigned short));
      // game (gw px)
      memcpy(dst + gx,
             frame_buffer + r * gw,
             gw * sizeof(unsigned short));
      // padding destro
      int right_pad = TFT_FB_W - gx - gw;
      if (right_pad > 0)
        memset(dst + gx + gw, 0, right_pad * sizeof(unsigned short));
    }
#ifdef DEBUG_BORDER
    apply_border_to_strip(TFT_Y_OFFSET + row * 8);
#endif
    // ⭐ La banda della misura, nei 32 px neri in cima al pannello. Va PRIMA
    // della barra del volume, cosi' quando si regola il volume la barra vince:
    // e' transitoria e in quel momento e' lei che l'utente sta guardando.
    prof_banda(out_strip, row);

    // Overlay barra volume non bloccante (sopra il game in user portrait alto)
    if (input_is_vol_mode_active()) {
      draw_volume_overlay(out_strip, row, audio.volumeSetting);
    }
    prof_ren_fine();
    prof_blt_inizio();
    video.write(out_strip, TFT_FB_W * 8);
    prof_blt_fine();
  }

  // 3) Bottom padding (chunks di max 8 righe)
  int bottom_pad = stretto ? 0 : (TFT_FB_H - TFT_Y_OFFSET - 224);
  if (bottom_pad > 0) {
    memset(out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));
    int rem = bottom_pad;
    while (rem > 0) {
      int chunk = (rem > 8) ? 8 : rem;
      prof_blt_inizio();
      video.write(out_strip, TFT_FB_W * chunk);
      prof_blt_fine();
      rem -= chunk;
    }
  }

#if ARK2_BLIT_STRETTO
  // ⭐ Barra del volume e banda della misura sono FUORI dalla finestra stretta:
  // si scrivono a parte, e SOLO quando cambiano — rifarle ad ogni quadro
  // sarebbero 7,7 KB di SPI buttati e due cambi di finestra
  // ([[feedback_barra_volume_player_autonomi]]).
  if (stretto) {
    static int s_vol_ultimo = -2;
    const bool vol_on = input_is_vol_mode_active();
    const int  vol_ora = vol_on ? audio.volumeSetting : -1;
    if (vol_ora != s_vol_ultimo) {
      s_vol_ultimo = vol_ora;
      videoVolBarTop(out_strip, vol_on, vol_on ? audio.volumeSetting : 30);
      s_a2_finestra = false;              // videoVolBarTop lascia la finestra sua
    }
    if (prof_banda_blit(out_strip)) s_a2_finestra = false;
  }
#endif

  // Pubblica l'hash del frame appena renderizzato (letto dall'attract nel loop).
  g_band_count  = 28;
  g_render_hash = fhash;

  // audio.transmit() ora gestita da audio_task() su core dedicato (vedi setup)
  emulation_notifyGive();
}
#endif // TEST_PATTERN

// Debug Serial ogni 60 frame (~1s)
extern volatile int16_t ec11_paddle_x;
class ArkanoidDebug : public Arkanoid {
public:
  Z80 *get_cpu() { return &cpu[0]; }
};
static void debug_tick() {
  static uint32_t fc = 0;
  static int last_start = -1, last_fire = -1, last_a = -1, last_b = -1;
  fc++;

  int ps = digitalRead(BTN_START);
  int ec_sw = digitalRead(EC11_PIN_SW);
  int ec_a = digitalRead(EC11_PIN_A);
  int ec_b = digitalRead(EC11_PIN_B);

  // Edge detection: stampa subito ogni cambiamento
  if (ps != last_start) {
    Serial.printf("[%lu] *** START %d -> %d\n", fc, last_start, ps);
    last_start = ps;
  }
  if (ec_sw != last_fire) {
    Serial.printf("[%lu] *** FIRE %d -> %d\n", fc, last_fire, ec_sw);
    last_fire = ec_sw;
  }
  if (ec_a != last_a) {
    Serial.printf("[%lu] *** EC_A %d -> %d (paddle=%d)\n", fc, last_a, ec_a, (int)ec11_paddle_x);
    last_a = ec_a;
  }
  if (ec_b != last_b) {
    Serial.printf("[%lu] *** EC_B %d -> %d (paddle=%d)\n", fc, last_b, ec_b, (int)ec11_paddle_x);
    last_b = ec_b;
  }

  // Heartbeat ogni 3 secondi
  if ((fc % 180) == 0) {
    int vnz = 0;
    for (int i = 0; i < 0x800; i++) if (memory[0x1000 + i]) vnz++;
    Z80 *cpu = ((ArkanoidDebug*)&arkanoid)->get_cpu();
    Serial.printf("[%lu] PC=%04X paddle=%d vram=%d  pins ST=%d FIRE=%d A%d B%d\n",
                  fc, cpu->PC.W, (int)ec11_paddle_x, vnz, ps, ec_sw, ec_a, ec_b);
  }
}

// =====================================================================
// TEST_AUDIO: bypassa l'emulazione e genera una sinusoide pura sul DAC.
// Decommenta per diagnosticare l'hardware audio (ES8311 + NS4150B + speaker):
//   - tono pulito a 440 Hz -> HW OK, problema e' nell'emulazione AY
//   - rumore/distorsione    -> HW (SD pin, gain, alimentazione, GND)
// Commenta per tornare al gioco normale.
// =====================================================================
//#define TEST_AUDIO

// Forward declarations dichiarate in input.cpp
extern bool input_consume_back_to_menu();
extern void input_reset_button_state();
extern bool input_is_vol_mode_active();
extern void input_set_machine_in_corso(bool on);   // guardia disableVolMode() solo in gioco

// ============================================================================
// Overlay barra volume (non bloccante): chiamata per ogni strip durante il
// render del game. Disegna nei pixel landscape che corrispondono alla parte
// alta del display in user portrait (= prime 16 colonne x_l del frame_buffer
// landscape, dentro il padding TFT_X_OFFSET=32 che e' normalmente nero).
//
// User view portrait (barra centrata orizzontalmente, lunga 112 px):
//   user_y_p ∈ [0..15]    -> barra (alta 16 px)
//   user_x_p ∈ [64..175]  -> lunghezza barra (112 px utili centrati in 240)
//
// Mapping a coords landscape (boot_menu): x_l = user_y_p, y_l = 239 - user_x_p
//   x_l ∈ [0..15]    (16 colonne landscape, dentro padding sx)
//   y_l ∈ [64..175]  (112 righe centrate, fuori range = padding nero)
//
// Volume range [1..30]: 1 = max, 30 = min. Inverso visivo:
//   fill_px = 112 * (31 - vol) / 30   ∈ [3..112]
// ============================================================================
static void draw_volume_overlay(unsigned short *strip_buf, int row, int vol_setting) {
  const int BAR_LEN  = 112;                       // metà di 224 (=larghezza precedente)
  const int BAR_Y_LO = (224 - BAR_LEN) / 2;       // 56  -> centratura (224-112)/2
  const int BAR_Y_HI = BAR_Y_LO + BAR_LEN - 1;    // 167

  int fill_px = (BAR_LEN * (31 - vol_setting)) / 30;
  if (fill_px < 0)        fill_px = 0;
  if (fill_px > BAR_LEN)  fill_px = BAR_LEN;

  for (int r = 0; r < 8; r++) {
    int y_l = row * 8 + r;
    if (y_l >= 224) break;

    // Fuori range barra: lascia il pixel nero (padding originale del game)
    if (y_l < BAR_Y_LO || y_l > BAR_Y_HI) continue;

    // Fill verde da sinistra-user (alto y_l) verso destra-user (basso y_l)
    bool fill = (y_l > BAR_Y_HI - fill_px);
    // Bordi sinistro/destro user = estremi della barra
    bool border_lr = (y_l == BAR_Y_LO) || (y_l == BAR_Y_HI);

    // Soglia "rossa" vicino al massimo: ultimi ~10 px del fill (verso il max
    // utente = bassi y_l = vicini a BAR_Y_LO) sono rossi, gli altri verdi.
    const int RED_ZONE = 10;
    bool fill_red = fill && (y_l <= BAR_Y_LO + RED_ZONE - 1);

    unsigned short *line = strip_buf + r * TFT_FB_W;

    for (int x_l = 0; x_l < 16; x_l++) {
      // Bordi top/bottom user = x_l 0 e 15
      bool border_tb = (x_l == 0) || (x_l == 15);

      unsigned short color;
      if (border_tb || border_lr) color = 0xFFFF;        // bianco bordo
      else if (fill_red)          color = 0x00F8;        // rosso (RGB565 byte-swap di 0xF800)
      else if (fill)              color = 0xE007;        // verde fill (RGB565 byte-swap)
      else                        color = 0x0000;        // nero background interno

      line[x_l] = color;
    }
  }
}

// ============================================================================
// Riempie un buffer 240x16 con la barra volume per i giochi landscape (path
// portrait: Popeye, Mario Bros, Moon Patrol, Green Beret). Il buffer viene poi
// disegnato nel PADDING NERO in cima al pannello via video.blitTopPadding(),
// cosi' la barra e' a filo del bordo e alla STESSA altezza della barra dei
// giochi del path normale (Gyruss). Geometria identica a draw_volume_overlay:
// lunghezza 112 px, centrata, riempimento sinistra->destra, ultimi 10 px rossi.
//
// Volume range [1..30]: 1 = max, 30 = min. fill = BAR_W * (31 - vol) / 30.
// ============================================================================
static void fill_volume_bar_portrait(unsigned short *buf, int vol_setting) {
  const int W = 240, H = 16;             // dimensioni buffer = padding superiore
  const int BAR_W = 112;                 // larghezza barra (= BAR_LEN path normale)
  const int BAR_X = (W - BAR_W) / 2;     // 64 -> centrata in 240
  const int RED_ZONE = 10;               // px finali rossi (vicino al max)

  int fill = (BAR_W * (31 - vol_setting)) / 30;
  if (fill < 0)      fill = 0;
  if (fill > BAR_W)  fill = BAR_W;

  for (int y = 0; y < H; y++) {
    unsigned short *line = buf + y * W;
    bool border_v = (y == 0) || (y == H - 1);
    for (int x = 0; x < W; x++) {
      unsigned short c = 0x0000;          // sfondo nero del padding
      if (x >= BAR_X && x < BAR_X + BAR_W) {
        int bx = x - BAR_X;
        bool border_h = (bx == 0) || (bx == BAR_W - 1);
        bool f        = (bx < fill);
        bool f_red    = f && (bx >= BAR_W - RED_ZONE);
        if (border_v || border_h) c = 0xFFFF;   // bianco bordo
        else if (f_red)           c = 0x00F8;   // rosso (byte-swap di 0xF800)
        else if (f)               c = 0xE007;   // verde fill (byte-swap)
        else                      c = 0x0000;   // nero interno
      }
      line[x] = c;
    }
  }
}

// ============================================================================
// Termina il gioco corrente e torna al menu boot (hold START 4s).
// ============================================================================
// Attract mode: tracker timeout in game, indice prossimo gioco per rotazione.
static unsigned long game_attract_t0 = 0;     // 0 = disarmato
static int           attract_next_idx = 0;    // gioco proposto al prossimo return menu

// Auto-skip schermo CONGELATO durante l'attract: se l'immagine renderizzata non
// cambia da ATTRACT_FROZEN_TIMEOUT_MS (gioco impiantato, es. GIGAS fermo nel
// self-test), l'attract ruota subito al gioco successivo invece di mostrare un
// fotogramma morto per l'intero timeout di 5 min. Generico: vale per qualsiasi
// gioco del carosello, senza escluderne nessuno a mano. NON tocca i giochi della
// griglia GIOCHI ARCADE (gamepad), che hanno l'attract disarmato (t0=0).
#define ATTRACT_FROZEN_TIMEOUT_MS  20000      // 20s di schermo fermo -> ruota
static uint32_t      attract_last_hash    = 0;  // hash dell'ultimo frame osservato (legacy/log)
static unsigned long attract_last_change  = 0;  // millis() dell'ultimo cambiamento
static bool          attract_sig_resample = true; // forza re-snapshot del fingerprint al 1o frame del gioco

static void return_to_menu(int next_idx = -1, bool to_grid = false) {
  Serial.println(to_grid ? F("[BOOT] back to grid") : F("[BOOT] back to menu"));
  audio.setMute(true);               // mute prima di stop per evitare click/loop
  // Se stavamo giocando un gioco LANDSCAPE (Green Beret), ripristina il pannello
  // al landscape trasposto normale PRIMA di rendere il menu (che usa strip 320).
  if (currentMachine && currentMachine->usesPortraitPanel())
    video.setLandscapeGame(false);
  // Azzera il mirror orizzontale: i giochi con videoFlipX()=1 (es. Lady Bug)
  // impostano video.mirror_x=true in start_machine; senza reset il menu boot lo
  // eredita e appare specchiato/ruotato. Il prossimo gioco riassegna il suo mirror.
  video.mirror_x = false;
#if ARK2_BLIT_STRETTO
  // ⛔ PRIMA di disegnare il menu: se veniamo da ARKANOID 2 la finestra SPI e'
  // STRETTA (256x224) e il menu scrive strisce da 320. Vedi il commento su
  // `ark2_finestra_ripristina()`.
  ark2_finestra_ripristina();
#endif
  emulation_stop();                  // ferma il task emulation, resetta machine
  audio.start(currentMachine);       // ri-azzera stati audio residui (es. Galaxian VCO sweep)
  input_reset_button_state();        // pulisce sequenza COIN/START e hold tracker
  // currentMachine resta puntata all'ultimo gioco: la sua disableVolMode() non
  // deve valere nei menu ne' nei player LASER DISC (vedi input.cpp).
  input_set_machine_in_corso(false);

#if LIBERA_BUFFER_ALL_USCITA
  // ⭐⭐⭐ QUI LA RAM INTERNA TORNA INDIETRO — PER TUTTI I GIOCHI, NON PER UNO.
  //
  // Referto: *«ogni volta che si apre un gioco e si chiude e si passa al
  // successivo si accumula buffer»*. Prima di questa riga c'erano DUE casi
  // speciali scritti a mano — PANG in `start_machine()` col commento «l'unico
  // allocatore enorme», e ARKANOID 2 aggiunto qui il giorno prima — e ogni
  // gioco nuovo che allocava avrebbe voluto il terzo, il quarto, il quinto.
  // Il conto vero e' in `config.h`: gli allocatori enormi erano SETTE, e il
  // piu' grosso (PHOENIX, 264 KB) non era nella lista. Una lista di eccezioni
  // che si allunga di uno per volta E' il difetto, non la cura
  // ([[feedback_costante_del_primo_caso]]).
  //
  // ⭐ `freeBuffers()` e' virtuale in `machineBase`: chi non ha niente da
  // restituire eredita il no-op, e un gioco portato domani entra in questa
  // riga senza che nessuno la debba aggiornare.
  //
  // ⚠️ Il POSTO e' `return_to_menu()`, non `start_machine()`: quest'ultima e'
  // l'uscita verso un altro GIOCO, ma i player laserdisc non ci passano
  // affatto (`spaceace_play()` e' chiamata dritta dal menu LASER DISC), ed era
  // esattamente la sequenza del referto — ARKANOID 2 -> SPACE ACE.
  // `return_to_menu()` invece e' l'uscita di TUTTI
  // ([[feedback_init_una_volta_sola_vs_rientro]], «chi la restringe, chi la
  // riallarga?»: il ripristino va su OGNI percorso di uscita).
  // ⚠️ E deve stare DOPO `emulation_stop()`, che chiama `currentMachine->reset()`
  // e ri-allocherebbe tutto quello che avessimo appena liberato; l'audio e' gia'
  // in mute dalla prima riga di questa funzione, quindi il task audio non tocca
  // piu' i buffer della machine (`audio.cpp`: `if (muted) { memset; continue; }`).
  if (currentMachine) currentMachine->freeBuffers();
#endif
  game_attract_t0 = 0;               // disarma attract finche' non riparte un gioco
  if (to_grid) {
    // Ritorno alla griglia GIOCHI ARCADE GAMEPAD (gioco avviato da li').
    gamesGrid.init(&input, out_strip);
    in_grid = true;
    in_menu = false;
    return;
  }
  int start = (next_idx >= 0) ? next_idx : 0;
  bootMenu.init(&input, machines, machinesCount, out_strip, start);
  in_menu = true;
}

// ============================================================================
// Esce dal menu di selezione e avvia il gioco scelto: setup macchina + audio
// + flip video + task emulation.
// ============================================================================
extern void ec11_apply_sensitivity(int level);   // input.cpp

// Avvio generico di una machine (paddle o joystick): setup + audio + flip video
// + task emulation. Non tocca l'attract (lo gestiscono i wrapper sotto).
static void start_machine(machineBase *m) {
#if LIBERA_BUFFER_ALL_USCITA
  // Rete di sicurezza: la liberazione vera avviene in `return_to_menu()`, che e'
  // l'uscita di TUTTI i giochi e passa sempre di qui prima di riavviarne un
  // altro. Questa riga serve solo se un domani qualcuno avviasse una machine
  // senza passare dal menu: e' idempotente (i puntatori sono gia' nullptr, ogni
  // `freeBuffers()` e' scritto `if (ptr) { free; ptr = nullptr; }`) e non tocca
  // la machine che stiamo per avviare. Prima qui c'erano i due casi speciali
  // scritti a mano, PANG e ARKANOID 2 — vedi `config.h` per il perche' non
  // bastavano.
  if (currentMachine && currentMachine != m) currentMachine->freeBuffers();
#endif
  currentMachine = m;
  input_set_machine_in_corso(true);  // da qui vale la sua disableVolMode()
  currentMachine->reset();
  // Applica la sensibilita' EC11 del gioco (step encoder paddle/dial).
  // L'hold direzionale e' gia' scalato in input.cpp via ec11HoldMsEff().
  ec11_apply_sensitivity(currentMachine->ec11SensLevel);
  audio.setMute(false);              // riattiva audio quando avvia il gioco
  audio.start(currentMachine);

  // Mirror orizzontale a livello buffer (video.mirror_x): meccanismo PULITO che
  // NON tocca MADCTL/finestra. Il vecchio flipHorizontal/flipVertical assumeva il
  // pannello CYD 240x240 e cambiava MADCTL+finestra a 224 -> ROMPE il rendering a
  // strip 320 del P4 (era innescato da Lady Bug, videoFlipX=1). Soluzione Davide.
  // Reset+set a OGNI avvio: evita di ereditare il mirror dal gioco precedente
  // (causa dei giochi "specchiati" dopo aver giocato Lady Bug).
  video.mirror_x = currentMachine->videoFlipX() ? true : false;

  // Giochi LANDSCAPE (Green Beret): commuta il pannello in PORTRAIT come FULL33
  // (finestra 240-wide), cosi' il gioco 240x224 entra a piena risoluzione.
  // I giochi normali restano nel landscape trasposto.
  if (currentMachine->usesPortraitPanel())
    video.setLandscapeGame(true);

  emulation_start();
}

static void start_selected_game(int idx) {
  launched_from_grid = false;        // avviato dal carosello: START 3s -> carosello
  start_machine(machines[idx]);
  Serial.printf("[BOOT] start machine %d (type=%d)\n", idx, currentMachine->machineType());

  // Arma timer attract game: dopo MASTER_ATTRACT_GAME_TIMEOUT torna al menu
  // e propone il gioco successivo (rotazione carosello).
  attract_next_idx = (idx + 1) % machinesCount;
  game_attract_t0 = millis();
  // Arma anche il rilevamento schermo-congelato: parte la finestra da capo.
  attract_last_hash    = 0;
  attract_last_change  = millis();
  attract_sig_resample = true;          // ri-snapshot del fingerprint sul nuovo gioco
}

// Avvio di un gioco joystick dalla griglia GIOCHI ARCADE. Attract disabilitato
// (game_attract_t0=0): si esce solo con START tenuto 3s (torna al carosello).
static void start_joystick_game(int ji) {
  launched_from_grid = true;         // avviato dalla griglia: START 3s -> griglia
  start_machine(joystick_machines[ji]);
  Serial.printf("[BOOT] start joystick %d (type=%d)\n", ji, currentMachine->machineType());
  game_attract_t0 = 0;
}

void loop() {
#ifdef TEST_AUDIO
  audio.testSine(440, 8000);
  return;
#endif

  // Aggiorna lo stato BT dal C6 (UART) una volta per giro di loop (~60 Hz):
  // legge i pacchetti gamepad e applica il movimento spinner da D-pad.
  // Va fatto su core 0 (qui), prima dei branch menu/impostazioni/gioco.
  input.updateBluetooth();

  if (in_settings) {
    settingsMenu.update();
    settingsMenu.render();
    if (settingsMenu.finished()) {
      save_ec11_sens();                 // persisti i livelli su NVS
      input_reset_button_state();       // pulisci sequenza START/COIN
      in_settings = false;
      in_menu     = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
    }
    delay(16);
    return;
  }

  // Schermata TEST JOYSTICK E PULSANTI: mostra e basta, non modifica nulla.
  // Si esce con START tenuto 3 s, come dalla griglia GIOCHI ARCADE.
  if (in_test) {
    testMenu.update();
    testMenu.render();
    if (input_consume_back_to_menu()) {
      in_test = false;
      in_menu = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
      input_reset_button_state();
    }
    delay(16);
    return;
  }

  // Schermata FILE MANAGER WIFI: il C6 accende la rete e gira al P4 le
  // richieste del browser, che qui si eseguono sulla SD. ⚠️ Niente delay(16):
  // update() tiene da sola il ritmo, una pausa per giro dimezzerebbe la
  // velocita' dei trasferimenti. Uscita con START 3 s; close() rende UART e
  // buffer, e il C6 spegne il Wi-Fi da solo pochi secondi dopo.
  if (in_wifi) {
    wifiFm.update();
    wifiFm.render();
    if (input_consume_back_to_menu()) {
      wifiFm.close();
      in_wifi = false;
      in_menu = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
      input_reset_button_state();
    }
    return;
  }

  if (in_grid) {
    gamesGrid.update();
    gamesGrid.render();
    // START tenuto 3s -> torna al carosello principale
    if (input_consume_back_to_menu()) {
      in_grid = false;
      in_menu = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
      input_reset_button_state();
      delay(16);
      return;
    }
    if (gamesGrid.finished()) {
      // ⚠️ 1944 NON e' un machineBase: e' un player AUTONOMO (task, video e
      // audio propri), come i giochi laserdisc. Va quindi intercettato PRIMA di
      // grid_to_machine(), che sa mappare solo i giochi galagino-style.
      // Il motivo e' architetturale: gli altri disegnano una riga alla volta su
      // richiesta (render_row), un CPS-2 compone tilemap e sprite in un
      // framebuffer. Forzarlo in render_row sarebbe una riscrittura.
      if (gamesGrid.selectedIndex() == GRID_IDX_1944) {
        gamesGrid.clearFinished();
        input_reset_button_state();
        cps1944_play(&input, out_strip);      // bloccante: torna con START 3s
        gamesGrid.init(&input, out_strip);    // rientro: ridisegna da zero
        input_reset_button_state();
        delay(16);
        return;
      }
      // Ghouls'n Ghosts (CPS-1): stesso caso di 1944, stessa forma.
      if (gamesGrid.selectedIndex() == GRID_IDX_GHOULS) {
        gamesGrid.clearFinished();
        input_reset_button_state();
        ghouls_play(&input, out_strip);       // bloccante: torna con START 3s
        gamesGrid.init(&input, out_strip);    // rientro: ridisegna da zero
        input_reset_button_state();
        delay(16);
        return;
      }
      // Out Run (Sega 1986, DUE 68000): stesso caso di 1944 e Ghouls.
      if (gamesGrid.selectedIndex() == GRID_IDX_OUTRUN) {
        gamesGrid.clearFinished();
        input_reset_button_state();
        outrun_play(&input, out_strip);       // bloccante: torna con START 3s
        gamesGrid.init(&input, out_strip);    // rientro: ridisegna da zero
        input_reset_button_state();
        delay(16);
        return;
      }
      // Gaplus (Namco 1984, tre 6809): stesso caso di Out Run, stessa forma.
      if (gamesGrid.selectedIndex() == GRID_IDX_GAPLUS) {
        gamesGrid.clearFinished();
        input_reset_button_state();
        gaplus_play(&input, out_strip);       // bloccante: torna con START 3s
        gamesGrid.init(&input, out_strip);    // rientro: ridisegna da zero
        input_reset_button_state();
        delay(16);
        return;
      }
      int mi = grid_to_machine(gamesGrid.selectedIndex());
      if (mi >= 0) {
        in_grid = false;
        start_joystick_game(mi);
      } else {
        // Gioco non ancora portato (Gyruss = fase 3): resta nella griglia.
        gamesGrid.clearFinished();
      }
    }
    delay(16);
    return;
  }

  if (in_neogeo) {
    neogeoMenu.update();
    neogeoMenu.render();
    // START tenuto 3s -> torna al carosello principale
    if (input_consume_back_to_menu()) {
      in_neogeo = false;
      in_menu   = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
      input_reset_button_state();
      delay(16);
      return;
    }
    if (neogeoMenu.finished()) {
      // Bloccante: torna qui solo quando l'utente tiene START 3s.
      // ⭐ Niente switch sull'indice: il menu dice il NOME DEL SET e il player si
      // cerca la sua riga. Cosi' aggiungere un gioco NEO GEO e' una voce nel
      // menu piu' i dati sulla SD, e non c'e' nessuna corrispondenza fra indici
      // da tenere allineata a mano (la griglia GIOCHI ARCADE, che invece ha tre
      // liste parallele, quel prezzo lo paga a ogni inserimento).
      neogeoMenu.clearFinished();
      input_reset_button_state();
      neogeo_play(&input, out_strip, neogeoMenu.selectedSet());
      neogeoMenu.init(&input, out_strip);
      input_reset_button_state();
      delay(16);
      return;
    }
    delay(16);
    return;
  }

  if (in_laserdisc) {
    laserdiscMenu.update();
    laserdiscMenu.render();
    // START tenuto 3s -> torna al carosello principale
    if (input_consume_back_to_menu()) {
      in_laserdisc = false;
      in_menu      = true;
      bootMenu.init(&input, machines, machinesCount, out_strip);
      input_reset_button_state();
      delay(16);
      return;
    }
    if (laserdiscMenu.finished()) {
      // FASE 2 step 2a: player VIDEO-ONLY + benchmark fps (vedi laserdisc.h).
      // Bloccante: torna qui solo quando l'utente tiene START 3s.
      laserdiscMenu.clearFinished();
      input_reset_button_state();
      // Instrada al player giusto. Ogni gioco laserdisc ha il SUO motore, non
      // c'e' un player generico: DL e' guidato da uno script (DirkSimple), gli
      // altri due da un ROM emulato di CPU diversa.
      //   switch, non piu' if/else: con la terza voce il ramo "else" avrebbe
      //   mandato DL2 dentro Dragon's Lair.
      switch (laserdiscMenu.selectedIndex()) {
        case LD_GAME_ACE: spaceace_play(&input, out_strip);  break;  // Z80 + LD-V1000
        case LD_GAME_DL2: dl2_play(&input, out_strip);       break;  // 8088 + LDP-1450
        default:          laserdisc_play(&input, out_strip); break;  // DirkSimple
      }
      // rientro nel menu: ripristina stato input e ridisegna da zero
      laserdiscMenu.init(&input, out_strip);
      input_reset_button_state();
      delay(16);
      return;
    }
    delay(16);
    return;
  }

  if (in_menu) {
    bootMenu.update();
    bootMenu.render();
    if (bootMenu.finished()) {
      int sel = bootMenu.selectedIndex();
      if (sel == machinesCount + EXTRA_SETTINGS) {
        // Voce "IMPOSTAZIONI" selezionata → apri il menu sensibilita' EC11.
        in_menu     = false;
        in_settings = true;
        settingsMenu.init(&input, machines, machinesCount, out_strip);
        input_reset_button_state();
      } else if (sel == machinesCount + EXTRA_ARCADE) {
        // Voce "GIOCHI ARCADE" → apri la griglia joystick.
        in_menu = false;
        in_grid = true;
        gamesGrid.init(&input, out_strip);
        input_reset_button_state();
      } else if (sel == machinesCount + EXTRA_TEST) {
        // Voce "TEST JOYSTICK E PULSANTI" → schermata di prova degli ingressi.
        in_menu = false;
        in_test = true;
        testMenu.init(&input, out_strip);
        input_reset_button_state();
      } else if (sel == machinesCount + EXTRA_WIFI) {
        // Voce "FILE MANAGER WIFI" → rete SPINNERINO + file sulla SD dal browser.
        in_menu = false;
        in_wifi = true;
        wifiFm.init(&input, out_strip);
        input_reset_button_state();
      } else if (sel == machinesCount + EXTRA_NEOGEO) {
        // Voce "GIOCHI NEO GEO" → menu dei giochi NeoGeo (ROM e cover da SD).
        // init() monta la SD e legge la cover: come il menu laserdisc, puo'
        // metterci qualche decimo di secondo.
        in_menu   = false;
        in_neogeo = true;
        neogeoMenu.init(&input, out_strip);
        input_reset_button_state();
      } else if (sel == machinesCount + EXTRA_LASERDISC) {
        // Voce "GIOCHI LASERDISC" → apri il menu dei giochi da microSD.
        // init() monta la SD: puo' metterci qualche decimo di secondo.
        in_menu      = false;
        in_laserdisc = true;
        laserdiscMenu.init(&input, out_strip);
        input_reset_button_state();
      } else {
        in_menu = false;
        start_selected_game(sel);
      }
    }
    delay(16);   // ~60 fps cap del menu
    return;
  }

  // Hold START 3s -> torna al menu boot
  // CRITICO: chiama buttons_get() ad ogni loop per assicurare che la logica
  // di hold tracker giri anche se la machine corrente non legge IN0 spesso
  // (es. Gigas in test mode legge IN0 raramente).
  unsigned char in_btn = input.buttons_get();
  if (input_consume_back_to_menu()) {
    // Se il gioco e' partito dalla griglia GIOCHI ARCADE GAMEPAD, torna alla
    // griglia; altrimenti (carosello) torna al carosello principale.
    return_to_menu(-1, launched_from_grid);
    return;
  }

#ifdef MASTER_ATTRACT_GAME_TIMEOUT
  // Reset attract timer su qualsiasi input dell'utente (tasti o rotazione EC11);
  // se nessun input per MASTER_ATTRACT_GAME_TIMEOUT ms, torna a menu con
  // prossimo gioco in rotazione.
  {
    extern volatile uint8_t ec11_dial_counter;
    static uint8_t last_dial = 0;
    uint8_t cur_dial = ec11_dial_counter;
    if (in_btn || cur_dial != last_dial) game_attract_t0 = millis();
    last_dial = cur_dial;
  }
  if (game_attract_t0 && (millis() - game_attract_t0) > MASTER_ATTRACT_GAME_TIMEOUT) {
    Serial.printf("[ATTRACT] game timeout, rotate to machine %d\n", attract_next_idx);
    return_to_menu(attract_next_idx);
    return;
  }
#endif

  unsigned long t0 = micros();
  render_one_frame();
  uint32_t render_us = micros() - t0;
  // ⚠️ `render_us` e' il LAVORO, preso PRIMA dell'attesa di pacing qui sotto:
  // dopo, il totale sarebbe sempre 16,7 ms e non direbbe niente
  // ([[feedback_fps_a_passo_fisso_sono_ciechi]]).
  prof_quadro(render_us);
  debug_tick();

#ifdef MASTER_ATTRACT_GAME_TIMEOUT
  // Auto-skip schermo CONGELATO: solo quando l'attract e' armato (carosello;
  // la griglia gamepad ha t0=0 ed e' esclusa). Se l'hash del frame renderizzato
  // resta identico per ATTRACT_FROZEN_TIMEOUT_MS, il gioco e' impiantato
  // (es. GIGAS nel self-test) -> ruota subito al gioco successivo.
  if (game_attract_t0) {
    // Anti-freeze TOLLERANTE: confronta il fingerprint PER-BANDA col frame
    // precedente e conta quante strisce sono cambiate. Se ne cambiano poche
    // (<= ATTRACT_STUCK_BANDS) lo schermo e' praticamente fermo, anche con un
    // piccolo elemento animato (es. blink/contatore del self-test di Gigas che
    // tocca 1-2 strisce) -> dopo ATTRACT_FROZEN_TIMEOUT_MS ruota al gioco dopo.
    // Un gioco vivo (attract/demo) cambia molte bande ogni frame e resetta il
    // timer, quindi non viene mai scambiato per impiantato. Sostituisce il
    // vecchio confronto sull'hash GLOBALE esatto, che un singolo pixel azzerava.
    static uint32_t prev_band[ATTRACT_MAX_BANDS];
    int bc = g_band_count;
    int changed;
    if (attract_sig_resample) {
      attract_sig_resample = false;
      changed = bc;                 // forza snapshot, niente confronto col gioco precedente
    } else {
      changed = 0;
      for (int i = 0; i < bc; i++)
        if (g_band_hash[i] != prev_band[i]) changed++;
    }
    if (changed > ATTRACT_STUCK_BANDS) {
      for (int i = 0; i < bc; i++) prev_band[i] = g_band_hash[i];
      attract_last_change = millis();
    } else if ((millis() - attract_last_change) > ATTRACT_FROZEN_TIMEOUT_MS) {
      Serial.printf("[ATTRACT] frozen screen (%d/%d bande mosse), skip to machine %d\n",
                    changed, bc, attract_next_idx);
      return_to_menu(attract_next_idx);
      return;
    }
  }
#endif

  // Diagnostica: media tempo render ogni 60 frame (1 sec) + max picco
  static uint32_t accum_render = 0, max_render = 0, frame_cnt = 0;
  accum_render += render_us;
  if (render_us > max_render) max_render = render_us;
  if (++frame_cnt >= 60) {
    Serial.printf("[R] render_us avg=%lu max=%lu (budget 16667)\n",
                  accum_render / 60, max_render);
    accum_render = 0; max_render = 0; frame_cnt = 0;
  }

  // Frame cap a ~60 fps (16 ms)
  unsigned long t1 = render_us / 1000;
  if (t1 < 16) delay(16 - t1);
}
