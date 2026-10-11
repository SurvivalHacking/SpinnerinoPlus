// ============================================================================
// outrun.cpp — OUT RUN (Sega 1986, hardware "Sega Out Run") per SPINNERINO PLUS.
//
// Porting da CABINATONE\outrun.cpp, dove il gioco e' stato scritto, validato
// con harness offline e confermato dall'utente sul device (video + input +
// audio completo con le tre musiche).
//
// ⭐ IL MODELLO DEL GUSCIO E' cps1944.cpp / ghouls.cpp DI QUESTO PROGETTO, non
//    outrun.cpp di CABINATONE: task, finestra SPI, blit a strisce, pagine di
//    stato, verifica del pack sulla SD, task audio sul core 0, uscita con
//    START 3 s sono quelli che su QUESTA scheda girano gia'.
//    La LOGICA DI EMULAZIONE (mappa di memoria, mapper 315-5195, bus dei due
//    68000, interrupt per scanline, tilemap, strada, sprite, SegaPCM) e'
//    trascritta ALLA LETTERA da CABINATONE: ogni ramo la' dentro e' stato
//    pagato con del debug (harness in C e Python che rendevano il gioco in
//    PNG), non si "semplifica" ([[feedback_porta_non_riprogettare]]).
//
// ⛔ COSA E' STATO ADATTATO rispetto a CABINATONE (e SOLO questo):
//   1. il display: la' pannello DSI 1024x600 + PPA hardware + lcd_draw_bitmap,
//      qui un pannello SPI 320x240 scritto A STRISCE di 8 righe. Via
//      or_scale()/or_flush()/il fallback software: c'e' or_blit(), modellata
//      su gh_blit()/c1944_blit(). Gioco DIRITTO, 320 colonne ridotte a 240
//      (se ne tengono 3 ogni 4), 224 righe centrate nell'asse da 320 -> 48 px
//      di banda nera sopra e sotto, finestra SPI STRETTA sulle sole 224
//      colonne che portano gioco.
//   2. la palette: qui la LUT e' BYTE-SWAPPED (lo vuole questo pannello SPI),
//      su CABINATONE il DSI voleva i valori nativi. E' UNA riga in
//      or_update_palette(); il renderer non cambia di un carattere.
//      ⚠️ L'unica altra conseguenza e' or_shade() (le ombre degli sprite), che
//      lavora sui CANALI: fa lo swap in ingresso e in uscita.
//   3. l'audio: audio_pce_* -> ponte laserdisc_audio_begin/write/end (I2S gia'
//      aperto a 32 kHz dal firmware). ⛔ NIENTE i2s_set_sample_rates
//      ([[spinnerino_audio_i2s_32khz]]). Il task esce su g_stop invece di
//      girare per sempre.
//   4. l'input: bt_get_buttons() -> g_input->buttons_get(), bt_btn_y() ->
//      g_input->button_y_pressed(), maschere BUTTON_*.
//   5. il Z80 sonoro e' RINOMINATO `Or*` (QUINTO core Z80, vedi i #define qui
//      sotto) e le sue globali si definiscono in questa unita' invece di
//      passare da z80_bus.cpp, che qui collidebbe coi giochi della griglia.
//   6. le allocazioni in RAM INTERNA: la' il binario e' di UN gioco solo (~305
//      KB liberi), qui ce ne sono trenta e ne restano ~105. Vedi or_alloc().
//   7. l'uscita: niente esp_restart(), si torna alla griglia come 1944/Ghouls.
// ============================================================================
// ⚡ come m68k_core.c, cps1944.cpp e ghouls.cpp: l'IDE compila a **-Os**, e qui
//    dentro ci sono sia i bus di DUE 68000 (31.554 istruzioni/frame MISURATE
//    sull'harness di CABINATONE) sia i cicli PER PIXEL del renderer.
#pragma GCC optimize("O2")
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "config.h"
#include "emulation/outrun.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"

// ---- Z80 sonoro, RINOMINATO per non collidere ------------------------------
// ⛔ In questo progetto esistono GIA' QUATTRO core Z80: `cpus/z80/` (i giochi
// della griglia), `cpus/z80ace/` (Space Ace, simboli `Ace*`), `cpus/z80cps/`
// (1944, `Cps*`) e `cpus/z80gh/` (Ghouls, `Gh*`). Il core di Marat dichiara
// RdZ80/WrZ80/... GLOBALI e senza contesto: due moduli che le definiscono
// entrambi = simbolo duplicato in link, anche se a runtime ne gira uno solo.
// Quindi QUINTA copia, `cpus/z80or/`, coi simboli `Or*`.
// ⚠️ Gli #include interni di Z80or.c sono stati ripuntati a cpus/z80or/; i
// Codes*.h restano condivisi da cpus/z80/ (sono frammenti di codice, non
// simboli), come gia' fanno z80ace, z80cps e z80gh. Verificato: Z80or.c e'
// IDENTICO a Z80gh.c a meno del percorso.
#define ExecZ80    OrExecZ80
#define RunZ80     OrRunZ80
#define ResetZ80   OrResetZ80
#define IntZ80     OrIntZ80
#define StepZ80    OrStepZ80
#define JumpZ80    OrJumpZ80
#define RdZ80      OrRdZ80
#define WrZ80      OrWrZ80
#define InZ80      OrInZ80
#define OutZ80     OrOutZ80
#define LoopZ80    OrLoopZ80
#define PatchZ80   OrPatchZ80
#define DebugZ80   OrDebugZ80
#define OpZ80_INL  OrOpZ80_INL
extern "C" {
#include "cpus/z80or/Z80.h"
}

// ---- YM2151 (Jarek Burczynski, versione vgmplay) ---------------------------
// ⚠️ mamedef.h PRIMA di ym2151.h: e' li' che stanno INT16 e stream_sample_t, da
// cui dipende il typedef SAMP. E' C puro: senza extern "C" i simboli verrebbero
// cercati decorati. Il .c lo tira dentro il wrapper di root ym2151_core.c (che
// c'e' gia' per Ghouls: qui NON si aggiunge nulla).
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
// stesse due funzioni che usano DL, Space Ace, DL2, 1944 e Ghouls.
extern bool input_is_vol_mode_active(void);
extern int  laserdisc_current_volume(void);

// ---------------------------------------------------------------- parametri
#define LD_FB_H       240
// ⛔⛔⛔ 32 KB, NON 16 — corretto il 2026-08-10, ed e' la causa del riavvio
// **sistematico al primo lancio dopo l'accensione** (visto su 1944, Ghouls e
// Out Run, cioe' su TUTTI e tre i giochi che leggono da SD, e su NESSUN altro).
// Questo task chiama `sd_mount()`, e quindi `esp_vfs_fat_sdmmc_mount()`, che fra
// init dell'SDMMC e scansione della FAT e' **molto** vorace di stack: con 16 KB
// lo sfonda e la scheda va in panico. Gira UNA VOLTA SOLA — al primo lancio,
// perche' dopo `sd_is_mounted()` e' vero e nessuno lo richiama piu': da qui il
// difetto che si presenta sempre la prima volta e mai le successive.
// ⭐ Il numero non e' scelto a occhio: e' quello che usano da sempre i player
// LASERDISC (`LD_TASK_STACK 32768` in laserdisc.cpp, spaceace.cpp e dl2.cpp),
// che montano la SD dal proprio task e infatti non si sono mai riavviati. I 16
// KB venivano da [[feedback_emulation_task_stack_fm]], che pero' parla degli
// interpreti di CPU, non di FATFS.
#define OR_STACK      32768
#define OR_DIR        SD_MOUNT_POINT "/outrun"

// ---------------------------------------------------------------- geometria
#define OR_W 320                          // area visibile del gioco
#define OR_H 224

// ---- geometria a schermo: la stessa forma di 1944 e Ghouls ----------------
// L'ALTEZZA del gioco (224) va sull'asse da TFT_FB_W=320, che e' il VERTICALE
// visto dall'utente: ci sta intera e avanzano 48 px di banda NERA sopra e sotto.
#define OR_X_OFF  ((TFT_FB_W - OR_H) / 2)         // = 48

// ---------------------------------------------------------------- timing
#define CYC_FRAME  166516                 // 10 MHz / 60,0543 Hz
#define FRAME_US   16651                  // 1e6 / 60,0543
#define LINES      262
#define CYC_HALF   (CYC_FRAME / LINES / 2)

// ⭐⭐ FINESTRA DIRETTA DELLO SPAZIO OPCODE, accesa **per CPU**.
// Su 1944 questa leva vale ~19% del tempo del 68000 (una chiamata INDIRETTA in
// meno nel fetch: 32 istruzioni host + 3 salti -> 20 e 1). Qui i 68000 sono
// DUE con ROM diverse, e la finestra e' UNA globale: si riprogramma dentro
// run_cpu(), che gia' cambia contesto. E' due store per fetta, niente rispetto
// al context switch che c'e' comunque.
// ⭐ EQUIVALENZA PER COSTRUZIONE, non sperata: il primo ramo di main_area() e'
// `a < 0x060000 -> rom_main[a]` e quello di sub_area() `a < 0x060000 ->
// rom_sub[a]`, cioe' ESATTAMENTE un'immagine lineare big-endian della ROM per
// gli indirizzi 0..ROM_SZ-1, e su Out Run le ROM NON sono cifrate (niente
// FD1089/FD1094: verificato dai byte in fase 0). Fuori dalla finestra si torna
// sul percorso di prima.
#define OR_OPCODE_WINDOW 1

// ⛔⭐ IDLE-SKIP **GENERICO** DI MUSASHI (`m68k_set_idle_skip`, per intervallo
// di indirizzi): **NON SI ACCENDE, E QUI MENO CHE MAI**. Su 1944 e' legittimo
// perche' dentro una fetta di m68k_execute() l'emulatore e' un sistema CHIUSO
// (nessun altro scrive la memoria su cui la CPU aspetta). Out Run ha **DUE
// 68000 che si parlano via RAM CONDIVISA** (0x260000 lato main, 0x060000 lato
// sub) e per giunta le fette dei due si alternano: e' esattamente il caso in
// cui quell'ipotesi cade. Resta spento, e or_free() lo rimette a spento perche'
// e' lo stato che il prossimo gioco a 68000 del binario si aspetta.
// ⭐ ⚠️ Da non confondere con `OR_IDLE_SKIP` (piu' avanti in questo file), che
// e' **un'altra cosa**: non un intervallo di indirizzi ma la firma STRETTA di
// UNA attesa precisa, ristretta alla WORK RAM del main — cioe' proprio fuori
// dalla RAM condivisa di cui parla questa nota. Quello e' stato DIMOSTRATO con
// l'harness A/B che qui sopra si chiedeva (`_verify_gfx/outrun/oridle.c`:
// 1.800 quadri in partita, 0 quadri diversi su 2.400), e si puo' accendere.

// ------------------------------------------------------------------- memoria
#define ROM_SZ     0x60000
#define WORK_SZ    0x8000
#define TILE_SZ    0x10000
#define TEXT_SZ    0x1000
#define PAL_SZ     0x2000
#define SPR_SZ     0x1000
#define SHARED_SZ  0x8000
#define ROAD_SZ    0x1000
#define TILEROM_SZ 0x30000
#define SPRROM_SZ  0x100000
#define ROADROM_SZ 0x10000
#define TILEPX_SZ  (8192 * 64)            // 8192 tile 8x8 espanse a 1 byte/pixel
#define REG5_SZ    0x160000               // 0x2A0000-0x3FFFFF, resto della regione 5

// tabelle di Musashi: 256 KB + 320 KB, allocate in PSRAM (vedi m68kops.c: il
// core e' gia' modificato perche' le allochi il CHIAMANTE, m68k_set_tables)
#define M68K_JUMP_SIZE (0x10000 * sizeof(void *))
#define M68K_CYC_SIZE  (5 * 0x10000)

static uint8_t *rom_main = nullptr, *rom_sub = nullptr;
static uint8_t *tilerom = nullptr, *sprrom = nullptr, *roadrom = nullptr;
static uint8_t *tilepx  = nullptr;        // tile espanse
static uint8_t *workram = nullptr, *tileram = nullptr, *textram = nullptr;
static uint8_t *palram  = nullptr, *sprram = nullptr, *shared = nullptr;
static uint8_t *roadram = nullptr;
// ⭐⭐ DOPPIO BUFFER. Sprite RAM e road RAM sono entrambe bufferizzate
// dall'hardware, ma con trigger DIVERSI:
//   sprite: SCRITTURA a 0x140070  -> `draw_write()`, scambia sprram <-> sprbuf
//   strada: LETTURA  di 0x?90000  -> `segaic16_road_control_0_r`, roadram <-> roadbuf
// In entrambi i casi e' uno SCAMBIO, non una copia: il buffer vecchio torna
// nella RAM vista dalla CPU, che ci ricostruisce sopra la lista successiva.
// Il DISEGNO legge sempre il buffer, mai la RAM viva.
static uint8_t *sprbuf  = nullptr, *roadbuf = nullptr;
static uint8_t road_control = 0;          // 0x90000: solo bit 0-1 su Out Run
static uint8_t *reg5 = nullptr;
static uint16_t *pal565 = nullptr;        // palette convertita (4096 entry)
static void *m68k_jump = nullptr, *m68k_cyc = nullptr;
static void *ctx_main = nullptr, *ctx_sub = nullptr;

static inline void or_swap_buf(uint8_t *a, uint8_t *b, size_t n)
{
    uint32_t *pa = (uint32_t *)a, *pb = (uint32_t *)b;
    for (size_t i = 0; i < n / 4; i++) { const uint32_t t = pa[i]; pa[i] = pb[i]; pb[i] = t; }
}

static int  cur_cpu = 0;                  // 0 = main, 1 = sub
static int  irq_line[2] = { 0, 0 };
static bool sub_halted = true;            // il sub parte in reset
static bool sub_reset_pending = false;
static uint8_t adc_sel = 0;
static uint8_t in_port = 0xFF;            // $140011, attivo basso
static bool pal_dirty = true;

// ---- video / stato del player ----------------------------------------------
static uint16_t *fb = nullptr;            // 320x224 RGB565 (byte-swapped)
static uint16_t *g_strip = nullptr;
static Input    *g_input = nullptr;
static volatile bool g_stop = false;
static volatile bool g_done = false;
static volatile bool g_aud_running = false;

// ============================================================ mappa memoria
// ⚠️ TRASCRITTA ALLA LETTERA da CABINATONE\outrun.cpp.
// ⭐ MAPPA (dal mapper 315-5195, letta dalla tabella in ROM $7B40 — NON quella
//    "logica" che i riassunti di segaorun.cpp danno, che per questo set e'
//    sbagliata):
//   reg 0  512K @ 0x000000  ROM (0-0x5FFFF) + work RAM (0x60000-0x67FFF)
//   reg 1  128K @ 0x100000  tile RAM (0x100000) + text RAM (0x110000)
//   reg 2   64K @ 0x120000  PALETTE (8K)
//   reg 3   64K @ 0x130000  SPRITE RAM (4K)
//   reg 4   64K @ 0x140000  I/O: PPI ($140001/3/5), input ($140011), wdog
//   reg 5    2M @ 0x200000  ROM del sub · shared 0x260000 · road RAM 0x280000
//   sub:   ROM 0-0x5FFFF · shared 0x60000 · road RAM 0x80000 · road ctrl 0x90000
static inline uint8_t *main_area(uint32_t a, uint32_t *off)
{
    a &= 0xFFFFFF;
    if (a < 0x060000)                  { *off = a;                    return rom_main; }
    if (a < 0x080000)                  { *off = a & (WORK_SZ - 1);    return workram; }
    if (a >= 0x100000 && a < 0x110000) { *off = a & (TILE_SZ - 1);    return tileram; }
    if (a >= 0x110000 && a < 0x120000) { *off = a & (TEXT_SZ - 1);    return textram; }
    if (a >= 0x120000 && a < 0x130000) { *off = a & (PAL_SZ - 1);     return palram; }
    if (a >= 0x130000 && a < 0x140000) { *off = a & (SPR_SZ - 1);     return sprram; }
    if (a >= 0x200000 && a < 0x260000) { *off = a - 0x200000;         return rom_sub; }
    if (a >= 0x260000 && a < 0x280000) { *off = a & (SHARED_SZ - 1);  return shared; }
    if (a >= 0x280000 && a < 0x290000) { *off = a & (ROAD_SZ - 1);    return roadram; }
    // ⚠️ Il resto della regione 5 deve essere RAM leggibile/scrivibile: nel
    // simulatore offline lo e', e con questa zona assente (letture 0xFF,
    // scritture perse) il gioco si comporta diversamente.
    if (a >= 0x2A0000 && a < 0x400000) { *off = a - 0x2A0000;         return reg5; }
    return nullptr;                     // 0x140000 = I/O, 0x290000 = road ctrl
}

static inline uint8_t *sub_area(uint32_t a, uint32_t *off)
{
    a &= 0xFFFFFF;
    if (a < 0x060000)                  { *off = a;                    return rom_sub; }
    if (a < 0x080000)                  { *off = a & (SHARED_SZ - 1);  return shared; }
    if (a >= 0x080000 && a < 0x090000) { *off = a & (ROAD_SZ - 1);    return roadram; }
    return nullptr;
}

// ------------------------------------------------------------------- I/O
#define IS_IO(a) (!cur_cpu && ((a) & 0xFFFFFF) >= 0x140000 && ((a) & 0xFFFFFF) < 0x150000)

// ⭐ Il ROAD CONTROL e' visibile a ENTRAMBE le CPU: il sub lo ha a 0x090000
// (`sub_map` del driver), il main a 0x290000 attraverso la regione 5 del mapper.
#define IS_ROADCTL(a) (((a) & 0xFFFFFF & ~0x200000u) >= 0x090000 && \
                       ((a) & 0xFFFFFF & ~0x200000u) <  0x0A0000 && \
                       (cur_cpu ? (((a) & 0xFFFFFF) < 0x100000) \
                                : (((a) & 0xFFFFFF) >= 0x200000)))

// ⭐ REGISTRI DEL MAPPER 315-5195. Il driver installa il mapper su TUTTO lo
// spazio con `umask16(0x00ff)` (solo il byte BASSO = indirizzi dispari) e poi ci
// sovrascrive sopra le sei regioni programmate: quindi i registri rispondono
// ovunque NON sia coperto da una regione, ripetuti ogni 0x40 byte.
// Out Run usa la finestra a **$FFFF00**: e' li' che la routine di reset copia la
// tabella di 16 word da `$7B40` (`LEA $FF20.W,A0` = $FFFF20).
//   reg = (indirizzo >> 1) & 0x1F   ·   $FFFF07 = reg 0x03 = PORTA SONORA
// Noi la mappa la teniamo fissa, quindi serve intercettare **solo il 3**.
#define IS_MAPPER(a) (!cur_cpu && ((a) & 0xFFFFFF) >= 0xFFFF00 && \
                                  ((a) & 0xFFFFFF) <= 0xFFFF3F)
#define MAPPER_REG(a) ((((a) & 0xFFFFFF) >> 1) & 0x1F)

// --- soundlatch (reg 0x03 del mapper) e reset del sonoro: lo stato sta QUI,
//     e non nel blocco audio piu' in basso, perche' lo usa il bus del 68000.
// ⚠️ Il flag PBF e' di LIVELLO verso il mapper, ma la linea NMI dello Z80 scatta
// solo sul FRONTE di salita: se il 68000 scrive un secondo comando prima che lo
// Z80 abbia letto la porta, il byte viene sovrascritto e **non parte un secondo
// NMI**. Va replicato, altrimenti si generano NMI che l'hardware vero non fa.
static volatile uint8_t snd_to = 0;
static volatile bool    snd_pbf = false;
static volatile bool    snd_nmi_pending = false;
static volatile bool    snd_halted = true;   // reset dal bit D0 della porta C
static volatile bool    snd_reset_pending = false;
// ⚠️⚠️ FINESTRA CIECA DOPO IL RESET DEL Z80 SONORO — difetto latente trovato
// sull'harness di CABINATONE (ci si e' inciampati davvero, non e' teoria). Se un
// NMI arriva nei primi ~12,5 us dopo il reset, cioe' PRIMA che il Z80 abbia
// eseguito l'EXX a $000E, il gestore a $0066 legge una porta a caso: **il latch
// non viene mai svuotato**, `snd_pbf` resta alto per sempre, non parte piu'
// nessun NMI e la musica resta MUTA in modo definitivo. Per evitarlo si tiene un
// margine di silenzio dopo ogni reset prima di consegnare il primo NMI: il
// comando non si perde (resta in coda), arriva solo qualche ms piu' tardi.
#define SND_GUARD_SAMPLES 128                // ~4 ms a 32 kHz
static uint32_t snd_guard = SND_GUARD_SAMPLES;

