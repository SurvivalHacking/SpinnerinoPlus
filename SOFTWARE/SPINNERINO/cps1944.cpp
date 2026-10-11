// ============================================================================
// cps1944.cpp — 1944: THE LOOP MASTER (Capcom CPS-2, set 1944u) per SPINNERINO.
//
// Porting da CABINATONE\cps1944.cpp, dove il gioco gira. Vedi emulation/cps1944.h
// per il perche' e' un player AUTONOMO e non un machineBase della griglia.
//
// FASI (incrementale, una conferma per fase — regola del progetto):
//   Fase 1-2 = 68000 + bus + ROM programma da SD  ✅
//   Fase 3   = Z80 sonoro (RINOMINATO Cps*, come spaceace.cpp con Ace*) +
//              QSound + task audio                ✅
//   Fase 4   = video: i tre tilemap + gli sprite con la priority bitmap, la
//              grafica letta dalla CACHE SU SD (20 MB, NON entrano in PSRAM) ✅
//   Fase 5   = input completo, uscita, barra volume ✅
//
//   ⭐ PERCHE' I VETTORI DI RESET RESTANO CONTROLLATI: sul CPS-2 gli OPCODE
//   arrivano dalla ROM DECIFRATA e i DATI da quella CIFRATA, e **anche i vettori
//   di reset passano dallo spazio opcode**. Se il doppio bus e' cablato bene
//   devono uscire esattamente SSP=00930000 e PC=000006D0 — due valori MISURATI
//   sul converter, non attesi a naso. Se escono altri numeri e' il pack sulla SD
//   ad essere sbagliato, e lo si dice PRIMA di far partire il gioco (qui non
//   c'e' Serial: [[feedback_p4_no_serial]]).
//
// ⛔ COSA E' STATO ADATTATO rispetto a CABINATONE (e SOLO questo):
//   - il display: la' pannello DSI 1024x600 + PPA hardware + lcd_draw_bitmap,
//     qui un ST7789 SPI 320x240 scritto A STRISCE di 8 righe (modello: ld_blit()
//     di spaceace.cpp). Vedi c1944_blit() per la geometria.
//   - l'input: bt_get_buttons() -> g_input->buttons_get(), maschere BUTTON_*.
//   - la palette: qui la LUT e' gia' BYTE-SWAPPED (lo vuole questo pannello SPI),
//     quindi il blit NON ri-swappa nulla.
// La LOGICA DI EMULAZIONE (layer, sprite, priorita') e' trascritta ALLA LETTERA:
// ogni ramo la' dentro e' stato pagato con del debug, non si "semplifica".
// ============================================================================
// ⚡ come m68k_core.c e outrun.cpp: l'IDE compila a **-Os**, e qui dentro ci sono
//    sia il bus del 68000 (~35.000 chiamate/frame) sia i cicli PER PIXEL del
//    renderer. A -Os `espandi8` non viene nemmeno inlinata e la memcpy da 8 byte
//    diventa una CHIAMATA a libc, ~50.000 volte per frame.
#pragma GCC optimize("O2")
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/cps1944.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"

// ---- Z80 sonoro, RINOMINATO per non collidere ------------------------------
// ⛔ In questo progetto esiste GIA' un core Z80 (`cpus/z80/`, usato dai giochi
// della griglia) e ne esiste un SECONDO per Space Ace (`cpus/z80ace/`, simboli
// `Ace*`). Il core di Marat dichiara RdZ80/WrZ80/... GLOBALI e senza contesto:
// due giochi che le definiscono entrambi = simbolo duplicato in link, anche se a
// runtime ne gira uno solo. Quindi TERZA copia, `cpus/z80cps/`, coi simboli
// `Cps*` — esattamente la cura di spaceace.cpp.
#define ExecZ80    CpsExecZ80
#define RunZ80     CpsRunZ80
#define ResetZ80   CpsResetZ80
#define IntZ80     CpsIntZ80
#define StepZ80    CpsStepZ80
#define JumpZ80    CpsJumpZ80
#define RdZ80      CpsRdZ80
#define WrZ80      CpsWrZ80
#define InZ80      CpsInZ80
#define OutZ80     CpsOutZ80
#define LoopZ80    CpsLoopZ80
#define PatchZ80   CpsPatchZ80
#define DebugZ80   CpsDebugZ80
#define OpZ80_INL  CpsOpZ80_INL
extern "C" {
#include "cpus/z80cps/Z80.h"
}
#include "qsound_hle.h"
#include "eeprom93c46.h"
#include "gfx_cache.h"
// ⚠️ dentro `extern "C"`, come faceva CABINATONE: `m68kops.h` (dove sta
// `m68k_set_tables`, NON in m68k.h) non ha guardie di linkage, e da un .cpp il
// nome verrebbe mangolato -> "undefined reference" in fase di link.
extern "C" {
#include "cpus/m68k/m68k.h"
#include "cpus/m68k/m68kops.h"
}
#include "m68k_bus.h"

extern Video video;
extern bool  input_consume_back_to_menu();

// ---------------------------------------------------------------- parametri
#define LD_FB_H       240
// ⛔⛔⛔ 32 KB, NON 16 — corretto il 2026-08-10. Questo task chiama `sd_mount()`,
// cioe' `esp_vfs_fat_sdmmc_mount()`, che fra init SDMMC e scansione FAT e' molto
// vorace di stack: con 16 KB lo sfonda e la scheda va in PANICO. Succede UNA
// VOLTA SOLA, al primo lancio dopo l'accensione (dopo, `sd_is_mounted()` e' vero
// e il mount non si rifa'): da qui il riavvio che l'utente vedeva **sempre la
// prima volta e mai le successive**, su tutti e tre i giochi da SD e su nessun
// altro. Il referto `PANICO` della schermata di caricamento torna: uno stack
// overflow e' esattamente quello.
// ⭐ 32768 non e' scelto a occhio: e' il valore che usano da sempre i player
// LASERDISC (LD_TASK_STACK in laserdisc.cpp/spaceace.cpp/dl2.cpp), che montano
// la SD dal proprio task e non si sono mai riavviati. I 16 KB venivano da
// [[feedback_emulation_task_stack_fm]], che pero' parla degli interpreti di CPU,
// non di FATFS.
#define C1944_STACK   32768
#define C1944_DIR     SD_MOUNT_POINT "/1944"

// ---- memoria (taglie da cps1944.cpp di CABINATONE, non arrotondate) --------
#define PROM_SZ   0x180000           // p.bin / pdec.bin, 1,5 MB ciascuno
#define RAM_SZ    0x010000
#define GFXRAM_SZ 0x030000
#define OBJ_SZ    0x002000
#define QRAM_SZ   0x002000
#define XRAM_SZ   0x004000
#define M68K_JUMP_SIZE (0x10000 * sizeof(void *))
#define M68K_CYC_SIZE  (5 * 0x10000)

// ---- video (fase 4) --------------------------------------------------------
#define CPS_W     384                // risoluzione nativa del CPS-2
#define CPS_H     224
#define GFX_BANK  4096               // banco della cache grafica
// ⚠️ 12 MB e non i 14 di CABINATONE: la' il binario e' un altro e la PSRAM ha un
// inquilino in meno. Qui il gioco tiene gia' ~12 MB (prom+pdec 3, q.bin 8,
// tabelle 68000 0,6) e il firmware ha i suoi buffer. E' un valore facile da
// alzare se la cache dovesse andare in thrashing (si vedrebbe dagli fps).
// 2026-08-09: portata a 14 MB — e' il valore di CABINATONE, dove il gioco gira.
// Meno cache = piu' miss, e ogni miss e' una lettura da SD dentro il quadro.
// PSRAM stimata in uso: ~26,5 MB su 32.
#define GFX_CACHE (14 * 1024 * 1024)

// ⭐⭐ IL CLOCK VERO DEL CPS-2 NON E' 16 MHz. cps2.cpp init_cps2nc():
//     m_maincpu->set_clock_scale(0.7375);   // HACK: RAM access waitstates...
// MAME lo fa girare a 0,7375 x 16 MHz = 11,8 MHz. Eseguire i cicli del clock
// nominale non e' solo il 36% di lavoro IN PIU': e' anche SBAGLIATO, perche' il
// gioco andrebbe piu' veloce dell'originale.
//   16.000.000 * 0,7375 / 59,63 = 197.900 cicli per frame
#define CPS_RIGHE    262
#define CPS_CIC      (197900 / CPS_RIGHE)
#define CPS_VBL_RIGA 240
#define FRAME_US     16770           // 59,63 Hz

// ⭐ Render a frame alterni: l'emulazione resta a 59,63 Hz (gioco e tempi
// giusti) e lo schermo si aggiorna un frame su due. Qui pesa ancora piu' che sul
// CABINATONE: un frame intero sul pannello e' 320*240*2 = 153.600 byte, che a 80
// MHz di SPI sono ~15 ms — quasi l'intero budget di un frame. Dimezzare il video
// e' la stessa leva che ha risolto Bubble Bobble.
#define CPS_VIDEO_HALF_RATE 1

// ⭐⭐ IDLE-SKIP DEL 68000 — 1 = acceso, 0 = spento (comportamento di prima).
// MISURA che lo giustifica (_verify_gfx/cps2_m68k/mk_m68kprof.py, 1800 quadri):
// su 16.239 istruzioni per quadro, 9.994 (il 62%) sono le DUE del ciclo
//     0007EE  4A79 0092 1CC4   TST.W  $921CC4
//     0007F4  6700 FFF8        BEQ.W  $0007EE
// cioe' il programma principale che aspetta il vblank: su CPS-2 il lavoro lo fa
// la ISR. run_frame() costa 18,6 ms su un budget di 16,77 e questa e' l'unica
// leva che vale un fattore 2.
// COME funziona: NON c'e' nessun indirizzo scritto a mano. Il core riconosce da
// solo il punto fisso (stesso PC, stessi registri, stesso SR, nessuna
// scrittura, letture solo da ROM o dalla finestra qui sotto) e consuma i cicli
// residui della FETTA — vedi il commento lungo in cpus/m68k/m68kcpu.c.
// ⛔ LA FINESTRA E' SOLO gfxram (0x900000-0x92FFFF), ed e' una scelta MOTIVATA:
//    dentro una fetta di m68k_execute() quella memoria la scrive solo d_w8/
//    d_w16/d_w32, cioe' il 68000 stesso (l'unico modo perche' cambi e' che giri
//    la sua ISR di vblank); il renderer la LEGGE e basta, e nessun altro task
//    la tocca. ⛔ NON ci si mette qram (0x618000-0x619FFF): quella e' la RAM
//    CONDIVISA col Z80 sonoro, che gira su un ALTRO core e la puo' scrivere
//    mentre la fetta e' in corso — un'attesa su qram NON e' saltabile.
//    (work RAM, objram e xram sarebbero altrettanto sicure, ma il ciclo vero sta
//    in gfxram e una sola finestra costa due confronti invece di sei.)
#define C1944_IDLE_SKIP 1
#define C1944_IDLE_LO   0x900000
#define C1944_IDLE_HI   0x92FFFF

// ---- geometria a schermo (vedi il commento lungo sopra c1944_blit) ----------
// L'asse da TFT_FB_W=320 e' il VERTICALE dell'utente, quello da LD_FB_H=240 e'
// l'ORIZZONTALE (lo dice ld_blit di spaceace.cpp: il video largo 176 e' centrato
// nei 320 con LD_X_OFF 72 e quelle sono le "bande" alta e bassa).
// L'ALTEZZA del gioco (224) va quindi sull'asse da 320: ci sta intera e avanzano
// 48 px di banda NERA sopra e 48 sotto — quelle che l'utente chiede.
#define C1944_X_OFF  ((TFT_FB_W - CPS_H) / 2)     // = 48, banda nera sopra/sotto
// La LARGHEZZA del gioco (384) va sull'asse da 240: si tengono 5 pixel ogni 8
// (384 * 5/8 = 240 esatti, 48 gruppi tondi). Vedi c1944_blit().

static uint16_t *g_strip = nullptr;
static Input    *g_input = nullptr;
static volatile bool g_stop = false;
static volatile bool g_done = false;

// ---- i cinque file e la loro dimensione ATTESA (byte veri del converter) ----
typedef struct { const char *nome; uint32_t byte; } c1944_file_t;
static const c1944_file_t C1944_FILES[] = {
    { "p.bin",     1572864 },   // 68000, spazio DATI (cifrato)
    { "pdec.bin",  1572864 },   // 68000, spazio OPCODE (decifrato)
    { "gfx.bin",  20971520 },   // tile+sprite, gia' de-interleavati (fase 4)
    { "m.bin",      327680 },   // Z80 sonoro (fase 3)
    { "q.bin",     8388608 },   // campioni QSound (fase 3)
};
static const int C1944_NFILE = sizeof(C1944_FILES) / sizeof(C1944_FILES[0]);

static int      g_ok = 0;
static uint64_t g_byte = 0;
static char     g_primo_ko[24] = "";

// ---- memoria emulata --------------------------------------------------------
static uint8_t *prom = nullptr, *pdec = nullptr;
static uint8_t *ram = nullptr, *gfxram = nullptr, *qram = nullptr, *xram = nullptr;
static uint8_t *objram[2] = { nullptr, nullptr };
// ⚠️ la lista sprite che si disegna NON e' quella viva: e' l'ISTANTANEA presa al
// vblank (cps2.cpp cps2_objram_latch). Senza, gli sprite tremolano di un frame.
static uint8_t *obj_latch = nullptr;
static void    *m68k_jump = nullptr, *m68k_cyc = nullptr;
static Eeprom93C46 eeprom;

static uint16_t cps_a[0x20], cps_b[0x20], out_reg[6];
static int      obj_bank = 0;
static uint16_t in0 = 0xFFFF, in1 = 0xFFFF, in2 = 0xFFFF;
// ⚠️ scritte dal bus del 68000 (CPU1) e lette dal task audio (core 0)
static volatile int z80_in_reset = 1, z80_reset_req = 0;

