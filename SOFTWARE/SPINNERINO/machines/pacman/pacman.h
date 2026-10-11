#ifndef PACMAN_H
#define PACMAN_H

#include "pacman_dipswitches.h"
#include "pacman_logo.h"
#include "pacman_rom.h"
#include "pacman_tilemap.h"
#include "pacman_spritemap.h"
#include "pacman_cmap.h"
#include "pacman_wavetable.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class pacman : public machineBase
{
public:
	pacman() { }
	~pacman() { }

	signed char machineType() override { return MCH_PACMAN; }
	int videoGameWidth() override { return 288; }   // Pac-Man 224x288: 288 px sull'asse lungo (trasposto)
	void reset() override;
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const signed char *waveRom(unsigned char value) override;
	const unsigned short *logo(void) override;
	bool hasNamcoAudio() override { return true; }
	
#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	virtual const unsigned short *tileRom(unsigned short addr);
	virtual const unsigned short *colorRom(unsigned short addr);
	virtual const unsigned long *spriteRom(unsigned char flags, unsigned char code);

private:
	// Render SPINNERINO trasposto (come Xevious): nativo 224x288 in pac_full
	// (PSRAM), poi render_row() lo traspone nel framebuffer 288 di SPINNERINO.
	unsigned short *pac_full = nullptr;
	unsigned short *rt       = nullptr;
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_BLUE, LED_BLACK, LED_YELLOW, LED_YELLOW, LED_YELLOW, LED_BLACK, LED_BLUE };
#endif
};

#endif