// ⭐⭐⭐ FIFO dei comandi sonori — NON e' quello che fa l'hardware, ed e' voluto.
// Sul cabinato c'e' UN SOLO byte di latch, ma il Z80 vero gira in lockstep col
// 68000 e fa in tempo a prenderlo. Qui il Z80 sonoro sta su un ALTRO CORE e
// avanza a blocchi: guarda l'NMI ogni 8 campioni = 250 us.
// ⚠️ Out Run manda i comandi **a coppie distanti 49,6 us** (due per ogni IRQ2,
// alle scanline 65/66, 129/130, 193/194 — misurato: mai meno di 496 cicli del
// 68000). Con un latch singolo il secondo della coppia veniva SEMPRE
// sovrascritto: **48% dei comandi persi**, un secondo di musica e poi silenzio.
// ⛔ Ridurre il passo dell'NMI NON risolve: provato a 1 campione (31 us), stesso
// identico risultato, perche' i due core non si sincronizzano fra loro.
// Con la FIFO: 0 comandi persi, uscita identica al modello hardware-fedele.
#define SND_FIFO_SZ 32                       // potenza di 2
static volatile uint8_t  snd_fifo[SND_FIFO_SZ];
static volatile uint16_t snd_fifo_w = 0, snd_fifo_r = 0;

// produttore: task di emulazione (core 1)
static void snd_latch_write(uint8_t v)
{
    const uint16_t w   = snd_fifo_w;
    const uint16_t nxt = (uint16_t)((w + 1) & (SND_FIFO_SZ - 1));
    if (nxt == snd_fifo_r) return;           // piena: non deve accadere
    snd_fifo[w] = v;
    __sync_synchronize();                    // il dato PRIMA dell'indice
    snd_fifo_w = nxt;
    if (!snd_pbf) { snd_pbf = true; snd_nmi_pending = true; }
}

// consumatore: task audio (core 0), quando il Z80 legge la porta 0x40-0x7F.
// Un NMI per comando: se dopo l'estrazione ne restano altri, se ne riarma uno.
static uint8_t snd_latch_read(void)
{
    const uint16_t r = snd_fifo_r;
    if (r == snd_fifo_w) { snd_pbf = false; return snd_to; }   // vuota
    snd_to = snd_fifo[r];
    __sync_synchronize();
    snd_fifo_r = (uint16_t)((r + 1) & (SND_FIFO_SZ - 1));
    if (snd_fifo_r != snd_fifo_w) snd_nmi_pending = true;
    else                          snd_pbf = false;
    return snd_to;
    // ⚠️ Finestra di corsa NOTA e ACCETTATA: se il produttore legge `snd_pbf`
    // mentre qui lo azzeriamo, quel comando resta in coda senza NMI armato
    // finche' non arriva il successivo. Non si perde nulla e l'ordine non
    // cambia: si ritarda al piu' di un frame.
    // ⛔ NON "risolverla" facendo riarmare l'NMI anche qui dopo aver azzerato
    // `snd_pbf`: produttore e consumatore potrebbero armarlo ENTRAMBI per lo
    // stesso byte, e il secondo NMI leggerebbe a coda vuota **duplicando il
    // comando**. Un ritardo impercettibile e' meglio di un suono ripetuto.
}

// LETTURA del road control = scambio dei due buffer della strada; il valore
// restituito e' sempre 0xFFFF. ⚠️ E' una lettura CON EFFETTO COLLATERALE.
static inline uint16_t road_ctl_read(void)
{
    or_swap_buf(roadram, roadbuf, ROAD_SZ);
    return 0xFFFF;
}
// SCRITTURA: solo il byte basso, e su Out Run mascherato con 3 (il bit 2,
// "indice per scanline", NON esiste: `data & ((type == ROAD_OUTRUN) ? 3 : 7)`).
static inline void road_ctl_write(uint8_t v) { road_control = v & 3; }

// ⭐⭐ Le QUATTRO porte digitali del gruppo 0x10 sono DISTINTE: l'indice e'
// `(off >> 1) & 3` — nel driver `m_digital_ports[offset & 3]->read()` su
// { SERVICE, UNKNOWN, COINAGE, DSW }.
// ⚠️ Se rispondessero TUTTE con `in_port`, il gioco leggerebbe i pulsanti anche
// come DIP switch: con la marcia sul bit 0x10 finirebbe dentro "Time Adj." del
// SWB — si cambierebbe la difficolta' cambiando marcia.
#define DSW_SWA 0xFF   // coinage: 1 gettone / 1 credito
// SWB per il set `outrun`: 0x01 cabinato "Mini Up" · 0x04=0 demo sounds ON ·
// 0x08 inutilizzato · 0x30 Time Adj. Normal · 0xC0 Difficulty Normal
#define DSW_SWB 0xF9

// valori dei canali ADC. 0 = sterzo (0x20..0xE0, centro 0x80), 1 = gas,
// 2 = freno (0x00 a riposo), 3 = bank motor. Il driver, per i canali non
// mappati, restituisce 0x10 (`read_safe(0x10)`).
static uint8_t adc_val[8] = { 0x80, 0x00, 0x00, 0x10, 0x10, 0x10, 0x10, 0x10 };

static uint8_t io_read(uint32_t off)
{
    switch (off & 0x70) {
        // ⚠️ PPI: porte di USCITA mai scritte -> si rileggono 0x00, non 0xFF.
        // Con 0xFF il main resta per sempre nello stato 0 del suo main loop
        // (trovato con la scansione esaustiva dei 256 valori, non a tentativi).
        case 0x00: return 0x00;
        case 0x10:
            switch ((off >> 1) & 3) {
                case 0:  return in_port;       // SERVICE: start, coin, marcia
                case 2:  return DSW_SWA;
                case 3:  return DSW_SWB;
                default: return 0xFF;          // UNKNOWN: tutto a riposo
            }
        case 0x30: return adc_val[adc_sel & 7];
        default:   return 0xFF;                // watchdog e resto
    }
}

static void io_write(uint32_t off, uint8_t v)
{
    // ⭐⭐ 0x140070 = `draw_write()`: SCAMBIA sprite RAM e buffer di disegno.
    // Nella ROM: `NOT.W $140070` a $9792 (init) e `MOVE.B #$FF,$140071` dentro
    // l'IRQ4 (una volta per frame).
    if ((off & 0x70) == 0x70) { or_swap_buf(sprram, sprbuf, SPR_SZ); return; }
    // ⚠️ La scrittura al gruppo 0x30 NON seleziona il canale: nel driver fa solo
    // `m_adc->write()`, cioe' avvia la conversione. Il canale sta altrove.
    if ((off & 0x70) == 0x30) return;
    // porta C del PPI ($140005). Il driver (`video_control_w`):
    //   D7 SG1 · D6 SG0 · D5 display · **D4-D2 = selezione ADC** · D1 CONT
    //   D0 reset della sezione sonora
    // ⚠️ Il bit 0 qui lo usiamo per tenere/rilasciare il RESET del SUB: e' quanto
    // trovato per scansione sistematica ed e' cio' che fa partire il gioco.
    // Sul fronte 0->1 anche la CPU SONORA riparte davvero (come l'hardware), e
    // con lei si azzera lo stato del latch: vedi la nota sulla finestra cieca.
    // ⚠️ AGGIRAMENTO DICHIARATO: in MAME il sub non e' mai tenuto in reset da un
    // registro. Compensa la granularita' con cui interlacciamo le due CPU (mezza
    // scanline contro il quantum 6000 Hz di MAME). Senza, l'azzeramento della
    // shared fatto dal main cancella quello che il sub ha gia' scritto e il
    // gioco si pianta a $7C5A durante l'init.
    if ((off & 0xFF) == 0x05) {
        adc_sel = (v >> 2) & 7;
        const bool gira = (v & 1) != 0;
        if (gira && snd_halted) snd_reset_pending = true;
        snd_halted = !gira;
        if (gira && sub_halted)  { sub_halted = false; sub_reset_pending = true; }
        else if (!gira)          { sub_halted = true; }
    }
}

// ------------------------------------------------------- callback di Musashi
static unsigned int or_r8(unsigned int a)
{
    uint32_t o; uint8_t *m = cur_cpu ? sub_area(a, &o) : main_area(a, &o);
    if (m) return m[o];
    if (IS_ROADCTL(a)) return (uint8_t)road_ctl_read();
    if (IS_IO(a)) return io_read(a & 0xFF);
    return 0xFF;
}
static unsigned int or_r16(unsigned int a)
{
    uint32_t o; uint8_t *m = cur_cpu ? sub_area(a, &o) : main_area(a, &o);
    if (m) return ((unsigned int)m[o] << 8) | m[o + 1];
    if (IS_ROADCTL(a)) return road_ctl_read();
    if (IS_IO(a)) return 0xFF00 | io_read(a & 0xFF);
    return 0xFFFF;
}
static unsigned int or_r32(unsigned int a)
{
    uint32_t o; uint8_t *m = cur_cpu ? sub_area(a, &o) : main_area(a, &o);
    if (m) return ((unsigned int)m[o] << 24) | ((unsigned int)m[o + 1] << 16) |
                  ((unsigned int)m[o + 2] << 8) | m[o + 3];
    // il driver ha un handler a 16 bit: una lettura a 32 bit sono DUE accessi,
    // quindi due scambi — che si annullano. Netto: nessuno scambio.
    if (IS_ROADCTL(a)) return 0xFFFFFFFF;
    return 0xFFFFFFFF;
}
static inline bool is_rom(const uint8_t *m) { return m == rom_main || m == rom_sub; }

static void or_w8(unsigned int a, unsigned int v)
{
    uint32_t o; uint8_t *m = cur_cpu ? sub_area(a, &o) : main_area(a, &o);
    if (m) {
        if (is_rom(m)) return;
        m[o] = (uint8_t)v;
        if (m == palram) pal_dirty = true;
        return;
    }
    // il driver aggiorna il control solo se e' in gioco il byte BASSO
    // (`ACCESSING_BITS_0_7`): per un accesso a 8 bit significa indirizzo dispari.
    if (IS_ROADCTL(a)) { if (a & 1) road_ctl_write((uint8_t)v); return; }
    // registro 0x03 del mapper = comando al Z80 sonoro (solo byte basso)
    if (IS_MAPPER(a)) { if ((a & 1) && MAPPER_REG(a) == 0x03) snd_latch_write((uint8_t)v); return; }
    if (IS_IO(a)) io_write(a & 0xFF, (uint8_t)v);
}
static void or_w16(unsigned int a, unsigned int v)
{
    uint32_t o; uint8_t *m = cur_cpu ? sub_area(a, &o) : main_area(a, &o);
    if (m) {
        if (is_rom(m)) return;
        m[o] = (uint8_t)(v >> 8); m[o + 1] = (uint8_t)v;
        if (m == palram) pal_dirty = true;
        return;
    }
    if (IS_ROADCTL(a)) { road_ctl_write((uint8_t)v); return; }
    if (IS_MAPPER(a)) { if (MAPPER_REG(a) == 0x03) snd_latch_write((uint8_t)v); return; }
    if (IS_IO(a)) io_write(a & 0xFF, (uint8_t)v);
}
static void or_w32(unsigned int a, unsigned int v)
{
    or_w16(a, v >> 16); or_w16(a + 2, v);
}
static int or_int_ack(int level)
{
    // ⚠️ Qui NON si abbassa la linea: su questo hardware gli interrupt sono
    // temporizzati sulle scanline (assert e clear a una riga di distanza), non
    // cancellati dall'acknowledge come su CPS-1.
    (void)level;
    return M68K_INT_ACK_AUTOVECTOR;
}
// ============================================================================
// ⭐⭐⭐ OR_IDLE_SKIP — il MAIN 68000 passa il 44% delle sue istruzioni ad
// ASPETTARE il vblank, e quei cicli si possono saltare.
//
// ⛔ Fino al 2026-08-30 in questo file c'era scritto «idle-skip DA NON
// ACCENDERE»: la ragione era che i due 68000 si parlano via RAM CONDIVISA, e un
// idle-skip generico («conta le letture e salta») avrebbe potuto saltare una
// fetta mentre il main aspetta il SUB — cioe' qualcuno che PUO' sbloccarlo
// dentro la fetta. Quella ragione e' giusta, e questa cura non la viola: e' la
// forma stretta di [[feedback_ottimizzare_contando_accessi]] caso 8 (SHINOBI),
// «non conta le letture: riconosci QUELLA attesa».
//
// ---- L'ATTESA, letta dalla ROM vera (non dedotta) --------------------------
//     $7DDA  4A79 00060800   TST.W  $60800
//     $7DE0  6CF8            BGE.S  $7DDA      ; gira finche' e' >= 0
//     $7DF4  33FC 0001 ...   MOVE.W #1,$60800  ; 1 -> 0 -> -1 = DUE vblank
// `$60800` sta in **0x060000..0x07FFFF**, che in `main_area()` e' `workram`,
// cioe' la RAM PRIVATA del main. ⭐⭐⭐ Il SUB **non puo' toccarla**: nella sua
// mappa (`sub_area()`) quell'intervallo e' `shared`, un altro buffer. L'unico
// che scrive `$60800` e' il gestore vblank del main stesso, e gli IRQ si alzano
// FRA una fetta e l'altra (`update_main_irqs()` a scanline). Quindi qui
// **nessun altro puo' sbloccare l'attesa dentro la fetta**: e' esattamente la
// condizione che caso 8 richiede, ed e' PROVATA, non assunta.
//
// ---- La firma: letture consecutive dello STESSO indirizzo in WORKRAM -------
// Ristretta alla work RAM apposta: cosi' i fetch degli opcode (ROM, indirizzi
// sempre diversi) non contano, il codice normale si azzera da solo, e la RAM
// CONDIVISA — l'unico posto dove il sub potrebbe sbloccare qualcosa — resta
// **fuori** dal meccanismo per costruzione. Ogni scrittura azzera il contatore.
//
// ---- I NUMERI, misurati offline (harness `_verify_gfx/outrun/oridle.c`) ----
// 1.800 quadri IN PARTITA (gettone + START + acceleratore a fondo), impronta
// FNV di tileram+textram+palram+sprram+roadram quadro per quadro:
//   soglia | MAIN istr/quadro | risparmio | quadri DIVERSI su 2.400
//     off  |     13.524       |    —      |   —
//      4   |      9.265       |  -31,5%   |   **2**   ⛔
//      5   |      9.773       |  -27,7%   |     0
//    **6** |   **10.281**     | **-24,0%**|   **0**   ✅ scelta
//      8   |     11.295       |  -16,5%   |     0
// ⭐ La soglia 4 sembrava pulita su 900 quadri e sbaglia 2 quadri su 2.400: e'
// il motivo per cui la finestra di misura va allungata prima di scegliere. Si
// prende **il punto migliore del compromesso, non il massimo risparmio**
// (stessa scelta di Batsugun).
// ⭐ Il SUB non e' toccato: 17.705 istruzioni/quadro identiche in tutte le
// prove — la cura e' per costruzione solo sulla CPU 0.
// 📐 Attesa sul device: istruzioni emulate totali -10,4% -> CAR ~124 -> ~116.
// ============================================================================
// ⛔⛔⛔ SPENTO IL 2026-08-30 DAL REFERTO DEL DEVICE: «peggiorato».
// La misura offline era GIUSTA e resta valida — l'immagine non cambia (0 quadri
// diversi su 2.400) e le istruzioni EMULATE calano del 24% sul main. Ma quella
// misura non risponde alla domanda che conta: **quanto COSTA il controllo**.
// `or_idle_leggi()` gira su OGNI lettura del bus del main — decine di migliaia
// per quadro — mentre il risparmio sono ~3.200 istruzioni 68000 per quadro. Se
// il costo per lettura supera il guadagno per istruzione saltata, il conto si
// rovescia, e l'harness **non poteva vederlo**: conta le istruzioni emulate,
// non i cicli dell'host.
// ⭐ E' [[feedback_ottimizzare_contando_accessi]] caso 3 (Raiden) preso in
// pieno: *«contare dice dove si SPENDE, non quanto si GUADAGNA a toglierlo»*.
// Avevo dimostrato la CORRETTEZZA e dato per scontata la CONVENIENZA.
// ⚠️ Il codice resta qui con tutti i numeri perche' la diagnosi vale: se un
// giorno si tocca il percorso del bus (o si sposta il controllo DENTRO
// `or_r16`, dove il puntatore alla regione e' gia' stato calcolato e il test
// costa UN confronto invece di due), si riparte da qui — ma **prima si misura
// il costo**, non solo il risparmio.
#define OR_IDLE_SKIP 0
#define OR_IDLE_N    6      // letture consecutive prima di mollare la fetta

#if OR_IDLE_SKIP
static uint32_t or_idle_addr = 0xFFFFFFFFu;
static int      or_idle_n    = 0;

// ⚠️ L'intervallo 0x060000..0x07FFFF E' esattamente l'insieme per cui
// `main_area()` restituisce `workram`: scritto come intervallo per non rifare
// la decodifica dell'indirizzo sul percorso caldo. Se un giorno si toccasse la
// mappa di memoria, questa riga va rivista insieme a `main_area()`.
static inline void or_idle_leggi(unsigned int a)
{
    if (cur_cpu != 0) return;
    const uint32_t aa = a & 0xFFFFFF;
    if (aa < 0x060000 || aa >= 0x080000) return;      // solo WORKRAM del main
    if (aa != or_idle_addr) { or_idle_addr = aa; or_idle_n = 1; return; }
    if (++or_idle_n >= OR_IDLE_N) { or_idle_n = 0; m68k_end_timeslice(); }
}
static inline void or_idle_scrivi(void) { or_idle_addr = 0xFFFFFFFFu; or_idle_n = 0; }

// ⭐ Involucri, non modifiche: `or_r8`/`or_r16`/`or_w8`/`or_w16`/`or_w32`
// restano IDENTICHE a CABINATONE carattere per carattere, e `confronta_outrun.py`
// continua a dimostrarlo. Il costo e' una chiamata in piu' per accesso, gia'
// indiretta perche' il bus passa da M68kBus.
static unsigned int or_r8_id (unsigned int a) { or_idle_leggi(a); return or_r8(a);  }
static unsigned int or_r16_id(unsigned int a) { or_idle_leggi(a); return or_r16(a); }
static void or_w8_id (unsigned int a, unsigned int v) { or_idle_scrivi(); or_w8(a, v);  }
static void or_w16_id(unsigned int a, unsigned int v) { or_idle_scrivi(); or_w16(a, v); }
static void or_w32_id(unsigned int a, unsigned int v) { or_idle_scrivi(); or_w32(a, v); }

static const M68kBus OR_BUS = {
    or_r8_id, or_r16_id, or_r32, or_w8_id, or_w16_id, or_w32_id, or_int_ack
};
#else
static const M68kBus OR_BUS = {
    or_r8, or_r16, or_r32, or_w8, or_w16, or_w32, or_int_ack
};
#endif

// ============================================================ palette
// ⚡ I tre contributi RGB565 precalcolati sui 32 livelli possibili: cosi'
//    spariscono **12.288 divisioni intere per frame** (3 per ognuna delle 4096
//    entry), che su RISC-V non sono gratis.
// ⚠️ ADATTAMENTO SPINNERINO — L'UNICA RIGA DIVERSA DEL RENDERER: il valore
//    finale e' BYTE-SWAPPED, perche' e' quello che vuole questo pannello SPI
//    (su CABINATONE il DSI voleva i valori nativi). Fatto nella LUT, una volta
//    per colore: il renderer e il blit non swappano niente.
static uint16_t pc_r[32], pc_g[32], pc_b[32];
static bool     pc_ready = false;

