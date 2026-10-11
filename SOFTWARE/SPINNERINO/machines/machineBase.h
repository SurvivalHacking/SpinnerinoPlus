#ifndef MACHINEBASE_H
#define MACHINEBASE_H

#include "Arduino.h"
#include "../cpus/z80/Z80.h"
#include "../cpus/i8048/i8048.h"
#include "../emulation/input.h"

#ifdef LED_PIN
#include <FastLED.h>
#define NUM_LEDS  7

#define LED_BLACK    CRGB::Black
#define LED_RED      CRGB::Red
#define LED_GREEN    CRGB::Green
#define LED_BLUE     CRGB::Blue
#define LED_YELLOW   CRGB::Yellow
#define LED_MAGENTA  CRGB::Magenta
#define LED_CYAN     CRGB::Cyan
#define LED_WHITE    CRGB::White
#endif

#if defined(ENABLE_PANG)
  // Pang mappa palette+color+vram+obj+work in 0..0x57FF -> serve 0x5800 (22528).
  // E' il piu' grande: deve avere priorita' su tutti gli altri branch.
  #define RAMSIZE   (0x5800)
#elif defined(ENABLE_XEVIOUS) || defined(ENABLE_ARKANOID) || defined(ENABLE_GOINDOL) || defined(ENABLE_SUPXEVS)
  // SUPXEVS (NES VS.): il motore tiene RAM, nametable, OAM e RAM tamponata
  // dentro questo blocco, fino a NESOFF_END = 0x2100 (8448 byte) — sopra gli
  // 8192 del ramo di default, quindi va elencato qui.
  #define RAMSIZE   (16384)
#elif (defined(ENABLE_1942) || defined(ENABLE_MRDO) || defined(ENABLE_LADYBUG) || defined(ENABLE_GYRUSS) || defined(ENABLE_VENUS) || defined(ENABLE_TIMEPLT) || defined(ENABLE_PENGO) || defined(ENABLE_TUTANKHM) || defined(ENABLE_MOTORACE) || defined(ENABLE_GREENBERET) || defined(ENABLE_SCRAMBLE) || defined(ENABLE_GAPLUS) || defined(ENABLE_BOMBJACK))
  #define RAMSIZE   (8192 + 1024 + 128)
#else
  #define RAMSIZE   (8192)
#endif

struct sprite_S {
  unsigned short code;
  unsigned char color, flags;
  short x, y;

  //bombjack
  unsigned char color_block; // Già shiftato di 3 per essere usato come offset
  char is_32x32;
  char flip_x;
  char flip_y;
};

