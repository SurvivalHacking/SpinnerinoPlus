// ============================================================================
// neogeo.cpp — NEO GEO (SNK, 1990) su SPINNERINO PLUS. Gioco: METAL SLUG.
//
// ⭐⭐ PORTATO DA `CABINATONE\neogeo.cpp`, dove il gioco e' COMPLETO e
// CONFERMATO DALL'UTENTE SUL DEVICE (video, input, audio YM2610, "fps
// perfetti!!"). L'EMULAZIONE E' TRASCRITTA ALLA LETTERA: bus del 68000, sprite,
// fix layer, palette, calendario, Z80 sonoro e YM2610 sono lo stesso codice.
// Cambia SOLO l'involucro d'ambiente, e ogni differenza e' dichiarata:
//   1. VIDEO   — la' DSI 1024x600 + PPA, qui SPI 240x320: ng_blit() al posto di
//                ng_scale()/ng_flush(), e palette BYTE-SWAPPATA nella LUT.
//   2. AUDIO   — `audio_pce_*` -> `laserdisc_audio_*`, col volume applicato nel
//                mix (il ponte dello SPINNERINO non lo applica: vedi ng_mix()).
//   3. INPUT   — `input_bt` -> i GPIO del mobile, tre tasti (vedi ng_read_inputs).
//   4. TESTO   — font8x8 + lcd_draw_bitmap -> FONT8 + strip ruotata.
//   5. SD      — `sd_rom.h` -> stdio su FatFs, con il RESPIRO in caricamento.
//   6. Z80     — sesto core rinominato `cpus/z80ng/` (simboli `Ng*`).
// Vale [[feedback_porta_non_riprogettare]]: quello che gia' gira non si
// riprogetta. La verifica meccanica della trascrizione e'
// `_verify_gfx\neogeo\confronta_neogeo.py`.
//
// ── Testo originale di CABINATONE ──────────────────────────────────────────
// FASE 2: emulazione vera. Il motore (68000 + video + fix layer + sprite +
// calendario uPD4990A) NON e' stato riprogettato qui: e' PORTATO ALLA LETTERA
// dall'harness `_verify_gfx/neogeo_harness/mk_ngtest.py`, che gira sul PC con
// lo stesso Musashi del CABINATONE, legge gli stessi .bin della SD e rende
// Metal Slug in PNG. Ogni funzione del bus e del disegno e' quella, riga per
// riga; qui intorno c'e' solo l'involucro (SD -> PSRAM, PPA, task, pacing,
// input, uscita), copiato da blktiger.cpp / boblbobl.cpp.
// ⚠️ Se qualcosa non si vede: si riproduce PRIMA nell'harness sul PC, non a
// tentativi sul device.
//
// ── COSA C'E' SULLA SD (romconv_neogeo.py) ─────────────────────────────────
//   /neogeo/mslug/  p.bin 2M · m.bin 128K · s.bin 128K · v.bin 8M
//                   c.ctile 16M (sprite gia' convertiti) · c.cusage 32K
//   /neogeo/bios/   sp.bin · sfix.bin · lo.bin · sm1.bin  (comuni a tutti)
//
// ── TRE COSE DA NON SBAGLIARE (verificate sul sorgente, gia' costate tempo) ─
// 1. Il bus del NeoGeo si legge in **LITTLE-ENDIAN NATIVO** (word = byte[0] |
//    byte[1]<<8, byte con indirizzo XOR 1). Leggendolo big-endian, come
//    verrebbe naturale per un 68000, lo stack iniziale esce 0x100000F3 invece
//    di 0x0010F300 e la CPU non scrive nulla.
// 2. `m68k_set_tables()` va chiamata PRIMA di `m68k_init()` (come outrun.cpp):
//    nel Musashi del progetto le tabelle da 576 KB le alloca il chiamante, e
//    init e' quello che costruisce la tabella dei salti.
// 3. Il calendario uPD4990A deve rispondere davvero (pd4990a.c in root): se il
//    Time Pulse non alterna o i bit dei dati sono tutti a 1 il BIOS si ferma
//    su "CALENDAR ERROR" e non arriva mai al gioco.
// ============================================================================
// ⭐⭐ L'IDE compila a **-Os**. Questo file ha SIA i cicli per pixel (il blit
// degli sprite e il fix layer) SIA i gestori del bus del 68000 e del Z80
// (ng_r8/ng_r16/ng_w8/ng_w16, ~35.000 chiamate per frame): sono esattamente i
// due casi per cui `outrun.cpp`, `cps1944.cpp`, `m68k_bus.cpp` e `m68k_core.c`
// hanno gia' il pragma. Qui mancava. Vedi [[m68k_ottimizzazioni_cabinatone]]:
// su 1944 la stessa mancanza faceva diventare una `memcpy` di 8 byte una
// CHIAMATA A LIBC ~50.000 volte per frame. Non cambia una riga di logica.
// ⭐ MISURATO, non supposto: il `.text` di questo file passa da 13.224 byte
// (-Os) a 15.280 (-O2), cioe' +15,5% di codice generato — il pragma sta
// davvero mordendo. Sul binario intero sono 1.792 byte in piu' su 13 MB.
#pragma GCC optimize("O2")

#include <Arduino.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_cache.h"
#if __has_include("driver/ppa.h")
  #include "driver/ppa.h"
  #define NG_HAS_PPA 1
#else
  #define NG_HAS_PPA 0
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <stdio.h>
#include "config.h"
#include "emulation/neogeo.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/sd_card.h"
#include "emulation/video.h"
#include "gfx_cache.h"     // cache a banchi da SD: serve ai set > 16 MB di sprite

// ---- Z80 sonoro, RINOMINATO per non collidere ------------------------------
// ⛔ In questo progetto esistono GIA' CINQUE core Z80: `cpus/z80/` (i giochi
// della griglia), `cpus/z80ace/` (`Ace*`), `cpus/z80cps/` (1944, `Cps*`),
// `cpus/z80gh/` (Ghouls, `Gh*`) e `cpus/z80or/` (Out Run, `Or*`). Il core di
// Marat dichiara RdZ80/WrZ80/... GLOBALI e senza contesto: due moduli che le
// definiscono entrambe = simbolo duplicato in link, anche se a runtime ne gira
// uno solo. Quindi SESTA copia, `cpus/z80ng/`, coi simboli `Ng*`.
// ⚠️ Verificato che `Z80ng.c` e' IDENTICO a `Z80gh.c` a meno del percorso degli
// include, e che nei suoi header NON esiste nessun nome `NG_*` o `Ng*` con cui
// le macro di questo file possano collidere — e' il controllo che su Out Run
// non era stato fatto e costo' un muro di errori che non nominavano la causa
// ([[feedback_prefisso_macro_collide_con_enum]]).
#define ExecZ80    NgExecZ80
#define RunZ80     NgRunZ80
#define ResetZ80   NgResetZ80
#define IntZ80     NgIntZ80
#define StepZ80    NgStepZ80
#define JumpZ80    NgJumpZ80
#define RdZ80      NgRdZ80
#define WrZ80      NgWrZ80
#define InZ80      NgInZ80
#define OutZ80     NgOutZ80
#define LoopZ80    NgLoopZ80
#define PatchZ80   NgPatchZ80
#define DebugZ80   NgDebugZ80
#define OpZ80_INL  NgOpZ80_INL
extern "C" {
#include "cpus/z80ng/Z80.h"
}

// Musashi e il calendario sono C puro: senza extern "C" i simboli verrebbero
// cercati decorati. pd4990a.h ha gia' le sue guardie, annidarle non fa danno.
extern "C" {
#include "cpus/m68k/m68k.h"
#include "cpus/m68k/m68kops.h"      // m68k_set_tables()
#include "pd4990a.h"
}
#include "m68k_bus.h"                // smistamento dei callback GLOBALI di Musashi

// Vivono nel .ino / in input.cpp / in audio.cpp, come per tutti gli altri
// player autonomi (stessa lista di dichiarazioni di outrun.cpp).
extern Video video;
extern bool  input_consume_back_to_menu();
extern bool  input_is_vol_mode_active(void);
extern int   laserdisc_current_volume(void);
extern void  laserdisc_audio_begin(void);
extern void  laserdisc_audio_end(void);
extern void  laserdisc_audio_write(const int16_t *stereo, int frames);

// bit dei comandi: qui sono i BUTTON_* dello SPINNERINO (emulation/input.h),
// non quelli del bridge C6 di CABINATONE. I valori coincidono, i nomi no.
#define B_LEFT  BUTTON_LEFT
#define B_RIGHT BUTTON_RIGHT
#define B_UP    BUTTON_UP
#define B_DOWN  BUTTON_DOWN
#define B_FIRE  BUTTON_FIRE
#define B_START BUTTON_START
#define B_COIN  BUTTON_COIN
#define B_EXTRA BUTTON_EXTRA

// Contesto del player, come in outrun.cpp/cps1944.cpp.
static uint16_t *g_strip = nullptr;      // striscia di rendering, prestata dal menu
static Input    *g_input = nullptr;
static volatile bool g_done = false;

static inline uint8_t ng_buttons(void) { return g_input->buttons_get(); }

// ---------------------------------------------------------------- schermo
// ⭐ geometria VERA di gngeo (esp32_platform.c:770-807): si disegna dentro un
// quadro 352x256 e lo schermo del NeoGeo e' il rettangolo che parte da (16,16).
// Il margine di 16 px serve agli sprite che entrano da sinistra: con una stride
// di 320 le colonne di destra sfondavano sulla riga successiva (era quello a far
// sembrare il testo "in colonne").
#define NG_W    352                  // stride del framebuffer interno
#define NG_H    256
#define NG_VX   16                   // origine dello schermo dentro il quadro
#define NG_VY   16
#define NG_VW   320                  // ⭐ 320, non i 304 di gngeo: vedi sotto
#define NG_VH   224

// ⭐ ADATTAMENTO 1 di 6 — VIDEO. Su CABINATONE qui c'era la scala del PPA verso
// un pannello DSI 1024x600 (320->780, 224->588). Qui il pannello e' uno SPI
// 240x320 in portrait, quindi si usa **la geometria di Out Run tale e quale**,
// che e' esattamente lo stesso formato di partenza (320x224):
//   - gioco DIRITTO, 320 colonne -> 240 tenendone **3 ogni 4** (80 gruppi tondi,
//     indici round(k*4/3) = 0,1,3);
//   - 224 righe centrate nell'asse da 320 = **48 px di banda nera** sopra e
//     sotto, dipinte UNA volta sola;
//   - finestra SPI **STRETTA** sulle sole 224 colonne che portano gioco: le
//     bande sono sempre identiche e rispedirle sarebbe il 30% di traffico SPI
//     buttato (misurato su 1944: BLT da 17,1 a 12,3 ms).
#define LD_FB_H   240                            // altezza della finestra del player
#define NG_X_OFF  ((TFT_FB_W - NG_VH) / 2)       // = 48, la banda nera

// ---------------------------------------------------------------- timing
// 264 linee x 758 cicli = 200.112 cicli per frame (il 68000 vero a 12 MHz ne
// farebbe 202.752: 758 e' il valore usato — e verificato — dall'harness).
// ⚠️ Le fette devono restare 264: il contatore raster (0x3C0006) deve avanzare
// DENTRO il frame, altrimenti il busy-wait del BIOS non finisce mai.
#define NG_LINES     264
#define NG_CYC_LINE  758
#define NG_CYC_VBL   1000            // fetta dopo aver alzato l'IRQ1 di vblank
#define NG_SPEED_PCT 100             // >100 = piu' veloce del vero
#define FRAME_US     (16897 * 100 / NG_SPEED_PCT)   // 1e6 / 59,1856

// Video a frame alterni: l'emulazione resta a 59,19 Hz, si aggiorna lo schermo
// meno spesso.
// ⭐⭐ ACCESO (2026-08-11) dopo il referto «gli fps sono molto lenti».
// Su CABINATONE NON serviva, e la differenza fra le due schede spiega perche':
//   - la' `fb` (176 KB) sta in RAM INTERNA; qui non ci puo' stare (l'interna
//     libera e' ~160 KB) e ogni quadro sono ~240.000 SCRITTURE in PSRAM
//     (90.112 del backdrop + ~150.000 pixel di sprite);
//   - la' il ridimensionamento e il trasferimento li fa il PPA in DMA verso un
//     pannello DSI, e alla CPU non costano nulla; qui il blit e' SPI e sono
//     ~107 KB per quadro, cioe' ~10,7 ms a 80 MHz — da soli piu' del 60% del
//     tempo di un quadro a 59,19 Hz.
// Dimezzare il VIDEO lascia l'emulazione a 59,19 Hz (il gioco resta alla
// velocita' giusta: e' la lezione di [[feedback_multi_step_emulation]], dove
// legare l'emulazione al rendering faceva sembrare il gioco lento) e toglie
// meta' del lavoro piu' caro. E' la stessa scelta che ha risolto Bubble Bobble.
#define NG_VIDEO_HALF_RATE 0

// ⭐⭐⭐ IL BLIT SPI E' QUASI TUTTO ATTESA: dentro ci sta l'emulazione.
// A 1 le 264 fette del 68000 del quadro SUCCESSIVO vengono eseguite dentro le
// attese del DMA del quadro corrente (dettaglio sopra ng_emula_fette()).
// Il conto, coi numeri letti dall'overlay: prima CPU 8,6 + REN 6,0 + BLT 12,4 =
// **27 ms** in fila; ora REN 6,0 + max(CPU 8,6 · BLT 12,4) = **18,4 ms**, ed e'
// per questo che NG_VIDEO_HALF_RATE torna a **0** come su CABINATONE, che con
// lo stesso identico loop fa 60 fps perche' il suo PPA lavora in DMA.
// ⛔ Se rompe: rimettere questo a 0 **e** NG_VIDEO_HALF_RATE a 1, e si torna
// esattamente al firmware di ieri ([[feedback_una_modifica_per_flash]]).
// ⛔ 2026-10-08: SPENTO perche' sostituito da NG_BLIT_CORE0 (qui sotto). Per
// tornare al firmware di prima: NG_BLIT_CORE0 0 e questo a 1.
#define NG_BLIT_OVERLAP 0

// ⭐⭐⭐ IL BLIT SUL CORE 0 (2026-10-08, PULSTAR «vanno ottimizzati gli fps»).
// Referto dell'overlay su Pulstar: FPS 48 · CAR 114 · CPU 0,0 · REN 5,7 ·
// BLT 13,0 · EMS 49 · AUD 59 — il conto chiude (18,7 su 19,3 ms).
// Con l'overlap il DMA puro e' 10,75 ms ma BLT ne prende 13: dentro il blit
// la CPU NON sta dietro al DMA, perche' sullo STESSO core fa il 68000 (~8,6)
// e la conversione 320->240 di ogni striscia. Il collo e' UN CORE SOLO che fa
// tutto: REN + 68000 + conversione = ~18,7 ms su 16,9.
// ⭐ Sul CABINATONE (60 fps, stesso loop) la conversione e l'invio li fa il PPA
// in DMA e alla CPU non costano niente. Qui il «PPA» diventa un task sul
// CORE 0 (dove sta gia' l'audio, priorita' 5; questo e' a 4, quindi l'audio
// passa sempre davanti): converte le strisce e le spedisce, e quasi tutto il
// suo tempo e' attesa del DMA, cioe' core libero per l'audio.
// Il core 1 fa SOLO emulazione + disegno, nell'ordine del CABINATONE (il ramo
// senza overlap: righe 0..223, fotografia, vblank): 8,6 + 5,7 = ~14,3 ms.
// ⭐ DUE framebuffer che si alternano: mentre il core 0 spedisce il quadro N,
// il core 1 disegna il quadro N+1 nell'altro. Se il secondo non entra in
// PSRAM si va con UNO solo e il core 1 aspetta che il blit finisca prima di
// ridisegnarci sopra (piu' lento ma corretto).
// ⚠️ Raggio d'azione: il player e' COMUNE a tutti i NEO GEO (Metal Slug 1/2,
// View Point, Pulstar), che hanno tutti lo stesso limite dei 47-48 fps.
// ⛔ Se rompe: questo a 0 e NG_BLIT_OVERLAP a 1 = firmware di prima.
#define NG_BLIT_CORE0 1

// ⭐⭐ SCALA E ROTAZIONE AL PPA (l'acceleratore del P4), invece che a mano.
// Su questo firmware il PPA **non lo usa nessun gioco**: e' hardware fermo. Il
// blit software costa ~3,3 ms di CPU per quadro (dei 14,1 di BLT; il resto e'
// il DMA dell'SPI, che non si comprime). Dandoli al PPA restano solo il DMA e
// una memcpy. Geometria verificata offline in `PY\mk_ngppa.py` PRIMA di
// scriverla: ANGLE_90 6,6% di scarto contro ANGLE_270 46,5%.
// ⛔ Se rompe (colori strani, immagine coricata, riavvio): questo a 0 e si
// torna al blit software, che resta nel file e funziona.
// ⚠️ Legato a NG_HAS_PPA: `ppa_client_handle_t` e le sue funzioni esistono solo
// se il core Arduino ha `driver/ppa.h`. Senza questa guardia il file non
// compilerebbe piu' su un core che non ce l'ha — ed e' la stessa ragione per
// cui gfx_cache.cpp protegge i suoi include di FatFs.
// ⛔⛔⛔ REVOCATA SUL DEVICE (2026-08-29). Referto: **«42 FPS e colori sbagliati
// in tutti e 2 i METAL SLUG»**. Cioe' ha sbagliato in TUTTI E DUE i modi:
// 1. **I COLORI**: era il rischio dichiarato prima del flash. `fb` contiene
//    RGB565 **byte-swappato** (lo produce `pal_lut` per il display SPI); il PPA
//    non si limita a copiare i pixel, quindi rimescola due byte che insieme non
//    sono un colore. Avevo scritto «se un giorno il PPA interpolasse i colori
//    uscirebbero sbagliati»: interpola.
// 2. **LA VELOCITA': 48 -> 42 fps, cioe' PEGGIO.** E qui la lezione e' mia: ho
//    **aggiunto** lavoro credendo di toglierlo. Il blit software leggeva `fb` e
//    scriveva dritto in `g_strip` (RAM interna); il percorso PPA fa un giro in
//    piu' tutto in PSRAM (fb -> ng_ppa_out -> dma_buffer) e ci mette sopra DUE
//    `esp_cache_msync` da 283 KB per quadro, piu' un PPA **BLOCKING** durante il
//    quale la CPU non emula. I 3,3 ms di conversione risparmiati costano piu' di
//    quanto rendono.
// 👉 E' [[feedback_ottimizzare_contando_accessi]] caso 3 (Raiden) e caso 5 (PC
//    Engine) per la terza volta: **contare dove si spende non dice quanto si
//    guadagna a toglierlo**, perche' in mezzo c'e' sempre un modello implicito
//    della memoria — qui «il PPA e' gratis» — e il modello puo' essere sbagliato
//    anche quando il conto e' giusto.
// ⭐ Il codice resta qui, spento e documentato: se un domani il framebuffer non
//    fosse piu' byte-swappato E il PPA lavorasse in NON_BLOCKING sovrapposto
//    all'emulazione, il conto potrebbe cambiare. Non prima.
#define NG_BLIT_PPA 0

// ⭐ MISURATORE PERMANENTE, come `OR_MISURA` in outrun.cpp. A 1 scrive tre righe
// in alto a sinistra dentro il quadro — FPS/CAR/PIC, CPU/REN/BLT, SD/MIS/AUD —
// cosi' si decompone il costo per DIFFERENZA invece di indovinare dove va il
// tempo. Il dettaglio di ogni voce sta sopra `ng_draw_misura()`.
// ⛔ Non e' un flash diagnostico: e' uno strumento messo UNA volta e lasciato
// nel codice, che si accende quando serve
// ([[feedback_fps_a_passo_fisso_sono_ciechi]] lo PRESCRIVE, e il corollario del
// 2026-08-20 dice che davanti a «e' lento» **il primo flash e' lo STRUMENTO,
// non un tentativo di cura**: cinque flash di ipotesi non avevano trovato
// niente, tre righe a schermo hanno chiuso in tre secondi).
// ⭐⭐ Acceso il 2026-08-29 per la caccia agli fps, e **RISPENTO lo stesso giorno
// a taratura chiusa** (referto finale: 47 fps, «mi accontento»): e' la
// convenzione del progetto, niente misuratori accesi in consegna.
// ⚠️ Si riaccende con questa riga, INSIEME a `GFX_CACHE_MISURA` in gfx_cache.h:
// da solo, la voce SD legge sempre 0.
// ⭐ Cosa ha fatto trovare, perche' la prossima volta valga la pena riaccenderlo:
// `CPU 8,6 · REN 6,0 · BLT 12,4` ha detto che il collo di bottiglia era il blit
// SPI e non il renderer, e `CPU 8,6 -> 0,2` dopo la cura ha PROVATO che
// l'overlap funzionava. Senza quei numeri la cura giusta non era distinguibile
// dalle due sbagliate (idle-skip e PPA), che infatti sono state bocciate.
// ⭐ RIACCESO il 2026-10-08 per PULSTAR («vanno ottimizzati gli fps»). Il banco
// offline dice che Pulstar costa MENO di Metal Slug 2 (sprite 127k px/quadro
// contro 149k, picco 214k contro 329k, miss da SD ~0,2/quadro in entrambi):
// la causa non si vede dal PC, quindi il primo flash e' lo strumento. Il
// referto va preso su Pulstar E su Metal Slug 2 nello stesso firmware: se
// calano tutti e due, il colpevole e' il player comune (il quadro spezzato al
// vblank di View Point, mai riferito su Metal Slug), non il gioco.
// ⚠️ Testo portato a SCALA 2 (NG_MIS_SC) e righe accorciate a <= 19
// caratteri: a scala 1 con la riduzione 320->240 erano ~6 px sul pannello
// ([[feedback_overlay_caratteri_grandi]]).
// ⭐ RISPENTO lo stesso giorno su richiesta dell'utente («togli overlay»),
// insieme a NG_BLIT_CORE0. Riaccendere con GFX_CACHE_MISURA se serve.
#define NG_MISURA 0

// La .cusage dice quali tile sono completamente trasparenti: saltarli e' esatto
// (pen 0 non scrive nulla) e toglie lavoro inutile. A 0 si ignora il file.
#define NG_USE_CUSAGE 1

// AUDIO: Z80 sonoro + YM2610 (FM 4ch + SSG 3ch + ADPCM-A 6ch), sul core 0.
// A 0 il gioco resta muto e non si caricano m.bin/v.bin (8,1 MB di PSRAM in
// meno e qualche secondo di SD in meno all'avvio).
#define NG_AUDIO 1

// Le ROM del sonoro (m.bin 128K, v.bin 8M) servono solo con l'audio acceso.
// ⛔ lo.bin (LUT di zoom) e sm1.bin (BIOS sonoro) NON si caricano: la prima la
// usa solo il rendering per scanline di MAME, il secondo serve ai giochi SENZA
// M1 — Metal Slug ce l'ha. Sono 256 KB di PSRAM risparmiati.
#define NG_LOAD_AUDIO_ROMS NG_AUDIO

// ---------------------------------------------------------------- memoria
#define BIOS_SZ     0x020000
// ⛔ la fix del BIOS e' 128 KB per definizione (sfix.sfix), quella del GIOCO no:
// Metal Slug 3 ne ha 512 (romsize[6] del .drv). Erano la stessa costante.
#define FIXBIOS_SZ  0x020000
#define FIXROM_SZ   (G->fix_sz)
#define MROM_SZ     (G->mrom_sz)

// ---- I GIOCHI --------------------------------------------------------------
// Le misure NON sono uguali per tutti e vengono dal .drv (le stampa
// romconv_neogeo.py). Erano costanti scritte nel codice, cioe' i valori di
// Metal Slug: con Metal Slug 2 il clamp del bank switch avrebbe tagliato il
// terzo MB di programma e la guardia sui tile avrebbe respinto META' degli
// sprite. ⚠️ Aggiungendo un gioco si aggiunge una riga QUI e una in
// neogeo_menu.cpp, nient'altro.
struct NgGame {
    const char *dir;                       // sottocartella su /neogeo/ della SD
    // ⛔ Titolo e anno delle pagine del player (caricamento ed errori). Erano
    // SCRITTI DENTRO ng_page ("METAL SLUG" / "SNK 1996"), cioe' i valori del
    // primo gioco: Metal Slug 2 caricava con scritto "METAL SLUG" e l'anno
    // sbagliato. E' la stessa famiglia di sviste delle misure qui sotto — una
    // costante che sembrava "uguale per tutti" perche' i giochi erano uno solo.
    const char *titolo;                    // riga grande gialla
    const char *anno;                      // riga piccola sotto
    uint32_t    prom_sz, ctile_sz;         // p.bin e c.ctile
    // ⛔ Anche la V-ROM cambia da gioco a gioco (romsize[3] del .drv): Metal Slug
    // 1 e 2 hanno 8 MB, Art of Fighting 2 ne ha **5**. Era una costante 0x800000
    // in DUE posti (il caricamento e YM2610Init) e con aof2 il primo dava
    // "MANCA/ERRATO .../v.bin" — cioe' la schermata d'errore che consiglia di
    // lanciare il .py, quando il file sulla SD era perfetto. Il secondo era
    // peggio: avrebbe fatto leggere all'ADPCM 3 MB OLTRE il buffer.
    uint32_t    vrom_sz;                   // v.bin (campioni ADPCM-A)
    // ⛔ terza volta che una misura "uguale per tutti" non lo era: Metal Slug 3
    // ha fix e M1 da 512 KB invece di 128. Ora stanno qui come le altre.
    uint32_t    fix_sz;                    // s.bin  (romsize[6] del .drv)
    uint32_t    mrom_sz;                   // m.bin  (romsize[1] del .drv)
    uint32_t    crc_p, crc_s, crc_c, crc_u, crc_m, crc_v;
    bool        spr_cache;                 // .ctile troppo grande per la PSRAM
    bool        pcm_cache;                 // anche v.bin resta sulla SD
    // ---- set con chip SMA (kof99, garou, mslug3, kof2000) ------------------
    // La P-ROM e' gia' decifrata dal converter; a runtime resta il BANK SWITCH
    // speciale: la scrittura word all'indirizzo il cui byte basso vale
    // `bksw_scr[0]` porta un numero di banco coi bit MESCOLATI, e la tabella
    // `bksw_off` dice a che offset corrisponde (memory.c:909-920).
    const uint8_t  *bksw_scr;              // 7 byte: indirizzo + 6 posizioni di bit
    const uint32_t *bksw_off;              // 64 offset relativi a 0x100000
    // fix layer a banchi: 0 = no, 1 = stile Garou/Metal Slug 3 (video.c:382)
    uint8_t     fix_bank_type;
    // ---- famiglia NEO-PVC (mslug5, svc, kof2003) ---------------------------
    // ⛔ E' l'unica protezione NeoGeo che gngeo NON emula: in gngeo/roms.c la
    // riga `install_pvc_protection(r)` dentro init_mslug5() e' **commentata**,
    // cioe' li' il gioco non parte. Non c'e' quindi un porting da copiare — le
    // funzioni piu' sotto sono trascritte da MAME
    // src/devices/bus/neogeo/prot_pvc.cpp, riga per riga.
    bool        pvc;
    // ---- Metal Slug X: chip ALTERA sulla NEO-MVS PROGEOP ------------------
    // Finestra 0x2FFFE0-0x2FFFEF: le scritture compongono un comando, le
    // letture rendono UN BIT alla volta preso dalla P-ROM. Anche questa non c'e'
    // in gngeo (la voce {"mslugx",init_mslugx} della tabella e' commentata, e
    // quella init RATTOPPA la P-ROM con dei NOP invece di emulare il chip):
    // la fonte e' MAME src/devices/bus/neogeo/prot_mslugx.cpp.
    bool        prot_mslugx;
};