// ---- sonoro (QSound) -------------------------------------------------------
// Schema di ghouls.cpp/CABINATONE: il Z80 sonoro gira DENTRO il task audio, sul
// core 0, e il metronomo e' la write bloccante sull'I2S. E' legittimo perche' il
// bus del Z80 e' suo: l'unico contatto col 68000 e' la RAM condivisa.
#define Z80ROM_SZ  0x50000        // 32 KB fissi + 6 banchi da 16 KB
#define QSROM_SZ   0x800000       // 8 MB di campioni QSound
#define AUD_HZ     32000          // I2S nativo dello SPINNERINO
#define AUD_CHUNK  64             // 2 ms, come ghouls.cpp
#define SND_Z80_HZ 8000000        // cps2.cpp: Z80(config, m_audiocpu, 8_MHz_XTAL)
// 8 MHz / 32000 Hz = 250 cicli tondi per campione: nessun accumulatore 16.16
#define Z80_CYC_PER_SAMPLE (SND_Z80_HZ / AUD_HZ)
// IRQ periodico a 250 Hz (cps2.cpp: attotime::from_hz(8_MHz_XTAL / 32000))
#define SND_IRQ_OGNI (AUD_HZ / 250)     // = 128 campioni

static uint8_t *z80rom = nullptr;       // m.bin, 0x50000
static uint8_t *z80ram = nullptr;       // RAM privata 0xF000-0xFFFF
static uint8_t *qsrom  = nullptr;       // q.bin, 8 MB di campioni
static int      z80_bank = 0;
static Z80      sndcpu;
static struct qsound_chip *qs = nullptr;
static int16_t *aud_out = nullptr;
static volatile bool g_aud_running = false;

// ---- video: framebuffer nativo, palette e priority bitmap ------------------
static uint16_t *fb = nullptr;          // 384x224 RGB565, risoluzione nativa
static uint16_t *pal_lut = nullptr;     // 0xC00 voci gia' in RGB565
// ⭐⭐ LA PRIORITY BITMAP (screen.priority() di MAME), un byte per pixel.
// I tre tilemap ci fanno l'OR del loro bit (1, 2, 4) dove disegnano un pixel
// opaco; ogni sprite la consulta con la propria maschera e poi ci scrive 31.
// Senza di lei NON esiste nessuna priorita', ne' fra sprite e layer ne' fra
// sprite e sprite — su CABINATONE era il motivo per cui al boss mancava grafica
// e l'aereo spariva sotto di lui.
static uint8_t  *pri_bm = nullptr;
static uint16_t  pri_ctrl = 0;          // CPS2_OBJ_PRI, latchato al vblank
static GfxCache  gfx;

extern void laserdisc_audio_begin(void);
extern void laserdisc_audio_end(void);
extern void laserdisc_audio_write(const int16_t *stereo, int frames);
// modalita' regolazione volume + volume corrente (1..30, 1 = massimo): sono le
// stesse due funzioni che usano DL, Space Ace e DL2 per la loro barra.
extern bool input_is_vol_mode_active(void);
extern int  laserdisc_current_volume(void);

// ============================================================ bus del 68000
// ⚠️ TRASCRITTO ALLA LETTERA da CABINATONE\cps1944.cpp:184-266. Ogni ramo qui
// dentro e' stato pagato con del debug altrui: non si "semplifica".
static inline uint16_t rd16(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void     wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static unsigned int d_r16(unsigned int a)
{
    const unsigned pg = (a >> 16) & 0xFF;
    if (pg <= 0x3F) return (a < PROM_SZ) ? rd16(prom + a) : 0;
    if (a >= 0xFF0000)                  return rd16(ram + (a & 0xFFFF));
    if (a >= 0x900000 && a <= 0x92FFFF) return rd16(gfxram + (a - 0x900000));
    // ⭐ objram a DOPPIO BUFFER: l'indirizzo sceglie il banco RELATIVO
    // (cps2.cpp: m_objram[m_objram_bank ^ Xor], Xor=0 per 0x700000, 1 per 0x708000)
    if (a >= 0x708000 && a <= 0x70FFFF) return rd16(objram[(obj_bank ^ 1) & 1] + ((a - 0x708000) & 0x1FFF));
    if (a >= 0x700000 && a <= 0x701FFF) return rd16(objram[obj_bank & 1] + (a - 0x700000));
    // ⭐⭐ La RAM condivisa col sonoro e' larga OTTO bit: un byte ogni WORD del
    // 68000 (cps1.h qsound_sharedram_r: `return ram[offset] | 0xff00`).
    //   0x618000+2i  <->  shared[i]  <->  Z80 0xC000+i
    if (a >= 0x618000 && a <= 0x619FFF) return 0xFF00 | qram[(a - 0x618000) >> 1];
    if (a >= 0x660000 && a <= 0x663FFF) return rd16(xram + (a - 0x660000));
    if (a >= 0x400000 && a <= 0x40000B) return out_reg[(a - 0x400000) >> 1];
    if ((a & 0xFFFFC0) == 0x804140 || (a & 0xFFFFC0) == 0x800140) return cps_b[(a >> 1) & 0x1F];
    if (a == 0x804000) return in0;
    if (a == 0x804010) return in1;
    if (a == 0x804020) return (uint16_t)((in2 & 0xFFFE) | ee93c46_do(&eeprom));
    if (a == 0x804030) return 0xE021;     // volume al massimo, niente addon/rete
    if (a >= 0x8040B0 && a <= 0x8040B2) return 0xFFFF;
    return 0xFFFF;
}
static unsigned int d_r8(unsigned int a)
{
    const unsigned int w = d_r16(a & ~1u);
    return (a & 1) ? (w & 0xFF) : (w >> 8);
}
static unsigned int d_r32(unsigned int a) { return (d_r16(a) << 16) | d_r16(a + 2); }

// ⭐ spazio OPCODE: le ISTRUZIONI arrivano dalla ROM DECIFRATA, i dati da quella
// cifrata. E' il cuore del CPS-2 (m68kconf.h: M68K_SEPARATE_READS acceso).
static unsigned int o_r16(unsigned int a)
{
    if (((a >> 16) & 0xFF) <= 0x3F) return (a < PROM_SZ) ? rd16(pdec + a) : 0;
    return d_r16(a);
}
static unsigned int o_r8 (unsigned int a) { const unsigned w = o_r16(a & ~1u);
                                            return (a & 1) ? (w & 0xFF) : (w >> 8); }
static unsigned int o_r32(unsigned int a) { return (o_r16(a) << 16) | o_r16(a + 2); }

static void d_w16(unsigned int a, unsigned int v)
{
    const uint16_t d = (uint16_t)v;
    if (a >= 0xFF0000)                  { wr16(ram + (a & 0xFFFF), d); return; }
    if (a >= 0x900000 && a <= 0x92FFFF) { wr16(gfxram + (a - 0x900000), d); return; }
    if (a >= 0x708000 && a <= 0x70FFFF) { wr16(objram[(obj_bank ^ 1) & 1] + ((a - 0x708000) & 0x1FFF), d); return; }
    if (a >= 0x700000 && a <= 0x701FFF) { wr16(objram[obj_bank & 1] + (a - 0x700000), d); return; }
    if (a >= 0x618000 && a <= 0x619FFF) { qram[(a - 0x618000) >> 1] = (uint8_t)d; return; }
    if (a >= 0x660000 && a <= 0x663FFF) { wr16(xram + (a - 0x660000), d); return; }
    if (a >= 0x400000 && a <= 0x40000B) { out_reg[(a - 0x400000) >> 1] = d; return; }
    if ((a & 0xFFFFC0) == 0x804100 || (a & 0xFFFFC0) == 0x800100) { cps_a[(a >> 1) & 0x1F] = d; return; }
    if ((a & 0xFFFFC0) == 0x804140 || (a & 0xFFFFC0) == 0x800140) { cps_b[(a >> 1) & 0x1F] = d; return; }
    if (a == 0x804040) {   // EEPROMOUT: 12 = DI, 13 = CLK, 14 = CS (attivi alti)
        ee93c46_write_lines(&eeprom, (d >> 14) & 1, (d >> 13) & 1, (d >> 12) & 1);
        // ⭐⭐⭐ LO STESSO REGISTRO porta il RESET DEL Z80 SONORO, nel byte basso:
        // cps2.cpp cps2_eeprom_port_w -> "bit 3 - Allows access to Z80 address
        // space (Z80 reset)". ⛔ Senza, il sonoro non parte MAI: il Z80 scrive la
        // firma 0x77 in 0xCFFF nelle sue prime istruzioni, ma poi il 68000
        // COLLAUDA la RAM condivisa e la cancella. Sul ferro il Z80 e' tenuto in
        // reset durante il collaudo e riparte DOPO: solo allora la firma resta.
        { const int fuori = (d >> 3) & 1;
          if (fuori && z80_in_reset) z80_reset_req = 1;   // lo eseguira' il task audio
          z80_in_reset = !fuori; }
        return;
    }
    if (a == 0x8040E0) { obj_bank = d & 1; return; }
}
static void d_w8(unsigned int a, unsigned int v)
{
    const unsigned int w = d_r16(a & ~1u);
    d_w16(a & ~1u, (a & 1) ? ((w & 0xFF00) | (v & 0xFF)) : ((w & 0x00FF) | ((v & 0xFF) << 8)));
}
static void d_w32(unsigned int a, unsigned int v) { d_w16(a, v >> 16); d_w16(a + 2, v); }

// ============================================================ bus del Z80
// cps1.cpp `qsound_sub_map` (usata anche dal CPS-2):
//   0000-7FFF ROM fissa · 8000-BFFF banco da 16 KB (registro 0xD003, 4 bit)
//   C000-CFFF RAM CONDIVISA col 68000 · D000-D002 QSound · D007 stato QSound
//   F000-FFFF RAM privata (sul CPS-2 NON e' vista dal 68000)
static uint16_t qs_latch = 0;

extern "C" byte RdZ80(word a)
{
    if (a < 0x8000) return z80rom[a];
    if (a < 0xC000) return z80rom[0x10000 + z80_bank * 0x4000 + (a - 0x8000)];
    if (a < 0xD000) return qram[a - 0xC000];
    if (a == 0xD007) return qs_hle_ready(qs);
    if (a >= 0xF000) return z80ram[a - 0xF000];
    return 0;
}
extern "C" void WrZ80(word a, byte v)
{
    if (a >= 0xC000 && a < 0xD000) { qram[a - 0xC000] = v; return; }
    if (a == 0xD000) { qs_latch = (uint16_t)((qs_latch & 0x00FF) | (v << 8)); return; }
    if (a == 0xD001) { qs_latch = (uint16_t)((qs_latch & 0xFF00) | v);        return; }
    if (a == 0xD002) { qs_hle_write(qs, v, qs_latch);                         return; }
    if (a == 0xD003) { z80_bank = v & 0x0F;                                   return; }
    if (a >= 0xF000) { z80ram[a - 0xF000] = v;                                return; }
}
// Prelievo dell'OPCODE: il core lo DICHIARA e basta, lo definisce il gioco
// (spaceace.cpp fa lo stesso a riga 352). Sul CPS-2 e' la stessa lettura del
// bus — infatti CABINATONE passava `snd_rd` anche come quinto campo di Z80Bus.
extern "C" unsigned char OpZ80_INL(unsigned short a) { return RdZ80((word)a); }
extern "C" byte InZ80(word p)            { (void)p; return 0xFF; }
extern "C" void OutZ80(word p, byte v)   { (void)p; (void)v; }
extern "C" word LoopZ80(Z80 *R)          { (void)R; return INT_NONE; }
extern "C" void PatchZ80(Z80 *R)         { (void)R; }

// l'ACK azzera la linea e da' l'autovettore (cps1.cpp irqack_r azzera IPL1+IPL2)
static int iack(int level) { (void)level; m68k_set_irq(0); return M68K_INT_ACK_AUTOVECTOR; }

// Musashi dichiara le callback GLOBALI e senza contesto: qui si registrano nel
// dispatcher condiviso (m68k_bus), cosi' possono convivere piu' giochi a 68000.
static const struct M68kBus CPS_BUS = {
    d_r8, d_r16, d_r32,
    d_w8, d_w16, d_w32,
    iack,
    o_r8, o_r16, o_r32,
};

// ============================================================ espansione tile
// ⭐ Espandere gli 8 pixel a mano costa 8 iterazioni x 4 estrazioni di bit = 32
// operazioni per gruppo, e i gruppi sono ~59.000 per frame. Con una tabella che
// "apre" un byte in 8 byte (uno per bit) bastano 4 letture e 3 or, e poi si
// sovrappongono i quattro piani spostati di 0,1,2,3. Identico bit per bit.
// ⛔⭐ ALLOCATA A RUNTIME, non statica: 2.048 byte di RAM INTERNA occupati anche
// mentre gira un ALTRO gioco del binario. E' la regola pagata tre volte su
// questo progetto ([[fm_c_lfo_pm_table_128kb_ram]]).
static uint64_t *espandi_tab = nullptr;
static bool      espandi_pronta = false;
static void espandi_init(void)
{
    if (!espandi_tab) {
        espandi_tab = (uint64_t *)heap_caps_calloc(256, sizeof(uint64_t), MALLOC_CAP_INTERNAL);
        if (!espandi_tab)
            espandi_tab = (uint64_t *)heap_caps_calloc(256, sizeof(uint64_t), MALLOC_CAP_SPIRAM);
        if (!espandi_tab) return;
    }
    for (int b = 0; b < 256; b++) {
        uint64_t v = 0;
        for (int i = 0; i < 8; i++) v |= (uint64_t)((b >> (7 - i)) & 1) << (8 * i);
        espandi_tab[b] = v;
    }
    espandi_pronta = true;
}
static inline void espandi8(uint32_t w, uint8_t *pen)
{
    const uint64_t v = espandi_tab[w & 0xFF]
                     | (espandi_tab[(w >> 8) & 0xFF]  << 1)
                     | (espandi_tab[(w >> 16) & 0xFF] << 2)
                     | (espandi_tab[(w >> 24) & 0xFF] << 3);
    memcpy(pen, &v, 8);
}
// riga tutta trasparente: pen 15 su tutti e 8 i pixel = tutti i piani a 1
#define RIGA_VUOTA(w) ((w) == 0xFFFFFFFFu)

// ============================================================ palette
// cps1_build_palette: 6 pagine da 0x200, saltate se il bit corrispondente di
// palette_control (CPS-B 0x30) e' 0. Si converte in RGB565 UNA VOLTA per frame,
// non a ogni pixel.
// ⛔⛔ LA MASCHERA DEL DRIVER E' 0x3FFFF, MA IL BUFFER E' 0x30000.
// `cps1_base()` di MAME maschera con 0x3FFFF (256 KB) perche' li' la gfxram e'
// grande cosi'; qui `gfxram` e' `GFXRAM_SZ = 0x30000` (192 KB). I registri
// `cps_a[]` li scrive **il 68000 emulato**, quindi possono valere QUALUNQUE
// cosa: con un valore fra 0x30000 e 0x3FFFF si legge **fino a 64 KB oltre la
// fine del blocco**, e la palette (che avanza senza rimascherare) arriva a
// +70 KB. In PSRAM di solito non si nota; se il blocco capita in cima
// all'allocato l'indirizzo esce dalla finestra mappata e diventa
// un'ECCEZIONE DI LETTURA — intermittente, perche' dipende da dove
// l'allocatore ha messo `gfxram` in quel giro e da cosa ha scritto il gioco.
// ⭐ Il clamp NON cambia niente di visibile: nell'uso legittimo il gioco tiene
// questi registri dentro la sua gfxram, e i valori fuori range sono per
// definizione spazzatura che sull'hardware vero non esiste.
static inline unsigned base_gfxram(int reg, int boundary)
{
    int base = cps_a[reg] * 256;
    base &= ~(boundary - 1);
    unsigned b = (unsigned)(base & 0x3FFFF);
    if (b >= GFXRAM_SZ) b &= (GFXRAM_SZ - 1);     // resta DENTRO il blocco vero
    return b / 2;                                 // indice in WORD
}
static inline uint16_t gword(unsigned widx) { return rd16(gfxram + widx * 2); }

// ⚠️ la formula del colore fa TRE divisioni per voce, per 3072 voci = 9216
// divisioni per frame. I fattori possibili pero' sono solo 16 nibble x 16
// valori di luminosita': si precalcolano una volta in 256 byte.
// ⚠️ puntatore a RIGA da 16, non `uint8_t *`: cosi' `conv_tab[bi][n]` continua
// a funzionare senza toccare i punti d'uso (trappola nota del progetto).
static uint8_t (*conv_tab)[16] = nullptr;
static bool     conv_pronta = false;

static void costruisci_palette(void)
{
    if (!espandi_pronta) espandi_init();
    if (!conv_tab) {
        conv_tab = (uint8_t (*)[16])heap_caps_calloc(16, 16, MALLOC_CAP_INTERNAL);
        if (!conv_tab) conv_tab = (uint8_t (*)[16])heap_caps_calloc(16, 16, MALLOC_CAP_SPIRAM);
        if (!conv_tab) return;
    }
    if (!conv_pronta) {
        for (int bi = 0; bi < 16; bi++) {
            const int bright = 0x0F + (bi << 1);
            for (int n = 0; n < 16; n++)
                conv_tab[bi][n] = (uint8_t)(n * 0x11 * bright / 0x2D);
        }
        conv_pronta = true;
    }
    const unsigned base = base_gfxram(5, 0x400);      // CPS1_PALETTE_BASE
    const uint16_t ctrl = cps_b[0x30 / 2];            // palette_control
    unsigned src = base;
    for (int page = 0; page < 6; page++) {
        if ((ctrl >> page) & 1) {
            for (int off = 0; off < 0x200; off++) {
                const uint16_t p = gword(src++);
                const uint8_t *ct = conv_tab[(p >> 12) & 0x0F];
                const int r = ct[(p >> 8) & 0x0F];
                const int g = ct[(p >> 4) & 0x0F];
                const int b = ct[(p >> 0) & 0x0F];
                // ⚠️ RGB565 BYTE-SWAPPED: su questo pannello SPI il byte alto va
                // per secondo (su CABINATONE il DSI voleva i valori nativi).
                const uint16_t v = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
                pal_lut[0x200 * page + off] = (uint16_t)((v >> 8) | (v << 8));
            }
        } else if (src != base) src += 0x200;
    }
}

// ============================================================ grafica da SD
// ⭐ UNA lettura di cache per TILE: 64/128/512 dividono il banco da 4096, quindi
// un tile non sta MAI a cavallo di due banchi. `unit` e' in unita' da 8 righe
// (64 byte), il denominatore comune dei tre formati.
static inline const uint8_t *tile_ptr(unsigned unit)
{
    const uint32_t off = unit * 64;
    const uint8_t *b = gfx_cache_bank(&gfx, off);
    return b + (off & (GFX_BANK - 1));
}

// ⭐⭐ IL PUNTO CALDO del renderer. I quattro byte dei piani sono gli STESSI per 8
// pixel consecutivi, sono contigui e allineati a 4: si leggono UNA VOLTA PER RIGA
// come word da 32 bit invece che 4 byte per PIXEL.
// Conti per frame, con ~472.000 pixel disegnati:
//   prima  ~1.900.000 letture (4 per pixel)   dopo  ~59.000 (1 ogni 8 pixel)
// ⚠️ Su little-endian la word vale b0 | b1<<8 | b2<<16 | b3<<24, cioe' proprio i
// quattro piani: il valore e' identico bit per bit alla lettura byte per byte.

// ============================================================ layer
// ⚠️ Il pen TRASPARENTE del CPS e' il **15**, non lo 0 (cps1_v.cpp
// set_transmask(i, mask, 0x8000)). Trattando lo 0 come trasparente il fondo esce
// grigio pieno.
// `primask` = il bit che questo layer lascia nella priority bitmap (1, 2 o 4),
// com'e' in cps1_render_layer(..., layer, primask). Con primask 0 la bitmap non
// si tocca: e' il caso pri_ctrl==0, dove nessuno sprite consulta i bit dei layer
// (tutte le primasks[1..7] valgono 0) — non scriverla e' EQUIVALENTE, e risparmia
// ~400.000 accessi per frame.
static void disegna_layer(int layer, unsigned primask)
{
    static const int LATO[3]    = { 8, 16, 32 };
    static const int SHIFT[3]   = { 0, 1, 3 };
    static const int COLBASE[3] = { 0x20, 0x40, 0x60 };
    static const int REGBASE[3] = { 1, 2, 3 };
    static const int REGX[3]    = { 6, 8, 10 };
    static const int REGY[3]    = { 7, 9, 11 };

    const int lato = LATO[layer];
    const unsigned mapbase = base_gfxram(REGBASE[layer], 0x4000);
    // ⭐ L'area VISIBILE del CPS parte da (HBEND, VBEND) = (64, 16), non da (0,0):
    // HTOTAL 512 con visibile 64..448, VTOTAL 262 con visibile 16..240 (cps1.h).
    // Il pixel in alto a sinistra del nostro buffer e' quindi lo schermo (64,16).
    // Senza questa somma l'immagine esce SPOSTATA.
    // ⭐ Conferma dai dati: il gioco scrive scroll 0xFFC0/0xFFF0, cioe' -64 e -16
    // — proprio l'opposto del bordo. Sommandolo vengono zero.
    const int sx0 = (int)cps_a[REGX[layer]] + 64;
    const int sy0 = (int)cps_a[REGY[layer]] + 16;
    const int mapw = lato * 64;

    // si scorre per TILE, non per pixel: e' cio' che rende sostenibile la cache
    const int y_ini = ((sy0 % mapw) + mapw) % mapw;
    const int x_ini = ((sx0 % mapw) + mapw) % mapw;

    for (int sy = -(y_ini % lato); sy < CPS_H; sy += lato) {
        const int my  = (((y_ini + sy) % mapw) + mapw) % mapw;
        const int row = my / lato;
        for (int sx = -(x_ini % lato); sx < CPS_W; sx += lato) {
            const int mx  = (((x_ini + sx) % mapw) + mapw) % mapw;
            const int col = mx / lato;

            unsigned off;
            if (layer == 0)      off = (row & 0x1F) + ((col & 0x3F) << 5) + ((row & 0x20) << 6);
            else if (layer == 1) off = (row & 0x0F) + ((col & 0x3F) << 4) + ((row & 0x30) << 6);
            else                 off = (row & 0x07) + ((col & 0x3F) << 3) + ((row & 0x38) << 6);

            unsigned code = gword(mapbase + 2 * off);
            const uint16_t attr = gword(mapbase + 2 * off + 1);
            if (layer == 2) code &= 0x3FFF;
            const uint16_t *prow = pal_lut + (((attr & 0x1F) + COLBASE[layer]) * 16);
            const int flipx = (attr & 0x20) ? 1 : 0, flipy = (attr & 0x40) ? 1 : 0;

            // mapper_cps2: i layer stanno nel banco 1 della grafica
            const unsigned unit = 0x20000 + ((code << SHIFT[layer]) & 0x1FFFF);
            const uint8_t *t = tile_ptr(unit);
            const int set = (layer == 0) ? ((off >> 5) & 1) : 0;   // meta' 8x8

            // estremi visibili del tile, calcolati UNA volta invece che per pixel
            const int tx0 = (sx < 0) ? -sx : 0;
            const int tx1 = (sx + lato > CPS_W) ? (CPS_W - sx) : lato;
            if (tx0 >= tx1) continue;

            for (int ty = 0; ty < lato; ty++) {
                const int py = sy + ty;
                if (py < 0 || py >= CPS_H) continue;
                const int fy = flipy ? (lato - 1 - ty) : ty;

                // i pen della riga: una word da 32 bit ogni 8 pixel
                uint8_t pen[32];
                int vuota = 1;
                for (int g = 0; g < lato / 8; g++) {
                    // dove sta il gruppo g di 8 pixel dentro la grafica
                    int riga, mezza;
                    if (layer == 0)      { riga = fy;                mezza = set; }
                    else if (layer == 1) { riga = fy;                mezza = g; }
                    else                 { riga = fy * 2 + (g >> 1); mezza = g & 1; }
                    const uint32_t w = *(const uint32_t *)(t + riga * 8 + mezza * 4);
                    if (RIGA_VUOTA(w)) { memset(pen + g * 8, 15, 8); continue; }
                    vuota = 0;
                    espandi8(w, pen + g * 8);
                }
                if (vuota) continue;              // niente da disegnare su questa riga

                uint16_t *drow = fb + (size_t)py * CPS_W + sx;
                // flipx e' deciso una volta per TILE: tenerlo nel ciclo dei pixel
                // voleva dire un ternario ogni pixel
                if (!primask) {
                    if (!flipx) {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[tx];
                            if (p != 15) drow[tx] = prow[p];   // ⭐ trasparente = 15
                        }
                    } else {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[lato - 1 - tx];
                            if (p != 15) drow[tx] = prow[p];
                        }
                    }
                } else {
                    // tilemap.cpp scanline_draw_masked_ind16:
                    //   pri[i] = (pri[i] & (pcode>>8)) | pcode, con maschera 0xff
                    // di default -> e' un OR del bit del layer.
                    uint8_t *prip = pri_bm + (size_t)py * CPS_W + sx;
                    const uint8_t pm = (uint8_t)primask;
                    if (!flipx) {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[tx];
                            if (p != 15) { drow[tx] = prow[p]; prip[tx] |= pm; }
                        }
                    } else {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[lato - 1 - tx];
                            if (p != 15) { drow[tx] = prow[p]; prip[tx] |= pm; }
                        }
                    }
                }
            }
        }
    }
}

