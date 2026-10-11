// ============================================================================
// ghouls.cpp — GHOULS'N GHOSTS (Capcom 1988, CPS-1, set `ghouls`) per SPINNERINO.
//
// Porting da CABINATONE\ghouls.cpp, dove il gioco e' stato scritto e validato.
// Vedi emulation/ghouls.h per il perche' e' un player AUTONOMO e non un
// machineBase della griglia.
//
// ⭐ IL MODELLO DI QUESTO FILE E' cps1944.cpp, non ghouls.cpp di CABINATONE:
//    il GUSCIO (task, finestra SPI, blit a strisce, pagine di stato, verifica
//    del pack sulla SD, task audio sul core 0, uscita con START 3 s) e' quello
//    di 1944, che su QUESTA scheda gira. La LOGICA DI EMULAZIONE (bus del
//    68000, mapper DM620, palette, tilemap, sprite, bus del Z80 sonoro) e'
//    trascritta ALLA LETTERA da CABINATONE: ogni ramo la' dentro e' stato
//    pagato con del debug (harness offline in C che rendeva il gioco in PNG),
//    non si "semplifica" ([[feedback_porta_non_riprogettare]]).
//
// ⛔ COSA E' STATO ADATTATO rispetto a CABINATONE (e SOLO questo):
//   1. il display: la' pannello DSI 1024x600 + PPA hardware + lcd_draw_bitmap,
//      qui un pannello SPI 320x240 scritto A STRISCE di 8 righe. Geometria
//      identica a 1944 (gh_blit): gioco DIRITTO, 384 colonne ridotte a 240,
//      224 righe centrate nell'asse da 320 -> 48 px di banda nera sopra e sotto.
//   2. la palette: qui la LUT e' BYTE-SWAPPED (lo vuole questo pannello SPI),
//      su CABINATONE il DSI voleva i valori nativi. E' UNA riga in
//      gh_build_palette(); il resto del renderer non cambia di un carattere.
//   3. l'audio: audio_pce_* -> ponte laserdisc_audio_begin/write/end (I2S gia'
//      aperto a 32 kHz dal firmware). ⛔ NIENTE i2s_set_sample_rates
//      ([[spinnerino_audio_i2s_32khz]]).
//   4. l'input: bt_get_buttons() -> g_input->buttons_get(), maschere BUTTON_*.
//   5. il Z80 sonoro e' RINOMINATO `Gh*` (vedi i #define qui sotto).
//   6. le allocazioni in RAM INTERNA: la' il binario e' di UN gioco solo, qui
//      ce ne sono dieci. Vedi il conto in gh_alloc().
// ============================================================================
// ⚡ come m68k_core.c e cps1944.cpp: l'IDE compila a **-Os**, e qui dentro ci
//    sono sia il bus del 68000 (~29.400 accessi/frame MISURATI sull'harness di
//    CABINATONE) sia i cicli PER PIXEL del renderer.
#pragma GCC optimize("O2")
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/ghouls.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"

// ---- Z80 sonoro, RINOMINATO per non collidere ------------------------------
// ⛔ In questo progetto esistono GIA' TRE core Z80: `cpus/z80/` (i giochi della
// griglia), `cpus/z80ace/` (Space Ace, simboli `Ace*`) e `cpus/z80cps/` (1944,
// simboli `Cps*`). Il core di Marat dichiara RdZ80/WrZ80/... GLOBALI e senza
// contesto: due moduli che le definiscono entrambi = simbolo duplicato in link,
// anche se a runtime ne gira uno solo. Quindi QUARTA copia, `cpus/z80gh/`, coi
// simboli `Gh*` — esattamente la cura di spaceace.cpp e cps1944.cpp.
// ⚠️ Gli #include interni di Z80gh.c sono stati ripuntati a cpus/z80gh/; i
// Codes*.h restano condivisi da cpus/z80/ (sono frammenti di codice, non
// simboli), come gia' fanno z80ace e z80cps.
#define ExecZ80    GhExecZ80
#define RunZ80     GhRunZ80
#define ResetZ80   GhResetZ80
#define IntZ80     GhIntZ80
#define StepZ80    GhStepZ80
#define JumpZ80    GhJumpZ80
#define RdZ80      GhRdZ80
#define WrZ80      GhWrZ80
#define InZ80      GhInZ80
#define OutZ80     GhOutZ80
#define LoopZ80    GhLoopZ80
#define PatchZ80   GhPatchZ80
#define DebugZ80   GhDebugZ80
#define OpZ80_INL  GhOpZ80_INL
extern "C" {
#include "cpus/z80gh/Z80.h"
}

// ---- YM2151 (Jarek Burczynski, versione vgmplay) ---------------------------
// ⚠️ mamedef.h PRIMA di ym2151.h: e' li' che stanno INT16 e stream_sample_t, da
// cui dipende il typedef SAMP. E' C puro: senza extern "C" i simboli verrebbero
// cercati decorati. Il .c lo tira dentro il wrapper di root ym2151_core.c.
extern "C" {
#include "cpus/ym2151/mamedef.h"
#include "cpus/ym2151/ym2151.h"
}

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
extern void laserdisc_audio_begin(void);
extern void laserdisc_audio_end(void);
extern void laserdisc_audio_write(const int16_t *stereo, int frames);
// modalita' regolazione volume + volume corrente (1..30, 1 = massimo): sono le
// stesse due funzioni che usano DL, Space Ace, DL2 e 1944 per la loro barra.
extern bool input_is_vol_mode_active(void);
extern int  laserdisc_current_volume(void);

// ---------------------------------------------------------------- parametri
#define LD_FB_H       240
// ⛔⛔⛔ 32 KB, NON 16 — corretto il 2026-08-10. Questo task chiama `sd_mount()`,
// cioe' `esp_vfs_fat_sdmmc_mount()`, che fra init SDMMC e scansione FAT e' molto
// vorace di stack: con 16 KB lo sfonda e la scheda va in PANICO. Succede UNA
// VOLTA SOLA, al primo lancio dopo l'accensione (dopo, `sd_is_mounted()` e' vero
// e il mount non si rifa'): da qui il riavvio che l'utente vedeva **sempre la
// prima volta e mai le successive**, su tutti e tre i giochi da SD.
// ⭐ 32768 e' il valore che usano da sempre i player LASERDISC (LD_TASK_STACK in
// laserdisc.cpp/spaceace.cpp/dl2.cpp), che montano la SD dal proprio task e non
// si sono mai riavviati. I 16 KB venivano da
// [[feedback_emulation_task_stack_fm]], che parla degli interpreti di CPU.
#define GH_STACK      32768
#define GH_DIR        SD_MOUNT_POINT "/ghouls"

// ---------------------------------------------------------------- geometria
#define GH_W  384                // area visibile: HBEND 64 -> HBSTART 448
#define GH_H  224                //                VBEND 16 -> VBSTART 240
// ⭐ L'origine dell'area visibile NON e' (0,0): MAME disegna in coordinate
// assolute del quadro e ritaglia a x=64..447, y=16..239. Le tilemap vanno
// quindi lette con +64/+16 sullo scroll, e gli sprite disegnati a -64/-16.
// (Il gioco stesso mette scroll1x = 0xFFC0 = -64 proprio per compensare: se si
// dimentica l'offset il layer del testo esce spostato di mezzo schermo.)
#define GH_XORG 64
#define GH_YORG 16

// ---- geometria a schermo: IDENTICA a 1944 (vedi il commento su gh_blit) ----
// L'ALTEZZA del gioco (224) va sull'asse da TFT_FB_W=320, che e' il VERTICALE
// visto dall'utente: ci sta intera e avanzano 48 px di banda NERA sopra e sotto.
#define GH_X_OFF  ((TFT_FB_W - GH_H) / 2)         // = 48

// PACING: 8e6/(512*262) = 59.63899 Hz -> 16767 us.
#define FRAME_US     16767
#define CYC_FRAME    167672              // 10 MHz / 59.63899 Hz

// ⭐ Render a frame alterni: l'emulazione resta a 59,639 Hz (gioco e tempi
// giusti) e lo schermo si aggiorna un frame su due. E' la stessa leva di 1944 su
// questa scheda: la finestra stretta e' 224*240*2 = 107.520 byte, che a 80 MHz di
// SPI sono ~10,7 ms — due terzi del budget di un frame, solo per il bus.
// Su CABINATONE non serviva (DSI + PPA); qui il collo di bottiglia e' lo SPI.
#define GH_VIDEO_HALF_RATE 1

// ⛔⭐ IDLE-SKIP DEL 68000: **NON SI ACCENDE**. Su 1944 e' legittimo perche'
// dentro una fetta di m68k_execute() l'emulatore e' un sistema chiuso; qui il
// 68000 dialoga col Z80 sonoro (soundlatch) che gira sull'ALTRO core, e
// l'ipotesi di "sistema chiuso" NON e' verificata. Andrebbe DIMOSTRATA con un
// harness A/B (come mk_m68kidle.py per 1944), non assunta.

// ------------------------------------------------------------------- memoria
#define PRG_SIZE     0x100000            // ghouls usa 0x000000-0x0fffff
#define GFXRAM_SIZE  0x030000            // 900000-92ffff
#define MAINRAM_SIZE 0x010000            // ff0000-ffffff
#define GFXROM_SIZE  0x300000            // region "gfx" interlacciata (da SD)
#define GFXPX_SIZE   (GFXROM_SIZE * 2)   // 6 MB espansi a 1 byte/pixel
#define GFXROWS      (GFXROM_SIZE / 8)   // 393216 righe da 16 pixel
#define OBJ_SIZE     0x800
#define PAL_ENTRIES  0xc00

// tabelle di Musashi: 256 KB + 320 KB, allocate in PSRAM (vedi m68kops.c: il
// core e' gia' modificato perche' le allochi il CHIAMANTE, m68k_set_tables)
#define M68K_JUMP_SIZE   (0x10000 * sizeof(void *))
#define M68K_CYC_SIZE    (5 * 0x10000)