// ---- tabelle SMA di Metal Slug 3 (gngeo/roms.c:121-137) --------------------
// bksw_scr[0] = 0xE4 e' il BYTE BASSO dell'indirizzo su cui il gioco scrive il
// banco (quindi 0x2FFFE4); gli altri sei dicono da quale bit del dato viene
// ciascun bit del numero di banco. La tabella degli offset ha 49 voci vere: le
// restanti restano a zero come in gngeo (array [64] inizializzato a meta').
static const uint8_t  SMA_SCR_MSLUG3[7] = { 0xE4, 14, 12, 15, 6, 3, 9 };
static const uint32_t SMA_OFF_MSLUG3[64] = {
    0x000000, 0x020000, 0x040000, 0x060000,   // 00
    0x070000, 0x090000, 0x0b0000, 0x0d0000,   // 04
    0x0e0000, 0x0f0000, 0x120000, 0x130000,   // 08
    0x140000, 0x150000, 0x180000, 0x190000,   // 12
    0x1a0000, 0x1b0000, 0x1e0000, 0x1f0000,   // 16
    0x200000, 0x210000, 0x240000, 0x250000,   // 20
    0x260000, 0x270000, 0x2a0000, 0x2b0000,   // 24
    0x2c0000, 0x2d0000, 0x300000, 0x310000,   // 28
    0x320000, 0x330000, 0x360000, 0x370000,   // 32
    0x380000, 0x390000, 0x3c0000, 0x3d0000,   // 36
    0x400000, 0x410000, 0x440000, 0x450000,   // 40
    0x460000, 0x470000, 0x4a0000, 0x4b0000,   // 44
    0x4c0000,                                 // 48 (le altre non sono usate)
};

static const NgGame NG_GAMES[] = {
    // Metal Slug: 26,25 MB in tutto, ci sta comodo in PSRAM.
    { "mslug", "METAL SLUG", "SNK 1996",
      0x200000, 0x1000000, 0x800000, 0x20000, 0x20000,
      0x4952089B, 0x2F55958D, 0x470385AC, 0x597EAA92, 0xC28B3253, 0x14CF8459,
      false, false, nullptr, nullptr, 0 },
    // ⛔ Metal Slug 2: 43,25 MB, NON entra nei 32 MB di PSRAM. I 32 MB di
    // sprite restano sulla SD e si leggono a banchi (gfx_cache), come fa
    // gngeo sull'ESP32 e come gia' fa 1944 in questo stesso firmware.
    { "mslug2", "METAL SLUG 2", "SNK 1998",
      0x300000, 0x2000000, 0x800000, 0x20000, 0x20000,
      0x5E8C6AA0, 0xF3D32F0F, 0xDC5BDC85, 0xBFA110B6, 0x94520EBD, 0xD19E4594,
      true,  false, nullptr, nullptr, 0 },
    // Art of Fighting 2 (SNK 1994, NGH-056, set `aof2`): 23,28 MB — MENO di
    // Metal Slug 1, quindi tutto in PSRAM come lui, niente cache da SD.
    // ⭐ p.bin e' 2 MB perche' la regione del .drv e' 0x200000, ma il programma
    // vero e' 1 MB solo: il secondo MB e' zeri e il gioco non lo legge mai (non
    // fa bank switch). E' esattamente cio' che fa gngeo, non una scelta nostra.
    // ADPCM-A 5 MB, ADPCM-B assente (romsize[4]=0) come nei Metal Slug.
    // ROM NON cifrate: verificato sui dati (vettori 10 00 00 F3 C0 00 02 04 =
    // SSP 0x0010F300 / PC 0x00C00402, s1 col 27,3% di byte a zero, m1 che parte
    // col solito stub SNK `F3 C3 B0 00`). Nessun CMC42/CMC50 da applicare.
    { "aof2", "ART OF FIGHTING 2", "SNK 1994",
      0x200000, 0x1000000, 0x500000, 0x20000, 0x20000,
      0xDD04D1FC, 0x8B02638E, 0x77BF75B4, 0x5F587FB2, 0xF27E9D52, 0xD28AD285,
      false, false, nullptr, nullptr, 0 },
    // Sonic Wings 2 / Aero Fighters 2 (Video System 1994, NGH-075, set
    // `sonicwi2`): **13,27 MB**, il piu' leggero dei quattro — meta' degli
    // sprite di Art of Fighting 2 (8 MB = 65.536 tile) e 3 MB di ADPCM-A.
    // Tutto in PSRAM, niente cache da SD.
    // ⚠️ P-ROM da 2 MB con le DUE META' SCAMBIATE, come Metal Slug 1 (il .drv
    // dice src 0x000000 -> dest 0x100000 e viceversa): per questo il CRC di
    // p.bin non e' quello di 075-p1.
    // ROM NON cifrate: vettori `10 00 00 F3 C0 00 02 04` = SSP 0x0010F300 /
    // PC 0x00C00402 come gli altri, s1 col 46% di byte a zero.
    { "sonicwi2", "SONIC WINGS 2", "VIDEO SYSTEM 1994",
      0x200000, 0x800000, 0x300000, 0x20000, 0x20000,
      0x346C9B58, 0xC9EEC367, 0xCF439D64, 0x12E375C8, 0xBB828DF1, 0x310843E1,
      false, false, nullptr, nullptr, 0 },
    // ---- Metal Slug 3 (SNK 2000, NGH-2560, set `mslug3`) -------------------
    // Il piu' pesante del gruppo e l'unico CIFRATO: 90,13 MB sulla SD contro i
    // 43 di Metal Slug 2. Le tre protezioni sono gia' sciolte dal converter
    // (CMC42 sulla grafica con chiave 0xAD, fix ricavata dalla C-ROM, P-ROM
    // descramblata dall'SMA); a runtime restano solo il BANK SWITCH dell'SMA e
    // il FIX A BANCHI, qui sotto.
    //   sprite  64 MB = 524.288 tile (15,3% invisibili)  -> cache da SD
    //   ADPCM-A 16 MB                                     -> cache da SD
    //   P-ROM    9 MB (0xC0000 di parte fissa + 256 KB di SMA + 8 MB bancati)
    //   fix e M1 512 KB ciascuno, NON 128 come tutti gli altri
    // ⚠️ 64+16 MB non entrano nei 32 di PSRAM in nessun modo: e' l'unico set
    //    che tiene DUE cache da SD. Vedi NG_SPR_CACHE / NG_PCM_CACHE.
    { "mslug3", "METAL SLUG 3", "SNK 2000",
      0x900000, 0x4000000, 0x1000000, 0x80000, 0x80000,
      0xD015D8AD, 0x2628D663, 0xABC863AC, 0xC66F744D, 0xEAEEC116, 0x644B999C,
      true,  true,  SMA_SCR_MSLUG3, SMA_OFF_MSLUG3, 1 },
    // ---- Metal Slug 4 (Mega/Playmore 2002, NGH-263, set `mslug4`) ----------
    // 69,72 MB sulla SD. Cifrato come Metal Slug 3 ma con protezioni DIVERSE,
    // tutte gia' sciolte dal converter:
    //   grafica  CMC50 con chiave 0x31   (Metal Slug 3 e' CMC42 con 0xAD)
    //   fix      ricavata dalla C-ROM    (come tutti i set CMC)
    //   M1       CIFRATO: sta nella regione 2 del .drv, non nella 1, e si
    //            ottiene con cmc50_m1_decrypt. Senza, il gioco sarebbe MUTO.
    //   campioni NEO-PCM2 (blocchi da 8 byte con le word permutate)
    //   P-ROM    IN CHIARO: niente SMA -> nessun bank switch speciale
    // ⚠️ La C-ROM e' 48 MB = 0x3000000, che NON e' potenza di due: e' il caso
    //    speciale del clamp nella decrittazione (vedi romconv_neogeo.py).
    //   sprite  48 MB = 393.216 tile (6,2% invisibili) -> cache da SD
    //   ADPCM-A 16 MB                                  -> cache da SD
    // ⚠️ i campioni vanno in cache come Metal Slug 3: da soli sarebbero 16 MB
    //    che, con i 16 della cache sprite e i 5 di programma, sforerebbero i 32
    //    di PSRAM. Con le due cache si sta sui ~26 MB, meno di Metal Slug 3.
    { "mslug4", "METAL SLUG 4", "PLAYMORE 2002",
      0x500000, 0x3000000, 0x1000000, 0x80000, 0x20000,
      0xC6DBDFF1, 0x1EAA05E0, 0x6D9B4F2C, 0x5A668BBC, 0xEF5DB532, 0x3BF11F09,
      true,  true,  nullptr, nullptr, 1, false },
    // ---- Metal Slug 5 (Playmore 2003, NGH-268, set `mslug5`) ---------------
    // 88,75 MB sulla SD: il piu' grosso del gruppo. Famiglia **NEO-PVC**, che
    // porta DUE cose che gli altri non hanno:
    //   P-ROM    8 MB **cifrata** (mslug5_decrypt_68k) e, nel .drv, assemblata
    //            INTERLACCIATA a word (p1 a dest 0x0, p2 a dest 0x2): le fa
    //            entrambe il converter. Il 68000 la vede in chiaro.
    //   PVC      protezione a RUNTIME: 8 KB di RAM di cartuccia a 0x2FE000 con
    //            pack/unpack dei colori e un bank switch tutto suo. E' qui
    //            sotto (ng_pvc_*), perche' gngeo non ce l'ha.
    //   campioni NEO-PCM2 variante **swap** (non snk_1999 come Metal Slug 4)
    //   grafica  CMC50 con chiave 0x19 · M1 cifrato nella regione 2 · fix
    //            ricavata dalla C-ROM · fix_bank_type 1 (init_mslug5)
    // ⚠️ DUE CACHE da SD come Metal Slug 3 e 4: 64 MB di sprite e 16 di campioni
    //    non entrano nei 32 di PSRAM nemmeno da soli.
    { "mslug5", "METAL SLUG 5", "PLAYMORE 2003",
      0x800000, 0x4000000, 0x1000000, 0x20000, 0x80000,
      0x3B0C2E7D, 0x64952683, 0x396C6768, 0x7A57E410, 0x39F3CBBA, 0x4182838B,
      true,  true,  nullptr, nullptr, 1, true },
    // ---- Blazing Star (Yumekobo 1998, NGH-239, set `blazstar`) -------------
    // 43,31 MB: la stessa taglia di Metal Slug 2, quindi la STESSA scelta —
    // sprite (32 MB) a banchi dalla SD, campioni (8 MB) in PSRAM.
    // ⭐ Set **NON cifrato**, e non e' una supposizione: i tre oracoli del
    //    progetto tornano tutti. Vettori del 68000 `10 00 00 F3 C0 00 02 04` =
    //    SSP 0x0010F300 / PC 0x00C00402 come gli altri set in chiaro; `m.bin`
    //    comincia con lo stub SNK **F3 C3 D0 00**; `s.bin` ha il **42%** di byte
    //    a zero (una fix cifrata sta sotto il 5%). Niente CMC, niente SMA,
    //    niente PCM2: il converter non applica nessuna decrittazione.
    // ⚠️ P-ROM da 3 MB in due pezzi ACCODATI (p1 a 0x000000, p2 a 0x100000):
    //    niente scambio di meta' e niente interlacciamento — il .drv e' pulito.
    { "blazstar", "BLAZING STAR", "YUMEKOBO 1998",
      0x300000, 0x2000000, 0x800000, 0x20000, 0x20000,
      0x635B1487, 0xD56CB498, 0x392D1B3A, 0xC179C26C, 0xD31A3AEA, 0x79544C10,
      true,  false, nullptr, nullptr, 0, false },
    // ---- Puzzle Bobble / Bust-A-Move (Taito 1994, NGH-083, set `pbobblen`) --
    // **9,76 MB: il piu' leggero di tutti** (Aero Fighters 2 ne fa 13,27) —
    // tutto in PSRAM, nessuna cache da SD.
    // ⭐ Set NON cifrato, provato con i tre oracoli: vettori `10 00 00 F3
    //    C0 00 02 04`, `m.bin` con lo stub SNK **F3 C3 D0 00**, `s.bin` col
    //    **49,8%** di byte a zero.
    // ⚠️ p.bin e' 1 MB perche' la regione del .drv e' 0x100000, ma il programma
    //    vero e' **512 KB**: la seconda meta' e' tutta a zero (verificato) e il
    //    gioco non la legge mai, perche' non fa bank switch. E' esattamente il
    //    caso gia' visto con Art of Fighting 2, ed e' cio' che fa gngeo.
    // ⚠️ Sprite in TRE coppie di misura DIVERSA (2+2+1 MB): il ciclo che le
    //    interlaccia le appaia per `dest`, quindi regge — ma e' il primo set del
    //    gruppo che non ha coppie tutte uguali.
    { "pbobblen", "PUZZLE BOBBLE", "TAITO 1994",
      0x100000, 0x500000, 0x380000, 0x20000, 0x20000,
      0x1C89D23E, 0x9CAAE538, 0x0F073E89, 0xFFF8FFED, 0xF424368A, 0xF864513F,
      false, false, nullptr, nullptr, 0, false },
    // ---- Metal Slug X (SNK 1999, NGH-2500, set `mslugx`) -------------------
    // 63,34 MB sulla SD. ⭐ Set **NON cifrato**, e i tre oracoli del progetto
    // tornano tutti: vettori del 68000 `10 00 00 F3 C0 00 02 04` = SSP
    // 0x0010F300 / PC 0x00C00402, `m.bin` che comincia con 0xF3 (DI), `s.bin`
    // col **48,7%** di byte a zero (una fix cifrata sta sotto il 5%).
    // Niente CMC, niente SMA, niente PCM2: il converter non decifra nulla.
    //   P-ROM   5 MB in due pezzi ACCODATI (p1 a 0, p2 a 0x100000): bank switch
    //           NORMALE a 0x2FFFF0, 4 banchi utili sui 4 MB della p2.
    //   sprite 48 MB = 393.216 tile (solo lo 0,2% invisibile) -> cache da SD
    //   ADPCM-A 10 MB                                          -> cache da SD
    //   S1 / M1 128 KB, fix layer NON a banchi (fix_bank_type 0)
    // ⚠️ DUE cache come Metal Slug 3/4: con i campioni in PSRAM resterebbero
    //    5 (p) + 10 (v) = 15 MB occupati e alla cache sprite ne avanzerebbero
    //    ~16 su 32 di PSRAM, cioe' il limite esatto — troppo tirato. Con la
    //    cache ADPCM si sta sui 25 MB, come Metal Slug 4.
    // ⛔ L'unica cosa che questo set ha in piu' e' la PROTEZIONE del chip
    //    ALTERA a 0x2FFFE0 (ng_mslugx_*): ultimo campo della riga.
    { "mslugx", "METAL SLUG X", "SNK 1999",
      0x500000, 0x3000000, 0xA00000, 0x20000, 0x20000,
      0x5E92F38B, 0xFB6F441D, 0x5A4B0D88, 0xB8AA2BA5, 0xFD42A842, 0x3A0D8E6A,
      true,  true,  nullptr, nullptr, 0, false, true },
    // ---- View Point (Sammy/Aicom 1992, NGH-051, set `viewpoin`) ------------
    // Dal CABINATONE, dove e' confermato: stessa riga, stessi CRC, stessi file
    // sulla SD (/neogeo/viewpoin/, 11,26 MB). Tutto in PSRAM, nessuna cache.
    // ⭐ Set NON cifrato: vettori `10 00 00 F3 C0 00 02 04`, `m.bin` con lo
    //    stub SNK F3 C3 D0 00, `s.bin` col 9,7% di byte a zero.
    // ⚠️ Sprite 6 MB con 2 MB di ZERI in mezzo (le C-ROM riempiono la regione a
    //    pezzi, ROM_CONTINUE di MAME): e' giusto, non un file rovinato.
    // ⛔ E' il gioco che ha richiesto il QUADRO SPEZZATO AL VBLANK (vedi
    //    ng_run_attivo): senza, una banda di sfondo lampeggia in partita.
    { "viewpoin", "VIEW POINT", "SAMMY 1992",
      0x100000, 0x600000, 0x400000, 0x20000, 0x20000,
      0x17AA899D, 0x9FEA5758, 0xCA03486D, 0x1B680250, 0x8E69F29A, 0x77F9135D,
      false, false, nullptr, nullptr, 0, false, false },
    // ---- Pulstar (Aicom 1995, NGH-089, set `pulstar`) -----------------------
    // 39,30 MB: la forma di Metal Slug 2 e Blazing Star (stessi autori) —
    // sprite (28 MB) a banchi dalla SD, campioni (8 MB) in PSRAM.
    // ⭐ Set NON cifrato, i tre oracoli: vettori `10 00 00 F3 C0 00 02 04`,
    //    `m.bin` con lo stub SNK F3 C3 D0 00, `s.bin` col 50,5% di byte a zero.
    // ⚠️ P-ROM 3 MB in due pezzi ACCODATI (p1 1 MB a 0, p2 2 MB a 0x100000):
    //    bank switch normale, come Metal Slug 2.
    // ⚠️ .ctile 28 MB = 0x1C00000, NON potenza di due: la guardia sui tile e'
    //    un confronto (>=), non una maschera, e gfx_cache prende la misura dal
    //    file. Nessun'altra costante la presuppone.
    // ⛔ Slot check SNK come Metal Slug 2: serve `sram.bin` sulla SD (vedi
    //    ng_task), altrimenti WARNING «ORIGINAL SNK BOARDS ONLY» per sempre.
    { "pulstar", "PULSTAR", "AICOM 1995",
      0x300000, 0x1C00000, 0x800000, 0x20000, 0x20000,
      0xC5C0BE46, 0xC79FC2C8, 0x7167911A, 0x7938BB54, 0xFF3DF7C7, 0x44036B4F,
      true,  false, nullptr, nullptr, 0, false, false },
};
#define NG_NGAMES ((int)(sizeof(NG_GAMES) / sizeof(NG_GAMES[0])))
static const NgGame *G = &NG_GAMES[0];     // gioco scelto dal menu

#define PROM_SZ    (G->prom_sz)
#define CTILE_SZ   (G->ctile_sz)
#define VROM_SZ    (G->vrom_sz)
// .cusage: 4 byte di magic + 2 bit per tile (16 tile per dword)
#define CUSAGE_SZ  (4 + ((CTILE_SZ >> 7) >> 4) * 4)

// ---- cache degli sprite da SD ---------------------------------------------
// Banchi da 4096 byte = 32 tile: un tile (128 byte) non sta MAI a cavallo di
// due banchi. Tetto 12 MB come RetroESP32-P4; gfx_cache_init dimezza da solo
// se la PSRAM non basta.
#define NG_SPR_BANK   4096
#define NG_SPR_SHIFT  12                   // log2(NG_SPR_BANK): shift, non divisione
// ⭐ 16 MB invece di 12: ogni miss e' una LETTURA DA SD, ed e' l'unico costo che
// Metal Slug 1 non paga (lui tiene i 16 MB di sprite tutti in PSRAM). Si passa
// dal 38% al 50% di sprite residenti. Il bilancio regge: mslug2 con 12 MB di
// cache usa ~24,4 MB di PSRAM, mslug ne usa ~27,4 e gira. Se non entrasse,
// gfx_cache_init scende da sola per gradi.
#define NG_SPR_CACHE  (16 * 1024 * 1024)
static GfxCache spr_cache;
static bool     spr_cache_on = false;

// ---- cache dei CAMPIONI ADPCM da SD ---------------------------------------
// Serve solo a Metal Slug 3: 16 MB di v.bin PIU' 9 MB di programma non
// lasciano alla cache degli sprite abbastanza spazio da reggere gli fps (su 64
// MB di grafica resterebbe residente il 7%). Con i campioni sulla SD la cache
// sprite torna a 16 MB, cioe' il 25%.
// ⭐ Costa pochissimo: l'ADPCM-A si legge in SEQUENZA, un byte ogni due
// campioni per canale — a 18,5 kHz un banco da 4 KB dura circa mezzo secondo,
// quindi si parla di una decina di letture da SD al secondo, non per frame.
// La cache e' la stessa `gfx_cache` gia' usata per gli sprite e per 1944.
#define NG_PCM_BANK   4096
#define NG_PCM_SHIFT  12
#define NG_PCM_CACHE  (4 * 1024 * 1024)
static GfxCache pcm_cache;
static bool     pcm_cache_on = false;
// memo dell'ultimo banco chiesto (vedi draw_sprite_tile)
static uint32_t       ng_last_bank = 0xFFFFFFFFu;
static const uint8_t *ng_last_ptr  = nullptr;
#define RAM_SZ      0x010000
#define VRAM_SZ     0x020000
#define SRAM_SZ     0x010000
#define PALRAM_SZ   0x004000         // 2 banchi da 0x2000
#define FIXUSE_B_SZ (FIXBIOS_SZ >> 5)   // un byte per tile: BIOS, sempre 4096
#define FIXUSE_G_SZ (FIXROM_SZ  >> 5)   // gioco: 4096, oppure 16384 su mslug3
#define VECTOR_SZ   0x80

#define M68K_JUMP_SIZE (0x10000 * sizeof(void *))
#define M68K_CYC_SIZE  (5 * 0x10000)

// tutto a runtime: in questo binario convivono 9 emulatori, un array statico
// occuperebbe RAM anche quando gira un altro gioco
static uint8_t  *prom = nullptr, *bios = nullptr;
static uint8_t  *fix_bios = nullptr, *fix_game = nullptr;
static uint8_t  *gfx = nullptr;              // la .ctile: sprite gia' convertiti
static uint8_t  *cusage_raw = nullptr;
static const uint32_t *tile_usage = nullptr; // dentro cusage_raw, dopo il magic
static uint8_t  *ram = nullptr, *vram = nullptr, *sram = nullptr;
static uint16_t *palram = nullptr;
static uint16_t *pal_lut = nullptr;          // LUT palette->RGB565 (vedi piu' avanti)
static inline void ng_pal_touch(int bank, int idx);   // usata dalle scritture, definita dopo
static uint8_t  *fix_usage_b = nullptr, *fix_usage_g = nullptr;
static uint8_t  *game_vector = nullptr;      // i vettori originali del gioco
static void     *m68k_jump = nullptr, *m68k_cyc = nullptr;
#if NG_LOAD_AUDIO_ROMS
static uint8_t  *mrom = nullptr;             // m.bin: programma del Z80 sonoro
static uint8_t  *vrom = nullptr;             // v.bin: campioni ADPCM-A (8 MB)
#endif

// ---- registri video / stato macchina (nomi e valori iniziali dell'harness) --
static uint16_t vptr = 0, rbuf = 0;
static int16_t  vmodulo = 1;                 // ⭐ video.c:1118 — con 0 la VRAM non avanza
static uint32_t bank_base = 0x100000;
// RAM di cartuccia della protezione NEO-PVC: 0x1000 word a 0x2FE000-0x2FFFFF.
// Allocata solo per i giochi che ce l'hanno (G->pvc); per gli altri resta NULL
// e nessuno dei controlli qui sotto costa niente.
static uint16_t *pvc_ram = nullptr;
#define PVC_WORDS 0x1000
static int      pal_bank = 0;
static int      fix_from_bios = 1;           // 0x3a000b/0x3a001b commutano fix e palette
static int      sram_lock = 0;
static int      cur_line = 0, frame_cnt = 0, anim_div = 0;

// ---- PERCHE' QUI NON C'E' IL WATCHDOG (2026-07-31) -------------------------
// ⛔ Provato e RIMOSSO: un watchdog che resetta dopo 8 frame senza scritture a
// $300001 (gngeo emu.c:788-794) sul PC si comporta benissimo — Metal Slug 2
// supera la protezione con UN solo reset e Metal Slug 1 non ne fa nessuno in
// 1200 frame — ma **sul device resetta in ciclo ENTRAMBI i giochi**.
// La causa della differenza non e' stata dimostrata, quindi non si tiene.
// ⭐ La strada giusta e' un'altra ed e' piu' fedele all'hardware: sul NeoGeo la
// backup RAM e' TAMPONATA A BATTERIA, quindi non e' mai "il primo avvio". Era
// quello a far scattare la protezione (NeoGeo Dev Wiki, Slot check security:
// al primo avvio il gioco forza un reset del watchdog e, se non arriva, mostra
// "ORIGINAL SNK BOARDS ONLY" e si blocca). Si carica dalla SD una backup RAM
// gia' "vissuta" e il gioco parte senza alcun reset: vedi sram.bin in ng_task.
static int      fc_speed = 8, irq2control = 0, anim_disabled = 0;

// ---- ingressi (attivi BASSI), aggiornati una volta per frame ---------------
static uint8_t  in_p1    = 0xFF;             // $300000: UP DOWN LEFT RIGHT A B C D
static uint8_t  in_start = 0xFF;             // $380000: bit0-3 START/SELECT
static uint8_t  in_coin  = 0x3F;             // $320001: bit0 = COIN1

// ---- video / PPA ----------------------------------------------------------
static uint16_t *fb = nullptr;               // 352x256 RGB565 (render nativo)
#if NG_BLIT_CORE0
// ⭐ Il quadro che il blit SPEDISCE non e' piu' per forza quello in cui si
// DISEGNA: con due framebuffer `fb` e' gia' passato all'altro mentre il core 0
// legge questo. Lo scrive il core 1 prima di svegliare il task, lo legge il
// core 0: `volatile`, e il passaggio e' ordinato dalla notifica FreeRTOS.
static const uint16_t * volatile ng_fb_out = nullptr;
static uint16_t *fb_due[2] = { nullptr, nullptr };   // [0] = l'allocazione di fb
#endif
static uint16_t *gamebuf = nullptr;          // solo se il PPA non scrive diretto
static size_t   fb_sz = 0, gb_sz = 0;
#if NG_HAS_PPA                               // il tipo esiste solo con driver/ppa.h
static ppa_client_handle_t ppa = nullptr;
#endif
static bool     ppa_ok = false;
static bool     ppa_direct = false;          // il PPA ha scritto nel pannello
#if NG_BLIT_PPA
// uscita del PPA: 224 x 240 RGB565, gia' scalata E ruotata, pronta per l'SPI.
// ⚠️ 107.520 byte = 840 x 128: e' gia' multiplo della linea di cache, come il
// driver PPA pretende ([[esp32p4_ppa_srm_vincoli]]).
#define NG_PPA_OUT_SZ ((size_t)NG_VH * LD_FB_H * 2)
static uint16_t *ng_ppa_out = nullptr;
#endif
static bool     ng_sw_scaled = false;        // gamebuf riempito dalla CPU?
static void    *panel_fb = nullptr;

// ============================================================ allocazione
// Preferisce la RAM INTERNA (il render legge VRAM e palette a ogni pixel) ma
// solo finche' ne resta abbastanza per il resto del sistema: stack, driver DSI,
// buffer del bridge. Sotto la riserva si passa alla PSRAM senza far fallire
// nulla. L'ordine delle chiamate e' l'ordine di priorita'.
// ⛔⛔⛔ 96 KB. L'avevo abbassata a 48 e il device mi ha smentito:
// **«TASK AUDIO KO IRAM 25K»**. Vale la pena scrivere perche', perche' l'errore
// di ragionamento e' istruttivo.
//
// Il mio argomento era: «la riserva serve all'I2S, che su CABINATONE si apre col
// gioco e qui e' gia' aperto al boot; e comunque con 98 KB liberi una riserva di
// 96 impedirebbe a `pal_lut` di entrare in interna». La prima meta' e' vera ma
// irrilevante; la seconda era **basata su un numero che non andava usato**: i
// «98.028 byte liberi» del report Arduino sono calcolati contro un totale
// (327.680) copiato dall'ESP32 classico, che sul P4 non vuol dire niente. La RAM
// interna vera a gioco avviato e' molto piu' alta, e infatti con 48 KB di
// riserva la `ram` del 68000 (64 KB) **entrava in interna** e si portava via
// proprio lo spazio che serviva al sonoro.
//
// ⭐ IL CRITERIO GIUSTO NON E' «quanti KB tengo da parte», E' **CHI DEVE STARE
// IN INTERNA PER FORZA**, in ordine di accessi al secondo:
//    stack del task audio  16 KB  — obbligatorio, un task non gira in PSRAM
//    tl_tab del YM2610     26 KB  — letta per operatore per campione (~500k/s)
//    pal_lut               16 KB  — una lettura per pixel disegnato (~150k/quadro)
// Sono ~58 KB che NON devono mai perdere la gara. La `ram` (64 KB) e la `vram`
// (128 KB) del 68000 fanno ~15.000 accessi a quadro in tutto: stanno benissimo
// in PSRAM. Con 96 KB di riserva sono esattamente loro due a restarne fuori.
// 👉 Regola generale, gia' scritta e qui riconfermata: **non conta quanta
// memoria si sposta, conta quanti accessi la toccano**
// ([[feedback_ottimizzare_contando_accessi]]).
#define NG_INT_RESERVE (96 * 1024)

static void *ng_alloc(size_t n, bool prefer_internal)
{
    void *p = nullptr;
    if (prefer_internal &&
        heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + NG_INT_RESERVE)
        p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
    if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    return p;
}