static void or_update_palette(void)
{
    if (!pc_ready) {
        for (uint32_t i = 0; i < 32; i++) {
            const uint32_t c = i * 255 / 31;
            pc_r[i] = (uint16_t)((c & 0xF8) << 8);
            pc_g[i] = (uint16_t)((c & 0xFC) << 3);
            pc_b[i] = (uint16_t)(c >> 3);
        }
        pc_ready = true;
    }
    // 4096 entry; formato S16: 5 bit per canale, il bit basso dai bit 12-14.
    for (int i = 0; i < PAL_SZ / 2; i++) {
        const uint32_t v = ((uint32_t)palram[i * 2] << 8) | palram[i * 2 + 1];
        const uint16_t c = (uint16_t)(pc_r[((v & 0x000F) << 1) | ((v >> 12) & 1)] |
                                      pc_g[(((v >> 4) & 0x000F) << 1) | ((v >> 13) & 1)] |
                                      pc_b[(((v >> 8) & 0x000F) << 1) | ((v >> 14) & 1)]);
        pal565[i] = (uint16_t)((c >> 8) | (c << 8));      // BYTE-SWAP (pannello SPI)
    }
    pal_dirty = false;
}

// ============================================================ grafica
// ⚡ 1 bit per tile: "tutti e 64 i pixel sono pen 0", cioe' la tile e' INVISIBILE.
//    Costa 1 KB e si calcola una volta sola, ma fa saltare interi blocchi di
//    schermo senza nemmeno toccare la PSRAM: la fascia del cielo, per esempio,
//    ha le tile tutte vuote (0/64, verificato sui dump del device).
static uint8_t tile_blank[8192 / 8];

// Espansione UNA TANTUM delle tile 8x8 a 3 bitplane -> 1 byte/pixel.
// ⚠️ vTaskDelay ogni 1024 tile: 8192 giri su 512 KB di PSRAM senza respiro
// farebbero scattare il watchdog dell'idle su questo core (stessa cura di
// gh_expand_gfx).
static void or_expand_tiles(void)
{
    memset(tile_blank, 0, sizeof(tile_blank));
    for (uint32_t n = 0; n < 8192; n++) {
        uint8_t any = 0;
        for (int y = 0; y < 8; y++) {
            const uint32_t o = n * 8 + y;
            const uint8_t b0 = tilerom[0x00000 + o];
            const uint8_t b1 = tilerom[0x10000 + o];
            const uint8_t b2 = tilerom[0x20000 + o];
            uint8_t *d = tilepx + (size_t)n * 64 + y * 8;
            for (int x = 0; x < 8; x++) {
                const uint8_t m = 0x80 >> x;
                d[x] = (uint8_t)((b0 & m ? 1 : 0) | (b1 & m ? 2 : 0) | (b2 & m ? 4 : 0));
                any |= d[x];
            }
        }
        if (!any) tile_blank[n >> 3] |= (uint8_t)(1 << (n & 7));
        if ((n & 1023) == 1023) vTaskDelay(1);
    }
}

// ---- LE DUE TILEMAP (sfondo e primo piano), con scroll e selezione pagine.
// Registri dentro la TEXT RAM (word big-endian):
//   0xE80 pagine FG · 0xE82 pagine BG — 4 nibble: bit 3-0 alto-sx, 7-4 alto-dx,
//                                       11-8 basso-sx, 15-12 basso-dx
//   0xE90 scrollY FG · 0xE92 scrollY BG   (bit 15 = column scroll, non gestito)
//   0xE98 scrollX FG · 0xE9A scrollX BG   (bit 15 = row scroll, non gestito)
// ⚠️ Una pagina e' 64*32*2 = **4096 BYTE** (2048 word): 16 pagine x 4096 = 64 KB
//    = tutta la tile RAM. Calcolandola doppia, le pagine 8/9 — proprio quelle
//    che il gioco usa per lo sfondo — finivano fuori e lo schermo restava nero.
// ⚠️ Il formato della entry NON e' quello del text layer: qui
//    tile = e & 0x1FFF e colore = (e >> 6) & 0x7F (nel testo: 0x1FF e >> 9).
static inline uint16_t or_tw(const uint8_t *ram, uint32_t off)
{
    return (uint16_t)(((uint16_t)ram[off] << 8) | ram[off + 1]);
}

// ⚡⚡ SCRITTA PER TILE (non per PIXEL) — misurato su CABINATONE: la versione per
// pixel costava **26,9 ms** di render. I SEGMENTI dipendono solo da `scrollx` e
// si calcolano una volta per frame; le entry si leggono una volta per riga di
// tile (da ~72.000 letture a ~1.200); i pixel di una tile si leggono di fila
// (una sola linea di cache); le tile invisibili si saltano in blocco.
// ⚠️ I confini di tile cadono sempre su multipli di 8, e sia il ribaltamento a
//    0x400 sia il cambio di quadrante a 0x200 sono multipli di 8: dentro un
//    segmento pagina e tile non cambiano MAI. E' cio' che rende lecito uscire
//    dal ciclo per pixel.
static void or_draw_tilemap(int bg)
{
    const uint16_t pages = or_tw(textram, bg ? 0xE82 : 0xE80);
    const int scrolly = or_tw(textram, bg ? 0xE92 : 0xE90) & 0x1FF;
    const int scrollx = or_tw(textram, bg ? 0xE9A : 0xE98) & 0x3FF;

    // ---- i segmenti della riga: dipendono solo da scrollx
    // ⭐ offset X delle tilemap: il driver usa `(0xC0 - xscroll) & 0x3FF` e poi
    // `tilemap_x = screen_x + quello`. Il **+0xC0** (192 px) e' lo stesso 192 del
    // text layer ma con segno diverso.
    struct seg_t { int16_t x, fx, run, tx; uint8_t qx; };
    seg_t seg[OR_W / 8 + 2];
    int nseg = 0;
    for (int x = 0; x < OR_W; ) {
        const int sx = (x + 0xC0 - scrollx + 0x400) & 0x3FF;   // 2 pagine = 1024 px
        const int fx = sx & 7;                                 // 1° pixel dentro la tile
        int run = 8 - fx;                                      // ... fino al bordo
        if (x + run > OR_W) run = OR_W - x;
        seg[nseg].x  = (int16_t)x;
        seg[nseg].fx = (int16_t)fx;
        seg[nseg].run = (int16_t)run;
        seg[nseg].tx = (int16_t)((sx >> 3) & 63);
        seg[nseg].qx = (uint8_t)((sx & 0x200) ? 1 : 0);
        nseg++; x += run;
    }

    uint16_t rowe[OR_W / 8 + 2];             // entry per segmento (0 = niente da fare)
    int last_ty = -1, last_qy = -1;

    for (int y = 0; y < OR_H; y++) {
        const int sy = (y + scrolly) & 0x1FF;         // 2 pagine = 512 px
        const int ty = (sy >> 3) & 31, py = sy & 7;
        const int qy = (sy & 0x100) ? 2 : 0;

        if (ty != last_ty || qy != last_qy) {         // cambiata riga di tile
            for (int i = 0; i < nseg; i++) {
                const int page = (pages >> ((qy | seg[i].qx) * 4)) & 0x0F;
                const uint32_t o = (uint32_t)page * 4096 +
                                   (uint32_t)(ty * 64 + seg[i].tx) * 2;
                uint16_t e = (o + 1 >= TILE_SZ) ? 0 : or_tw(tileram, o);
                const uint32_t tn = e & 0x1FFF;
                if (e && (tile_blank[tn >> 3] & (1 << (tn & 7)))) e = 0;  // tile vuota
                rowe[i] = e;
            }
            last_ty = ty; last_qy = qy;
        }

        uint16_t *dst = fb + (size_t)y * OR_W;
        for (int i = 0; i < nseg; i++) {
            const uint16_t e = rowe[i];
            if (!e) continue;
            // (e >> 6) & 0x7F = banco di 8 pen; l'indice massimo e' 0x7F*8+7 =
            // 1023, sempre dentro le 4096 entry.
            const uint8_t  *s   = tilepx + (size_t)(e & 0x1FFF) * 64 + py * 8 + seg[i].fx;
            const uint16_t *pal = pal565 + ((e >> 6) & 0x7F) * 8;
            uint16_t *d = dst + seg[i].x;
            for (int k = 0; k < seg[i].run; k++) {
                const uint8_t p = s[k];
                if (p) d[k] = pal[p];
            }
        }
    }
}

// ---- il TEXT layer. Entry: bit 9-11 colore, bit 0-8 tile (diverso dalle tilemap)
// ⭐⭐ `col0` = prima colonna VISIBILE. La text RAM e' larga `stride` colonne
// (64) ma sullo schermo da 320 px ne entrano solo 40: le visibili sono le
// ULTIME, cioe' 24..63 (64-40=24), non 0..39 (`set_scrolldx(-192,-192)` nel
// driver, `x = 8*mx - 192` in Cannonball). Leggendo dalla 0 tutto il testo
// usciva spostato a destra e tagliato ("BES" invece di "BEST OUTRUNNERS").
static void or_draw_tiles(const uint8_t *ram, int cols, int rows, int stride, int col0)
{
    for (int ty = 0; ty < rows; ty++) {
        const int y0 = ty * 8;
        if (y0 >= OR_H) break;
        for (int tx = 0; tx < cols; tx++) {
            const uint32_t o = (uint32_t)(ty * stride +
                                          ((tx + col0) & (stride - 1))) * 2;
            const uint16_t e = ((uint16_t)ram[o] << 8) | ram[o + 1];
            if (!e) continue;
            const uint32_t tn = e & 0x1FF;
            if (tile_blank[tn >> 3] & (1 << (tn & 7))) continue;   // tile invisibile
            const uint8_t *src = tilepx + (size_t)tn * 64;
            const uint16_t *pal = pal565 + ((e >> 9) & 7) * 8;
            const int x0 = tx * 8;
            for (int y = 0; y < 8; y++) {
                const int py = y0 + y;
                if (py >= OR_H) break;
                uint16_t *dst = fb + (size_t)py * OR_W + x0;
                const uint8_t *s = src + y * 8;
                for (int x = 0; x < 8; x++) {
                    const uint8_t p = s[x];
                    if (p && (unsigned)(x0 + x) < OR_W) dst[x] = pal[p];
                }
            }
        }
    }
}

// ---- la strada (generatore 315-5217)
// road RAM: 0x000+ control/indice per scanline (road 0), 0x100+ road 1,
//           0x200+ posizioni orizzontali road 0, 0x400+ road 1,
//           0x600+ indici di colore.
// ROM: mappa 512x256 a 2 bit, piani separati (0x0000 e 0x4000), 64 byte/riga.
// lettura di una word dal BUFFER della strada, per indice di word
static inline uint16_t or_rw(uint32_t widx)
{
    const uint32_t o = widx * 2;
    if (o + 1 >= ROAD_SZ) return 0;
    return (uint16_t)(((uint16_t)roadbuf[o] << 8) | roadbuf[o + 1]);
}

// ⭐ Il CIELO: le scanline col bit 11 (riempimento solido) stanno sopra
// l'orizzonte e non hanno strada. Le tile di quella zona sono **tutte vuote**
// (0/64 verificato sui dump), quindi il colore di fondo lo da' il road
// hardware. Va disegnato per PRIMO.
static void or_draw_sky(void)
{
    const int control = road_control & 3;
    for (int y = 0; y < OR_H; y++) {
        const uint16_t d0 = or_rw(0x000 + y), d1 = or_rw(0x100 + y);
        // ⭐ quale delle DUE strade decide il cielo dipende dal control: con la
        // priorita' invertita comanda road 1.
        int color = -1;
        switch (control) {
            case 0: if (d0 & 0x800) color = d0 & 0x7F; break;
            case 1: if (d0 & 0x800) color = d0 & 0x7F;
               else if (d1 & 0x800) color = d1 & 0x7F; break;
            case 2: if (d1 & 0x800) color = d1 & 0x7F;
               else if (d0 & 0x800) color = d0 & 0x7F; break;
            case 3: if (d1 & 0x800) color = d1 & 0x7F; break;
        }
        if (color < 0) continue;
        // ⭐ base **0x780** (colorbase3), non 0x400: le entry 0x780-0x7BF sono
        // TUTTE 4F90 = (0,148,255), azzurro cielo — 64 entry identiche, uniche
        // in tutta la palette, ed e' esattamente l'ampiezza di (d & 0x7F). Con
        // 0x400 l'indice cadeva su 0x43F = beige, da cui il "cielo marrone".
        const uint16_t c = pal565[(0x780 | color) & 0xFFF];
        uint16_t *dst = fb + (size_t)y * OR_W;
        for (int x = 0; x < OR_W; x++) dst[x] = c;
    }
}

// ⭐ un pixel della ROM strada. `road` 0 o 1: le due meta' del dump da 64K
// (0x0000 piano 0 / 0x4000 piano 1 per road 0, +0x8000 per road 1). 64 byte per
// riga = 512 pixel, bit dal piu' significativo.
// ⚠️ Il decode del driver marca con il bit 2 (valore **7**) i pixel di valore 3
// nelle colonne 248..255: e' la "stripe", e ha una voce di colore tutta sua.
// Senza, quella fascia usciva col colore del fuori-strada.
static inline uint8_t or_road_pix(int road, uint32_t line, uint32_t h)
{
    if (h >= 0x200) return 3;
    const uint8_t *p0 = roadrom + (road ? 0x8000 : 0) + line * 64;
    const uint8_t *p1 = p0 + 0x4000;
    const uint32_t o = h >> 3;
    const uint8_t m = (uint8_t)(0x80 >> (h & 7));
    uint8_t p = (uint8_t)((p0[o] & m ? 1 : 0) | (p1[o] & m ? 2 : 0));
    if (p == 3 && h >= 256 - 8 && h < 256) p |= 4;
    return p;
}

// ⭐ I COLORI VERI della strada (tabella di hwroad.cpp): colorbase1 = 0x400
// (strada), 0x420 (esterno). Cinque voci per strada, non quattro — la quinta e'
// la "stripe" (indice 7).
// ⚠️ Prima il pixel 3 lo lasciavo trasparente e i lati dello schermo restavano
//    NERI: il fuori-strada e' un COLORE del road hardware, non trasparenza.
static void or_road_colors(uint16_t *col, uint16_t d, uint16_t c, int road)
{
    const int s = road ? 4 : 0;              // road 1 usa i bit alti di `color`
    const int b = road ? 0x08 : 0x00;        // e uno scostamento di base
    col[0] = pal565[(0x400 ^ (b + 0x00) ^ ((c >> (s + 0)) & 1)) & 0xFFF];
    col[1] = pal565[(0x400 ^ (b + 0x02) ^ ((c >> (s + 1)) & 1)) & 0xFFF];
    col[2] = pal565[(0x400 ^ (b + 0x04) ^ ((c >> (s + 2)) & 1)) & 0xFFF];
    col[3] = (d & 0x200) ? col[0]
           : pal565[(0x420 ^ (road ? 0x10 : 0x00) ^ ((c >> 8) & 0xF)) & 0xFFF];
    col[7] = pal565[(0x400 ^ (b + 0x06) ^ ((c >> (s + 3)) & 1)) & 0xFFF];
}

static void or_draw_road(void)
{
    // ⭐⭐ priorita' fra le due strade quando sono attive entrambe. Tabella presa
    // dal driver: `(priority_map[control-1][pix0] >> pix1) & 1` -> vince road 1.
    static const uint8_t prio_map[2][8] = {
        { 0x80, 0x81, 0x81, 0x87, 0, 0, 0, 0x00 },
        { 0x81, 0x81, 0x81, 0x8F, 0, 0, 0, 0x80 }
    };
    const int control = road_control & 3;
    for (int y = 0; y < OR_H; y++) {
        const uint16_t d0 = or_rw(0x000 + y), d1 = or_rw(0x100 + y);
        // se ENTRAMBE le strade sono "sopra l'orizzonte" la riga e' solo cielo
        if ((d0 & 0x800) && (d1 & 0x800)) continue;
        // ⭐⭐⭐ L'hpos NON si legge per scanline: la line table contiene in
        // `d & 0x1FF` un indice di PROFONDITA' (Z), ed e' QUELLO che indicizza
        // la tabella di scroll — come gia' si faceva per il colore. Il driver:
        //     hpos0 = roadram[0x200 + (data0 & 0x1ff)] & 0xfff
        // (`control & 4` userebbe la scanline, ma su Out Run il control e'
        // mascherato con 3: quel bit non esiste mai.)
        // ⚠️ Leggendolo per scanline la strada curvava in modo diverso da dove
        // il gioco piazza gli oggetti a bordo strada (che usa la STESSA tabella
        // indicizzata per Z): su rettilineo non si vede, in curva le palme e i
        // cartelli "non seguono la strada".
        const uint32_t i0 = d0 & 0x1FF, i1 = d1 & 0x1FF;
        const uint16_t c0 = or_rw(0x600 + i0), c1 = or_rw(0x600 + i1);
        uint16_t col0[8] = {0}, col1[8] = {0};
        or_road_colors(col0, d0, c0, 0);
        or_road_colors(col1, d1, c1, 1);
        // riga della ROM; con il bit 11 alzato si usa la riga fittizia (tutta 3)
        const uint32_t l0 = (d0 >> 1) & 0xFF, l1 = (d1 >> 1) & 0xFF;
        const int dummy0 = (d0 & 0x800) != 0, dummy1 = (d1 & 0x800) != 0;
        // ⚠️ costante di centratura 0x5F8: senza, hpos resta oltre 0x200 e non si
        //    disegna nulla (esce tutta "erba").
        uint32_t h0 = (uint32_t)((or_rw(0x200 + i0) - 0x5F8) & 0xFFF);
        uint32_t h1 = (uint32_t)((or_rw(0x400 + i1) - 0x5F8) & 0xFFF);
        uint16_t *dst = fb + (size_t)y * OR_W;
        // lo switch sta FUORI dal ciclo sui pixel, come nel driver.
        if (control == 0) {
            if (dummy0) continue;
            for (int x = 0; x < OR_W; x++) {
                dst[x] = col0[or_road_pix(0, l0, h0)];
                h0 = (h0 + 1) & 0xFFF;
            }
        } else if (control == 3) {
            if (dummy1) continue;
            for (int x = 0; x < OR_W; x++) {
                dst[x] = col1[or_road_pix(1, l1, h1)];
                h1 = (h1 + 1) & 0xFFF;
            }
        } else {
            const uint8_t *pm = prio_map[control - 1];
            for (int x = 0; x < OR_W; x++) {
                const uint8_t p0 = dummy0 ? 3 : or_road_pix(0, l0, h0);
                const uint8_t p1 = dummy1 ? 3 : or_road_pix(1, l1, h1);
                dst[x] = ((pm[p0] >> p1) & 1) ? col1[p1] : col0[p0];
                h0 = (h0 + 1) & 0xFFF;
                h1 = (h1 + 1) & 0xFFF;
            }
        }
    }
}

// ---- gli sprite (315-5211)
// ⭐ Trascrizione FEDELE di `sega_outrun_sprite_device::draw` (MAME
//    src/mame/sega/sega16sp.cpp), che coincide riga per riga con
//    `hwsprites::render()` di Cannonball.
// Entry di 16 byte: +0 fine lista/nascosto/banco/top+256 · +2 offset · +4
// pitch+X · +6 ombre/priorita'/zoomV · +8 verso/flip/zoomH · +A altezza+colore.
// ⚠️ height = (word[5] >> 8) + 1 e' l'altezza IN RIGHE DI SCHERMO; lo zoom
//    verticale non la cambia, decide solo quanto avanza l'indirizzo sorgente.
// ⚠️ la riga finisce quando il PENULTIMO nibble NELL'ORDINE DI CONSUMO e' 0xF,
//    non al primo 0xF incontrato; 0x0 e 0xF sono ENTRAMBI trasparenti.

