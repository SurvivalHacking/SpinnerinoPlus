#ifndef MARIOBROS_H
#define MARIOBROS_H

#include "../machineBase.h"

#ifdef ENABLE_MARIOBROS

// ============================================================================
// Mario Bros (Nintendo 1983) — porting -> SPINNERINO WIFI (ESP32-P4).
//
// Gioco ARCADE ORIZZONTALE (MAME ROT0): nativo 256 wide x 224 tall, single
// screen, NO scroll. Reso come Green Beret / Moon Patrol: il pannello viene
// commutato in PORTRAIT (video.setLandscapeGame) e le righe native vengono
// scritte DIRETTAMENTE in frame_buffer da render_row (righe 4..31), come FULL33.
//
// CAVEAT 256 vs 240: il pannello portrait e' largo 240, Mario Bros e' 256.
// Il render FULL33 (qui VERBATIM) scala 256->240 con nearest-neighbor
// (tabella scale_x[] per le tile, *15>>4 per gli sprite), quindi il gioco
// entra a piena larghezza senza croppare. NIENTE transpose software, niente
// buffer full-frame PSRAM.
//
//   - render_row(row) per row=4..31 (28 bande x 8 = 224) -> 240 px scalati.
//   - prepare_frame() esegue solo il parsing sprite (1x/frame), VERBATIM FULL33.
//   Il decode tile/sprite/palette/gfx-bank/scroll e' IDENTICO a FULL33.
//   usesPortraitPanel()=true; il pannello ruota, niente videoFlipX/Y.
// ============================================================================

#define MARIOBROS_AUDIO_QUEUE_LEN   16
#define MARIOBROS_AUDIO_QUEUE_MASK (MARIOBROS_AUDIO_QUEUE_LEN-1)

// Geometria nativa arcade orizzontale (MAME ROT0): 256 wide x 224 tall.
// Reso a 240 px (scalato in render_row come FULL33), pannello in PORTRAIT.
#define MARIO_NW   256
#define MARIO_NH   224

#include "mariobros_rom1.h"
#include "mariobros_rom1_high.h"
#include "mariobros_rom2.h"
#include "mariobros_dipswitches.h"
#include "mariobros_tilemap.h"
#include "mariobros_spritemap.h"
#include "mariobros_cmap.h"
#include "mariobros_logo.h"

class mariobros : public machineBase
{
public:
	mariobros() { }
	~mariobros() { }

 	void reset() override;
	signed char machineType() override { return MCH_MARIOBROS; }

	// Gioco LANDSCAPE 256-wide: pannello commutato in PORTRAIT come FULL33/Green
	// Beret (render diretto a righe da 240, scalate da 256). Vedi
	// video.setLandscapeGame + render_one_frame (branch usesPortraitPanel).
	bool isLandscape() override { return true; }
	bool usesPortraitPanel() override { return true; }
	// FULL33 multi-machine: Mario ha videoFlipY()=1 -> il loop di render inverte
	// l'ordine delle bande (flipRows: row = 35-row). E' COSI' che FULL33 raddrizza
	// Mario verticalmente (NON via MADCTL). La X e' gia' ribaltata dentro render_row
	// (ptr[(W-1)-px]); videoFlipX in FULL33 multi-machine non ha effetto a runtime,
	// quindi qui resta 0 (altrimenti start_machine attiverebbe mirror_x errato a 320).
	signed char videoFlipY() override { return 1; }

	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	// Z80 I/O ports (Z80 DMA chip at port 0x00)
	void outZ80(unsigned short Port, unsigned char Value) override;
	unsigned char inZ80(unsigned short Port) override;

	void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) override;
	unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;
	void wrI8048_xdm(struct i8048_state_S *state, unsigned char addr, unsigned char data) override;

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;

	const unsigned short *logo(void) override { return mariobros_logo; }

#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

	// AUDIO come il CABINATONE (mario.cpp, confermato sul cabinato): l'MCU
	// sonora gira sul task AUDIO a cicli veri, 733.333 / 32.000 per campione, e
	// il DAC si legge a ogni campione. Lo chiama audio.cpp, uno per campione.
	short audio_sample(void);

protected:
	// Not used — render_row does pixel-by-pixel rendering
	void blit_tile(short row, char col) override { }
	void blit_sprite(short row, unsigned char s) override { }

private:
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_BLACK, LED_RED, LED_GREEN, LED_GREEN, LED_GREEN, LED_RED, LED_BLACK };
#endif
	// Audio CPU (M58715 = I8049 clone, MCS-48 compatible). Vive sul task AUDIO.
	i8048_state_S cpu_8035;

	// ⚠️ Fra i due task passano solo BYTE volatile (i latch del driver MAME):
	// il Z80 li scrive, l'MCU li legge. Come sul CABINATONE.
	volatile unsigned char latch0 = 0;    // 7E00: il comando (MOVX con P2.7 = 1)
	volatile unsigned char latch1 = 0;    // 7F03-7F06: granchio, tartaruga, mosca, moneta -> P1
	volatile unsigned char latch3 = 0;    // 7F01-7F02: moneta, ghiaccio -> T0, T1
	volatile unsigned char int_line = 0;  // 7F00: morte -> INT dell'MCU, di LIVELLO
	unsigned char latch2 = 0xFF;          // P2 dell'MCU (pagina ROM, EA, selettore)
	volatile bool snd_pronto = false;     // false durante reset(): l'audio tace
	unsigned int  cyc_frac = 0;
	int           snd_debito = 0;
	unsigned char dac = 0x80;
	// filtri d'uscita della scheda (netlist nl_mario_dac + mixer)
	float bq_b0, bq_b1, bq_b2, bq_a1, bq_a2, bq_z1, bq_z2, lp_a, lp_y;
	int   hp_x, hp_y;
	void snd_run(int cicli);

	// LS259 mainlatch registers
	unsigned char gfx_bank = 0;
	unsigned char palette_bank = 0;
	unsigned char nmi_mask = 0;
	unsigned char gfx_scroll = 0;

	// Z80 DMA (simple hardcoded copy like DKong)
};

#endif // ENABLE_MARIOBROS
#endif // MARIOBROS_H