// ============================================================ calendario
// read_neo_control() — memory.c:85-121:
//   (scan << 7) | (conf.pal << 3) | (frame_counter & 7),  scan = linea + 0xF8
static uint16_t neo_control(void)
{
    const int scan = cur_line + 0xF8;
    return (uint16_t)((scan << 7) | (frame_cnt & 0x0007));
}

// ---- il latch del sonoro, visto dal lato 68000 -----------------------------
// Le definizioni stanno nella sezione "suono", piu' in basso: qui servono solo
// al bus. Con l'audio spento valgono i due valori inerti di gngeo con
// `conf.sound == 0` (memory.c:453: la lettura pari e' la costante 0x01).
#if NG_AUDIO
static inline uint8_t ng_sound_result(void);
static inline void    ng_sound_command(uint8_t code);
#else
static inline uint8_t ng_sound_result(void) { return 0x01; }
static inline void    ng_sound_command(uint8_t code) { (void)code; }
#endif

// ============================================================ bus del 68000
// Mappa per pagina (addr>>16), da musashi_interf.c di gngeo. COPIATA
// DALL'HARNESS: le uniche aggiunte sono i tre registri di ingresso.
static long r_io = 0, w_video = 0, w_pal = 0, w_ram = 0;

// ============================================================ NEO-PVC
// Protezione della famiglia mslug5 / svc / kof2003. Il gioco ci parla
// attraverso 8 KB di RAM sulla cartuccia, mappati a **0x2FE000-0x2FFFFF**:
// scrive dei parametri in certe celle e ne rilegge il risultato dalle celle
// accanto. Tre servizi, tutti dentro protection_w():
//   0xff0        converte un colore NeoGeo "impacchettato" nei suoi componenti
//   0xff4/0xff5  fa l'operazione inversa
//   0xff8/0xff9  BANK SWITCH della finestra 0x200000-0x2FFFFF
//
// ⛔ NON e' il bank switch dell'SMA di Metal Slug 3, che sta qualche riga piu'
//    giu': li' il valore scritto e' un INDICE coi bit mescolati da rimettere in
//    ordine e da cercare in una tabella; qui e' un INDIRIZZO assoluto, e la
//    lettura ha per giunta un effetto collaterale (rimette a posto due celle).
//    Confonderli farebbe eseguire al 68000 il megabyte sbagliato.
//
// ⭐ Trascritte da MAME src/devices/bus/neogeo/prot_pvc.cpp. gngeo NON le ha
//    (in roms.c `install_pvc_protection` e' commentata), quindi qui non c'e' un
//    porting funzionante da copiare: la fonte e' MAME e basta.
static void ng_pvc_unpack_color(void)
{
    const uint16_t pen = pvc_ram[0xff0];
    const uint8_t b = (uint8_t)(((pen & 0x000f) << 1) | ((pen & 0x1000) >> 12));
    const uint8_t g = (uint8_t)(((pen & 0x00f0) >> 3) | ((pen & 0x2000) >> 13));
    const uint8_t r = (uint8_t)(((pen & 0x0f00) >> 7) | ((pen & 0x4000) >> 14));
    const uint8_t s = (uint8_t)((pen & 0x8000) >> 15);
    pvc_ram[0xff1] = (uint16_t)((g << 8) | b);
    pvc_ram[0xff2] = (uint16_t)((s << 8) | r);
}

static void ng_pvc_pack_color(void)
{
    const uint16_t gb = pvc_ram[0xff4];
    const uint16_t sr = pvc_ram[0xff5];
    pvc_ram[0xff6] = (uint16_t)(((gb & 0x001e) >>  1) | ((gb & 0x1e00) >>  5) |
                                ((sr & 0x001e) <<  7) | ((gb & 0x0001) << 12) |
                                ((gb & 0x0100) <<  5) | ((sr & 0x0001) << 14) |
                                ((sr & 0x0100) <<  7));
}

// get_bank_base() di MAME: legge l'indirizzo dalle due celle, poi le RIMETTE A
// POSTO — l'effetto collaterale fa parte del protocollo, non e' pulizia.
static void ng_pvc_bankswitch(void)
{
    uint32_t b = (uint32_t)((pvc_ram[0xff8] >> 8) | (pvc_ram[0xff9] << 8));
    pvc_ram[0xff8] = (uint16_t)((pvc_ram[0xff8] & 0xfe00) | 0x00a0);
    pvc_ram[0xff9] &= 0x7fff;
    b += 0x100000;
    /* ⚠️ rete di sicurezza come per gli altri set: la finestra bancata e' di
       1 MB, quindi l'ultimo byte letto e' b + 0xFFFFF e deve stare dentro la
       p.bin. Senza, un valore sporco leggerebbe fuori dal buffer. */
    if (b + 0x100000 > PROM_SZ) b = 0x100000;
    bank_base = b;
}

// Vero se l'indirizzo cade nella finestra della RAM di cartuccia.
static inline bool ng_pvc_hit(unsigned int a)
{
    return pvc_ram && ((a & 0xFFFFFF) >= 0x2FE000) && ((a & 0xFFFFFF) <= 0x2FFFFF);
}

// offset in WORD dentro la RAM di cartuccia (e' cosi' che MAME indicizza)
static inline unsigned ng_pvc_off(unsigned int a) { return ((a & 0x1FFF) >> 1); }

// Parte comune di protection_w: scrive la word e fa scattare i tre servizi.
static void ng_pvc_store(unsigned off, uint16_t val)
{
    pvc_ram[off] = val;
    if (off == 0xff0)                     ng_pvc_unpack_color();
    else if (off >= 0xff4 && off <= 0xff5) ng_pvc_pack_color();
    else if (off >= 0xff8)                 ng_pvc_bankswitch();
}

// ---- MSLUGX: la protezione di Metal Slug X ---------------------------------
// Scheda NEO-MVS PROGEOP (1999.2.2), chip ALTERA EPM7128SQC100-15 legato alla
// 250-P1. Sta nella finestra **0x2FFFE0-0x2FFFEF** (MAME neogeo.cpp:1439-1440,
// case NEOGEO_MSLUGX): le scritture compongono un COMANDO, e ogni lettura
// restituisce **un solo BIT**, preso da un blocco di 0x1000 byte che sta nella
// P-ROM a 0xDEDD2. Il gioco lo usa come "firma": se i bit non tornano si ferma.
//
// ⛔ gngeo NON la emula: nella tabella di roms.c la voce {"mslugx",init_mslugx}
//    e' COMMENTATA, e quella init_mslugx non e' nemmeno un'emulazione — cerca
//    nel programma la sequenza `andi.w #1,D3 / bne` e ci scrive sopra dei NOP,
//    cioe' BUCA il controllo. Qui si e' scelto di emulare il chip: la fonte e'
//    MAME src/devices/bus/neogeo/prot_mslugx.cpp, trascritta riga per riga.
//
// ⭐ Le due letture passano da ng_r8/ng_r16, cioe' dagli stessi accessi del bus
//    del 68000 — e' quello che fa MAME con space.read_byte()/read_word(), e
//    cosi' ci si porta dietro gratis la convenzione little-endian nativa di
//    gngeo (byte = indirizzo XOR 1). Riscrivere gli accessi a mano qui sarebbe
//    la solita seconda copia che si da' ragione da sola.
static unsigned int ng_r8(unsigned int a);
static unsigned int ng_r16(unsigned int a);

static int      mslugx_prot    = 0;   // acceso in ng_task solo per questo set
static uint16_t mslugx_counter = 0;   // m_counter di MAME (uint16_t)
static uint16_t mslugx_command = 0;   // m_command

static inline bool ng_mslugx_hit(unsigned int a)
{
    return mslugx_prot && ((a & 0xFFFFFF) >= 0x2FFFE0) && ((a & 0xFFFFFF) <= 0x2FFFEF);
}

// protection_w(). `offset` in MAME e' l'indice in WORD dentro la finestra,
// quindi i suoi `case 0x2/2` qui diventano (a & 0xF) >> 1.
static void ng_mslugx_w(unsigned int a, uint16_t d)
{
    switch ((a & 0xF) >> 1) {
    case 0x0 / 2:  mslugx_command = 0; break;              /* start new read? */
    case 0x2 / 2:                                          /* comando: pulsato */
    case 0x4 / 2:  mslugx_command |= d; break;             /*   col dato, poi 0 */
    case 0x6 / 2:  break;                                  /* finished? */
    case 0xa / 2:  mslugx_counter = 0; mslugx_command = 0; break;   /* init? */
    default:       break;
    }
}

// protection_r(): un BIT per lettura. I due comandi sono quelli veri del gioco.
static uint16_t ng_mslugx_r(void)
{
    uint16_t res = 0;
    switch (mslugx_command) {
    case 0x0001:                    /* $3bdc(?) e $3c30 — registro D7 */
        res = (uint16_t)((ng_r8(0xdedd2 + ((mslugx_counter >> 3) & 0xfff))
                          >> (~mslugx_counter & 0x07)) & 1);
        mslugx_counter++;
        break;
    case 0x0fff: {                  /* tutti gli altri accessi — registro D2 */
        /* il selettore sta in RAM a 0x10F00A; il -1 e' di MAME, che ci mette
           accanto un "How should this be calculated?": e' comunque cio' che il
           gioco si aspetta, e non si inventa una formula diversa. */
        const int32_t select = (int32_t)ng_r16(0x10f00a) - 1;
        res = (uint16_t)((ng_r8(0xdedd2 + ((select >> 3) & 0x0fff))
                          >> (~select & 0x07)) & 1);
        break; }
    default: break;
    }
    return res;
}

static unsigned int ng_r8(unsigned int a)
{
    unsigned int pg = (a >> 16) & 0xFF;
    if (pg <= 0x0F) return prom[(a & 0xFFFFF) ^ 1];
    if (pg == 0x10) return ram[(a & 0xFFFF) ^ 1];
    /* ⚠️ PRIMA della finestra bancata: la RAM di cartuccia PVC sta dentro la
       pagina 0x2F, quindi se la si controllasse dopo non verrebbe mai letta. */
    if (ng_pvc_hit(a)) {
        const uint16_t w = pvc_ram[ng_pvc_off(a)];
        return (a & 1) ? (w & 0xFF) : (w >> 8);   /* 68000: byte pari = alto */
    }
    /* stessa ragione: la finestra della protezione di Metal Slug X sta DENTRO
       la pagina 0x2F e va vista prima del banco. MAME installa un gestore a 16
       bit, quindi una lettura a byte prende comunque il bit e poi ne mostra la
       meta' che le tocca (il byte pari e' quello alto). */
    if (ng_mslugx_hit(a)) {
        const uint16_t w = ng_mslugx_r();
        return (a & 1) ? (w & 0xFF) : (w >> 8);
    }
    if (pg >= 0x20 && pg <= 0x2F) return prom[(bank_base + (a & 0xFFFFF)) ^ 1];
    if (pg == 0xC0 || pg == 0xC1) return bios[(a & 0x1FFFF) ^ 1];
    if (pg >= 0xD0 && pg <= 0xDF) return sram[(a & 0xFFFF) ^ 1];
    if (pg == 0x30) {
        r_io++;
        // $300000 (byte PARI: il registro sta sul byte alto del bus) = P1.
        // Il byte dispari e' il DIP switch, tutto a 1 = nessuna opzione.
        if (!(a & 1)) return in_p1;
        return ((a & 0xFFFF) == 0x0081) ? 0xBF : 0xFF;
    }
    if (pg == 0x32) {   /* memory.c:425: intern_coin ^ (TP<<6) ^ (data<<7) */
        r_io++;
        if (!(a & 1)) return ng_sound_result(); /* pari: codice risultato suono */
        // ⭐ il bit 6 (Time Pulse) e il bit 7 (dato) vengono dal calendario: se
        // restano fermi il BIOS dice CALENDAR ERROR.
        return (Uint8)(in_coin ^ (read_4990_testbit() << 6)
                               ^ (read_4990_databit() << 7));
    }
    if (pg == 0x34) { r_io++; return 0xFF; }    /* P2: nessun secondo giocatore */
    if (pg == 0x38) { r_io++; return (a & 1) ? 0xFF : in_start; }
    if (pg == 0x40) return ((uint8_t *)palram)[((pal_bank << 13) | (a & 0x1FFF)) ^ 1];
    return 0xFF;
}

static unsigned int ng_r16(unsigned int a)
{
    unsigned int pg = (a >> 16) & 0xFF;
    if (pg <= 0x0F) { const uint8_t *p = prom + (a & 0xFFFFF); return (p[1] << 8) | p[0]; }
    if (pg == 0x10) { const uint8_t *p = ram + (a & 0xFFFF);   return (p[1] << 8) | p[0]; }
    if (ng_pvc_hit(a)) return pvc_ram[ng_pvc_off(a)];   /* prima della finestra bancata */
    if (ng_mslugx_hit(a)) return ng_mslugx_r();         /* idem: sta nella pagina 0x2F */
    if (pg >= 0x20 && pg <= 0x2F) { const uint8_t *p = prom + bank_base + (a & 0xFFFFF); return (p[1] << 8) | p[0]; }
    if (pg == 0xC0 || pg == 0xC1) { const uint8_t *p = bios + (a & 0x1FFFF); return (p[1] << 8) | p[0]; }
    if (pg >= 0xD0 && pg <= 0xDF) { const uint8_t *p = sram + (a & 0xFFFF); return (p[1] << 8) | p[0]; }
    if (pg == 0x3C) {                       /* registri video */
        switch (a & 0x7) {
        case 0x0: case 0x2: return rbuf;
        case 0x4: return (uint16_t)vmodulo;
        case 0x6: return neo_control();     /* ⭐ contatore raster: senza, il
                                               BIOS resta in busy-wait */
        default:  return 0;
        }
    }
    if (pg == 0x40) { const uint8_t *p = (uint8_t *)palram + (pal_bank << 13) + (a & 0x1FFF); return (p[1] << 8) | p[0]; }
    /* ⭐⭐ musashi_interf.c:157-168: le letture WORD dell'I/O vanno ai gestori,
       non a 0xFFFF. Con 0xFFFF il BIOS leggeva bit7=1 e bit6=1 SEMPRE, cioe'
       ogni bit del calendario a 1 e il Time Pulse fermo -> CALENDAR ERROR.
       memory.c:458: word = (byte_pari << 8) | byte_dispari. */
    if (pg == 0x30 || pg == 0x32 || pg == 0x34 || pg == 0x38)
        return (ng_r8(a & ~1u) << 8) | ng_r8(a | 1u);
    return 0xFFFF;
}

static unsigned int ng_r32(unsigned int a)
{
    return (ng_r16(a) << 16) | ng_r16(a + 2);
}

static void store_video(unsigned int a, uint16_t d)
{
    w_video++;
    switch (a & 0xF) {
    case 0x0: vptr = d & 0xFFFF; rbuf = (vram[(vptr << 1) + 1] << 8) | vram[vptr << 1]; break;
    case 0x2:
        vram[vptr << 1]       = d & 0xFF;
        vram[(vptr << 1) + 1] = d >> 8;
        /* l'avanzamento del puntatore e' quello di mem68k_store_video_word */
        vptr = (vptr & 0x8000) + ((vptr + vmodulo) & 0x7FFF);
        rbuf = (vram[(vptr << 1) + 1] << 8) | vram[vptr << 1];
        break;
    case 0x4: vmodulo = (int16_t)d; break;
    case 0x6:   /* write_neo_control (memory.c:123-127) */
        fc_speed      = ((d >> 8) & 0xFF) + 1;   /* velocita' auto-animazione */
        irq2control   = d & 0xFF;
        anim_disabled = (d >> 3) & 1;            /* neogeo_v.cpp:222 */
        break;
    default: break;                          /* 0x8/0xa irq2pos, 0xc ACK IRQ */
    }
}

static void ng_w8(unsigned int a, unsigned int v)
{
    unsigned int pg = (a >> 16) & 0xFF;
    /* memory.c:679 mem68k_store_z80_byte — comando al sonoro */
    if ((a & 0xFFFFFF) == 0x320000) { ng_sound_command((uint8_t)v); return; }
    if (pg == 0x38) { write_4990_control_w(a, v); return; }  /* memory.c:666 */
    if (pg == 0x10) { ram[(a & 0xFFFF) ^ 1] = v; w_ram++; return; }
    /* NEO-PVC: scrittura di un solo byte = COMBINE_DATA con mem_mask a meta'.
       Si ricompone la word e si passa dal solito ng_pvc_store, cosi' i tre
       servizi scattano anche se il gioco scrive a byte. */
    if (ng_pvc_hit(a)) {
        const unsigned off = ng_pvc_off(a);
        const uint16_t old = pvc_ram[off];
        ng_pvc_store(off, (a & 1) ? (uint16_t)((old & 0xFF00) | (v & 0xFF))
                                  : (uint16_t)((old & 0x00FF) | ((v & 0xFF) << 8)));
        return;
    }
    /* Metal Slug X scrive il comando a WORD; questa e' solo la rete di
       sicurezza. Il dato si mette nella corsia che gli spetta, esattamente come
       farebbe il gestore a 16 bit di MAME davanti a una scrittura a byte. */
    if (ng_mslugx_hit(a)) { ng_mslugx_w(a, (uint16_t)((a & 1) ? (v & 0xFF) : ((v & 0xFF) << 8))); return; }
    if (pg >= 0xD0 && pg <= 0xDF) { sram[(a & 0xFFFF) ^ 1] = v; return; }
    if (pg == 0x40) {
        const int off = (a & 0x1FFF);
        ((uint8_t *)palram)[((pal_bank << 13) | off) ^ 1] = v;
        ng_pal_touch(pal_bank, off >> 1);        /* tiene allineata la LUT */
        w_pal++; return;
    }
    if (pg == 0x3A) {
        /* ⭐ mappa VERA (memory.c:744-794): non e' un registro a bit, e' un
           dispatch su indirizzi ESATTI dispari. 0x3A000x = modo BIOS/sistema,
           0x3A001x = modo gioco. */
        switch (a & 0xFFFF) {
        case 0x0003: memcpy(prom, bios, VECTOR_SZ); break;        /* vettori BIOS  */
        case 0x0013: memcpy(prom, game_vector, VECTOR_SZ); break; /* vettori gioco */
        case 0x000B: fix_from_bios = 1; break;               /* fix del BIOS  */
        case 0x001B: fix_from_bios = 0; break;               /* fix del gioco */
        case 0x000D: sram_lock = 1; break;
        case 0x001D: sram_lock = 0; break;
        case 0x000F: pal_bank = 1; break;                    /* palette banco 1 */
        case 0x001F: pal_bank = 0; break;                    /* palette banco 0 */
        }
        return;
    }
    if (pg == 0x2F) {                       /* memory.c:874-888 */
        if ((a & 0xFFFFFF) < 0x2FFFF0) return;      /* solo il latch a 0x2FFFF0 */
        uint32_t b = 0x100000 + ((v & 0x7) << 20);
        /* ⛔⭐ Il clamp va sulla dimensione VERA della p.bin, non su 2 MB: con
           Metal Slug 2 (3 MB) il banco 0x200000 e' legittimo e riportarlo a
           0x100000 fa eseguire al 68000 il megabyte SBAGLIATO — sprite assenti
           e schermo nero, col fix layer del BIOS ancora visibile. Era rimasta
           la costante di Metal Slug (per cui PROM_SZ vale 0x200000: nessun
           cambiamento). Anche MAME confronta con la dimensione della regione. */
        if (b >= PROM_SZ) b = 0x100000;
        bank_base = b; return;
    }
}

static void ng_w16(unsigned int a, unsigned int v)
{
    unsigned int pg = (a >> 16) & 0xFF;
    /* memory.c:709 mem68k_store_z80_word: qui il codice sta nel byte ALTO
       ("tpgolf usa la scrittura word per il suono") */
    if ((a & 0xFFFFFF) == 0x320000) { ng_sound_command((uint8_t)(v >> 8)); return; }
    if (pg == 0x38) { write_4990_control_w(a, v); return; }
    if (pg == 0x10) { ram[a & 0xFFFF] = v & 0xFF; ram[(a & 0xFFFF) + 1] = v >> 8; w_ram++; return; }
    if (pg == 0x3C) { store_video(a, v); return; }
    if (pg == 0x40) {
        const int woff = (a & 0x1FFF);
        uint8_t *p = (uint8_t *)palram + (pal_bank << 13) + woff;
        p[0] = v & 0xFF; p[1] = v >> 8;
        ng_pal_touch(pal_bank, woff >> 1);       /* tiene allineata la LUT */
        w_pal++; return;
    }
    if (pg >= 0xD0 && pg <= 0xDF) { sram[a & 0xFFFF] = v & 0xFF; sram[(a & 0xFFFF) + 1] = v >> 8; return; }
    if (pg == 0x3A) { ng_w8(a | 1, v); return; }   /* word -> registro dispari (memory.c:796) */
    /* NEO-PVC: e' la strada normale (il gioco scrive word). Sta PRIMA del ramo
       0x2F qui sotto, che altrimenti se la prenderebbe come bank switch. */
    if (ng_pvc_hit(a)) { ng_pvc_store(ng_pvc_off(a), (uint16_t)v); return; }
    /* protezione di Metal Slug X: 0x2FFFE0-0x2FFFEF. Sta PRIMA del ramo 0x2F
       (il bank switch normale resta al suo posto, a 0x2FFFF0). */
    if (ng_mslugx_hit(a)) { ng_mslugx_w(a, (uint16_t)v); return; }
    if (pg == 0x2F) {                       /* memory.c:874-888 */
        /* ⭐ SET CON SMA (Metal Slug 3): il banco NON e' un numero da 0 a 7
           scritto a 0x2FFFF0, e' un valore coi BIT MESCOLATI scritto a un
           indirizzo tutto suo (0x2FFFE4 per mslug3). Si rimettono in ordine i
           sei bit e si legge l'offset dalla tabella — mem68k_store_bk_normal_word,
           memory.c:909-920, dove la condizione e' esattamente questa:
           (addr & 0xFF) == bksw_unscramble[0]. Nessun clamp: gli offset della
           tabella arrivano a 0x4C0000 e con la finestra da 1 MB il byte piu'
           alto letto e' 0x6BFFFF, dentro i 9 MB di p.bin. */
        if (G->bksw_scr && (a & 0xFF) == G->bksw_scr[0]) {
            const uint8_t *s = G->bksw_scr;
            uint32_t d = (((v >> s[1]) & 1) << 0) | (((v >> s[2]) & 1) << 1) |
                         (((v >> s[3]) & 1) << 2) | (((v >> s[4]) & 1) << 3) |
                         (((v >> s[5]) & 1) << 4) | (((v >> s[6]) & 1) << 5);
            bank_base = 0x100000 + G->bksw_off[d & 63];
            return;
        }
        if ((a & 0xFFFFFF) < 0x2FFFF0) return;      /* solo il latch a 0x2FFFF0 */
        uint32_t b = 0x100000 + ((v & 0x7) << 20);
        /* ⛔⭐ Il clamp va sulla dimensione VERA della p.bin, non su 2 MB: con
           Metal Slug 2 (3 MB) il banco 0x200000 e' legittimo e riportarlo a
           0x100000 fa eseguire al 68000 il megabyte SBAGLIATO — sprite assenti
           e schermo nero, col fix layer del BIOS ancora visibile. Era rimasta
           la costante di Metal Slug (per cui PROM_SZ vale 0x200000: nessun
           cambiamento). Anche MAME confronta con la dimensione della regione. */
        if (b >= PROM_SZ) b = 0x100000;
        bank_base = b; return;
    }
}

static void ng_w32(unsigned int a, unsigned int v)
{
    ng_w16(a, v >> 16);
    ng_w16(a + 2, v & 0xFFFF);
}

static long n_irq = 0;
static int ng_iack(int level)
{
    (void)level;
    n_irq++;
    m68k_set_irq(0);            /* neogeo_int_ack: la linea si abbassa QUI */
    return M68K_INT_ACK_AUTOVECTOR;
}
static const M68kBus NG_BUS = { ng_r8, ng_r16, ng_r32, ng_w8, ng_w16, ng_w32, ng_iack };

// ============================================================ palette
// ⭐⭐ La conversione NeoGeo->RGB565 si fa in SCRITTURA, non in lettura.
// `pal_rgb()` era chiamata **per ogni pixel** disegnato (sprite, fix, backdrop):
// ~150.000 volte per frame, ognuna con due letture, la ricomposizione della word,
// tre estrazioni di bit e il packing. Le scritture alla palette RAM sono invece
// ~50 per frame. Precalcolando in una LUT si passa da ~2 milioni di operazioni
// per frame a un singolo accesso a tabella per pixel.
// La LUT sta in RAM INTERNA (2 banchi x 4096 voci x 2 byte = 16 KB): e' letta
// per ogni pixel, quindi e' il dato piu' caldo che ci sia.
// (Stesso schema di `bb_update_palette` in boblbobl.cpp.)
static inline uint16_t ng_pal_conv(uint16_t c)
{
    /* NeoGeo: bit 15..12 = "dark" bits di R,G,B (qui il bit basso in piu') */
    const int r = (((c >> 8) & 0xF) << 1) | ((c >> 14) & 1);
    const int g = (((c >> 4) & 0xF) << 1) | ((c >> 13) & 1);
    const int b = (( c       & 0xF) << 1) | ((c >> 12) & 1);
    const uint16_t v = (uint16_t)(((r & 0x1F) << 11) | ((g & 0x1F) << 6) | (b & 0x1F));
    // ⭐⭐ ADATTAMENTO 1 di 6 (video), SECONDA META': il pannello SPI di questa
    // scheda vuole RGB565 **BYTE-SWAPPATO**; il DSI di CABINATONE no.
    // ⚠️ Lo swap va fatto QUI e in nessun altro posto: `pal_lut` e' l'unica
    // sorgente di colore del disegno (sprite, fix layer e backdrop leggono
    // tutti da li' via `pal_rgb`), quindi una riga sola copre l'intero
    // rendering e `draw_sprite_tile`/`draw_fix`/`clear_backdrop` restano
    // IDENTICHE a CABINATONE — che e' esattamente ciò che l'harness pretende.
    // Il nero della cornice (0x0000) e' invariante allo swap e non va toccato.
    // 👉 L'ha fatto emergere `confronta_neogeo.py`: segnalava `ng_pal_touch`
    //    come "identica" mentre l'intestazione del file prometteva la palette
    //    byte-swappata. Le due cose non potevano essere vere insieme.
    return (uint16_t)((v >> 8) | (v << 8));
}

// ricalcola UNA voce della LUT dopo una scrittura in palette RAM
static inline void ng_pal_touch(int bank, int idx)
{
    const uint8_t *p = (const uint8_t *)palram + (bank << 13) + idx * 2;
    pal_lut[(bank << 12) | idx] = ng_pal_conv((uint16_t)((p[1] << 8) | p[0]));
}

// il colore di un indice: un solo accesso, nessun calcolo
static inline uint16_t pal_rgb(int idx)
{
    return pal_lut[(pal_bank << 12) | (idx & 0xFFF)];
}

static void clear_backdrop(void)
{
    /* video.c:647: SDL_FillRect(buffer, NULL, current_pc_pal[4095]) — il fondo
       e' il BACKDROP, l'ultimo colore della palette attiva, non il nero. */
    /* due pixel per scrittura: 45.056 store invece di 90.112 (fb e' allineato) */
    const uint16_t bd = pal_rgb(4095);
    const uint32_t bd2 = ((uint32_t)bd << 16) | bd;
    // ⭐ Si riempie SOLO la finestra visibile (320x224), non tutto il quadro
    // 352x256: la scala legge da `fb + NG_VY*NG_W + NG_VX` esattamente quella
    // finestra (e il PPA usa gli stessi block_offset), quindi il bordo del
    // buffer non lo guarda nessuno. Da 45.056 a 35.840 store per frame, a
    // parita' ESATTA di immagine. Gli sprite possono scrivere fuori dalla
    // finestra (il blit clippa a 352x256): quei pixel restano li' e non
    // vengono mai letti.
    // ⚠️ NG_VX e NG_VW sono pari, quindi le righe restano allineate a 4 byte.
    for (int y = NG_VY; y < NG_VY + NG_VH; y++) {
        uint32_t *p = (uint32_t *)(fb + (size_t)y * NG_W + NG_VX);
        for (int i = 0; i < NG_VW / 2; i++) *p++ = bd2;
    }
}