// ============================================================ sprite
// cps2_render_sprites. Record da 4 word. ⚠️ La lista e' l'ISTANTANEA del vblank.
// ⛔⭐⭐ NON e' vero che "si disegna dall'ultimo al primo cosi' il primo finisce
// sopra": drawgfx.cpp prio_transpen fa `pmask |= 1 << 31` e la macro
// PIXEL_OP_REBASE_TRANSPEN_PRIORITY scrive **31** nella priority bitmap su ogni
// pixel opaco, anche quando il pixel viene scartato. Uno sprite disegnato DOPO
// trova quel 31 e non passa: fra due sprite sovrapposti vince quello disegnato
// PRIMA, cioe' l'ULTIMO della lista. Sovrascrivere e basta da' l'ordine INVERSO
// — ed e' esattamente il "l'aereo passa sotto il boss".
// ⭐⭐⭐ Quali PRIORITA' compaiono davvero nella lista sprite di QUESTO frame.
// Serve a non far scrivere ai layer bit della priority bitmap che nessuno
// leggera' mai: e' l'ottimizzazione piu' grossa del renderer (vedi render()).
static uint8_t sprite_pri_usate(void)
{
    const uint8_t *ob = obj_latch;
    int last = OBJ_SZ / 2 - 4;
    for (int off = 0; off < OBJ_SZ / 2; off += 4)
        if (rd16(ob + (off + 1) * 2) >= 0x8000 || rd16(ob + (off + 3) * 2) >= 0xFF00) {
            last = off - 4; break;
        }
    uint8_t m = 0;
    for (int i = last; i >= 0; i -= 4) m |= (uint8_t)(1u << ((rd16(ob + i * 2) >> 13) & 7));
    return m;
}

