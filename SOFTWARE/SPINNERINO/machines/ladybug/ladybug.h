#ifndef LADYBUG_H
#define LADYBUG_H

#include "../machineBase.h"

#ifdef ENABLE_LADYBUG

// ============================================================================
// Lady Bug (Universal, 1981) - porting da FULL33 a SPINNERINO WIFI (ESP32-P4).
//
// Hardware (MAME universal/ladybug.cpp):
//   CPU:   Z80A @ 4 MHz (single CPU, no audio CPU)
//   ROM:   24KB (0x0000-0x5FFF)
//   Sound: 2x SN76489 (0xB000 / 0xB001), path SN dedicato in audio.cpp
//   Video: tilemap 32x32 + sprite 16x16. Game nativo VERTICALE 224 x 288.
//   IRQ:   nessun vblank IRQ (polling IN1 bit 6-7), coin -> NMI.
//
// Adattamento SPINNERINO (vs FULL33), stesso schema di bombjack/pacman:
//   - Core emulazione (rd/wr/op/run_frame + parsing sprite) IDENTICO a FULL33.
//   - Render: FULL33 disegnava strip 224-wide gia' "display oriented" in
//     frame_buffer. Qui i blit scrivono il frame NATIVO completo (224x288) in
//     lb_full (PSRAM) usando 'rt' come destinazione (math invariata, stride
//     224 = NW), poi render_row TRASPONE 90 in frame_buffer landscape
//     (stride = LADYBUG_NH = 288).
//   - isLandscape()=false, videoGameWidth()=288 (asse lungo display).
// ============================================================================

#include "ladybug_rom.h"
#include "ladybug_dipswitches.h"
#include "ladybug_logo.h"
#include "ladybug_tilemap.h"
#include "ladybug_spritemap.h"
#include "ladybug_cmap.h"
// NOTE: Lady Bug does NOT use shared tileaddr.h - has custom tilemap scan

// Geometria nativa portrait (vertical arcade): 224 wide x 288 tall.
#define LADYBUG_NW 224
#define LADYBUG_NH 288

class ladybug : public machineBase
{
public:
	ladybug() {
		pRom1 = ladybug_rom_cpu1;
	}
	~ladybug() { }

	void reset() override;
	signed char machineType() override { return MCH_LADYBUG; }
	signed char videoFlipX() override { return 1; }
	bool hasNamcoAudio() override { return false; }
	bool isLandscape() override { return false; }
	// Asse lungo display = altezza nativa 288 (centrata in TFT_FB_W, come bombjack).
	int videoGameWidth() override { return LADYBUG_NH; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;

#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	// Helper SPINNERINO: costruisce una strip nativa (8 righe) in rt.
	void render_strip(short srow);

	const unsigned char *pRom1;    // 24KB CPU ROM

	// Video state
	unsigned char flipScreen = 0;

	// Vblank state (toggled in run_frame for polling via IN1 bits 6-7)
	unsigned char vblankActive = 0;

	// Coin NMI tracking
	unsigned char coinPrev = 0;

	// Frame counter for game_started detection
	unsigned short frameCount = 0;

private:
	void SN76489_Write_2chip(int chip, unsigned char data);
	int sn_last_register[2] = {0, 0};

	// Frame nativo completo 224x288 (PSRAM, lazy-alloc in reset).
	unsigned short *lb_full = nullptr;
	// Target di scrittura della strip corrente (= lb_full + srow*8*LADYBUG_NW).
	unsigned short *rt = nullptr;

#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_RED, LED_RED, LED_BLACK, LED_RED, LED_BLACK, LED_RED, LED_RED };
#endif
};

#endif // ENABLE_LADYBUG
#endif // LADYBUG_H
