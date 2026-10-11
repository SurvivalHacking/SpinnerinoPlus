#ifndef DKONGJR_H
#define DKONGJR_H

#include "../machineBase.h"

#ifdef ENABLE_DKONGJR

// ============================================================================
// Donkey Kong Jr (1982) — porting da FULL33 a SPINNERINO WIFI (ESP32-P4).
//
// dkongjr EREDITA dalla classe base `dkong` (Z80 + i8048 audio + samples).
// Dalla base prende: rd/wr/op Z80 (override), audio i8048, trigger_sound, run_frame.
// Qui si adatta SOLO il render per SPINNERINO.
//
// Adattamento SPINNERINO (vs FULL33):
//   - Core emulazione (rd/wr/op + audio i8048 + parsing sprite) IDENTICO a FULL33.
//   - Render: FULL33 disegnava strip 224-wide direttamente in frame_buffer. Qui si
//     rende l'intero frame NATIVO (224x256) in dkj_full (PSRAM) dentro prepare_frame
//     riusando i blit VERBATIM (destinazione rt invece di frame_buffer), poi
//     render_row TRASPONE 90 in frame_buffer landscape (stride = DKONGJR_NH = 288),
//     esattamente come pacman/bombjack.
//   - videoGameWidth() = 288 (= altezza nativa, asse lungo display).
//
// NB altezza: l'area display arcade di dkong/dkongjr e' 224x288 (vedi video.cpp
//     FULL33 setAddrWindow 224x288; tileaddr ha 36 righe -> 36*8=288). Le righe
//     tile attive sono 2..33 (256px di gioco), 0,1,34,35 restano nere. Usare 256
//     troncherebbe le righe tile 32,33 e gli sprite con y>256. Quindi NH=288.
// ============================================================================

#include "dkongjr_rom1.h"
#include "dkongjr_rom2.h"
#include "dkongjr_logo.h"
#include "dkongjr_tilemap.h"
#include "dkongjr_spritemap.h"
#include "dkongjr_cmap.h"
#include "../dkong/dkong.h"
#include "../tileaddr.h"

// Geometria nativa portrait (vertical arcade): 224 wide x 288 tall.
#define DKONGJR_NW   224
#define DKONGJR_NH   288

class dkongjr : public dkong
{
public:
	dkongjr() { }
	~dkongjr() { }

	void reset() override;
	signed char machineType() override { return MCH_DKONGJR; }
	bool isLandscape() override { return false; }
	// Asse lungo display = altezza nativa 288 (centrata in TFT_FB_W=320).
	int videoGameWidth() override { return DKONGJR_NH; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;

protected:
	// Render NATIVO di una strip (8 righe) in rt = dkj_full + offset. Math VERBATIM
	// FULL33 (stride 224 = DKONGJR_NW).
	void render_strip(short srow);
 	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	// Frame nativo completo 224x256 (PSRAM, lazy-alloc in reset).
	unsigned short *dkj_full = nullptr;
	// Target di scrittura della strip corrente (= dkj_full + srow*8*DKONGJR_NW).
	unsigned short *rt = nullptr;

	unsigned char palette_bank = 0; // bank-switching palette (banco 0/1/2/3)
	unsigned char gfx_bank = 0;     // bank-switching tile (banco 0: 0..255, banco 1: 256..511)
	unsigned char flip_screen = 0;  // cocktail (non usato in upright)
};

#endif // ENABLE_DKONGJR
#endif // DKONGJR_H