// ============================================================ sprite
/* draw_screen() di video.c. Struttura in VRAM (indici word):
     SCB1  offs = count<<6   -> coppie (tileno, tileatr) della colonna
     SCB2  0x10000 + count   -> t3: zoom X (bit 8-11), zoom Y (bit 0-7)
     SCB3  0x10400 + count   -> t1: bit6 = "attaccato al precedente", bit0-5 =
                                numero di tile, bit7+ = posizione Y
     SCB4  0x10800 + count   -> t2: posizione X (bit 7+)
   ⚠️ Per gli sprite il nibble ALTO e' il pixel SINISTRO — convenzione OPPOSTA
   al fix layer (roms.c:1604: `dw |= pen << ((7-x)<<2)`). */
static inline uint16_t rd16v(unsigned off)   /* VRAM little-endian nativa */
{
    return (uint16_t)((vram[off + 1] << 8) | vram[off]);
}

// ---------------------------------------------------------------- zoom
// Tabelle trascritte VERBATIM da gngeo video.c:84-101 e 132.
// `ddaxskip[zx]` e' la maschera dei 16 pixel sorgente in ordine di
// DESTINAZIONE: ne restano accesi esattamente zx+1, e sono quelli emessi.
// ⭐ La riga 15 e' tutta di 1 e da' 16 pixel: quindi il ramo "zx==15" di gngeo
// (che salta la tabella) e' lo STESSO risultato di usare ddaxskip[15], e
// `rzx = (zx != 15) ? zx+1 : 16` si riduce a `rzx = zx + 1`.
static const uint8_t ddaxskip[16][16] = {
    { 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 },
    { 0, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0 },
    { 0, 0, 1, 0, 1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 1, 0 },
    { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 0, 0, 1, 0, 1, 0 },
    { 0, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
    { 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0 },
    { 1, 0, 1, 0, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0 },
    { 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 0 },
    { 1, 0, 1, 1, 1, 0, 1, 0, 1, 1, 1, 0, 1, 0, 1, 1 },
    { 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 0, 1, 1 },
    { 1, 0, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
    { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
};
// quante righe SORGENTE saltare prima di disegnare la riga di destinazione l.
// Senza zoom e' l'identita' (la prima riga non salta nulla).
static const uint8_t full_y_skip[16] = { 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
static uint8_t dda_y_skip[17];

/* video_template.h: `zx`=rzx e `zy`=yskip sono le dimensioni di DESTINAZIONE.
   - riga: `gfxdata += l_y_skip[l] << 1` avviene PRIMA di disegnare la riga l
     (2 dword = 1 riga sorgente), quindi la riga sorgente e' la somma corrente.
   - colonna: `br++` sta DENTRO l'if della maschera, quindi escono rzx pixel
     CONTIGUI; l'indice della maschera e' in ordine di destinazione, per cui
     con xflip l'indice 0 corrisponde al pixel sorgente 15.
   - yflip: le righe SORGENTE restano crescenti e si scrive DAL BASSO
     (`br` parte da `((zy-1)+sy)` e decrementa) — con lo zoom NON equivale a
     invertire la sorgente, per questo il flip e' sulla destinazione. */
static void draw_sprite_tile(uint32_t tileno, int sx, int sy, int rzx, int yskip,
                             const uint8_t *lys, const uint8_t *dxs,
                             int pal, int flipx, int flipy)
{
    /* ⭐ la .ctile e' 16 MB = 131.072 tile: con un limite di 8192 tutti gli
       sprite dei banchi alti (ta & 0x10/0x20/0x40) sarebbero respinti */
    if ((size_t)tileno >= (CTILE_SZ >> 7)) return;
#if NG_USE_CUSAGE
    /* tile interamente trasparente: il converter lo ha marcato, saltarlo non
       cambia un pixel (pen 0 non scrive) e toglie 128 byte di PSRAM da leggere */
    if (tile_usage && ((tile_usage[tileno >> 4] >> ((tileno & 15) * 2)) & 1)) return;
#endif
    /* I 128 byte del tile: dalla PSRAM se il gioco ci sta tutto, altrimenti dal
       banco di cache letto da SD. ⚠️ TRAPPOLA di gfx_cache: dopo la chiamata
       l'indice e' LOCALE al banco — tutto cio' che serve sull'indice GLOBALE
       (qui il .cusage) va calcolato PRIMA, ed e' fatto sopra.
       Un tile sta sempre dentro un solo banco: 4096 = 32 x 128, allineati. */
    const uint8_t *t;
    if (spr_cache_on) {
        const uint32_t off = (uint32_t)tileno << 7;
        const uint32_t bk  = off >> NG_SPR_SHIFT;
        /* ⭐ memo dell'ULTIMO banco: gli sprite NeoGeo usano tile CONSECUTIVI
           (una colonna e' fino a 32 tile = esattamente un banco da 4096 B),
           quindi chiamate di fila chiedono lo stesso banco. Cosi' si risparmia
           l'accesso alla tabella `ptr[]` in PSRAM su gran parte delle ~1.300
           chiamate per frame.
           ⚠️ Sicuro: lo sfratto avviene SOLO dentro gfx_cache_miss, che si
           raggiunge solo da gfx_cache_bank — fra due chiamate consecutive con
           lo stesso banco non puo' esserci uno sfratto, e appena il banco
           cambia la memo viene riscritta. */
        const uint8_t *b;
        if (bk == ng_last_bank) {
            b = ng_last_ptr;
        } else {
            b = gfx_cache_bank(&spr_cache, off);
            ng_last_bank = bk; ng_last_ptr = b;
        }
        t = b + (off & (NG_SPR_BANK - 1));
    } else {
        t = gfx + ((size_t)tileno << 7);                 /* 128 byte per tile */
    }
    /* la riga di palette e' la stessa per tutti i 256 pixel del tile: si calcola
       una volta sola invece che a ogni pixel. pal <= 0xFF -> base+pen <= 4095,
       quindi l'indice non esce mai dal banco e il mascheramento e' superfluo. */
    const uint16_t *prow = pal_lut + (pal_bank << 12) + pal * 16;

    /* ---- caso comune: nessuno zoom (rzx=16, yskip=16) ----------------------
       E' il ramo gia' verificato byte per byte, ed e' quello che decide gli fps:
       in Metal Slug la stragrande maggioranza degli sprite ha zx=15 e rzy=255.
       Con la maschera tutta accesa e i salti tutti a 1, il ramo zoom qui sotto
       darebbe gli stessi pixel, ma pagando un test per pixel. */
    if (rzx == 16 && yskip == 16) {
        /* ⚠️ `px` NON puo' uscire dal quadro, quindi il confronto per pixel e'
           un ramo morto: il chiamante ammette solo `sx >= -16 && sx+15 < 336`,
           cioe' sx in [-16, 320], e passa sx+NG_VX = sx+16 in [0, 336]; da li'
           px = sx+16 + 0..15 sta in [0, 351] e NG_W e' 352.
           `py` invece va controllato davvero: sy puo' valere -15..255. */
        const int passo  = flipx ? -1 : 1;              /* invariante per tile */
        const int inizio = flipx ? 15 : 0;
        for (int y = 0; y < 16; y++) {
            const int py = sy + y;
            if (py < 0 || py >= NG_H) continue;
            const int ty = flipy ? (15 - y) : y;
            /* t e' allineato a 128 e l'offset e' multiplo di 4: la lettura a 32
               bit e' lecita e su little-endian da' lo stesso identico valore
               della ricomposizione byte per byte — 1 accesso alla PSRAM su 4. */
            const uint32_t *q = (const uint32_t *)(t + ty * 8);
            const uint32_t d0 = q[0], d1 = q[1];
            /* riga tutta trasparente: la salta anche gngeo (video_template.h:45,
               `if (gfxdata[1] || gfxdata[0])`). Sui tile veri capita spesso. */
            if (!(d0 | d1)) continue;
            uint16_t *d = fb + (size_t)py * NG_W + sx + inizio;
            uint32_t w = d0;
            for (int half = 0; half < 2; half++) {
                for (int x = 0; x < 8; x++) {
                    const int pen = (int)(w >> 28);     /* nibble ALTO = pixel sinistro */
                    w <<= 4;
                    if (pen) *d = prow[pen];
                    d += passo;
                }
                w = d1;
            }
        }
        return;
    }

    /* ---- sprite scalato ---------------------------------------------------
       ⚠️ `srcrow` non puo' uscire dal tile: il DDA distribuisce ESATTAMENTE 16
       righe sorgente sulle voci 0..yskip di dda_y_skip, e il blit ne usa solo
       0..yskip-1, quindi la somma si ferma a 16 - dda_y_skip[yskip] <= 15.
       Per full_y_skip la somma e' 15. Nessun clamp da aggiungere. */
    int srcrow = 0;
    for (int l = 0; l < yskip; l++) {
        srcrow += lys[l];
        const int py = flipy ? (sy + yskip - 1 - l) : (sy + l);
        if (py < 0 || py >= NG_H) continue;
        const uint32_t *q = (const uint32_t *)(t + srcrow * 8);
        const uint32_t d0 = q[0], d1 = q[1];
        /* ⭐ riga tutta trasparente: la salta gngeo stesso (video_template.h:45,
           `if (gfxdata[1] || gfxdata[0])`) e la saltava gia' il ramo veloce qui
           sopra — mancava SOLO qui. Equivalenza: con d0|d1 == 0 ogni pen e' 0,
           quindi il ciclo non scriveva niente comunque; `dx` e' locale alla riga
           e `srcrow` e' gia' stato avanzato. Zero pixel di differenza. */
        if (!(d0 | d1)) continue;
        uint16_t *drow = fb + (size_t)py * NG_W + sx;
        /* ⚠️ `sx + dx` NON puo' uscire dal quadro, quindi il confronto per pixel
           era un ramo morto — la stessa dimostrazione del ramo veloce: il
           chiamante ammette solo `sx >= -16 && sx+15 < 336` e passa `sx+NG_VX`,
           cioe' [0, 336]; `dx` arriva al massimo a rzx-1 <= 15 perche' le voci
           non nulle di `ddaxskip[zx]` sono ESATTAMENTE zx+1 = rzx. Quindi
           l'indice sta in [0, 351] e NG_W e' 352. */
        int dx = 0;
        /* ⭐ `flipx` deciso UNA VOLTA per riga invece che a ogni pixel: prima
           costava due ternari e uno shift a distanza variabile per pixel. Qui
           il nibble esce da un registro che scorre, come nel ramo veloce.
           senza flip: dword 0 = meta' sinistra, nibble ALTO = pixel sinistro;
           con flip si parte dalla dword 1 e i nibble si leggono al contrario
           (video_template.h:227-245 contro :290-309). */
        if (!flipx) {
            uint32_t w = d0;
            for (int i = 0; i < 16; i++) {
                const int pen = (int)(w >> 28);
                w <<= 4;
                if (i == 7) w = d1;
                if (dxs[i]) {                      /* 0 = pixel sorgente scartato */
                    if (pen) drow[dx] = prow[pen];
                    dx++;                          /* avanza anche se trasparente */
                }
            }
        } else {
            uint32_t w = d1;
            for (int i = 0; i < 16; i++) {
                const int pen = (int)(w & 0xF);
                w >>= 4;
                if (i == 7) w = d0;
                if (dxs[i]) {
                    if (pen) drow[dx] = prow[pen];
                    dx++;
                }
            }
        }
    }
}

static void draw_sprites(void)
{
    int sx = 0, sy = 0, oy = 0, my = 0, zx = 1, rzy = 1, rzx = 16, fullmode = 0;
    int dday = 0, yskip = 16;
    for (unsigned count = 0; count < 0x300; count += 2) {
        const uint16_t t3 = rd16v(0x10000 + count);
        const uint16_t t1 = rd16v(0x10400 + count);
        const uint16_t t2 = rd16v(0x10800 + count);

        if (t1 & 0x40) {                 /* colonna attaccata alla precedente */
            /* ⚠️ `sx` avanza della larghezza della colonna PRECEDENTE: lo zoom
               nuovo si legge DOPO (video.c:662-670). Con rzx fisso a 16 le
               colonne attaccate di uno sprite ristretto si scollavano. */
            sx += rzx;
            zx = (t3 >> 8) & 0x0f;
            sy = oy;
        } else {
            zx  = (t3 >> 8) & 0x0f;
            rzy = t3 & 0xff;
            if (rzy == 0) continue;
            sx = t2 >> 7;
            my = t1 & 0x3f;
            fullmode = (my == 0x20) ? 1 : ((my >= 0x21) ? 2 : 0);
            sy = 0x200 - (t1 >> 7);
            if (sy > 0x110) sy -= 0x200;
            if (fullmode == 2 || (fullmode == 1 && rzy == 0xff))
                while (sy < 0) sy += ((rzy + 1) << 1);
            oy = sy;
            if (rzy < 0xff && my < 0x10 && my) { my = my * 255 / rzy; if (my > 0x10) my = 0x10; }
            if (my > 0x20) my = 0x20;
        }
        if (my == 0) continue;

        /* zoom X (video.c:719-724). ddaxskip[15] e' tutta di 1 e vale 16 pixel,
           quindi copre anche il ramo "niente zoom" senza un caso a parte. */
        const uint8_t *dxs = ddaxskip[zx];
        rzx = zx + 1;

        if (sx >= 0x1F0) sx -= 0x200;
        if (sx >= 320) continue;

        /* zoom Y (video.c:730-734): `dday` si azzera UNA VOLTA per colonna e si
           porta dietro la fase da un tile all'altro dello strip. */
        if (rzy == 255) yskip = 16; else dday = 0;

        unsigned offs = count << 6;
        for (int y = 0; y < my; y++) {
            uint32_t tileno  = rd16v(offs);     offs += 2;
            const uint16_t ta = rd16v(offs);    offs += 2;
            /* MAME neogeo_spr.cpp:375: code = ((attr<<12)&0xf0000)|tile -> 20 bit.
               ⚠️ gngeo usa solo 0x10/0x20/0x40 e IGNORA il bit 7 (video.c:525-531):
               serve ai giochi con C-ROM >= 64 MB (KOF2003, Metal Slug 5, SVC). */
            tileno |= ((uint32_t)(ta & 0xF0) << 12);
            /* ⭐ MAME (neogeo_spr.cpp:378-384) SOSTITUISCE i bit bassi; gngeo li
               SOMMA (video.c:533). Coincidono solo se il tile base e' allineato:
               l'hardware sostituisce. Priorita' al bit 3 sul bit 2. */
            if (!anim_disabled) {
                if (ta & 0x8)      tileno = (tileno & ~7u) | (frame_cnt & 7);
                else if (ta & 0x4) tileno = (tileno & ~3u) | (frame_cnt & 3);
            }

            if (fullmode == 2 || (fullmode == 1 && rzy == 0xff)) {
                if (sy >= 248) sy -= ((rzy + 1) << 1);
            } else if (fullmode == 1) {
                if (y == 0x10) sy -= ((rzy + 1) << 1);
            } else if (sy > 0x110) sy -= 0x200;

            /* DDA verticale, uno per TILE (video.c:777-795). Le 16 righe
               sorgente si distribuiscono su `yskip` righe di destinazione. */
            if (rzy != 255) {
                yskip = 0;
                dda_y_skip[0] = 0;
                for (int i = 0; i < 16; i++) {
                    dda_y_skip[i + 1] = 0;
                    dday -= (rzy + 1);
                    if (dday <= 0) { dday += 256; yskip++; dda_y_skip[yskip]++; }
                    else dda_y_skip[yskip]++;
                }
            }

            if (sx >= -16 && sx + 15 < 336 && sy > -16 && sy < 256)   /* il blit clippa per riga */
                draw_sprite_tile(tileno, sx + NG_VX, sy, rzx, yskip,
                                 /* video_template.h:29-32 sceglie in base a zy,
                                    non a rzy: con yskip=16 vale l'identita'. */
                                 (yskip == 16) ? full_y_skip : dda_y_skip,
                                 dxs, ta >> 8, ta & 0x01, ta & 0x02);
            /* ⚠️ `sy += yskip`, non 16: e' l'altezza EFFETTIVA appena disegnata
               (video.c:872). Con 16 fisso gli sprite scalati si sfilacciavano. */
            sy += yskip;
        }
    }
}

// ============================================================ fix layer
/* draw_fix_char() di video.c: VRAM word a 0xE000+((y+(x<<5))<<1),
   bit0-11 = tile, bit12-15 = palette. Tile = 8 uint32, un nibble per pixel,
   dal piu' alto (pixel 7) al piu' basso (pixel 0). Pen 0 = trasparente. */
static void draw_fix(int fill)
{
    const uint8_t *fixrom = fix_from_bios ? fix_bios : fix_game;
    const uint8_t *usage  = fix_from_bios ? fix_usage_b : fix_usage_g;
    if (fill) clear_backdrop();

    /* ⭐ FIX A BANCHI (Metal Slug 3, Garou) — video.c:380-395.
       La fix del gioco e' 512 KB = 16.384 tile, ma il codice nella VRAM ha
       solo 12 bit (4096). Il banco lo sceglie il GIOCO scrivendo una coppia di
       marcatori nella VRAM: la word a 0xEA00 vale 0x0200 e quella a 0xEB00 ha
       il byte alto 0xFF; i due bit bassi di quest'ultima sono il banco, e vale
       da quella riga in giu'. Senza questo, tre quarti dei caratteri di Metal
       Slug 3 sarebbero quelli sbagliati.
       ⚠️ 33 voci e non 32: in gngeo il ramo che trova il marcatore incrementa
       `y` DUE volte, quindi all'ultimo giro scrive una posizione oltre la fine
       del suo array. Qui c'e' spazio, cosi' il comportamento resta identico
       senza scrivere fuori. */
    int garou[33];
    const int fix_banked = (!fix_from_bios && G->fix_bank_type == 1 &&
                            FIXROM_SZ > 0x1000);
    if (fix_banked) {
        int banco = 0, k = 0, y = 0;
        while (y < 32) {
            const int a = 0xEA00 + (k << 1), b = 0xEB00 + (k << 1);
            const uint16_t wa = (uint16_t)((vram[a + 1] << 8) | vram[a]);
            const uint16_t wb = (uint16_t)((vram[b + 1] << 8) | vram[b]);
            if (wa == 0x0200 && (wb & 0xFF00) == 0xFF00) {
                banco = wb & 3;
                garou[y++] = banco;
            }
            garou[y++] = banco;
            k += 2;
        }
    }

    const uint32_t n_fix_tiles = FIXROM_SZ >> 5;
    for (int y = 1; y < 31; y++)
        for (int x = 1; x < 39; x++) {
            int off = 0xE000 + ((y + (x << 5)) << 1);
            uint16_t w = (vram[off + 1] << 8) | vram[off];
            int code = w & 0xFFF, pl = w >> 12;
            /* il banco vale per la riga: video.c:420, con lo XOR 3 */
            if (fix_banked) code += 0x1000 * (garou[(y - 2) & 31] ^ 3);
            if ((uint32_t)code >= n_fix_tiles) continue;   /* video.c:428 */
            if (!usage[code]) continue;      /* tile vuoto */
            const uint8_t *g = fixrom + (code << 5);
            /* ⭐ la riga di palette e' la stessa per tutti i 64 pixel del tile:
               si calcola UNA volta, non a ogni pixel. E' lo stesso guadagno che
               sugli sprite ha risolto gli fps (pal_rgb girava ~150.000 volte per
               frame). `pl` e' 4 bit -> pl*16+col <= 255: non esce dal banco. */
            const uint16_t *prow = pal_lut + (pal_bank << 12) + pl * 16;
            /* ⚠️ rami morti tolti, con la prova: y sta in [1,30] -> py = 8..247,
               e NG_H e' 256; x sta in [1,38] -> px = 8*38+7+16 = 327 al massimo,
               e NG_W e' 352. Nessuno dei due confronti poteva scattare. */
            uint16_t *drow = fb + (size_t)(y * 8) * NG_W + (x * 8 + NG_VX);
            for (int yy = 0; yy < 8; yy++, drow += NG_W) {
                uint32_t mw = *(const uint32_t *)(g + yy * 4);   /* allineato: vedi draw_sprite_tile */
                if (!mw) continue;           /* riga tutta trasparente */
                for (int xx = 0; xx < 8; xx++) {
                    const int col = (int)(mw & 0xF);   /* qui il nibble BASSO e'
                                                          il pixel SINISTRO:
                                                          convenzione OPPOSTA
                                                          agli sprite */
                    mw >>= 4;
                    if (col) drow[xx] = prow[col];
                }
            }
        }
}

// Ordine di gngeo (video.c:880): fondo, poi sprite, poi il fix SOPRA. E' la
// "PROVA B" dell'harness, quella che ha prodotto neogeo_frame.png.
// ⭐ Le COLONNE DI BORDO vanno coperte col fondo.
// gngeo mostra 304 pixel di larghezza su 320 perche' le colonne ai due estremi
// sull'arcade vero stanno **sotto la cornice del cabinato**: li' i giochi
// lasciano entrare e uscire gli sprite mezzi disegnati. Noi mostriamo tutte e
// 320 (e' l'unica larghezza con una scala esatta per il PPA, 39/16), quindi
// quelle colonne si vedono: comparivano come due bande verticali che
// LAMPEGGIANO al passaggio del personaggio.
// Si coprono col backdrop **dentro `fb`, come ultima cosa del frame**, cosi'
// passano per la scala insieme all'immagine e non possono sfarfallare — stessa
// regola della barra del volume.
// ⚠️ NERO, non il backdrop: quelle bande ERANO il backdrop (ciano in quel
// livello) che restava scoperto ai due estremi. Ridipingerle col backdrop non
// avrebbe cambiato un pixel — vanno spente. Vale per TUTTI i giochi NeoGeo:
// e' una caratteristica dell'hardware, non del singolo gioco.
#define NG_BORDO 8                       // colonne mascherate per lato
static void ng_mask_borders(void)
{
    const uint16_t bd = 0x0000;          // nero = cornice del cabinato
    for (int y = NG_VY; y < NG_VY + NG_VH; y++) {
        uint16_t *row = fb + (size_t)y * NG_W;
        for (int x = 0; x < NG_BORDO; x++) {
            row[NG_VX + x]                    = bd;   // bordo sinistro
            row[NG_VX + NG_VW - 1 - x]        = bd;   // bordo destro
        }
    }
}

static void ng_render(void)
{
    clear_backdrop();
    draw_sprites();
    draw_fix(0);
    ng_mask_borders();               // ultima cosa: vedi sopra
}

// ============================================================ conversione S-ROM
/* convert_all_char() di roms.c: la S-ROM va riordinata prima di poterla usare.
   Per ogni tile (32 byte) 8 gruppi da 4 byte presi dagli offset 16,24,0,8.
   L'OR dei byte da' l'usage: 0 = tile vuoto, si salta. */
static bool converti_fix(uint8_t *p, int taille, uint8_t *usage)
{
    uint8_t *src = (uint8_t *)heap_caps_malloc(taille, MALLOC_CAP_SPIRAM);
    if (!src) return false;
    memcpy(src, p, taille);
    uint8_t *s = src, *d = p;
    for (int i = taille; i > 0; i -= 32) {
        uint8_t u = 0;
        for (int j = 0; j < 8; j++) {
            *d++ = s[16]; u |= s[16];
            *d++ = s[24]; u |= s[24];
            *d++ = s[0];  u |= s[0];
            *d++ = s[8];  u |= s[8];
            s++;
        }
        s += 24;
        *usage++ = u;
    }
    heap_caps_free(src);
    return true;
}

// ============================================================ barra volume
// ⭐ ADATTAMENTO — su CABINATONE la barra si disegnava DENTRO fb; qui NO.
// Nei player autonomi dello SPINNERINO il blit riduce l'immagine in orizzontale
// (qui 320->240) e l'area del gioco parte a 48 px dal bordo: una barra dentro
// `fb` uscirebbe di misura diversa dagli altri giochi e nel posto sbagliato.
// La disegna `videoVolBarTop()` in pixel di PANNELLO, nella banda nera, con la
// stessa geometria di tutti gli altri (112x16). Vedi
// [[feedback_barra_volume_player_autonomi]].
// ⚠️ Non lampeggia perche' si ridisegna SOLO quando cambia qualcosa: la banda
// nera sta fuori dalla finestra stretta del gioco, quindi fra un aggiornamento
// e l'altro resta esattamente com'era.
static void ng_window(int quale);        // definita sotto, insieme al blit
#if NG_BLIT_OVERLAP
// ⚠️ ng_blit() sta PRIMA nel file e chiama questa: serve la dichiarazione
// anticipata, o il compilatore la trova solo 900 righe piu' in basso.
static void ng_emula_fette(int fino_a);
#endif

#if NG_MISURA
// Testo dentro `fb`, in coordinate di GIOCO: passa dal blit insieme
// all'immagine, quindi non puo' lampeggiare e non serve una finestra SPI sua.
// ⭐ Scala in UNA costante: a 2 ogni carattere e' 16x16 nel quadro, ~12x16
// sul pannello dopo la riduzione 3-su-4 delle colonne. 19 caratteri = 304 px:
// stanno nei 320 visibili partendo da NG_VX + 2.
#define NG_MIS_SC 2
static void ng_fb_text(int x, int y, const char *s, uint16_t col)
{
    for (int i = 0; s[i]; i++) {
        const int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8 * NG_MIS_SC; r++) {
            const unsigned char row = pgm_read_byte(&FONT8[idx][r / NG_MIS_SC]);
            uint16_t *d = fb + (size_t)(y + r) * NG_W + x + i * 8 * NG_MIS_SC;
            for (int c = 0; c < 8 * NG_MIS_SC; c++)
                if (row & (0x80 >> (c / NG_MIS_SC))) d[c] = col;
        }
    }
}

// ============================================================================
// ⭐⭐ COSA SI LEGGE, e perche' queste voci e non altre.
//
// Un contatore di FPS DA SOLO e' cieco: questo ciclo e' a passo fisso (in fondo
// aspetta fino a `t0 + FRAME_US`), quindi segna il valore nominale finche' non
// e' GIA' rotto e non dice mai QUANTO MANCA a rompersi
// ([[feedback_fps_a_passo_fisso_sono_ciechi]]). Il numero che decide e' il
// CARICO; quello che spiega gli scatti e' il PICCO.
//
//   riga 1  FPS nn EMS nn CAR nnn PIC nn.n
//     FPS = quadri VISTI al secondo (con NG_VIDEO_HALF_RATE sono meta' di
//           quelli emulati: e' il numero che l'occhio riferisce)
//     EMS = quadri EMULATI al secondo, cioe' i GIRI del loop. Serve a rendere
//           il bilancio verificabile da chi legge: **dev'essere 2 x FPS**.
//     CAR = CARICO: lavoro medio per giro / FRAME_US, in %. 60 = largo,
//           95 = al limite, oltre 100 = quadri persi. **E' il numero che decide
//           se c'e' qualcosa da ottimizzare.** Se sta sotto 100, il gioco non
//           va toccato e le cure candidate si buttano.
//     PIC = PICCO del lavoro di un singolo giro nel secondo, in ms (budget
//           16,9). La media nasconde il picco: con CAR 80 e PIC 20 si perdono
//           quadri lo stesso ([[feedback_misurare_anche_cosa_non_cerchi]]).
//
//   riga 2  CPU nn.n REN nn.n BLT nn.n     (millisecondi medi per giro)
//     CPU = i due passi di m68k_execute (emulazione del 68000)
//     REN = ng_render(): backdrop + sprite + fix layer dentro fb
//     BLT = ng_blit(): riduzione 320->240 e trasferimento SPI
//   ⛔⛔ CON `NG_BLIT_OVERLAP 1` QUESTE DUE VOCI CAMBIANO SIGNIFICATO, e va
//   saputo PRIMA di leggerle o il referto viene interpretato al contrario:
//     CPU = solo la CODA del quadro (vblank e retrace) -> **deve essere BASSA**,
//           meno di 1 ms. Le 264 fette non sono piu' li'.
//     BLT = il blit **PIU' le 264 fette del quadro successivo** eseguite dentro
//           le sue attese, cioe' **max(DMA · emulazione)**, non la somma.
//   👉 Percio' il confronto col referto di ieri si fa su **CAR**, non su BLT:
//      CAR e' l'unica voce che significa la stessa cosa nei due casi.
//      Atteso: da 182/107 a circa 110 (18,4 ms su 16,9), con FPS ~55-60.
//   👉 E se BLT resta 12,4 mentre CPU crolla, l'overlap sta funzionando: vuol
//      dire che le fette sono entrate DENTRO l'attesa del DMA senza allungarla.
//
//   riga 3  SD nnn MIS nnnn AUD nn
//     SD  = **ms AL SECONDO** (su 1000) passati dentro disk_read a prendere i
//           banchi di sprite dalla SD. ⭐⭐ E' la voce che serviva e non c'era:
//           e' compresa dentro REN, ed e' **l'unica differenza fra METAL SLUG e
//           METAL SLUG 2**. Su Metal Slug deve stare a 0 (grafica tutta in
//           PSRAM); se su Metal Slug 2 e' grossa, la manopola e' la CACHE
//           (NG_SPR_CACHE), non il renderer.
//     MIS = MISS della cache sprite **AL SECONDO**. Insieme a SD dice quanto
//           costa un miss (SD/MIS): con MIS alto e SD basso la SD non e' il
//           problema.
//     ⛔ Erano tutt'e due PER GIRO, in aritmetica intera, e sotto l'unita'
//        mostravano 0: il 2026-08-29 Metal Slug 2 ha segnato `SD 0 MIS 0`
//        esattamente come Metal Slug, che dalla SD non legge affatto. Due
//        giochi diversi con lo stesso numero non sono una misura.
//     AUD = blocchi audio consegnati all'I2S al secondo, **atteso 59**
//           (32000 Hz / 541 campioni). Se e' molto sotto, il task audio non sta
//           in pari ed e' QUELLA la diagnosi, non gli fps
//           ([[feedback_task_audio_indietro_watchdog]]).
//
// ⛔⛔ IL CONTO DEVE CHIUDERE, e con NG_VIDEO_HALF_RATE **non e' la somma
// secca**. CPU e' per GIRO (ogni quadro emulato); REN e BLT sono per quadro
// DISEGNATO, che con l'half rate acceso e' uno su due. Quindi:
//
//     NG_VIDEO_HALF_RATE 1  ->  CPU + (REN + BLT)/2  ~=  CAR/100 x 16,9 ms
//     NG_VIDEO_HALF_RATE 0  ->  CPU +  REN + BLT     ~=  CAR/100 x 16,9 ms
//
// ⭐ REN e BLT sono tenuti per quadro DISEGNATO apposta: e' il costo di UN
// rendering, cioe' il numero che serve per decidere se una cura sul renderer
// vale la pena. Ammortizzarlo sui giri lo dimezzerebbe e farebbe sembrare il
// video meta' del problema di quello che e'.
// ⚠️ SD dev'essere <= REN (la lettura dalla SD avviene DENTRO il rendering,
// quando un tile chiede un banco non residente).
// Se il bilancio non torna, il numero che si sta guardando puo' essere lo
// STRUMENTO e non il paziente — e' successo tre volte
// ([[feedback_ottimizzare_contando_accessi]] caso 7): prima di credere a
// qualunque cosa, si verifica che il conto quadri.
// ⚠️ Il disegno delle tre righe cade DENTRO la voce REN (ng_draw_misura() e'
// chiamata fra ng_render() e la lettura di m2): sono ~5.000 pixel, molto meno
// dei ~240.000 del quadro, ma e' scritto qui perche' un termometro dev'essere
// dichiarato, non scoperto ([[feedback_no_costly_calls_in_render]]).
// ⚠️ Qui video ed emulazione stanno sullo STESSO task (`ng_task` fa run_frame,
// render e blit di fila), quindi le tre voci si sommano davvero. Non e' il caso
// dei giochi normali, dove l'emulazione sta sull'altro core e va misurata a
// parte (vedi prof.h). L'unico ramo parallelo qui e' l'AUDIO, ed e' AUD.
// ⚠️ Niente `%f` nelle stringhe: su questa build la stringa esce VUOTA e
// l'overlay non compare affatto ([[esp32_arduino_no_float_printf]]). I decimi
// si fanno a mano con `%lu.%lu`.
// ⚠️ Righe di lunghezza FISSA: se una si accorcia, i caratteri della precedente
// restano a schermo (il testo si sovrascrive, non si cancella — a cancellarlo
// e' il backdrop del quadro dopo).
// ============================================================================
// quello che si LEGGE a schermo (aggiornato una volta al secondo)
static uint32_t ng_r_fps = 0, ng_r_car = 0, ng_r_pic = 0, ng_r_ems = 0;
static uint32_t ng_r_cpu = 0, ng_r_ren = 0, ng_r_blt = 0;
static uint32_t ng_r_sd  = 0, ng_r_mis = 0, ng_r_aud = 0;
#if NG_BLIT_CORE0
// ⭐ Con il blit sul core 0, BLT non e' piu' tempo del core 1. Le due voci che
// servono al suo posto:
//   B0  = durata media di un blit SUL CORE 0 (conversione + DMA): deve stare
//         sotto 16,9, altrimenti e' lui a dettare il passo;
//   ATT = quanto il core 1 ASPETTA il core 0 per giro: ~0 = il core 0 tiene il
//         passo; alto = il collo e' tornato il video.
static uint32_t ng_r_b0 = 0, ng_r_att = 0, ng_acc_att = 0;
static volatile uint32_t ng_b0_us = 0, ng_b0_n = 0;   // li scrive il core 0
#endif

// quello che si ACCUMULA giro per giro (azzerato a ogni finestra)
static uint32_t ng_acc_cpu = 0, ng_acc_ren = 0, ng_acc_blt = 0;
static uint32_t ng_acc_lav = 0, ng_acc_pic = 0;
static uint32_t ng_acc_giri = 0;     // giri del loop = quadri EMULATI
static uint32_t ng_acc_vis  = 0;     // quadri DISEGNATI (meta', con half rate)

// La cache degli sprite da SD: c'e' solo sui set che ce l'hanno (Metal Slug 2
// si', Metal Slug no). Con la cache spenta questi due valgono 0 e le voci SD e
// MIS restano a zero da sole, senza un caso particolare da scrivere.
static inline uint32_t ng_sd_miss(void)
{
    return spr_cache_on ? spr_cache.miss : 0;
}
static inline uint32_t ng_sd_us(void)
{
#if GFX_CACHE_MISURA
    return spr_cache_on ? spr_cache.us_miss : 0;
#else
    return 0;   // ⚠️ se GFX_CACHE_MISURA e' 0 la voce SD legge sempre 0.0:
#endif          //    i due interruttori vanno accesi INSIEME.
}

// il task AUDIO (core 0) incrementa, questo task legge e non azzera mai:
// la differenza fra due istanti non ha bisogno di lucchetti.
// ⚠️ `volatile` perche' lo scrive un ALTRO core ([[spinnerino_volume_multicore_race]]);
// `static` perche' non deve uscire da questa unita' di compilazione.
static volatile uint32_t ng_aud_blocchi = 0;

// decimi di ms senza `%f`
static inline uint32_t ng_ms_i(uint32_t us) { return us / 1000; }
static inline uint32_t ng_ms_d(uint32_t us) { return (us % 1000) / 100; }

static void ng_draw_misura(void)
{
    char s[40];
    // ⭐ EMS = quadri EMULATI al secondo (i giri del loop). Senza questo numero
    // il bilancio non e' verificabile da chi legge: CPU e' per giro, REN e BLT
    // per quadro disegnato, e senza sapere QUANTI sono i giri non si puo' dire
    // se CAR e' plausibile. Con NG_VIDEO_HALF_RATE dev'essere **EMS = 2 x FPS**:
    // se non lo e', il primo sospetto e' lo strumento, non il gioco.
    // ⭐ 2026-10-08: SCALA 2 -> massimo 19 caratteri per riga, quindi le stesse
    // nove voci stanno su QUATTRO righe invece di tre (passo 18 px). Le voci e
    // il loro significato non cambiano: cambia solo come vanno a capo.
    //   1 FPS CAR PIC     il verdetto (giallo)
    //   2 CPU REN         3 BLT EMS AUD         4 SD MIS
    const int MY = 8 * NG_MIS_SC + 2;
    snprintf(s, sizeof s, "FPS%2lu CAR%3lu PIC%2lu",
             (unsigned long)ng_r_fps, (unsigned long)ng_r_car,
             (unsigned long)ng_ms_i(ng_r_pic));
    // ⚠️ GIALLO **BYTE-SWAPPATO**: `fb` e' riempito da `pal_lut`, che in questo
    // porting restituisce RGB565 gia' scambiato per il display SPI
    // (ADATTAMENTO 2 di 6). 0xFFE0 -> 0xE0FF; il bianco 0xFFFF e' simmetrico e
    // non se ne accorge — ed e' proprio per questo che un colore sbagliato qui
    // non darebbe nessun errore, solo una tinta strana.
    ng_fb_text(NG_VX + 2, NG_VY + 2, s, 0xE0FF);          // giallo: il verdetto
    snprintf(s, sizeof s, "CPU%2lu.%lu REN%2lu.%lu",
             (unsigned long)ng_ms_i(ng_r_cpu), (unsigned long)ng_ms_d(ng_r_cpu),
             (unsigned long)ng_ms_i(ng_r_ren), (unsigned long)ng_ms_d(ng_r_ren));
    ng_fb_text(NG_VX + 2, NG_VY + 2 + MY, s, 0xFFFF);
#if NG_BLIT_CORE0
    // riga 3 = il core 0: durata del blit (B0) e attesa del core 1 (ATT)
    snprintf(s, sizeof s, "B0 %2lu.%lu ATT%2lu.%lu",
             (unsigned long)ng_ms_i(ng_r_b0), (unsigned long)ng_ms_d(ng_r_b0),
             (unsigned long)ng_ms_i(ng_r_att), (unsigned long)ng_ms_d(ng_r_att));
    ng_fb_text(NG_VX + 2, NG_VY + 2 + 2 * MY, s, 0xFFFF);
    snprintf(s, sizeof s, "EMS%2lu AUD%2lu SD%4lu",
             (unsigned long)ng_r_ems, (unsigned long)ng_r_aud,
             (unsigned long)ng_r_sd);
    ng_fb_text(NG_VX + 2, NG_VY + 2 + 3 * MY, s, 0xFFFF);
#else
    snprintf(s, sizeof s, "BLT%2lu.%lu EMS%2lu AUD%2lu",
             (unsigned long)ng_ms_i(ng_r_blt), (unsigned long)ng_ms_d(ng_r_blt),
             (unsigned long)ng_r_ems, (unsigned long)ng_r_aud);
    ng_fb_text(NG_VX + 2, NG_VY + 2 + 2 * MY, s, 0xFFFF);
    // SD in ms AL SECONDO (su 1000), MIS in miss AL SECONDO: vedi il commento
    // sul calcolo. Per giro erano sotto l'unita' e mostravano sempre 0.
    snprintf(s, sizeof s, "SD%4lu MIS%5lu",
             (unsigned long)ng_r_sd, (unsigned long)ng_r_mis);
    ng_fb_text(NG_VX + 2, NG_VY + 2 + 3 * MY, s, 0xFFFF);
#endif
}
#endif

static void ng_draw_vol_bar(void)
{
    static int  last_fill = -2;          // -2 = mai disegnata, -1 = banda nera
    const bool  on = input_is_vol_mode_active();
    const int   f  = on ? laserdisc_current_volume() : -1;
    if (f == last_fill) return;          // niente e' cambiato: nessuno SPI
    last_fill = f;
    videoVolBarTop(g_strip, on, on ? f : 30);
    ng_window(1);                        // ridai la finestra STRETTA al gioco
}

// ============================================================ finestra + blit
// ⭐ ADATTAMENTO 1 di 6 — VIDEO. Al posto di ng_scale()/ng_flush() (PPA + DSI)
// c'e' ng_blit(), copiato da or_blit() di outrun.cpp: stesso formato di
// partenza (320x224), stessa riduzione 3-su-4, stessa finestra stretta.
//
// 2 = tutta la finestra del player (320x240): pagine di stato e bande nere.
// 1 = SOLO le 224 colonne che portano gioco, saltando le bande.
// 0 = si restituisce la finestra al menu.
static void ng_window(int quale) {
    video.waitDMA();
    if (quale == 2)      video.setAddrWindow(TFT_HW_X, 0, TFT_FB_W, LD_FB_H);
    else if (quale == 1) video.setAddrWindow(TFT_HW_X + NG_X_OFF, 0, NG_VH, LD_FB_H);
    else                 video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// Dipinge di nero TUTTA la finestra del player. Una volta sola, prima di
// stringere la finestra sul gioco: da li' in poi le bande restano com'erano.
static void ng_bande_nere(void) {
    ng_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    video.waitDMA();
}

static void ng_blit(void)
{
#if NG_BLIT_PPA
    // ========================================================================
    // ⭐⭐⭐ SCALA E ROTAZIONE AL PPA, in DMA. La CPU non tocca piu' un pixel.
    //
    // Quello che il ciclo software qui sotto fa a mano — 320->240 tenendo 3
    // colonne su 4, piu' la trasposizione per il pannello in portrait — e'
    // esattamente **una scala piu' una rotazione di 90 gradi**, cioe' il mestiere
    // del PPA. Costava ~3,3 ms di CPU per quadro, e per giunta leggendo `fb`
    // dalla PSRAM con passo 704 byte (una linea di cache caricata per usarne 22).
    //
    // LA GEOMETRIA, verificata OFFLINE prima di scriverla (PY\mk_ngppa.py, sul
    // frame vero dell'harness): contro l'immagine del blit software,
    // **ANGLE_90 = 6,6% di pixel diversi · ANGLE_270 = 46,5%**. Il 6,6% non e'
    // un difetto: e' quale colonna dei gruppi di 4 viene scartata (il software
    // tiene {0,1,3}, il PPA usa il suo accumulatore). Il verso e' quello giusto,
    // e **si e' guardato invece di dedurlo** ([[ppa_srm_ordine_operazioni]]).
    //
    // ⚠️ LE SCALE VANNO INCROCIATE, perche' il PPA scala PRIMA di ruotare e con
    // 90/270 gli assi d'uscita sono scambiati (TRM 35.5.3.1):
    //     scale_x = ALTEZZA uscita / LARGHEZZA gioco = 240/320 = 0,75
    //     scale_y = LARGHEZZA uscita / ALTEZZA gioco = 224/224 = 1,0
    // 0,75 e' 12/16: cade esatto sul passo di 1/16 del PPA, niente troncamento.
    // ⚠️ `ppa_srm_oper_config_t op = {}` AZZERATA: un `rotation_angle` spazzatura
    // fa abort() dentro il driver ([[esp32p4_ppa_srm_vincoli]] punto 2).
    // ⚠️ I due msync vanno in versi OPPOSTI e scambiarli cancella il quadro
    // ([[esp32p4_cache_msync_ppa_gamebuf]]): C2M su `fb` perche' l'ha scritto la
    // CPU e lo legge il DMA; M2C su `ng_ppa_out` perche' l'ha scritto il DMA e
    // lo rilegge la CPU dentro video.write().
    // ⚠️ Il byte-swap della palette sopravvive perche' la scala e' NEAREST: i
    // pixel vengono copiati, non mescolati. Se un giorno il PPA interpolasse,
    // i colori uscirebbero sbagliati — e' il primo posto dove guardare.
    // ========================================================================
    if (ppa_ok && ng_ppa_out) {
        esp_cache_msync(fb, fb_sz, ESP_CACHE_MSYNC_FLAG_DIR_C2M);

        ppa_srm_oper_config_t op = {};
        op.in.buffer            = fb;
        op.in.pic_w             = NG_W;     op.in.pic_h  = NG_H;
        op.in.block_w           = NG_VW;    op.in.block_h = NG_VH;
        op.in.block_offset_x    = NG_VX;    op.in.block_offset_y = NG_VY;
        op.in.srm_cm            = PPA_SRM_COLOR_MODE_RGB565;
        op.out.buffer           = ng_ppa_out;
        op.out.buffer_size      = NG_PPA_OUT_SZ;
        op.out.pic_w            = NG_VH;    op.out.pic_h = LD_FB_H;   // 224 x 240
        op.out.block_offset_x   = 0;        op.out.block_offset_y = 0;
        op.out.srm_cm           = PPA_SRM_COLOR_MODE_RGB565;
        op.rotation_angle       = PPA_SRM_ROTATION_ANGLE_90;   // antiorario
        op.scale_x              = (float)LD_FB_H / (float)NG_VW;   // 0,75
        op.scale_y              = (float)NG_VH   / (float)NG_VH;   // 1,0
        op.mirror_x             = false;
        op.mirror_y             = false;
        op.mode                 = PPA_TRANS_MODE_BLOCKING;

        if (ppa_do_scale_rotate_mirror(ppa, &op) == ESP_OK) {
            esp_cache_msync(ng_ppa_out, NG_PPA_OUT_SZ,
                            ESP_CACHE_MSYNC_FLAG_DIR_M2C);
            // Le strisce escono gia' nell'ordine giusto: il PPA scrive per
            // righe 224 pixel, e la finestra SPI e' larga esattamente 224.
            for (int s = 0; s < LD_FB_H / 8; s++) {
                video.write(ng_ppa_out + (size_t)s * NG_VH * 8, NG_VH * 8);
#if NG_BLIT_OVERLAP
                ng_emula_fette((int)((int64_t)NG_LINES * (s + 1) / (LD_FB_H / 8)));
#endif
            }
            video.waitDMA();
            return;
        }
        // ⛔ Fallita: si scende al blit software e non si riprova a ogni quadro
        // (stamperebbe un errore 48 volte al secondo e resterebbe lento uguale).
        ppa_ok = false;
    }
#endif // NG_BLIT_PPA

    // indici tenuti dentro ogni gruppo di 4 pixel sorgente (round(k*4/3))
    static const uint8_t KEEP[3] = { 0, 1, 3 };

    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        // ⭐ NIENTE memset: con la finestra STRETTA (224 colonne) il ciclo copre
        // TUTTE le 224*8 posizioni della striscia. Le bande nere stanno FUORI
        // dalla finestra e le ha gia' dipinte ng_bande_nere().
        int gx8[8];
        for (int i = 0; i < 8; i++) {
            const int x_p = (LD_FB_H - 1) - (sy + i);   // 0..239 = colonna UTENTE
            gx8[i] = (x_p / 3) * 4 + KEEP[x_p % 3];     // 0..319 = colonna visibile
        }

        // ⚠️ Lo schermo del NeoGeo NON parte dall'origine del framebuffer: sta a
        // (NG_VX, NG_VY) dentro il quadro 352x256 di gngeo. Il +16 e' il margine
        // che serve agli sprite che entrano da sinistra.
#if NG_BLIT_CORE0
        const uint16_t *src = ng_fb_out + (size_t)NG_VY * NG_W + NG_VX;
#else
        const uint16_t *src = fb + (size_t)NG_VY * NG_W + NG_VX;
#endif
        uint16_t *dst = g_strip;   // finestra STRETTA: la striscia parte dal gioco
        for (int gy = 0; gy < NG_VH; gy++) {
            dst[0]           = src[gx8[0]];
            dst[1 * NG_VH]   = src[gx8[1]];
            dst[2 * NG_VH]   = src[gx8[2]];
            dst[3 * NG_VH]   = src[gx8[3]];
            dst[4 * NG_VH]   = src[gx8[4]];
            dst[5 * NG_VH]   = src[gx8[5]];
            dst[6 * NG_VH]   = src[gx8[6]];
            dst[7 * NG_VH]   = src[gx8[7]];
            src += NG_W;                                // riga successiva di fb
            dst += 1;                                   // colonna successiva
        }
        video.write(g_strip, NG_VH * 8);   // finestra STRETTA: 224, non 320
#if NG_BLIT_OVERLAP
        // ⭐ Il DMA della striscia appena accodata sta uscendo: invece di
        // aspettarlo dentro la write successiva, si emula. Le fette sono
        // distribuite in proporzione alle strisce, cosi' a fine blit il quadro
        // dopo e' emulato: 264 x (s+1) / 30, che in fondo fa esattamente 264.
        ng_emula_fette((int)((int64_t)NG_LINES * (sy / 8 + 1) / (LD_FB_H / 8)));
#endif
    }
    // video.write() e' ASINCRONA e lascia una transazione SPI in volo: si drena
    // qui, UNA volta per frame.
    video.waitDMA();
}

#if NG_BLIT_CORE0
// ============================================================================
// ⭐⭐⭐ IL TASK VIDEO SUL CORE 0 — fa il mestiere che sul CABINATONE fa il PPA.
// Riceve un quadro gia' disegnato (`ng_fb_out`), lo converte e lo spedisce con
// lo STESSO ng_blit() di prima, poi la barra del volume, poi restituisce il
// gettone `ng_blit_libero`.
// ⛔⛔ UN SOLO PROPRIETARIO DELL'SPI ([[feedback_bus_condiviso_un_solo_proprietario]]):
// durante la partita parla col pannello SOLO questo task — blit E barra del
// volume (che usa anche `g_strip`). Il core 1 non tocca piu' l'SPI finche' il
// task non e' chiuso (ng_blit_ferma), e solo dopo ridisegna le bande d'uscita.
// ⚠️ Priorita' 4, sotto l'audio (5) sullo stesso core: la conversione puo'
// essere interrotta dal Z80/YM2610, mai il contrario. E quasi tutto il suo
// tempo e' attesa del DMA (spi_device_get_trans_result BLOCCA, non gira a
// vuoto), quindi l'idle del core 0 viene servito.
// ============================================================================
static SemaphoreHandle_t   ng_blit_libero  = nullptr;   // dato = core 0 libero
static TaskHandle_t        ng_blit_task_h  = nullptr;
static volatile bool       ng_blit_esci    = false;
static volatile bool       ng_blit_finito  = false;

static void ng_blit_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (ng_blit_esci) break;
#if NG_MISURA
        const int64_t b0 = esp_timer_get_time();
#endif
        ng_blit();               // 320x224 -> 240, finestra stretta
        ng_draw_vol_bar();       // dopo il blit: sta nella banda, non nel gioco
#if NG_MISURA
        ng_b0_us += (uint32_t)(esp_timer_get_time() - b0);
        ng_b0_n++;
#endif
        xSemaphoreGive(ng_blit_libero);
    }
    ng_blit_finito = true;
    vTaskDelete(nullptr);
}

// Avvio: dopo le bande nere e la finestra stretta, prima del ciclo.
// Il SECONDO framebuffer si prende in PSRAM; se non c'e', si va con uno solo.
// ⚠️ Si chiama DOPO l'avvio delle cache da SD: cosi' la cache degli sprite si
// e' gia' presa i suoi 16 MB e questo non la fa scendere a 12 (gfx_cache cala
// del 25% se non trova spazio, e meno cache = piu' letture dalla SD).
static bool ng_blit_avvia(char *err, size_t errlen)
{
    fb_due[0] = fb;
    fb_due[1] = (uint16_t *)heap_caps_aligned_alloc(128, fb_sz, MALLOC_CAP_SPIRAM);
    if (fb_due[1]) memset(fb_due[1], 0, fb_sz);
    ng_fb_out = fb;
    ng_blit_esci = false;
    ng_blit_finito = false;
    ng_blit_libero = xSemaphoreCreateBinary();
    if (!ng_blit_libero) {
        snprintf(err, errlen, "SEMAFORO VIDEO KO");
        return false;
    }
    xSemaphoreGive(ng_blit_libero);              // all'inizio il core 0 e' libero
    if (xTaskCreatePinnedToCore(ng_blit_task, "ng_video", 8192, nullptr, 4,
                                &ng_blit_task_h, 0) != pdPASS) {
        ng_blit_task_h = nullptr;
        snprintf(err, errlen, "TASK VIDEO KO  IRAM %uK",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        return false;
    }
    return true;
}

// Prima di DISEGNARE in `fb`: con un framebuffer solo, il core 0 potrebbe
// starlo ancora leggendo -> si aspetta che finisca. Con due no: `fb` e' quello
// che il core 0 ha gia' finito di spedire prima di ricevere l'altro.
static inline void ng_blit_prima_di_disegnare(void)
{
    if (!fb_due[1]) {
        xSemaphoreTake(ng_blit_libero, portMAX_DELAY);
        xSemaphoreGive(ng_blit_libero);
    }
}

// Dopo aver disegnato: si aspetta che il core 0 abbia finito il quadro PRIMA
// (e' il punto in cui un core 0 troppo lento si farebbe sentire: voce ATT),
// gli si passa questo e si scambiano i framebuffer.
static inline void ng_blit_consegna(void)
{
    xSemaphoreTake(ng_blit_libero, portMAX_DELAY);
    ng_fb_out = fb;
    xTaskNotifyGive(ng_blit_task_h);
    if (fb_due[1]) fb = (fb == fb_due[0]) ? fb_due[1] : fb_due[0];
}

// Chiusura: si aspetta l'ultimo quadro, si ferma il task e si rimette `fb`
// sull'allocazione ORIGINALE, che e' quella che ng_cleanup() libera.
// ⛔ Va chiamata su OGNI strada d'uscita del ciclo prima di toccare l'SPI
// ([[feedback_init_una_volta_sola_vs_rientro]]: quante sono le uscite?). Qui
// sono tutte un `break` dello stesso ciclo, quindi un punto solo.
static void ng_blit_ferma(void)
{
    if (ng_blit_task_h) {
        xSemaphoreTake(ng_blit_libero, portMAX_DELAY);
        ng_blit_esci = true;
        xTaskNotifyGive(ng_blit_task_h);
        while (!ng_blit_finito) vTaskDelay(1);
        ng_blit_task_h = nullptr;
    }
    if (ng_blit_libero) { vSemaphoreDelete(ng_blit_libero); ng_blit_libero = nullptr; }
    if (fb_due[0]) fb = fb_due[0];
    if (fb_due[1]) { heap_caps_free(fb_due[1]); }
    fb_due[0] = fb_due[1] = nullptr;
    ng_fb_out = nullptr;
}
#endif // NG_BLIT_CORE0

#if 0   // ⛔ ng_scale()/ng_flush() di CABINATONE: PPA + pannello DSI 1024x600.
        // Tenute qui SPENTE e non cancellate perche' sono il riferimento del
        // porting: se un domani questo gioco tornasse su una scheda con PPA,
        // la geometria giusta e' questa, non va ricostruita.
static void ng_scale(void)
{
#if NG_HAS_PPA
    if (ppa_ok) {
        // il PPA legge fb via DMA: prima va scritta indietro la cache (l'ha
        // riempito la CPU). ⚠️ mai C2M su un buffer riempito dal PPA.
        esp_cache_msync(fb, fb_sz, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
        ppa_srm_oper_config_t op = {};       // ⚠️ azzerarla: un rotation_angle
                                             // spazzatura fa abort() nel driver
        op.in.buffer          = fb;
        op.in.pic_w           = NG_W;   op.in.pic_h   = NG_H;
        // ⭐ il ritaglio dello schermo dentro il quadro 352x256 lo fa il PPA
        // stesso con block_offset: nessuna copia intermedia
        op.in.block_w         = NG_VW;  op.in.block_h = NG_VH;
        op.in.block_offset_x  = NG_VX;  op.in.block_offset_y = NG_VY;
        op.in.srm_cm          = PPA_SRM_COLOR_MODE_RGB565;
        // uscita DIRETTA nel framebuffer del pannello: il gioco viene scritto
        // gia' al suo posto e si salta del tutto la copia intermedia
        const bool direct = (panel_fb != nullptr);
        op.out.buffer         = direct ? panel_fb : gamebuf;
        op.out.buffer_size    = direct ? (size_t)LCD_H_RES * LCD_V_RES * 2 : gb_sz;
        op.out.pic_w          = direct ? LCD_H_RES : GW;
        op.out.pic_h          = direct ? LCD_V_RES : GH;
        op.out.block_offset_x = direct ? NG_OX : 0;
        op.out.block_offset_y = direct ? NG_OY : 0;
        op.out.srm_cm         = PPA_SRM_COLOR_MODE_RGB565;
        op.rotation_angle     = PPA_SRM_ROTATION_ANGLE_0;
        op.scale_x            = (float)GW / (float)NG_VW;   // 2.4375 = 39/16
        op.scale_y            = (float)GH / (float)NG_VH;   // 2.625  = 42/16
        op.mode               = PPA_TRANS_MODE_BLOCKING;
        if (ppa_do_scale_rotate_mirror(ppa, &op) == ESP_OK) {
            ppa_direct = direct;
            ng_sw_scaled = false;          // a scrivere e' stato il DMA del PPA
            return;
        }
        ppa_ok = false;                    // fallito: da qui in poi software
    }
#endif
    // ripiego software: nearest-neighbour a virgola fissa, sulla stessa finestra
    ppa_direct = false;
    if (!gamebuf) return;
    const uint32_t xstep = ((uint32_t)NG_VW << 16) / GW;
    const uint32_t ystep = ((uint32_t)NG_VH << 16) / GH;
    const uint16_t *base = fb + (size_t)NG_VY * NG_W + NG_VX;
    for (int y = 0; y < GH; y++) {
        const uint16_t *src = base + (size_t)(((uint32_t)y * ystep) >> 16) * NG_W;
        uint16_t *dst = gamebuf + (size_t)y * GW;
        uint32_t ux = 0;
        for (int x = 0; x < GW; x++) { dst[x] = src[ux >> 16]; ux += xstep; }
    }
    ng_sw_scaled = true;
}

static void ng_flush(void)
{
    if (ppa_direct) return;                // il PPA ha gia' scritto nel pannello
    if (!gamebuf) return;
    // ⚠️ La direzione del sync dipende da CHI ha riempito gamebuf. Fare C2M su un
    // buffer appena scritto dal PPA (che scrive in DMA) riversa la cache STANTIA
    // sopra i dati freschi e cancella il frame: e' il bug che e' costato 4 flash
    // su Out Run (fermo-immagine con emulazione perfetta).
    esp_cache_msync(gamebuf, gb_sz, ng_sw_scaled ? ESP_CACHE_MSYNC_FLAG_DIR_C2M
                                                 : ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    lcd.lcd_draw_bitmap(NG_OX, NG_OY, NG_OX + GW, NG_OY + GH, gamebuf);
}
#endif  // ⛔ fine del blocco PPA/DSI di CABINATONE

// ============================================================ testo / errori
// ⭐ ADATTAMENTO 4 di 6 — TESTO. Su CABINATONE si componeva un rettangolo e lo
// si blittava con `lcd.lcd_draw_bitmap`. Qui il pannello si scrive a STRISCE
// RUOTATE, quindi si usa lo schema di outrun.cpp/cps1944.cpp: FONT8 (lo stesso
// dei menu e dei player laserdisc) disegnato dentro la striscia trasposta.
static void ng_line(int sy, const char *s, int y_p, int scale, uint16_t col)
{
    const int slen = (int)strlen(s);
    int x0 = (240 - slen * 8 * scale) / 2; if (x0 < 2) x0 = 2;
    for (int i = 0; i < slen; i++) {
        const int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            const unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++) {
                if (!(row & (0x80 >> c))) continue;
                for (int dr = 0; dr < scale; dr++) {
                    const int x_l = y_p + r * scale + dr;
                    if (x_l < 0 || x_l >= TFT_FB_W) continue;
                    for (int dc = 0; dc < scale; dc++) {
                        const int y_l = 239 - (x0 + (i * 8 + c) * scale + dc);
                        const int y_in = y_l - sy;
                        if (y_in < 0 || y_in >= 8) continue;
                        g_strip[y_in * TFT_FB_W + x_l] = col;
                    }
                }
            }
        }
    }
}

static const uint16_t C_YELLOW = 0xE0FF;   // byte-swapped, come gli altri player
static const uint16_t C_CYAN   = 0xFF07;
static const uint16_t C_GREY   = 0xEF7B;
static const uint16_t C_RED    = 0x00F8;

// pagina a 5 righe sotto il titolo (identica a quella di Out Run e 1944)
// ⛔ Il titolo viene dalla TABELLA, non e' piu' scritto qui dentro: con due
// giochi nel menu, "METAL SLUG" fisso voleva dire caricare Metal Slug 2 con il
// nome del primo. ⭐ Il corpo raddoppiato entra fino a 14 caratteri (14x16 = 224
// px sui 240 del pannello): oltre quella misura si scende a corpo 1 invece di
// far uscire le lettere dallo schermo — cosi' vale per TUTTI i titoli della
// tabella, non solo per i due esposti oggi.
static void ng_page(const char *l[5], const uint16_t c[5])
{
    static const int Y[5] = { 150, 172, 194, 216, 250 };
    const char *tit = (G && G->titolo) ? G->titolo : "NEO GEO";
    const int   sc  = (strlen(tit) <= 14) ? 2 : 1;
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        ng_line(sy, tit, 96, sc, C_YELLOW);
        for (int k = 0; k < 5; k++)
            if (l[k] && l[k][0]) ng_line(sy, l[k], Y[k], 1, c[k]);
        video.write(g_strip, TFT_FB_W * 8);
    }
    // dopo una pagina di stato si legge quasi sempre dalla SD: video.write
    // lascia una transazione SPI in volo e va drenata prima di toccare la SD.
    video.waitDMA();
}

static void ng_msg(const char *a, const char *b, uint16_t col)
{
    const char *l[5] = { (G && G->anno) ? G->anno : "", a, b, "", "START 3s ESCI" };
    const uint16_t c[5] = { C_GREY, col, C_GREY, C_GREY, C_GREY };
    ng_page(l, c);
}

// Schermata di CARICAMENTO: titolo, anno e una riga — convenzione del progetto.
// ⚠️ Le schermate di ERRORE restano dettagliate: senza Serial sono l'unico modo
// per sapere cosa manca sulla SD.
// ⛔ La riga "IRAM nnnK PSRAM nnM" e' stata TOLTA su richiesta dell'utente
// (2026-08-11). Era servita durante il bring-up di Metal Slug, quando la RAM
// interna libera era il numero che decideva tutto (stack del task audio,
// `tl_tab`, `pal_lut`); adesso quelle scelte sono chiuse e verificate, e una
// schermata di caricamento non e' il posto dove tenere una misura per sempre.
// Se un domani servisse di nuovo, il misuratore vero e completo e' `NG_MISURA`.
static void ng_caricamento(void)
{
    const char *l[5] = { (G && G->anno) ? G->anno : "", "", "CARICAMENTO DA SD", "", "" };
    const uint16_t c[5] = { C_GREY, C_GREY, C_CYAN, C_GREY, C_GREY };
    ng_page(l, c);
}

static bool ng_want_exit(uint8_t b) {
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & B_START) {
        const uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}

// ⭐⭐ ADATTAMENTO 6 di 6 — USCITA. Su CABINATONE `ng_fatal()` finiva con
// `esp_restart()`, perche' li' USCIRE DA UN GIOCO RIAVVIA SEMPRE LA SCHEDA.
// Qui il firmware TORNA AL MENU e deve poter RIENTRARE nel gioco: quindi si
// libera tutto e si esce dal task in modo pulito.
// ⛔ Non e' un dettaglio di stile. E' esattamente il passaggio «da un guscio che
// RIPARTE a uno che RIENTRA» che su 1944/Ghouls/Out Run ha nascosto per giorni
// il difetto di `m68k_init()`: con esp_restart nessuno si accorge se qualcuno
// non ripulisce lo stato. La domanda da farsi per OGNI risorsa e' «chi la
// libera, e chi la ricostruisce la prossima volta?»
// ([[feedback_init_una_volta_sola_vs_rientro]]).
static volatile bool g_stop = false;
static void ng_cleanup(void);            // definita in fondo, accanto al task

static void ng_fatal(const char *msg)
{
    // Resta sull'errore finche' l'utente non esce: senza Serial questa
    // schermata e' l'unico modo per sapere COSA manca sulla SD.
    while (!g_stop) {
        ng_msg(msg, "ROMCONV + COPIA SU SD", C_RED);
        if (ng_want_exit(ng_buttons())) break;
        vTaskDelay(pdMS_TO_TICKS(60));
    }
    ng_cleanup();
    ng_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    ng_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ============================================================ caricamento SD
static uint32_t ng_crc32(const uint8_t *p, size_t n)
{
    // ⚠️ LOCALE, non statica: 1 KB di RAM interna occupato per sempre in un
    // binario con 9 emulatori, per una tabella che serve solo al caricamento.
    uint32_t tab[256];
    bool init = false;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            tab[i] = c;
        }
        init = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    // ⛔ RESPIRO: su Metal Slug qui passano 26 MB. Un ciclo di CPU pura cosi'
    // lungo non cede mai il processore e il TASK WATCHDOG riavvia la scheda —
    // e' il difetto gia' pagato su Out Run, dove il CRC di sprites.bin erano
    // otto milioni di giri ([[feedback_task_audio_indietro_watchdog]]).
    for (size_t i = 0; i < n; i++) {
        c = tab[(c ^ p[i]) & 0xff] ^ (c >> 8);
        if ((i & 0xFFFF) == 0xFFFF) vTaskDelay(1);      // un tick ogni 64 KB
    }
    return c ^ 0xFFFFFFFFu;
}

// ---- ADATTAMENTO 5 di 6 — SD ----------------------------------------------
// CABINATONE ha `sd_rom.h` (sd_mount_ready / sd_read_exact); qui c'e'
// `sd_card.h` + stdio su FatFs. Due funzioni di compatibilita', cosi' `ng_load()`
// resta IDENTICA a quella di CABINATONE e non va riletta riga per riga.
// ⭐ Ed e' anche il posto giusto per il RESPIRO in lettura: 26 MB da SD sono
// decine di secondi senza mai bloccarsi.
static bool sd_mount_ready(void) { return sd_is_mounted() ? true : sd_mount(); }

static size_t sd_read_exact(const char *path, uint8_t *dst, size_t len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    const size_t CHUNK = 32768;
    size_t letti = 0;
    int    pezzi = 0;
    while (letti < len) {
        size_t q = len - letti; if (q > CHUNK) q = CHUNK;
        const size_t got = fread(dst + letti, 1, q, f);
        if (got != q) { fclose(f); return letti + got; }
        letti += q;
        if ((++pezzi & 7) == 0) vTaskDelay(1);          // un tick ogni 256 KB
    }
    fclose(f);
    return letti;
}

// CRC32 stampati da romconv_neogeo.py: una copia troncata sulla SD deve dare un
// errore leggibile, non uno schermo nero. ⚠️ sd_read_exact() PRETENDE che il
// file sia lungo esattamente `len` (sd_rom.cpp): non si puo' leggerne un pezzo.
struct NgFile { const char *path; size_t len; uint32_t crc; uint8_t **dst; const char *what; };

// I percorsi dipendono dal gioco scelto nel menu, quindi la tabella si compone
// a runtime invece di essere una costante. I CRC stanno in NG_GAMES.
#define NG_MAXFILES 8
static char ng_paths[NG_MAXFILES][48];

static bool ng_load(char *err, size_t errlen)
{
    if (!sd_mount_ready()) { snprintf(err, errlen, "SD NON MONTATA"); return false; }

    NgFile NG_FILES[NG_MAXFILES];
    int n = 0;
    #define NGF(fmt, arg, len_, crc_, dst_, what_)                              \
        do { snprintf(ng_paths[n], sizeof(ng_paths[0]), fmt, arg);              \
             NG_FILES[n].path = ng_paths[n]; NG_FILES[n].len = (len_);          \
             NG_FILES[n].crc = (crc_); NG_FILES[n].dst = (dst_);                \
             NG_FILES[n].what = (what_); n++; } while (0)

    NGF("%s", "/sdcard/neogeo/bios/sp.bin",   BIOS_SZ,   0x9036D879, &bios,     "BIOS");
    NGF("%s", "/sdcard/neogeo/bios/sfix.bin", FIXBIOS_SZ, 0x354029FC, &fix_bios, "BIOS fix layer");
    NGF("/sdcard/neogeo/%s/p.bin",    G->dir, PROM_SZ,   G->crc_p, &prom,       "programma 68000");
    NGF("/sdcard/neogeo/%s/s.bin",    G->dir, FIXROM_SZ, G->crc_s, &fix_game,   "fix layer");
    // ⭐ la .ctile si CARICA solo se il gioco entra in PSRAM; altrimenti resta
    // sulla SD e la legge gfx_cache a banchi (il suo CRC lo ha gia' verificato
    // il converter, e 32 MB di CRC all'avvio sarebbero secondi di attesa).
    if (!G->spr_cache)
        NGF("/sdcard/neogeo/%s/c.ctile", G->dir, CTILE_SZ, G->crc_c, &gfx,      "sprite");
    NGF("/sdcard/neogeo/%s/c.cusage", G->dir, CUSAGE_SZ, G->crc_u, &cusage_raw, "mappa tile vuoti");
#if NG_LOAD_AUDIO_ROMS
    NGF("/sdcard/neogeo/%s/m.bin",    G->dir, MROM_SZ,   G->crc_m, &mrom,       "programma Z80");
    // ⭐ come la .ctile: i 16 MB di campioni di Metal Slug 3 restano sulla SD e
    // li legge la cache. Il loro CRC lo ha gia' verificato il converter.
    if (!G->pcm_cache)
        NGF("/sdcard/neogeo/%s/v.bin", G->dir, VROM_SZ, G->crc_v, &vrom, "campioni ADPCM");
#endif
    #undef NGF

    // Nessuna riga di avanzamento a schermo: durante il caricamento si vede solo
    // "LOADING...SD" centrata, come in tutti i giochi ARCADE. I messaggi di
    // ERRORE (sotto, via ng_fatal) restano.
    for (int i = 0; i < n; i++) {
        const NgFile &f = NG_FILES[i];

        if (!*f.dst) {
            *f.dst = (uint8_t *)heap_caps_malloc(f.len, MALLOC_CAP_SPIRAM);
            if (!*f.dst) { snprintf(err, errlen, "PSRAM ESAURITA (%s)", f.path); return false; }
        }
        if (sd_read_exact(f.path, *f.dst, f.len) != f.len) {
            snprintf(err, errlen, "MANCA/ERRATO %s", f.path);
            return false;
        }
        const uint32_t c = ng_crc32(*f.dst, f.len);
        if (c != f.crc) {
            snprintf(err, errlen, "CRC %s: %08lX invece di %08lX",
                     f.what, (unsigned long)c, (unsigned long)f.crc);
            return false;
        }
    }
    return true;
}

// RAM emulata: allocata a runtime, azzerata come lo era il .bss. L'ordine e'
// quello di PRIORITA' per la RAM interna: la palette si legge a OGNI pixel
// disegnato (pal_rgb), la work RAM a ogni accesso del 68000, la SRAM di backup
// quasi mai. Chi non ci sta finisce in PSRAM senza far fallire nulla.
static bool ng_alloc_ram(char *err, size_t errlen)
{
    struct { void **dst; size_t n; bool interna; const char *what; } R[] = {
        { (void **)&palram,      PALRAM_SZ, true,  "palette"      },
        { (void **)&pal_lut,     2 * 4096 * 2, true, "LUT palette" },  /* 16 KB, letta per pixel */
        { (void **)&fix_usage_b, FIXUSE_B_SZ, true, "usage BIOS"  },
        { (void **)&fix_usage_g, FIXUSE_G_SZ, true, "usage gioco" },
        { (void **)&ram,         RAM_SZ,    true,  "work RAM"     },
        { (void **)&vram,        VRAM_SZ,   true,  "VRAM"         },
        { (void **)&sram,        SRAM_SZ,   false, "SRAM backup"  },
        { (void **)&game_vector, VECTOR_SZ, true,  "vettori"      },
    };
    for (unsigned i = 0; i < sizeof(R) / sizeof(R[0]); i++) {
        if (*R[i].dst) { memset(*R[i].dst, 0, R[i].n); continue; }
        *R[i].dst = ng_alloc(R[i].n, R[i].interna);
        if (!*R[i].dst) { snprintf(err, errlen, "MEMORIA ESAURITA (%s)", R[i].what); return false; }
    }
    // ⭐ RAM di cartuccia della protezione NEO-PVC (8 KB): solo per i giochi che
    //    ce l'hanno. Va in RAM INTERNA perche' il gioco ci parla in continuazione
    //    (ogni conversione di colore passa di qui), ed e' comunque poca roba.
    if (G->pvc && !pvc_ram) {
        pvc_ram = (uint16_t *)ng_alloc(PVC_WORDS * 2, true);
        if (!pvc_ram) { snprintf(err, errlen, "MEMORIA ESAURITA (RAM cartuccia PVC)"); return false; }
    }
    if (pvc_ram) memset(pvc_ram, 0, PVC_WORDS * 2);

    // tabelle di Musashi (576 KB): non stanno nella DRAM interna, vanno in PSRAM
    if (!m68k_jump) m68k_jump = heap_caps_malloc(M68K_JUMP_SIZE, MALLOC_CAP_SPIRAM);
    if (!m68k_cyc)  m68k_cyc  = heap_caps_malloc(M68K_CYC_SIZE,  MALLOC_CAP_SPIRAM);
    if (!m68k_jump || !m68k_cyc) { snprintf(err, errlen, "MEMORIA: tabelle 68000"); return false; }
    memset(m68k_jump, 0, M68K_JUMP_SIZE);
    memset(m68k_cyc,  0, M68K_CYC_SIZE);
    return true;
}

// ============================================================ input
// $300000 (REG_P1CNT) e' "DCBArldu" ATTIVO BASSO — bit0 su' … bit3 destra,
// bit4 A, bit5 B, bit6 C, bit7 D (NeoGeo Development Wiki, "Memory mapped
// registers"). Metal Slug usa TRE tasti: A spara, B salta, C granata; D non
// e' usato dal gioco.
// ⭐ Col bridge C6 **V2** i quattro tasti del NeoGeo ci sono tutti davvero:
//      A = FIRE (spara) · B = EXTRA (salta) · C = **Y** (granata) · D = X
//    X e Y arrivano nel byte `flags` del pacchetto UART, che era quasi tutto
//    inutilizzato; il pacchetto e' rimasto di 4 byte.
// ⚠️ COM'ERA PRIMA (bridge V1): il bridge dava un solo pulsante di fuoco piu'
//    EXTRA, cosi' la granata doveva stare su START e contava sul fatto che il
//    gioco legge START1 solo fuori dalla partita. Quel trucco non serve piu':
//    START torna a fare **solo** START1 ($380000 bit0), e la D — che Metal Slug
//    non usa ma altri giochi si' — non e' piu' irraggiungibile.
// ⚠️ START TENUTO 3 s resta l'uscita al menu (ng_task).
//
// ⭐⭐ ADATTAMENTO 3 di 6 — INPUT. **La mappa di CABINATONE NON si copia**: quella
// console ha il bridge C6 **V2**, che manda X e Y separati; questa ha il ponte a
// UN FILO e quei due tasti non arrivano. Il difetto sarebbe stato INVISIBILE —
// `bt_btn_y()` esisterebbe, compilerebbe, e la granata semplicemente non
// partirebbe mai ([[feedback_tasto_esiste_ma_non_arriva]]).
//
// MAPPA DI QUESTO MOBILE (richiesta dell'utente, 2026-08-11, seconda passata):
//     tasto A  -> SPARO    (NeoGeo A, bit 0x10)
//     tasto B  -> SALTO    (NeoGeo B, bit 0x20)
//     FIRE     -> GRANATA  (NeoGeo C, bit 0x40)
//
// ⛔⛔ PERCHE' LA GRANATA NON PARTIVA (referto utente): non era il tasto, era che
// **ogni azione veniva messa in OR con `buttons_get()`**. Quella funzione fonde
// il tasto A (GPIO2) e il pulsante dell'encoder (GPIO48) nello STESSO bit
// BUTTON_FIRE: quindi premendo FIRE per la granata partiva ANCHE lo sparo, e i
// due tasti non erano mai davvero distinti. Il difetto non era nel GPIO — che
// funziona — ma nell'averlo sommato a un bit che contiene gia' un altro tasto.
// ✅ Ora i tre pulsanti del mobile si leggono SOLO dai loro tre GPIO, e il
// gamepad BT si somma a parte con `bridge_buttons()`, che non contiene i tasti
// del mobile. Cosi' le tre azioni sono indipendenti per costruzione.
// 👉 Regola: se due tasti fisici finiscono nello stesso bit, quel bit non si usa
// per distinguerli — nemmeno «in OR, tanto non fa male».
//
// ⚠️ VERIFICATO SUI PIN, non sull'API: nel firmware `buttons_get()` fonde il
// tasto A (BTN_A_PIN, GPIO2) e il pulsante dell'encoder (EC11_PIN_SW, GPIO48)
// **nello stesso bit BUTTON_FIRE**. Sono pero' due GPIO DISTINTI, gia' messi in
// INPUT_PULLUP da Input::init(): qui si leggono separati, cosi' i tre tasti che
// l'utente ha chiesto sono davvero tre. Nessun altro gioco viene toccato —
// l'adattamento sta nel player, non nel livello di input condiviso.
//
// Col GAMEPAD BT (ponte C6) i pulsanti separati sono solo due, quindi:
//     FIRE del pad -> SPARO   ·   EXTRA del pad -> SALTO   ·   granata: nessuno.
// E' un limite dell'hardware del ponte, non una scelta: sul mobile ci sono tutti
// e tre.
// ⚠️ EFFETTO COLLATERALE NOTO: tenere il pulsante dell'encoder 3 s attiva la
// regolazione del volume (e' cosi' per tutti i giochi di questo firmware).
// Tenendo premuta la granata per 3 s comparira' la barra: non e' un difetto del
// gioco. Per cambiarlo servirebbe toccare input.cpp, che vale per tutti.
static void ng_read_inputs(void)
{
    const uint8_t b = ng_buttons();
    // i tre tasti d'azione, letti dai LORO pin (attivi bassi)
    const bool k_a    = !digitalRead(BTN_A_PIN);      // tasto A del mobile
    const bool k_b    = !digitalRead(BTN_B_PIN);      // tasto B del mobile
    const bool k_fire = !digitalRead(EC11_PIN_SW);    // pulsante dell'encoder
    // ⭐ SOLO il gamepad BT: non contiene i pulsanti del mobile, quindi sommarlo
    // non puo' rimescolare i tre tasti fisici.
    const uint8_t pad = g_input->bridge_buttons();

    uint8_t p1 = 0xFF;
    if (b & B_UP)    p1 &= ~0x01;
    if (b & B_DOWN)  p1 &= ~0x02;
    if (b & B_LEFT)  p1 &= ~0x04;
    if (b & B_RIGHT) p1 &= ~0x08;
    if (k_a    || (pad & B_FIRE))  p1 &= ~0x10;   // A del NeoGeo — SPARA
    if (k_b    || (pad & B_EXTRA)) p1 &= ~0x20;   // B del NeoGeo — SALTA
    // ⭐ GRANATA: pulsante FIRE del mobile **oppure START**.
    // START come terzo tasto e' il ripiego gia' usato da CABINATONE ai tempi del
    // ponte C6 V1, con la motivazione scritta li': **il gioco legge START1 solo
    // FUORI dalla partita**, quindi in gioco quel tasto e' libero e non toglie
    // niente. Serve perche' dal gamepad BLE non arriva nessun terzo pulsante:
    // il ponte C6 di questa scheda manda solo FIRE ed EXTRA, e il flag Y lo
    // popola un solo formato HID su quattro (per un pad Xbox e' sempre false).
    // ⛔ Il flag Y resta nell'OR ma OGGI NON ARRIVA: la correzione al ponte e'
    // scritta in C6_BLE_Bridge.ino ma **il C6 non e' stato riflashato**, per
    // scelta dell'utente. Se un domani lo si flasha, la granata funziona anche
    // su Y senza toccare niente qui.
    // ⚠️ START continua a fare START1 (sotto) e il long-press di 3 s continua a
    // uscire al menu: tenendo premuta la granata per 3 s si torna al menu. E' il
    // prezzo del ripiego, ed e' lo stesso che ha CABINATONE.
    if (k_fire || (b & B_START) || g_input->button_y_pressed()) p1 &= ~0x40;
    // D (0x80) non e' usato da Metal Slug e su questa scheda non c'e' un tasto.
    in_p1 = p1;

    uint8_t st = 0xFF;
    if (b & B_START) st &= ~0x01;      // START1 (SELECT = bit1, non mappato)
    in_start = st;

    // $320001: 0x3F e' il valore che il BIOS vede a riposo (memory.c / emu.c),
    // i bit 6 e 7 li mette il calendario dentro ng_r8.
    uint8_t coin = 0x3F;
    if (b & B_COIN) coin &= ~0x01;     // COIN1
    in_coin = coin;
}

// ============================================================ suono (YM2610)
#if NG_AUDIO
// ⚠️ gli include stanno QUI e non in cima: NG_AUDIO e' definito piu' sopra, e
// cosi' con l'audio spento non si tira dentro nulla del sonoro.
// ⚠️ ADATTAMENTO: `cpus/z80/Z80.h` NON si include qui. Il core Z80 di questo
// gioco e' `cpus/z80ng/`, gia' incluso in cima coi simboli rinominati `Ng*`:
// reincludere quello generico farebbe tornare i simboli duplicati con gli altri
// cinque Z80 del progetto.
extern "C" {
#include "ng_ym2610.h"
}
// Il ponte audio dello SPINNERINO e' `laserdisc_audio_*`, dichiarato in cima:
// niente `audio_pce.h`, che e' il codec ES8311 di CABINATONE.

// Portato da gngeo: Z80 sonoro + YM2610, con la struttura del suo port ESP32
// (esp32_platform.c z80_sound_task): 16 fette di Z80 per frame, `my_timer()`
// dopo ognuna, poi UNA generazione di campioni per frame.
//
// ⭐ PERCHE' QUI IL SONORO PUO' STARE SU UN ALTRO CORE (a differenza di Bubble
// Bobble, dove spostarlo fu un errore): lo spazio di memoria del Z80 sonoro e'
// COMPLETAMENTE DISGIUNTO da quello del 68000 — un array piatto di 64 KB piu' i
// registri del YM2610. L'unico punto in comune e' il latch dei comandi, che e'
// un byte. Su Bubble Bobble invece i tre Z80 condividevano il bus, ed e' li' che
// il parallelismo faceva danno.
//
// L'orologio e' `audio_pce_write`, che e' BLOCCANTE: consegnando 541 campioni
// per volta a 32 kHz il task si autoregola a 59,15 Hz senza semafori. E' lo
// stesso schema di boblbobl.cpp e blktiger.cpp.

// ⭐ ADATTAMENTO 2 di 6 — AUDIO. Il rate e' lo stesso (32 kHz), ma la costante
// arriva da un altro posto: la' `PCE_SAMPLERATE` dell'ES8311, qui e' il rate
// NATIVO dell'I2S dello SPINNERINO. ⛔ NIENTE `i2s_set_sample_rates`: col driver
// nuovo e' codice morto, e su questa scheda l'I2S e' aperto al BOOT
// ([[spinnerino_audio_i2s_32khz]]).
#define NG_SND_RATE    32000
#define NG_SND_FRAME   (NG_SND_RATE * 100 / 5919)   // 541 campioni a 59,1856 Hz
#define NG_Z80_HZ      4000000
#define NG_Z80_SLICES  16                           // = z80_nb_interlace di gngeo
#define NG_Z80_CYC     (73333 / NG_Z80_SLICES)      // cpu_z80_timeslice, emu.c:363
#define NG_SND_GAIN_NUM 1
#define NG_SND_GAIN_DEN 1

// ⛔ GUADAGNO E SOFT LIMITER, nella forma di laserdisc.cpp/dl2.cpp/outrun.cpp.
// Su CABINATONE il guadagno e' 1/1 e l'utente ha confermato l'audio: qui si
// PARTE DA LI' e non si inventa un livello a tavolino. Il limiter c'e' comunque
// perche' e' il volume a moltiplicare, e senza compressione un picco si sente
// come "gracchio" invece che come un taglio morbido.
// ⚠️ Il livello vero su QUESTA scheda non e' mai stato ascoltato: se risultasse
// basso, si alza NG_BOOST_NUM (come si e' fatto per Out Run, 2/1 -> 7/2), NON si
// tocca il mix del chip.
#define NG_BOOST_NUM   1
#define NG_BOOST_DEN   1
#define NG_LIM_T       24000                        // soglia del limiter

// Stack del task audio: dentro ci girano il Z80 e YM2610Update_stream, che ha
// parecchie variabili locali.
// ⭐⭐ PRENOTATO PER PRIMO, prima di ogni altra allocazione interna, e il task si
// crea in forma STATICA su quel buffer. E' lo stesso rimedio che questo progetto
// usa gia' in `emulation.cpp` (stack statico «cosi' la creazione non puo' piu'
// fallire»), applicato al motivo per cui e' nato: **lo stack di un task
// FreeRTOS non puo' stare in PSRAM**, quindi e' il concorrente che NON deve mai
// perdere la gara per la RAM interna. Chi arriva prima si serve, e il primo
// dev'essere cio' che non ha alternative.
// ⚠️ A differenza di emulation.cpp qui NON e' un array statico: sarebbero 16 KB
// di .bss tolti a TUTTI gli altri giochi per sempre. Si prende dall'heap
// all'ingresso e si restituisce in ng_cleanup().
#define NG_SND_STACK   16384
static StackType_t  *ng_snd_stack = nullptr;   // StackType_t = byte su ESP-IDF
static StaticTask_t *ng_snd_tcb   = nullptr;

// ---- memoria per il core (mai statica: nel binario stanno 9 emulatori) -----
extern "C" void *ng_snd_alloc(size_t n)
{
    void *p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
    return p;
}
extern "C" void *ng_snd_alloc_caldo(size_t n)
{
    // tl_tab e' letta per operatore per campione: se c'e' posto va in interna.
    // Se non c'e', si ripiega sulla PSRAM invece di fallire (regola imparata
    // sulle tabelle dell'OPL di Bubble Bobble).
    // ⛔⛔ RISERVA RIDOTTA, non azzerata — e il perche' e' costato un flash.
    // Il ragionamento giusto resta quello di 1944: *la riserva serve a impedire
    // che il VIDEO affami l'AUDIO; applicarla ai buffer DELL'AUDIO e'
    // rovesciato*, e con `n + 48 KB` questa tabella (26 KB, letta per operatore
    // per campione) finirebbe in PSRAM in silenzio.
    // ⛔ MA azzerare la riserva del tutto era sbagliato in un altro modo: questa
    // e' l'ULTIMA allocazione interna prima della creazione del task audio, e
    // **lo stack di un task FreeRTOS DEVE stare in RAM interna**. Presa tutta la
    // memoria qui, la create falliva, il Z80 non girava e il BIOS mostrava
    // «Z80 ERROR» — un messaggio che accusa il gioco. Ora lo stack e' prenotato
    // all'ingresso di ng_task, quindi qui basta un margine per il resto del
    // sistema; il margine resta perche' la lezione vale in generale:
    // 👉 quando si toglie una riserva, guardare **cosa alloca subito dopo**.
    void *p = nullptr;
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > n + 16384)
        p = heap_caps_calloc(1, n, MALLOC_CAP_INTERNAL);
    if (!p) p = heap_caps_calloc(1, n, MALLOC_CAP_SPIRAM);
    return p;
}
extern "C" void ng_snd_free(void *p) { if (p) heap_caps_free(p); }