static uint8_t  *prg      = nullptr;     // ROM 68000            1 MB  PSRAM
static uint8_t  *gfxram   = nullptr;     // 900000-92ffff      192 KB  PSRAM
static uint8_t  *mainram  = nullptr;     // ff0000-ffffff       64 KB  PSRAM
static uint8_t  *gfxpx    = nullptr;     // grafica espansa      6 MB  PSRAM
static uint8_t  *rowblank = nullptr;     // 1 bit per riga      48 KB  PSRAM
static uint16_t *objbuf   = nullptr;     // obj latch a vblank   2 KB  INTERNA
static uint16_t *pal565   = nullptr;     // palette convertita   6 KB  INTERNA
static void     *m68k_jump = nullptr, *m68k_cyc = nullptr;

// ------------------------------------------------------- sottosistema sonoro
// Z80 @ 3.579545 MHz + YM2151 @ 3.579545 MHz. Niente OKI6295: il set 'ghouls'
// non ha ROM di campioni (ROMREGION_ERASEFF), quindi $F002/$F006 sono inerti.
//
// ⭐ Analisi STATICA del ROM sonoro (fatta sui byte di snd.bin, non supposta):
//   $0000  DI / IM 1 / LD SP,$D7FF / JP $005B          -> modo di interrupt 1
//   $0038  DI / LD BC,$F001 / LD A,(BC) / BIT 0,A ...  -> il gestore LEGGE lo
//          STATO del YM2151 e testa il bit 0 = flag del Timer A; termina con
//          RETI a $0059 (il core di Marat implementa ED 4D).
//   $F008 letto in un solo punto ($0124) = soundlatch; $F00A a $03B0.
//   $F004 = banco ROM (5 riferimenti), $F002/$F006 = OKI (inerti qui).
// Quindi: l'IRQ del Z80 sonoro nasce dal TIMER del YM2151, e si inietta a
// LIVELLO leggendo il registro di stato (in questo core la callback di IRQ e'
// commentata via, ma i flag vengono alzati dentro ym2151_update_one).
#define SND_ROM_SIZE  0x18000            // 0x0000-0x7fff + 2 banchi a 0x10000
#define SNDRAM_SIZE   0x800              // 0xd000-0xd7ff
#define AUD_RATE      32000              // I2S nativo dello SPINNERINO
#define YM_CLOCK      3579545
#define SND_Z80_HZ    3579545
#define AUD_CHUNK     64                 // campioni per blocco (2 ms)
// cicli Z80 per campione in virgola fissa 16.16: 3579545/32000 = 111.8608
#define Z80_CYC_PER_SAMPLE_FP ((uint32_t)(((uint64_t)SND_Z80_HZ << 16) / AUD_RATE))
// Guadagno, MISURATO sull'harness di CABINATONE (sndtest.c) e non messo a
// occhio: mandando al ROM sonoro i comandi $00..$40 il picco piu' alto del chip
// e' 16852, cioe' -5.8 dBFS. Con 3/4 il caso peggiore assoluto e'
// 32767*3/4 = 24575: **non puo' mai clippare**.
#define AUD_GAIN_NUM  3
#define AUD_GAIN_DEN  4

static uint8_t  *snd_rom = nullptr;      // 96 KB PSRAM
static uint8_t  *sndram  = nullptr;      // 2 KB INTERNA
static void     *ym      = nullptr;
static Z80       sndcpu;
static int       snd_bank = 0;           // $F004 bit0 -> banco a 0x10000/0x14000
static uint8_t   ym_addr  = 0;           // registro latchato da $F000
static volatile uint8_t soundlatch = 0xFF, soundlatch2 = 0xFF;   // scritti dal 68000
static bool      audio_ok = false;
// ⚠️ i buffer del YM2151 sono a 32 BIT, non 16: in ym2151.h `SAMP` e'
// `stream_sample_t` = INT32 (la variante INT16 e' commentata via). Allocandoli
// a 16 bit, ym2151_update_one scrive il doppio dei byte e sfonda l'heap —
// crash preso dall'harness sul PC, sarebbe successo identico sul device.
static int32_t  *ym_l = nullptr, *ym_r = nullptr;
static int16_t  *aud_out = nullptr;

static uint16_t cps_a[0x20], cps_b[0x20];
static uint8_t  in0 = 0xFF, in1_lo = 0xFF;      // tutti ACTIVE LOW
static uint8_t  dswa = 0xFF, dswb = 0xFF, dswc = 0xFF;
static bool     pal_dirty = true;

// ---- video / stato del player ----------------------------------------------
static uint16_t *fb = nullptr;           // 384x224 RGB565 (byte-swapped)
static uint16_t *g_strip = nullptr;
static Input    *g_input = nullptr;
static volatile bool g_stop = false;
static volatile bool g_done = false;
static volatile bool g_aud_running = false;

// ---- i tre file e la loro dimensione ATTESA (byte veri del converter) -------
typedef struct { const char *nome; uint32_t byte; } gh_file_t;
static const gh_file_t GH_FILES[] = {
    { "code.bin", 1048576 },   // 68000, gia' interlacciato big-endian
    { "gfx.bin",  3145728 },   // region grafica GREZZA, interlacciata
    { "snd.bin",    98304 },   // Z80 sonoro + 2 banchi
};
static const int GH_NFILE = sizeof(GH_FILES) / sizeof(GH_FILES[0]);
static int  g_ok = 0;
static char g_primo_ko[24] = "";

// ============================================================ mappa memoria
// ⚠️ TRASCRITTA ALLA LETTERA da CABINATONE\ghouls.cpp. Percorso caldissimo:
// ordinato per frequenza misurata sull'harness (26k letture a 16 bit per frame,
// di cui la stragrande maggioranza dalla ROM).
static inline uint32_t rd16(uint32_t a)
{
    if (a < PRG_SIZE)                  return (prg[a] << 8) | prg[a + 1];
    if (a >= 0xff0000)                 { a &= 0xffff; return (mainram[a] << 8) | mainram[a + 1]; }
    if (a >= 0x900000 && a < 0x930000) { a -= 0x900000; return (gfxram[a] << 8) | gfxram[a + 1]; }
    if (a >= 0x800000 && a < 0x800008) return (in1_lo << 8) | in1_lo;   // IN1: P1 basso, P2 alto
    if (a >= 0x800018 && a < 0x800020) {                                // cps1_dsw_r
        const int off = (a - 0x800018) >> 1;
        int v = 0xff;
        if      (off == 0) v = in0;
        else if (off == 1) v = dswa;
        else if (off == 2) v = dswb;
        else if (off == 3) v = dswc;
        return (v << 8) | 0xff;        // il byte basso e' sempre 0xff
    }
    // CPS-B in lettura: per ghouls (CPS_B_01) non c'e' ne' ID ne' moltiplicatore
    // di protezione (cpsb_addr = -1, mult "__not_applicable__"): torna il registro.
    if (a >= 0x800140 && a < 0x800180) return cps_b[(a - 0x800140) >> 1];
    return 0xffff;
}

static inline void wr16(uint32_t a, uint32_t v)
{
    if (a >= 0xff0000)                 { a &= 0xffff; mainram[a] = v >> 8; mainram[a + 1] = v; return; }
    if (a >= 0x900000 && a < 0x930000) {
        a -= 0x900000; gfxram[a] = v >> 8; gfxram[a + 1] = v;
        pal_dirty = true;              // vedi gh_build_palette(): rifa' solo se serve
        return;
    }
    if (a >= 0x800100 && a < 0x800140) { cps_a[(a - 0x800100) >> 1] = v; pal_dirty = true; return; }
    if (a >= 0x800140 && a < 0x800180) { cps_b[(a - 0x800140) >> 1] = v; pal_dirty = true; return; }
    // soundlatch: cps1_soundlatch_w prende il byte BASSO se l'accesso tocca i
    // bit 0-7, altrimenti quello alto (qui arriva una word: byte basso).
    if (a >= 0x800180 && a < 0x800188) { soundlatch  = (uint8_t)v; return; }
    if (a >= 0x800188 && a < 0x800190) { soundlatch2 = (uint8_t)v; return; }
    // coin control (800030): ignorato. La ROM e' sola lettura.
}

// Le callback GLOBALI di Musashi stanno in m68k_bus.cpp: qui si registrano nel
// dispatcher condiviso, cosi' possono convivere piu' giochi a 68000 nel binario.
static unsigned int gh_r8(unsigned int a)
{
    if (a < PRG_SIZE)                  return prg[a];
    if (a >= 0xff0000)                 return mainram[a & 0xffff];
    if (a >= 0x900000 && a < 0x930000) return gfxram[a - 0x900000];
    { const unsigned int w = rd16(a & ~1u); return (a & 1) ? (w & 0xff) : (w >> 8); }
}
static unsigned int gh_r16(unsigned int a) { return rd16(a); }
static unsigned int gh_r32(unsigned int a) { return (rd16(a) << 16) | rd16(a + 2); }

static void gh_w8(unsigned int a, unsigned int v)
{
    if (a >= 0xff0000)                 { mainram[a & 0xffff] = v; return; }
    if (a >= 0x900000 && a < 0x930000) { gfxram[a - 0x900000] = v; pal_dirty = true; return; }
    // ⚠️ il soundlatch va intercettato QUI e non tramite lettura-modifica-scrittura
    // della word: il gioco ci scrive un BYTE, e passando da wr16 si perderebbe
    // quale dei due byte e' stato scritto (MAME sceglie in base a mem_mask).
    if (a >= 0x800180 && a < 0x800188) { soundlatch  = (uint8_t)v; return; }
    if (a >= 0x800188 && a < 0x800190) { soundlatch2 = (uint8_t)v; return; }
    unsigned int w = rd16(a & ~1u);
    w = (a & 1) ? ((w & 0xff00) | (v & 0xff)) : ((w & 0x00ff) | ((v & 0xff) << 8));
    wr16(a & ~1u, w);
}
static void gh_w16(unsigned int a, unsigned int v) { wr16(a, v); }
static void gh_w32(unsigned int a, unsigned int v) { wr16(a, v >> 16); wr16(a + 2, v); }

