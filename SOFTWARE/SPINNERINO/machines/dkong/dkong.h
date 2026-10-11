#ifndef DKONG_H
#define DKONG_H

#if !defined(ENABLE_DKONG) && !defined(ENABLE_DKONGJR)
#error "ENABLE_DKONG mancante nel config.h!"
#endif

#define DKONG_AUDIO_QUEUE_LEN   16
#define DKONG_AUDIO_QUEUE_MASK (DKONG_AUDIO_QUEUE_LEN-1)

// Render SPINNERINO trasposto (come dkongjr/1942): frame nativo 224x288 in
// dk_full (PSRAM), poi render_row ruota 90 nel framebuffer (stride DKONG_NH).
#define DKONG_NW   224
#define DKONG_NH   288

#include "dkong_rom1.h"
#include "dkong_rom2.h"
#include "dkong_dipswitches.h"
#include "dkong_logo.h"
#include "dkong_tilemap.h"
#include "dkong_spritemap.h"
#include "dkong_cmap.h"
#include "dkong_sample_walk0.h"
#include "dkong_sample_walk1.h"
#include "dkong_sample_walk2.h"
#include "dkong_sample_jump.h"
#include "dkong_sample_stomp.h"
#include "dkong_sample_fall.h"
#include "dkong_sample_roar.h"
#include "dkong_sample_snapjaw.h"
#include "../tileaddr.h"
#include "../machineBase.h"

class dkong : public machineBase
{
public:
	dkong() { }
	~dkong() { }

 	void reset() override;
	signed char machineType() override { return MCH_DKONG; }
	int videoGameWidth() override { return DKONG_NH; }   // 224x288 trasposto
	unsigned char rdZ80(unsigned short Addr) override;
	void wrZ80(unsigned short Addr, unsigned char Value) override;
	unsigned char opZ80(unsigned short Addr) override;

	void wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char pos) override;
	unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
	unsigned char rdI8048_xdm(struct i8048_state_S *state, unsigned char addr) override;
	unsigned char rdI8048_rom(struct i8048_state_S *state, unsigned short addr) override;

	// ⛔⛔⛔ FREE-RUN, ed e' la cura del «si sente male» (2026-08-28).
	// L'8048 riempie `dkong_audio_transfer_buffer` **dentro `run_frame`**, e il
	// task audio lo drena a ritmo FISSO: l'I2S consuma 32.000 campioni al
	// secondo qualunque cosa faccia il video. Senza questo override `dkong` e'
	// **GATED**: `emulation.cpp` fa `ulTaskNotifyTake(portMAX_DELAY)` e aspetta
	// il task video, quindi `run_frame` gira alla velocita' del RENDER. Se il
	// render non tiene i 60 fps, l'8048 produce meno campioni di quanti se ne
	// consumano, il buffer si svuota a intervalli regolari e si sente il
	// gracchio — piu' la musica rallentata.
	// ⭐ Non e' una scoperta nuova: sta scritto nel commento di
	// `emulation.cpp:92` («se l'emulazione fosse gated dal render, gameplay E
	// musica rallenterebbero in proporzione») e in `dkong3.h`, dove lo stesso
	// override C'E' GIA' per la stessa identica ragione. E' rimasto fuori da
	// dkong e da dkongjr, che hanno lo stesso schema a coda.
	// ⚠️ Prezzo dichiarato: il display aggiorna piu' lento quando il render non
	// arriva a 60 fps. E' il compromesso che questo progetto ha gia' scelto per
	// pbaction, PANG, Bubble Bobble e dkong3: meglio meno quadri che l'audio
	// rotto.
	// ⭐ Vale anche per DONKEY KONG JR, che eredita da questa classe.
	bool freeRunEmulation() override { return true; }

	void run_frame(void) override;
	void prepare_frame(void) override;
	void render_row(short row) override;
	const unsigned short *logo(void) override;
#ifdef LED_PIN
	void menuLeds(CRGB *leds) override;
	void gameLeds(CRGB *leds) override;
#endif

	unsigned char dkong_obuf_toggle = 0;
	unsigned char dkong_audio_transfer_buffer[DKONG_AUDIO_QUEUE_LEN][64];
	unsigned char dkong_audio_rptr = 0, dkong_audio_wptr = 0;
	unsigned short dkong_sample_cnt[6] = { 0,0,0,0,0,0 };
	const signed char *dkong_sample_ptr[6];

protected:
 	void blit_tile(short row, char col) override;
	void blit_sprite(short row, unsigned char s) override;
	void trigger_sound(char snd);

	// the audio cpu is a mb8884 which in turn is 8048/49 compatible
	i8048_state_S cpu_8048;

	// sound effects register between 8048 and z80
	unsigned char dkong_sfx_index = 0x00;

	unsigned char dkong_audio_assembly_buffer[64];

private:
#ifdef LED_PIN
	const CRGB menu_leds[7] = { LED_BLACK, LED_YELLOW, LED_RED, LED_RED, LED_RED, LED_YELLOW, LED_BLACK };
#endif
	// special variables for dkong
	unsigned char colortable_select = 0;

	// Render trasposto SPINNERINO: buffer nativo 224x288 (PSRAM) + base strip.
	unsigned short *dk_full = nullptr;
	unsigned short *rt      = nullptr;
};

#endif