enum {
  MCH_MENU = 0,
  #ifdef ENABLE_PACMAN  
    MCH_PACMAN,
  #endif 	
  #ifdef ENABLE_GALAGA  
    MCH_GALAGA,
  #endif 	
  #ifdef ENABLE_DIGDUG  
    MCH_DIGDUG,
  #endif 	
  #ifdef ENABLE_FROGGER  
    MCH_FROGGER,
  #endif 	
  #if defined(ENABLE_DKONG) || defined(ENABLE_DKONGJR)
    MCH_DKONG,        // serve anche a dkongjr: la classe base dkong ritorna MCH_DKONG
  #endif
  #ifdef ENABLE_DKONGJR
    MCH_DKONGJR,
  #endif
  #ifdef ENABLE_DKONG3
    MCH_DKONG3,
  #endif
  #ifdef ENABLE_1942
    MCH_1942,
  #endif 	
  #ifdef ENABLE_LIZARD
    MCH_LIZARD,
  #endif
  #ifdef ENABLE_THEGLOB
    MCH_THEGLOB,
  #endif
  #ifdef ENABLE_MRTNT
    MCH_MRTNT,
  #endif
  #ifdef ENABLE_EYES  
    MCH_EYES,
  #endif
  #ifdef ENABLE_CRUSH  
    MCH_CRUSH,
  #endif 	
  #ifdef ENABLE_ANTEATER  
    MCH_ANTEATER,
  #endif 	
  #ifdef ENABLE_MRDO  
    MCH_MRDO,
  #endif 	
  #ifdef ENABLE_BOMBJACK  
    MCH_BOMBJACK,
  #endif 	
  #ifdef ENABLE_MSPACMAN
    MCH_MSPACMAN,
  #endif
  #ifdef ENABLE_COMMANDO
    MCH_COMMANDO,
  #endif
  #ifdef ENABLE_GNG
    MCH_GNG,
  #endif
  #ifdef ENABLE_GALAXIAN
    MCH_GALAXIAN,
  #endif 	
  #ifdef ENABLE_LADYBUG  
    MCH_LADYBUG,
  #endif 	
  #ifdef ENABLE_GYRUSS  
    MCH_GYRUSS,
  #endif 	
  #ifdef ENABLE_TIMEPLT  
    MCH_TIMEPLT,
  #endif 	
  #ifdef ENABLE_PENGO  
    MCH_PENGO,
  #endif 	
  #ifdef ENABLE_BAGMAN  
    MCH_BAGMAN,
  #endif 
  #ifdef ENABLE_SPACE
    MCH_SPACE,
  #endif
  #ifdef ENABLE_TUTANKHM
    MCH_TUTANKHM,
  #endif
  #ifdef ENABLE_MOTORACE
    MCH_MOTORACE,
  #endif
  #ifdef ENABLE_GREENBERET
    MCH_GREENBERET,
  #endif
  #ifdef ENABLE_MARIOBROS
    MCH_MARIOBROS,
  #endif
  #ifdef ENABLE_MOONPATROL
    MCH_MOONPATROL,
  #endif
  #ifdef ENABLE_BRUBBER
    MCH_BRUBBER,
  #endif
  #ifdef ENABLE_SCRAMBLE
    MCH_SCRAMBLE,
  #endif
  #ifdef ENABLE_SUPERCOBRA
    MCH_SUPERCOBRA,
  #endif
  #ifdef ENABLE_POPEYE
    MCH_POPEYE,
  #endif
  #ifdef ENABLE_FANTASY
    MCH_FANTASY,
  #endif
  #ifdef ENABLE_NIBBLER
    MCH_NIBBLER,
  #endif
  #ifdef ENABLE_GAPLUS
    MCH_GAPLUS,
  #endif
  #ifdef ENABLE_MOONCRESTA
    MCH_MOONCRESTA,
  #endif
  #ifdef ENABLE_VENUS
    MCH_VENUS,
  #endif
  #ifdef ENABLE_XEVIOUS
    MCH_XEVIOUS,
  #endif
  #ifdef ENABLE_ARKANOID
    MCH_ARKANOID,
  #endif
  #ifdef ENABLE_TEMPEST
    MCH_TEMPEST,
  #endif
  #ifdef ENABLE_ARKANOID2
    MCH_ARKANOID2,
  #endif
  #ifdef ENABLE_GIGAS
    MCH_GIGAS,
  #endif
  #ifdef ENABLE_GIGAS2
    MCH_GIGAS2,
  #endif
  #ifdef ENABLE_GOINDOL
    MCH_GOINDOL,
  #endif
  #ifdef ENABLE_SBRKOUT
    MCH_SBRKOUT,
  #endif
  #ifdef ENABLE_PHOENIX
    MCH_PHOENIX,
  #endif
  #ifdef ENABLE_PBACTION
    MCH_PBACTION,
  #endif
  #ifdef ENABLE_BOMBBEE
    MCH_BOMBBEE,
  #endif
  #ifdef ENABLE_ROADFIGHTER
    MCH_ROADFIGHTER,
  #endif
  #ifdef ENABLE_BOBLBOBL
    MCH_BOBLBOBL,
  #endif
  // NB: MCH_PANG e' in FONDO all'enum di proposito: la sensibilita' EC11 per gioco
  // e' salvata in NVS con chiave "t<valore machineType()>" (SPINNERINO.ino). Mettere
  // PANG in mezzo sposterebbe il valore dei giochi successivi -> le loro impostazioni
  // salvate verrebbero lette con la chiave sbagliata. In fondo = zero impatto.
  #ifdef ENABLE_PANG
    MCH_PANG,
  #endif
  // Stessa regola di PANG: le voci NUOVE si aggiungono IN FONDO, mai in mezzo,
  // o si spostano i machineType() dei giochi gia' salvati in NVS.
  #ifdef ENABLE_SUPXEVS
    MCH_SUPXEVS,
  #endif
};


// one inst at 3Mhz ~ 500k inst/sec = 500000/60 inst per frame
#define INST_PER_FRAME 300000/60/4 //=1250

#ifdef LED_PIN
  typedef const CRGB (*MenuLedType)[12][NUM_LEDS];
#endif

class machineBase
{
public:
    machineBase() { }
    virtual ~machineBase() { }