// ---- OMBRE. Il pen 0xA di uno sprite col flag ombra (bit 14 di +6) NON e' un
// colore: l'hardware sposta il pixel GIA' PRESENTE in una seconda copia della
// palette, piu' scura. Disegnandolo come colore opaco usciva NERO.
// Fisicamente e' una resistenza in parallelo al DAC a 5 bit, tirata a massa:
//   attenuazione = rShade / (rShade + rDAC),  rDAC = 128,93 ohm
// Cannonball (Chris White, dall'hardware vero): rShade = 220 ohm -> **0,63**.
// MAME usa 470 ohm -> 0,785; Cannonball annota che e' sbagliato, e per Out Run
// la fonte da seguire e' Cannonball. 0,63 x 256 = 161 (MAME sarebbe 201).
// ⚠️ ADATTAMENTO SPINNERINO: qui `fb` contiene colori BYTE-SWAPPED, e questa e'
//    l'unica funzione del renderer che guarda DENTRO i canali di un pixel gia'
//    scritto. Si riporta in ordine nativo, si attenua, si rimette swappato: due
//    swap per pixel d'ombra (sono poche centinaia per frame, le ombre sotto le
//    auto), e il risultato e' identico a CABINATONE.
#define OR_SHADOW_MUL 161
static inline uint16_t or_shade(uint16_t cs)
{
    const uint16_t c = (uint16_t)((cs >> 8) | (cs << 8));      // -> nativo
    const uint32_t r = ((c >> 11) & 0x1F) * OR_SHADOW_MUL >> 8;
    const uint32_t g = ((c >>  5) & 0x3F) * OR_SHADOW_MUL >> 8;
    const uint32_t b = ( c        & 0x1F) * OR_SHADOW_MUL >> 8;
    const uint16_t o = (uint16_t)((r << 11) | (g << 5) | b);
    return (uint16_t)((o >> 8) | (o << 8));                    // -> byte-swapped
}

static void or_draw_sprites(void)
{
    const uint32_t ndw = SPRROM_SZ / 4;
    const uint32_t nbank = ndw / 0x10000;
    for (int i = 0; i < (int)(SPR_SZ / 16); i++) {
        // ⭐ dal BUFFER, non dalla RAM viva: la CPU sta gia' costruendo la
        // lista del frame successivo dentro `sprram`.
        const uint8_t *e = sprbuf + i * 16;
        #define EW(k) (((uint16_t)e[(k)] << 8) | e[(k) + 1])
        const uint16_t w0 = EW(0);
        if (w0 & 0x8000) break;                       // fine lista
        if ((w0 & 0x4000) || (w0 & 0x1000)) continue; // nascosto
        const uint16_t w2 = EW(4), w4 = EW(8);
        // ⚠️ zoom su 11 bit (0x7FF), non 10: con 0x3FF uno sprite ingrandito
        // 2x o piu' (zoom >= 0x400) diventava 0 e veniva SCARTATO. E lo zoom
        // piccolo non si scarta, si CLAMPA a 0x40 = 8x.
        int32_t zv = EW(6) & 0x7FF, zh = w4 & 0x7FF;
        if (zv < 0x40) zv = 0x40;
        if (zh < 0x40) zh = 0x40;
        const int shadow = (EW(6) >> 14) & 1;   // ombra: il pen 0xA scurisce
        const uint32_t bank = ((w0 >> 9) & 7) % (nbank ? nbank : 1);
        int top = (int)(w0 & 0x1FF) - 0x100;
        const uint16_t off = EW(2);
        int32_t pitch = (int32_t)(int16_t)((w2 >> 1) | ((w4 & 0x1000) << 3)) >> 8;
        // ⚠️ l'aggiustamento della X va fatto PRIMA di togliere 0xBE, e va
        // insieme al bound direzionale del ciclo sui dword (sotto): da solo
        // porterebbe x a 322..449 e lo sprite morirebbe al primo dword.
        int xpos = (int)(w2 & 0x1FF);
        if (xpos < 0x80 && ((w4 & 0x2000) == 0)) xpos += 0x200;
        int x0 = xpos - 0xBE;
        // ⭐⭐ `flip` e' il bit 14 NEGATO, e comanda TRE cose: il verso di
        // lettura della ROM, l'ordine dei nibble e QUALE nibble e' il
        // terminatore di riga. Il bit 13 comanda solo il verso sullo SCHERMO.
        const int flip   = (~w4 >> 14) & 1;
        const int xdelta = (w4 & 0x2000) ? 1 : -1;
        const int ydelta = (w4 & 0x8000) ? 1 : -1;
        const int h    = (EW(0x0A) >> 8) + 1;
        // ⭐ IL COLORE sta nel byte BASSO della stessa word +0x0A da cui viene
        // l'altezza: e' un indice di banco da 16 pen. La palette degli sprite
        // parte da **0x800**, quindi: palette = 0x800 + (colore << 4) + pixel.
        // ⚠️ Servono ENTRAMBI: senza lo 0x800 le palme escono rosa, senza il
        // (colore << 4) escono tutte nere.
        const int color = EW(0x0A) & 0x7F;
        #undef EW
        const int adelta = flip ? -1 : 1;
        const uint32_t mark = flip ? 0x0F000000u : 0x000000F0u;
        // ⭐⭐⭐ NIENTE ±1 sull'indirizzo di partenza. L'idioma di Cannonball
        // `data[7] = addr - 1; pixels = spritedata[++data[7]]` PRE-incrementa,
        // quindi il primo dword letto e' `addr`, non `addr-1`. Leggendolo come
        // un post-incremento ogni riga partiva da un dword estraneo: era il
        // difetto piu' pesante (misurato: 9.669 -> 770 pixel da solo).
        const uint32_t sbase = bank * 0x10000;
        uint16_t addr = off;                  // ⚠️ 16 bit: ruota DENTRO il banco
        uint32_t yacc = 0;
        // ⚠️ il verso verticale viene dal bit 15 di +8: si puo' disegnare
        // dal basso verso l'alto. Il ciclo va fino a ytarget, e le righe fuori
        // schermo si SALTANO (niente break: il resto dello sprite puo' rientrare).
        const int ytarget = top + ydelta * h;
        for (int y = top; y != ytarget; y += ydelta) {
            if ((unsigned)y < (unsigned)OR_H) {
                int cx = x0; uint32_t xacc = 0; uint16_t a = addr;
                uint16_t *dst = fb + (size_t)y * OR_W;
                // bound DIREZIONALE come nel driver: chi scorre verso destra
                // esce a destra, chi scorre verso sinistra esce a sinistra.
                while ((xdelta > 0 && cx < (int)OR_W) || (xdelta < 0 && cx >= 0)) {
                    const uint8_t *sp = sprrom + (size_t)(sbase + a) * 4;
                    const uint32_t d = (uint32_t)sp[0] | ((uint32_t)sp[1] << 8) |
                                       ((uint32_t)sp[2] << 16) | ((uint32_t)sp[3] << 24);
                    a = (uint16_t)(a + adelta);
                    for (int n = 0; n < 8; n++) {
                        const int s = flip ? (n * 4) : (28 - n * 4);
                        const uint8_t p = (uint8_t)((d >> s) & 0xF);
                        while (xacc < 0x200) {
                            if ((unsigned)cx < OR_W) {
                                // ⭐ ordine come nel driver: l'OMBRA viene prima
                                // e vale solo per il pen 0xA; su uno sprite senza
                                // flag ombra il pen 0xA e' un colore normale.
                                if (shadow && p == 0x0A) {
                                    dst[cx] = or_shade(dst[cx]);
                                } else if (p != 0 && p != 0xF) {
                                    dst[cx] = pal565[(0x800 + (color << 4) + p) & 0xFFF];
                                }
                            }
                            cx += xdelta;
                            xacc += (uint32_t)zh;
                        }
                        xacc -= 0x200;
                    }
                    // ⚠️ il terminatore e' il PENULTIMO nibble nell'ordine di
                    // consumo: 0x000000F0 se si legge dal MSB, 0x0F000000 se
                    // si legge dal LSB (caso flipped).
                    if ((d & mark) == mark) break;
                }
            }
            yacc += (uint32_t)zv;
            addr = (uint16_t)(addr + pitch * (int32_t)(yacc >> 9));
            yacc &= 0x1FF;
        }
    }
}

// ---------------------------------------------------------------- misuratore
// ⭐⭐ STRUMENTO DI MISURA PERMANENTE, messo UNA VOLTA — non un flash
// diagnostico. E' la forma che [[feedback_fps_a_passo_fisso_sono_ciechi]]
// PRESCRIVE e che [[feedback_una_modifica_per_flash]] chiede espressamente:
// «quando il difetto e' di PRESTAZIONI e non si sa quale sottosistema e' in
// ritardo, il primo flash e' lo STRUMENTO, non un tentativo di cura».
// `OR_MISURA 0` = via tutto, nemmeno una variabile.
//
// ------------------------------------------------------------- PERCHE' COSI'
// Il ciclo di Out Run e' a PASSO FISSO (`next_us += FRAME_US`, poi si aspetta):
// il contatore degli fps segna il valore nominale finche' non e' GIA' rotto,
// quindi da solo non dice mai quanto margine resta. Quello che serve e' il
// **CARICO** (lavoro medio per giro / FRAME_US) e il **PICCO**.
//
// ⭐ Qui — a differenza dei giochi normali, dove l'emulazione sta sull'altro
// core (vedi prof.h) — `or_run_frame()`, `or_render()` e `or_blit()` girano
// TUTTI sullo STESSO task. Quindi le tre voci **si sommano davvero** e il collo
// di bottiglia si legge direttamente. L'unico ramo parallelo e' l'AUDIO (task
// suo su core 0), ed e' la voce AUD.
//
// ------------------------------------------------------------- COSA SI LEGGE
// Tre righe nella BANDA NERA in fondo al pannello — 48 px che non usa nessuno
// (il quadro e' alto 224 dentro i 320 del pannello: OR_X_OFF = 48 sopra e 48
// sotto) — scritte in pixel di PANNELLO **1:1**, non dentro `fb`.
// ⛔ Dentro `fb` sarebbe illeggibile: il blit riduce 320 -> 240 tenendo 3 pixel
// su 4, quindi a ogni carattere sparirebbe una colonna su quattro. E' la stessa
// ragione per cui la barra del volume e' stata tolta da `fb`
// ([[feedback_barra_volume_player_autonomi]]).
// ⭐ Caratteri alti il DOPPIO (8x16) perche' si leggano da lontano: 3 righe x 16
// px = 48 = esattamente la banda.
//
//   FPS 30 EMS 60 CAR 118 PIC 24.3      <- giallo, il verdetto
//   CPU  8.6 REN  6.0 BLT 12.4
//   AUD 500 SFO  12 TOT 19.7
//
//   FPS  quadri DISEGNATI al secondo — attesi **30** (la logica di Out Run e' a
//        30 Hz per fedelta' all'originale, non per compromesso)
//   EMS  quadri EMULATI al secondo — attesi **60**. ⭐ Senza questo numero il
//        bilancio non e' verificabile: se EMS non e' 2 x FPS, il primo sospetto
//        e' lo strumento.
//   CAR  carico: lavoro medio per giro / FRAME_US, in %. **E' il numero che
//        decide**: 60 = largo, 95 = al limite, oltre 100 = quadri persi.
//   PIC  picco del lavoro in UN giro, ms. La media nasconde il picco
//        ([[feedback_misurare_anche_cosa_non_cerchi]]).
//   CPU  or_read_inputs + or_run_frame, ms, PER GIRO (i due 68000)
//   REN  or_render, ms, per quadro DISEGNATO
//   BLT  or_blit, ms, per quadro DISEGNATO (e' tutto SPI)
//   AUD  blocchi audio al secondo, attesi **500** (32000 Hz / AUD_CHUNK 64)
//   SFO  giri al secondo in cui il lavoro ha SFORATO FRAME_US
//   TOT  lavoro medio per giro, ms — misurato a parte, serve a chiudere il conto
//
// ⛔ IL CONTO DEVE CHIUDERE, e NON e' la somma secca: REN e BLT sono per quadro
// DISEGNATO, e i quadri disegnati sono META' dei giri. Quindi:
//
//        CPU + (REN + BLT) / 2   ~=   TOT   ~=   CAR/100 x 16,651 ms
//
// Se non torna, il numero che si sta guardando puo' essere lo STRUMENTO e non il
// paziente — e' gia' successo tre volte ([[feedback_ottimizzare_contando_accessi]]
// caso 7: un termometro che costava piu' del lavoro misurato).
//
// ⛔⛔ CON `OR_BLIT_OVERLAP 1` DUE VOCI CAMBIANO SIGNIFICATO (ma il conto
// CHIUDE lo stesso, perche' ogni microsecondo resta contato UNA volta sola):
//   CPU  nei giri DISPARI vale ~0 — quel quadro e' gia' stato emulato dentro il
//        blit del giro prima. La media per giro scende a circa META'.
//   BLT  non e' piu' solo SPI: dentro ci sono le 262 fette del quadro dopo.
//        Sale, e **deve** salire; quello che conta e' che TOT e CAR scendano.
// ⭐ Il numero da guardare per giudicare la cura e' **EMS** (quadri emulati/s,
// bersaglio 60) insieme a CAR: sono gli unici due che non cambiano significato.
// ⭐ REN e BLT sono tenuti per quadro disegnato APPOSTA: e' il costo di UN
// rendering, cioe' il numero che serve per decidere se una cura sul video vale
// la pena. Ammortizzarli sui giri li dimezzerebbe e farebbero sembrare il video
// meta' del problema di quello che e'.
// ⚠️ AUD molto sotto 500 = il task audio non sta in pari, ed e' QUELLA la
// diagnosi, non gli fps ([[feedback_task_audio_indietro_watchdog]]).
// ⚠️ Niente `%f` nelle stringhe: su questa build escono VUOTE e l'overlay non
// compare affatto ([[esp32_arduino_no_float_printf]]). I decimi si fanno a mano
// con `%lu.%lu`, e i valori sono CLAMPATI: un numero piu' largo del previsto
// spingerebbe fuori banda quelli dopo.
//
// ---------------------------------------------------------------- COSTO
// Sei letture di `esp_timer_get_time()` per giro e UN blit della banda **una
// volta al secondo**, non a ogni quadro: 240x48 = 11.520 pixel ogni ~30 quadri.
// Si puo' fare cosi' perche' la banda nera non la tocca nessun altro — sta FUORI
// dalla finestra stretta del gioco, quindi fra un aggiornamento e l'altro resta
// esattamente com'era.
// ⭐ E il blit della banda e' chiamato **DOPO** la misura del lavoro, cosi' non
// si misura da solo: finisce dentro l'attesa di pacing, che c'e' finche' CAR<100.
// ⛔ SPENTO il 2026-08-30 su richiesta dell'utente, a lavoro finito. A 0 sparisce
// TUTTO — funzioni, stringhe e variabili — e resta solo la cura
// (`OR_BLIT_OVERLAP`). ⭐ Si riaccende con UN carattere e i numeri che sa dare
// sono documentati qui sopra: e' uno strumento permanente, non del debug da
// buttare.
#define OR_MISURA 0
#if OR_MISURA

// ⚠️ La banda e' quella IN FONDO per il giocatore, non quella in cima: li' i
// primi 16 px sono della BARRA DEL VOLUME (videoVolBarTop), e due cose nello
// stesso posto vuol dire vederne una sola quando serve l'altra.
#define OR_PF_X0     (OR_X_OFF + OR_H)         // 272: prima riga della banda
#define OR_PF_H      (TFT_FB_W - OR_PF_X0)     // 48 px del giocatore in verticale
#define OR_PF_ZOOM   2                         // caratteri alti il doppio
#define OR_PF_RIGHE  3                         // 3 x 8 x 2 = 48 = OR_PF_H esatto
#define OR_PF_COL    30                        // 30 x 8 px = 240 = tutta la larghezza
#define OR_PF_LARG   LD_FB_H                   // 240 px del giocatore in orizzontale

// giallo e bianco BYTE-SWAPPATI, come tutto cio' che va sul display SPI di
// questo progetto: 0xFFE0 -> 0xE0FF, mentre 0xFFFF e' simmetrico. Sono le
// stesse costanti di videoVolBarTop(), che gia' scrive su questo pannello.
#define OR_PF_GIALLO 0xE0FF
#define OR_PF_BIANCO 0xFFFF

static char     or_pf_riga[OR_PF_RIGHE][OR_PF_COL + 1] = { "", "", "" };
static bool     or_pf_nuovo = false;           // il testo e' cambiato dall'ultimo blit

// accumulatori del task di gioco (scritti e letti dallo stesso task)
static uint32_t or_pf_cpu = 0, or_pf_ren = 0, or_pf_blt = 0, or_pf_lav = 0;
static uint32_t or_pf_pic = 0, or_pf_giri = 0, or_pf_vis = 0, or_pf_sfo = 0;
static uint32_t or_pf_t0ms = 0;

// Scritto dal task AUDIO (core 0), letto e azzerato qui: una somma riletta una
// volta al secondo non ha bisogno di lucchetti, ma serve `volatile` perche' la
// scrive un ALTRO core ([[spinnerino_volume_multicore_race]]).
static volatile uint32_t or_pf_aud = 0;

// decimi di ms senza `%f`, clampati a 99.9 per non allargare la riga
static inline uint32_t or_pf_i(uint32_t us) { return us > 99999 ? 99 : us / 1000; }
static inline uint32_t or_pf_d(uint32_t us) { return us > 99999 ?  9 : (us % 1000) / 100; }
static inline uint32_t or_pf_cap(uint32_t v, uint32_t max) { return v > max ? max : v; }

// il VIDEO: chiamata solo sui quadri DISEGNATI (uno ogni due giri)
static void or_pf_video(uint32_t ren_us, uint32_t blt_us)
{
    or_pf_ren += ren_us;
    or_pf_blt += blt_us;
    or_pf_vis++;
}

// fine giro. `lavoro_us` e' il tempo del giro PRIMA dell'attesa di pacing: dopo,
// il totale sarebbe sempre FRAME_US e non direbbe niente.
static void or_pf_giro(uint32_t cpu_us, uint32_t lavoro_us)
{
    or_pf_cpu += cpu_us;
    or_pf_lav += lavoro_us;
    if (lavoro_us > or_pf_pic)            or_pf_pic = lavoro_us;
    if (lavoro_us > (uint32_t)FRAME_US)   or_pf_sfo++;
    or_pf_giri++;

    const uint32_t ora = millis();
    if (or_pf_t0ms == 0) { or_pf_t0ms = ora; return; }
    if (ora - or_pf_t0ms < 1000) return;

    const uint32_t ms   = ora - or_pf_t0ms;
    const uint32_t ngir = or_pf_giri ? or_pf_giri : 1;
    const uint32_t nvis = or_pf_vis  ? or_pf_vis  : 1;

    const uint32_t fps = or_pf_vis  * 1000 / ms;
    const uint32_t ems = or_pf_giri * 1000 / ms;
    const uint32_t cpu = or_pf_cpu / ngir;
    const uint32_t ren = or_pf_ren / nvis;
    const uint32_t blt = or_pf_blt / nvis;
    const uint32_t lav = or_pf_lav / ngir;
    const uint32_t car = (uint32_t)((uint64_t)lav * 100 / (uint32_t)FRAME_US);
    const uint32_t aud = or_pf_aud * 1000 / ms;
    const uint32_t sfo = or_pf_sfo * 1000 / ms;

    snprintf(or_pf_riga[0], sizeof or_pf_riga[0],
             "FPS %2lu EMS %2lu CAR %3lu PIC %2lu.%lu",
             (unsigned long)or_pf_cap(fps, 99), (unsigned long)or_pf_cap(ems, 99),
             (unsigned long)or_pf_cap(car, 999),
             (unsigned long)or_pf_i(or_pf_pic), (unsigned long)or_pf_d(or_pf_pic));
    snprintf(or_pf_riga[1], sizeof or_pf_riga[1],
             "CPU %2lu.%lu REN %2lu.%lu BLT %2lu.%lu  ",
             (unsigned long)or_pf_i(cpu), (unsigned long)or_pf_d(cpu),
             (unsigned long)or_pf_i(ren), (unsigned long)or_pf_d(ren),
             (unsigned long)or_pf_i(blt), (unsigned long)or_pf_d(blt));
    snprintf(or_pf_riga[2], sizeof or_pf_riga[2],
             "AUD %3lu SFO %3lu TOT %2lu.%lu   ",
             (unsigned long)or_pf_cap(aud, 999), (unsigned long)or_pf_cap(sfo, 999),
             (unsigned long)or_pf_i(lav), (unsigned long)or_pf_d(lav));
    or_pf_nuovo = true;

    or_pf_cpu = or_pf_ren = or_pf_blt = or_pf_lav = 0;
    or_pf_pic = or_pf_giri = or_pf_vis = or_pf_sfo = 0;
    or_pf_aud = 0;
    or_pf_t0ms = ora;
}