static void disegna_sprite(const uint32_t *primasks)
{
    const uint8_t *ob = obj_latch;
    const int xoffs = 64 - (int)out_reg[4];      // CPS2_OBJ_XOFFS
    const int yoffs = 16 - (int)out_reg[5];      // CPS2_OBJ_YOFFS
    const unsigned n_tile = 0x1400000u / 128u;

    int last = OBJ_SZ / 2 - 4;
    for (int off = 0; off < OBJ_SZ / 2; off += 4)
        if (rd16(ob + (off + 1) * 2) >= 0x8000 || rd16(ob + (off + 3) * 2) >= 0xFF00) {
            last = off - 4; break;
        }

    for (int i = last; i >= 0; i -= 4) {
        int x = rd16(ob + (i + 0) * 2);
        int y = rd16(ob + (i + 1) * 2);
        // cps2.cpp: `const int priority = (x >> 13) & 0x07;` — si legge PRIMA che
        // x venga corretto dall'offset, e sceglie una delle 8 maschere.
        const uint32_t pmask = primasks[(x >> 13) & 0x07] | (1u << 31);
        const unsigned code0 = (unsigned)rd16(ob + (i + 2) * 2) + (unsigned)((y & 0x6000) << 3);
        const uint16_t colore = rd16(ob + (i + 3) * 2);
        const uint16_t *prow = pal_lut + ((colore & 0x1F) * 16);
        const int flipx = (colore >> 5) & 1, flipy = (colore >> 6) & 1;
        if ((colore >> 7) & 1) { x += (int)out_reg[4]; y += (int)out_reg[5]; }
        const int nx = ((colore & 0x0F00) >> 8) + 1;
        const int ny = ((colore & 0xF000) >> 12) + 1;

        for (int nys = 0; nys < ny; nys++) {
            // ⚠️ NIENTE conversione con segno: MAME maschera & 0x3FF e basta, e il
            // ritaglio lo fa la finestra di disegno. Convertire in negativo faceva
            // comparire sprite che sull'originale sono fuori schermo (377 pixel
            // diversi, trovati da mk_fwcmp.py sul PC).
            // ⭐ gli sprite hanno coordinate di SCHERMO: il nostro buffer parte da
            // (64,16), quindi si sottrae il bordo (vedi disegna_layer).
            const int sy = ((y + nys * 16 + yoffs) & 0x3FF) - 16;
            if (sy >= CPS_H) continue;
            for (int nxs = 0; nxs < nx; nxs++) {
                const int sx = ((x + nxs * 16 + xoffs) & 0x3FF) - 64;
                if (sx >= CPS_W) continue;
                const int ix = flipx ? (nx - 1 - nxs) : nxs;
                const int iy = flipy ? (ny - 1 - nys) : nys;
                // ⛔⭐⭐ Il codice del tile NON e' semplicemente `code + ix`. Nel
                // caso SENZA FLIP, MAME fa riciclare l'indice orizzontale dentro i
                // 4 bit BASSI invece di riportarlo sui bit alti (cps2.cpp,
                // commentato "pgear fix"):
                //     (code & ~0xf) + ((code + nxs) & 0xf) + 0x10 * nys
                // Su uno sprite LARGO — cioe' un boss — se `code & 0xF` non e' zero
                // i tile oltre il riporto finiscono su grafica del tutto diversa:
                // e' il motivo per cui al primo boss mancavano pezzi.
                // ⚠️ Gli altri tre rami (con flip) usano la somma semplice: e'
                // asimmetrico, ma e' cosi' nel driver e si copia com'e'.
                const unsigned code = (!flipx && !flipy)
                        ? (((code0 & ~0xFu) + ((code0 + nxs) & 0xF) + 0x10 * nys) % n_tile)
                        : ((code0 + ix + 0x10 * iy) % n_tile);
                const uint8_t *t = tile_ptr(code * 2);      // 16 righe = 2 unita'
                const int tx0 = (sx < 0) ? -sx : 0;
                const int tx1 = (sx + 16 > CPS_W) ? (CPS_W - sx) : 16;
                if (tx0 >= tx1) continue;
                for (int ty = 0; ty < 16; ty++) {
                    const int py = sy + ty;
                    if (py < 0 || py >= CPS_H) continue;
                    const int fy = flipy ? (15 - ty) : ty;
                    const uint32_t w0 = *(const uint32_t *)(t + fy * 8);
                    const uint32_t w1 = *(const uint32_t *)(t + fy * 8 + 4);
                    if (RIGA_VUOTA(w0) && RIGA_VUOTA(w1)) continue;
                    uint8_t pen[16];
                    if (RIGA_VUOTA(w0)) memset(pen, 15, 8);     else espandi8(w0, pen);
                    if (RIGA_VUOTA(w1)) memset(pen + 8, 15, 8); else espandi8(w1, pen + 8);
                    uint16_t *drow = fb + (size_t)py * CPS_W + sx;
                    uint8_t  *prip = pri_bm + (size_t)py * CPS_W + sx;
                    // PIXEL_OP_REBASE_TRANSPEN_PRIORITY, trascritta:
                    //   if (srcdata != trans_pen) {
                    //       if (((1 << (PRIORITY & 0x1f)) & pmask) == 0) DEST = ...;
                    //       PRIORITY = 31;
                    //   }
                    if (!flipx) {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[tx];
                            if (p == 15) continue;
                            if (((1u << (prip[tx] & 0x1F)) & pmask) == 0) drow[tx] = prow[p];
                            prip[tx] = 31;
                        }
                    } else {
                        for (int tx = tx0; tx < tx1; tx++) {
                            const int p = pen[15 - tx];
                            if (p == 15) continue;
                            if (((1u << (prip[tx] & 0x1F)) & pmask) == 0) drow[tx] = prow[p];
                            prip[tx] = 31;
                        }
                    }
                }
            }
        }
    }
}

// ============================================================ barra volume
// ⭐ Dentro `fb`, alla risoluzione NATIVA, come ULTIMA cosa del frame: da li'
// passa per il blit INSIEME all'immagine. Disegnarla DOPO il blit la farebbe
// sparire e riapparire a ogni frame ([[feedback_vol_bar_no_flicker]]).
// ⚠️ colori BYTE-SWAPPED come tutto il resto del framebuffer di questo pannello.
// ⚠️ Il volume qui e' quello del ponte laserdisc_audio_* (1..30, 1 = massimo):
// e' lo stesso di DL/Space Ace/DL2, e a REGOLARLO e' buttons_get() del firmware.
// ⭐ STRUMENTO DI MISURA PERMANENTE, messo UNA VOLTA (non un flash diagnostico:
// e' la forma consentita dalle regole del progetto). Mostra gli fps VISTI a
// schermo e il tempo del quadro in decimi di ms, ripartito fra emulazione+render
// e blit. Serve a sapere DOVE va il tempo prima di toccare altro: senza questi
// tre numeri ogni ottimizzazione successiva sarebbe un'ipotesi.
// C1944_MISURA 0 = via tutto, nemmeno il codice.
// ⭐⭐ ACCESO il 2026-08-10 per la caccia alla MUSICA LENTA, insieme al contatore
// `AUD` qui sotto. Da rispegnere quando la questione e' chiusa.
#define C1944_MISURA 0

#if C1944_MISURA
static volatile uint32_t g_fps = 0, g_us_cpu = 0, g_us_blt = 0;

// testo 8x8 DENTRO fb (384x224). Il bianco 0xFFFF e' simmetrico, quindi va bene
// anche con la palette gia' byte-swappata.
static void fb_text(int x, int y, const char *s, uint16_t col)
{
    for (int i = 0; s[i]; i++) {
        const int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            const unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++) {
                if (!(row & (0x80 >> c))) continue;
                const int px = x + i * 8 + c, py = y + r;
                if ((unsigned)px < (unsigned)CPS_W && (unsigned)py < (unsigned)CPS_H)
                    fb[(size_t)py * CPS_W + px] = col;
            }
        }
    }
}
static void disegna_misura(void)
{
    char l[32];
    snprintf(l, sizeof l, "%lu FPS  CPU %lu  BLT %lu",
             (unsigned long)g_fps, (unsigned long)g_us_cpu, (unsigned long)g_us_blt);
    fb_text(8, 6, l, 0xFFFF);
}
#endif

// ---------------------------------------------------------------- barra volume
// ⛔ NON si disegna piu' dentro `fb`. Lo faceva, e usciva SBAGLIATA per due
// motivi indipendenti, segnalati dall'utente il 2026-08-10:
//   1. **POSIZIONE**: dentro `fb` la barra finisce nell'area del GIOCO, che sul
//      pannello parte a C1944_X_OFF = 48 px dal bordo. Su tutti gli altri giochi
//      la barra sta invece a filo del bordo ALTO, nella banda nera.
//   2. **DIMENSIONE**: il blit riduce l'immagine in orizzontale (384->240),
//      quindi 200 px nativi diventavano 125 px sullo schermo — ne' i 112 px
//      degli altri giochi, ne' la stessa cosa che si vede su Out Run (che con
//      320->240 ne dava 180).
// ✅ Ora la disegna `videoVolBarTop()` in pixel di PANNELLO — vedi
// emulation/video.h. E' condivisa da 1944, Ghouls e Out Run apposta: erano tre
// copie che potevano divergere, ed e' proprio l'uniformita' il requisito.
// ⚠️ NON DEVE LAMPEGGIARE: garantito dal fatto che si ridisegna SOLO quando
// cambia qualcosa, e che la banda nera non la tocca nessun altro (sta fuori
// dalla finestra stretta del gioco) ([[feedback_vol_bar_no_flicker]]).
static void c1944_window(int quale);     // definita sotto, insieme al blit

static void draw_vol_bar(void)
{
    static int last_fill = -2;           // -2 = mai disegnata, -1 = banda nera
    const bool on = input_is_vol_mode_active();
    const int  f  = on ? laserdisc_current_volume() : -1;
    if (f == last_fill) return;          // niente e' cambiato: nessuno SPI
    last_fill = f;
    videoVolBarTop(g_strip, on, on ? f : 30);
    c1944_window(1);                     // ridai la finestra STRETTA al gioco
}

// ============================================================ render
// ⭐⭐⭐ cps2.cpp `cps2_state::render_layers`, trascritto riga per riga.
// L'ordine dei layer NON e' fisso: sta nei bit 6-13 di layer_control e in 1944
// CAMBIA da schermata a schermata (misurato sull'harness: 078E e 06CE, che sono
// scroll2 e scroll3 scambiati fra loro). Gli sprite vanno SEMPRE per ultimi, e
// quel che li fa finire sopra o sotto un layer sono le primasks + la priority
// bitmap, non l'ordine delle chiamate.
// ⚠️ I registri CPS-B 0x28-0x2E NON servono su CPS-2: reggono il passaggio "high
// layer" (cps1_render_high_layer), che `cps2_state::render_layers` non chiama
// mai. Su CPS-2 di ogni tile si disegna solo il gruppo LAYER1, cioe' tutti i pen
// diversi dal 15.
static void render(void)
{
    costruisci_palette();
    memset(fb, 0, (size_t)CPS_W * CPS_H * 2);

    const uint16_t lc = cps_b[0x26 / 2];        // layer_control
    const uint16_t vc = cps_a[17];              // videocontrol
    unsigned l0 = (lc >> 0x06) & 0x03, l1 = (lc >> 0x08) & 0x03;
    unsigned l2 = (lc >> 0x0a) & 0x03, l3 = (lc >> 0x0c) & 0x03;
    unsigned l0pri = (pri_ctrl >> (4 * l0)) & 0x0F, l1pri = (pri_ctrl >> (4 * l1)) & 0x0F;
    unsigned l2pri = (pri_ctrl >> (4 * l2)) & 0x0F, l3pri = (pri_ctrl >> (4 * l3)) & 0x0F;

    /* take out the CPS1 sprites layer */
    if (l0 == 0) { l0 = l1; l1 = 0; l0pri = l1pri; }
    if (l1 == 0) { l1 = l2; l2 = 0; l1pri = l2pri; }
    if (l2 == 0) { l2 = l3; l3 = 0; l2pri = l3pri; }

    uint32_t primasks[8] = { 0 };
    {
        uint32_t mask0 = 0xaa, mask1 = 0xcc;
        if (l0pri > l1pri) mask0 &= ~0x88u;
        if (l0pri > l2pri) mask0 &= ~0xa0u;
        if (l1pri > l2pri) mask1 &= ~0xc0u;
        primasks[0] = 0xff;
        for (unsigned i = 1; i < 8; i++) {
            if (i <= l0pri && i <= l1pri && i <= l2pri) { primasks[i] = 0xfe; continue; }
            primasks[i] = 0;
            if (i <= l0pri) primasks[i] |= mask0;
            if (i <= l1pri) primasks[i] |= mask1;
            if (i <= l2pri) primasks[i] |= 0xf0;
        }
    }

    // ⭐⭐⭐ QUALI BIT DELLA PRIORITY BITMAP SERVONO DAVVERO.
    // Un layer scrive il suo bit su OGNI pixel opaco: a schermo pieno sono
    // ~107.000 scritture per frame, e la bitmap sta in PSRAM. Ma il bit `b` cambia
    // l'esito solo se esiste una maschera, FRA QUELLE USATE DAGLI SPRITE DI QUESTO
    // FRAME, per cui `((1<<v) & pmask)` cambia scambiando quel bit. Se non cambia
    // mai, non scriverlo e' EQUIVALENTE — non un'euristica.
    // In 1944 gli sprite usano le priorita' 0, 4 e 7 (maschere FF, F0, 00): serve
    // il solo bit di l2, cioe' un layer che disegna ~1.100 pixel invece di 86.000.
    // ⭐ Regola di [[feedback_ottimizzare_contando_accessi]]: si toglie lavoro dal
    // path caldo, non si sposta memoria.
    const int usa_pri = (pri_ctrl != 0);
    unsigned bit_utili = 0;
    if (usa_pri) {
        const uint8_t usate = sprite_pri_usate();
        for (unsigned p = 0; p < 8; p++) {
            if (!(usate & (1u << p))) continue;
            const uint32_t pm = primasks[p];
            for (unsigned b = 1; b <= 4; b <<= 1)
                for (unsigned v = 0; v < 8; v++)
                    if ((((1u << v) & pm) != 0) != (((1u << (v ^ b)) & pm) != 0))
                        bit_utili |= b;
        }
    }
    memset(pri_bm, 0, (size_t)CPS_W * CPS_H);   // screen.priority().fill(0)

    // cps1_get_video_base: abilitazione dei tre tilemap. Maschere di
    // CPS_B_21_DEF {0x02,0x04,0x08}; scroll2/scroll3 vogliono anche il bit 2/3 di
    // videocontrol.
    const unsigned ORD[3] = { l0, l1, l2 };
    // ⚠️ NON chiamarlo BIT: in ESP-IDF `BIT(n)` e' una macro (esp_bit_defs.h) e un
    // array con quel nome non compila.
    const unsigned PRIBIT[3] = { 1, 2, 4 };
    for (int k = 0; k < 3; k++) {
        const unsigned L = ORD[k];              // 1=scroll1 2=scroll2 3=scroll3
        if (L == 0) continue;                   // il posto degli sprite CPS1
        int acceso;
        if (L == 1)      acceso = (lc & 0x02) != 0;
        else if (L == 2) acceso = (lc & 0x04) && ((vc >> 2) & 1);
        else             acceso = (lc & 0x08) && ((vc >> 3) & 1);
        if (acceso) disegna_layer((int)L - 1, (PRIBIT[k] & bit_utili) ? PRIBIT[k] : 0);
    }
    disegna_sprite(primasks);                   // sempre per ULTIMI
    // ⛔ la barra del volume NON sta piu' qui: vedi draw_vol_bar(), che la
    // disegna nella banda nera in pixel di pannello.
#if C1944_MISURA
    disegna_misura();     // ULTIMA cosa dentro fb: passa dal blit come il gioco
#endif
}

