#ifndef BOBLBOBL_H
#define BOBLBOBL_H

// ============================================================================
// Bubble Bobble (set "boblbobl" = bootleg "Bobble Bobble" SENZA MCU 68705,
// protezione realizzata da PAL ic43_a/b).  Taito 1986.
//
// FASE 3: 3 CPU Z80 (main + slave/video + sound) — sound NON ancora attivo —
// memory map completa, banking 0xFB40, protezione PAL ic43, rendering object/
// sprite (sistema a colonne di Bubble Bobble) -> primo schermo vero.
// Audio = FASE 5 (riuso fm.c YM2203 + OPL YM3526).
//
// Sorgenti MAME (verificati): taito/bublbobl.cpp, bublbobl_m.cpp, bublbobl_v.cpp.
//
// Hardware:
//   Main CPU  : Z80 @ 6 MHz, banco 0x8000-0xBFFF via 0xFB40 ((data^4)&7)
//   Slave CPU : Z80 @ 6 MHz, parte in RESET finche' main non setta bit4 di 0xFB40
//   Sound CPU : Z80 @ 3 MHz + YM2203 + YM3526 (FASE 5)
//   Video     : 256x224 ROT0, 100% sprite. videoram+objectram = STESSA SRAM 8KB
//               (0xC000-0xDFFF): il rendering indicizza fino a 0x1FFF -> trattate
//               come UN buffer unico 0x2000. Palette in RAM 0xF800-0xF9FF
//               (256 col x 2 byte, RGBx_444 big-endian). PROM a71-25.41 pilota
//               le colonne sprite. Reso landscape su pannello portrait (240-wide).
// ============================================================================

#include "../machineBase.h"

// ROM/gfx/PROM: extern decl. Le definizioni sono nei *.h inclusi UNA volta da
// boblbobl.cpp (una sola copia in flash).
extern const uint8_t boblbobl_rom_main[0x30000];    // bb3+bb5+bb4 (banchi da 0x10000)
extern const uint8_t boblbobl_rom_sub[0x8000];      // a78-08 (slave/video)
extern const uint8_t boblbobl_rom_audio[0x8000];    // a78-07 (sound, FASE 5)
extern const uint8_t boblbobl_gfx_decoded[16384*64];// 16384 tile 8x8 4bpp, 1B/px
extern const uint8_t boblbobl_prom[256];            // a71-25.41 timing/colonne

// ~6 MHz Z80, 60 fps -> ~100k cicli/frame; *4 StepZ80 per iter (avg ~10 cicli).
// Calibrabile se troppo lento/veloce.
#define BB_MAIN_ITER_PER_FRAME 2400
#define BB_SUB_ITER_PER_FRAME  2400
// Sound Z80 @ 3 MHz -> ~5000 istr/frame (/4 StepZ80 = 1250 iter). Calibrabile.
#define BB_SND_ITER_PER_FRAME  1250
// Trim TEMPO musica: >1.0 = piu' LENTO, <1.0 = piu' veloce. Il tick musicale e'
// l'IRQ del timer YM2203 (l'OPL e' solo per sync), quindi questo trim agisce sul
// periodo dei timer YM2203 (e OPL, per coerenza). Tarato a orecchio.
#define BB_MUSIC_TEMPO  1.04f

class boblbobl : public machineBase
{
public:
    boblbobl() {}
    ~boblbobl() {}

    void init(Input *input, unsigned short *framebuffer, sprite_S *spritebuffer, unsigned char *memorybuffer) override;
    void reset() override;
    // Restituisce i ~92 KB di RAM interna (indexfb 64K + vram + share1 + i tre
    // buffer audio): la chiama `return_to_menu()`, `reset()` li ri-alloca al
    // rientro. Il conto e le due condizioni di sicurezza sono in boblbobl.cpp.
    void freeBuffers() override;

    signed char machineType() override { return MCH_BOBLBOBL; }
    bool isLandscape() override { return true; }
    bool usesPortraitPanel() override { return true; }   // landscape su pannello portrait (come GnG)
    // Render pesante (compose object 256x256): disaccoppia emul/render -> il
    // gioco gira a 60 Hz REALI, il display si aggiorna piu' lento (come pbaction).
    bool freeRunEmulation() override { return true; }
    // Render video (CPU1) dimezzato: alleggerisce il carico e la contesa PSRAM/bus
    // col core emulazione+audio (CPU0), che con i core FM reali e' al limite.
    signed char useVideoHalfRate() override { return 1; }

    unsigned char rdZ80(unsigned short Addr) override;
    void          wrZ80(unsigned short Addr, unsigned char Value) override;
    unsigned char opZ80(unsigned short Addr) override;
    unsigned char inZ80(unsigned short Port) override;
    void          outZ80(unsigned short Port, unsigned char Value) override;

    void run_frame(void) override;
    void prepare_frame(void) override;
    void render_row(short row) override;

    // ===== AUDIO (FASE 5) — accessibili da callback C e da audio.cpp =====
    // Chip emulati (handle opachi): YM2203 (fm.c, FM+timer), YM3526 (fmopl.c, OPL
    // musica), PSG emu2149 (SSG dello YM2203). Tutto generato sul core emulazione.
    void *ym2203_chip = nullptr;
    void *ym3526_chip = nullptr;
    void *psg_chip    = nullptr;
    uint8_t  ssg_latch = 0;                 // registro SSG selezionato (callback fm.c)
    volatile int sound_irq_ym2203 = 0;      // set dal FM_IRQHANDLER (timer YM2203)
    volatile int sound_irq_ym3526 = 0;      // set dall'OPL_IRQHANDLER