// Il pixel della banda in coordinate del GIOCATORE:
//   k  = riga verticale dentro la banda (0..OR_PF_H-1, 0 = subito sotto il gioco)
//   px = colonna del giocatore (0..239, 0 = a sinistra)
static inline uint16_t or_pf_pixel(int k, int px)
{
    const int riga = k / (8 * OR_PF_ZOOM);
    if (riga >= OR_PF_RIGHE) return 0x0000;
    const int gy   = (k % (8 * OR_PF_ZOOM)) / OR_PF_ZOOM;   // riga del glifo, x2
    const int col  = px >> 3;
    if (col >= OR_PF_COL) return 0x0000;
    const char ch = or_pf_riga[riga][col];
    if (!ch) return 0x0000;
    const unsigned char bits = pgm_read_byte(&FONT8[char_to_idx(ch)][gy]);
    if (!(bits & (0x80 >> (px & 7)))) return 0x0000;
    return riga == 0 ? OR_PF_GIALLO : OR_PF_BIANCO;
}

// Scrive la banda con una finestra SPI PROPRIA, e SOLO quando il testo e'
// cambiato (una volta al secondo). Ritorna true se ha scritto: in quel caso la
// finestra E' CAMBIATA e il chiamante deve ridarla al gioco con or_window(1).
// ⭐ Stessa forma di or_vol_bar() e di prof_banda_blit(): non e' inventata qui.
static bool or_pf_blit(void)
{
    if (!or_pf_nuovo || !g_strip) return false;
    or_pf_nuovo = false;

    // ⚠️ la finestra si cambia solo a DMA FERMA: una transazione in volo
    // scriverebbe nella finestra sbagliata (stessa precauzione di or_window()).
    video.waitDMA();
    video.setAddrWindow(TFT_HW_X + OR_PF_X0, 0, OR_PF_H, OR_PF_LARG);

    // 48 righe x 48 px = 2.304 pixel per blocco: sta nel dma_buffer (2560) e
    // nella striscia di lavoro del player (320x8 = 2560).
    const int PASSO = 48;
    for (int r0 = 0; r0 < OR_PF_LARG; r0 += PASSO) {
        uint16_t *p = g_strip;
        for (int y_l = r0; y_l < r0 + PASSO; y_l++) {
            const int px = (OR_PF_LARG - 1) - y_l;      // colonna del GIOCATORE
            for (int k = 0; k < OR_PF_H; k++) *p++ = or_pf_pixel(k, px);
        }
        video.write(g_strip, PASSO * OR_PF_H);
    }
    video.waitDMA();
    return true;
}
#endif

// ---------------------------------------------------------------- barra volume
// ⛔ NON si disegna piu' dentro `fb`. Lo faceva (come su CABINATONE) e usciva
// SBAGLIATA per due motivi indipendenti, segnalati dall'utente:
//   1. **POSIZIONE**: dentro `fb` la barra finisce nell'area del GIOCO, che sul
//      pannello parte a OR_X_OFF = 48 px dal bordo. Su tutti gli altri giochi la
//      barra sta invece a filo del bordo ALTO, nella banda nera.
//   2. **DIMENSIONE**: il blit riduce l'immagine in orizzontale (qui 320->240,
//      su 1944 384->240), quindi la stessa barra disegnata nel framebuffer esce
//      di lunghezza diversa da gioco a gioco e comunque diversa da quella degli
//      altri (112x16 px di pannello).
// ✅ Ora la disegna `videoVolBarTop()` in pixel di PANNELLO, nella banda nera —
// vedi emulation/video.h. E' condivisa da 1944, Ghouls e Out Run apposta: erano
// tre copie che potevano divergere, ed e' proprio l'uniformita' il requisito.
// ⚠️ NON DEVE LAMPEGGIARE: e' garantito dal fatto che si ridisegna SOLO quando
// cambia qualcosa. La banda nera non la tocca nessun altro (sta fuori dalla
// finestra stretta del gioco), quindi fra un aggiornamento e l'altro resta
// esattamente com'era ([[feedback_vol_bar_no_flicker]]).
static void or_window(int quale);        // definita sotto, insieme al blit

static void or_vol_bar(void)
{
    static int  last_fill = -2;          // -2 = mai disegnata, -1 = banda nera
    const bool  on = input_is_vol_mode_active();
    const int   f  = on ? laserdisc_current_volume() : -1;
    if (f == last_fill) return;          // niente e' cambiato: nessuno SPI
    last_fill = f;
    videoVolBarTop(g_strip, on, on ? f : 30);
    or_window(1);                        // ridai la finestra STRETTA al gioco
}

// ⭐ ORDINE DI COMPOSIZIONE validato offline rendendo l'attract mode in PNG:
// cielo (dal road hardware) -> tilemap BG -> tilemap FG -> strada -> sprite ->
// testo. Non e' un'opinione: con un altro ordine spariscono pezzi di scena.
static void or_render(void)
{
    if (pal_dirty) or_update_palette();
    memset(fb, 0, (size_t)OR_W * OR_H * 2);
    or_draw_sky();                               // fondo: cielo dal road hardware
    or_draw_tilemap(1);                          // tilemap di sfondo
    or_draw_tilemap(0);                          // tilemap di primo piano
    or_draw_road();
    or_draw_sprites();
    // testo sopra a tutto. ⭐ col0=24: delle 64 colonne della text RAM sullo
    // schermo entrano solo le ULTIME 40 (scrolldx -192 nel driver).
    or_draw_tiles(textram, 40, 28, 64, 24);
    // ⛔ la barra del volume NON sta piu' qui: vedi or_vol_bar(), che la disegna
    // nella banda nera in pixel di pannello.
    // ⛔ E nemmeno l'OVERLAY delle prestazioni: sta anch'esso nella banda, in
    // pixel di pannello (or_pf_blit()). Dentro `fb` il blit gli mangerebbe una
    // colonna ogni quattro.
}

// ============================================================ esecuzione
static void run_cpu(int which, int cycles)
{
    if (which == 1 && sub_halted) return;
    cur_cpu = which;
    m68k_set_context(which ? ctx_sub : ctx_main);
#if OR_OPCODE_WINDOW
    // ⭐ la finestra segue la CPU: vedi la nota in testa al file.
    m68k_set_opcode_window(which ? rom_sub : rom_main, ROM_SZ);
#endif
    if (which == 1 && sub_reset_pending) { m68k_pulse_reset(); sub_reset_pending = false; }
    // il livello si impone SEMPRE, anche 0: le linee di IRQ si alzano e si
    // abbassano a tempo (vedi or_run_frame), non all'acknowledge.
    m68k_set_irq(irq_line[which]);
    m68k_execute(cycles);
    m68k_get_context(which ? ctx_sub : ctx_main);
}

// ⭐⭐ Interrupt come li fa il driver (scanline_tick + update_main_irqs):
//   65/129/193  assert IRQ2      66/130/194  CLEAR IRQ2
//   223         assert VBLANK    224         CLEAR VBLANK
// Ogni linea di interrupt resta alzata **una sola scanline**, e il livello del
// main si RICAVA dai due stati: 6 se sono attivi entrambi, 4 se solo vblank,
// 2 se solo irq2.
// ⚠️ Questo era il difetto che costo' tre giri su CABINATONE: tenendo l'IRQ
// alzato fino all'acknowledge, un IRQ4 di vblank non ancora servito veniva
// SOVRASCRITTO dall'IRQ2 della scanline 65 del frame dopo. Il main perdeva il
// vblank e smetteva di aggiornare il video, mentre il sub continuava a
// disegnare la strada: sullo schermo restava solo quella.
// ⚠️ Il vettore IRQ6 punta a $7DC0 = `STOP #$2700`, che ferma il 68000 per
// sempre: con le linee temporizzate correttamente IRQ2 e vblank non si
// sovrappongono mai (65..66 contro 223..224) e il livello 6 non si presenta.
static int irq2_state = 0, vblank_state = 0;

static inline void update_main_irqs(void)
{
    irq_line[0] = (vblank_state && irq2_state) ? 6 :
                  (vblank_state ? 4 : (irq2_state ? 2 : 0));
}

// ============================================================================
// ⭐⭐⭐ OR_BLIT_OVERLAP — le STESSE IDENTICHE fette, eseguite DENTRO le attese
// del DMA dello SPI.
//
// IL FATTO, dai numeri dell'overlay (2026-08-30): `CPU 13.4 REN 11.0 BLT 12.3`,
// TOT 24 ms su 16,651 = CAR 152%. E su CABINATONE lo stesso gioco misura
// `CPU 17.8 REN 11.6 SCA 5.5 BLT 0.0` = 157% ([[outrun_performance_margini]]):
// le due console sono allo STESSO punto, quindi il porting non e' mal
// configurato — Out Run e' semplicemente caro ([[feedback_confronta_col_gemello_sano]]:
// il gemello e' stato guardato PRIMA di inseguire ipotesi).
//
// ⭐ Ma i 12,3 ms di BLT non sono LAVORO, sono ATTESA. `Video::write()` ha **un
// solo dma_buffer**: a ogni striscia fa `spi_device_get_trans_result()` e si
// ferma finche' la striscia precedente non e' uscita. 224x240 px = 107.520 byte
// che sullo SPI valgono ~10,8 ms in cui il P4 non fa NIENTE. Su CABINATONE
// questa leva NON esiste (li' BLT e' gia' 0,0: PPA + DSI in DMA); qui si', ed e'
// gia' stata pagata su questa stessa scheda dal NeoGeo, dove ha portato Metal
// Slug da 22 a 47 fps (`NG_BLIT_OVERLAP` in neogeo.cpp, da cui questo codice e'
// trascritto — [[feedback_porta_non_riprogettare]]).
//
// ⭐⭐ PERCHE' SI PUO' RIEMPIRE QUELL'ATTESA, ed e' SICURO:
// il blit LEGGE `fb` e scrive `g_strip`; i due 68000 scrivono tileram, palram,
// sprram, textram, workram, la RAM condivisa e roadram. **Non hanno un solo byte
// in comune** — `fb` lo riempie or_render(), che a quel punto ha gia' finito, e
// il render successivo arriva solo dopo `video.waitDMA()`.
//
// ⛔⛔ E L'EMULAZIONE NON CAMBIA DI UN CICLO, ne' di un quadro:
//   prima  giro 0: quadro 0, render(stato dopo 0), blit · giro 1: quadro 1
//   dopo   giro 0: quadro 0, render(stato dopo 0), blit **+ quadro 1** · giro 1: —
// La sequenza delle fette e' la stessa, gli IRQ cadono alle stesse scanline, e
// **il render vede esattamente lo stesso stato di prima**. Cambia solo QUANDO,
// rispetto al video.
// ⚠️ Prezzo dichiarato: nei giri DISPARI `or_read_inputs()` viene chiamata dopo
// che quel quadro e' gia' stato emulato, quindi i quadri dispari usano gli
// ingressi letti un giro prima (~26 ms). Le rampe analogiche (sterzo 4, gas 20,
// freno 40 per giro) avanzano dello stesso passo: cambia la fase, non la
// velocita' di risposta.
// ⛔ Con OR_BLIT_OVERLAP 0 `or_ovl_linea` resta sempre 0 e or_run_frame() fa
// tutte e 262 le fette come prima: l'interruttore riporta il codice di ieri.
// ============================================================================
#define OR_BLIT_OVERLAP 1

// prossima scanline del quadro IN CORSO. ⚠️ Va riazzerata all'avvio del gioco:
// su questo firmware si RIENTRA nel menu senza riavvio, quindi uscendo a meta'
// di un blit resterebbe sporca ([[feedback_init_una_volta_sola_vs_rientro]]).
static int or_ovl_linea = 0;

// Il CORPO di UNA scanline — UNA sola copia, trascritta da CABINATONE. Sta in
// una funzione a parte proprio perche' non ne esistano due che possono
// divergere: la chiamano sia or_run_frame() sia or_emula_fette().
static inline void or_una_linea(int line)
{
    switch (line) {
        case 65: case 129: case 193: irq2_state = 1;  update_main_irqs(); break;
        case 66: case 130: case 194: irq2_state = 0;  update_main_irqs(); break;
        case 223: vblank_state = 1; irq_line[1] = 4;  update_main_irqs(); break;
        case 224: vblank_state = 0; irq_line[1] = 0;  update_main_irqs(); break;
        default: break;
    }
    run_cpu(0, CYC_HALF); run_cpu(1, CYC_HALF);
    run_cpu(0, CYC_HALF); run_cpu(1, CYC_HALF);
}

// esegue le fette da quella corrente fino a `fino_a` (esclusa)
static void or_emula_fette(int fino_a)
{
    if (fino_a > LINES) fino_a = LINES;
    while (or_ovl_linea < fino_a) { or_una_linea(or_ovl_linea); or_ovl_linea++; }
}

// Chiude il quadro: le fette che il blit non ha fatto in tempo a coprire. Con
// l'overlap spento sono tutte e 262, cioe' il comportamento di prima.
static void or_run_frame(void)
{
    or_emula_fette(LINES);
    or_ovl_linea = 0;
}

// ==================================================== AUDIO — Z80 + YM2151 + PCM
// Hardware sonoro di Out Run, dal driver (`sound_map` / `sound_portmap`):
//   Z80 @ 16 MHz/4 = 4 MHz
//     0000-EFFF  ROM (epr-10187, 32 KB dentro una region da 64 KB)
//     F000-F0FF  SegaPCM 315-5218, mirror 0x0700 -> risponde su F000-F7FF
//     F800-FFFF  RAM 2 KB
//   porte (global_mask 0xFF):
//     00-01 mirror 3E -> YM2151 @ 4 MHz, quindi risponde su 00-3F
//     40    mirror 3F -> soundlatch (`pread` del mapper), risponde su 40-7F
//
// ⚠️⚠️ L'UNICO interrupt del Z80 sonoro e' l'NMI dal mapper: in Out Run la linea
//   IRQ del YM2151 **non e' collegata** (nel machine config ci sono solo le
//   `add_route`, nessun `irq_handler`). E' la differenza piu' importante
//   rispetto a Ghouls, dove l'IRQ nasce dal timer del chip: qui il programma
//   sonoro lavora a POLLING sullo stato e l'NMI serve solo a consegnare il
//   comando. ⭐ Il TEMPO se lo da' il Timer A del YM2151: il ciclo principale si
//   blocca su `$0A61: IN A,($01) / BIT 0,A / JP Z,$0A61` finche' non trabocca.
//   Registro $10=$83 -> NA=524 -> 125,00 tick/s esatti, da cui i 125,00 BPM
//   esatti di Magical Sound Shower.
#define SND_ROM_SZ   0x10000
#define SNDRAM_SZ    0x800
#define PCMREG_SZ    0x100
#define AUD_RATE     32000                // I2S nativo dello SPINNERINO
#define OR_YM_CLOCK  4000000
#define OR_Z80_HZ    4000000
#define AUD_CHUNK    64                   // campioni per blocco (2 ms)
// cicli Z80 per campione in virgola fissa 16.16: 4000000/32000 = 125 esatti
#define Z80_CYC_PER_SAMPLE_FP ((uint32_t)(((uint64_t)OR_Z80_HZ << 16) / AUD_RATE))
// ⭐ Proporzioni del mix: sono quelle del driver (`add_route` 0.30 per il YM2151
// e 0.70 per il SegaPCM), applicate nel ciclo di mix come 77/256 e 179/256.
// Non sono messe a occhio: il picco della sola parte FM, misurato sull'harness
// di CABINATONE su 34 s di gioco, e' 28362 = -1,25 dBFS; 0,30 x 28362 = 8509
// lascia 22937 di margine al PCM. WAV di 34 s: picco 18304/16473, ZERO clipping.

static uint8_t *snd_rom = nullptr;        // 64 KB PSRAM
static uint8_t *sndram  = nullptr;        // 2 KB interna
static uint8_t *pcmreg  = nullptr;        // 256 byte: registri SegaPCM
static void    *ym      = nullptr;
static Z80      sndcpu;
static uint8_t  ym_addr = 0;              // registro latchato dalla porta pari
// ⚠️ i buffer del YM2151 sono a 32 BIT, non 16: in ym2151.h `SAMP` e'
// `stream_sample_t` = INT32. Allocandoli a 16 bit, ym2151_update_one scrive il
// doppio dei byte e sfonda l'heap — crash gia' pagato su Ghouls.
static int32_t *ym_l = nullptr, *ym_r = nullptr;
static int16_t *aud_out = nullptr;
static bool     audio_ok = false;

// ---- SegaPCM 315-5218
// ⚠️ I registri NON sono 16 canali da 16 byte contigui: sono **due blocchi da 8
// separati da 0x80** (nel driver `.select(0x78)` -> canale = (offset >> 3) & 15).
//   basso  +2 volume L · +3 volume R · +4/5 loop addr (bit 8-15 / 16-23)
//          +6 end addr · +7 delta (passo)
//   alto   +4/5 indirizzo corrente · +6 CTRL (bit0 spento, bit1 loop disabilitato,
//          bit4-6 banco)
// ⚠️ Gli offset non elencati sono **RAM vera**: il driver sonoro Sega li usa come
// scratchpad, non sono "registri sconosciuti da ignorare".
// ⭐ L'indirizzo qui e' un accumulatore **16.16** invece del 16.8 dell'hardware:
// cosi' il ricampionamento da 31250 Hz (clock/128) a 32000 e' ESATTO e gratuito,
// perche' 31250/32000 = 125/128 e basta avanzare di `delta * 250`.
#define PCM_ROM_SZ 0x80000
static uint8_t *pcm_rom = nullptr;
static uint32_t pcm_addr[16];

static inline void spcm_write(uint16_t a, uint8_t d)
{
    const uint8_t off = (uint8_t)a;               // mirror 0x0700: 8 alias, 256 reg
    pcmreg[off] = d;
    if (off & 0x80) {
        const int ch = (off >> 3) & 15;
        switch (off & 7) {
            case 4: pcm_addr[ch] = (pcm_addr[ch] & 0xFF00FFFFu) | ((uint32_t)d << 16); break;
            case 5: pcm_addr[ch] = (pcm_addr[ch] & 0x00FFFFFFu) | ((uint32_t)d << 24); break;
            case 6: if (d & 1) pcm_addr[ch] &= 0xFFFF0000u; break;   // spento: frazione a 0
        }
    }
}

static inline uint8_t spcm_read(uint16_t a)
{
    const uint8_t off = (uint8_t)a;
    if (off & 0x80) {
        const int ch = (off >> 3) & 15;
        // ⚠️ devono restituire l'indirizzo VIVO, non l'ultimo scritto: il driver
        // li polla per sapere se un campione e' finito.
        if ((off & 7) == 4) return (uint8_t)(pcm_addr[ch] >> 16);
        if ((off & 7) == 5) return (uint8_t)(pcm_addr[ch] >> 24);
    }
    return pcmreg[off];
}

// ---- bus del Z80 sonoro
// ⚠️ ADATTAMENTO: su CABINATONE queste erano i campi di una `Z80Bus`
// (z80_bus.cpp con le globali). Qui il core e' incluso DENTRO questa unita' di
// compilazione coi simboli Or* (vedi i #define in testa), quindi le sue globali
// si definiscono qui — la stessa forma di spaceace.cpp, cps1944.cpp e
// ghouls.cpp. Il CORPO delle funzioni e' identico carattere per carattere.
extern "C" byte RdZ80(word a)
{
    if (a < 0xF000) return snd_rom[a];
    if (a < 0xF800) return spcm_read(a);          // SegaPCM (mirror 0x0700)
    return sndram[a & (SNDRAM_SZ - 1)];
}

extern "C" void WrZ80(word a, byte v)
{
    if (a < 0xF000) return;                       // ROM
    if (a < 0xF800) { spcm_write(a, v); return; }
    sndram[a & (SNDRAM_SZ - 1)] = v;
}

extern "C" byte InZ80(word p)
{
    const uint8_t port = (uint8_t)p;              // global_mask(0xFF)
    if (port < 0x40) {                            // YM2151, mirror 0x3E
        if (port & 1) return (uint8_t)ym2151_read_status(ym);
        return 0xFF;
    }
    if (port < 0x80) return snd_latch_read();     // soundlatch: `pread`
    return 0xFF;
}

extern "C" void OutZ80(word p, byte v)
{
    const uint8_t port = (uint8_t)p;
    if (port < 0x40) {                            // YM2151: pari = indirizzo, dispari = dato
        if (port & 1) ym2151_write_reg(ym, ym_addr, v);
        else          ym_addr = v;
    }
}