    virtual void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) {
      this->input = input;
      this->frame_buffer = framebuffer; 
      this->sprite = spritebuffer;
      this->memory = memorybuffer;
      memset(soundregs, 0, sizeof(soundregs)); 
     }

    virtual void reset() {
      Z80_i8080 = 0;   // default: flag P/V = overflow (Z80). I giochi 8080 (Bomb Bee) lo forzano a 1.
      for(current_cpu = 0; current_cpu < sizeof(cpu) / sizeof(Z80); current_cpu++)
        ResetZ80(&cpu[current_cpu]);

      memset(memory, 0, RAMSIZE);
      memset(soundregs, 0, sizeof(soundregs)); 

      for (int chip = 0; chip < 4; chip++) {
        for (int c = 0; c < 4; c++) {
          sn_period[chip][c] = 0;
          sn_volume[chip][c] = 15; // Muto
          sn_min_volume[chip][c] = 15;
          sn_hold[chip][c] = 0;
        }
      }
      current_cpu = 0;
      game_started = 0;
    }

    // Libera ROM caricate da SD (giochi SD-based, es. GnG da GALAGONE 4.3). In
    // SPINNERINO le ROM sono in flash -> default no-op.
    virtual void freeRoms() {}

    // ⭐⭐⭐ RESTITUISCE TUTTA LA MEMORIA CHE LA MACHINE SI TIENE FRA UNA PARTITA
    // E L'ALTRA. La chiama `return_to_menu()` per la machine che si sta
    // lasciando — cioe' l'uscita di TUTTI i giochi, una volta sola, con
    // l'emulazione gia' ferma e l'audio gia' in mute.
    //
    // ⛔ Perche' esiste: i giochi di questo progetto allocano i loro buffer al
    // primo `reset()` e non li liberavano mai. Finche' erano quattro giochi
    // piccoli non si vedeva; oggi PHOENIX ne tiene 264 KB e BUBBLE BOBBLE 92,
    // su ~768 KB di RAM interna totale. Il sintomo non arriva mai sul gioco
    // colpevole: arriva sul PROSSIMO gioco che chiede memoria interna e non la
    // trova ([[feedback_audio_muto_e_ram_interna]] — difetto a distanza).
    // Prima di questo gancio la cura era una lista di casi speciali in
    // `start_machine()` che cresceva di un gioco alla volta e restava sempre
    // indietro di uno ([[feedback_costante_del_primo_caso]]).
    //
    // ⚠️ COME SI IMPLEMENTA, per chi porta un gioco nuovo:
    //   1. si libera SOLO cio' che `reset()` sa ri-allocare da solo, cioe' le
    //      allocazioni scritte `if (!ptr) ptr = malloc(...)`. Un buffer allocato
    //      in `init()` NON va liberato qui: `init()` gira una volta sola al boot
    //      e nessuno lo richiama al rientro ([[feedback_init_una_volta_sola_vs_rientro]]);
    //   2. ogni puntatore liberato va messo a `nullptr`, altrimenti la guardia
    //      `if (!ptr)` del punto 1 non scatta e il gioco riparte su memoria
    //      restituita all'heap;
    //   3. se il gioco ha un flag «cache gia' pronta» (Phoenix: `cache_done`,
    //      Tempest: `rom_cached`), va rimesso a false insieme ai puntatori.
    //
    // Il default chiama `freeRoms()`, cosi' PANG — che aveva gia' la sua
    // liberazione scritta li' — funziona senza toccare una riga del suo file.
    virtual void freeBuffers() { freeRoms(); }

    virtual signed char machineType() { return MCH_MENU; }
    virtual signed char videoFlipY() { return 0; }
    virtual signed char videoFlipX() { return 0; }
    virtual signed char useVideoHalfRate() { return 0; }
    // Active row range for 240px displays: if >= 0, render 30 rows starting here at full 8px (no scaling)
    virtual int videoActiveStart() { return -1; }
    virtual bool isLandscape() { return false; }
    // true -> il gioco e' LANDSCAPE 240-wide e va reso commutando il pannello in
    // PORTRAIT (come FULL33): render diretto a righe da 240, niente strisce
    // trasposte. Vedi video.setLandscapeGame + render_one_frame. Solo Green Beret.
    virtual bool usesPortraitPanel() { return false; }
    virtual bool hasOpaqueBG() { return false; }  // true = skip memset before render_row
    virtual bool freeRunEmulation() { return false; }  // true = emulation runs on fixed timer, not vblank-locked
    // Periodo (us) del pacing free-run a tempo assoluto. Default 60Hz. PANG usa
    // 57.42Hz (17422) = refresh reale MAME capcom/mitchell.cpp.
    virtual uint32_t freeRunPeriodUs() { return 16667; }
    // Larghezza area gioco nel framebuffer (asse lungo, centrata in TFT_FB_W=320).
    // Default 256; Xevious usa 288 (224x288 portrait, per mostrare tutta l'altezza).
    virtual int videoGameWidth() { return 256; }

    virtual unsigned char rdZ80(unsigned short Addr) { return 0xff; }
    virtual void wrZ80(unsigned short Addr, unsigned char Value) { };
    virtual void outZ80(unsigned short Port, unsigned char Value) { };
    virtual unsigned char opZ80(unsigned short Addr) { return 0x00; }
    virtual unsigned char inZ80(unsigned short Port) { return 0x00; }

    virtual void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) { }
    virtual unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) { return 0x00; };
    virtual unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr)  { return 0x00; };
    virtual void wrI8048_xdm(struct i8048_state_S *state, unsigned char addr, unsigned char data) { };
    virtual unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) { return 0x00; };

    virtual void run_frame(void) { };
    virtual void prepare_frame(void) { };
    virtual void render_row(short row) { };
    
    virtual const signed char *waveRom(unsigned char value) { return 0; }
    virtual const unsigned short *logo(void) { return 0; };
    virtual bool hasNamcoAudio() { return false; }

    // Durata in millisecondi dell'hold direzionale BUTTON_LEFT/RIGHT generato
    // dall'EC11 per ogni click. 0 = nessun hold (default, OK per giochi che
    // leggono IN1 una volta per frame o usano paddle continuo come Arkanoid).
    // Giochi che leggono IN1 molte volte per frame dal Z80 (Space Invaders,
    // Galaga, MotoRace) ridefiniscono a ~100 ms per non perdere gli input.
    virtual int ec11PulseHoldMs() { return 0; }

    // Sensibilita' EC11 per-gioco, livello 1..10 (5 = default di fabbrica).
    // Regolata dal menu IMPOSTAZIONI nel carosello, persistita in NVS per
    // machineType(). Scala l'hold direzionale (qui sotto) e lo step encoder
    // paddle/dial (applicato in input.cpp via ec11_apply_sensitivity()).
    int ec11SensLevel = 5;
    // Hold direzionale effettivo = base * livello / 5. A livello 5 = invariato.
    // Min 15 ms quando il gioco usa l'hold (base>0), per non azzerarlo.
    int ec11HoldMsEff() {
      int base = ec11PulseHoldMs();
      if (base <= 0) return 0;
      int v = (base * ec11SensLevel) / 5;
      return v < 15 ? 15 : v;
    }

    // Disabilita il vol mode (hold FIRE 3s) per giochi dove FIRE viene tenuto
    // premuto a lungo come parte del gameplay normale (es. MotoRace dove
    // FIRE = acceleratore). Default false = vol mode abilitato.
    virtual bool disableVolMode() { return false; }