// I campioni ADPCM-A dalla SD, quando il v.bin non entra in PSRAM (Metal Slug
// 3). La macro ADPCM_READ_A del core chiama qui solo se `adpcm_cacheA.active`.
// ⚠️ NON e' un path per pixel: sono ~55.000 chiamate al secondo in tutto (sei
// canali che leggono un byte ogni due campioni), quindi non serve nessun memo
// del banco — gfx_cache_bank sul colpo buono e' uno shift e un confronto.
extern "C" u8 adpcm_cache_read(adpcm_cache_t *c, u32 addr)
{
    (void)c;
    if (addr >= VROM_SZ) return 0;      /* fuori dalla ROM: silenzio, non crash */
    const uint8_t *b = gfx_cache_bank(&pcm_cache, addr);
    return b[addr & (NG_PCM_BANK - 1)];
}

// dove YM2610Update_stream scrive: lo punta ng_audio_task prima di ogni giro
extern "C" Uint16 *play_buffer = nullptr;

// ---- il temporizzatore di gngeo (timer.c), portato tale e quale ------------
// Tre posti fissi, nessuna allocazione. `ng_timer_count` e' in SECONDI e
// avanza di 1/59,19/16 a ogni fetta di Z80.
#define NG_MAX_TIMER 3
extern "C" double ng_timer_count = 0.0;
static ng_timer_struct ng_timers[NG_MAX_TIMER];
static double ng_timer_inc = 0.0;