// Prelievo dell'OPCODE: il core lo DICHIARA e basta, lo definisce il gioco
// (spaceace.cpp, cps1944.cpp e ghouls.cpp fanno lo stesso). Qui e' la stessa
// lettura del bus — infatti CABINATONE passava `or_snd_rd` anche come quinto
// campo di Z80Bus.
extern "C" unsigned char OpZ80_INL(unsigned short a) { return RdZ80((word)a); }
extern "C" word LoopZ80(Z80 *R)          { (void)R; return INT_NONE; }
extern "C" void PatchZ80(Z80 *R)         { (void)R; }

// ---- fine del bus sonoro ---------------------------------------------------

static void or_audio_init(void)
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

    ym = ym2151_init(OR_YM_CLOCK, AUD_RATE);
    if (!ym) return;
    ym2151_reset_chip(ym);

    ResetZ80(&sndcpu);
    sndcpu.IRequest   = INT_NONE;
    sndcpu.IAutoReset = 1;
    sndcpu.TrapBadOps = 0;

    audio_ok = true;
}

// Task audio sul CORE 0 (l'emulazione sta sul core 1). E' l'audio a dare il
// tempo al Z80 sonoro, non il video: laserdisc_audio_write e' BLOCCANTE finche'
// il DMA non consuma, quindi i 32000 campioni/s scandiscono da soli il clock
// della CPU sonora. Lezione di Hyper Sports: se lo ritma il video, la musica
// rallenta quando calano gli fps.
// ⭐ GUADAGNO + SOFT LIMITER (vedi il commento nel mix, piu' sotto).
// ⚠️ ALZATO da 2/1 a **7/2 = 3,5x (+11 dB)** il 2026-08-10: col 2/1 l'utente ha
// riferito che era ancora basso. Il conto: il picco del mix misurato
// sull'harness di CABINATONE e' 18304 su 32767, e i giochi normali di questo
// firmware saturano a fondo scala — mancavano ~5 dB solo per pareggiare, e
// l'utente ne vuole di piu'. Con 3,5x tutto cio' che sta sotto 6857 passa
// guadagnato per intero; sopra interviene il limiter, che comprime con
// continuita' invece di tagliare (e' li' apposta).
// ⛔ Non si alza oltre senza risentire: da qui in su la compressione comincia a
// mangiarsi la dinamica del motore e delle percussioni, e la cura giusta
// diventerebbe alzare la SOGLIA, non il guadagno.
#define OR_BOOST_NUM 7
#define OR_BOOST_DEN 2
#define OR_LIM_T     24000

static void or_audio_task(void *arg)
{
    (void)arg;
    laserdisc_audio_begin();
    uint32_t acc = 0;
    uint32_t respiro = 0;                 // contatore per lo yield anti-watchdog
    while (!g_stop) {
        // la CPU sonora e' appena stata rilasciata dal reset: riparte da zero e
        // si azzera anche lo stato del latch, altrimenti un comando rimasto in
        // sospeso la troverebbe nella finestra cieca dei primi ~12,5 us.
        // ⚠️ Il reset si serve solo a inizio blocco (64 campioni = 2 ms) mentre
        // `snd_halted = false` ha effetto subito: per al piu' 2 ms qui gira il
        // Z80 NON ancora resettato. Innocuo e misurato senza effetti.
        if (snd_reset_pending) {
            snd_reset_pending = false;
            ResetZ80(&sndcpu);
            sndcpu.IRequest = INT_NONE;
            snd_fifo_r = snd_fifo_w;                 // svuota la coda
            snd_pbf = false;
            snd_nmi_pending = false;
            snd_guard = SND_GUARD_SAMPLES;
        }
        // 1) il Z80 sonoro avanza del tempo del blocco, a fette di 8 campioni.
        //    L'NMI si consegna sul FRONTE, una volta sola per comando.
        for (int s = 0; s < AUD_CHUNK; s += 8) {
            acc += Z80_CYC_PER_SAMPLE_FP * 8;
            const int cyc = (int)(acc >> 16);
            acc &= 0xFFFF;
            if (!snd_halted) {
                // niente NMI finche' il Z80 non ha finito la sua inizializzazione
                if (snd_guard) snd_guard = (snd_guard > 8) ? (snd_guard - 8) : 0;
                else if (snd_nmi_pending) { snd_nmi_pending = false; IntZ80(&sndcpu, INT_NMI); }
                ExecZ80(&sndcpu, cyc);
            }
        }

        // 2) il chip genera il blocco (qui dentro avanzano i suoi timer, che il
        //    programma sonoro legge a polling)
        SAMP *bufs[2] = { (SAMP *)ym_l, (SAMP *)ym_r };
        ym2151_update_one(ym, bufs, AUD_CHUNK);

        // ⛔⛔ VOLUME + GUADAGNO. Due cose diverse, entrambe mancanti prima:
        // 1. il VOLUME non lo applicava nessuno. `laserdisc_audio_write()` scrive
        //    i campioni sull'I2S tali e quali, non conosce `audio.volumeSetting`:
        //    lo applica ogni player nel proprio mix (DL, Space Ace, DL2 lo fanno).
        // 2. il livello era comunque BASSO anche al massimo, e non e' un'opinione:
        //    il picco misurato sull'harness di CABINATONE su 34 s e' **18304 su
        //    32767 = -5 dBFS**, mentre i giochi normali di questo firmware
        //    arrivano a fondo scala (`value * 128 / volumeSetting` con
        //    volumeSetting = 1 satura).
        // ⭐ Cura nella forma gia' usata da laserdisc.cpp e dl2.cpp: guadagno +
        //    SOFT LIMITER. Sotto soglia il segnale passa intatto e guadagnato;
        //    sopra viene compresso con continuita' fino a 32767 invece di essere
        //    tagliato di netto (il clamp secco si sente come "gracchio").
        // ⚠️ Le PROPORZIONI del mix (77/256 YM + 179/256 PCM = le `add_route`
        //    0.30/0.70 del driver) NON si toccano: il guadagno e' applicato dopo,
        //    uguale sui due contributi, quindi il bilanciamento resta quello.
        const int vs  = laserdisc_current_volume();
        const int vol = (vs >= 30) ? 0 : (31 - vs);        // 0..30, 30 = barra piena
        // ⚡ UN moltiplicatore per BLOCCO, in virgola fissa 8.8: dentro il ciclo
        // restano una moltiplicazione e uno shift. Qui NON si mettono divisioni
        // per campione ([[feedback_no_costly_calls_in_render]]).
        const int mul = (OR_BOOST_NUM * vol * 256) / (OR_BOOST_DEN * 30);
        // fondo scala IN INGRESSO al limiter col guadagno corrente: scala col
        // volume, cosi' a barra bassa il limiter e' del tutto inerte.
        int in_max = (32767 * mul) >> 8;
        if (in_max <= OR_LIM_T) in_max = OR_LIM_T + 1;
        // 3) SegaPCM + mix stereo -> I2S. La write blocca: e' il nostro orologio.
        for (int i = 0; i < AUD_CHUNK; i++) {
            int32_t pl = 0, pr = 0;
            for (int ch = 0; ch < 16; ch++) {
                const uint8_t *lo = pcmreg + ch * 8;          // registri bassi
                uint8_t       *hi = pcmreg + 0x80 + ch * 8;   // registri alti
                const uint8_t ctrl = hi[6];
                if (ctrl & 1) continue;                       // canale spento
                uint32_t ad = pcm_addr[ch];
                // ⚠️ fine campione: confronto con **end + 1**. MAME master ha
                // tolto il +1 nel commit 926cb394b (2026-06-19) senza verifica
                // hardware nella PR, contro libvgm e vent'anni di MAME: senza il
                // +1 ogni campione viene troncato di 256 byte. Provato sui dati
                // veri di pcm.bin: senza, il canale 0 fa 993 loop invece di 300
                // (un ronzio) e gli ultimi 256 byte di ogni banco non suonano.
                if ((ad >> 24) == (uint8_t)(lo[6] + 1)) {
                    if (ctrl & 2) {                           // loop disabilitato
                        hi[6] = (uint8_t)(ctrl | 1);          // il canale si SPEGNE da solo
                        pcm_addr[ch] = ad & 0xFFFF0000u;
                        continue;
                    }
                    ad = ((uint32_t)lo[5] << 24) | ((uint32_t)lo[4] << 16);
                }
                // il banco si rilegge da ctrl a OGNI campione: cambiarlo mentre
                // il canale suona cambia ROM all'istante. BANK_512 = (ctrl & 0x70) << 12
                const uint32_t ra = (((uint32_t)(ctrl & 0x70) << 12) | (ad >> 16))
                                    & (PCM_ROM_SZ - 1);
                const int32_t v = (int32_t)pcm_rom[ra] - 0x80;   // 8 bit unsigned, 0x80 = silenzio
                pl += v * (lo[2] & 0x7F);                        // il bit 7 del volume si maschera
                pr += v * (lo[3] & 0x7F);
                pcm_addr[ch] = ad + (uint32_t)lo[7] * 250u;      // 31250 -> 32000 esatto
            }
            // ⭐ proporzioni del driver: YM2151 a 0.30, SegaPCM a 0.70
            // (`add_route`). 77/256 = 0,3008 · 179/256 = 0,6992.
            int l = ((ym_l[i] * 77) >> 8) + ((pl * 179) >> 8);
            int r = ((ym_r[i] * 77) >> 8) + ((pr * 179) >> 8);
            // volume + guadagno, poi soft limiter sulle sole punte
            l = (l * mul) >> 8;
            r = (r * mul) >> 8;
            { int a = (l < 0) ? -l : l;
              if (a > OR_LIM_T) {
                  a = OR_LIM_T + ((a - OR_LIM_T) * (32767 - OR_LIM_T)) / (in_max - OR_LIM_T);
                  if (a > 32767) a = 32767;
                  l = (l < 0) ? -a : a;
              } }
            { int a = (r < 0) ? -r : r;
              if (a > OR_LIM_T) {
                  a = OR_LIM_T + ((a - OR_LIM_T) * (32767 - OR_LIM_T)) / (in_max - OR_LIM_T);
                  if (a > 32767) a = 32767;
                  r = (r < 0) ? -a : a;
              } }
            // il clamp resta la rete di sicurezza finale
            if (l >  32767) l =  32767;  if (l < -32768) l = -32768;
            if (r >  32767) r =  32767;  if (r < -32768) r = -32768;
            aud_out[i * 2]     = (int16_t)l;
            aud_out[i * 2 + 1] = (int16_t)r;
        }
        laserdisc_audio_write(aud_out, AUD_CHUNK);
#if OR_MISURA
        // un blocco consegnato all'I2S: attesi 500/s (32000 Hz / AUD_CHUNK 64).
        // Se la voce AUD dell'overlay e' molto sotto, il task audio NON sta in
        // pari ed e' quella la diagnosi, non gli fps.
        or_pf_aud++;
#endif
        // ⛔⛔ RESPIRO OBBLIGATORIO — e' la rete che evita il RIAVVIO.
        // Finche' il task sta in tempo reale, la write qui sopra BLOCCA e questo
        // task dorme: l'idle del core 0 gira e non succede nulla. Ma se resta
        // indietro (buffer caldi finiti in PSRAM, picco di carico) la write **non
        // blocca piu'** e questo ciclo gira al 100% a priorita' 6 sul core 0:
        // l'idle non viene mai schedulato e dopo 5 s scatta il task watchdog ->
        // la scheda si RIAVVIA. E' il difetto segnalato dall'utente («a volte
        // quando lo faccio partire si riavvia»), intermittente proprio perche'
        // dipende da dove sono finiti i buffer.
        // ⚠️ Un tick ogni 16 blocchi = 1 ms ogni 32: quando l'audio e' in pari non
        // costa nulla (il DMA ha 6 descrittori da 256 frame = 48 ms di margine).
        if ((++respiro & 15) == 0) vTaskDelay(1);
    }
    laserdisc_audio_end();
    g_aud_running = false;
    vTaskDelete(nullptr);
}

// ============================================================ input
// ⭐ Mappa dei tasti (il bridge C6 manda tasti digitali, nessun asse analogico):
//   LEFT/RIGHT = sterzo · A (FIRE) = acceleratore · **GIU' = freno**
//   **B (EXTRA) = marcia lenta/veloce** (alterna) · START = avvia · COIN = credito
// ⛔⛔ QUI NON SI COPIA LA MAPPA DI CABINATONE. La' il cambio sta sul tasto Y
//    perche' quella console ha il **bridge C6 V2**, che manda X e Y separati
//    ([[cabinatone_c6_bridge_v2]]); su SPINNERINO PLUS il ponte e' quello a UN
//    filo e il tasto Y **non arriva**. `Input::button_y_pressed()` esiste e
//    compila (legge `uart_bridge_y_pressed` da `BRIDGE_FLAG_Y`), ma quel flag
//    resta 0 per sempre: col cambio su Y la marcia sarebbe stata **inutilizzabile
//    senza nessun errore visibile**. Quindi si torna alla mappa pre-V2 di
//    CABINATONE: la marcia su B e il freno su GIU'.
// ⚠️ Il freno non deve MAI stare su START: tenere START 3 s e' l'uscita dal
//    gioco (qui) e su altre schede fa perfino `esp_restart()`. Con il freno su
//    START una frenata lunga chiuderebbe la partita.
//
// Porta SERVICE, dal driver — bit e polarita':
//   0x08 START1 (attivo BASSO) · 0x40 COIN1 (attivo BASSO)
//   0x10 GEAR SHIFT — attivo ALTO, ed e' un PORT_TOGGLE: sul cabinato e' un
//        INTERRUTTORE a due posizioni, non un pulsante. Qui lo alterna il
//        fronte di salita di Y. Stato iniziale 0 = marcia LENTA.
//   0x02 service/test, 0x04 service1, 0x80 coin2: lasciati a riposo.
// ⚠️ Prima si usavano 0x01 per COIN e 0x02 per START: 0x01 e' "unknown" e 0x02
//    e' il TEST switch. Erano i bit sbagliati.
static uint8_t gear_high = 0;             // 0 = lenta, 1 = veloce
// ⚠️ FUORI dalla funzione apposta: era una `static` LOCALE e non veniva
// riazzerata al rilancio. Su questo firmware si RIENTRA nel menu senza riavvio
// ([[feedback_init_una_volta_sola_vs_rientro]]): uscendo con B premuto,
// `prev_shift` restava vero e alla partita dopo la PRIMA cambiata non si
// registrava. Ora si azzera con `gear_high` nella reset del gioco.
static bool gear_prev = false;

// ⭐⭐⭐ OR_MAPPA_CABINATO — la mappa dei tasti PROPRIA di Out Run
// (richiesta utente 2026-09-02: «il FIRE del pin 48 deve diventare COIN e il
// pin 47 deve diventare START, ma solo su questo gioco»).
//
// ⛔ La mappa richiesta NON si ottiene leggendo `b`, e il perche' sta tutto nel
// livello di input, non qui:
//   GPIO48 (`EC11_PIN_SW`) -> `buttons_get()` lo mette in `BUTTON_FIRE`,
//                             **nello stesso bit del tasto A (GPIO2)**: quel
//                             bit non puo' distinguerli
//                             ([[feedback_tasto_esiste_ma_non_arriva]] cor. 1);
//   GPIO47 (`BTN_START`)   -> non arriva MAI come tasto premuto: `Input::update()`
//                             lo usa come sorgente della sequenza automatica
//                             gettone->START (80 ms di `BUTTON_COIN`, poi
//                             240 ms di `BUTTON_START | BUTTON_EXTRA`).
// Si leggono percio' i GPIO direttamente, qui dentro.
//
// ⭐ Perche' la modifica sta in QUESTO file e non in `input.cpp`: quella
// sequenza e quel merge servono agli altri 27 giochi. Raggio d'azione: un solo
// gioco ([[feedback_una_modifica_per_flash]]). Costo: tre `digitalRead` per
// giro — `or_read_inputs()` e' chiamata UNA volta per quadro e non dal ciclo di
// bus, quindi non e' il caso che rese necessaria la throttle di
// `read_digital_joystick()`. I pin sono gia' `INPUT_PULLUP` da `Input::begin()`
// (`input.cpp:215` e `:216`).
//
// ⚠️ CONSEGUENZE DICHIARATE, tutte volute:
//   1. l'acceleratore resta **solo** sul tasto A (GPIO2) e sul FIRE del gamepad
//      BT — non piu' sul 48, che ora e' il gettone. Si legge il GPIO2 per conto
//      suo e i tasti del ponte con `bridge_buttons()`, proprio perche'
//      `BUTTON_FIRE` contiene ANCHE il 48;
//   2. il gettone non arriva piu' da solo premendo START: si mette il gettone
//      col 48 e **poi** si preme il 47, come su un cabinato vero;
//   3. tenendo il 48 per 3 s si entra ancora nella regolazione del volume (e'
//      globale, dentro `buttons_get()`): il gettone si toglie appena la vol
//      mode e' attiva, ma quei 3 secondi di pressione il gioco li conta come un
//      credito. Conviene regolare il volume prima di mettere il gettone;
//   4. l'uscita al menu resta il long press del 47 (4 s, `input.cpp`): quel
//      tasto fa le due cose insieme, START corto e uscita lungo.
// 👉 Rimettere **0** per tornare esattamente alla mappa precedente.
#define OR_MAPPA_CABINATO 1