// ============================================================ blit sul pannello
// ⛔ QUI NON SI COPIA DA CABINATONE: la' c'erano un pannello DSI 1024x600, il PPA
// hardware e lcd_draw_bitmap, che qui NON esistono. Il modello e' ld_blit() di
// spaceace.cpp: strisce da 8 righe, memset + copia + video.write().
//
// ⭐⭐ GEOMETRIA — LA STESSA DI BOMB JACK. Il pannello e' montato in PORTRAIT ma
// si pilota TRASPOSTO (MADCTL 0x60): una "riga" scritta e' larga TFT_FB_W=320 e
// ce ne sono LD_FB_H=240. In coordinate UTENTE (portrait 240x320, le stesse del
// boot_menu, di ld_px_vis e di c1944_line qui sotto):
//     x_l = y_p        y_l = 239 - x_p
// cioe' l'asse da 320 e' il VERTICALE visto dall'utente e quello da 240
// l'ORIZZONTALE. Lo conferma ld_blit() di spaceace.cpp:571, dove il video largo
// 176 sta nei 320 con LD_X_OFF=72 e quelle sono le bande "alta" e "bassa".
//
// ⭐ LA FORMULA VIENE DA machines/bombjack/bombjack.cpp:564-575 (render_row):
//       dst = frame_buffer + fb_y_strip * BJ_NH;      // riga della striscia
//       col = (BJ_NW - 1) - fb_y;                     // colonna nativa
//       for (y = 0; y < BJ_NH; y++) dst[y] = src[y * BJ_NW];
//   cioe', in coordinate di pannello:  x_l = game_y     y_l = MAX - game_x
//   (il gioco NATIVO entra trasposto: la sua ALTEZZA va sull'asse da 320, la sua
//   LARGHEZZA sull'asse da 240, e l'asse orizzontale e' ROVESCIATO).
// Qui si applica IDENTICA, con l'unica differenza che 1944 e' ORIZZONTALE:
//       x_l = C1944_X_OFF + game_y      (224 righe di gioco nei 320 verticali)
//       y_l = 239 - x_p,  x_p = game_x ridotto da 384 a 240
// Risultato: il gioco esce DIRITTO come Bomb Jack — stesso modo di tenere il
// cabinato — con le BANDE NERE sopra e sotto (48 px per parte).
//
// ⚠️ ASPECT SACRIFICATO DI PROPOSITO. 384 -> 240 e' un fattore 0,625; tenendo le
// 224 righe intere l'immagine risulta SCHIACCIATA in orizzontale. La scelta
// alternativa (224 * 0,625 = 140 righe, bande da 90 px) rispetterebbe l'aspect ma
// BUTTEREBBE 84 righe su 224: il punteggio e la scritta di stato sono alti 8 px e
// diventerebbero illeggibili. In questo firmware NESSUN gioco ricampiona in
// verticale (Bubble Bobble fa 256->240 e tiene le righe, Green Beret e' 240
// nativo): si segue la stessa regola.
//
// ⚠️ Riduzione 384 -> 240: si tengono 5 pixel ogni 8 (48 gruppi tondi). Gli
// indici tenuti dentro il gruppo sono quelli di round(k*1,6) = 0,2,3,5,6, cioe'
// gli scarti sono SPARSI e non tre di fila. Nessuna divisione resta nel ciclo
// caldo: le 8 colonne sorgente di una striscia si calcolano UNA volta per
// striscia (8 divisioni x 30 strisce = 240 per quadro) e il ciclo interno fa solo
// letture indicizzate.
//
// ⭐ ORDINE DEI CICLI = ACCESSI ALLA PSRAM ([[feedback_ottimizzare_contando_accessi]]).
// Trasporre vuol dire che una RIGA del pannello e' una COLONNA di `fb`: presa
// riga per riga sarebbero 240x224 = 53.760 letture a passo 768 byte, cioe' una
// cache miss ciascuna. Qui il ciclo esterno e' la riga di `fb` (che si percorre
// in ORDINE) e dentro si riempiono TUTTE E OTTO le righe della striscia: le 8
// colonne servite stanno in ~13 px = 26 byte, cioe' una o due linee di cache per
// riga di fb. Le scritture, a passo 320, cadono in g_strip che e' RAM INTERNA.
static void c1944_blit(void)
{
    // indici tenuti dentro ogni gruppo di 8 pixel sorgente (round(k*1,6))
    static const uint8_t KEEP[5] = { 0, 2, 3, 5, 6 };

    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        // ⭐ NIENTE memset: con la finestra STRETTA (224 colonne) il ciclo qui
        // sotto copre TUTTE le 224*8 posizioni della striscia, quindi non resta
        // un solo pixel vecchio. Le bande nere stanno FUORI dalla finestra e le
        // ha gia' dipinte c1944_bande_nere() una volta sola.

        // le 8 colonne di `fb` servite da questa striscia (una per riga scritta)
        int gx8[8];
        for (int i = 0; i < 8; i++) {
            const int x_p = (LD_FB_H - 1) - (sy + i);   // 0..239 = colonna UTENTE
            gx8[i] = (x_p / 5) * 8 + KEEP[x_p % 5];     // 0..383 = colonna di fb
        }

        const uint16_t *src = fb;                       // riga 0 del gioco
        uint16_t *dst = g_strip;   // finestra STRETTA: la striscia parte dal gioco
        for (int gy = 0; gy < CPS_H; gy++) {
            dst[0]                 = src[gx8[0]];
            dst[1 * CPS_H]      = src[gx8[1]];
            dst[2 * CPS_H]      = src[gx8[2]];
            dst[3 * CPS_H]      = src[gx8[3]];
            dst[4 * CPS_H]      = src[gx8[4]];
            dst[5 * CPS_H]      = src[gx8[5]];
            dst[6 * CPS_H]      = src[gx8[6]];
            dst[7 * CPS_H]      = src[gx8[7]];
            src += CPS_W;                               // riga successiva di fb
            dst += 1;                                   // colonna successiva
        }
        video.write(g_strip, CPS_H * 8);   // finestra STRETTA: 224, non 320
    }
    // ⚠️ video.write() e' ASINCRONA e lascia una transazione SPI in volo. Il frame
    // successivo legge la cache GFX dalla SD, e video.h chiede esplicitamente di
    // non toccare la SD con la DMA in corso: si drena qui, UNA volta per frame.
    video.waitDMA();
}

// ============================================================ input
// Attivi BASSI. IN2 bit 8 = START1, bit 12 = COIN1.
// ⛔⭐ L'ORDINE DELLE DIREZIONI IN IN0 **NON** e' quello del NeoGeo. Letto da
// cps2.cpp PORT_START("IN0"):
//     0x0001 = RIGHT   0x0002 = LEFT   0x0004 = DOWN   0x0008 = UP
//     0x0010 = BUTTON1 (spara)   0x0020 = BUTTON2 (bomba)
// Dare per scontato "UP DOWN LEFT RIGHT" (che e' l'ordine del NeoGeo, "DCBArldu")
// faceva andare destra in SU e sinistra in GIU'.
static void leggi_input(void)
{
    const uint8_t b = g_input->buttons_get();
    uint16_t p1 = 0xFFFF, st = 0xFFFF;
    if (b & BUTTON_RIGHT) p1 &= ~0x0001;
    if (b & BUTTON_LEFT)  p1 &= ~0x0002;
    if (b & BUTTON_DOWN)  p1 &= ~0x0004;
    if (b & BUTTON_UP)    p1 &= ~0x0008;
    if (b & BUTTON_FIRE)  p1 &= ~0x0010;      // spara
    if (b & BUTTON_EXTRA) p1 &= ~0x0020;      // bomba (2o pulsante)
    if (b & BUTTON_START) st &= ~0x0100;      // START1
    if (b & BUTTON_COIN)  st &= ~0x1000;      // COIN1
    in0 = p1; in2 = st;
}

// ============================================================ un fotogramma
static void run_frame(void)
{
    // ⭐ DUE fette invece di 262: non c'e' nessun interrupt raster, l'unico evento
    // a meta' frame e' il vblank alla riga 240. Ogni ingresso in m68k_execute
    // costa un prologo con un setjmp() (24 registri salvati su RISC-V con FPU) e
    // un check degli interrupt: 260 volte per frame a vuoto. I cicli totali sono
    // IDENTICI: 240*755 + 22*755 = 262*755 = 197.810.
    m68k_execute(CPS_CIC * CPS_VBL_RIGA);
    // ⭐ l'istantanea PRIMA dell'interrupt (screen_vblank -> objram_latch)
    // ⚠️ `m_pri_ctrl = m_output[CPS2_OBJ_PRI]` sta nella STESSA funzione: anche la
    // priorita' e' ritardata di un frame, non si legge al volo.
    pri_ctrl = out_reg[2];
    memcpy(obj_latch, objram[obj_bank & 1], OBJ_SZ);
    m68k_set_irq(2);                                      // IPL1 = vblank
    m68k_execute(CPS_CIC * (CPS_RIGHE - CPS_VBL_RIGA));   // righe 240..261
    // ✅ Niente trucchi sul byte 0x77: gira il Z80 VERO (task audio, core 0) e
    // quella firma la scrive lui, appena il 68000 lo toglie dal reset col bit 3
    // di 0x804040.
}

// ---------------------------------------------------------------- schermo
// 2 = tutta la finestra del player (320x240): pagine di stato e bande nere.
// 1 = SOLO le 224 colonne che portano gioco, saltando le bande.
// 0 = si restituisce la finestra al menu.
static void c1944_window(int quale) {
    video.waitDMA();
    if (quale == 2)      video.setAddrWindow(TFT_HW_X, 0, TFT_FB_W, LD_FB_H);
    // ⭐⭐ SI SPEDISCE MENO. MISURA sul device: BLT 17,1 ms per quadro, e
    // 320*240*2 = 153.600 byte a 80 MHz sono ~15,4 ms teorici: il blit e' AL
    // LIMITE DEL BUS, nel codice non c'e' piu' niente da spremere.
    // Ma di quei 320 pixel solo **224 portano gioco**: 96 colonne sono banda
    // nera, sempre IDENTICA, e la si rispediva ad OGNI quadro =
    // **46.080 byte di puro nero per quadro**, il 30% del traffico SPI.
    // Le bande si dipingono UNA VOLTA all'avvio e non si toccano piu'.
    else if (quale == 1) video.setAddrWindow(TFT_HW_X + C1944_X_OFF, 0, CPS_H, LD_FB_H);
    else                 video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// Dipinge di nero TUTTA la finestra del player. Si chiama una volta sola, prima
// di stringere la finestra sul gioco: da li' in poi le bande restano com'erano.
static void c1944_bande_nere(void) {
    c1944_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    video.waitDMA();
}

// Testo SCALATO nella strip ruotata: stessa geometria di ld_line_strip di
// spaceace.cpp e di dl2_line_strip (x_l = y_p + r*scale, y_l = 239 - x_p).
static void c1944_line(int sy, const char *s, int y_p, int scale, uint16_t col) {
    int slen = (int)strlen(s);
    int x0 = (240 - slen * 8 * scale) / 2; if (x0 < 2) x0 = 2;
    for (int i = 0; i < slen; i++) {
        int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++) {
                if (!(row & (0x80 >> c))) continue;
                for (int dr = 0; dr < scale; dr++) {
                    int x_l = y_p + r * scale + dr;
                    if (x_l < 0 || x_l >= TFT_FB_W) continue;
                    for (int dc = 0; dc < scale; dc++) {
                        int y_l = 239 - (x0 + (i * 8 + c) * scale + dc), y_in = y_l - sy;
                        if (y_in < 0 || y_in >= 8) continue;
                        g_strip[y_in * TFT_FB_W + x_l] = col;
                    }
                }
            }
        }
    }
}

static const uint16_t C_YELLOW = 0xE0FF;   // byte-swapped, come DL/Space Ace
static const uint16_t C_CYAN   = 0xFF07;
static const uint16_t C_GREEN  = 0xE007;
static const uint16_t C_GREY   = 0xEF7B;
static const uint16_t C_RED    = 0x00F8;

// pagina a 5 righe sotto il titolo
static void c1944_page(const char *l[5], const uint16_t c[5])
{
    static const int Y[5] = { 150, 172, 194, 216, 250 };
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        c1944_line(sy, "1944", 96, 3, C_YELLOW);
        for (int k = 0; k < 5; k++)
            if (l[k] && l[k][0]) c1944_line(sy, l[k], Y[k], 1, c[k]);
        video.write(g_strip, TFT_FB_W * 8);
    }
    // ⚠️ subito dopo una pagina di stato si legge quasi sempre dalla SD (le ROM,
    // la mappa della cache GFX): video.write lascia una transazione SPI in volo e
    // video.h chiede di drenarla prima di toccare la SD.
    video.waitDMA();
}

static void c1944_msg(const char *a, const char *b, uint16_t col)
{
    const char *l[5] = { "THE LOOP MASTER", a, b, "", "START 3s ESCI" };
    const uint16_t c[5] = { C_YELLOW, col, C_GREY, C_GREY, C_GREY };
    c1944_page(l, c);
}

// Schermata di CARICAMENTO: solo il nome del gioco e una riga. Niente
// avanzamento file per file, niente suggerimenti — erano scritte di servizio,
// utili mentre si scriveva il porting e fuori posto adesso.
// ⚠️ Le schermate di ERRORE restano com'erano (c1944_msg in rosso): non sono
// debug, sono l'unico modo di sapere cosa manca sulla SD.
static void c1944_caricamento(void)
{
    const char *l[5] = { "THE LOOP MASTER", "", "CARICAMENTO DA SD", "", "" };
    const uint16_t c[5] = { C_YELLOW, C_GREY, C_CYAN, C_GREY, C_GREY };
    c1944_page(l, c);
}

// ---------------------------------------------------------------- verifica pack
// Si controlla la TAGLIA, non il contenuto: leggere 31 MB per un CRC costerebbe
// decine di secondi ad ogni avvio. Il CRC lo ha gia' fatto il converter sul PC.
static bool c1944_probe(void) {
    g_ok = 0; g_byte = 0; g_primo_ko[0] = 0;
    for (int i = 0; i < C1944_NFILE; i++) {
        char p[64];
        snprintf(p, sizeof p, C1944_DIR "/%s", C1944_FILES[i].nome);
        FILE *f = fopen(p, "rb");
        if (!f) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "MANCA %s", C1944_FILES[i].nome);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if ((uint32_t)n != C1944_FILES[i].byte) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "TAGLIA %s", C1944_FILES[i].nome);
            continue;
        }
        g_ok++;
        g_byte += C1944_FILES[i].byte;
    }
    return g_ok == C1944_NFILE;
}