// ACK dell'interrupt: la scheda risponde VPA e CANCELLA la linea (irqack_r).
// Senza questo la IPL1 resterebbe asserita e il 68000 rientrerebbe nel gestore
// all'infinito appena riabilita gli interrupt.
static int gh_int_ack(int level)
{
    (void)level;
    m68k_set_irq(0);
    return M68K_INT_ACK_AUTOVECTOR;
}

// ⚠️ SETTE campi, non dieci: p8/p16/p32 (spazio OPCODE separato) restano
// nullptr e m68k_bus_set() li fa cadere sulle r*, che e' esattamente giusto —
// sul CPS-1 il programma NON e' cifrato e opcode e dati vengono dallo stesso
// `prg` (e' il CPS-2 ad avere le due immagini).
static const M68kBus GH_BUS = {
    gh_r8, gh_r16, gh_r32, gh_w8, gh_w16, gh_w32, gh_int_ack
};

// ============================================================ basi video
// cps1_base() di cps1_v.cpp:2099:
//   base = reg * 256;  base &= ~(boundary - 1);  indice = (base & 0x3ffff)
// ⚠️ la maschera e' 0x3FFFF FISSA, NON la dimensione della gfxram (0x30000):
//    sbagliandola la palette finisce a offset 0, sopra la tilemap del testo.
// ⛔⛔ …e proprio perche' e' FISSA a 0x3FFFF (256 KB) mentre `gfxram` qui e'
// `GFXRAM_SIZE = 0x30000` (192 KB), va poi RIPORTATA DENTRO IL BLOCCO VERO.
// `cps_a[]` lo scrive il 68000 emulato: con un valore fra 0x30000 e 0x3FFFF si
// legge fino a 64 KB oltre la fine dell'allocazione (e la palette, che avanza
// senza rimascherare, fino a +70 KB). In PSRAM di solito non si nota; se il
// blocco capita in cima all'allocato l'indirizzo esce dalla finestra mappata e
// diventa un'ECCEZIONE — intermittente, perche' dipende da dove l'allocatore ha
// messo `gfxram` e da cosa ha scritto il gioco.
static inline uint32_t cps_base(int reg, uint32_t boundary)
{
    uint32_t b = (uint32_t)cps_a[reg] * 0x100;
    b &= ~(boundary - 1);
    b &= 0x3ffff;
    if (b >= GFXRAM_SIZE) b &= (GFXRAM_SIZE - 1);   // resta DENTRO il blocco vero
    return b;
}
// indici a WORD dei registri CPS-A (cps1.h:176 e seguenti)
#define CPSA_OBJ        (0x00 / 2)
#define CPSA_SCROLL1    (0x02 / 2)
#define CPSA_SCROLL2    (0x04 / 2)
#define CPSA_SCROLL3    (0x06 / 2)
#define CPSA_OTHER      (0x08 / 2)
#define CPSA_PALETTE    (0x0a / 2)
#define CPSA_SCR1X      (0x0c / 2)
#define CPSA_SCR1Y      (0x0e / 2)
#define CPSA_SCR2X      (0x10 / 2)
#define CPSA_SCR2Y      (0x12 / 2)
#define CPSA_SCR3X      (0x14 / 2)
#define CPSA_SCR3Y      (0x16 / 2)
#define CPSA_ROWSCROLL  (0x20 / 2)
#define CPSA_VIDEOCTRL  (0x22 / 2)
// CPS-B-01: ctrl 0x26, priorita' 0x28-0x2e, palctrl 0x30, layer enable
// {scroll1 0x02, scroll2 0x04, scroll3 0x08} (cps1_v.cpp:486)
#define CPSB_LAYERCTRL  (0x26 / 2)
#define CPSB_PALCTRL    (0x30 / 2)

static inline uint32_t grw(uint32_t off)         // word big-endian nella gfxram
{
    return ((uint32_t)gfxram[off] << 8) | gfxram[off + 1];
}

// ============================================================ mapper DM620
// cps1_v.cpp:580. bank_sizes = { 0x8000, 0x2000, 0x2000, 0 }
//   SCROLL3 0x8000-0xbfff -> banco 1
//   SPRITES 0x2000-0x3fff -> banco 2
//   tutto il resto        -> banco 0
// Lo shift porta il codice nell'unita' comune (sprite/scroll2 x2, scroll3 x8).
#define GT_SPRITES 1
#define GT_SCROLL1 2
#define GT_SCROLL2 4
#define GT_SCROLL3 8
#define GT_STARS   16

static inline int dm620_map(int type, int code)
{
    int shift = 0;
    if      (type == GT_SPRITES) shift = 1;
    else if (type == GT_SCROLL2) shift = 1;
    else if (type == GT_SCROLL3) shift = 3;
    code <<= shift;

    if (code >= 0x8000 && code <= 0xbfff && (type & GT_SCROLL3))
        return (0x8000 + (code & 0x1fff)) >> shift;              // banco 1
    if (code >= 0x2000 && code <= 0x3fff && (type & GT_SPRITES))
        return (0xa000 + (code & 0x1fff)) >> shift;              // banco 2
    if (code >= 0x00000 && code <= 0x1ffff)
        return (code & 0x7fff) >> shift;                         // banco 0
    return -1;
}

// ============================================================ palette
// cps1_build_palette(): 6 pagine da 0x200 voci, copiate SOLO se abilitate dal
// registro palctrl; se una pagina iniziale e' saltata, le successive scalano.
// Colore: xRGB 4-4-4-4 con la "brightness" nei 4 bit alti.
static uint8_t bright_tab[16][16];        // [brightness][componente] -> 0..255

static void gh_init_bright(void)
{
    for (int b = 0; b < 16; b++) {
        const int bright = 0x0f + (b << 1);
        for (int c = 0; c < 16; c++)
            bright_tab[b][c] = (uint8_t)(c * 0x11 * bright / 0x2d);
    }
}

static void gh_build_palette(void)
{
    if (!pal_dirty) return;
    pal_dirty = false;

    const uint32_t off  = cps_base(CPSA_PALETTE, 0x400);   // m_palette_align
    const uint16_t ctrl = cps_b[CPSB_PALCTRL];
    uint32_t src = off;
    for (int page = 0; page < 6; page++) {
        if (ctrl & (1 << page)) {
            uint16_t *d = pal565 + page * 0x200;
            for (int i = 0; i < 0x200; i++) {
                const uint32_t v = grw(src); src += 2;
                const uint8_t *t = bright_tab[v >> 12];
                const uint8_t r = t[(v >> 8) & 0x0f];
                const uint8_t g = t[(v >> 4) & 0x0f];
                const uint8_t b = t[v & 0x0f];
                const uint16_t c = (uint16_t)(((r & 0xf8) << 8) | ((g & 0xfc) << 3) | (b >> 3));
                // ⚠️ UNICO adattamento del renderer: RGB565 BYTE-SWAPPED. Su
                // questo pannello SPI il byte alto va per secondo (su CABINATONE
                // il DSI voleva i valori nativi). Facendolo QUI, una volta per
                // colore e solo quando la palette cambia, il resto del renderer
                // e il blit non devono swappare NIENTE.
                d[i] = (uint16_t)((c >> 8) | (c << 8));
            }
        } else if (src != off) {
            src += 0x400;                 // pagina saltata (0x200 word)
        }
    }
}

// ============================================================ grafica
// Espansione UNA TANTUM di gfx.bin (3 MB interlacciati) a 1 byte/pixel (6 MB).
// Struttura della region: 8 byte = 1 riga da 16 pixel a 4 bit,
//   byte 0..3 = piani 0..3 dei pixel 0..7   (bit MSB-first)
//   byte 4..7 = idem per i pixel 8..15
// da cui: tile 16x16 n -> riga n*16+y ; 8x8 n -> riga n*8+y meta' 0/1 ;
//         32x32 n -> righe n*64+2y e +1 (32 byte CONTIGUI).
static void gh_expand_gfx(const uint8_t *raw)
{
    uint8_t *d = gfxpx;
    memset(rowblank, 0, GFXROWS / 8);
    for (uint32_t r = 0; r < GFXROWS; r++) {
        const uint8_t *s = raw + (size_t)r * 8;
        uint8_t any = 0;
        for (int h = 0; h < 2; h++) {
            const uint8_t b0 = s[h * 4], b1 = s[h * 4 + 1], b2 = s[h * 4 + 2], b3 = s[h * 4 + 3];
            for (int x = 0; x < 8; x++) {
                const uint8_t m = (uint8_t)(0x80 >> x);
                const uint8_t v = (uint8_t)(((b3 & m) ? 8 : 0) | ((b2 & m) ? 4 : 0) |
                                            ((b1 & m) ? 2 : 0) | ((b0 & m) ? 1 : 0));
                *d++ = v;
                if (v != 15) any = 1;      // pen 15 = trasparente
            }
        }
        // riga interamente trasparente: il renderer la salta di netto. E' la
        // stessa vittoria del layer di testo di Black Tiger (2000us -> 121us).
        if (!any) rowblank[r >> 3] |= (uint8_t)(1 << (r & 7));
        // ⚠️ ADATTAMENTO: 393.216 giri di ciclo su 6 MB di PSRAM sono qualche
        // secondo pieno su un core solo. Senza un respiro l'idle task di questo
        // core non gira mai e scatta il watchdog. Su CABINATONE non c'era: la'
        // il binario e' di un gioco solo e la configurazione del WDT e' un'altra.
        if ((r & 0x3FFF) == 0x3FFF) vTaskDelay(1);
    }
}

static inline bool row_blank(uint32_t row)
{
    return (rowblank[row >> 3] >> (row & 7)) & 1;
}

// ============================================================ render tilemap
// Scan delle tre tilemap (cps1_v.cpp:2434): logico (col,row) -> offset in RAM.
static inline int scan1(int c, int r) { return (r & 0x1f) + ((c & 0x3f) << 5) + ((r & 0x20) << 6); }
static inline int scan2(int c, int r) { return (r & 0x0f) + ((c & 0x3f) << 4) + ((r & 0x30) << 6); }
static inline int scan3(int c, int r) { return (r & 0x07) + ((c & 0x3f) << 3) + ((r & 0x38) << 6); }