static void or_read_inputs(void)
{
    const uint8_t b     = g_input->buttons_get();
    const bool    brake = (b & BUTTON_DOWN) != 0;     // GIU'

#if OR_MAPPA_CABINATO
    // ⛔⛔⛔ IL GAMEPAD BLE VA SOMMATO, NON SOSTITUITO.
    // La mappa «48 = gettone, 47 = START» chiedeva di leggere i GPIO invece dei
    // bit di `buttons_get()`. Fin qui giusto — ma leggendo SOLO i GPIO si e'
    // tolto al pad BLE il gettone e lo START, che infatti non funzionavano
    // (referto utente 2026-09-05). L'acceleratore no, ed e' la prova: li' il
    // pad era gia' sommato con `bridge_buttons()`, tre righe piu' sotto.
    // 👉 Sostituire una sorgente di input e' quasi sempre sbagliato: le
    //    sorgenti si SOMMANO. [[feedback_guardia_sbagliata_sull_input]]
    //
    // ⭐ Si somma `bridge_buttons()` e NON `b`: `b` contiene anche la sequenza
    // FINTA che `Input::update()` inietta alla pressione del 47 (80 ms di
    // COIN, poi START+EXTRA). Sommando `b` tornerebbe il gettone regalato da
    // START, che questa mappa toglie apposta (conseguenza dichiarata n. 2).
    // `bridge_buttons()` da' solo i tasti VERI del pad.
    const uint8_t bb = g_input->bridge_buttons();
    const bool coin  = (!digitalRead(EC11_PIN_SW) || (bb & BUTTON_COIN) != 0)
                       && !input_is_vol_mode_active();
    const bool start = !digitalRead(BTN_START) || (bb & BUTTON_START) != 0;
    const bool gas   = !digitalRead(BTN_A_PIN)  || (bb & BUTTON_FIRE)  != 0;
#else
    const bool coin  = (b & BUTTON_COIN)  != 0;
    const bool start = (b & BUTTON_START) != 0;
    const bool gas   = (b & BUTTON_FIRE)  != 0;
#endif

    // ⛔⛔⛔ IL TASTO B NON ARRIVA SOLO QUANDO LO PREMI TU.
    // `Input::update()` ha una sequenza che, alla pressione di START, INIETTA
    // dei tasti finti per far partire i giochi (input.cpp, `start_seq_t0`):
    //     dt <  80 ms  -> BUTTON_COIN
    //     dt < 400 ms  -> BUTTON_START | **BUTTON_EXTRA**
    // Quei 240 ms di EXTRA finto sono ~14 giri di questa funzione: il fronte di
    // salita c'e' e la marcia **si alzava da sola a ogni START**. Da qui il
    // referto dell'utente: *«all'avvio solo col pulsante A arriva a 280 km/h
    // senza premere il B»* — cioe' la partita cominciava in marcia ALTA.
    // ⭐ Il finto si distingue dal vero perche' arriva SEMPRE INSIEME a START
    // (`states |= BUTTON_START | BUTTON_EXTRA`, stessa riga), mentre una
    // cambiata vera si fa guidando, con START a riposo.
    // ⭐ Curato QUI e non in `input.cpp`: quella sequenza serve a far partire
    // altri 27 giochi ([[feedback_una_modifica_per_flash]]: si cura dove il
    // difetto si manifesta, non dove nasce, se curarlo alla nascita vuol dire
    // toccare venti casi sani).
    // ⚠️ Prezzo dichiarato, ed e' piccolo: `BUTTON_START` viene messo in
    // `states` **solo** dentro quella finestra di 240 ms (dopo, anche tenendo
    // premuto il tasto fisico, il gioco non vede piu' START). Quindi l'unico
    // caso perso e' premere B **entro un quarto di secondo** dall'aver premuto
    // START — cioe' mentre la partita sta ancora cominciando.
    const bool shift = (b & BUTTON_EXTRA) && !(b & BUTTON_START);

    // ---- digitali
    // Il cambio e' un INTERRUTTORE, non un pulsante: alterna sul fronte di
    // salita di B. ⭐ Confermato alla FONTE (MAME `sega/segaorun.cpp`,
    // `PORT_START("SERVICE")`): `PORT_BIT( 0x10, IP_ACTIVE_HIGH, IPT_BUTTON3 )
    // PORT_NAME("Gear Shift") PORT_TOGGLE` — bit 0x10, **attivo ALTO** e a
    // levetta, mentre COIN1 (0x40) e START1 (0x08) sono attivi BASSI. Le tre
    // polarita' qui sotto sono quelle, verificate riga per riga.
    if (shift && !gear_prev) gear_high ^= 1;
    gear_prev = shift;

    uint8_t v = 0xFF;
    if (coin)             v &= (uint8_t)~0x40;      // COIN1  (GPIO48)
    if (start)            v &= (uint8_t)~0x08;      // START1 (GPIO47)
    if (gear_high)        v |=  0x10;               // attivo ALTO
    else                  v &= (uint8_t)~0x10;
    in_port = v;

    // ---- analogici, simulati da tasti digitali. Le velocita' di corsa sono
    // quelle del driver (PORT_KEYDELTA): sterzo 4, gas 20, freno 40 per frame.
    // Lo sterzo si RICENTRA da solo al rilascio, come un volante con la molla.
    // ⚠️ Se lo sterzo risultasse invertito basta scambiare LEFT e RIGHT qui: il
    //    set `outrun` non ha PORT_REVERSE (ce l'hanno altri set).
    int s = adc_val[0];
    if      (b & BUTTON_LEFT)  s -= 4;
    else if (b & BUTTON_RIGHT) s += 4;
    else if (s > 0x80)  { s -= 6; if (s < 0x80) s = 0x80; }
    else if (s < 0x80)  { s += 6; if (s > 0x80) s = 0x80; }
    if (s < 0x20) s = 0x20;
    if (s > 0xE0) s = 0xE0;
    adc_val[0] = (uint8_t)s;

    int g = adc_val[1] + (gas ? 20 : -20);                 // acceleratore (A)
    adc_val[1] = (uint8_t)(g < 0 ? 0 : (g > 0xFF ? 0xFF : g));
    int f = adc_val[2] + (brake ? 40 : -40);               // freno (GIU')
    adc_val[2] = (uint8_t)(f < 0 ? 0 : (f > 0xFF ? 0xFF : f));
}

// ============================================================ blit sul pannello
// ⛔ QUI NON SI COPIA DA CABINATONE: la' c'erano un pannello DSI 1024x600, il PPA
// hardware e lcd_draw_bitmap, che qui NON esistono. Il modello e' gh_blit() /
// c1944_blit(), che girano su QUESTA scheda.
//
// ⭐⭐ GEOMETRIA. Il pannello e' montato in PORTRAIT ma si pilota TRASPOSTO: una
// "riga" scritta e' larga TFT_FB_W=320 e ce ne sono LD_FB_H=240. In coordinate
// UTENTE (portrait 240x320):
//     x_l = y_p        y_l = 239 - x_p
// cioe' l'asse da 320 e' il VERTICALE visto dall'utente e quello da 240
// l'ORIZZONTALE. Quindi:
//     x_l = OR_X_OFF + game_y      (224 righe di gioco nei 320 verticali)
//     y_l = 239 - x_p,  x_p = game_x ridotto da 320 a 240
// Risultato: il gioco esce DIRITTO, con le BANDE NERE sopra e sotto (48 px).
//
// ⚠️ ASPECT SACRIFICATO DI PROPOSITO, come su 1944 e Ghouls: 320 -> 240 e' un
// fattore 0,75 e tenendo le 224 righe intere l'immagine e' schiacciata in
// orizzontale. L'alternativa (224*0,75 = 168 righe) rispetterebbe l'aspect ma
// butterebbe 56 righe su 224, e in Out Run il punteggio, il cronometro e la
// scritta della tappa sono alti 8 px: diventerebbero illeggibili. In questo
// firmware NESSUN gioco ricampiona in verticale.
//
// ⭐ Riduzione 320 -> 240: si tengono **3 pixel ogni 4** — 80 gruppi TONDI, un
// caso piu' pulito del 5-su-8 di 1944. Gli indici tenuti sono round(k*4/3) =
// 0,1,3, cioe' lo scarto e' uno solo ogni quattro ed e' sempre lo stesso.
// Nessuna divisione resta nel ciclo caldo.
//
// ⭐ ORDINE DEI CICLI = ACCESSI ALLA PSRAM ([[feedback_ottimizzare_contando_accessi]]).
// Trasporre vuol dire che una RIGA del pannello e' una COLONNA di `fb`: presa
// riga per riga sarebbero 240x224 = 53.760 letture a passo 640 byte, cioe' una
// cache miss ciascuna. Qui il ciclo esterno e' la riga di `fb` (che si percorre
// in ORDINE) e dentro si riempiono TUTTE E OTTO le righe della striscia.
static void or_blit(void)
{
    // indici tenuti dentro ogni gruppo di 4 pixel sorgente (round(k*4/3))
    static const uint8_t KEEP[3] = { 0, 1, 3 };

    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        // ⭐ NIENTE memset: con la finestra STRETTA (224 colonne) il ciclo qui
        // sotto copre TUTTE le 224*8 posizioni della striscia. Le bande nere
        // stanno FUORI dalla finestra e le ha gia' dipinte or_bande_nere().

        // le 8 colonne di `fb` servite da questa striscia (una per riga scritta)
        int gx8[8];
        for (int i = 0; i < 8; i++) {
            const int x_p = (LD_FB_H - 1) - (sy + i);   // 0..239 = colonna UTENTE
            gx8[i] = (x_p / 3) * 4 + KEEP[x_p % 3];     // 0..319 = colonna di fb
        }

        const uint16_t *src = fb;                       // riga 0 del gioco
        uint16_t *dst = g_strip;   // finestra STRETTA: la striscia parte dal gioco
        for (int gy = 0; gy < OR_H; gy++) {
            dst[0]         = src[gx8[0]];
            dst[1 * OR_H]  = src[gx8[1]];
            dst[2 * OR_H]  = src[gx8[2]];
            dst[3 * OR_H]  = src[gx8[3]];
            dst[4 * OR_H]  = src[gx8[4]];
            dst[5 * OR_H]  = src[gx8[5]];
            dst[6 * OR_H]  = src[gx8[6]];
            dst[7 * OR_H]  = src[gx8[7]];
            src += OR_W;                                // riga successiva di fb
            dst += 1;                                   // colonna successiva
        }
        video.write(g_strip, OR_H * 8);   // finestra STRETTA: 224, non 320
#if OR_BLIT_OVERLAP
        // ⭐ Il DMA della striscia appena accodata sta uscendo: invece di
        // aspettarlo dentro la `write` successiva, si emula. Le fette sono
        // distribuite in proporzione alle strisce, cosi' a fine blit il quadro
        // dopo e' emulato: 262 x (s+1) / 30, che in fondo fa esattamente 262.
        // Se il blit finisce prima, le fette che restano le fa or_run_frame().
        or_emula_fette((int)((int64_t)LINES * (sy / 8 + 1) / (LD_FB_H / 8)));
#endif
    }
    // ⚠️ video.write() e' ASINCRONA e lascia una transazione SPI in volo: si
    // drena qui, UNA volta per frame.
    video.waitDMA();
}

// ---------------------------------------------------------------- schermo
// 2 = tutta la finestra del player (320x240): pagine di stato e bande nere.
// 1 = SOLO le 224 colonne che portano gioco, saltando le bande.
// 0 = si restituisce la finestra al menu.
static void or_window(int quale) {
    video.waitDMA();
    if (quale == 2)      video.setAddrWindow(TFT_HW_X, 0, TFT_FB_W, LD_FB_H);
    // ⭐⭐ SI SPEDISCE MENO: dei 320 pixel solo 224 portano gioco, 96 colonne
    // sono banda nera SEMPRE IDENTICA. Rispedirle ad ogni quadro sarebbe il 30%
    // del traffico SPI buttato (misura fatta su 1944: BLT da 17,1 a 12,3 ms).
    else if (quale == 1) video.setAddrWindow(TFT_HW_X + OR_X_OFF, 0, OR_H, LD_FB_H);
    else                 video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// Dipinge di nero TUTTA la finestra del player. Si chiama una volta sola, prima
// di stringere la finestra sul gioco: da li' in poi le bande restano com'erano.
static void or_bande_nere(void) {
    or_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    video.waitDMA();
}

// Testo SCALATO nella strip ruotata: stessa geometria di gh_line/c1944_line.
static void or_line(int sy, const char *s, int y_p, int scale, uint16_t col) {
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
static void or_page(const char *l[5], const uint16_t c[5])
{
    static const int Y[5] = { 150, 172, 194, 216, 250 };
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        or_line(sy, "OUT RUN", 96, 3, C_YELLOW);
        for (int k = 0; k < 5; k++)
            if (l[k] && l[k][0]) or_line(sy, l[k], Y[k], 1, c[k]);
        video.write(g_strip, TFT_FB_W * 8);
    }
    // ⚠️ subito dopo una pagina di stato si legge quasi sempre dalla SD:
    // video.write lascia una transazione SPI in volo e video.h chiede di
    // drenarla prima di toccare la SD.
    video.waitDMA();
}

static void or_msg(const char *a, const char *b, uint16_t col)
{
    const char *l[5] = { "SEGA 1986", a, b, "", "START 3s ESCI" };
    const uint16_t c[5] = { C_GREY, col, C_GREY, C_GREY, C_GREY };
    or_page(l, c);
}

// Schermata di CARICAMENTO: solo il nome del gioco e una riga. Niente
// avanzamento file per file — e' la convenzione del progetto
// ([[cabinatone_scritta_loading]], qui nella forma delle pagine dello SPINNERINO).
// ⚠️ Le schermate di ERRORE restano (or_msg in rosso): non sono debug, sono
// l'unico modo di sapere cosa manca sulla SD.
static void or_caricamento(void)
{
    const char *l[5] = { "SEGA 1986", "", "CARICAMENTO DA SD", "", "" };
    const uint16_t c[5] = { C_GREY, C_GREY, C_CYAN, C_GREY, C_GREY };
    or_page(l, c);
}

// ---------------------------------------------------------------- pack su SD
// ⭐ I SETTE file, con taglia E CRC32 di riferimento (quelli prodotti da
// romconv_outrun.py, gli stessi byte di CABINATONE — MD5 verificati 7/7).
// ⚠️ Qui il CRC si verifica DAVVERO, a differenza di 1944 e Ghouls dove si
// controlla la sola taglia: la' i pack sono 31 e 4 MB e rileggerli costerebbe
// decine di secondi, qui sono 2,53 MB che vengono letti comunque, e su Out Run
// una sprites.bin corrotta darebbe sprite mancanti SENZA alcun avviso (e' gia'
// costato una sessione di dubbi su CABINATONE).
typedef struct { const char *nome; uint32_t byte; uint32_t crc; } or_file_t;
static const or_file_t OR_FILES[] = {
    { "main.bin",     393216, 0x6D38E5C1 },   // 68000 principale
    { "sub.bin",      393216, 0x981EBAE3 },   // 68000 secondario
    { "tiles.bin",    196608, 0xEFC8FD91 },   // 3 bitplane, 8192 tile 8x8
    { "sprites.bin", 1048576, 0x14827157 },   // dword little-endian
    { "road.bin",      65536, 0x0A5F6C05 },   // due mappe strada 2bpp
    { "snd.bin",       32768, 0xA10ABAA9 },   // Z80 sonoro (epr-10187)
    { "pcm.bin",      524288, 0x56DFD68E },   // 8 banchi SegaPCM da 64 KB
};
static const int OR_NFILE = sizeof(OR_FILES) / sizeof(OR_FILES[0]);
static int  g_ok = 0;
static char g_primo_ko[24] = "";

static bool or_probe(void) {
    g_ok = 0; g_primo_ko[0] = 0;
    for (int i = 0; i < OR_NFILE; i++) {
        char p[64];
        snprintf(p, sizeof p, OR_DIR "/%s", OR_FILES[i].nome);
        FILE *f = fopen(p, "rb");
        if (!f) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "MANCA %s", OR_FILES[i].nome);
            continue;
        }
        fseek(f, 0, SEEK_END);
        long n = ftell(f);
        fclose(f);
        if ((uint32_t)n != OR_FILES[i].byte) {
            if (!g_primo_ko[0]) snprintf(g_primo_ko, sizeof g_primo_ko, "TAGLIA %s", OR_FILES[i].nome);
            continue;
        }
        g_ok++;
    }
    return g_ok == OR_NFILE;
}

// CRC32 (stessa tabella di zlib), calcolato al volo senza tabella in memoria.
// ⛔ RESPIRO OGNI 64 KB: su sprites.bin sono 1 MB = otto milioni di giri del
// ciclo interno, tutti CPU pura senza mai bloccarsi. Sommati agli altri file e
// alle allocazioni, e' il genere di tratto che tiene il task occupato per secondi
// e fa scattare il task watchdog -> riavvio in caricamento.
static uint32_t or_crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    size_t fatto = 0;
    while (fatto < n) {
        const size_t fine = (n - fatto > 65536) ? (fatto + 65536) : n;
        for (size_t i = fatto; i < fine; i++) {
            c ^= p[i];
            for (int k = 0; k < 8; k++)
                c = (c >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(c & 1)));
        }
        fatto = fine;
        vTaskDelay(1);
    }
    return ~c;
}

// carica un file intero dalla SD in un buffer gia' allocato e ne verifica il CRC
static bool or_load_file(const or_file_t *F, uint8_t *dst)
{
    char p[64];
    snprintf(p, sizeof p, OR_DIR "/%s", F->nome);
    FILE *f = fopen(p, "rb");
    if (!f) return false;
    // a blocchi: una fread da 1 MB su FATFS non e' una buona idea, e cosi' si
    // puo' anche cedere la CPU fra un pezzo e l'altro
    uint32_t fatti = 0;
    unsigned pezzi = 0;
    while (fatti < F->byte) {
        uint32_t q = F->byte - fatti; if (q > 32768) q = 32768;
        if (fread(dst + fatti, 1, q, f) != q) { fclose(f); return false; }
        fatti += q;
        // ⛔ RESPIRO IN CARICAMENTO: 2,65 MB di lettura piu' il CRC piu' le
        // allocazioni sono un tratto lungo in cui il task non cede la CPU. Se
        // l'idle non gira per 5 s il task watchdog riavvia la scheda MENTRE
        // CARICA ([[feedback_task_audio_indietro_watchdog]]).
        if ((++pezzi & 7) == 0) vTaskDelay(1);
    }
    fclose(f);
    if (or_crc32(dst, F->byte) != F->crc) return false;
    return true;
}

// ---------------------------------------------------------------- allocazioni
// ⭐⭐ IL CONTO DELLA RAM INTERNA, fatto PRIMA di consegnare come chiede la
// regola ([[feedback_audio_muto_e_ram_interna]]). Baseline del binario:
// 105.240 byte interni liberi (riga "Le variabili globali usano..." del
// compilatore). Out Run chiede in interna:
//     tl_tab del YM2151      26.624   (dentro ym2151.c, fallback PSRAM;
//                                      se ha gia' girato Ghouls e' gia' li')
//     struct YM2151 (malloc)  ~3.500
//     pal565                   8.192
//     sprbuf                   4.096   (letto TUTTO a ogni frame reso)
//     roadbuf                  4.096   (idem: e' la strada)
//     sndram                   2.048
//     pcmreg                     256   (ripercorso 16 canali x 32.000 volte/s)
//     ctx_main + ctx_sub      ~1.100   (contesti Musashi, scambiati 1048 volte
//                                       per frame: sono i piu' caldi in assoluto)
//     ym_l + ym_r + aud_out      768
//                            -------
//                            ~50.700  ->  ne restano ~54 oltre alla riserva
// ⛔ NON entrano in interna, e restano in PSRAM di proposito: `fb` (143 KB),
// `workram` (32 KB), `tileram` (64 KB), `palram`, `textram`, `sprram`,
// `roadram`. Su CABINATONE `fb` va in interna, ma la' il binario e' di UN gioco
// solo e ha ~305 KB liberi. `fb` allineato a 128 (la linea di cache: una riga e'
// 320*2 = 640 byte = 5 linee esatte, quindi con la base allineata lo e' ogni riga).
#define OR_RISERVA_INT (48 * 1024)

// ⭐ Buffer PICCOLO e CALDO: interna se resta spazio oltre la riserva, PSRAM
// altrimenti. Meglio restare in PSRAM che muti.
static void *or_calloc_caldo(size_t n)
{
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) >= n + OR_RISERVA_INT) {
        void *p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
        if (p) return p;
    }
    return heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
}

// ⭐⭐ ALLINEATO A 128 IN PSRAM: e' la linea di cache, e `fb` e' il buffer con
// piu' accessi per frame. Con la base disallineata OGNI riga ne sporca una in
// testa e una in coda.
static void *or_psram_allineata(size_t n)
{
    const size_t na = (n + 127) & ~(size_t)127;
    void *p = heap_caps_aligned_alloc(128, na, MALLOC_CAP_SPIRAM);
    if (p) memset(p, 0, na);
    return p;
}