// carica un file intero dalla SD in un buffer gia' allocato
static bool c1944_load(const char *nome, uint8_t *dst, uint32_t n)
{
    char p[64];
    snprintf(p, sizeof p, C1944_DIR "/%s", nome);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    // a blocchi: una fread da 1,5 MB su FATFS non e' una buona idea, e cosi' si
    // puo' anche cedere la CPU fra un pezzo e l'altro
    uint32_t fatti = 0;
    unsigned pezzi = 0;
    while (fatti < n) {
        uint32_t q = n - fatti; if (q > 32768) q = 32768;
        if (fread(dst + fatti, 1, q, f) != q) { fclose(f); return false; }
        fatti += q;
        // ⛔⛔ RESPIRO IN CARICAMENTO. Qui si leggono **11,3 MB** (p.bin, pdec.bin,
        // m.bin e gli 8 MB di q.bin) e poco piu' avanti si azzerano ~26 MB di
        // PSRAM fra allocazioni e cache grafica: e' un tratto lungo secondi in cui
        // questo task non cede mai esplicitamente la CPU. Se l'idle del core non
        // gira per 5 s scatta il task watchdog e **la scheda si riavvia mentre
        // carica** — che e' esattamente il sintomo riferito («molte volte quando
        // lo avvii riavvia il dispositivo»), ed e' intermittente perche' dipende
        // da quanto e' veloce la SD in quel momento.
        // ⚠️ Un tick ogni 8 pezzi = 1 ms ogni 256 KB: sul totale sono frazioni di
        // secondo, contro un riavvio.
        if ((++pezzi & 7) == 0) vTaskDelay(1);
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- allocazioni
// Tutto in PSRAM: sono megabyte, e la RAM interna qui e' la risorsa scarsa
// ([[feedback_audio_muto_e_ram_interna]]). Le tabelle di Musashi comprese —
// il core e' gia' modificato perche' le allochi il chiamante (m68k_set_tables).
// ⛔⛔ RISERVA DI RAM INTERNA — ERA 96 KB, ED ERA IL VALORE SBAGLIATO PER QUESTA
// SCHEDA (corretto il 2026-08-10, sintomo: **musica lentissima** e ogni tanto la
// scheda che si riavvia).
// I 96 KB venivano da CABINATONE, dove servono a lasciare spazio all'I2S e ai
// suoi descrittori DMA, che LI' vengono allocati quando parte il gioco. Su
// SPINNERINO **l'I2S e' aperto al BOOT**: `audio.init()` sta in setup() del .ino
// e fa i2s_new_channel + i2s_channel_init_std_mode + i2s_channel_enable, DMA
// compresi; `laserdisc_audio_begin()` alza solo un flag di bypass e non alloca
// niente. Quindi qui non c'e' nessuna allocazione tardiva da proteggere — e'
// esattamente la nota gia' scritta in ghouls.cpp, che infatti usa 48 KB.
// ⭐ COSA COSTAVA: con 96 KB la soglia di c1944_calloc_caldo diventa
// `n + 98.304`, e la RAM interna VERAMENTE libera a gioco avviato (dopo BT, SD,
// stack dei task e I2S) non ci arriva mai. Finivano quindi in PSRAM i due buffer
// piu' caldi del CORE 0: lo **stato del chip QSound** (~6 KB, ripercorso INTERO
// 32.000 volte al secondo: 16 voci + 3 ADPCM + eco da 1024 word + 4 FIR da 95
// tap) e la **RAM privata del Z80 sonoro** (4 KB: stack e variabili di una CPU a
// 8 MHz emulata). Da li' il task audio non ce la fa a produrre 32.000
// campioni/s, `laserdisc_audio_write()` non blocca piu' e il Z80 sonoro avanza
// alla velocita' del task invece che a quella dell'I2S: **la musica va lenta**.
// (E il task, non bloccandosi piu', gira al 100% a priorita' 6 sul core 0 e
// affama l'idle -> watchdog -> riavvio. Vedi anche il respiro in c1944_audio_task.)
#define C1944_RISERVA_INT (48 * 1024)

// ⭐⭐ ALLINEATO A 128 ANCHE IN PSRAM — da CABINATONE\cps1944.cpp:1032-1039, dove
// `fb` si alloca SEMPRE con `ALIGN_UP(..., 128)` + `heap_caps_aligned_alloc(128,…)`,
// tanto in interna quanto in PSRAM (e lo stesso fa per `gamebuf`, riga 1152).
// Qui invece i due buffer piu' caldi nascevano da una `heap_caps_calloc` semplice e
// restavano allineati "a caso" se non riuscivano a salire in interna. 128 e' la
// LINEA DI CACHE: una riga di `fb` e' 384*2 = 768 byte = 6 linee esatte e una di
// `pri_bm` e' 384 = 3 linee esatte, quindi con la BASE allineata lo e' ogni riga;
// con la base disallineata OGNI riga ne sporca una in piu' in testa e una in coda.
// Sono i due buffer con ~400.000 accessi per frame (numeri MISURATI sull'harness di
// CABINATONE su 600 frame), cioe' quelli dove lo spreco si moltiplica di piu'.
static void *c1944_psram_allineata(size_t n)
{
    const size_t na = (n + 127) & ~(size_t)127;          // ALIGN_UP(n, 128)
    void *p = heap_caps_aligned_alloc(128, na, MALLOC_CAP_SPIRAM);
    if (p) memset(p, 0, na);
    return p;                                            // NULL -> ci pensa la calloc
}

// ⭐ Buffer PICCOLO e CALDISSIMO: interna se c'e' posto oltre la riserva, PSRAM
// altrimenti. E' la coppia `if (!x) x = calloc(..., SPIRAM)` di CABINATONE
// (cps1944.cpp:1112-1115 per z80ram e per lo stato del QSound), col controllo
// della riserva in piu' perche' qui l'I2S non e' ancora partito
// ([[feedback_audio_muto_e_ram_interna]]): meglio restare in PSRAM che muti.
static void *c1944_calloc_caldo(size_t n)
{
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + C1944_RISERVA_INT) {
        void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
        if (p) return p;
    }
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

static bool c1944_alloc(const char **perche)
{
    // ⛔⛔⛔ PRIMA DI TUTTO IL RESTO, E SENZA RISERVA: i due buffer del
    // sottosistema SONORO. Corretto il 2026-08-10 dopo il referto «la musica
    // dell'attract e' ancora lenta» — abbassare la riserva da 96 a 48 KB non era
    // bastato, perche' la RAM interna VERAMENTE libera a gioco avviato (dopo BT,
    // SD, stack dei task e I2S) non arriva nemmeno a 48 KB + il buffer.
    // ⭐ IL RAGIONAMENTO CHE MANCAVA: la riserva esiste per impedire che i buffer
    // VIDEO (fb, 172 KB) affamino l'audio ([[feedback_audio_muto_e_ram_interna]]).
    // Applicarla ai buffer DELL'AUDIO STESSO e' rovesciato: si tiene da parte
    // memoria "per l'audio" negandola proprio all'audio. Questi due sono 10 KB in
    // tutto e sono i piu' battuti del binario:
    //   qs      ~6 KB  ripercorso INTERO 24.038 volte al secondo da update_sample()
    //                  (16 voci + 3 ADPCM + eco da 1024 word + 4 FIR da 95 tap)
    //   z80ram   4 KB  stack e variabili di un Z80 a 8 MHz emulato
    // In PSRAM il task audio non produce 32.000 campioni/s, la write non blocca
    // piu' e il Z80 sonoro avanza al ritmo del task: **musica lenta**
    // ([[feedback_task_audio_indietro_watchdog]]).
    // La riserva resta dov'e' giusta: in c1944_prova_interna(), per fb e pri_bm.
    if (!qs)     qs     = (struct qsound_chip *)heap_caps_calloc(1, qs_hle_size(), MALLOC_CAP_INTERNAL);
    if (!qs)     qs     = (struct qsound_chip *)heap_caps_calloc(1, qs_hle_size(), MALLOC_CAP_SPIRAM);
    if (!z80ram) z80ram = (uint8_t *)heap_caps_calloc(1, 0x1000, MALLOC_CAP_INTERNAL);

    // ⭐ ORDINE DI PRIORITA' PER LA RAM INTERNA = quante volte al frame si tocca.
    // Qui l'interna e' ~300 KB e ci vive anche il resto del firmware, quindi si
    // prende SOLO cio' che e' piccolo e caldissimo:
    //   pal_lut  6 KB   ~400.000 letture per frame   -> interna se c'e' posto
    //   fb     172 KB   ~400.000 scritture per frame -> vedi c1944_prova_interna()
    // ⛔ `fb` e `pri_bm` NON si prendono in interna QUI: su CABINATONE si fa a
    // questo punto (la' il binario e' di UN gioco solo), mentre qui l'I2S non e'
    // ancora partito e 172 KB tolti adesso sono esattamente il modo di ritrovarsi
    // col video a posto e l'AUDIO MUTO ([[feedback_audio_muto_e_ram_interna]]).
    // Si tenta DOPO l'avvio del task audio, con c1944_prova_interna().
    if (!pal_lut) {
        pal_lut = (uint16_t *)heap_caps_calloc(1, 0xC00 * 2, MALLOC_CAP_INTERNAL);
        // se non c'e', il ciclo qui sotto la prende in PSRAM come tutto il resto
    }
    // ⭐⭐ i due buffer da ~400.000 accessi per frame: ALLINEATI A 128 gia' qui,
    // com'e' su CABINATONE, cosi' l'allineamento non dipende dal fatto che poi
    // riescano a salire in interna (c1944_prova_interna). Se l'aligned_alloc non
    // riesce restano nulli e li prende il ciclo qui sotto, esattamente come prima.
    if (!fb)     fb     = (uint16_t *)c1944_psram_allineata((size_t)CPS_W * CPS_H * 2);
    if (!pri_bm) pri_bm = (uint8_t  *)c1944_psram_allineata((size_t)CPS_W * CPS_H);
    // ⭐ RAM PRIVATA DEL Z80 SONORO (0xF000-0xFFFF): 4 KB dove stanno stack e
    // variabili di un Z80 che gira a 8 MHz emulati, cioe' il buffer piu' battuto del
    // core 0. Su CABINATONE (cps1944.cpp:1112) e' `MALLOC_CAP_INTERNAL` con fallback
    // in PSRAM. ⛔ Gia' tentato in cima a questa funzione, senza riserva: se non
    // c'e' entrato lo prende il ciclo PSRAM qui sotto.
    struct { void **p; size_t n; const char *nome; } B[] = {
        { (void **)&qs,        qs_hle_size(),  "QSOUND"    },
        { (void **)&prom,      PROM_SZ,        "p.bin"     },
        { (void **)&pdec,      PROM_SZ,        "pdec.bin"  },
        { (void **)&ram,       RAM_SZ,         "RAM"       },
        { (void **)&gfxram,    GFXRAM_SZ,      "GFXRAM"    },
        { (void **)&qram,      QRAM_SZ,        "QRAM"      },
        { (void **)&xram,      XRAM_SZ,        "XRAM"      },
        { (void **)&objram[0], OBJ_SZ,         "OBJ0"      },
        { (void **)&objram[1], OBJ_SZ,         "OBJ1"      },
        { (void **)&obj_latch, OBJ_SZ,         "OBJ LATCH" },
        { &m68k_jump,          M68K_JUMP_SIZE, "TAB SALTI" },
        { &m68k_cyc,           M68K_CYC_SIZE,  "TAB CICLI" },
        { (void **)&z80rom,    Z80ROM_SZ,      "m.bin"     },
        { (void **)&z80ram,    0x1000,         "Z80 RAM"   },
        { (void **)&qsrom,     QSROM_SZ,       "q.bin"     },
        { (void **)&fb,        CPS_W * CPS_H * 2, "FB"     },
        { (void **)&pal_lut,   0xC00 * 2,      "PALETTE"   },
        { (void **)&pri_bm,    CPS_W * CPS_H,  "PRIORITA'" },
    };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++) {
        if (*B[i].p) continue;
        *B[i].p = heap_caps_calloc(1, B[i].n, MALLOC_CAP_SPIRAM);
        if (!*B[i].p) { *perche = B[i].nome; return false; }
    }
    return true;
}

// ⭐⭐ RAM INTERNA PER I DUE BUFFER PIU' CALDI — dal sorgente CABINATONE
// (cps1944.cpp: `fb` e' la PRIMA cosa che prova la RAM interna, `pri_bm` la
// seconda, ciascuna con una riserva di 96 KB). I numeri sono quelli MISURATI la'
// sull'harness, su 600 frame:
//   fb       ~400.000 SCRITTURE per frame  (e da qui rilegge anche il blit)
//   pri_bm    1 scrittura per pixel di layer, 1 lettura + 1 scrittura per pixel
//             di sprite — "si tocca quasi quanto fb"
//   work RAM ~300 scritture · objram ~2.000 letture · gfxram ~4.000 -> PSRAM
// ⛔ QUI SI FA DOPO CHE L'AUDIO E' PARTITO, non prima come su CABINATONE: la' il
// binario e' di un gioco solo. La regola pagata su questo progetto e'
// «inizializzare l'audio PRIMA del video, e sommare le MALLOC_CAP_INTERNAL prima
// di flashare» ([[feedback_audio_muto_e_ram_interna]]): a I2S gia' avviato la
// heap_caps_get_free_size dice la VERITA', e se non c'e' posto si resta in PSRAM,
// cioe' come prima e non peggio.
// (C1944_RISERVA_INT = 96 KB, definita sopra a c1944_alloc perche' serve anche li'.)

static void c1944_prova_interna(void **p, size_t n)
{
    if (!*p) return;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) < n + C1944_RISERVA_INT) return;
    // ⭐ allineato a 128 come su CABINATONE (ALIGN_UP + aligned_alloc): e' la
    // linea di cache, e un buffer disallineato la spreca su ogni riga.
    const size_t na = (n + 127) & ~(size_t)127;
    void *nuovo = heap_caps_aligned_alloc(128, na, MALLOC_CAP_INTERNAL);
    if (!nuovo) return;                        // interna frammentata: resta dov'e'
    memset(nuovo, 0, na);
    heap_caps_free(*p);
    *p = nuovo;
}

static void c1944_free(void)
{
    // ⛔ PRIMA di liberare `pdec`: la finestra diretta dello spazio opcode punta
    // dentro quel buffer. Lasciarla accesa dopo la free significherebbe che il
    // prossimo gioco a 68000 del binario preleva le istruzioni da memoria
    // liberata. Spegnerla e' una riga; non spegnerla e' un crash silenzioso.
    m68k_set_opcode_window(nullptr, 0);
    // ⭐ e con lei l'idle-skip: e' acceso solo per questo gioco, e il prossimo
    // gioco a 68000 del binario dev'essere identico a com'era prima.
    m68k_set_idle_skip(1, 0);            // lo > hi = spento
    void *B[] = { prom, pdec, ram, gfxram, qram, xram, objram[0], objram[1],
                  obj_latch, m68k_jump, m68k_cyc, z80rom, z80ram, qsrom, qs,
                  aud_out, fb, pal_lut, pri_bm, espandi_tab, conv_tab };
    gfx_cache_free(&gfx);
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++)
        if (B[i]) heap_caps_free(B[i]);
    fb = pal_lut = nullptr; pri_bm = nullptr;
    espandi_tab = nullptr; espandi_pronta = false;
    conv_tab = nullptr;    conv_pronta = false;
    prom = pdec = ram = gfxram = qram = xram = nullptr;
    objram[0] = objram[1] = obj_latch = nullptr;
    m68k_jump = m68k_cyc = nullptr;
    z80rom = z80ram = qsrom = nullptr;
    qs = nullptr; aud_out = nullptr;
}

