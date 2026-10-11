#ifndef XEVIOUS_H
#define XEVIOUS_H

#include "xevious_rom_cpu1.h"
#include "xevious_rom_cpu2.h"
#include "xevious_rom_cpu3.h"
#include "xevious_dipswitches.h"
#include "xevious_logo.h"
#include "xevious_spritemap.h"
#include "xevious_tilemap.h"
#include "xevious_bgtiles.h"
#include "xevious_cmap.h"
#include "xevious_bgdata.h"       // bb_r rom2a/rom2b: xvi_9.2a + xvi_10.2b ORIGINALI (con CPU patchata 0x3ed4)
#include "xevious_bg_rom2c.h"     // bb_r rom2c: xvi_11.2c ORIGINALE
// #include "xevious_bgmap.h"     // 10.1d grezza (bootleg) NON usata: la CPU patchata vuole la mappa originale
#include "xevious_sr3.h"
#include "xevious_wavetable.h"
#include "xevious_scrolltable.h"
#include "../machineBase.h"

#define XEVIOUS_NMI_DELAY  30

class xevious : public machineBase
{
public:
	// ⭐ ESPLOSIONI (Namco 54XX). Sull'hardware le fa un MCU che pilota un
	// generatore di rumore; noi non lo emuliamo, ma i suoi COMANDI arrivano
	// gia' qui dentro (e' il device 3 del 06XX) e finivano in namco_customio
	// senza che li leggesse nessuno. Si riconoscono dai primi due byte del
	// blocco: `30 40` = navetta, `40 40` = bersaglio (misurati, vedi
	// [[xevious_esplosioni_54xx]]). Il suono e' un campione preso da Super
	// Xevious (NES), riprodotto con lo STESSO schema gia' collaudato su
	// Galaga e letto da `Audio::namco_render_buffer()`: il contatore vale il
	// DOPPIO dei campioni perche' il puntatore avanza ogni due (16 kHz su 32).
	volatile unsigned short  snd_boom_cnt = 0;
	const signed char       *snd_boom_ptr = NULL;
	unsigned char            s54_b[2] = {0, 0};   // primi due byte del blocco
	unsigned char            s54_n = 0;           // quanti byte gia' visti
	xevious() {
		pRom1 = xevious_rom_cpu1;
		pRom2 = xevious_rom_cpu2;
		pRom3 = xevious_rom_cpu3;
	}
	~xevious() { }

	void reset() override;
	signed char machineType() override { return MCH_XEVIOUS; }
	int videoGameWidth() override { return 288; }  // Xevious 224x288: usa 288 px verticali

	// --- Velocita' gioco (turbo leggero, richiesta utente 2026-07-04) ---
	// free-run = emulazione a cadenza FISSA (timer), sganciata dal render. Il
	// periodo determina la velocita': 16667us = 60Hz (normale). Piu' BASSO = piu'
	// VELOCE. NON tocca XEV_STEPS/NMI (timing-critici per scroll/grafica).
	//   16000 = 62.5Hz ~ +4%   15600 = 64Hz ~ +7%   15300 = 65Hz ~ +9%
	// Se troppo veloce, ALZA il numero verso 16667. (Effetto collaterale: render
	// asincrono -> possibile lievissimo tearing, come PANG.)
	bool freeRunEmulation() override { return true; }
	uint32_t freeRunPeriodUs() override { return 16000; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;

	const signed char *waveRom(unsigned char value) override;
	const unsigned short *logo(void) override;
	bool hasNamcoAudio() override { return true; }

protected:
	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;

private:
	void blit_bg_row(short row);
	inline unsigned char namco_read(unsigned short Addr);
	inline void namco_write(unsigned short Addr, unsigned char Value);

	unsigned char namco_command = 0;
	unsigned char namco_mode = 0;
	unsigned char namco_nmi_counter = 0;
	unsigned char namco_credit = 0x00;
	unsigned char namco_customio[16] = {};


	const unsigned char *pRom1;
	const unsigned char *pRom2;
	const unsigned char *pRom3;

	unsigned short scrollBgX = 0;
	unsigned short scrollBgY = 0;
	unsigned short scrollFgX = 0;
	unsigned short scrollFgY = 0;
	unsigned char flipScreen = 0;

	unsigned char bs_reg[2] = {};

	unsigned char watchdog_counter = 0;
	unsigned short watchdog_feed_count = 0;

	char sub_cpu_reset = 1;

	// --- Render SPINNERINO (trasposto) ---
	// xevious.cpp renderizza il frame nativo (orientamento GALAGONE, 224x224)
	// in xev_full (PSRAM), poi render_row() lo traspone nel framebuffer 256.
	unsigned short *xev_full = nullptr;   // frame nativo intero (PSRAM, lazy)
	unsigned short *rt        = nullptr;  // base render-target della strip corrente

	bool demoActive = false;
	unsigned short demoFrame = 0;
	bool coinInserted = false;
	bool coinReported = false;
	bool startReported = false;
	// Era una `static` dentro namco_read(): reset() non poteva azzerarla, quindi
	// al riavvio a caldo l'edge dei tasti veniva dalla partita precedente.
	unsigned char prevKeymask = 0;
};

#endif
