#ifndef BOMBJACK_H
#define BOMBJACK_H

#include "../machineBase.h"

#ifdef ENABLE_BOMBJACK

// ============================================================================
// Tehkan Bomb Jack (1984) — porting da FULL33 a SPINNERINO WIFI (ESP32-P4).
//
// Hardware (MAME tehkan/bombjack.cpp):
//   Main CPU:  Z80 @ 4 MHz, 48 KB ROM ($0000-$7FFF + $C000-$DFFF)
//   Audio CPU: Z80 @ 3 MHz, 16 KB ROM
//   Sound:     3× AY-3-8910 (path generico AY in audio.cpp, gia' cablato)
//   Video:     bg image selezionabile (8 sfondi) + fg char 32x32 + 32 sprite
//              (16x16 e 32x32). Game nativo VERTICALE 224 wide x 288 tall.
//
// Adattamento SPINNERINO (vs FULL33):
//   - Core emulazione (rd/wr/out/in/op/run_frame + parsing sprite) IDENTICO a
//     FULL33: stesso framework machineBase, stesse ROM (.h non-PROGMEM in flash).
//   - Render: FULL33 disegnava strip 224-wide direttamente. Qui rendiamo l'intero
//     frame NATIVO (224x288) in bj_full (PSRAM) dentro prepare_frame riusando i
//     blit VERBATIM, poi render_row TRASPONE 90 in frame_buffer landscape
//     (stride = 288), esattamente come pbaction/1942/pacman.
//   - videoGameWidth() = 288 (= altezza nativa, asse lungo display, come Xevious).
// ============================================================================

#include "bombjack_rom1.h"
#include "bombjack_rom2.h"
#include "bombjack_dipswitches.h"
#include "bombjack_logo.h"
#include "bombjack_bg_maps.h"
#include "bombjack_bg_tiles.h"
#include "bombjack_fg_tiles.h"
#include "bombjack_sprites.h"
#include "../tileaddr.h"

#define PORT_P1_IN      0xB000  // P1 Controls (JOYSTICK + BUTTON)
#define PORT_P2_IN      0xB001  // P2 Controls (per modalita' COCKTAIL)
#define PORT_SYSTEM_IN  0xB002  // System Inputs (COIN + START)
#define PORT_WATCHDOG   0xB003  // Watchdog Reset (lettura resetta il timer)
#define PORT_DSW1_IN    0xB004  // DIP Switch 1
#define PORT_DSW2_IN    0xB005  // DIP Switch 2

// Geometria nativa portrait (vertical arcade): 224 wide x 288 tall.
#define BJ_NW   224
#define BJ_NH   288

class bombjack : public machineBase
{
public:
	bombjack() { }
	~bombjack() { }

	void reset() override;
	signed char machineType() override { return MCH_BOMBJACK; }
	bool isLandscape() override { return false; }
	// FREE-RUN: render pesante (224x288 nativo + transpose) < 60fps; con emulazione
	// gated dal render musica+gameplay risultavano lenti. freeRunEmulation()=true ->
	// emulazione a 60Hz reali (emulation.cpp), tempo/frequenze corretti come FULL33
	// con il rapporto audio 3:4 (BJ_AUDIO_NUM=30). Vedi spinnerino_wifi_freerun_emulation.
	bool freeRunEmulation() override { return true; }
	// Asse lungo display = altezza nativa 288 (centrata in TFT_FB_W=320, come Xevious)
	int videoGameWidth() override { return BJ_NH; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;
	unsigned char inZ80(unsigned short Port) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;

protected:
	// Blit NATIVI (output non ruotato) in rt = bj_full + offset strip. Math
	// IDENTICA a FULL33 (la rotazione e' solo nel sampling delle gfx ROM).
	void blit_tile_bg(short logical_row);
	void blit_tile_fg(short row, char col);
	void blit_sprite(short row, unsigned char s_idx);

private:
	// Frame nativo completo 224x288 (PSRAM, lazy-alloc in reset).
	unsigned short *bj_full = nullptr;
	// Cache dello SFONDO statico (224x288): ricalcolato pixel-per-pixel solo
	// quando m_bg_image cambia (a cambio schermo/livello). Ogni frame si fa solo
	// memcpy bj_bg->bj_full + overlay fg/sprite = render sotto i 16ms -> 60 fps.
	unsigned short *bj_bg = nullptr;
	int bj_bg_cached = -1;     // ultimo m_bg_image renderizzato in bj_bg
	// Target di scrittura della strip corrente (= base + srow*8*BJ_NW).
	unsigned short *rt = nullptr;

	uint16_t bombjack_palette[128];
	uint8_t palette[256];

	unsigned char sound_latch; // Scritta dalla Main CPU, letta dalla Audio CPU
	unsigned char sound_latch_backup = 0;
	unsigned char ay_address[3];

	unsigned char m_bg_image;  // Codice sfondo + bit visibilita'
	bool m_nmi_on;             // abilita NMI main CPU
	bool m_mmi_skip_audio_cpu;
	bool m_flip;               // screen flip
};

#endif // ENABLE_BOMBJACK
#endif // BOMBJACK_H