// ============================================================ task audio (core0)
// ⭐ E' l'AUDIO a dare il tempo al Z80 sonoro, non il video: la write sull'I2S
// blocca finche' non c'e' spazio, quindi consegnando 32000 campioni al secondo
// il task si autoregola. (Lezione di Hyper Sports: se lo ritma il video, la
// musica rallenta quando calano gli fps.)
// ⭐ GUADAGNO + SOFT LIMITER (vedi il commento nel mix, in fondo al task).
// 5/2 = +8 dB, che e' esattamente quanto mancava: il picco misurato del QSound e'
// 12771 e i giochi normali arrivano a fondo scala. La soglia del limiter e' la
// stessa di laserdisc.cpp (LD_LIM_T), dove e' stata tarata sull'orecchio.
#define C1944_BOOST_NUM 5
#define C1944_BOOST_DEN 2
#define C1944_LIM_T     24000

static void c1944_audio_task(void *arg)
{
    (void)arg;
    laserdisc_audio_begin();
    int al_prossimo_irq = SND_IRQ_OGNI;
    int z80_sforo = 0;                    // ICount residuo (<=0) della fetta prima
    uint32_t respiro = 0;                 // contatore per lo yield anti-watchdog
    while (!g_stop) {
        // il 68000 (CPU1) puo' aver tolto il Z80 dal reset: qui lo si esegue
        if (z80_reset_req) {
            z80_reset_req = 0;
            CpsResetZ80(&sndcpu);
            sndcpu.IRequest = INT_NONE; sndcpu.IAutoReset = 1; sndcpu.TrapBadOps = 0;
        }
        if (!z80_in_reset) {
            // ⭐⭐⭐ UN CAMPIONE PER VOLTA: Z80 e QSound avanzano INSIEME.
            // Prima si eseguiva l'INTERO blocco di Z80 (64 campioni = 16.000
            // cicli) e solo dopo si faceva avanzare il DSP. Ma il driver di
            // 1944 aspetta il DSP con un busy-wait — `LD A,($D007) / RLCA /
            // JR NC,-6`, presente in 26 punti di m.bin — e con il DSP fermo
            // quel flag non poteva tornare alto prima del blocco successivo:
            // dopo OGNI comando il Z80 girava a vuoto per un blocco intero.
            // ⭐ Non e' un'invenzione, e' cio' che fanno TUTTE le fonti:
            //   MAME `qsoundhle.cpp`: `qsound_r() { m_stream->update();
            //        return m_ready_flag; }`, e lo stesso dentro `qsound_w`;
            //   MAME `qsound.cpp` (LLE): `machine().scheduler().synchronize()`;
            //   FBNeo `qs_c.cpp` — il core DA CUI VIENE il nostro qsound_hle.c:
            //        `QscRead()  { QscSyncQsnd(); return chip.ready_flag; }`
            //        `QscWrite() { ... QscSyncQsnd(); ... }`
            //   Il porting aveva perso quel sync, e nient'altro.
            // ⭐ MISURATO offline in `_verify_gfx\cps2_snd` su 45 s di attract:
            //   comandi QSound consegnati 350/s -> 606/s; letture di D007
            //   3.422.972 -> 33.655 (cento volte meno tempo buttato ad
            //   aspettare); picco 19.734 -> 28.814. E contro il rip VGM di
            //   VGMRips (timing di MAME) reso con QUESTO stesso chip, il brano
            //   passa da irriconoscibile (correlazione 0,31) a **velocita'
            //   1.000, correlazione 0,92**. Provate anche fette da 8 e da 4
            //   campioni: i comandi passano tutti gia' con 8, ma la fedelta'
            //   sale ancora (0,76 -> 0,86 -> 0,92), perche' conta anche
            //   l'ISTANTE in cui il comando arriva. Il prezzo e' stato MISURATO
            //   (`bench_render.py`): il render passa da 243 a 268 ms ogni 30 s
            //   di audio, +10% su una voce che e' una frazione del task; il
            //   lavoro vero (gli `update_sample`) e' identico, cambia solo il
            //   numero di chiamate. `qs_hle_render` ora tiene il passo del
            //   ricampionamento in una static invece di rifare una divisione a
            //   64 bit 32.000 volte al secondo.
            for (int i = 0; i < AUD_CHUNK; i++) {
                // `CpsExecZ80` torna l'ICount residuo (<= 0): su fette da 250
                // cicli lo sforo dell'ultima istruzione pesa, e va sottratto
                // alla fetta dopo, altrimenti il Z80 guadagna velocita'.
                z80_sforo = CpsExecZ80(&sndcpu, Z80_CYC_PER_SAMPLE + z80_sforo);
                if (--al_prossimo_irq <= 0) {
                    al_prossimo_irq = SND_IRQ_OGNI;
                    // ⛔⛔⛔ QUI STAVA LA MUSICA LENTA (trovato il 2026-08-10, dopo
                    // che i referti avevano ESCLUSO la memoria: 156 KB interni
                    // liberi e stato del QSound gia' in interna).
                    // C'era `if (sndcpu.IFF & IFF_1) IntZ80(...)`, cioe' l'IRQ si
                    // consegnava SOLO se in quell'istante il Z80 aveva gli
                    // interrupt abilitati — **altrimenti si buttava via**. Ma nel
                    // driver il tempo lo scandisce proprio questo IRQ a 250 Hz, e
                    // il Z80 passa una fetta enorme del suo tempo con IFF1 basso
                    // (dentro il suo stesso gestore, che gira mascherato fino al
                    // RETI). Ogni tick perso e' un tick di sequencer non fatto:
                    // **la musica rallenta**, e nessuna ottimizzazione di memoria
                    // puo' cambiarlo.
                    // ⭐ Nel driver e' `set_periodic_int(irq0_line_hold, 250 Hz)`:
                    // `_hold` vuol dire linea TENUTA ALTA finche' non viene
                    // servita. Il core di Marat lo sa fare: si alza `IRequest` e
                    // ci pensa lui a servirla subito dopo la prossima EI
                    // (Z80cps.c:547), azzerandola da solo grazie a IAutoReset.
                    // ⚠️ Stesso errore gia' pagato su altre due CPU di questo
                    // progetto: [[fake6502_irq_ignores_iflag]] («level held + ack»)
                    // e [[fm_c_ym2203_irqmask_mai_impostata]]. Un interrupt di
                    // LIVELLO non si scarta: si tiene alzato.
                    // ⚠️ L'idiom veniva da ghouls.cpp, dove pero' e' innocuo: la'
                    // l'IRQ nasce dal FLAG del timer del YM2151, che resta alto
                    // finche' il gestore non lo azzera, quindi il giro dopo
                    // riprova da solo. Qui l'evento e' unico: se lo scarti, e'
                    // perso per sempre.
                    sndcpu.IRequest = INT_RST38;         // alza la linea e la TIENE
                }
                // servila appena il Z80 la accetta. Se ha gli interrupt
                // disabilitati non si tocca niente: la linea resta alzata e la
                // consegna il core dopo la prossima EI.
                // ⚠️ Il controllo di IFF_1 si fa QUI e non dentro IntZ80 (che pure
                // lo farebbe) perche' IntZ80 toglie comunque la CPU dallo stato
                // HALT prima di guardarlo: chiamarla a interrupt mascherati
                // sveglierebbe un Z80 che l'hardware vero lascerebbe fermo.
                if ((sndcpu.IRequest != INT_NONE) && (sndcpu.IFF & IFF_1))
                    CpsIntZ80(&sndcpu, INT_RST38);
                // il DSP avanza di UN campione, subito: e' questo il `sync` che
                // sblocca il busy-wait del driver. Il QSound gira a 24038,46 Hz
                // e ricampiona lui verso i 32000 dell'I2S.
                qs_hle_render(qs, aud_out + i * 2, 1, AUD_HZ);
            }
        } else {
            // Z80 tenuto in reset dal 68000: nessuno scrive al DSP, il blocco
            // si puo' rendere in un colpo solo.
            qs_hle_render(qs, aud_out, AUD_CHUNK, AUD_HZ);
        }
        // ⛔⛔ VOLUME + GUADAGNO. Due cose diverse, entrambe mancanti prima:
        // 1. il VOLUME non lo applicava nessuno. `laserdisc_audio_write()` scrive
        //    i campioni sull'I2S tali e quali, non conosce `audio.volumeSetting`:
        //    lo applica ogni player nel proprio mix (DL, Space Ace, DL2 lo fanno).
        //    Qui mancava -> la barra si muoveva e il volume non cambiava.
        // 2. il livello era comunque BASSO anche al massimo, e non e' un'opinione:
        //    il picco del QSound misurato sull'harness di CABINATONE su 40 s di
        //    gioco e' **12771 su 32767 = -8,2 dBFS**, mentre i giochi normali di
        //    questo firmware arrivano a fondo scala (`value * 128 / volumeSetting`
        //    con volumeSetting=1 satura). Mancavano ~8 dB.
        // ⭐ Cura nella forma gia' usata da laserdisc.cpp e dl2.cpp: guadagno +
        //    SOFT LIMITER. Sotto soglia il segnale passa intatto e guadagnato;
        //    sopra viene compresso con continuita' fino a 32767 invece di essere
        //    tagliato di netto (il clamp secco e' quello che si sente come
        //    "gracchio" sulle punte).
        {
            const int vs  = laserdisc_current_volume();
            const int vol = (vs >= 30) ? 0 : (31 - vs);        // 0..30, 30 = barra piena
            if (vol == 0) {
                memset(aud_out, 0, (size_t)AUD_CHUNK * 2 * sizeof(int16_t));
            } else {
                // ⚡ UN moltiplicatore per BLOCCO, in virgola fissa 8.8: dentro il
                // ciclo resta una moltiplicazione e uno shift. Il task audio e' la
                // cosa piu' delicata che gira su questo core: qui NON si mettono
                // divisioni per campione ([[feedback_no_costly_calls_in_render]]).
                const int mul = (C1944_BOOST_NUM * vol * 256) / (C1944_BOOST_DEN * 30);
                // fondo scala IN INGRESSO al limiter col guadagno corrente: scala
                // col volume, cosi' a barra bassa il limiter e' del tutto inerte.
                int in_max = (32767 * mul) >> 8;
                if (in_max <= C1944_LIM_T) in_max = C1944_LIM_T + 1;
                for (int i = 0; i < AUD_CHUNK * 2; i++) {
                    int v = ((int)aud_out[i] * mul) >> 8;
                    int a = (v < 0) ? -v : v;
                    if (a > C1944_LIM_T) {          // solo le punte, raramente
                        a = C1944_LIM_T + ((a - C1944_LIM_T) * (32767 - C1944_LIM_T))
                                          / (in_max - C1944_LIM_T);
                        if (a > 32767) a = 32767;
                        v = (v < 0) ? -a : a;
                    }
                    aud_out[i] = (int16_t)v;
                }
            }
        }
        laserdisc_audio_write(aud_out, AUD_CHUNK);
        // ⛔⛔ RESPIRO OBBLIGATORIO — e' la rete che evita il RIAVVIO.
        // Finche' il task riesce a stare in tempo reale, la write qui sopra
        // BLOCCA e questo task dorme: l'idle del core 0 gira e non succede
        // nulla. Ma se per un qualunque motivo il task resta indietro (buffer
        // caldi finiti in PSRAM, picco di carico), la write **non blocca piu'**
        // e questo ciclo gira al 100% a priorita' 6 sul core 0: l'idle non viene
        // mai schedulato e dopo 5 s scatta il task watchdog -> la scheda si
        // RIAVVIA. E' il difetto segnalato dall'utente («a volte quando lo faccio
        // partire si riavvia»), ed e' intermittente proprio perche' dipende da
        // dove sono finiti i buffer.
        // ⚠️⚠️ `pdMS_TO_TICKS(1)` e NON `vTaskDelay(1)`: il secondo dorme UN TICK,
        // che dura 1 ms solo se il tick di FreeRTOS e' a 1000 Hz. Se fosse a 100
        // sarebbero **10 ms ogni 32 di audio prodotto**, cioe' un terzo del tempo
        // buttato — da solo basterebbe a far andare la musica lenta. Espresso in
        // MILLISECONDI la durata e' la stessa comunque sia configurato il kernel
        // (e a 100 Hz si riduce a una semplice cessione della CPU).
        if ((++respiro & 15) == 0) vTaskDelay(pdMS_TO_TICKS(1));
    }
    laserdisc_audio_end();
    g_aud_running = false;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- uscita
static bool c1944_want_exit(uint8_t b) {
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & BUTTON_START) {
        uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}

// ---------------------------------------------------------------- task (CPU1 = unico writer SPI)
static void c1944_task(void *arg) {
    (void)arg;
    c1944_window(2);

    // ---- SD + pack ----
    if (!(sd_is_mounted() ? true : sd_mount())) {
        while (!g_stop) { c1944_msg("SD NON MONTATA", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    if (!c1944_probe()) {
        while (!g_stop) { c1944_msg(g_primo_ko[0] ? g_primo_ko : "DATI MANCANTI",
                                    "SERVE /1944 SU SD", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    {
    // ---- memoria ----
    const char *perche = "";
    c1944_caricamento();          // legge e mostra la fase del giro PRECEDENTE
    if (!c1944_alloc(&perche)) {
        while (!g_stop) { char l[32]; snprintf(l, sizeof l, "PSRAM KO: %s", perche);
                          c1944_msg(l, "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- ROM programma: DUE immagini, dati e opcode ----
    if (!c1944_load("p.bin", prom, PROM_SZ)) {
        while (!g_stop) { c1944_msg("LETTURA P.BIN KO", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    /* avanzamento tolto: resta la sola schermata di caricamento */
    if (!c1944_load("pdec.bin", pdec, PROM_SZ)) {
        while (!g_stop) { c1944_msg("LETTURA PDEC.BIN KO", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- sonoro: m.bin + q.bin + chip QSound ----
    /* avanzamento tolto: resta la sola schermata di caricamento */
    if (!c1944_load("m.bin", z80rom, Z80ROM_SZ)) {
        while (!g_stop) { c1944_msg("LETTURA M.BIN KO", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    /* avanzamento tolto: resta la sola schermata di caricamento */
    if (!c1944_load("q.bin", qsrom, QSROM_SZ)) {
        while (!g_stop) { c1944_msg("LETTURA Q.BIN KO", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    // ⭐ LO STATO DEL CHIP QSOUND IN RAM INTERNA, come su CABINATONE
    // (cps1944.cpp:1114-1115: INTERNAL con fallback PSRAM). Sono ~6 KB, ma
    // `update_sample()` li ripercorre TUTTI a ogni campione: 16 voci + 3 ADPCM + la
    // linea di eco da 1024 word + quattro filtri FIR da 95 tap, 32.000 volte al
    // secondo. E' esattamente il caso della regola [[feedback_ottimizzare_contando_accessi]]:
    // non conta quanta memoria si sposta, contano gli accessi.
    // ⛔ `qs` ORA SI ALLOCA IN c1944_alloc(), per primo e SENZA riserva: vedi la
    // nota li'. Qui resta solo il controllo.
    aud_out = (int16_t *)heap_caps_calloc(1, AUD_CHUNK * 2 * sizeof(int16_t), MALLOC_CAP_INTERNAL);
    if (!qs || !aud_out) {
        while (!g_stop) { c1944_msg("PSRAM KO: QSOUND", "", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    qs_hle_init(qs, qsrom, QSROM_SZ);
    qs_hle_reset(qs);
    memset(&sndcpu, 0, sizeof sndcpu);
    z80_bank = 0;

    // ---- grafica: 20 MB che restano sulla SD ----
    // ⛔ Non si carica: 20 MB non entrano in PSRAM insieme a programma (3 MB) e
    // campioni QSound (8 MB). Si legge a BANCHI da 4 KB con la cache di gngeo,
    // che NON passa da FATFS ma da disk_read sui settori (vedi gfx_cache.h).
    /* avanzamento tolto: resta la sola schermata di caricamento */
    if (!gfx_cache_init(&gfx, "/1944/gfx.bin", GFX_BANK, GFX_CACHE)) {
        while (!g_stop) { c1944_msg("CACHE GFX KO", "GFX.BIN ILLEGGIBILE", C_RED);
                          if (c1944_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- 68000 ----
    memset(cps_a, 0, sizeof cps_a);
    memset(cps_b, 0, sizeof cps_b);
    memset(out_reg, 0, sizeof out_reg);
    obj_bank = 0;
    pri_ctrl = 0;             // e' statico: va riazzerato se si rientra nel gioco
    in0 = in1 = in2 = 0xFFFF; // ingressi attivi bassi: tutto a riposo
    z80_in_reset = 1; z80_reset_req = 0;
    ee93c46_reset(&eeprom, 1);          // vergine: il gioco ci scrive i suoi valori

    m68k_bus_set(&CPS_BUS);
    m68k_set_tables(m68k_jump, m68k_cyc);   // ⚠️ PRIMA di m68k_init
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    // ⭐⭐ LA SCORCIATOIA DEL PRELIEVO DELLE ISTRUZIONI (vedi m68k.h).
    // Il 68000 prende le istruzioni da `pdec` per tutti gli indirizzi < PROM_SZ:
    // e' esattamente il primo ramo di o_r16(). MISURATO col profilatore offline
    // (_verify_gfx/cps2_m68k/mk_m68kprof.py, 300 quadri): 30.446 prelievi dallo
    // spazio opcode per quadro su 38.279 accessi al bus totali — l'80%. Ognuno
    // costava una chiamata fra unita' di compilazione PIU' una chiamata
    // indiretta (due salti non predicibili) per finire in due `lbu`.
    // ⭐ PROVA DI EQUIVALENZA, non "dovrebbe andare": o_r16(a) restituisce
    //   rd16(pdec + a)  se  ((a>>16)&0xFF) <= 0x3F  &&  a < PROM_SZ.
    // Con PROM_SZ = 0x180000 < 0x400000, `a < PROM_SZ` implica gia'
    // ((a>>16)&0xFF) <= 0x17 <= 0x3F: la condizione della scorciatoia e' un
    // sottoinsieme ESATTO di quel ramo, e restituisce gli stessi due byte nello
    // stesso ordine. Fuori dalla finestra si torna sul percorso di prima.
    m68k_set_opcode_window(pdec, PROM_SZ);
#if C1944_IDLE_SKIP
    m68k_set_idle_skip(C1944_IDLE_LO, C1944_IDLE_HI);
#endif
    m68k_pulse_reset();

    // ⭐ I VETTORI DI RESET arrivano dallo spazio OPCODE, cioe' dalla ROM
    // DECIFRATA: se il doppio bus e' cablato bene valgono esattamente questi due
    // numeri. Qui non c'e' il Serial ([[feedback_p4_no_serial]]), quindi se non
    // tornano lo si SCRIVE A SCHERMO per due secondi e poi si parte lo stesso: il
    // gioco non partira', ma il messaggio dice DOVE guardare (il pack sulla SD).
    {
        const uint32_t ssp = o_r32(0), rpc = o_r32(4);
        if (ssp != 0x00930000u || rpc != 0x000006D0u) {
            char l1[32], l2[32];
            snprintf(l1, sizeof l1, "SSP %08lX", (unsigned long)ssp);
            snprintf(l2, sizeof l2, "PC0 %08lX", (unsigned long)rpc);
            const char *l[5] = { "VETTORI KO", l1, l2, "PACK SD SBAGLIATO?", "" };
            const uint16_t c[5] = { C_RED, C_RED, C_RED, C_GREY, C_GREY };
            c1944_page(l, c);
            vTaskDelay(pdMS_TO_TICKS(2000));
        }
    }

    // task audio su core0, prio 6: sta quasi sempre fermo dentro la write
    // bloccante sull'I2S, quindi dorme e non affama l'emulazione su CPU1.
    g_aud_running = true;
    // ⛔ 16 KB, non 8: qui girano un Z80 interpretato e il QSound HLE (16 voci,
    // eco, quattro FIR da 95 tap). Vedi la stessa nota in outrun.cpp.
    // ⚠️ L'esito della create VA CONTROLLATO: se fallisce e `g_aud_running`
    // resta true, il gioco parte muto SENZA nessun avviso (il ramo `else` copre
    // solo `audio_ok == false`) e ogni uscita spreca i 2 s pieni di attesa.
    if (xTaskCreatePinnedToCore(c1944_audio_task, "c1944aud", 16384,
                                nullptr, 6, nullptr, 0) != pdPASS)
        g_aud_running = false;

    // ⭐ Le bande nere si dipingono UNA VOLTA, poi la finestra si stringe sulle
    // sole 224 colonne del gioco: da qui in avanti lo SPI non trasporta piu' un
    // solo pixel di nero. Vale 46.080 byte per quadro = il 30% del traffico.
    c1944_bande_nere();
    c1944_window(1);

    // ⭐ SOLO ADESSO si prova a portare i due buffer caldi in RAM INTERNA: il
    // task audio ha gia' fatto laserdisc_audio_begin() (I2S + i suoi buffer DMA),
    // quindi la memoria libera che si misura e' quella VERA e non si puo' restare
    // muti per colpa del video ([[feedback_audio_muto_e_ram_interna]]).
    vTaskDelay(pdMS_TO_TICKS(200));
    espandi_init();                 // 2 KB caldissimi: prima dei 172 di fb
    // ⛔⛔ PROMOZIONE IN RAM INTERNA DI fb E pri_bm: **DISATTIVATA** il
    // 2026-08-10, ed e' un candidato serio per il riavvio intermittente.
    // I numeri, misurati sul device: la RAM interna libera qui e' 156-183 KB.
    //   fb      172.032 byte -> serve 172+48 = 220 KB: **non riesce MAI**.
    //   pri_bm   86.016 byte -> serve  86+48 = 134 KB: **riesce SOLO quando si
    //            e' sul lato alto del range**, e allora si porta via 84 KB per
    //            tutta la partita.
    // Cioe': l'ottimizzazione o non fa nulla, oppure lascia la RAM interna a
    // ~25-50 KB proprio mentre il gioco batte la SD (la cache grafica fa ~1.300
    // letture per quadro, e SDMMC/FATFS allocano in interna a ogni miss). Da li'
    // una allocazione interna che fallisce dentro l'IDF finisce in un abort =
    // **ESP_RST_PANIC**, e intermittente perche' dipende dal frammento piu'
    // grande disponibile in quel giro.
    // ⚠️ `heap_caps_get_free_size()` per giunta restituisce il TOTALE libero, non
    // il blocco contiguo piu' grande: la riserva non era nemmeno onorata davvero.
    // ⭐ Costo del disattivarla: `fb` e `pri_bm` restano in PSRAM, dove stavano
    // gia' in tutte le partite in cui l'ottimizzazione non scattava — cioe' non
    // si perde niente di misurato.
#define C1944_PROMUOVI_INTERNA 0
#if C1944_PROMUOVI_INTERNA
    c1944_prova_interna((void **)&fb,     (size_t)CPS_W * CPS_H * 2);
    c1944_prova_interna((void **)&pri_bm, (size_t)CPS_W * CPS_H);
#endif

    // ---- il ciclo di gioco -------------------------------------------------
    // Un giro = UN fotogramma del CPS-2 a 59,63 Hz. Il video va a frame alterni
    // (vedi CPS_VIDEO_HALF_RATE): l'emulazione resta a tempo, lo schermo no.
    {
    uint32_t fcount = 0;
    // ⭐⭐ SCADENZA CUMULATIVA, non "riparto da adesso ogni giro".
    // MISURA sul device: il quadro SENZA video costa 9,7 ms e quello CON video
    // 25,1 (12,8 di CPU + 12,3 di blit), contro un budget di 16,77. Ripartendo
    // da capo ad ogni giro il quadro leggero finiva in anticipo e **l'anticipo
    // veniva buttato** (7 ms di attesa a vuoto), mentre quello pesante restava
    // 8,3 ms indietro senza poterli recuperare: 44 quadri/s invece di 59,63.
    // Accumulando la scadenza, la media e' (9,7+25,1)/2 = 17,4 ms contro 16,77:
    // il gioco passa dal 74% al ~96% della velocita' vera.
    int64_t next_us = esp_timer_get_time();
    while (!g_stop) {
        // ⭐ fase 13 = il gioco gira da un secondo. Se il riavvio arriva con
        // questa fase, non e' l'avvio ma il regime, ed e' un'altra caccia.
        const int64_t t0 = esp_timer_get_time();
        leggi_input();
        run_frame();
#if CPS_VIDEO_HALF_RATE
        if ((fcount & 1) == 0)
#endif
#if C1944_MISURA
        {
            const int64_t ta = esp_timer_get_time();      // fine emulazione+render
            render();
            const int64_t tb = esp_timer_get_time();
            c1944_blit();
            const int64_t tc = esp_timer_get_time();
            // in DECIMI di millisecondo: con gli interi tondi si perderebbero
            // fino a 4 ms sul totale (lezione del cronometro di CABINATONE)
            static uint32_t acc_cpu = 0, acc_blt = 0, nq = 0, t_sec = 0, fr = 0;
            acc_cpu += (uint32_t)((tb - t0) / 100);       // include leggi_input+run_frame
            acc_blt += (uint32_t)((tc - tb) / 100);
            nq++; fr++;
            const uint32_t ora = millis();
            if (!t_sec) t_sec = ora;
            if (ora - t_sec >= 1000) {
                g_fps = fr * 1000 / (ora - t_sec);
                g_us_cpu = nq ? acc_cpu / nq : 0;
                g_us_blt = nq ? acc_blt / nq : 0;
                acc_cpu = acc_blt = nq = fr = 0; t_sec = ora;
            }
            (void)ta;
        }
#else
        { render(); c1944_blit(); }
#endif
        // barra del volume nella banda nera: si ridisegna solo se e' cambiata
        draw_vol_bar();

        if (c1944_want_exit(g_input->buttons_get())) break;

        // ritmo: si dorme il grosso dell'attesa e si aspetta a vuoto solo
        // l'ultimo millisecondo, cosi' il frame non scivola in avanti.
        // ⭐ La scadenza AVANZA di FRAME_US dalla precedente, NON da adesso:
        // cosi' l'anticipo del quadro leggero copre il ritardo di quello pesante.
        next_us += FRAME_US;
        const int64_t due = next_us;
        const int64_t left = due - esp_timer_get_time();
        if (left > 2000) vTaskDelay(pdMS_TO_TICKS((left - 1000) / 1000));
        while (esp_timer_get_time() < due) { }
        // ⚠️ se si resta indietro in modo CRONICO (piu' di 4 quadri) non si
        // accumula debito all'infinito: si riparte da adesso. Senza, dopo una
        // pausa lunga (caricamento da SD, cambio scena) il gioco correrebbe in
        // avanti a velocita' doppia per recuperare.
        { const int64_t ora = esp_timer_get_time();
          if (ora - next_us > 4 * FRAME_US) next_us = ora; }
        // ⚠️ se il frame e' arrivato in ritardo non si dorme MAI: senza questo
        // respiro l'idle di questo core non gira e parte il watchdog.
        if ((++fcount & 7) == 0) vTaskDelay(1);
    }
    }
    }

uscita:
    // ferma il task audio PRIMA di liberare la memoria che sta usando; puo'
    // essere bloccato dentro la write sull'I2S, gli si lascia il tempo di uscire
    g_stop = true;
    for (int i = 0; i < 200 && g_aud_running; i++) vTaskDelay(pdMS_TO_TICKS(10));
    c1944_free();
    // pulisci a nero l'intera finestra e torna alla finestra del menu
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    c1944_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- entry
bool cps1944_play(Input *input, unsigned short *out_strip) {
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;
    g_aud_running = false;   // stato pulito anche se la create audio fallì

    if (xTaskCreatePinnedToCore(c1944_task, "c1944", C1944_STACK,
                                nullptr, 2, nullptr, 1) != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

// ============================================================================
// Il core Z80 di 1944 vive IN QUESTA UNITA' DI COMPILAZIONE (simboli Cps*, vedi
// i #define in testa) — stessa soluzione di spaceace.cpp: cosi' le RdZ80/WrZ80
// globali del core di Marat non collidono con quelle degli altri due Z80 del
// progetto. `register` neutralizzato: rimosso in C++17, il core lo usa come
// storage-class.
// ============================================================================
#define register
extern "C" {
#include "cpus/z80cps/Z80cps.c"
}
#undef register