static bool or_alloc(const char **perche)
{
    // ---- i CALDI, in interna se c'e' posto oltre la riserva
    // ⭐ ORDINE = quante volte al SECONDO si tocca il buffer, non quanto e'
    // piccolo ([[feedback_ottimizzare_contando_accessi]]). Il YM2151 e il SegaPCM
    // girano 32.000 volte al secondo e hanno gia' preso la loro parte in
    // or_audio_init(), chiamata PRIMA di qui apposta.
    if (!pal565) pal565 = (uint16_t *)or_calloc_caldo(PAL_SZ);          // 4096 entry x 2
    if (!sndram) sndram = (uint8_t  *)or_calloc_caldo(SNDRAM_SZ);       // stack del Z80 a 4 MHz
    if (!pcmreg) pcmreg = (uint8_t  *)or_calloc_caldo(PCMREG_SZ);       // 16 canali x 32.000/s
    // ⛔ `sprbuf` e `roadbuf` NON vanno piu' in RAM interna: erano 8 KB tolti al
    // sottosistema audio per niente. CONTATI gli accessi: si leggono una volta
    // per QUADRO DISEGNATO, cioe' 30 volte al secondo — sprbuf 16 byte per entry
    // su al massimo 256 entry, roadbuf ~1.300 word in tutto. Sono tre ordini di
    // grandezza sotto ai 32.000 giri al secondo del chip sonoro, che invece la
    // RAM interna la usa davvero. Restano nel ciclo PSRAM qui sotto.
    if (!fb)     fb     = (uint16_t *)or_psram_allineata((size_t)OR_W * OR_H * 2);

    struct { void **p; size_t n; const char *nome; } B[] = {
        { (void **)&rom_main, ROM_SZ,         "main.bin"  },
        { (void **)&rom_sub,  ROM_SZ,         "sub.bin"   },
        { (void **)&tilerom,  TILEROM_SZ,     "tiles.bin" },
        { (void **)&sprrom,   SPRROM_SZ,      "sprites"   },
        { (void **)&roadrom,  ROADROM_SZ,     "road.bin"  },
        { (void **)&tilepx,   TILEPX_SZ,      "TILE PX"   },
        { (void **)&tileram,  TILE_SZ,        "TILE RAM"  },
        { (void **)&textram,  TEXT_SZ,        "TEXT RAM"  },
        { (void **)&palram,   PAL_SZ,         "PAL RAM"   },
        { (void **)&sprram,   SPR_SZ,         "SPR RAM"   },
        { (void **)&roadram,  ROAD_SZ,        "ROAD RAM"  },
        { (void **)&workram,  WORK_SZ,        "WORK RAM"  },
        { (void **)&shared,   SHARED_SZ,      "SHARED"    },
        { (void **)&reg5,     REG5_SZ,        "REG5"      },
        { (void **)&snd_rom,  SND_ROM_SZ,     "snd.bin"   },
        { (void **)&pcm_rom,  PCM_ROM_SZ,     "pcm.bin"   },
        { &m68k_jump,         M68K_JUMP_SIZE, "TAB SALTI" },
        { &m68k_cyc,          M68K_CYC_SIZE,  "TAB CICLI" },
        // reti di sicurezza: se uno dei caldi non e' entrato nemmeno in PSRAM
        { (void **)&pal565,   PAL_SZ,         "PALETTE"   },
        { (void **)&sprbuf,   SPR_SZ,         "SPR BUF"   },
        { (void **)&roadbuf,  ROAD_SZ,        "ROAD BUF"  },
        { (void **)&sndram,   SNDRAM_SZ,      "SND RAM"   },
        { (void **)&pcmreg,   PCMREG_SZ,      "PCM REG"   },
        { (void **)&fb,       OR_W * OR_H * 2, "FB"       },
    };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++) {
        if (*B[i].p) continue;
        *B[i].p = heap_caps_calloc(1, B[i].n, MALLOC_CAP_SPIRAM);
        if (!*B[i].p) { *perche = B[i].nome; return false; }
    }

    // ⭐ i due CONTESTI di Musashi: sono i buffer piu' caldi del gioco (uno
    // scambio ogni fetta = 1048 salvataggi + 1048 ripristini per frame), quindi
    // interna con fallback PSRAM.
    // ⚠️ ALLINEATI A 16, non alla semplice `calloc`. Il contesto di Musashi e' la
    // struct `m68ki_cpu_core`, che con la softfloat del 68881 contiene campi a 64
    // bit: su RISC-V un accesso a 8 byte su un indirizzo allineato a 4 e' un
    // **accesso disallineato**, cioe' un'eccezione — e un'eccezione qui si
    // presenta come un `PANICO` che dipende da dove capita il blocco, quindi
    // intermittente. L'heap di ESP-IDF non promette piu' di 4 byte: lo si chiede.
    const unsigned int csz = m68k_context_size();
    if (!ctx_main) { ctx_main = heap_caps_aligned_alloc(16, (csz + 15) & ~15u, MALLOC_CAP_INTERNAL);
                     if (ctx_main) memset(ctx_main, 0, (csz + 15) & ~15u); }
    if (!ctx_sub)  { ctx_sub  = heap_caps_aligned_alloc(16, (csz + 15) & ~15u, MALLOC_CAP_INTERNAL);
                     if (ctx_sub)  memset(ctx_sub, 0, (csz + 15) & ~15u); }
    if (!ctx_main) ctx_main = or_calloc_caldo(csz);      // ripiego
    if (!ctx_sub)  ctx_sub  = or_calloc_caldo(csz);
    if (!ctx_main || !ctx_sub) { *perche = "CONTESTI 68000"; return false; }
    return true;
}

static void or_free(void)
{
    // ⛔ PRIMA di liberare le ROM: la finestra diretta dello spazio opcode punta
    // dentro uno di quei due buffer. Lasciarla accesa dopo la free significa che
    // il prossimo gioco a 68000 del binario preleva le istruzioni da memoria
    // liberata. Spegnerla e' una riga; non spegnerla e' un crash silenzioso.
    m68k_set_opcode_window(nullptr, 0);
    // l'idle-skip GENERICO di Musashi (per intervallo di indirizzi) qui non e'
    // mai stato acceso e non va acceso — due 68000 su RAM condivisa. Lo si
    // spegne comunque: e' lo stato che il prossimo gioco a 68000 si aspetta.
    // ⚠️ `OR_IDLE_SKIP` e' un'altra cosa e vive tutto dentro questo file: non
    // lascia nessuno stato nel core, quindi qui non c'e' niente da spegnere.
    m68k_set_idle_skip(1, 0);            // lo > hi = spento

    // ✅ ym2151_shutdown() ora restituisce ANCHE la `tl_tab` da 26 KB di RAM
    // INTERNA (`YM2151_LIBERA_TL_TAB` in cpus/ym2151/ym2151.c). Qui c'era
    // scritto che restava allocata «apposta», e per un rientro in Out Run era
    // vero — ma il gioco DOPO puo' essere un altro: col Neo Geo quei 26 KB
    // facevano cadere il framebuffer da 176 KB dalla RAM interna alla PSRAM, e
    // Metal Slug rallentava video e audio SENZA UN MESSAGGIO.
    // 👉 «Non e' una perdita, la ricicliamo al rientro» vale solo se a rientrare
    //    e' lo stesso gioco. In un binario con 30 emulatori non e' mai vero.
    if (ym) { ym2151_shutdown(ym); ym = nullptr; }

    void *B[] = { rom_main, rom_sub, tilerom, sprrom, roadrom, tilepx,
                  tileram, textram, palram, sprram, roadram, workram,
                  shared, reg5, snd_rom, pcm_rom, m68k_jump, m68k_cyc,
                  pal565, sprbuf, roadbuf, sndram, pcmreg, fb,
                  ctx_main, ctx_sub, ym_l, ym_r, aud_out };
    for (unsigned i = 0; i < sizeof(B) / sizeof(B[0]); i++)
        if (B[i]) heap_caps_free(B[i]);
    rom_main = rom_sub = tilerom = sprrom = roadrom = tilepx = nullptr;
    tileram = textram = palram = sprram = roadram = workram = nullptr;
    shared = reg5 = snd_rom = pcm_rom = nullptr;
    m68k_jump = m68k_cyc = nullptr;
    pal565 = nullptr; sprbuf = roadbuf = sndram = pcmreg = nullptr;
    fb = nullptr; ctx_main = ctx_sub = nullptr;
    ym_l = ym_r = nullptr; aud_out = nullptr;
    audio_ok = false;
}

// ---------------------------------------------------------------- uscita
static bool or_want_exit(uint8_t b) {
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
static void or_task(void *arg) {
    (void)arg;
    or_window(2);

    // ---- SD + pack ----
    if (!(sd_is_mounted() ? true : sd_mount())) {
        while (!g_stop) { or_msg("SD NON MONTATA", "", C_RED);
                          if (or_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }
    if (!or_probe()) {
        while (!g_stop) { or_msg(g_primo_ko[0] ? g_primo_ko : "DATI MANCANTI",
                                 "SERVE /OUTRUN SU SD", C_RED);
                          if (or_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    {
    // ⛔ QUI c'era `or_audio_init()`, spostato in avanti il 2026-08-10 per far
    // vincere alla `tl_tab` del YM2151 (26,6 KB) la RAM interna. **TOLTO il
    // 2026-08-10 stesso**: la misura sul device ha detto **156-183 KB interni
    // liberi**, quindi non c'era nessuna gara da vincere — quel riordino
    // risolveva un problema inesistente. E siccome l'inizializzazione del chip
    // e' l'UNICA cosa che gira diversamente al primo avvio dopo l'accensione
    // (la `tl_tab` si costruisce una volta sola e poi resta), era anche l'unico
    // sospettato per il riavvio che l'utente vede **sempre al primo lancio**.
    // Regola: se la misura smentisce il MOTIVO di una modifica, la modifica si
    // toglie — non si tiene "tanto non fa male".
    // ---- memoria ----
    const char *perche = "";
    or_caricamento();            // mostra la fase del giro PRECEDENTE
    if (!or_alloc(&perche)) {
        while (!g_stop) { char l[32]; snprintf(l, sizeof l, "PSRAM KO: %s", perche);
                          or_msg(l, "", C_RED);
                          if (or_want_exit(g_input->buttons_get())) break;
                          vTaskDelay(pdMS_TO_TICKS(60)); }
        goto uscita;
    }

    // ---- i sette file, con verifica del CRC ----
    {
        uint8_t *dest[7] = { rom_main, rom_sub, tilerom, sprrom, roadrom,
                             snd_rom, pcm_rom };
        for (int i = 0; i < OR_NFILE; i++) {
            if (or_load_file(&OR_FILES[i], dest[i])) continue;
            char l[32];
            snprintf(l, sizeof l, "KO %s", OR_FILES[i].nome);
            while (!g_stop) { or_msg(l, "LETTURA O CRC", C_RED);
                              if (or_want_exit(g_input->buttons_get())) break;
                              vTaskDelay(pdMS_TO_TICKS(60)); }
            goto uscita;
        }
    }
    // ⭐ PROVA INTEGRATA: i vettori di reset sono la firma dell'interlacciamento
    // dei due 68000. Se un giorno le ROM venissero rigenerate male si vede qui,
    // non con uno schermo nero a emulatore finito.
    {
        const uint32_t ssp = ((uint32_t)rom_main[0] << 24) | ((uint32_t)rom_main[1] << 16) |
                             ((uint32_t)rom_main[2] << 8) | rom_main[3];
        if (ssp != 0x00068000) {
            while (!g_stop) { or_msg("MAIN.BIN", "VETTORE SBAGLIATO", C_RED);
                              if (or_want_exit(g_input->buttons_get())) break;
                              vTaskDelay(pdMS_TO_TICKS(60)); }
            goto uscita;
        }
    }

    // ---- espansione delle tile (192 KB -> 512 KB a 1 byte/pixel) ----
    or_expand_tiles();

    // ---- stato iniziale ----
    memset(workram, 0, WORK_SZ); memset(tileram, 0, TILE_SZ);
    memset(textram, 0, TEXT_SZ); memset(palram, 0, PAL_SZ);
    memset(sprram, 0, SPR_SZ);   memset(shared, 0, SHARED_SZ);
    memset(roadram, 0, ROAD_SZ); memset(reg5, 0, REG5_SZ);
    memset(sprbuf, 0, SPR_SZ);   memset(roadbuf, 0, ROAD_SZ);
    road_control = 0;
    // sopra i 32 KB della ROM sonora non c'e' nulla (la region del driver e'
    // da 64 KB ma monta una sola EPROM da 32)
    memset(snd_rom + 0x8000, 0, SND_ROM_SZ - 0x8000);
    memset(sndram, 0, SNDRAM_SZ);
    // ⚠️ i registri del SegaPCM partono a 0xFF, NON a 0: il bit 0 di ctrl e'
    // "canale disabilitato", quindi con gli zeri partirebbero tutti e 16 accesi.
    memset(pcmreg, 0xFF, PCMREG_SZ);
    memset(pcm_addr, 0, sizeof(pcm_addr));
    memset(&sndcpu, 0, sizeof sndcpu);
    snd_fifo_r = snd_fifo_w = 0;
    snd_pbf = snd_nmi_pending = false;
    snd_halted = true; snd_reset_pending = false;
    snd_guard = SND_GUARD_SAMPLES;
    ym_addr = 0; gear_high = 0; gear_prev = false;
    adc_val[0] = 0x80; adc_val[1] = 0x00; adc_val[2] = 0x00;
    adc_val[3] = adc_val[4] = adc_val[5] = adc_val[6] = adc_val[7] = 0x10;
    in_port = 0xFF; adc_sel = 0;
    irq2_state = vblank_state = 0; irq_line[0] = irq_line[1] = 0;
    // ⚠️ Su questo firmware si RIENTRA nel menu senza riavvio: uscendo col long
    // press a meta' di un blit, `or_ovl_linea` resterebbe a meta' quadro e al
    // rilancio il primo quadro partirebbe da li'
    // ([[feedback_init_una_volta_sola_vs_rientro]]).
    or_ovl_linea = 0;
#if OR_IDLE_SKIP
    or_idle_addr = 0xFFFFFFFFu; or_idle_n = 0;   // stesso motivo: si RIENTRA
#endif
    pal_dirty = true;

    // ---- i due 68000 ----
    // ⚠️ m68k_set_tables() PRIMA di m68k_init(): e' init a costruire la tabella
    // dei salti, e senza puntatori validi scriverebbe a indirizzo 0.
    m68k_bus_set(&OR_BUS);          // le callback globali stanno in m68k_bus.cpp
    m68k_set_tables(m68k_jump, m68k_cyc);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);

    cur_cpu = 0; m68k_pulse_reset(); m68k_get_context(ctx_main);
    cur_cpu = 1; m68k_pulse_reset(); m68k_get_context(ctx_sub);
    sub_halted = true;                 // lo rilascia il main dal PPI
    sub_reset_pending = false;

    // ---- audio: chip + Z80 sonoro su un TASK PROPRIO, core 0 ----
    // Init QUI, a ROM caricate e memoria pronta: e' la posizione originale, ed
    // e' quella giusta. Se fallisce si gioca muti, non si blocca niente.
    or_audio_init();
    if (audio_ok) {
        g_aud_running = true;
        // prio 6: sta quasi sempre fermo dentro la write bloccante sull'I2S,
        // quindi dorme e non affama l'emulazione su CPU1.
        // ⛔ 16 KB, non 8: qui dentro girano un Z80 INTERPRETATO (Marat, uno
        // switch enorme), il YM2151 e il mix del SegaPCM. 8 KB erano il valore
        // ereditato dai player laserdisc, che pero' nel loro task audio fanno
        // solo mixaggio di PCM gia' pronti, senza emulare nessuna CPU.
        // ⚠️ L'esito VA CONTROLLATO: se la create fallisce e `g_aud_running`
        // resta true, il gioco parte muto senza nessun avviso e ogni uscita
        // spreca i 2 s pieni di attesa.
        if (xTaskCreatePinnedToCore(or_audio_task, "or_audio", 16384,
                                    nullptr, 6, nullptr, 0) != pdPASS)
            g_aud_running = false;
    } else {
        or_msg("AUDIO KO", "RAM INTERNA FINITA?", C_RED);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    // ⭐ Le bande nere si dipingono UNA VOLTA, poi la finestra si stringe sulle
    // sole 224 colonne del gioco: da qui in avanti lo SPI non trasporta piu' un
    // solo pixel di nero.
    or_bande_nere();
    or_window(1);

    // ---- il ciclo di gioco -------------------------------------------------
    {
    uint32_t fcount = 0;
    // ⭐⭐ SCADENZA CUMULATIVA, non "riparto da adesso ogni giro": col video a
    // frame alterni il quadro leggero finisce in anticipo e quello pesante resta
    // indietro. Ripartendo da capo l'anticipo verrebbe BUTTATO; accumulando, e'
    // la MEDIA a dover stare nel budget. (Misurato su 1944: dal 74% al ~96%.)
    int64_t next_us = esp_timer_get_time();
    while (!g_stop) {
        // ⭐⭐ FASI 20-25 = DENTRO IL CICLO, un passo alla volta.
        // Le fasi 1-9 hanno gia' fatto il loro lavoro: i referti `F8` e `F9`
        // hanno escluso TUTTO il caricamento (SD, 7 file, CRC, allocazioni,
        // espansione tile, avvio dei due 68000, init sonoro, finestra) e messo il
        // riavvio dentro il ciclo di gioco. Adesso lo stesso numero dice QUALE
        // dei cinque passi del quadro stava girando: da "il ciclo" a una decina
        // di righe. E' lo stesso strumento, con la lente piu' corta — non un
        // ciclo di prove ([[feedback_no_debug_static_only]]).
        // ⚠️ Scrivere una parola in RAM RTC costa quanto una qualsiasi
        // assegnazione: 5 scritture per quadro sono niente.
#if OR_MISURA
        const int64_t t0 = esp_timer_get_time();
#endif
        or_read_inputs();
        or_run_frame();
#if OR_MISURA
        // CPU = ingressi + i DUE 68000. Si chiude qui perche' il render e il
        // blit vengono dopo e vanno attribuiti a parte: se non si separano, un
        // referto "e' lento" non dice se il collo di bottiglia e' l'emulazione
        // o lo SPI, e le due cure sono opposte.
        const int64_t t1 = esp_timer_get_time();
#endif
        // ⚠️ la LOGICA del gioco gira a 30 Hz: si disegna un frame ogni due
        // vblank. Non e' un compromesso, e' quello che faceva la macchina vera
        // (i Sega Super Scaler aggiornano la logica ogni due quadri: nella ROM
        // `$7DF4: MOVE.W #$0001,$060800` fa contare 1->0->-1 = DUE vblank).
        if ((fcount & 1) == 0) {
            or_render();
#if OR_MISURA
            const int64_t t2 = esp_timer_get_time();
#endif
            or_blit();
#if OR_MISURA
            const int64_t t3 = esp_timer_get_time();
            or_pf_video((uint32_t)(t2 - t1), (uint32_t)(t3 - t2));
#endif
        }
        // barra del volume nella banda nera: si ridisegna solo se e' cambiata
        or_vol_bar();
        fcount++;

        if (or_want_exit(g_input->buttons_get())) break;
#if OR_MISURA
        // ⭐ Il LAVORO del giro si chiude QUI, **prima** dell'attesa di pacing:
        // dopo, il totale sarebbe sempre FRAME_US e non direbbe niente
        // ([[feedback_fps_a_passo_fisso_sono_ciechi]]).
        or_pf_giro((uint32_t)(t1 - t0),
                   (uint32_t)(esp_timer_get_time() - t0));
        // ...e la banda si scrive DOPO la misura, cosi' il termometro non si
        // misura da solo. Cambia la finestra SPI: va ridata al gioco.
        if (or_pf_blit()) or_window(1);
#endif

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
        // ⚠️⚠️ Un frame di Out Run costa molto piu' di uno di Ghouls (DUE 68000):
        // se l'emulazione sfora il tempo di frame, `left` e' negativo e senza uno
        // yield esplicito il task non cede MAI la CPU -> watchdog dei task, la
        // scheda si resetta e il gioco sembra "fermo all'inizio". Qui il respiro
        // c'e' SEMPRE, non ogni N quadri (difetto gia' pagato su CABINATONE).
        vTaskDelay(1);
    }
    }
    }

uscita:
    // ferma il task audio PRIMA di liberare la memoria che sta usando; puo'
    // essere bloccato dentro la write sull'I2S, gli si lascia il tempo di uscire
    g_stop = true;
    for (int i = 0; i < 200 && g_aud_running; i++) vTaskDelay(pdMS_TO_TICKS(10));
    or_free();
    // pulisci a nero l'intera finestra e torna alla finestra del menu
    or_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    or_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- entry
bool outrun_play(Input *input, unsigned short *out_strip) {
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;
    g_aud_running = false;   // stato pulito anche se la create audio fallì

    if (xTaskCreatePinnedToCore(or_task, "outrun", OR_STACK,
                                nullptr, 2, nullptr, 1) != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

// ============================================================================
// Il core Z80 di Out Run vive IN QUESTA UNITA' DI COMPILAZIONE (simboli Or*,
// vedi i #define in testa) — stessa soluzione di spaceace.cpp, cps1944.cpp e
// ghouls.cpp: cosi' le RdZ80/WrZ80 globali del core di Marat non collidono con
// quelle degli altri quattro Z80 del progetto. `register` neutralizzato:
// rimosso in C++17, il core lo usa come storage-class.
//
// ⛔⛔ TRAPPOLA NUOVA, PAGATA QUI (non c'era su Ghouls ne' su 1944, che usano i
// prefissi GH_/CPS_): l'`enum Codes` del core Z80 elenca gli opcode col loro
// nome mnemonico, e fra questi ci sono **OR_A, OR_B, OR_C, OR_D, OR_E, OR_H,
// OR_L, OR_xHL** (l'istruzione OR del Z80). `OR_H` e' anche la macro
// dell'altezza del gioco qui sopra (224): senza questo #undef il preprocessore
// scrive `...,OR_E,224,OR_L,...` dentro la definizione dell'enum, l'enum NON
// compila e di conseguenza risultano "non dichiarati" TUTTI i suoi nomi (DI,
// EI, EXX, PFX_CB...) in Codes.h — un muro di errori che non nomina mai la
// causa vera.
// ⚠️ Va bene fare #undef QUI perche' tutte le espansioni di OR_W/OR_H sono
// sopra: dopo questa riga il file non deve piu' usarle.
#undef OR_W
#undef OR_H
#define register
extern "C" {
#include "cpus/z80or/Z80or.c"
}