static void gh_draw_layer(int layer)
{
    int sz, colbase, type;
    uint32_t off = 0;
    int sx, sy;
    if (layer == 1) {
        sz = 8;  colbase = 0x20; type = GT_SCROLL1; off = cps_base(CPSA_SCROLL1, 0x4000);
        sx = cps_a[CPSA_SCR1X] + GH_XORG; sy = cps_a[CPSA_SCR1Y] + GH_YORG;
    } else if (layer == 2) {
        sz = 16; colbase = 0x40; type = GT_SCROLL2; off = cps_base(CPSA_SCROLL2, 0x4000);
        sx = cps_a[CPSA_SCR2X] + GH_XORG; sy = cps_a[CPSA_SCR2Y] + GH_YORG;
    } else {
        sz = 32; colbase = 0x60; type = GT_SCROLL3; off = cps_base(CPSA_SCROLL3, 0x4000);
        sx = cps_a[CPSA_SCR3X] + GH_XORG; sy = cps_a[CPSA_SCR3Y] + GH_YORG;
    }
    sx &= 0xffff; sy &= 0xffff;

    // row scroll dello SCROLL2 (videocontrol bit0): ogni riga dello schermo ha
    // il suo scrollx = scroll2x + other[(riga + rowscroll_offs) & 0x3ff]
    const bool rowscroll = (layer == 2) && (cps_a[CPSA_VIDEOCTRL] & 1);
    const uint32_t other_off = cps_base(CPSA_OTHER, 0x800);
    const int rs_offs = cps_a[CPSA_ROWSCROLL];

    const int shift = (sz == 8) ? 3 : (sz == 16 ? 4 : 5);
    const int mask  = sz - 1;

    // Col row scroll ogni riga ha un'origine diversa: si disegna per righe di
    // schermo. Senza, si va per griglia di tile (molto piu' economico).
    if (rowscroll) {
        for (int py = 0; py < GH_H; py++) {
            const int rsx = (sx + (int16_t)grw(other_off + (((py + rs_offs) & 0x3ff) * 2))) & 0xffff;
            const int ty  = ((sy >> shift) + ((sy & mask) + py) / sz) & 0x3f;
            const int oy  = ((sy & mask) + py) & mask;
            for (int tc = -1;; tc++) {
                const int px0 = tc * sz - (rsx & mask);
                if (px0 >= GH_W) break;
                if (px0 + sz <= 0) continue;
                const int tx  = ((rsx >> shift) + tc) & 0x3f;
                const int idx = scan2(tx, ty);
                const int code = grw(off + idx * 4), attr = grw(off + idx * 4 + 2);
                const int c = dm620_map(type, code);
                if (c < 0) continue;
                const uint32_t row = (uint32_t)c * 16 + ((attr & 0x40) ? (15 - oy) : oy);
                if (row_blank(row)) continue;
                const uint8_t *src = gfxpx + (size_t)row * 16;
                const uint16_t *pal = pal565 + (((attr & 0x1f) + colbase) * 16);
                uint16_t *dst = fb + (size_t)py * GH_W;
                for (int x = 0; x < sz; x++) {
                    const int px = px0 + x;
                    if ((unsigned)px >= GH_W) continue;
                    const uint8_t v = src[(attr & 0x20) ? (sz - 1 - x) : x];
                    if (v != 15) dst[px] = pal[v];
                }
            }
        }
        return;
    }

    const int oy0 = sy & mask, ox0 = sx & mask;
    for (int tr = 0; tr * sz - oy0 < GH_H; tr++) {
        const int ty = ((sy >> shift) + tr) & 0x3f;
        const int syy = tr * sz - oy0;
        for (int tc = 0; tc * sz - ox0 < GH_W; tc++) {
            const int tx = ((sx >> shift) + tc) & 0x3f;
            const int sxx = tc * sz - ox0;
            const int idx = (sz == 8) ? scan1(tx, ty) : (sz == 16 ? scan2(tx, ty) : scan3(tx, ty));
            const int code = grw(off + idx * 4);
            const int attr = grw(off + idx * 4 + 2);
            const int c = dm620_map(type, (sz == 32) ? (code & 0x3fff) : code);
            if (c < 0) continue;
            const uint16_t *pal = pal565 + (((attr & 0x1f) + colbase) * 16);
            const bool fx = attr & 0x20, fy = attr & 0x40;
            // per lo scroll1 la META' del tile 16x16 dipende dal bit 5 dell'INDICE
            // in mappa, non dal codice (get_tile0_info: gfxset = BIT(tile_index,5))
            const int half = (sz == 8) ? ((idx >> 5) & 1) : 0;

            const int y0 = (syy < 0) ? -syy : 0;
            const int y1 = (syy + sz > GH_H) ? (GH_H - syy) : sz;
            const int x0 = (sxx < 0) ? -sxx : 0;
            const int x1 = (sxx + sz > GH_W) ? (GH_W - sxx) : sz;
            for (int y = y0; y < y1; y++) {
                const int sry = fy ? (sz - 1 - y) : y;
                uint32_t row;
                const uint8_t *src;
                if (sz == 8)       { row = (uint32_t)c * 8 + sry;      src = gfxpx + (size_t)row * 16 + half * 8; }
                else if (sz == 16) { row = (uint32_t)c * 16 + sry;     src = gfxpx + (size_t)row * 16; }
                else               { row = (uint32_t)c * 64 + sry * 2; src = gfxpx + (size_t)row * 16; }
                if (row_blank(row) && (sz != 32 || row_blank(row + 1))) continue;
                uint16_t *dst = fb + (size_t)(syy + y) * GH_W + sxx;
                if (!fx) {
                    for (int x = x0; x < x1; x++) {
                        const uint8_t v = src[x];
                        if (v != 15) dst[x] = pal[v];
                    }
                } else {
                    for (int x = x0; x < x1; x++) {
                        const uint8_t v = src[sz - 1 - x];
                        if (v != 15) dst[x] = pal[v];
                    }
                }
            }
        }
    }
}

// ============================================================ render sprite
// Tabella oggetti: 8 byte per voce (x, y, codice, attributi), fine marcata da
// attributi == 0xff00. La RAM oggetti e' BUFFERIZZATA a vblank (cps1_objram_latch).
static void gh_draw_sprite16(int c, const uint16_t *pal, bool fx, bool fy, int sx, int sy)
{
    const int y0 = (sy < 0) ? -sy : 0;
    const int y1 = (sy + 16 > GH_H) ? (GH_H - sy) : 16;
    const int x0 = (sx < 0) ? -sx : 0;
    const int x1 = (sx + 16 > GH_W) ? (GH_W - sx) : 16;
    if (y0 >= y1 || x0 >= x1) return;
    for (int y = y0; y < y1; y++) {
        const uint32_t row = (uint32_t)c * 16 + (fy ? (15 - y) : y);
        if (row_blank(row)) continue;
        const uint8_t *src = gfxpx + (size_t)row * 16;
        uint16_t *dst = fb + (size_t)(sy + y) * GH_W + sx;
        for (int x = x0; x < x1; x++) {
            const uint8_t v = src[fx ? (15 - x) : x];
            if (v != 15) dst[x] = pal[v];
        }
    }
}

// Latch della tabella oggetti a vblank (cps1_objram_latch di MAME): il renderer
// deve vedere la tabella com'era a fine quadro, non mentre il gioco la riscrive.
// ⚠️ NON si puo' fare con un memcpy: nella gfxram le word sono BIG-ENDIAN,
// mentre objbuf e' un uint16_t* letto in little-endian dal RISC-V. Copiando i
// byte cosi' com'erano, x=0x01B7 diventava 0xB701 e TUTTI gli sprite finivano
// fuori schermo — il gioco si vedeva completo di fondali ma senza personaggi.
// (Trovato con l'harness offline sul PC, prima di flashare.)
static void gh_latch_obj(void)
{
    const uint8_t *s = gfxram + cps_base(CPSA_OBJ, 0x800);
    for (int i = 0; i < OBJ_SIZE / 2; i++)
        objbuf[i] = (uint16_t)((s[i * 2] << 8) | s[i * 2 + 1]);
}

static void gh_draw_sprites(void)
{
    for (int i = 0; i < OBJ_SIZE / 8; i++) {
        const uint16_t *e = objbuf + i * 4;
        const int colour = e[3];
        if ((colour & 0xff00) == 0xff00) break;      // marcatore di fine tabella
        const int x = e[0], y = e[1];
        int code = e[2];
        const int c0 = dm620_map(GT_SPRITES, code);
        if (c0 < 0) continue;
        const uint16_t *pal = pal565 + ((colour & 0x1f) * 16);
        const bool fx = colour & 0x20, fy = colour & 0x40;

        if (colour & 0xff00) {
            const int nx = ((colour & 0x0f00) >> 8) + 1;
            const int ny = ((colour & 0xf000) >> 12) + 1;
            for (int nys = 0; nys < ny; nys++) {
                const int sy = ((y + nys * 16) & 0x1ff) - GH_YORG;
                for (int nxs = 0; nxs < nx; nxs++) {
                    const int sx = ((x + nxs * 16) & 0x1ff) - GH_XORG;
                    // blocco: il codice avanza di 1 in orizzontale ma resta
                    // dentro la stessa "riga" di 16 (code & ~0xf), e di 0x10
                    // per ogni riga verticale (cps1_render_sprites)
                    int cc;
                    if (fx) cc = (code & ~0xf) + ((code + (nx - 1) - nxs) & 0xf);
                    else    cc = (code & ~0xf) + ((code + nxs) & 0xf);
                    cc += 0x10 * (fy ? (ny - 1 - nys) : nys);
                    const int c = dm620_map(GT_SPRITES, cc);
                    if (c >= 0) gh_draw_sprite16(c, pal, fx, fy, sx, sy);
                }
            }
        } else {
            gh_draw_sprite16(c0, pal, fx, fy,
                             (x & 0x1ff) - GH_XORG, (y & 0x1ff) - GH_YORG);
        }
    }
}