extern "C" double ng_timer_get_time(void) { return ng_timer_count; }

extern "C" ng_timer_struct *ng_insert_timer(double duration, int param, void (*func)(int))
{
    for (int i = 0; i < NG_MAX_TIMER; i++)
        if (ng_timers[i].del_it) {
            ng_timers[i].time  = ng_timer_count + duration;
            ng_timers[i].param = param;
            ng_timers[i].func  = func;
            ng_timers[i].del_it = 0;
            return &ng_timers[i];
        }
    return nullptr;                       /* gngeo: "No timer free!" */
}
extern "C" void ng_del_timer(ng_timer_struct *ts) { if (ts) ts->del_it = 1; }
extern "C" void ng_free_all_timer(void)
{
    for (int i = 0; i < NG_MAX_TIMER; i++) ng_timers[i].del_it = 1;
}
extern "C" void ng_timer_reset(int nb_interlace)
{
    ng_timer_count = 0.0;
    ng_timer_inc   = 0.01666 / (double)nb_interlace;   /* timer.c:my_timer */
    ng_free_all_timer();
}
extern "C" void ng_my_timer(void)
{
    ng_timer_count += ng_timer_inc;
    for (int i = 0; i < NG_MAX_TIMER; i++)
        if (!ng_timers[i].del_it && ng_timer_count >= ng_timers[i].time) {
            if (ng_timers[i].func) ng_timers[i].func(ng_timers[i].param);
            ng_timers[i].del_it = 1;
        }
}

