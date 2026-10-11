#ifndef GYRUSS_H
#define GYRUSS_H

#include "../machineBase.h"
#include "../../cpus/m6809/m6809.h"
#include "gyruss_rom_main.h"
#include "gyruss_rom_sub.h"
#include "gyruss_rom_audio.h"
#include "gyruss_rom_i8039.h"
#include "gyruss_tilemap.h"
#include "gyruss_spritemap.h"
#include "gyruss_palette.h"
#include "gyruss_dipswitches.h"
#include "gyruss_logo.h"
#include "gyruss_logo_tft.h"

// Gyruss memory layout offsets in shared memory[] buffer
// memory[0x0000..0x03FF] = Color RAM (1KB)
// memory[0x0400..0x07FF] = Video RAM (1KB)
// memory[0x0800..0x17FF] = Main Z80 Work RAM (4KB)
// memory[0x1800..0x1FFF] = Audio Z80 RAM (1KB, at offset 0x1800 from AY perspective mapped at 0x6000)
#define GYR_CRAM_OFF   0x0000
#define GYR_VRAM_OFF   0x0400
#define GYR_WRAM_OFF   0x0800
#define GYR_ARAM_OFF   0x1800

class gyruss : public machineBase
{
public:
    gyruss() { }
    ~gyruss() { }

    void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) override;
    void reset() override;

    signed char machineType() override { return MCH_GYRUSS; }
    signed char useVideoHalfRate() override { return 1; }
    signed char videoFlipY() override { return 0; }

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

    // M6809 sub-CPU memory access (called from C callbacks)
    uint8_t sub_read(uint16_t addr);
    void sub_write(uint16_t addr, uint8_t val);
    uint8_t sub_read_opcode(uint16_t addr);

    // Audio Z80 dual-core support
    void start_audio_task();
    void stop_audio_task();
    void run_audio_samples(int nsamp);   // fette da UN CAMPIONE, a CICLI VERI
    inline bool is_audio_cpu();
    volatile uint8_t audio_running;

    // --- i8039 sample MCU (drums/percussioni di Gyruss) ---
    // 6o generatore audio: lo Z80 audio scrive un comando su soundlatch2 (port
    // 0x18) e pulsa l'IRQ (port 0x14); l'8039 legge il comando dal BUS e
    // riproduce il campione scrivendo P1 -> i8039_dac (DAC). Vedi MAME konami/gyruss.cpp.
    void          wrI8048_port(struct i8048_state_S *state, unsigned char port, unsigned char val) override;
    unsigned char rdI8048_port(struct i8048_state_S *state, unsigned char port) override;
    unsigned char rdI8048_xdm (struct i8048_state_S *state, unsigned char addr) override;
    unsigned char rdI8048_rom (struct i8048_state_S *state, unsigned short addr) override;
    inline int    i8039_sample() { return (int)i8039_dac - 128; }  // DAC centrato per il mix audio
    inline void   step_i8039()   { i8048_step(&i8039_cpu); }       // 1 istruzione 8039 (chiamato dal render)
    // IRQ 8039 passato via flag volatile (lo Z80 audio e il render girano su core diversi)
    inline void   service_i8039_irq() {
        if (i8039_irq_pending) { i8039_cpu.notINT = false; i8039_irq_pending = 0; }
    }

protected:
    void blit_tile(short row, char col);
    void blit_sprite(short row, unsigned char s_idx);

private:
    // M6809 sub-CPU
    m6809_state sub_cpu;
    unsigned char sub_ram[0x800];     // Sub-CPU local RAM (0x4000-0x47FF)
    unsigned char shared_ram[0x800];  // Shared RAM (Z80: 0xA000-0xA7FF, M6809: 0x6000-0x67FF)
    unsigned char sub_irq_mask;

    // Audio
    volatile unsigned char sound_latch;
    unsigned char sound_latch_pending;
    volatile unsigned char sound_irq_pending;  // latched IRQ for audio Z80
    unsigned char ay_address[5];      // 5 AY address latches (tutti e 5 renderizzati)
    // Contatore dei cicli VERI dello Z80 sonoro: e' la base dei tempi che il
    // driver legge dal registro 14 dell'AY3 (MAME: total_cycles()/1024 % 10),
    // cioe' il TEMPO della musica. Non e' piu' "approssimato" nonostante il nome.
    volatile unsigned long audio_cycle_approx;
    uint32_t snd_cyc_acc;             // resto 16.16 dei cicli per campione (111,86)

    // i8039 sample MCU state
    struct i8048_state_S i8039_cpu;
    volatile unsigned char soundlatch2;       // comando dallo Z80 audio (port 0x18)
    volatile unsigned char i8039_dac;         // P1 = valore DAC corrente (campione)
    volatile unsigned char i8039_irq_pending; // IRQ 8039 latchato dallo Z80 (port 0x14)

    // Audio dual-core
    TaskHandle_t audio_task_handle;
    int emu_core_id;

    // Video
    unsigned char flip_screen;
    unsigned char scanline_counter;
    unsigned char tile_snapshot[0x800]; // CRAM+VRAM snapshot for tearing-free rendering

    // Frame counter for sprite double-buffering
    unsigned char frame_odd;

    // --- Render SPINNERINO (trasposto): nativo 224x256 (stride 224) in gyr_full
    // (PSRAM), poi render_row() lo traspone (fb_x=gfb_y, fb_y=223-gfb_x, gw=256).
    unsigned short *gyr_full = nullptr;
    unsigned short *rt        = nullptr;

#ifdef LED_PIN
    const CRGB menu_leds[7] = { LED_BLUE, LED_CYAN, LED_WHITE, LED_CYAN, LED_WHITE, LED_CYAN, LED_BLUE };
#endif
};

// Global pointer for M6809 callbacks (only one gyruss instance)
extern gyruss *g_gyruss_instance;

#endif