// ---------------------------------------------------------------- barra volume
// ⛔ NON si disegna piu' dentro `fb`. Lo faceva, e usciva SBAGLIATA per due
// motivi indipendenti (segnalati dall'utente il 2026-08-10 su 1944 e Out Run,
// ma il codice era lo stesso anche qui):
//   1. **POSIZIONE**: dentro `fb` la barra finisce nell'area del GIOCO, che sul
//      pannello parte a GH_X_OFF = 48 px dal bordo. Su tutti gli altri giochi la
//      barra sta invece a filo del bordo ALTO, nella banda nera.
//   2. **DIMENSIONE**: il blit riduce l'immagine in orizzontale (384->240),
//      quindi la barra usciva piu' corta e diversa da quella degli altri giochi
//      (che e' 112x16 px di PANNELLO).
// ✅ Ora la disegna `videoVolBarTop()` in pixel di pannello — vedi
// emulation/video.h. E' condivisa da 1944, Ghouls e Out Run apposta: erano tre
// copie che potevano divergere, ed e' proprio l'uniformita' il requisito.
// ⚠️ NON DEVE LAMPEGGIARE: garantito dal fatto che si ridisegna SOLO quando
// cambia qualcosa, e che la banda nera non la tocca nessun altro (sta fuori
// dalla finestra stretta del gioco) ([[feedback_vol_bar_no_flicker]]).
static void gh_window(int quale);        // definita sotto, insieme al blit

static void gh_vol_bar(void)
{
    static int last_fill = -2;           // -2 = mai disegnata, -1 = banda nera
    const bool on = input_is_vol_mode_active();
    const int  f  = on ? laserdisc_current_volume() : -1;
    if (f == last_fill) return;          // niente e' cambiato: nessuno SPI
    last_fill = f;
    videoVolBarTop(g_strip, on, on ? f : 30);
    gh_window(1);                        // ridai la finestra STRETTA al gioco
}

static void gh_render(void)
{
    gh_build_palette();

    // Sfondo: i giochi CPS-1 usano la pen 0xbff come colore di fondo
    // (bitmap.fill(0xbff) in screen_update_cps1).
    {
        const uint16_t bg = pal565[0xbff];
        uint32_t *p = (uint32_t *)fb;
        const uint32_t v2 = ((uint32_t)bg << 16) | bg;
        for (size_t i = 0; i < (size_t)GH_W * GH_H / 2; i++) p[i] = v2;
    }

    // ordine e abilitazione dei layer dal registro di controllo del CPS-B.
    // Le priorita' fini (maschere 0x28-0x2e) NON sono implementate: come su
    // Black Tiger e GnG, gli sprite restano sopra i tile. E' il limite noto del
    // sorgente, non una semplificazione fatta qui.
    const uint16_t ctrl = cps_b[CPSB_LAYERCTRL];
    const int order[4] = { (ctrl >> 6) & 3, (ctrl >> 8) & 3, (ctrl >> 10) & 3, (ctrl >> 12) & 3 };
    for (int i = 0; i < 4; i++) {
        const int l = order[i];
        if (l == 0) gh_draw_sprites();
        else if (l == 1 && (ctrl & 0x02)) gh_draw_layer(1);
        else if (l == 2 && (ctrl & 0x04)) gh_draw_layer(2);
        else if (l == 3 && (ctrl & 0x08)) gh_draw_layer(3);
    }

    // ⛔ la barra del volume NON sta piu' qui: vedi gh_vol_bar(), che la disegna
    // nella banda nera in pixel di pannello.
}

// ============================================================ bus del Z80
// Mappa del Z80 sonoro (sub_map di cps1.cpp:631):
//   0000-7fff ROM fissa          8000-bfff ROM bancata ($F004 bit0)
//   d000-d7ff RAM                f000/f001 YM2151 (indirizzo/dato; f001 in
//                                lettura = STATO)
//   f002 OKI (assente)           f004 banco    f006 pin7 OKI (assente)
//   f008 soundlatch              f00a soundlatch 2 ("sound timer fade")
// ⚠️ ADATTAMENTO: su CABINATONE queste due erano i campi di una `Z80Bus`
// (z80_bus.cpp con le globali). Qui il core e' incluso DENTRO questa unita' di
// compilazione coi simboli Gh* (vedi i #define in testa), quindi le sue globali
// si definiscono qui — la stessa forma di spaceace.cpp e cps1944.cpp. Il CORPO
// delle due funzioni e' identico carattere per carattere.
extern "C" byte RdZ80(word a)
{
    if (a < 0x8000)  return snd_rom[a];
    if (a < 0xc000)  return snd_rom[0x10000 + snd_bank * 0x4000 + (a - 0x8000)];
    if (a >= 0xd000 && a < 0xd800) return sndram[a - 0xd000];
    if (a == 0xf001) return (uint8_t)ym2151_read_status(ym);
    if (a == 0xf002) return 0;              // OKI assente: stato "non suona"
    if (a == 0xf008) return soundlatch;
    if (a == 0xf00a) return soundlatch2;
    return 0xff;
}

extern "C" void WrZ80(word a, byte v)
{
    if (a >= 0xd000 && a < 0xd800) { sndram[a - 0xd000] = v; return; }
    if (a == 0xf000) { ym_addr = v; return; }
    if (a == 0xf001) { ym2151_write_reg(ym, ym_addr, v); return; }
    if (a == 0xf004) { snd_bank = v & 1; return; }
    // f002/f006 = OKI: nessuna ROM di campioni in questo set, scritture inerti
}

// Prelievo dell'OPCODE: il core lo DICHIARA e basta, lo definisce il gioco
// (spaceace.cpp e cps1944.cpp fanno lo stesso). Sul CPS-1 e' la stessa lettura
// del bus — infatti CABINATONE passava `gh_snd_rd` anche come quinto campo di
// Z80Bus.
extern "C" unsigned char OpZ80_INL(unsigned short a) { return RdZ80((word)a); }
extern "C" byte InZ80(word p)            { (void)p; return 0xff; }
extern "C" void OutZ80(word p, byte v)   { (void)p; (void)v; }
extern "C" word LoopZ80(Z80 *R)          { (void)R; return INT_NONE; }
extern "C" void PatchZ80(Z80 *R)         { (void)R; }

// ============================================================ audio
static void gh_audio_init(void)
{
    // ⚠️ Tutti e tre sono minuscoli (512 + 512 + 256 byte) e battuti 32.000
    // volte al secondo: RAM INTERNA con fallback in PSRAM, cosi' 768 byte che
    // non entrano non possono spegnere l'audio ([[feedback_audio_muto_e_ram_interna]]).
    if (!ym_l)    ym_l    = (int32_t *)heap_caps_calloc(AUD_CHUNK, 4, MALLOC_CAP_INTERNAL);
    if (!ym_l)    ym_l    = (int32_t *)heap_caps_calloc(AUD_CHUNK, 4, MALLOC_CAP_SPIRAM);
    if (!ym_r)    ym_r    = (int32_t *)heap_caps_calloc(AUD_CHUNK, 4, MALLOC_CAP_INTERNAL);
    if (!ym_r)    ym_r    = (int32_t *)heap_caps_calloc(AUD_CHUNK, 4, MALLOC_CAP_SPIRAM);
    if (!aud_out) aud_out = (int16_t *)heap_caps_calloc(AUD_CHUNK * 2, 2, MALLOC_CAP_INTERNAL);
    if (!aud_out) aud_out = (int16_t *)heap_caps_calloc(AUD_CHUNK * 2, 2, MALLOC_CAP_SPIRAM);
    if (!ym_l || !ym_r || !aud_out) return;

    ym = ym2151_init(YM_CLOCK, AUD_RATE);
    if (!ym) return;
    ym2151_reset_chip(ym);

    ResetZ80(&sndcpu);
    sndcpu.IRequest = INT_NONE;
    sndcpu.IAutoReset = 1;
    sndcpu.TrapBadOps = 0;

    audio_ok = true;
}

// Task audio sul CORE 0 (l'emulazione sta sul core 1). E' l'audio a dare il
// tempo al Z80 sonoro, non il video: laserdisc_audio_write e' BLOCCANTE finche'
// il DMA non consuma, quindi i 32000 campioni/s scandiscono da soli il clock del
// chip e della CPU sonora. Lezione di Hyper Sports: se lo ritma il video, la
// musica rallenta quando calano gli fps.
// ⭐ GUADAGNO + SOFT LIMITER. Era `AUD_GAIN 3/4`, scelto perche' 32767*3/4 non
// puo' MAI clippare — ma il picco vero misurato sull'harness e' 16852, cioe'
// -5,8 dBFS, e i giochi normali di questo firmware arrivano a fondo scala
// (`value * 128 / volumeSetting` con volumeSetting = 1 satura). Mancavano ~6 dB,
// lo stesso difetto segnalato dall'utente su 1944 e Out Run. Ora il guadagno e'
// 3/2 e le punte le tiene un SOFT LIMITER invece del clamp secco (che si sente
// come "gracchio"). Soglia uguale a quella di laserdisc.cpp, tarata sull'orecchio.
#define GH_BOOST_NUM 3
#define GH_BOOST_DEN 2
#define GH_LIM_T     24000