// ---- il latch dei comandi fra 68000 (core 1) e Z80 (core 0) ---------------
// ⚠️ FIFO, non un latch singolo: se il 68000 manda due comandi ravvicinati, un
// byte solo ne perde uno. E' la lezione pagata su Out Run, dove il latch
// singolo perdeva il 48% dei comandi sonori.
#define NG_CMD_FIFO 64
static volatile uint8_t  ng_cmd[NG_CMD_FIFO];
static volatile uint32_t ng_cmd_w = 0, ng_cmd_r = 0;
static volatile uint8_t  sound_code = 0, pending_command = 0, result_code = 0;

static inline void ng_sound_command(uint8_t code)      // dal bus del 68000
{
    const uint32_t w = ng_cmd_w;
    if ((uint32_t)(w - ng_cmd_r) >= NG_CMD_FIFO) return;    /* piena: si scarta */
    ng_cmd[w & (NG_CMD_FIFO - 1)] = code;
    ng_cmd_w = w + 1;
}

// memory.c:439-455 — il byte PARI di $320000 e' il codice di risposta del Z80.
// ⚠️ Con l'audio spento era la costante 0x01; appena il Z80 gira bisogna dare
// TUTTO il protocollo, altrimenti quello 0x01 fisso diventa un blocco.
static inline uint8_t ng_sound_result(void)
{
    uint8_t res = result_code;
    if (pending_command) res &= 0x7F;
    return res;
}

// ---- il Z80 sonoro ---------------------------------------------------------
static Z80      snd_cpu;
static uint8_t *z80mem = nullptr;            // 64 KB piatti (mame_z80mem)
static uint16_t z80_bank_reg[4] = { 0x8000, 0xC000, 0xE000, 0xF000 };

// mamez80_interf.c cpu_z80_switchbank: il banco si COPIA nell'immagine piatta.
// ⚠️ la memcpy e' protetta dal confronto con la dimensione della ROM: con m.bin
// da 128 KB gli indici alti uscirebbero dal buffer.
static void ng_z80_switchbank(uint8_t bank, uint16_t port)
{
    if (bank > 3) return;
    z80_bank_reg[bank] = port;
    const size_t sz = MROM_SZ;    /* m.bin: 128 KB, ma 512 su Metal Slug 3 —
                                     con la costante vecchia i banchi alti del
                                     suo driver sonoro non venivano mai copiati
                                     e la musica sarebbe stata quella sbagliata */
    size_t off, dst, len;
    switch (bank) {
    case 0: off = 0x4000 * ((port >> 8) & 0x0F); dst = 0x8000; len = 0x4000; break;
    case 1: off = 0x2000 * ((port >> 8) & 0x1F); dst = 0xC000; len = 0x2000; break;
    case 2: off = 0x1000 * ((port >> 8) & 0x3F); dst = 0xE000; len = 0x1000; break;
    default:off = 0x0800 * ((port >> 8) & 0x7F); dst = 0xF000; len = 0x0800; break;
    }
    if (off < sz) memcpy(z80mem + dst, mrom + off, len);
}

static uint8_t ng_z80_rd(uint16_t a)            { return z80mem[a]; }
static uint8_t ng_z80_op(uint16_t a)            { return z80mem[a]; }
static void    ng_z80_wr(uint16_t a, uint8_t v)
{
    /* la sola RAM del sonoro e' 0xF800-0xFFFF (memory map di gngeo) */
    if (a >= 0xF800) z80mem[a] = v;
}

// memory.c:139-220 — le porte. ⚠️ i banchi si commutano IN LETTURA (0x08-0x0B).
static uint8_t ng_z80_in(uint16_t port)
{
    switch (port & 0xFF) {
    case 0x00: pending_command = 0; return sound_code;
    case 0x04: return YM2610Read(0);          /* stato A   */
    case 0x05: return YM2610Read(1);          /* porta dati*/
    case 0x06: return YM2610Read(2);          /* stato B   */
    case 0x08: ng_z80_switchbank(3, port); return 0;
    case 0x09: ng_z80_switchbank(2, port); return 0;
    case 0x0A: ng_z80_switchbank(1, port); return 0;
    case 0x0B: ng_z80_switchbank(0, port); return 0;
    }
    return 0;
}
static void ng_z80_out(uint16_t port, uint8_t v)
{
    switch (port & 0xFF) {
    case 0x04: YM2610Write(0, v); break;      /* controllo A */
    case 0x05: YM2610Write(1, v); break;      /* dati A      */
    case 0x06: YM2610Write(2, v); break;      /* controllo B */
    case 0x07: YM2610Write(3, v); break;      /* dati B      */
    case 0x0C: result_code = v;   break;
    }
}
// ⚠️ ADATTAMENTO 6 di 6 — Z80. Su CABINATONE queste cinque erano i campi di una
// `Z80Bus` (z80_bus.cpp con le globali smistate su puntatori a funzione). Qui il
// core e' incluso DENTRO questa unita' di compilazione coi simboli `Ng*` (vedi i
// #define in testa), quindi le sue globali si definiscono qui — la stessa forma
// di spaceace.cpp, cps1944.cpp, ghouls.cpp e outrun.cpp. I CORPI restano quelli
// di CABINATONE: qui c'e' solo l'inoltro, una riga per funzione.
extern "C" byte RdZ80(word a)                    { return ng_z80_rd(a); }
extern "C" void WrZ80(word a, byte v)            { ng_z80_wr(a, v); }
extern "C" byte InZ80(word p)                    { return ng_z80_in(p); }
extern "C" void OutZ80(word p, byte v)           { ng_z80_out(p, v); }
extern "C" unsigned char OpZ80_INL(unsigned short a) { return ng_z80_op(a); }
extern "C" word LoopZ80(Z80 *R)                  { (void)R; return INT_NONE; }
extern "C" void PatchZ80(Z80 *R)                 { (void)R; }

// ---- i due agganci che il core YM2610 chiama -------------------------------
static ng_timer_struct *ng_fm_timer[2] = { nullptr, nullptr };

extern "C" void ng_timer_callback_2610(int param)   /* 2610intf.c */
{
    ng_fm_timer[param] = nullptr;
    YM2610TimerOver(param);
}
static void ng_fm_timer_handler(int c, int count, double stepTime)
{
    if (count == 0) {                       /* ferma il timer */
        if (ng_fm_timer[c]) { ng_del_timer(ng_fm_timer[c]); ng_fm_timer[c] = nullptr; }
    } else if (ng_fm_timer[c] == nullptr) { /* avvialo */
        ng_fm_timer[c] = ng_insert_timer((double)count * stepTime, c, ng_timer_callback_2610);
    }
}
// memory.c:74 neogeo_sound_irq — linea di LIVELLO, non impulso.
static volatile int ng_snd_irq = 0;
static void ng_fm_irq_handler(int irq) { ng_snd_irq = irq ? 1 : 0; }

// ---- il task audio (core 0) ------------------------------------------------
static int16_t *ng_snd_buf = nullptr;         // NG_SND_FRAME coppie L/R
static volatile bool ng_snd_run = false;
static TaskHandle_t ng_snd_task_h = nullptr;

// Un giro = un frame sonoro. E' una funzione a se' perche' cosi' il banco di
// prova sul PC (`_verify_gfx/neogeo_harness/mk_ngsnd.py`) puo' chiamare QUESTO
// codice, invece di una sua copia: e' la regola del verificare contro il
// sorgente e non contro una propria copia.
static void ng_audio_step(void)
{
    {
        // 1) i comandi arrivati dal 68000: uno per volta, come fa il task Z80 di
        //    gngeo (NMI + 300 cicli per digerirlo)
        while (ng_cmd_r != ng_cmd_w && ng_snd_run) {
            sound_code = ng_cmd[ng_cmd_r & (NG_CMD_FIFO - 1)];
            ng_cmd_r++;
            pending_command = 1;
            IntZ80(&snd_cpu, INT_NMI);
            ExecZ80(&snd_cpu, 300);
        }

        // 2) un frame di Z80 in 16 fette, col temporizzatore fra una e l'altra
        for (int i = 0; i < NG_Z80_SLICES; i++) {
            // la linea IRQ del YM2610 e' di LIVELLO: si ripropone finche' resta
            // alta e la CPU ha gli interrupt abilitati (idiom di blktiger)
            if (ng_snd_irq && (snd_cpu.IFF & IFF_1)) IntZ80(&snd_cpu, INT_RST38);
            ExecZ80(&snd_cpu, NG_Z80_CYC);
            ng_my_timer();
        }

        // 3) i campioni del frame, scritti direttamente nel buffer di uscita
        play_buffer = (Uint16 *)ng_snd_buf;
        YM2610Update_stream(NG_SND_FRAME);

        // ⛔⛔ 3-bis) IL VOLUME. `laserdisc_audio_write()` scrive sull'I2S i
        // campioni TALI E QUALI: non conosce `audio.volumeSetting`, lo applica
        // ogni player nel proprio mix. I tre player portati da CABINATONE se ne
        // erano dimenticati tutti e tre, e il sintomo era «la barra verde si
        // muove ma il volume non cambia» ([[feedback_ponte_audio_non_applica_volume]]).
        // ⚡ UN moltiplicatore per BLOCCO in virgola fissa 8.8: dentro il ciclo
        // restano una moltiplicazione e uno shift, nessuna divisione per
        // campione — questo e' il task che non deve restare indietro.
        {
            const int vs  = laserdisc_current_volume();       // 1..30, 1 = MASSIMO
            const int vol = (vs >= 30) ? 0 : (31 - vs);       // 0..30, 30 = barra piena
            if (vol == 0) {
                memset(ng_snd_buf, 0, NG_SND_FRAME * 2 * sizeof(int16_t));
            } else {
                const int mul = (NG_BOOST_NUM * vol * 256) / (NG_BOOST_DEN * 30);
                // fondo scala IN INGRESSO al limiter col guadagno corrente:
                // scala col volume, cosi' a barra bassa il limiter e' inerte.
                int in_max = (32767 * mul) >> 8;
                if (in_max <= NG_LIM_T) in_max = NG_LIM_T + 1;
                for (int i = 0; i < NG_SND_FRAME * 2; i++) {
                    int s = (ng_snd_buf[i] * mul) >> 8;
                    const int a = s < 0 ? -s : s;
                    if (a > NG_LIM_T) {
                        // compressione continua fra la soglia e il fondo scala
                        const int over = a - NG_LIM_T;
                        const int room = in_max - NG_LIM_T;
                        const int comp = NG_LIM_T + (over * (32767 - NG_LIM_T)) / room;
                        s = (s < 0) ? -comp : comp;
                    }
                    if (s >  32767) s =  32767;
                    if (s < -32768) s = -32768;
                    ng_snd_buf[i] = (int16_t)s;
                }
            }
        }

        // 4) BLOCCANTE: e' questo a dare il tempo al task
        laserdisc_audio_write(ng_snd_buf, NG_SND_FRAME);
#if NG_MISURA
        // ⭐ Contato DOPO la write, cioe' solo quando il blocco e' davvero
        // uscito: e' la misura di quanto l'audio sta in pari, non di quanti
        // giri fa il task. Atteso 59/s (32000 / 541). Se e' molto sotto, il
        // difetto e' qui e non negli fps ([[feedback_task_audio_indietro_watchdog]]:
        // musica lenta e riavvii sono lo stesso difetto).
        ng_aud_blocchi++;
#endif
    }
}

static void ng_audio_task(void *arg)
{
    (void)arg;
    // ⛔ RESPIRO OBBLIGATORIO. Il pacing «la write bloccante e' il mio orologio»
    // regge SOLO finche' si sta in pari: appena il task va in ritardo il DMA e'
    // sempre vuoto, la write non blocca piu' e questo ciclo gira al 100% a
    // priorita' alta, l'idle non viene mai schedulato e il TASK WATCHDOG riavvia
    // la scheda. 1 ms ogni 16 blocchi non costa nulla quando si sta in pari (il
    // DMA ha decine di ms di margine) e salva la scheda quando non ci si sta
    // ([[feedback_task_audio_indietro_watchdog]]).
    uint32_t respiro = 0;
    while (ng_snd_run) {
        ng_audio_step();
        if ((++respiro & 15) == 0) vTaskDelay(1);
    }
    ng_snd_task_h = nullptr;
    vTaskDelete(nullptr);
}

// avvio/arresto: chiamati da ng_task
static bool ng_sound_begin(char *err, size_t errlen)
{
    if (!z80mem)    z80mem    = (uint8_t *)ng_snd_alloc(0x10000);
    if (!ng_snd_buf) ng_snd_buf = (int16_t *)ng_snd_alloc(NG_SND_FRAME * 2 * sizeof(int16_t));
    if (!z80mem || !ng_snd_buf || !ng_ym2610_alloc()) {
        snprintf(err, errlen, "MEMORIA ESAURITA (audio)");
        return false;
    }

    // ---- Z80: immagine piatta = i primi 0xF800 byte della M1, poi i banchi --
    memcpy(z80mem, mrom, 0xF800);
    z80_bank_reg[0] = 0x8000; z80_bank_reg[1] = 0xC000;
    z80_bank_reg[2] = 0xE000; z80_bank_reg[3] = 0xF000;
    ResetZ80(&snd_cpu);
    snd_cpu.IRequest = INT_NONE;
    snd_cpu.IAutoReset = 1;

    // ---- YM2610: 8 MHz, ADPCM-A = v.bin.
    // ⛔⛔ QUI C'ERA IL BUG DELLA MUSICA MANCANTE (trovato con Metal Slug 4).
    //     Il commento diceva "Metal Slug NON ha ADPCM-B: nel .drv la regione 4
    //     e' vuota". La regione 4 vuota NON vuol dire che il gioco non usi il
    //     Delta-T: vuol dire che il Delta-T legge dalle STESSE V-ROM. gngeo lo
    //     scrive nero su bianco (roms.c:1475-1478):
    //         if (r->adpcmb.size == 0) { r->adpcmb.p = r->adpcma.p;
    //                                    r->adpcmb.size = r->adpcma.size; }
    //     Passando `nullptr, 0` il canale Delta-T restava spento per TUTTI i
    //     giochi: sul NeoGeo e' il canale singolo di qualita' alta su cui sta
    //     la MUSICA, mentre i 6 canali ADPCM-A fanno percussioni ed effetti.
    //     Sintomo: effetti perfetti, musica assente.
    // ⚠️ La misura e' VROM_SZ, non una costante: Art of Fighting 2 ha 5 MB di
    //    ADPCM-A e non 8, e passarne una piu' grande del buffer farebbe leggere
    //    l'ADPCM fuori dalla PSRAM.
    ng_timer_reset(NG_Z80_SLICES);
    for (int i = 0; i < 2; i++) ng_fm_timer[i] = nullptr;
    ng_snd_irq = 0;
    ng_cmd_w = ng_cmd_r = 0;
    sound_code = pending_command = result_code = 0;
    // ⭐ con la cache attiva `vrom` e' NULL apposta e i byte arrivano da
    //    adpcm_cache_read(); `VROM_SZ` deve restare la dimensione VERA, perche'
    //    il core ci confronta start/end dei campioni.
    adpcm_cacheA.active = pcm_cache_on ? 1 : 0;
    // ⭐ stessa V-ROM e stessa cache per il Delta-T: `adpcm_cache_read` ignora
    //    il puntatore alla cache e legge da `pcm_cache`, che e' corretto
    //    proprio perche' A e B condividono il file.
    adpcm_cacheB.active = pcm_cache_on ? 1 : 0;
    YM2610Init(8000000, NG_SND_RATE, vrom, VROM_SZ, vrom, VROM_SZ,
               ng_fm_timer_handler, ng_fm_irq_handler);
    YM2610Reset();

    laserdisc_audio_begin();         // ponte verso l'I2S (gia' aperto in setup())
    ng_snd_run = true;
    // ⚠️ stack 16384: dentro ci girano il Z80 e YM2610Update_stream, che ha
    // parecchie variabili locali. I core FM reali sforano gli stack piccoli —
    // e' gia' successo su Bubble Bobble (reboot dopo pochi secondi di musica).
    // ⛔⛔ L'ESITO SI CONTROLLA. Su CABINATONE non si controllava e non si vedeva,
    // perche' li' di RAM interna ne avanza; qui una create fallita non da'
    // NESSUN segno — il Z80 non gira, non risponde al comando del BIOS, e
    // l'unica cosa che si vede e' il POST che si ferma su **«Z80 ERROR»**,
    // cioe' un messaggio che punta al gioco quando il difetto e' nel firmware.
    // Con questo controllo lo stesso guasto si presenta col suo nome e con il
    // numero che serve a capirlo (la RAM interna rimasta).
    // ⭐ Creazione STATICA sullo stack prenotato all'ingresso di ng_task: non
    // dipende ne' dalla RAM libera in questo istante ne' dalla frammentazione.
    // ⛔ L'esito si controlla comunque. Su CABINATONE non si controllava e non
    // si vedeva, perche' li' di RAM interna ne avanza; qui una create fallita
    // non dava NESSUN segno — il Z80 non girava, non rispondeva al comando del
    // BIOS, e l'unica cosa visibile era il POST fermo su **«Z80 ERROR»**, cioe'
    // un messaggio che accusa il gioco mentre il difetto e' nel firmware.
    ng_snd_task_h = (ng_snd_stack && ng_snd_tcb)
        ? xTaskCreateStaticPinnedToCore(ng_audio_task, "ng_audio", NG_SND_STACK,
                                        nullptr, 5, ng_snd_stack, ng_snd_tcb, 0)
        : nullptr;                        // core 0: il 68000 sta sull'1
    if (!ng_snd_task_h) {
        ng_snd_run = false;
        snprintf(err, errlen, "TASK AUDIO KO  IRAM %uK",
                 (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024));
        return false;
    }
    return true;
}

static void ng_sound_end(void)
{
    ng_snd_run = false;
    for (int i = 0; i < 50 && ng_snd_task_h; i++) vTaskDelay(pdMS_TO_TICKS(10));
    // ⚠️ `ng_snd_task_h` lo azzera il task UNA RIGA PRIMA di vTaskDelete: in quel
    // momento sta ancora eseguendo sul proprio stack, che ng_cleanup sta per
    // liberare. 50 ms di margine perche' lo scheduler lo tolga davvero di mezzo:
    // e' la differenza fra "quasi sempre" e "sempre" su una use-after-free.
    vTaskDelay(pdMS_TO_TICKS(50));
    ng_ym2610_free();
    // ⛔ MANCAVA: senza questa, `s_ld_bypass` resta a true dopo l'uscita e il
    // task audio del firmware non riprende a scrivere sull'I2S — cioe' TUTTI
    // gli altri giochi restano muti finche' non si riavvia. Su CABINATONE il
    // problema non esiste perche' li' l'uscita fa esp_restart().
    laserdisc_audio_end();
}
#endif  // NG_AUDIO

// ============================================================ esecuzione
// ⛔⛔⛔ IL QUADRO SI SPEZZA AL VBLANK, NON ALLA FINE — portato dal CABINATONE
// (neogeo.cpp, ng_run_attivo/ng_run_vblank), dove e' la cura della «banda scura
// che lampeggia» di VIEW POINT (2026-09-20), confermata su tutti i NEO GEO.
// Sul Neo Geo vero il chip video legge la lista degli sprite MENTRE disegna le
// 224 righe visibili, e il gioco la riscrive NEL VBLANK. Eseguendo tutte e 264
// le linee e disegnando DOPO si fotografava la lista a META' riscrittura: le
// colonne di sfondo «attaccate» finivano fuori schermo e spariva una fetta.
// ⚠️ Le 264 linee si eseguono TUTTE, nello stesso ordine e con gli stessi
// cicli: cambia solo DOVE si fotografa il quadro (dopo la riga 223).
#define NG_LINE_VBL 224          // le righe visibili: da qui parte il vblank

// la coda del vblank, dopo le righe 224..263
static void ng_coda_vblank(void)
{
    pd4990a_addretrace();            // il calendario avanza di un frame
    // emu.c:228 — il bit 3 di irq2control blocca il contatore di animazione
    if (!(irq2control & 0x8) && (++anim_div % (fc_speed ? fc_speed : 8)) == 0) frame_cnt++;
    m68k_set_irq(1);                 // vblank: la abbassa l'ack (ng_iack), non noi
    m68k_execute(NG_CYC_VBL);
}

#if !NG_BLIT_OVERLAP
// senza overlap: righe visibili, fotografia, vblank — come il CABINATONE
static void ng_run_attivo(void)
{
    for (cur_line = 0; cur_line < NG_LINE_VBL; cur_line++) m68k_execute(NG_CYC_LINE);
}

static void ng_run_vblank(void)
{
    // 264 linee per frame: il contatore raster deve avanzare DENTRO il frame,
    // altrimenti il busy-wait del BIOS su 0x3C0006 non finisce mai.
    for (; cur_line < NG_LINES; cur_line++) m68k_execute(NG_CYC_LINE);
    ng_coda_vblank();
}
#endif

#if NG_BLIT_OVERLAP
// ============================================================================
// ⭐⭐⭐ LE STESSE IDENTICHE FETTE, ma eseguite DENTRO le attese del DMA.
//
// IL FATTO (2026-08-29, misurato): su CABINATONE questo gioco fa 60 fps con
// NG_VIDEO_HALF_RATE **0**, cioe' disegnando OGNI quadro, sulla STESSA CPU P4.
// Il loop e' identico riga per riga; l'unica differenza e' come finisce
// l'immagine sullo schermo:
//     CABINATONE   ng_scale() + ng_flush()  -> PPA in DMA verso DSI   ~2 ms
//     SPINNERINO   ng_blit()                -> SPI 107 KB            12,4 ms
// Con CPU 8,6 e REN 6,0 (letti dall'overlay): la' 16,6 ms = 60 fps, qui 27 ms.
//
// ⭐ E quei 12,4 ms non sono LAVORO, sono ATTESA. `Video::write()` ha **un solo
// dma_buffer**: a ogni striscia fa `spi_device_get_trans_result()` e si ferma
// finche' la striscia precedente non e' uscita. Sono ~0,3 ms x 30 strisce in
// cui il P4 non fa NIENTE, mentre il bus e' occupato.
//
// ⭐⭐ PERCHE' SI PUO' RIEMPIRE QUELL'ATTESA, e perche' e' SICURO:
// il blit LEGGE `fb`; il 68000 SCRIVE la VRAM, la RAM di lavoro e la palette.
// **Non hanno un solo byte in comune** — `fb` lo riempie ng_render(), che qui
// e' gia' finito. Quindi far avanzare l'emulazione del quadro SUCCESSIVO
// mentre esce l'immagine del quadro corrente non e' una modifica
// dell'emulazione: e' la stessa cosa che fanno gia' tutti gli altri giochi di
// questo firmware, dove l'emulazione sta su un core e il video sull'altro
// ([[feedback_chi_disegna_su_quale_core]]). Il player NeoGeo era l'unico a fare
// tutto in fila su un task solo.
//
// ⛔⛔ E L'EMULAZIONE NON CAMBIA DI UN CICLO. Le fette restano **264 da
// NG_CYC_LINE**, nello stesso ordine, con lo stesso vblank in fondo: cambia
// solo QUANDO, rispetto al video, non QUANTO ne' in che sequenza. Questo e' il
// motivo per cui questa cura si puo' spedire e l'idle-skip no — quello saltava
// fette e il contatore raster se ne accorgeva.
// ⚠️ `cur_line` continua ad avanzare una fetta per volta, quindi il busy-wait
// del BIOS su 0x3C0006 vede esattamente la sequenza di prima.
//
// ⭐ COL QUADRO SPEZZATO AL VBLANK (vedi ng_run_attivo) dentro il blit non gira
// piu' «il quadro dopo dall'inizio», ma il pezzo che sta fra due fotografie:
//   passi  0..39    le righe di vblank 224..263 del quadro appena disegnato
//   passo  40       la coda del vblank (retrace, contatore, IRQ1, NG_CYC_VBL)
//   passi 41..264   le 224 righe visibili del quadro DOPO
// e si fotografa a passo 265, cioe' dopo la riga 223: come il CABINATONE.
// ============================================================================
#define NG_PASSO_CODA (NG_LINES - NG_LINE_VBL)              // 40
#define NG_PASSI      (NG_PASSO_CODA + 1 + NG_LINE_VBL)     // 265
// Si parte dalle righe visibili del primo quadro (rimesso a ogni avvio).
static int ng_ovl_passo = NG_PASSO_CODA + 1;