    // Timer OPL (YM3526) pilotato a mano: il core fmopl.c VGMPlay NON auto-temporizza
    // (handler commentati). Bubble Bobble usa il TIMER OPL per il tick musicale
    // (poll su status bit7). Intercetto reg 0x02/0x03/0x04 e chiamo ym3526_timer_over.
    uint8_t opl_reg = 0;                     // registro OPL latchato
    uint8_t opl_t1_val = 0, opl_t2_val = 0;  // reg 0x02 / 0x03
    bool    opl_t1_on = false, opl_t2_on = false;
    float   opl_t1_acc = 0, opl_t2_acc = 0;
    float   opl_t1_reload = 0, opl_t2_reload = 0;

    // Timer YM2203 pilotato a mano (handler non-NULL -> fm.c disabilita il timer
    // interno). E' il TICK MUSICALE. Periodo dal callback bb_fm_timer (count/clock).
    bool    ym_tA_on = false, ym_tB_on = false;
    float   ym_tA_period = 0, ym_tB_period = 0;   // in campioni
    float   ym_tA_acc = 0, ym_tB_acc = 0;

    // Ring buffer audio (SPSC: produttore=core emul, consumatore=task audio).
    static const int BB_RING = 4096;        // potenza di 2
    int16_t *ring = nullptr;
    volatile uint32_t ring_head = 0;        // letto/avanzato dal consumatore
    volatile uint32_t ring_tail = 0;        // avanzato dal produttore

    // Estrae fino a n campioni mono dal ring (chiamato dal task audio). Ritorna
    // il numero effettivamente estratti (< n se ring quasi vuoto).
    int audioPop(int16_t *out, int n);

private:
    // --- RAM allocata in reset() (DRAM interna, fallback PSRAM) ---
    uint8_t *vram     = nullptr;   // 0x2000 : videoram 0xC000-0xDCFF + objectram 0xDD00-0xDFFF (SRAM unica)
    uint8_t *share1   = nullptr;   // 0x1800 : work RAM condivisa main<->sub (0xE000-0xF7FF)
    uint8_t *palram   = nullptr;   // 0x200  : palette RAM 0xF800-0xF9FF
    uint8_t *miscram  = nullptr;   // 0x200  : bootleg work 0xFC00-0xFDFF
    uint8_t *indexfb  = nullptr;   // 256*256: framebuffer indici palette (composto in prepare_frame)
    uint16_t pal_rgb565[256];      // palette pre-calcolata RGB565 byte-swap

    // --- Banking ---
    const uint8_t *rom_bank_ptr = boblbobl_rom_main + 0x10000;  // banco 0x8000-0xBFFF

    // --- Stato macchina ---
    bool    sub_running   = false; // bit4 di 0xFB40 (slave Z80 in reset finche' false)
    uint8_t video_enable  = 0;     // bit6 di 0xFB40
    uint8_t flipscreen    = 0;     // bit7 di 0xFB40

    // --- Protezione PAL ic43 (boblbobl) ---
    uint8_t ic43_a = 0;
    uint8_t ic43_b = 0;
    uint32_t prot_rng = 0x12345678;  // per ic43_a_r offset!=0 (machine().rand())

    // --- DIP (default: Game English, 3 vite, Normal) ---
    uint8_t dsw0 = 0xFE;
    uint8_t dsw1 = 0x3F;

    // --- Sound CPU (cpu[2], Z80 @ 3 MHz) ---
    uint8_t *sndram = nullptr;     // 0x8000-0x8FFF (4KB)
    bool sound_reset = false;      // 0xFA03 (true = sound CPU tenuto in reset)
    uint8_t  main_to_sound = 0, sound_to_main = 0;          // latch 0xFA00 / 0xB000
    volatile bool main_to_sound_pending = false;
    volatile bool sound_to_main_pending = false;
    bool sound_nmi_enabled = false;                          // 0xB001/0xB002
    bool sound_nmi_prev = false;                             // edge-detect linea NMI (enabled&&pending)
    // Generazione audio (temp buffer 1 frame)
    int32_t *fmbuf  = nullptr;     // YM2203 FM
    int32_t *oplbuf = nullptr;     // YM3526 OPL
    uint32_t snd_accum = 0;        // accumulatore frazionario (residuo us->campioni)
    uint32_t bb_last_audio_us = 0; // timestamp ultima generazione (tempo reale)
    void sound_init_chips();
    void gen_audio(int nsamples);

    void bankswitch_w(uint8_t data);
    void update_palette(int idx);
    uint8_t ic43_a_r(uint8_t offset);
    void    ic43_a_w(uint8_t offset);
    uint8_t ic43_b_r(uint8_t offset);
    void    ic43_b_w(uint8_t offset, uint8_t data);
    uint8_t read_in0();
    void    draw_tile(int code, int color, int flipx, int flipy, int x, int y);
};

#endif // BOBLBOBL_H