static void gh_audio_task(void *arg)
{
    (void)arg;
    laserdisc_audio_begin();
    uint32_t acc = 0;
    uint32_t respiro = 0;                 // contatore per lo yield anti-watchdog
    while (!g_stop) {
        // 1) il Z80 sonoro avanza del tempo del blocco, a fette di 8 campioni,
        //    iniettando l'IRQ del timer. A LIVELLO, non a fronte: il gestore
        //    azzera il flag scrivendo il registro $14, e finche' il flag c'e'
        //    la linea resta asserita. IntZ80 azzera da solo IFF_1.
        for (int s = 0; s < AUD_CHUNK; s += 8) {
            acc += Z80_CYC_PER_SAMPLE_FP * 8;
            const int cyc = (int)(acc >> 16);
            acc &= 0xffff;
            ExecZ80(&sndcpu, cyc);
            if ((ym2151_read_status(ym) & 0x03) && (sndcpu.IFF & IFF_1))
                IntZ80(&sndcpu, INT_RST38);
        }

        // 2) il chip genera il blocco (qui dentro avanzano i suoi timer)
        SAMP *bufs[2] = { (SAMP *)ym_l, (SAMP *)ym_r };
        ym2151_update_one(ym, bufs, AUD_CHUNK);

        // ⛔⛔ VOLUME + GUADAGNO. Due cose diverse, entrambe mancanti prima:
        // 1. il VOLUME non lo applicava nessuno. `laserdisc_audio_write()` scrive
        //    i campioni sull'I2S tali e quali, non conosce `audio.volumeSetting`:
        //    lo applica ogni player nel proprio mix (DL, Space Ace, DL2 lo fanno).
        // 2. il livello era comunque basso: picco misurato 16852 su 32767, cioe'
        //    -5,8 dBFS, contro i giochi normali che saturano.
        // ⭐ Guadagno + SOFT LIMITER, la forma di laserdisc.cpp e dl2.cpp.
        const int vs  = laserdisc_current_volume();
        const int vol = (vs >= 30) ? 0 : (31 - vs);        // 0..30, 30 = barra piena
        // ⚡ UN moltiplicatore per BLOCCO, virgola fissa 8.8: dentro il ciclo
        // restano una moltiplicazione e uno shift, nessuna divisione per campione.
        const int mul = (GH_BOOST_NUM * vol * 256) / (GH_BOOST_DEN * 30);
        int in_max = (32767 * mul) >> 8;
        if (in_max <= GH_LIM_T) in_max = GH_LIM_T + 1;
        // 3) mix stereo -> I2S. La write blocca: e' il nostro orologio.
        for (int i = 0; i < AUD_CHUNK; i++) {
            int l = (ym_l[i] * mul) >> 8;
            int r = (ym_r[i] * mul) >> 8;
            { int a = (l < 0) ? -l : l;
              if (a > GH_LIM_T) {
                  a = GH_LIM_T + ((a - GH_LIM_T) * (32767 - GH_LIM_T)) / (in_max - GH_LIM_T);
                  if (a > 32767) a = 32767;
                  l = (l < 0) ? -a : a;
              } }
            { int a = (r < 0) ? -r : r;
              if (a > GH_LIM_T) {
                  a = GH_LIM_T + ((a - GH_LIM_T) * (32767 - GH_LIM_T)) / (in_max - GH_LIM_T);
                  if (a > 32767) a = 32767;
                  r = (r < 0) ? -a : a;
              } }
            if (l >  32767) l =  32767; if (l < -32768) l = -32768;
            if (r >  32767) r =  32767; if (r < -32768) r = -32768;
            aud_out[i * 2]     = (int16_t)l;
            aud_out[i * 2 + 1] = (int16_t)r;
        }
        laserdisc_audio_write(aud_out, AUD_CHUNK);
        // ⛔⛔ RESPIRO OBBLIGATORIO — e' la rete che evita il RIAVVIO. Finche' il
        // task sta in tempo reale la write BLOCCA e il task dorme; se resta
        // indietro la write non blocca piu' e questo ciclo gira al 100% a
        // priorita' 6 sul core 0, l'idle non viene mai schedulato e dopo 5 s
        // scatta il task watchdog -> la scheda si RIAVVIA (difetto visto
        // dall'utente su 1944 e Out Run il 2026-08-10).
        if ((++respiro & 15) == 0) vTaskDelay(1);
    }
    laserdisc_audio_end();
    g_aud_running = false;
    vTaskDelete(nullptr);
}

// ============================================================ input
// ⚠️ ADATTAMENTO: bt_get_buttons() -> g_input->buttons_get(), maschere BUTTON_*.
// La mappa dei bit di IN0/IN1 e' quella del sorgente, invariata.
static void gh_read_inputs(void)
{
    const uint8_t b = g_input->buttons_get();
    uint8_t i0 = 0xFF, i1 = 0xFF;
    // IN1 (cps1_2b_4way): b0 DESTRA, b1 SINISTRA, b2 GIU', b3 SU,
    //                     b4 BUTTON1 (spara), b5 BUTTON2 (salta)
    if (b & BUTTON_RIGHT) i1 &= ~0x01;
    if (b & BUTTON_LEFT)  i1 &= ~0x02;
    if (b & BUTTON_DOWN)  i1 &= ~0x04;
    if (b & BUTTON_UP)    i1 &= ~0x08;
    if (b & BUTTON_FIRE)  i1 &= ~0x10;
    if (b & BUTTON_EXTRA) i1 &= ~0x20;
    // IN0: b0 COIN1, b4 START1 (b6 = interruttore SERVICE, lasciato a riposo)
    if (b & BUTTON_COIN)  i0 &= ~0x01;
    if (b & BUTTON_START) i0 &= ~0x10;
    in0 = i0; in1_lo = i1;
}

// ============================================================ un fotogramma
static void gh_run_frame(void)
{
    m68k_set_irq(2);                    // vblank -> IPL1 = IRQ2
    m68k_execute(CYC_FRAME);
    gh_latch_obj();      // tabella oggetti bufferizzata (vedi la nota sul big-endian)
}

// ============================================================ blit sul pannello
// ⛔ QUI NON SI COPIA DA CABINATONE: la' c'erano un pannello DSI 1024x600, il PPA
// hardware e lcd_draw_bitmap, che qui NON esistono. Il modello e' c1944_blit()
// di cps1944.cpp, che gira su QUESTA scheda con la STESSA risoluzione nativa
// (CPS-1 e CPS-2 sono entrambi 384x224).
//
// ⭐⭐ GEOMETRIA — LA STESSA DI BOMB JACK E DI 1944. Il pannello e' montato in
// PORTRAIT ma si pilota TRASPOSTO: una "riga" scritta e' larga TFT_FB_W=320 e ce
// ne sono LD_FB_H=240. In coordinate UTENTE (portrait 240x320):
//     x_l = y_p        y_l = 239 - x_p
// cioe' l'asse da 320 e' il VERTICALE visto dall'utente e quello da 240
// l'ORIZZONTALE. Quindi:
//     x_l = GH_X_OFF + game_y      (224 righe di gioco nei 320 verticali)
//     y_l = 239 - x_p,  x_p = game_x ridotto da 384 a 240
// Risultato: il gioco esce DIRITTO, con le BANDE NERE sopra e sotto (48 px).
//
// ⚠️ ASPECT SACRIFICATO DI PROPOSITO, come su 1944: 384 -> 240 e' un fattore
// 0,625 e tenendo le 224 righe intere l'immagine e' schiacciata in orizzontale.
// L'alternativa (224*0,625 = 140 righe) rispetterebbe l'aspect ma butterebbe 84
// righe su 224, e in Ghouls il punteggio, il tempo e la barra dell'armatura sono
// alti 8 px: diventerebbero illeggibili. In questo firmware NESSUN gioco
// ricampiona in verticale.
//
// ⚠️ Riduzione 384 -> 240: si tengono 5 pixel ogni 8 (48 gruppi tondi). Gli
// indici tenuti sono quelli di round(k*1,6) = 0,2,3,5,6, cioe' gli scarti sono
// SPARSI e non tre di fila. Nessuna divisione resta nel ciclo caldo: le 8 colonne
// sorgente di una striscia si calcolano UNA volta per striscia.
//
// ⭐ ORDINE DEI CICLI = ACCESSI ALLA PSRAM ([[feedback_ottimizzare_contando_accessi]]).
// Trasporre vuol dire che una RIGA del pannello e' una COLONNA di `fb`: presa
// riga per riga sarebbero 240x224 = 53.760 letture a passo 768 byte, cioe' una
// cache miss ciascuna. Qui il ciclo esterno e' la riga di `fb` (che si percorre
// in ORDINE) e dentro si riempiono TUTTE E OTTO le righe della striscia.
static void gh_blit(void)
{
    // indici tenuti dentro ogni gruppo di 8 pixel sorgente (round(k*1,6))
    static const uint8_t KEEP[5] = { 0, 2, 3, 5, 6 };

    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        // ⭐ NIENTE memset: con la finestra STRETTA (224 colonne) il ciclo qui
        // sotto copre TUTTE le 224*8 posizioni della striscia. Le bande nere
        // stanno FUORI dalla finestra e le ha gia' dipinte gh_bande_nere().

        // le 8 colonne di `fb` servite da questa striscia (una per riga scritta)
        int gx8[8];
        for (int i = 0; i < 8; i++) {
            const int x_p = (LD_FB_H - 1) - (sy + i);   // 0..239 = colonna UTENTE
            gx8[i] = (x_p / 5) * 8 + KEEP[x_p % 5];     // 0..383 = colonna di fb
        }

        const uint16_t *src = fb;                       // riga 0 del gioco
        uint16_t *dst = g_strip;   // finestra STRETTA: la striscia parte dal gioco
        for (int gy = 0; gy < GH_H; gy++) {
            dst[0]         = src[gx8[0]];
            dst[1 * GH_H]  = src[gx8[1]];
            dst[2 * GH_H]  = src[gx8[2]];
            dst[3 * GH_H]  = src[gx8[3]];
            dst[4 * GH_H]  = src[gx8[4]];
            dst[5 * GH_H]  = src[gx8[5]];
            dst[6 * GH_H]  = src[gx8[6]];
            dst[7 * GH_H]  = src[gx8[7]];
            src += GH_W;                                // riga successiva di fb
            dst += 1;                                   // colonna successiva
        }
        video.write(g_strip, GH_H * 8);   // finestra STRETTA: 224, non 320
    }
    // ⚠️ video.write() e' ASINCRONA e lascia una transazione SPI in volo: si
    // drena qui, UNA volta per frame.
    video.waitDMA();
}