static void ng_passo(int p)
{
    if (p < NG_PASSO_CODA) {                     // vblank: righe 224..263
        cur_line = NG_LINE_VBL + p;
        m68k_execute(NG_CYC_LINE);
    } else if (p == NG_PASSO_CODA) {             // coda del vblank
        cur_line = NG_LINES;
        ng_coda_vblank();
    } else {                                     // righe visibili 0..223
        cur_line = p - NG_PASSO_CODA - 1;
        m68k_execute(NG_CYC_LINE);
    }
}

// Il blit chiama con `fino_a` sulla scala delle 264 fette (264 x (s+1) / 30):
// la si riporta sui 265 passi, cosi' a fine blit sono stati eseguiti tutti.
static void ng_emula_fette(int fino_a)
{
    if (fino_a > NG_LINES) fino_a = NG_LINES;
    const int fino_passo = (int)((int64_t)fino_a * NG_PASSI / NG_LINES);
    while (ng_ovl_passo < fino_passo) ng_passo(ng_ovl_passo++);
}

// prima della fotografia: i passi rimasti (se il blit e' finito prima, o se
// questo giro non ha disegnato), cioe' fino alla riga 223 del quadro.
static inline void ng_chiudi_quadro(void)
{
    while (ng_ovl_passo < NG_PASSI) ng_passo(ng_ovl_passo++);
    ng_ovl_passo = 0;
}
#endif // NG_BLIT_OVERLAP

// ============================================================ task
static void ng_task(void *arg)
{
    (void)arg;
    char err[96] = { 0 };

    #define ALIGN_UP(v, a) (((v) + (a) - 1) & ~((size_t)(a) - 1))
    fb_sz = ALIGN_UP((size_t)NG_W * NG_H * 2, 128);

    // ⚠️ La finestra opcode di Musashi e' UNA globale condivisa da tutti i giochi
    // a 68000 di questo firmware. Qui NON si usa (il fetch del NeoGeo passa da
    // banchi e dal BIOS, non e' un'immagine lineare): la si spegne all'ingresso,
    // cosi' un player uscito male non puo' lasciarcela armata su un'altra ROM.
    m68k_set_opcode_window(nullptr, 0);

    // ⭐⭐ PRIMA DI TUTTO IL RESTO: lo stack del task audio. E' l'unica cosa in
    // tutto il gioco che NON puo' stare in PSRAM, quindi e' la prima a servirsi.
    // Prendendolo per ultimo — com'era — restava fuori e il Z80 non partiva mai.
#if NG_AUDIO
    if (!ng_snd_stack)
        ng_snd_stack = (StackType_t *)heap_caps_malloc(NG_SND_STACK, MALLOC_CAP_INTERNAL);
    if (!ng_snd_tcb)
        ng_snd_tcb = (StaticTask_t *)heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL);
#endif

    ng_window(2);
    ng_caricamento();

    // fb: 176 KB, ed e' il buffer PIU' CALDO di tutti — ~240.000 scritture per
    // frame (90.112 del backdrop + ~148.000 pixel di sprite) contro le ~15.000
    // letture di VRAM e work RAM messe insieme. Per questo si serve per PRIMO
    // sulla RAM interna; se non ce n'e' abbastanza (con la riserva per stack,
    // DSI e bridge) va in PSRAM e il resto se la prende.
    // ⚠️ allineato a 128: sotto c'e' esp_cache_msync, che pretende indirizzo e
    // dimensione multipli della linea di cache.
    if (heap_caps_get_free_size(MALLOC_CAP_INTERNAL) > fb_sz + NG_INT_RESERVE)
        fb = (uint16_t *)heap_caps_aligned_alloc(128, fb_sz, MALLOC_CAP_INTERNAL);
    if (!fb) fb = (uint16_t *)heap_caps_aligned_alloc(128, fb_sz, MALLOC_CAP_SPIRAM);
    if (!fb) ng_fatal("MEMORIA ESAURITA (framebuffer)");
    memset(fb, 0, fb_sz);

#if NG_BLIT_PPA
    // ⭐ Il PPA del P4 su questo firmware NON LO USA NESSUN GIOCO: e' un pezzo
    // di hardware fermo. Qui gli si da' da fare la scala e la rotazione del
    // blit, che oggi costano ~3,3 ms di CPU per quadro.
    // ⚠️ `buffer_size` e indirizzo multipli di 128 (linea di cache L2), e la
    // dimensione deve coprire davvero il buffer: se non lo copre, il driver
    // fa `assert()` -> abort -> riavvio ([[esp32p4_ppa_srm_vincoli]] punto 1).
    // 224*240*2 = 107.520 = 840 x 128: torna esatto.
    {
        ppa_client_config_t pc = {};
        pc.oper_type = PPA_OPERATION_SRM;
        ppa_ok = (ppa_register_client(&pc, &ppa) == ESP_OK);
        if (ppa_ok) {
            ng_ppa_out = (uint16_t *)heap_caps_aligned_alloc(
                             128, NG_PPA_OUT_SZ, MALLOC_CAP_SPIRAM);
            if (ng_ppa_out) memset(ng_ppa_out, 0, NG_PPA_OUT_SZ);
            else { ppa_unregister_client(ppa); ppa = nullptr; ppa_ok = false; }
        }
    }
    // Se una qualunque di queste fallisce si resta sul blit software: il gioco
    // gira come ieri, solo piu' piano. Un acceleratore non deve poter impedire
    // di giocare.
#endif

    if (!ng_alloc_ram(err, sizeof(err))) ng_fatal(err);
    if (!ng_load(err, sizeof(err)))      ng_fatal(err);

    // ⭐⭐⭐ BACKUP RAM "TAMPONATA A BATTERIA" (file opzionale sulla SD).
    // Sul NeoGeo la backup RAM e' alimentata da una batteria e conserva i dati
    // fra un'accensione e l'altra: per il gioco non e' MAI il primo avvio.
    // Da noi partiva azzerata ogni volta, ed e' esattamente questo a far
    // scattare la protezione dei giochi SNK (NeoGeo Dev Wiki, Slot check
    // security): al primo avvio il gioco forza un reset del watchdog e, non
    // arrivando, mostra "ORIGINAL SNK BOARDS ONLY" e si blocca per sempre.
    // Metal Slug 2 faceva esattamente questo. Con una backup RAM gia' "vissuta"
    // parte al primo colpo, senza bisogno di emulare il watchdog.
    // Il file lo produce l'harness sul PC (mk_ngtest.py con NG_SRAMOUT).
    // Se manca non succede nulla: si parte azzerati, come prima.
    {
        char sp[56];
        snprintf(sp, sizeof(sp), "/sdcard/neogeo/%s/sram.bin", G->dir);
        sd_read_exact(sp, sram, SRAM_SZ);      // manca il file -> sram resta a 0
    }

    // ⛔ NIENTE scambio di meta' sul BIOS: gngeo lo carica tale e quale dallo zip
    // (roms.c usa gn_unzip_file_malloc diretto). Lo scambio vale solo per la
    // P-ROM, ed e' il .drv a prescriverlo (lo fa gia' romconv_neogeo.py).
    // ⚠️ La conversione della S-ROM va fatta PRIMA delle cache: si appoggia a
    // una copia temporanea in PSRAM (512 KB con Metal Slug 3) e le cache, che
    // prendono tutto lo spazio che trovano, gliela farebbero fallire.
    if (!converti_fix(fix_bios, FIXBIOS_SZ, fix_usage_b) ||
        !converti_fix(fix_game, FIXROM_SZ,  fix_usage_g))
        ng_fatal("PSRAM ESAURITA (conversione S-ROM)");

    // ⭐ sprite da SD per i set che non entrano in PSRAM (Metal Slug 2: 32 MB).
    // ⚠️ il percorso e' FATFS, senza il prefisso /sdcard che vuole sd_read_exact.
    // ⚠️ PRIMA i campioni, POI gli sprite, e non e' un dettaglio di stile: la
    // cache ADPCM chiede 4 MB e basta, quella degli sprite si prende tutto
    // quello che trova e, se non basta, scende da sola per gradi. Nell'ordine
    // opposto la seconda si ritroverebbe le briciole.
    if (G->pcm_cache) {
        char vp[48];
        snprintf(vp, sizeof(vp), "/neogeo/%s/v.bin", G->dir);
        if (!gfx_cache_init(&pcm_cache, vp, NG_PCM_BANK, NG_PCM_CACHE))
            ng_fatal("CACHE ADPCM: v.bin non leggibile da SD");
        pcm_cache_on = true;
    }
    if (G->spr_cache) {
        char cp[48];
        snprintf(cp, sizeof(cp), "/neogeo/%s/c.ctile", G->dir);
        if (!gfx_cache_init(&spr_cache, cp, NG_SPR_BANK, NG_SPR_CACHE))
            ng_fatal("CACHE SPRITE: c.ctile non leggibile da SD");
        spr_cache_on = true;
    }

#if NG_USE_CUSAGE
    // magic 'C U 0 9' scritto little-endian dal converter: se non torna si
    // ignora la mappa e si disegna tutto (piu' lento, mai sbagliato).
    {
        const uint32_t magic = (uint32_t)cusage_raw[0] | ((uint32_t)cusage_raw[1] << 8) |
                               ((uint32_t)cusage_raw[2] << 16) | ((uint32_t)cusage_raw[3] << 24);
        tile_usage = (magic == 0x43553039u) ? (const uint32_t *)(cusage_raw + 4) : nullptr;
    }
#endif

    memcpy(game_vector, prom, VECTOR_SZ);   // i vettori del gioco, da ripristinare
    memcpy(prom, bios, VECTOR_SZ);          // all'avvio valgono quelli del BIOS

    // ⛔ Qui CABINATONE registrava il client PPA e prendeva il framebuffer del
    // pannello DSI. Su questa scheda il video esce dallo SPI a strisce
    // (ng_blit): niente PPA, niente gamebuf, niente esp_cache_msync — e quindi
    // niente nemmeno la trappola della DIREZIONE del msync che costo' 4 flash su
    // Out Run, perche' qui a scrivere e' sempre e solo la CPU.

    // ---- reset dello stato (il modulo deve poter ripartire pulito) ----
    vptr = rbuf = 0; vmodulo = 1;
    bank_base = 0x100000; pal_bank = 0; fix_from_bios = 1; sram_lock = 0;
    /* la RAM di cartuccia PVC riparte azzerata, come device_reset() di MAME */
    if (pvc_ram) memset(pvc_ram, 0, PVC_WORDS * 2);
    /* protezione di Metal Slug X: si accende solo per il suo set, e riparte col
       contatore e il comando a zero (il costruttore di mslugx_prot_device). */
    mslugx_prot = G->prot_mslugx ? 1 : 0;
    mslugx_counter = mslugx_command = 0;
    cur_line = frame_cnt = anim_div = 0;
    /* memo dell'ultimo banco della cache sprite: appartiene alla cache appena
       creata, non a quella del gioco precedente (vedi ng_cleanup) */
    ng_last_bank = 0xFFFFFFFFu; ng_last_ptr = nullptr;
    fc_speed = 8; irq2control = 0; anim_disabled = 0;
    r_io = w_video = w_pal = w_ram = n_irq = 0;

    /* la LUT parte allineata alla palette RAM (azzerata dall'allocazione):
       da qui in poi la tengono aggiornata le scritture, voce per voce */
    for (int b = 0; b < 2; b++) for (int i = 0; i < 4096; i++) ng_pal_touch(b, i);

    pd4990a_init();                  // orologio: prende data e ora dal sistema

    // ⚠️ m68k_set_tables() PRIMA di m68k_init(): e' init a costruire la tabella
    // dei salti, e senza puntatori validi scriverebbe a indirizzo 0.
    m68k_bus_set(&NG_BUS);
    m68k_set_tables(m68k_jump, m68k_cyc);
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();              // SP e PC arrivano dai vettori del BIOS

#if NG_AUDIO
    // dopo il reset del 68000: il Z80 sonoro parte con la sua CPU gia' pronta a
    // ricevere il primo comando, e il task sul core 0 comincia a scandire il
    // tempo con audio_pce_write
    if (!ng_sound_begin(err, sizeof(err))) ng_fatal(err);
#endif

    // Le bande nere si dipingono UNA volta, poi la finestra SPI si stringe sulle
    // 224 colonne del gioco e non le tocca piu' nessuno.
    ng_bande_nere();
    ng_window(1);
#if NG_BLIT_CORE0
    // da qui l'SPI e' del task video sul core 0, fino a ng_blit_ferma()
    if (!ng_blit_avvia(err, sizeof(err))) { ng_blit_ferma(); ng_fatal(err); }
#endif

    int64_t start_held_since = 0;
    uint32_t fcount = 0;
#if NG_BLIT_OVERLAP
    ng_ovl_passo = NG_PASSO_CODA + 1;   // a ogni avvio: dalle righe visibili
#endif

    for (;;) {
        const int64_t t0 = esp_timer_get_time();

        ng_read_inputs();
#if NG_MISURA
        const int64_t m0 = esp_timer_get_time();
#endif
#if NG_BLIT_OVERLAP
        // ⭐ Il quadro e' gia' stato emulato DENTRO il blit del giro precedente
        // (vblank del quadro prima + righe visibili 0..223 di questo); qui si
        // eseguono solo i passi rimasti, e si fotografa al vblank.
        ng_chiudi_quadro();
#else
        ng_run_attivo();                // le 224 righe visibili, poi si disegna
#endif
#if NG_MISURA
        const int64_t m1 = esp_timer_get_time();
#endif
#if NG_BLIT_CORE0
        // ⭐ Core 1: disegno, consegna al core 0, vblank. Il blit e la barra del
        // volume li fa il task video sul core 0 (ng_blit_task).
        {
#if NG_MISURA
            const int64_t a0 = esp_timer_get_time();
#endif
            ng_blit_prima_di_disegnare();   // solo con UN framebuffer
#if NG_MISURA
            const int64_t a1 = esp_timer_get_time();
#endif
            ng_render();
#if NG_MISURA
            ng_draw_misura();               // i numeri del quadro PRECEDENTE
            const int64_t m2 = esp_timer_get_time();
#endif
            ng_blit_consegna();             // aspetta il core 0 libero, poi scambia
#if NG_MISURA
            const int64_t m3 = esp_timer_get_time();
            ng_acc_ren += (uint32_t)(m2 - a1);
            ng_acc_att += (uint32_t)((a1 - a0) + (m3 - m2));
            ng_acc_vis++;
            // CPU = righe visibili (m1-m0, sommate sotto) + questo vblank:
            // SOLO emulazione, senza attese ne' disegno
            const int64_t mv0 = esp_timer_get_time();
            ng_run_vblank();                // il vblank DOPO la fotografia
            ng_acc_cpu += (uint32_t)(esp_timer_get_time() - mv0);
#else
            ng_run_vblank();                // il vblank DOPO la fotografia
#endif
        }
#else
#if NG_VIDEO_HALF_RATE
        if ((fcount & 1) == 0)
#endif
        {
            ng_render();
#if NG_MISURA
            ng_draw_misura();               // i numeri del quadro PRECEDENTE
            const int64_t m2 = esp_timer_get_time();
#endif
            ng_blit();          // 320x224 -> 240, finestra stretta
            ng_draw_vol_bar();  // dopo il blit: sta nella banda, non nel gioco
#if NG_MISURA
            const int64_t m3 = esp_timer_get_time();
            ng_acc_ren += (uint32_t)(m2 - m1);
            ng_acc_blt += (uint32_t)(m3 - m2);
            ng_acc_vis++;
#endif
        }
#if !NG_BLIT_OVERLAP
        ng_run_vblank();                // il vblank DOPO la fotografia
#endif
#endif // NG_BLIT_CORE0
#if NG_MISURA
        {
            // ⭐ ACCUMULATORI SU UNA FINESTRA DI UN SECONDO, non media mobile.
            // La media mobile su 8 quadri era comoda da leggere ma NON permette
            // di far chiudere il conto: REN e BLT si aggiornano solo nei quadri
            // pari (NG_VIDEO_HALF_RATE) e sommarli a CPU, che si aggiorna a
            // ogni giro, darebbe un totale che non corrisponde a niente. Con le
            // somme divise per il NUMERO GIUSTO di campioni,
            // CPU + REN + BLT torna uguale al lavoro medio per giro — ed e'
            // quel bilancio a dire se lo strumento e' attendibile
            // ([[feedback_ottimizzare_contando_accessi]] caso 7).
            const int64_t mfine = esp_timer_get_time();
            ng_acc_cpu += (uint32_t)(m1 - m0);
            // LAVORO del giro = tutto quello fatto PRIMA dell'attesa di pacing.
            // ⛔ Va preso qui e non in fondo: dopo l'attesa il totale sarebbe
            // sempre FRAME_US e il carico segnerebbe 100 per costruzione
            // ([[feedback_fps_a_passo_fisso_sono_ciechi]]).
            const uint32_t lavoro = (uint32_t)(mfine - t0);
            ng_acc_lav += lavoro;
            if (lavoro > ng_acc_pic) ng_acc_pic = lavoro;
            ng_acc_giri++;

            static int64_t sec_t0 = 0;
            static uint32_t sd0 = 0, mis0 = 0, aud0 = 0;
            if (!sec_t0) {
                sec_t0 = m0;
                sd0 = ng_sd_us(); mis0 = ng_sd_miss(); aud0 = ng_aud_blocchi;
            } else if (m0 - sec_t0 >= 1000000) {
                const uint32_t ms   = (uint32_t)((m0 - sec_t0) / 1000);
                const uint32_t giri = ng_acc_giri ? ng_acc_giri : 1;
                const uint32_t vis  = ng_acc_vis  ? ng_acc_vis  : 1;

                ng_r_fps = ng_acc_vis  * 1000 / ms;
                ng_r_ems = ng_acc_giri * 1000 / ms;   // giri = quadri EMULATI
                ng_r_cpu = ng_acc_cpu / giri;
                ng_r_ren = ng_acc_ren / vis;      // ⚠️ per quadro DISEGNATO
                ng_r_blt = ng_acc_blt / vis;
                ng_r_pic = ng_acc_pic;
                ng_r_car = (ng_acc_lav / giri) * 100 / FRAME_US;
#if NG_BLIT_CORE0
                ng_r_att = ng_acc_att / giri;
                {   // il core 0 scrive, qui si legge per DIFFERENZA
                    static uint32_t b0us0 = 0, b0n0 = 0;
                    const uint32_t us1 = ng_b0_us, n1 = ng_b0_n;
                    ng_r_b0 = (n1 != b0n0) ? (us1 - b0us0) / (n1 - b0n0) : 0;
                    b0us0 = us1; b0n0 = n1;
                }
                ng_acc_att = 0;
#endif

                // le tre voci che vengono da FUORI questo task o da un altro
                // modulo: sempre per DIFFERENZA, mai azzerate alla fonte
                const uint32_t sd1 = ng_sd_us(), mis1 = ng_sd_miss();
                const uint32_t aud1 = ng_aud_blocchi;
                // ⛔⛔ AL SECONDO, non per giro. Erano per giro, in aritmetica
                // INTERA: con 40 miss al secondo su 42 giri il conto faceva
                // 0,95 -> **0**, e la voce che serviva mostrava zero proprio
                // quando c'era qualcosa da vedere. Se ne e' accorto il referto
                // del 2026-08-29: METAL SLUG 2, che legge gli sprite dalla SD,
                // segnava `SD 0 MIS 0` esattamente come METAL SLUG, che dalla SD
                // non legge affatto. Due giochi diversi con lo stesso numero non
                // sono una misura: sono uno strumento cieco
                // ([[feedback_ottimizzare_contando_accessi]] caso 7 — prima di
                // accusare il paziente si verifica il termometro).
                // SD ora e' in **ms al secondo** (confrontabile con 1000: 250 =
                // un quarto del tempo speso sulla scheda), MIS in **miss al
                // secondo**.
                ng_r_sd  = (sd1  - sd0)  / 1000;
                ng_r_mis = (mis1 - mis0) * 1000 / ms;
                ng_r_aud = (aud1 - aud0) * 1000 / ms;
                sd0 = sd1; mis0 = mis1; aud0 = aud1;

                ng_acc_cpu = ng_acc_ren = ng_acc_blt = 0;
                ng_acc_lav = 0; ng_acc_pic = 0;
                ng_acc_giri = 0; ng_acc_vis = 0;
                sec_t0 = m0;
            }
        }
#endif

        // USCITA: START TENUTO 3 SECONDI (START serve anche al gioco, quindi
        // conta la durata). Stessa convenzione degli altri player autonomi.
        if (g_stop) break;
        if (input_consume_back_to_menu()) break;
        if (ng_buttons() & B_START) {
            if (!start_held_since) start_held_since = esp_timer_get_time();
            else if (esp_timer_get_time() - start_held_since >= 3000000) break;
        } else start_held_since = 0;

        // pacing a 59,1856 Hz
        const int64_t due = t0 + FRAME_US;
        int64_t left = due - esp_timer_get_time();
        if (left > 2000) vTaskDelay(pdMS_TO_TICKS((left - 1000) / 1000));
        while (esp_timer_get_time() < due) { /* rifinitura */ }
        // ⚠️ Respiro per il watchdog: vTaskDelay(1), non taskYIELD() (che NON
        // alimenta il task IDLE). Ogni 8 frame: il busy-wait del pacing tiene il
        // core occupato e cedere di rado non basta.
        if ((++fcount & 7) == 0) vTaskDelay(1);
    }

#if NG_BLIT_CORE0
    ng_blit_ferma();                 // l'SPI torna a questo task; fb = l'originale
#endif
#if NG_AUDIO
    ng_sound_end();                  // ferma il task del core 0 prima di uscire
#endif
    ng_cleanup();
    ng_window(2);
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    ng_window(0);
    g_done = true;
    vTaskDelete(nullptr);
}

// ⭐⭐ CHI LIBERA COSA. Su CABINATONE questa funzione NON ESISTE: li' l'uscita fa
// `esp_restart()` e ci pensa il reset. Qui il gioco si LASCIA e si RIPRENDE, e
// sono 26,8 MB di PSRAM: senza questa funzione il secondo lancio troverebbe la
// memoria gia' presa e fallirebbe. ⚠️ Ogni puntatore azzerato e' anche la
// condizione perche' `ng_load`/`ng_alloc_ram` (che riusano il buffer se non e'
// NULL) rialloch(in)o davvero al rientro.
// ⚠️ Le tabelle di Musashi si liberano qui e si ricostruiscono al rientro: e'
// `m68k_set_tables()` a dichiarare che vanno rifatte, e `m68k_init()` legge quel
// flag ([[feedback_init_una_volta_sola_vs_rientro]]).
static void ng_cleanup(void)
{
    m68k_set_opcode_window(nullptr, 0);   // PRIMA di liberare la ROM
    // ⛔⛔ La memo dell'ultimo banco va azzerata QUI, insieme alla cache che la
    // riempie. `ng_last_ptr` punta DENTRO `spr_cache.data`: liberare la cache
    // senza azzerarla lascia un puntatore a memoria restituita all'heap, e al
    // rilancio il primo tile il cui banco coincide con quello memorizzato
    // verrebbe letto da li'. Su CABINATONE non si vede, perche' l'uscita fa
    // esp_restart() e le statiche ripartono da zero: e' ancora
    // [[feedback_init_una_volta_sola_vs_rientro]] — guscio che RIENTRA invece
    // di RIPARTIRE. Riguarda solo i set con cache sprite (Metal Slug 2 e c.).
    ng_last_bank = 0xFFFFFFFFu;
    ng_last_ptr  = nullptr;
    if (spr_cache_on) { gfx_cache_free(&spr_cache); spr_cache_on = false; }
    if (pcm_cache_on) { gfx_cache_free(&pcm_cache); pcm_cache_on = false; }

    void **P[] = {
        (void **)&prom, (void **)&bios, (void **)&fix_bios, (void **)&fix_game,
        (void **)&gfx,  (void **)&cusage_raw, (void **)&ram, (void **)&vram,
        (void **)&sram, (void **)&palram, (void **)&pal_lut,
        (void **)&fix_usage_b, (void **)&fix_usage_g, (void **)&game_vector,
        (void **)&pvc_ram, (void **)&mrom, (void **)&vrom, (void **)&fb,
        (void **)&m68k_jump, (void **)&m68k_cyc,
#if NG_BLIT_PPA
        (void **)&ng_ppa_out,          // 105 KB: su questo firmware si RIENTRA
#endif
#if NG_AUDIO
        (void **)&z80mem, (void **)&ng_snd_buf,
#endif
    };
    for (unsigned i = 0; i < sizeof(P) / sizeof(P[0]); i++)
        if (*P[i]) { heap_caps_free(*P[i]); *P[i] = nullptr; }

#if NG_BLIT_PPA
    // ⚠️ Anche il CLIENT del PPA va restituito, non solo il buffer: qui si torna
    // al menu senza riavviare, e al rilancio si registra di nuovo. Un client
    // lasciato aperto a ogni partita finirebbe le risorse del driver dopo
    // qualche giro ([[feedback_init_una_volta_sola_vs_rientro]]: chi libera,
    // chi ricostruisce?). `ppa_ok` va rimesso a false, o al rientro si userebbe
    // un handle morto — e quello che l'ha spento per un errore lo lascerebbe
    // spento per sempre.
    if (ppa) { ppa_unregister_client(ppa); ppa = nullptr; }
    ppa_ok = false;
#endif

#if NG_AUDIO
    // ⚠️ Lo stack del task si libera SOLO dopo che il task e' davvero finito
    // (ng_sound_end aspetta che ng_snd_task_h torni nullptr): liberarlo mentre
    // ci sta ancora girando sopra sarebbe una corruzione silenziosa.
    if (!ng_snd_task_h) {
        if (ng_snd_stack) { heap_caps_free(ng_snd_stack); ng_snd_stack = nullptr; }
        if (ng_snd_tcb)   { heap_caps_free(ng_snd_tcb);   ng_snd_tcb   = nullptr; }
    }
#endif
    tile_usage = nullptr;
}

// ---------------------------------------------------------------- entry
// La tabella NG_GAMES di CABINATONE e' rimasta INTERA (10 titoli, protezioni
// SMA/PVC/mslugx comprese): esporre un titolo in piu' e' una riga nel menu e i
// dati sulla SD, non un porting nuovo. Vale [[feedback_porta_non_riprogettare]]
// — potare il sorgente che gia' gira sarebbe una riscrittura, non un porting.
//
// ⭐ Il gioco si sceglie per NOME DEL SET, non per indice: il menu ha la sua
// tabella (nome a schermo, cover, file di prova) e l'emulatore ha la propria
// (dimensioni, CRC, protezioni). Due elenchi, due ordini possibili: un indice
// passato da uno all'altro si disallineerebbe **in silenzio** appena qualcuno
// riordina o inserisce una voce, e il sintomo sarebbe "parte il gioco
// sbagliato". Con il nome, un set assente e' un errore leggibile.
bool neogeo_play(Input *input, unsigned short *out_strip, const char *set)
{
    const NgGame *scelto = nullptr;
    for (int i = 0; i < NG_NGAMES; i++)
        if (set && !strcmp(NG_GAMES[i].dir, set)) { scelto = &NG_GAMES[i]; break; }
    if (!scelto) return false;        // set sconosciuto: non si avvia nulla

    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;

    G = scelto;
    if (xTaskCreatePinnedToCore(ng_task, "neogeo", 32768, nullptr, 2, nullptr, 1)
        != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}

// ============================================================================
// Il core Z80 sonoro vive IN QUESTA UNITA' DI COMPILAZIONE (simboli Ng*, vedi i
// #define in testa) — stessa soluzione di spaceace.cpp, cps1944.cpp, ghouls.cpp
// e outrun.cpp: cosi' le RdZ80/WrZ80 globali del core di Marat non collidono con
// quelle degli altri cinque Z80 del progetto. `register` neutralizzato: rimosso
// in C++17, il core lo usa come storage-class.
// ============================================================================
#if NG_AUDIO
#define register
extern "C" {
#include "cpus/z80ng/Z80ng.c"
}
#undef register
#endif
