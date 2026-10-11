#ifndef _1942_H
#define _1942_H

#include "1942_rom1.h"
#include "1942_rom2.h"
#include "1942_rom1_b0.h"
#include "1942_rom1_b1.h"
#include "1942_rom1_b2.h"
#include "1942_dipswitches.h"
#include "1942_logo.h"
#include "1942_character_cmap.h"
#include "1942_spritemap.h"
#include "1942_tilemap.h"
#include "1942_charmap.h"
#include "1942_sprite_cmap.h"
#include "1942_tile_cmap.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class _1942 : public machineBase
{
public:
	_1942() { }
	~_1942() { }

	signed char machineType() override { return MCH_1942; } 
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void reset() override;
	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;
	bool hasNamcoAudio() override { return false; }
	// 224x288 native portrait -> trasposto in landscape SP (288 wide, come Xevious/Pacman)
	int videoGameWidth() override { return 288; }

#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	void blit_bgtile_row(short row);
	void lsl64(unsigned long *mask, int pix);
	void lsr64(unsigned long *mask, int pix);
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_WHITE, LED_BLACK, LED_GREEN, LED_GREEN, LED_GREEN, LED_BLACK, LED_WHITE };
#endif
	unsigned char _1942_bank = 0;
	unsigned char _1942_palette = 0;
	unsigned short _1942_scroll = 0;
	unsigned char _1942_sound_latch = 0;
	unsigned char _1942_ay_addr[2];
	char sub_cpu_reset = 1;
	// Render trasposto: frame nativo 224x288 in PSRAM, poi ruotato 90 in render_row
	unsigned short *_1942_full = nullptr;
	unsigned short *rt = nullptr;
};

#endif