// ---------------------------------------------------------------- schermo
// 2 = tutta la finestra del player (320x240): pagine di stato e bande nere.
// 1 = SOLO le 224 colonne che portano gioco, saltando le bande.
// 0 = si restituisce la finestra al menu.
static void gh_window(int quale) {
    video.waitDMA();
    if (quale == 2)      video.setAddrWindow(TFT_HW_X, 0, TFT_FB_W, LD_FB_H);
    // ⭐⭐ SI SPEDISCE MENO: dei 320 pixel solo 224 portano gioco, 96 colonne
    // sono banda nera SEMPRE IDENTICA. Rispedirle ad ogni quadro sarebbe il 30%
    // del traffico SPI buttato (misura fatta su 1944: BLT da 17,1 a 12,3 ms).
    // Le bande si dipingono UNA VOLTA all'avvio e non si toccano piu'.
    else if (quale == 1) video.setAddrWindow(TFT_HW_X + GH_X_OFF, 0, GH_H, LD_FB_H);
    else                 video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// Dipinge di nero TUTTA la finestra del player. Si chiama una volta sola, prima
// di stringere la finestra sul gioco: da li' in poi le bande restano com'erano.
static void gh_bande_nere(void) {
    gh_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    video.waitDMA();
}

// Testo SCALATO nella strip ruotata: stessa geometria di c1944_line.
static void gh_line(int sy, const char *s, int y_p, int scale, uint16_t col) {
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

static const uint16_t C_YELLOW = 0xE0FF;   // byte-swapped, come DL/Space Ace/1944
static const uint16_t C_CYAN   = 0xFF07;
static const uint16_t C_GREY   = 0xEF7B;
static const uint16_t C_RED    = 0x00F8;

// pagina a 5 righe sotto il titolo
static void gh_page(const char *l[5], const uint16_t c[5])
{
    static const int Y[5] = { 150, 172, 194, 216, 250 };
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        gh_line(sy, "GHOULS", 96, 3, C_YELLOW);
        for (int k = 0; k < 5; k++)
            if (l[k] && l[k][0]) gh_line(sy, l[k], Y[k], 1, c[k]);
        video.write(g_strip, TFT_FB_W * 8);
    }
    // ⚠️ subito dopo una pagina di stato si legge quasi sempre dalla SD:
    // video.write lascia una transazione SPI in volo e video.h chiede di
    // drenarla prima di toccare la SD.
    video.waitDMA();
}

static void gh_msg(const char *a, const char *b, uint16_t col)
{
    const char *l[5] = { "N GHOSTS", a, b, "", "START 3s ESCI" };
    const uint16_t c[5] = { C_YELLOW, col, C_GREY, C_GREY, C_GREY };
    gh_page(l, c);
}

// Schermata di CARICAMENTO: solo il nome del gioco e una riga. Niente
// avanzamento file per file — e' la convenzione del progetto
// ([[cabinatone_scritta_loading]], qui nella forma delle pagine dello SPINNERINO).
// ⚠️ Le schermate di ERRORE restano (gh_msg in rosso): non sono debug, sono
// l'unico modo di sapere cosa manca sulla SD.
static void gh_caricamento(void)
{
    const char *l[5] = { "N GHOSTS", "", "CARICAMENTO DA SD", "", "" };
    const uint16_t c[5] = { C_YELLOW, C_GREY, C_CYAN, C_GREY, C_GREY };
    gh_page(l, c);
}

// ---------------------------------------------------------------- verifica pack
// Si controlla la TAGLIA, non il contenuto: il CRC lo ha gia' fatto il converter
// sul PC (romconv_ghouls.py verifica i 26 CRC del set contro MAME).
static bool gh_probe(void) {
    g_ok = 0; g_primo_ko[0] = 0;
    for (int i = 0; i < GH_NFILE; i++) {
        char p[64];
        snprintf(p, sizeof p, GH_DIR "/%s", GH_FILES[i].nome);
        FILE *f = fopen(p, "rb");
        if (!f) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "MANCA %s", GH_FILES[i].nome);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if ((uint32_t)n != GH_FILES[i].byte) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "TAGLIA %s", GH_FILES[i].nome);
            continue;
        }
        g_ok++;
    }
    return g_ok == GH_NFILE;
}

// carica un file intero dalla SD in un buffer gia' allocato
static bool gh_load_file(const char *nome, uint8_t *dst, uint32_t n)
{
    char p[64];
    snprintf(p, sizeof p, GH_DIR "/%s", nome);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    // a blocchi: una fread da 3 MB su FATFS non e' una buona idea, e cosi' si
    // puo' anche cedere la CPU fra un pezzo e l'altro
    uint32_t fatti = 0;
    unsigned pezzi = 0;
    while (fatti < n) {
        uint32_t q = n - fatti; if (q > 32768) q = 32768;
        if (fread(dst + fatti, 1, q, f) != q) { fclose(f); return false; }
        fatti += q;
        // ⛔ RESPIRO IN CARICAMENTO: qui si leggono 4,09 MB e subito dopo si
        // espandono 3 MB di grafica in 6. Se l'idle del core non gira per 5 s
        // scatta il task watchdog e la scheda si riavvia MENTRE CARICA — sintomo
        // visto su 1944 e Out Run ([[feedback_task_audio_indietro_watchdog]]).
        if ((++pezzi & 7) == 0) vTaskDelay(1);
    }
    fclose(f);
    return true;
}

// ---------------------------------------------------------------- allocazioni
// ⭐⭐ IL CONTO DELLA RAM INTERNA, fatto PRIMA di consegnare come chiede la
// regola ([[feedback_audio_muto_e_ram_interna]]). Baseline del binario:
// 109.912 byte interni liberi (riga "Le variabili globali usano..." del
// compilatore). Ghouls chiede in interna:
//     tl_tab del YM2151      26.624   (dentro ym2151.c, fallback PSRAM)
//     struct YM2151 (malloc)  ~3.500
//     pal565                   6.144
//     objbuf                   2.048
//     sndram                   2.048
//     ym_l + ym_r + aud_out      768
//                            -------
//                            ~41.100  ->  restano ~68 KB
// ⛔ `fb` (168 KB) e `mainram` (64 KB) NON entrano: su CABINATONE `fb` va in
// interna, ma la' il binario e' di UN gioco solo e ha ~305 KB. Qui restano in
// PSRAM, `fb` allineato a 128 (la linea di cache: una riga e' 384*2 = 768 byte =
// 6 linee esatte, quindi con la base allineata lo e' ogni riga).
// ⚠️ Il canale I2S e i suoi descrittori DMA sono aperti dal FIRMWARE al boot
// (audio.cpp), non da laserdisc_audio_begin(): a differenza di CABINATONE qui
// non c'e' un'allocazione DMA tardiva da proteggere. La riserva serve agli stack
// dei task e allo stack BT.
#define GH_RISERVA_INT (48 * 1024)

// ⭐ Buffer PICCOLO e CALDO: interna se resta spazio oltre la riserva, PSRAM
// altrimenti. Meglio restare in PSRAM che muti.
static void *gh_calloc_caldo(size_t n)
{
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + GH_RISERVA_INT) {
        void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
        if (p) return p;
    }
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

// ⭐⭐ ALLINEATO A 128 IN PSRAM: e' la linea di cache, e `fb` e' il buffer con
// piu' accessi per frame (~86.000 scritture del renderer + 53.760 letture del
// blit). Con la base disallineata OGNI riga ne sporca una in testa e una in coda.
static void *gh_psram_allineata(size_t n)
{
    const size_t na = (n + 127) & ~(size_t)127;
    void *p = heap_caps_aligned_alloc(128, na, MALLOC_CAP_SPIRAM);
    if (p) memset(p, 0, na);
    return p;
}

static bool gh_alloc(const char **perche)
{
    if (!pal565) pal565 = (uint16_t *)gh_calloc_caldo(PAL_ENTRIES * 2);
    if (!objbuf) objbuf = (uint16_t *)gh_calloc_caldo(OBJ_SIZE);
    if (!sndram) sndram = (uint8_t  *)gh_calloc_caldo(SNDRAM_SIZE);
    if (!fb)     fb     = (uint16_t *)gh_psram_allineata((size_t)GH_W * GH_H * 2);

    struct { void **p; size_t n; const char *nome; } B[] = {
        { (void **)&prg,      PRG_SIZE,        "code.bin"  },
        { (void **)&gfxpx,    GFXPX_SIZE,      "GFX"       },
        { (void **)&rowblank, GFXROWS / 8,     "ROWBLANK"  },
        { (void **)&gfxram,   GFXRAM_SIZE,     "GFXRAM"    },
        { (void **)&mainram,  MAINRAM_SIZE,    "RAM"       },
        { (void **)&snd_rom,  SND_ROM_SIZE,    "snd.bin"   },
        { &m68k_jump,         M68K_JUMP_SIZE,  "TAB SALTI" },
        { &m68k_cyc,          M68K_CYC_SIZE,   "TAB CICLI" },
        { (void **)&objbuf,   OBJ_SIZE,        "OBJ"       },
        { (void **)&pal565,   PAL_ENTRIES * 2, "PALETTE"   },
        { (void **)&sndram,   SNDRAM_SIZE,     "SND RAM"   },
        { (void **)&fb,       GH_W * GH_H * 2, "FB"        },
    };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++) {
        if (*B[i].p) continue;
        *B[i].p = heap_caps_calloc(1, B[i].n, MALLOC_CAP_SPIRAM);
        if (!*B[i].p) { *perche = B[i].nome; return false; }
    }
    return true;
}

static void gh_free(void)
{
    // ⛔ PRIMA di liberare `prg`: la finestra diretta dello spazio opcode punta
    // dentro quel buffer. Lasciarla accesa dopo la free significherebbe che il
    // prossimo gioco a 68000 del binario preleva le istruzioni da memoria
    // liberata. Spegnerla e' una riga; non spegnerla e' un crash silenzioso.
    m68k_set_opcode_window(nullptr, 0);
    // l'idle-skip qui non e' mai stato acceso, ma lo si spegne comunque: e' lo
    // stato che il prossimo gioco a 68000 si aspetta.
    m68k_set_idle_skip(1, 0);            // lo > hi = spento

    // ✅ ym2151_shutdown() ora restituisce ANCHE la `tl_tab` da 26 KB di RAM
    // INTERNA (`YM2151_LIBERA_TL_TAB` in cpus/ym2151/ym2151.c). Qui c'era
    // scritto «un costo UNA TANTUM»: giusto come cifra, ma quei 26 KB mancano
    // al gioco SUCCESSIVO — e col Neo Geo bastano a spingere il framebuffer da
    // 176 KB in PSRAM, con video e audio piu' lenti e nessun messaggio.
    // Stessa cura in outrun.cpp.
    if (ym) { ym2151_shutdown(ym); ym = nullptr; }

    void *B[] = { prg, gfxpx, rowblank, gfxram, mainram, snd_rom,
                  m68k_jump, m68k_cyc, objbuf, pal565, sndram, fb,
                  ym_l, ym_r, aud_out };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++)
        if (B[i]) heap_caps_free(B[i]);
    prg = gfxpx = rowblank = gfxram = mainram = snd_rom = nullptr;
    m68k_jump = m68k_cyc = nullptr;
    objbuf = nullptr; pal565 = nullptr; sndram = nullptr; fb = nullptr;
    ym_l = ym_r = nullptr; aud_out = nullptr;
    audio_ok = false;
}

