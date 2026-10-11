#ifndef FROGGER_H
#define FROGGER_H

#include "../machineBase.h"

#ifdef ENABLE_FROGGER

// ============================================================================
// Konami/Sega Frogger (1981) — porting da FULL33 a SPINNERINO WIFI (ESP32-P4).
//
// Hardware (MAME galaxian/frogger):
//   Main CPU:  Z80, ROM cpu1 ($0000-$3FFF)
//   Audio CPU: Z80 + AY-3-8910 (path generico AY in audio.cpp, gia' cablato)
//   Video:     tile 8x8 con SCROLL per-corsia (acqua/strada) + 8 sprite 16x16.
//              Game nativo VERTICALE 224 wide x 288 tall.
//
// Adattamento SPINNERINO (vs FULL33):
//   - Core emulazione (rd/wr/out/in/op/run_frame + parsing sprite + SCROLL)
//     IDENTICO a FULL33: stesso framework machineBase, stesse ROM (.h in flash).
//   - Render: FULL33 disegnava la strip 224-wide direttamente in frame_buffer.
//     Qui rendiamo l'intero frame NATIVO (224x288) in fr_full (PSRAM) dentro
//     prepare_frame riusando i blit + lo scroll VERBATIM, poi render_row
//     TRASPONE 90 in frame_buffer landscape (stride = 288), come pacman/bombjack.
//   - videoGameWidth() = 288 (= altezza nativa, asse lungo display).
//
// NOTA: NH = 288 (NON 256). Frogger usa lo schema Galagino a 36 righe logiche
// (tileaddr 0..35 -> 36*8 = 288); sprite Y testati < 288; render scarta solo
// le righe 0,1,34,35. Identico a pacman SPINNERINO.
// ============================================================================

#include "frogger_rom1.h"
#include "frogger_rom2.h"
#include "frogger_dipswitches.h"
#include "frogger_logo.h"
#include "frogger_tilemap.h"
#include "frogger_spritemap.h"
#include "frogger_cmap.h"
#include "../tileaddr.h"

// Geometria nativa portrait (vertical arcade): 224 wide x 288 tall.
#define FROGGER_NW   224
#define FROGGER_NH   288

class frogger : public machineBase
{
public:
	frogger() { }
	~frogger() { }

	void reset() override;
	signed char machineType() override { return MCH_FROGGER; }
	bool isLandscape() override { return false; }
	// Asse lungo display = altezza nativa 288 (come pacman/bombjack/Xevious)
	int videoGameWidth() override { return FROGGER_NH; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;
	unsigned char inZ80(unsigned short Port) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
    const unsigned short *logo(void) override;

#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
    // Blit NATIVI (output non ruotato) in rt = fr_full + offset strip. Math
    // IDENTICA a FULL33 (la rotazione e' solo nella trasposizione di render_row).
    void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	void blit_tile_scroll(short row, signed char col, short scroll);
	// Corpo originale di render_row(), scrive in rt (frame nativo).
	void render_strip(short row);
	virtual const unsigned short *tileRom(unsigned short addr);
	virtual const unsigned short *colorRom(unsigned short addr);
	virtual const unsigned long *spriteRom(unsigned char flags, unsigned char code);

	unsigned char snd_irq_state = 0;
	unsigned char snd_command;
	unsigned char snd_filter;
	unsigned long snd_icnt;
	unsigned char snd_ay_port;

private:
	// Frame nativo completo 224x288 (PSRAM, lazy-alloc in reset).
	unsigned short *fr_full = nullptr;
	// Target di scrittura della strip corrente (= fr_full + srow*8*FROGGER_NW).
	unsigned short *rt = nullptr;
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_RED, LED_GREEN, LED_YELLOW, LED_YELLOW, LED_YELLOW, LED_GREEN, LED_RED };
#endif

};

#endif // ENABLE_FROGGER
#endif // FROGGER_H