#if defined(ENABLE_TEMPEST) || defined(ENABLE_SBRKOUT)
    // M6502 (fake6502): dispatcher in emulation.cpp inoltra qui.
    // Wrapped in TEMPEST||SBRKOUT per non alterare la vtable quando 6502 non serve
    // (mantiene binary bit-identico per macchine Z80-only).
    virtual unsigned char rd6502(unsigned short Addr) { return 0xff; }
    virtual void          wr6502(unsigned short Addr, unsigned char Value) { }
    virtual unsigned char op6502(unsigned short Addr) { return rd6502(Addr); }
#endif
#ifdef LED_PIN
    virtual void menuLeds(CRGB *leds) { memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB)); };
    virtual void gameLeds(CRGB *leds) { memcpy(leds, menu_leds, NUM_LEDS*sizeof(CRGB)); };
#endif
    char game_started;	
    unsigned char soundregs[80];
    
    //Mr.Do!
    // SN76489: fino a 4 chip (Gigas ne ha 4 @ 0xFC00-0xFC03), 4 canali ciascuno
    // (3 tono + 1 rumore). Estesi da [2][4] a [4][4] per supporto Gigas.
    int sn_period[4][4];
    int sn_volume[4][4];
    int sn_min_volume[4][4]; // latched min volume per audio render cycle
    int sn_hold[4][4];       // hold counter: keep sound active for N render cycles
protected:
    virtual void blit_tile(short row, char col) { }
    virtual void blit_sprite(short row, unsigned char s) { }
	
    Input *input;
    Z80 cpu[3];
    char irq_enable[3];
    char current_cpu;
    unsigned char irq_ptr;

    int active_sprites;
    sprite_S *sprite;
    unsigned short *frame_buffer;
    unsigned char *memory;

private:	
#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK, LED_BLACK };
#endif
};

#endif