// ---------------------------------------------------------------- uscita
static bool gh_want_exit(uint8_t b) {
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & BUTTON_START) {
        uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}

// ---------------------------------------------------------------- task (CPU1)
static void gh_task(void *arg) {
    (void)arg;
    gh_window(2);

    // ---- SD + pack ----
    if (!(sd_is_mounted() ? true : sd_mount())) {
        while (!g_stop) { gh_msg("SD NON MONTATA", "", C_RED);
                          if (gh_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    if (!gh_probe()) {
        while (!g_stop) { gh_msg(g_primo_ko[0] ? g_primo_ko : "DATI MANCANTI",
                                 "SERVE /GHOULS SU SD", C_RED);
                          if (gh_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    {
    // ⛔ Qui c'era `gh_audio_init()`, spostato in avanti e poi TOLTO lo stesso
    // giorno: la misura sul device ha detto 156-183 KB di RAM interna libera,
    // quindi non c'era nessuna gara da vincere. Vedi la stessa nota in
    // outrun.cpp — se la misura smentisce il MOTIVO di una modifica, la
    // modifica si toglie.
    // ---- memoria ----
    const char *perche = "";
    gh_caricamento();
    if (!gh_alloc(&perche)) {
        while (!g_stop) { char l[32]; snprintf(l, sizeof l, "PSRAM KO: %s", perche);
                          gh_msg(l, "", C_RED);
                          if (gh_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- ROM del 68000 ----
    memset(prg, 0xff, PRG_SIZE);
    if (!gh_load_file("code.bin", prg, PRG_SIZE)) {
        while (!g_stop) { gh_msg("LETTURA CODE.BIN KO", "", C_RED);
                          if (gh_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    // ⭐ PATCH DI MAME: il self-test di ghouls dura un'eternita' (con il ROM
    // integro l'harness misurava 21000 istr/frame in un loop di 153 PC e nessun
    // disegno). MAME lo salta cosi': rom[0x61964/2] = JMP $00000400.
    prg[0x61964] = 0x4e; prg[0x61965] = 0xf9;
    prg[0x61966] = 0x00; prg[0x61967] = 0x00;
    prg[0x61968] = 0x04; prg[0x61969] = 0x00;

    // ---- grafica: 3 MB grezzi -> 6 MB espansi a 1 byte/pixel ----
    // ⚠️ i 3 MB grezzi servono solo qui: si allocano, si espandono e si liberano
    // SUBITO. Il picco di PSRAM e' quindi ~11 MB, non permanente.
    {
        uint8_t *raw = (uint8_t *)heap_caps_malloc(GFXROM_SIZE, MALLOC_CAP_SPIRAM);
        if (!raw) {
            while (!g_stop) { gh_msg("PSRAM KO: GFX GREZZA", "", C_RED);
                              if (gh_want_exit(g_input->buttons_get())) break;
                              vTaskDelay(pdMS_TO_TICKS(60)); }
            goto uscita;
        }
        if (!gh_load_file("gfx.bin", raw, GFXROM_SIZE)) {
            heap_caps_free(raw);
            while (!g_stop) { gh_msg("LETTURA GFX.BIN KO", "", C_RED);
                              if (gh_want_exit(g_input->buttons_get())) break;
                              vTaskDelay(pdMS_TO_TICKS(60)); }
            goto uscita;
        }
        gh_expand_gfx(raw);
        heap_caps_free(raw);
    }

    // ---- ROM del Z80 sonoro ----
    if (!gh_load_file("snd.bin", snd_rom, SND_ROM_SIZE)) {
        while (!g_stop) { gh_msg("LETTURA SND.BIN KO", "", C_RED);
                          if (gh_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- stato iniziale ----
    gh_init_bright();
    memset(cps_a, 0, sizeof(cps_a));
    memset(cps_b, 0, sizeof(cps_b));
    memset(mainram, 0, MAINRAM_SIZE);
    memset(gfxram, 0, GFXRAM_SIZE);
    memset(&sndcpu, 0, sizeof sndcpu);
    snd_bank = 0; ym_addr = 0;
    soundlatch = soundlatch2 = 0xFF;
    in0 = in1_lo = 0xFF; dswa = dswb = dswc = 0xFF;
    pal_dirty = true;

    // ---- 68000 ----
    // ⚠️ m68k_set_tables() PRIMA di m68k_init(): e' init che costruisce la
    // tabella dei salti, e senza puntatori validi scriverebbe a indirizzo 0.
    m68k_bus_set(&GH_BUS);          // le callback globali stanno in m68k_bus.cpp
    m68k_set_tables(m68k_jump, m68k_cyc);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    // ⭐⭐ FINESTRA DIRETTA DELLO SPAZIO OPCODE (vedi m68k.h).
    // ⭐ PROVA DI EQUIVALENZA, non "dovrebbe andare": il primo ramo di rd16()
    // e' `if (a < PRG_SIZE) return (prg[a]<<8)|prg[a+1]`, cioe' ESATTAMENTE
    // un'immagine lineare big-endian di `prg` per gli indirizzi 0..PRG_SIZE-1;
    // e sul CPS-1 lo spazio opcode E' lo spazio dati (nessuna cifratura), tanto
    // che GH_BUS lascia p8/p16/p32 a nullptr. La scorciatoia restituisce quindi
    // gli stessi due byte nello stesso ordine, e fuori dalla finestra si torna
    // sul percorso di prima.
    // ⚠️ DOPO la patch anti self-test qui sopra: la finestra punta a `prg`, che
    // e' lo stesso buffer, quindi la patch e' visibile da entrambe le strade.
    m68k_set_opcode_window(prg, PRG_SIZE);
    m68k_pulse_reset();

    // ---- audio: chip + Z80 sonoro su un TASK PROPRIO, core 0 ----
    // Init QUI, a ROM caricate: posizione originale. Se fallisce si gioca muti.
    gh_audio_init();
    if (audio_ok) {
        g_aud_running = true;
        // prio 6: sta quasi sempre fermo dentro la write bloccante sull'I2S,
        // quindi dorme e non affama l'emulazione su CPU1.
        // ⛔ 16 KB, non 8: qui girano un Z80 interpretato e il YM2151. Vedi la
        // stessa nota in outrun.cpp. L'esito VA CONTROLLATO: se la create
        // fallisce e `g_aud_running` resta true, il gioco parte muto senza
        // avviso e ogni uscita spreca i 2 s pieni di attesa.
        if (xTaskCreatePinnedToCore(gh_audio_task, "gh_audio", 16384,
                                    nullptr, 6, nullptr, 0) != pdPASS)
            g_aud_running = false;
    } else {
        // rete di sicurezza permanente: due secondi di avviso, poi si gioca muti
        gh_msg("AUDIO KO", "RAM INTERNA FINITA?", C_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // ⭐ Le bande nere si dipingono UNA VOLTA, poi la finestra si stringe sulle
    // sole 224 colonne del gioco: da qui in avanti lo SPI non trasporta piu' un
    // solo pixel di nero.
    gh_bande_nere();
    gh_window(1);

    // ---- il ciclo di gioco -------------------------------------------------
    {
    uint32_t fcount = 0;
    // ⭐⭐ SCADENZA CUMULATIVA, non "riparto da adesso ogni giro": col video a
    // frame alterni il quadro leggero finisce in anticipo e quello pesante resta
    // indietro. Ripartendo da capo l'anticipo verrebbe BUTTATO; accumulando, e'
    // la MEDIA a dover stare nel budget. (Misurato su 1944: dal 74% al ~96%.)
    int64_t next_us = esp_timer_get_time();
    while (!g_stop) {
        gh_read_inputs();
        gh_run_frame();
#if GH_VIDEO_HALF_RATE
        if ((fcount & 1) == 0)
#endif
        { gh_render(); gh_blit(); }
        // barra del volume nella banda nera: si ridisegna solo se e' cambiata
        gh_vol_bar();

        if (gh_want_exit(g_input->buttons_get())) break;

        // ritmo: si dorme il grosso dell'attesa e si aspetta a vuoto solo
        // l'ultimo millisecondo, cosi' il frame non scivola in avanti.
        next_us += FRAME_US;
        const int64_t due = next_us;
        const int64_t left = due - esp_timer_get_time();
        if (left > 2000) vTaskDelay(pdMS_TO_TICKS((left - 1000) / 1000));
        while (esp_timer_get_time() < due) { }
        // ⚠️ se si resta indietro in modo CRONICO (piu' di 4 quadri) non si
        // accumula debito all'infinito: si riparte da adesso. Senza, dopo una
        // pausa lunga il gioco correrebbe in avanti a velocita' doppia.
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
    gh_free();
    // pulisci a nero l'intera finestra e torna alla finestra del menu
    gh_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    gh_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- entry
bool ghouls_play(Input *input, unsigned short *out_strip) {
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;
    g_aud_running = false;   // stato pulito anche se la create audio fallì

    if (xTaskCreatePinnedToCore(gh_task, "ghouls", GH_STACK,
                                nullptr, 2, nullptr, 1) != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

// ============================================================================
// Il core Z80 di Ghouls vive IN QUESTA UNITA' DI COMPILAZIONE (simboli Gh*, vedi
// i #define in testa) — stessa soluzione di spaceace.cpp e cps1944.cpp: cosi' le
// RdZ80/WrZ80 globali del core di Marat non collidono con quelle degli altri tre
// Z80 del progetto. `register` neutralizzato: rimosso in C++17, il core lo usa
// come storage-class.
// ============================================================================
#define register
extern "C" {
#include "cpus/z80gh/Z80gh.c"
}
