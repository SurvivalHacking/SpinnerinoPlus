#ifndef GREENBERET_H
#define GREENBERET_H

#include "../machineBase.h"

#ifdef ENABLE_GREENBERET

// ============================================================================
// Green Beret / Rush'n Attack (Konami 1985) — porting da FULL33 a SPINNERINO
// WIFI (ESP32-P4).
//
// RESA LANDSCAPE COME FULL33 (2026-06-18): Green Beret e' un gioco orizzontale
// 240x224. FULL33 lo mostra mettendo il pannello ST7789 in PORTRAIT (MADCTL
// senza MV) e aprendo una finestra larga 240: il gioco riempie tutta la
// larghezza fisica del pannello, piena risoluzione, niente schiacciamento.
// SPINNERINO normalmente gira trasposto (MADCTL 0x60); per questo gioco
// commutiamo il pannello in portrait (vedi video.setLandscapeGame /
// start_machine / return_to_menu) e usiamo il render FULL33 VERBATIM:
//   - isLandscape() = true  -> il loop usa il path portrait (36 righe da 240).
//   - render_row(row) scrive le righe native 240-wide DIRETTAMENTE in
//     frame_buffer (stride 240), righe 4..31 = 224 px gioco centrati in 288.
//   - NESSUN buffer full-frame, NESSUNA trasposizione/scaling software.
//
// Hardware (MAME konami/gberet.cpp): Z80 @ 3.072 MHz, SN76489AN, tilemap 64x32
// scroll per-riga (k005849), tile 8x8 4bpp, sprite 16x16 4bpp pre-flippati.
//
// Incluso dal .ino in TU singola Arduino.
// ============================================================================

#include "greenberet_rom.h"
#include "greenberet_dipswitches.h"
#include "greenberet_logo.h"
#include "greenberet_tilemap.h"
#include "greenberet_spritemap.h"
#include "greenberet_cmap.h"

// Memory layout - from MAME gberet_state::prg_map (EXACT)
// memory[0x0000..0x07FF] = Color RAM  (Z80: 0xC000-0xC7FF, 2KB)
// memory[0x0800..0x0FFF] = Video RAM  (Z80: 0xC800-0xCFFF, 2KB)
// memory[0x1000..0x10FF] = Sprite RAM2(Z80: 0xD000-0xD0FF, 256B)
// memory[0x1100..0x11FF] = Sprite RAM (Z80: 0xD100-0xD1FF, 256B)
// memory[0x1200..0x1FFF] = Work RAM   (Z80: 0xD200-0xDFFF, 3.5KB !!!)
#define GB_CRAM_OFF    0x0000
#define GB_VRAM_OFF    0x0800
#define GB_SPRAM2_OFF  0x1000
#define GB_SPRAM_OFF   0x1100
#define GB_WRAM_OFF    0x1200

// MANOPOLA VELOCITA' (gioco + musica): Green Beret ha 1 solo Z80 ed e'
// frame-locked, quindi il TEMPO della musica = frame-rate. run_frame esegue
// NUM/DEN frame di gioco per frame video. 10/10 = 1.0x (fedele). Se la musica
// risulta lenta, alza GB_SPEED_NUM (11, 12, ...).
#define GB_SPEED_NUM 10
#define GB_SPEED_DEN 10

// ANTI-FLICKER: SPINNERINO mostra a 60Hz, FULL33 a 30Hz (VIDEO_HALF_RATE). Green
// Beret fa sfarfallare gli sprite di alcuni nemici a frame alterni: a 60Hz si
// vede il blink ("sprite mancanti"), a 30Hz no (campiona sempre la stessa fase).
// Con persistenza di N frame per-slot, uno sprite spento viene ridisegnato dal
// frame precedente -> nemico sempre completo, NIENTE ghosting sui movimenti (gli
// sprite attivi usano sempre la posizione corrente). 1 = contrasta il blink a
// frame alterni; 0 = comportamento originale (sfarfalla).
#define GB_SPRITE_PERSIST 1

// Numero di slot sprite hardware (offs 0..0xBC step 4 = 48).
#define GB_SPRITE_SLOTS 48

class greenberet : public machineBase
{
public:
    greenberet() { }
    ~greenberet() { }

    signed char machineType() override { return MCH_GREENBERET; }
    bool isLandscape() override { return true; }
    // true -> il render loop usa il path PORTRAIT (pannello commutato, righe 240).
    bool usesPortraitPanel() override { return true; }

    void reset() override;

    unsigned char rdZ80(unsigned short Addr) override;
    void wrZ80(unsigned short Addr, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;
    unsigned char inZ80(unsigned short Port) override;
    void outZ80(unsigned short Port, unsigned char Value) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;
    const unsigned short *logo(void) override;

#ifdef LED_PIN
    void menuLeds(CRGB *leds) override;
    void gameLeds(CRGB *leds) override;
#endif

private:
    // Accumulatore manopola velocita' (vedi GB_SPEED_NUM/DEN e run_frame).
    int speed_acc = 0;

    // Anti-flicker: ultimo sprite valido per slot HW + eta' (frame da quando lo
    // slot ha prodotto uno sprite valido). Vedi GB_SPRITE_PERSIST e prepare_frame.
    sprite_S      gb_slot_spr[GB_SPRITE_SLOTS];
    unsigned char gb_slot_age[GB_SPRITE_SLOTS];

    // k005849 scroll registers: 64 bytes (0-31=low, 32-63=high bit)
    unsigned char k005849_scroll[64];

    // k005849 control registers (0xE040-0xE047)
    unsigned char k005849_ctrl[8];

    // SN76489 sound
    unsigned char sn_latch;
    unsigned char sn_latch_reg;

    void sn76489_write(unsigned char data);

#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_GREEN, LED_WHITE, LED_GREEN, LED_WHITE, LED_GREEN, LED_WHITE, LED_GREEN };
#endif
};

#endif // ENABLE_GREENBERET
#endif // GREENBERET_H
