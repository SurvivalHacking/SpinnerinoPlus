// ============================================================================
// dl2.cpp — DRAGON'S LAIR II: TIME WARP (Leland 1991) per SPINNERINO.
//
// Porting da CABINATONE\dl2_hw.cpp, dove il gioco e' COMPLETO E GIOCABILE su
// device. A differenza di Dragon's Lair (DirkSimple) e di Space Ace (Z80 +
// LD-V1000), DL2 gira su una scheda che e' di fatto un PC/XT: la logica di
// gioco e' un ROM 8088 da 64 KB emulato con libvxt, che pilota un LDP-1450 via
// porta seriale. Nessun hardware video proprio: tutto cio' che si vede e' il
// filmato del laserdisc piu' l'overlay di testo che il player disegna su
// comando del ROM.
//
// ⚠️ FASE 5 — PORTING COMPLETO: motore + video + audio + HUD stile DL con i
//   simboli freccia/spada al posto del prompt testuale del ROM.
//
// ARCHITETTURA — presa da spaceace.cpp, che e' il porting laserdisc ROM-driven
// che GIRA su questo hardware ([[feedback_porta_non_riprogettare]]). ⚠️ E'
// l'INVERSA di CABINATONE (li' il ROM sta su core1 e il video su core0) perche'
// qui il vincolo lo detta lo SPI:
//   - VIDEO su CPU1 = l'UNICO scrittore SPI (task dl2_play, prio 2);
//   - ROM/8088 su CORE0 a passo proprio (task dl2_rom, prio 5), pubblica il
//     frame del disco in g_ld_frame (volatile, cross-core);
//   - decoder JPEG SOFTWARE (JPEGDEC): quello hardware del P4 crasha con questo
//     display SPI;
//   - frame 176x240 GIA' RUOTATI e RGB565 big-endian sulla SD -> niente
//     byte-swap (su CABINATONE costava 11.6 ms/frame) e il blit e' un memcpy.
//
// BILANCIO DELLA RAM INTERNA (fatto PRIMA di flashare, non dopo: e' la regola di
// [[feedback_audio_muto_e_ram_interna]] — «video ok, audio muto» su questa scheda
// e' quasi sempre l'interna finita, e l'audio, che alloca per ultimo, non lascia
// nessun sintomo). Richieste MALLOC_CAP_INTERNAL del player:
//     stack task video (CPU1)   32.768
//     stack task ROM   (core0)  16.384
//     stack task audio (core0)   8.192
//     JPEGDEC su heap           17.884   (misurato con nm, non stimato)
//     prefetch audio g_apf      16.384   (se manca: silenzio di scena, non crash)
//                             --------
//                          ~ 91.600 byte di picco
// contro i ~114 KB che il linker dichiara liberi. La copia scrivibile del ROM
// (64 KB) sta in PSRAM apposta per non entrare in questa somma — vedi la nota in
// dl2_rom_setup. Termine di paragone: Space Ace su questa stessa scheda tiene
// 64 KB interni PERMANENTI (ace_z80mem) piu' le stesse voci, e funziona.
//
// PACK VIDEO — MISURATO sui file veri, non stimato: 212 clip, 35930 frame,
//   frame piu' grande 15974 B (15.6 KB), media 8151 B => 0.24 MB/s a 29.97 fps
//   contro ~1.5 MB/s della SD. La banda avanza sei volte: la lettura sincrona
//   basta e non serve il lettore asincrono a doppio buffer di CABINATONE.
// ============================================================================
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <stdio.h>
#include <stdlib.h>                 // atoi() nel parser dei comandi LDP
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// vxt.h e' C11 e usa _Static_assert, che in C++ non esiste (si chiama
// static_assert). Senza questo alias il file non compila: verificato su
// CABINATONE, non dedotto.
#if defined(__cplusplus) && !defined(_Static_assert)
  #define _Static_assert static_assert
#endif
#include <vxt/vxt.h>
#include <vxt/vxtu.h>

#include "config.h"
#include "emulation/font8.h"
#include "emulation/input.h"
#include "emulation/laserdisc.h"    // dl2_play() dichiarata qui accanto alle altre
#include "emulation/sd_card.h"
#include "emulation/video.h"

#include <JPEGDEC.h>
#include "dl2_rom.h"
#include "dl2_nvram.h"

extern Video video;
extern bool  input_consume_back_to_menu();
extern bool  input_is_vol_mode_active(void);
// Ponte audio del player: mentre gira, Audio::transmit() si sfila (bypass) e il
// player e' l'UNICO writer sull'I2S. `laserdisc_audio_write` e' BLOCCANTE, ed e'
// proprio lei a scandire i 32 kHz. ⛔ NIENTE i2s_set_sample_rates: e' l'API del
// driver legacy, al link trascina dentro il driver vecchio e la scheda NON BOOTA
// ([[spinnerino_audio_i2s_32khz]]). L'I2S e' gia' nativo a 32 kHz = il rate dei .pcm.
extern void  laserdisc_audio_begin(void);
extern void  laserdisc_audio_end(void);
extern void  laserdisc_audio_write(const int16_t *stereo, int frames);
extern int   laserdisc_current_volume(void);       // audio.volumeSetting (1..30)

// ---------------------------------------------------------------- parametri (= DL / Space Ace)
#define LD_W          176
#define LD_H          240
#define LD_X_OFF      72            // letterbox: 176 centrati nei 320 del FB
#define LD_FB_H       240
// Frame piu' grande del pack MISURATO: 15974 byte. 48 KB e' il valore di Space
// Ace, lasciato uguale: sta in PSRAM, non costa nulla, e copre qualunque
// riconversione futura del pack.
#define LD_IN_MAX     (48 * 1024)
#define LD_TASK_STACK 32768         // task VIDEO (CPU1), come Space Ace
// ⚠️ 16 KB, non 8: i core emulati veri sforano lo stack (lezione SPINNERINO,
// [[feedback_emulation_task_stack_fm]]). Su CABINATONE il task dl2 e' a 16384.
#define DL2_ROM_STACK 16384         // task EMULAZIONE (core0)

// ⭐ INTERRUTTORE DI BISEZIONE. 1 = video vero. 0 = la pagina diagnostica della
// fase 2 (stato LDP, frame, comandi, testo del player, EMU kHz): il motore gira
// identico, cambia solo cosa si vede. Serve a separare "il ROM non funziona" da
// "il video non funziona" SENZA spendere un flash in piu'.
#define DL2_VIDEO     1
#define DL2_OC_N      4             // handle .mjpg tenuti aperti (cache LRU)
#define DL2_MAX_CLIPS 224           // dl2.meta ne ha 212 (misurati, non stimati)

// ------------------------------------------------------------ AUDIO (fase 4)
// Due sorgenti, come sul cabinato vero:
//   1. MUSICA/VOCE della scena = <clip>.pcm sulla SD (s16le stereo 32000 Hz,
//      copiati INVARIATI dal pack sorgente: sono gia' al rate nativo dell'I2S).
//   2. PC SPEAKER = l'UNICO chip audio della scheda Leland (PIT ch2 + porta
//      0x61). Verificato sul simulatore: DL2 lo usa davvero.
#define DL2_AUDIO       1           // 0 = muto (bisezione)
#define DL2_AUDIO_SCENE 1           // 0 = resta il solo bip: separa "e' la SD"
                                    //     da "e' il task audio in se'"
#define DL2_AC_N        4           // .pcm tenuti aperti (LRU). 4 video + 4 audio
                                    //  = 8 <= SD_MAX_FILES 12 (config.h)
#define DL2_APF_N       4096        // prefetch = 128 ms @32kHz = 16 KB

// ⭐ GUADAGNI RICAVATI DA UNA MISURA, non ereditati da CABINATONE (li' erano
// tarati sul DAC ES8311, un'altra catena). RMS dei .pcm misurato sui file veri:
//     Dragon's Lair  3.5 %FS  -> AUD_BOOST 4 x trim 72  = 2.88x -> 10.1 %FS
//     Space Ace     17.6 %FS  -> scena 10/8 x trim 50   = 0.63x -> 11.0 %FS
//     DL2            6.9 %FS  (0.3..13.6, picchi fino al 100%)
// Due giochi indipendenti concordano sul livello percepito giusto: ~10-11 %FS in
// uscita. Per DL2 serve quindi un fattore netto ~1.6 -> 14/8 x 90% = 1.575
// => uscita attesa 10.9 %FS, in mezzo agli altri due.
#define DL2_GAIN_SCENE  14          // x/8 sulla scena
#define DL2_TRIM_PCT    90          // attenuazione generale del player
// Soglia del SOFT-LIMITER, identica a LD_LIM_T/ACE_LIM_T: i .pcm di DL2 hanno
// picchi fino al 100% FS e col guadagno sopra il mix sfonderebbe -> senza
// limiter tornerebbe l'hard clamp squadrato, cioe' il gracchio.
#define DL2_LIM_T       24000
#define DL2_SPK_AMP     12000       // ampiezza dell'onda quadra del PC speaker
#define DL2_GAIN_SPK    10          // x/8
// ⭐ VOLUME SEPARATO PER IL BIP DELLE MOSSE (richiesta utente su CABINATONE).
// Il PC speaker fa TUTTI gli effetti, ma a frequenze diverse — MISURATE sul
// simulatore, non dedotte (300 s di partita):
//     881 Hz  x108  = il bip di OGNI MOSSA (102 mosse)   <- questo si abbassa
//    1441 Hz  x2    = START ·  1223 Hz x1 = moneta
//    500/1000/440   = sequenza di avvio
// Essendo l'881 lontano da tutti gli altri, si abbassa da solo con una finestra
// larga: 840..930 lascia margine sia verso il 500 sotto sia verso il 1000 sopra
// (e assorbe l'arrotondamento: la nota vecchia diceva 880, il valore vero e' 881).
#define DL2_MOVE_HZ_LO  840
#define DL2_MOVE_HZ_HI  930
#define DL2_GAIN_MOVE   5           // meta' di DL2_GAIN_SPK

#define DL2_DIR       SD_MOUNT_POINT "/dl2"
#define DL2_META      DL2_DIR "/dl2.meta"

// Clock dell'8088 emulato. Il vero e' 10 MHz; su CABINATONE (stesso P4) la
// scheda ne sostiene ~1.18M e la costante e' allineata a quella MISURA, non al
// valore nominale: il disco avanza sui CICLI EMULATI, quindi chiedere piu' di
// quanto la scheda regge fa girare il filmato al rallentatore.
// ⚠️ QUI IL NUMERO E' EREDITATO DA CABINATONE, NON ANCORA MISURATO SU SPINNERINO:
// va ritarato con l'indicatore BOOT della pagina diagnostica (DL2_VIDEO 0).
#define DL2_CPU_HZ    1180000
// Durata dell'assertion di START1 verso il ROM, da una pressione del tasto.
// ⭐ MISURATO col ROM VERO da PY\mk_dl2start.py (sweep con -D), non scelto a occhio:
//     1200 (valore ereditato da CABINATONE) -> partita NON avviata
//     1500 / 2000 / 2400                    -> partita NON avviata
//     2800                                  -> PARTITA AVVIATA (soglia)
//     3000 / 3400 / 3600 / 4000 / 6000      -> PARTITA AVVIATA, nessun effetto
//                                              collaterale (stesso istante, LEVEL 1)
// Il CREDIT compare 3,29 s dopo la pressione (CABINATONE misurava 3,21: due
// misure indipendenti che concordano).
// SCELTO 6000 e non 3000: la soglia vale a DL2_CPU_HZ = 1,18 MHz, ma quel numero
// su SPINNERINO non e' ancora misurato. Se la scheda sostiene MENO di 1,18 MHz
// l'emulazione va piu' lenta del tempo reale, quindi una finestra in millisecondi
// REALI copre MENO cicli emulati e la soglia effettiva sale. 6000 = 2,1x la
// soglia, copre fino a ~0,55 MHz reali, ed e' stato verificato che tenerlo a
// lungo non guasta nulla. Costa zero e fa risparmiare un flash.
#ifndef DL2_START_HOLD_MS
  #define DL2_START_HOLD_MS 6000
#endif
// Costo in KILOCICLI del boot del ROM, dall'avvio al primo SEARCH. MISURATO
// dall'harness: primo SEARCH a 42,9 s emulati x 1,18 MHz = ~50.600 kcicli.
// Serve alla barra della schermata di caricamento: e' un avanzamento VERO
// (cicli emulati / cicli totali), non un'animazione finta.
#define DL2_BOOT_KCYCLES 50600
// Vite ricaricate in modalita' FACILE. 10 = lo stesso valore di partenza
// dell'EEPROM: il contatore semplicemente non scende mai.
#define DL2_EASY_LIVES 10
#define DL2_ROM_BASE  0xF0000
#define DISC_FPS_N    30000         // numerazione frame del laserdisc: 29.97
#define DISC_FPS_D    1001

// ═══════════════════════════════ HUD stile Dragon's Lair ════════════════════
// Il ROM di DL2 scrive LEVEL / n LIVES / CREDIT n / il marquee "INSERT 1
// COIN(S) FOR n LIVES." come TESTO nell'overlay del player, sparso su 3 righe
// in mezzo al filmato. Qui quelle righe vengono INTERCETTATE (i valori finiscono
// nella riga in cima, come dl_draw_scoreboard di Dragon's Lair) e NON disegnate.
// Restano invece testo del ROM: titolo, tabella record e inserimento iniziali.
#define DL2_HUD        1
// ⭐ PUNTEGGIO: il ROM NON lo scrive mai a schermo durante la partita (solo a
// game over), quindi va letto dalla RAM emulata. Indirizzo trovato col
// simulatore offline e CONFERMATO su CABINATONE su tre partite con punteggi
// diversi (1641/16414, 114/1148, 46/469): il valore tenuto e' il punteggio x10.
#define DL2_SCORE_ADDR 0x156B0      // u16 LE = contatore punteggio del ROM
// DIV 1 = si mostra il valore PIENO. Il ROM tiene 16414 e a schermo scrive 1641
// perche' il suo campo di testo e' largo 4 cifre: TRONCA, non divide. Qui lo
// spazio c'e'. ⚠️ Resta una SCELTA (fatta dall'utente su CABINATONE), non una
// correzione: il punteggio "ufficiale" di DL2 e' quello a 4 cifre — prova, i
// record salvati in EEPROM sono 99/60/55/50/30..., gia' troncati. Quindi la
// tabella dei record disegnata dal ROM resta a 4 cifre: incoerenza ACCETTATA.
#define DL2_SCORE_DIV  1
// ⭐ VITE: stessa storia, e stessa struttura dati (4 byte prima).
// ⛔ BUG GIA' CORRETTO SU CABINATONE — "le vite sono a zero da subito": il ROM
// annuncia 'n    LIVES' **solo DOPO una morte** (misurato: LEVEL 1 a 86.5s,
// primo annuncio a 178.1s = 92 s dopo!), quindi prendere le vite dal TESTO le
// lascia a zero per tutta la prima vita. Lette dalla RAM sono giuste da subito.
// Indirizzo confermato: unico byte che combacia con TUTTI e 18 gli annunci del
// ROM su una partita simulata, e vale 9 gia' a inizio partita.
#define DL2_LIVES_ADDR 0x156AC      // u8  = vite rimaste

// ⭐ SIMBOLI DELLE MOSSE al posto del prompt testuale del ROM ("LEFT", "SWORD").
// Stile Dragon's Lair: SOLO IL CONTORNO bianco, niente riempimento ne' riquadro,
// cosi' non copre la grafica del filmato. Geometria presa da `dl_draw_hint` di
// laserdisc.cpp, che e' gia' tarata su questo schermo.
#define DL2_MOVE_SYMBOLS 1
#define DL2_HINT_R   20
#define DL2_HINT_CX  120            // 240/2  (asse lungo dell'area visibile)
#define DL2_HINT_CY  152            // 176-24 (asse corto)

static uint16_t *g_strip = nullptr;
static Input    *g_input = nullptr;
static volatile bool g_stop = false;
static volatile bool g_done = false;
static volatile bool g_rom_running = false;

// ---------------------------------------------------------------- allocatore
// libvxt alloca tutto tramite questa callback: la RAM del PC (960 KB) NON puo'
// stare nella RAM interna del P4, va in PSRAM. I blocchi piccoli restano interni.
// ⛔ NOTA DA CABINATONE, DA NON PERDERE — profilo degli accessi (misurato sul
// simulatore, boot completo del ROM): dei 29.4 MILIONI di accessi alla memoria
// emulata il 100% cade in TRE blocchi da 64 KB (ROM 0xF0000 47.0%, RAM 0x00000
// 30.4%, RAM 0x10000 22.7%). Sembrava ovvio spostarli in RAM INTERNA: PROVATO
// SUL DEVICE, PEGGIORA (boot 43 -> 58 s). Non rifarlo senza prima capire perche'.
static void *dl2_alloc(void *ptr, size_t sz) {
    if (!sz) { if (ptr) heap_caps_free(ptr); return NULL; }
    void *p = heap_caps_realloc(ptr, sz,
                  sz >= 32768 ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
    if (!p) p = heap_caps_realloc(ptr, sz, MALLOC_CAP_SPIRAM);
    return p;
}

// -------------------------------------------------------------- ROM + patch
static uint8_t *dl2_ram_rom;        // copia scrivibile del ROM (per le patch)

static void dl2_patch_rom(uint8_t *r) {
    // Le due FIX obbligatorie di daphne (lair2::patch_roms, v3.19): bug del
    // compilatore Leland. Senza la seconda l'init del laserdisc non passa.
    #define OFF(a) ((a) - DL2_ROM_BASE)
    memmove(&r[OFF(0xF0B93)], &r[OFF(0xF0B92)], 85);
    static const int d1[] = {0xF0B9A,0xF0BA0,0xF0BA7,0xF0BB1,0xF0BBB,
                             0xF0BC5,0xF0BCC,0xF0BD6,0xF0BE0,0xF0BE6};
    for (int i = 0; i < 10; i++) r[OFF(d1[i])]--;
    r[OFF(0xF0B91)] = 0xFE; r[OFF(0xF0B92)] = 0xC0;   // INC AL non INC AX

    memmove(&r[OFF(0xF0C39)], &r[OFF(0xF0C38)], 17);
    r[OFF(0xF0C37)] = 0xFE; r[OFF(0xF0C38)] = 0xC0;
    static const int d2[] = {0xF0C3A,0xF0C40,0xF0C47,0xF0C49};
    for (int i = 0; i < 4; i++) r[OFF(d2[i])]--;

    // Cheat del ROM "hints for all levels": senza, i prompt delle mosse
    // escono SOLO al livello 1. L'utente li vuole accesi ovunque.
    r[OFF(0xF2804)] = 0x90;
    r[OFF(0xF2805)] = 0x90;
    #undef OFF
}

// ------------------------------------------------------------- LDP-1450
// Protocollo da daphne src/ldp-in/ldp1000.cpp: byte BINARI (non ASCII, tranne
// le cifre '0'..'9'), ACK = 0x0A.
enum { LDP_STOP, LDP_PLAY, LDP_PAUSE };

static struct {
    int   state;
    int   frame;          // frame laserdisc corrente
    int   seek_target;    // ultimo SEARCH richiesto (diagnostica)
    char  digits[16];
    int   ndigits;
    int   pending;        // 1 = search, 2 = repeat
    int   repeat_to;
    char  text[3][12];    // le 3 righe dell'overlay LDP-1450
    int   tmode, tline, tn;
    int   tn_total;       // caratteri RICEVUTI (non troncati): serve al clear-all
    int   tx, ty;         // posizione del blocco di testo (0x80 0x00 <X> <Y> <mode>)
    int   text_on;        // 0x81 / 0x82
    char  tbuf[24];
    char  last_cmd[24];
    struct vxt_peripheral *uart;
} ldp;

// --------------------------------- stato pubblicato al task VIDEO (altro core)
// REGOLA ([[spinnerino_volume_multicore_race]]): ogni valore letto da un altro
// core DEVE essere volatile, e solo a 32 bit (niente int64 cross-core: su RV32
// si leggono "strappati").
//   g_ld_frame = -1  -> il ROM non ha ancora fatto il primo SEARCH: il video
//   mostra la schermata di caricamento invece del frame 1 (che sarebbe nero).
static volatile int      g_ld_frame = -1;
static volatile uint32_t g_text_seq = 0;   // ++ ad ogni modifica delle 3 righe

// ⭐ TACHIMETRO. Cicli emulati in MIGLIAIA (32 bit: niente int64 cross-core).
static volatile uint32_t g_emu_kc   = 0;
static volatile uint32_t g_emu_khz  = 0;   // istantanea
static volatile uint32_t g_boot_khz = 0;   // capacita' vera, congelata al 1o SEARCH

static volatile uint32_t g_hud_score  = 0;
static volatile int      g_hud_lives  = 0;   // letto dalla RAM, non dal testo
static volatile int      g_hud_level  = 0;
static volatile int      g_hud_credit = 0;
static volatile int      g_hud_ingame = 0;
static volatile int      g_hud_played = 0;

// ⭐ CONTATORI DEGLI ERRORI DEL VIDEO. Su CABINATONE hanno chiuso in UN flash una
// caccia in cui erano gia' fallite 6 ipotesi, e il valore ZERO e' stato
// informativo quanto un valore alto ("non fallisce: non viene proprio chiamata").
static volatile uint32_t g_e_clip, g_e_idx, g_e_range, g_e_read, g_e_dec;
static volatile uint32_t g_v_fps;

enum { TL_ALTRO = 0, TL_LEVEL, TL_LIVES, TL_CREDIT, TL_MARQUEE };

// Classifica una riga dell'overlay. NIENTE effetti collaterali: la usano sia il
// core dell'emulazione (per aggiornare i valori) sia il task video (fase 5, per
// sapere se saltare la riga), e devono essere sempre d'accordo.
// ⚠️ COPIA LETTERALE da CABINATONE: e' collaudata offline sulle 123 stringhe
// VERE dell'overlay (dl2_hud_test.c). Se si tocca qui, aggiornare anche quella.
static int dl2_text_class(const char *s, int *val)
{
    while (*s == ' ') s++;
    if (!*s) return TL_ALTRO;

    // --- marquee del game over ---
    // Scorre un carattere alla volta, quindi arriva in decine di frammenti
    // ('ERT 1 COIN', 'R  9 LIVES', 'LIVES.    ', ...): si riconoscono dai pezzi
    // di parola che NESSUNA riga buona contiene. ⚠️ Non basta cercare '.': lo
    // contengono anche ' BLUTH CO.' e '   CORP.  ' del titolo, che vanno tenuti.
    if (strstr(s, "INSERT") || strstr(s, "FOR")   || strstr(s, "(S)") ||
        strstr(s, "N(S")    || strstr(s, "IN(S")  || strstr(s, "OIN(S") ||
        strstr(s, "LIVES.") || strstr(s, "IVES.") || strstr(s, "VES.") ||
        strstr(s, "ES."))
        return TL_MARQUEE;
    // coda del marquee: frammenti cortissimi fatti solo di 'S' e '.'
    {
        int only_tail = 1, has_dot = 0;
        for (const char *p = s; *p; p++) {
            if (*p == '.') has_dot = 1;
            else if (*p != 'S' && *p != ' ') { only_tail = 0; break; }
        }
        if (only_tail && has_dot) return TL_MARQUEE;
    }

    if (!strncmp(s, "LEVEL", 5))  { if (val) *val = atoi(s + 5); return TL_LEVEL;  }
    if (!strncmp(s, "CREDIT", 6)) { if (val) *val = atoi(s + 6); return TL_CREDIT; }

    // "8    LIVES" / "1     LIFE" — la tabella record ('9 WBG   13') comincia
    // anch'essa con una cifra, ma non contiene LIVES/LIFE, quindi non collide.
    if (*s >= '0' && *s <= '9' && (strstr(s, "LIVES") || strstr(s, "LIFE"))) {
        if (val) *val = atoi(s);
        return TL_LIVES;
    }
    return TL_ALTRO;
}

// Aggiorna i valori dell'HUD. Gira sul core dell'emulazione, dentro ldp_write.
static void dl2_hud_on_text(const char *s)
{
    int v = 0;
    switch (dl2_text_class(s, &v)) {
    case TL_LEVEL:  g_hud_level = v; g_hud_ingame = 1; g_hud_played = 1; break;
    // le vite NON si prendono da qui (il ROM le annuncia solo dopo una morte,
    // misurato: LEVEL 1 a 86.5s, primo annuncio a 178.1s): il valore arriva
    // dalla RAM. Questo caso serve solo a sapere che la partita e' finita.
    case TL_LIVES:  if (!v) g_hud_ingame = 0;                            break;
    case TL_CREDIT: g_hud_credit = v;                                    break;
    default: break;
    }
}

// Coda RX verso il ROM. INDISPENSABILE: l'8250 ha UN SOLO registro di
// ricezione. Spedendo i 5 byte della Status Inquiry tutti insieme si
// sovrascrivono e il ROM stampa "FAULT AT / LOCATION / SONY: 9001".
// (Bug gia' preso e risolto nell'harness offline: non ripeterlo.)
static uint8_t  rxq[256];
static volatile uint16_t rxq_head, rxq_tail;

static void rx_flush(void) {
    while (rxq_head != rxq_tail && vxtu_uart_ready(ldp.uart))
        vxtu_uart_write(ldp.uart, rxq[rxq_head++ & 0xFF]);
}
static void ldp_tx(uint8_t b) { rxq[rxq_tail++ & 0xFF] = b; rx_flush(); }
static void ldp_ack(void)     { ldp_tx(0x0A); }

static void ldp_write(uint8_t v) {
    // --- text overlay (prefisso 0x80) ---
    if (ldp.tmode == 1) {
        if (v == 0x00)      ldp.tmode = 2;
        else if (v == 0x01) { ldp.tmode = 4; ldp.tn = 0; ldp.tn_total = 0; ldp.tline = 0; }
        else if (v == 0x02) ldp.tmode = 3;
        else                ldp.tmode = 0;
        return;
    }
    // 0x80 0x00 <X> <Y> <mode>: posizione del blocco di testo. Serve a mettere
    // il testo DOVE lo vuole il gioco (titolo in alto, INSERT COIN / LEVEL /
    // prompt delle mosse piu' in basso).
    if (ldp.tmode == 2) {
        static int n;
        if (n == 0)      ldp.tx = v;
        else if (n == 1) ldp.ty = v;
        if (++n >= 3) { n = 0; ldp.tmode = 0; }
        return;
    }
    if (ldp.tmode == 3) { ldp.tmode = 0; return; }
    if (ldp.tmode == 4) {
        if (v == 0x00 || v == 0x0A || v == 0x14) ldp.tline = v / 10;
        else if (v == 0x1A) {
            ldp.tbuf[ldp.tn] = 0;
            // CANCELLA TUTTE LE RIGHE: una stringa di piu' di 20 spazi non e' del
            // testo, e' il modo in cui il ROM pulisce l'overlay (daphne ldp1000.cpp).
            // ⚠️ SENZA QUESTO le righe restano incollate sopra il filmato: verificato
            // sul simulatore offline, il ROM manda "TEXT ON" + 31 SPAZI su riga 0
            // prima di ogni cambio schermata (attract, INSERT COIN, LEVEL).
            // Il conteggio va fatto sui byte RICEVUTI (tn_total), non su tbuf che e'
            // troncato a 23.
            bool all_spaces = true;
            for (int i = 0; i < ldp.tn; i++) if (ldp.tbuf[i] != ' ') { all_spaces = false; break; }
            if (all_spaces && ldp.tn_total > 20) {
                memset(ldp.text, 0, sizeof ldp.text);
            } else {
                int l = ldp.tline; if (l < 0 || l > 2) l = 0;
                strncpy(ldp.text[l], ldp.tbuf, sizeof(ldp.text[0]) - 1);
                ldp.text[l][sizeof(ldp.text[0]) - 1] = 0;
                // ⚠️ i valori si prendono da tbuf (la riga COMPLETA), non da
                // ldp.text[] che e' troncato a 11 caratteri: 'CREDIT   1' ci sta,
                // ma non c'e' margine e il numero e' proprio in fondo.
                dl2_hud_on_text(ldp.tbuf);
            }
            g_text_seq = g_text_seq + 1;        // sveglia il task video (no ++ su volatile)
            ldp.tmode = 0;
        } else if (v >= 0x20) {
            ldp.tn_total++;
            if (ldp.tn < (int)sizeof(ldp.tbuf) - 1) ldp.tbuf[ldp.tn++] = (char)v;
        }
        return;
    }

    if (v >= 0x30 && v <= 0x39) {                       // cifre
        if (ldp.ndigits < 15) ldp.digits[ldp.ndigits++] = (char)v;
        ldp_ack(); return;
    }
    switch (v) {
    case 0x80: ldp.tmode = 1; return;
    // NB (traccia offline): DL2 manda TEXT ON di continuo e TEXT OFF MAI — la
    // pulizia la fa con la stringa di spazi qui sopra. Gestiamo comunque 0x82.
    case 0x81: ldp.text_on = 1; strcpy(ldp.last_cmd, "TEXT ON");  return;
    case 0x82: ldp.text_on = 0; g_text_seq = g_text_seq + 1;
               strcpy(ldp.last_cmd, "TEXT OFF"); return;
    case 0x56: ldp.ndigits = 0; ldp.pending = 0; ldp.repeat_to = 0; ldp_ack();
               strcpy(ldp.last_cmd, "CLEAR"); return;
    case 0x43: ldp.pending = 1; ldp.ndigits = 0; ldp_ack();
               strcpy(ldp.last_cmd, "SEARCH"); return;
    case 0x44: ldp.pending = 2; ldp.ndigits = 0; ldp_ack();
               strcpy(ldp.last_cmd, "REPEAT"); return;
    case 0x3A: ldp.state = LDP_PLAY;  ldp_ack(); strcpy(ldp.last_cmd, "PLAY");  return;
    case 0x4F: ldp.state = LDP_PAUSE; ldp_ack(); strcpy(ldp.last_cmd, "STILL"); return;
    case 0x40:                                          // Enter = esegui
        ldp_ack();
        ldp.digits[ldp.ndigits] = 0;
        if (ldp.pending == 1 && ldp.ndigits) {
            ldp.frame = atoi(ldp.digits);
            ldp.seek_target = ldp.frame;
            ldp.state = LDP_PAUSE;
            // ⛔⛔ UNA SEARCH ANNULLA IL REPEAT PENDENTE. Sul player vero e' ovvio;
            // su CABINATONE mancava, e repeat_to si azzerava SOLO quando il disco
            // lo raggiungeva. CONSEGUENZA (il bug "in partita 0.9 fps"):
            // l'attract imposta REPEAT 2101; se si infila la MONETA prima che il
            // disco arrivi a 2101, repeat_to resta 2101. La partita fa SEARCH 2177
            // + PLAY e alla prima avanzata `2178 >= 2101` -> STOP IMMEDIATO: il
            // disco si pianta sul primo frame del gioco. L'attract funzionava
            // benissimo (li' il repeat serve davvero) -> "rotto solo in gameplay".
            ldp.repeat_to = 0;
            ldp_tx(0x01);                               // search completata
        } else if (ldp.pending == 2 && ldp.ndigits) {
            ldp.repeat_to = atoi(ldp.digits);
        }
        ldp.pending = 0; ldp.ndigits = 0;
        return;
    case 0x60: {                                        // Addr Inq
        char b[8]; snprintf(b, sizeof b, "%05d", ldp.frame);
        for (int i = 0; i < 5; i++) ldp_tx((uint8_t)b[i]);
        return;
    }
    case 0x67:                                          // Status Inq
        ldp_tx(0x80); ldp_tx(0x00); ldp_tx(0x10); ldp_tx(0x00);
        ldp_tx(ldp.state == LDP_PAUSE ? 0x20 : (ldp.state == LDP_PLAY ? 0x01 : 0x00));
        return;
    default:
        ldp_ack(); return;
    }
}

static void uart_data_cb(struct vxt_peripheral *p, vxt_byte data, void *ud) {
    (void)p; (void)ud; ldp_write(data);
}
static void uart_ready_cb(struct vxt_peripheral *p, void *ud) {
    (void)p; (void)ud; rx_flush();          // il ROM ha letto: manda il prossimo
}

// ------------------------------ porta 0x201 (input) e 0x202 (EEPROM + monete)
static volatile uint8_t in_201 = 0xFF;      // ATTIVO BASSO
static volatile uint8_t in_202 = 0x01;      // ATTIVO ALTO + bit0 = dato EEPROM
// latch della moneta: armato dalla pressione, azzerato quando il ROM la consuma
// (scrittura del bit 6 su 0x202). Dichiarato qui perche' serve gia' a io_out().
static bool coin_pending, coin_prev;
static uint16_t eeprom[0x100];

// EEPROM National 9536: porting fedele di lair2::EEPROM_9536_write (daphne).
static uint8_t  nv_opcode = 0xFF, nv_old = 0;
static uint16_t nv_data = 0, nv_address = 0;
static int      nv_addr_count = 0, nv_bit_count = 0;

static void eeprom_write(uint8_t value) {
    const int org = 1;
    if (value & 0x04) {                                  // CS alto
        if ((value & 0x02) && !(nv_old & 0x02)) {        // fronte del clock
            if (nv_opcode == 0xFF) {
                nv_data = (uint16_t)((nv_data << 1) | (value & 1));
                if (nv_data & 0x04) nv_opcode = nv_data & 0x03;
            } else if (nv_addr_count < 9 - org) {
                nv_address = (uint16_t)(((nv_address << 1) | (value & 1)) & 0xFF);
                nv_addr_count++;
                in_202 = (uint8_t)((in_202 & ~1) | ((eeprom[nv_address] >> 15) & 1));
            } else if (nv_addr_count == 9 - org) {
                if (nv_opcode == 2) {                    // lettura
                    int sh = ((org + 1) * 8) - nv_bit_count - 1;
                    if (sh >= 0)
                        in_202 = (uint8_t)((in_202 & ~1) | ((eeprom[nv_address] >> sh) & 1));
                }
                if (nv_opcode == 1) {                    // scrittura
                    if (!nv_bit_count) eeprom[nv_address] = 0;
                    eeprom[nv_address] = (uint16_t)((eeprom[nv_address] << 1) | (value & 1));
                    in_202 |= 1;
                }
                nv_bit_count++;
            }
        }
    } else {
        nv_opcode = 0xFF; nv_address = 0; nv_data = 0;
        nv_bit_count = 0; nv_addr_count = 0;
    }
    nv_old = value;
}

struct dl2io { int unused; };

static vxt_byte io_in(struct dl2io *d, vxt_word port) {
    (void)d; return (port == 0x201) ? in_201 : in_202;
}
static void io_out(struct dl2io *d, vxt_word port, vxt_byte data) {
    (void)d;
    if (port != 0x202) return;
    if (data & 0x40) {                                   // il ROM consuma le monete
        in_202 &= (uint8_t)~0x3C;
        coin_pending = false;                            // latch servito: una sola moneta
    }
    eeprom_write(data);
}
static vxt_error io_install(struct dl2io *d, vxt_system *s) {
    vxt_system_install_io(s, VXT_GET_PERIPHERAL(d), 0x201, 0x202);
    return VXT_NO_ERROR;
}
static const char *io_name(struct dl2io *d) { (void)d; return "DL2 input + EEPROM"; }

static struct vxt_peripheral *dl2io_create(vxt_allocator *alloc)
VXT_PERIPHERAL_CREATE(alloc, dl2io, {
    PERIPHERAL->install = &io_install;
    PERIPHERAL->name    = &io_name;
    PERIPHERAL->io.in   = &io_in;
    PERIPHERAL->io.out  = &io_out;
})

// ------------------------------------------------------------------- input
// Gamepad -> porte DL2. Mappa da lair2.cpp: 0x201 attivo BASSO
//   b0 UP  b1 DOWN  b2 LEFT  b3 RIGHT  b4 START1  b5 START2  b6 SWORD  b7 SERVICE
// COIN sta invece su 0x202 bit2, attivo ALTO.
// ⭐ Le maschere BUTTON_* dello SPINNERINO sono IDENTICHE alle B_* di CABINATONE
// (LEFT 0x01, RIGHT 0x02, UP 0x04, DOWN 0x08, FIRE 0x10, START 0x20, COIN 0x40):
// verificato in emulation/input.h, non dedotto.
static uint32_t start_hold_until;

static void dl2_read_inputs(uint8_t b) {
    uint32_t now = millis();
    uint8_t v = 0xFF;

    // In vol-mode il firmware sta regolando il volume: direzioni e COIN NON
    // devono arrivare al ROM (identico a ace_read_inputs). Senza questo, tenere
    // COIN per alzare il volume infila anche una moneta, e in partita una
    // direzione involontaria ammazza Dirk.
    if (input_is_vol_mode_active())
        b &= (uint8_t)~(BUTTON_UP | BUTTON_DOWN | BUTTON_LEFT | BUTTON_RIGHT | BUTTON_COIN);

    bool coin = (b & BUTTON_COIN) != 0;

    if (b & BUTTON_UP)    v &= ~0x01;
    if (b & BUTTON_DOWN)  v &= ~0x02;
    if (b & BUTTON_LEFT)  v &= ~0x04;
    if (b & BUTTON_RIGHT) v &= ~0x08;
    if (b & BUTTON_FIRE)  v &= ~0x40;            // SWORD

    // ⛔⛔ QUANTO VA TENUTO START — il numero di CABINATONE QUI NON VALE.
    // La' era 1200 ms, validato offline con la moneta iniettata a t=77 s e lo
    // START a t=82 s (dl2_vxt.c:679), cioe' con **5 secondi** di margine: quando
    // lo START arrivava, il credito era gia' contato da un pezzo.
    // Su SPINNERINO il firmware fa un'altra cosa: UNA pressione del tasto START
    // genera una SEQUENZA (input.cpp:521) — COIN per 80 ms, gap, poi START per
    // 240 ms. Lo START arriva quindi **160 ms dopo la moneta**, mentre il ROM ci
    // mette ~3,2 s a registrare il credito a 1,18 MHz (misura di CABINATONE: il
    // ritardo del credito e' cycle-dependent, 0,42 s a 10 MHz -> 3,21 s a 1,18).
    // Con 1200 ms lo START scade SEMPRE prima che il ROM lo possa guardare.
    // Il valore giusto e' MISURATO col ROM vero da PY\mk_dl2start.py, non stimato.
    if (b & BUTTON_START) start_hold_until = now + DL2_START_HOLD_MS;
    if (now < start_hold_until) v &= ~0x10;      // START1

    // MONETA = LATCH, non un impulso a tempo. Su CABINATONE il bit COIN veniva
    // tenuto alto 400 ms fissi: sbagliato in due modi opposti.
    //  - se il ROM ci mette di piu' a leggerla, la moneta si PERDE;
    //  - se la legge subito (misurato: 0.03 s) si continua a riasserire il bit
    //    per i restanti ~370 ms, e il ROM puo' contare PIU' monete con una sola
    //    pressione.
    // Il gettoniere vero arma un latch che il ROM legge e azzera: qui uguale.
    // Il latch si arma sul RILASCIO, cosi' la fase 5 (volume su COIN+SU/GIU) non
    // deve rimettere mano alla logica della moneta.
    if (!coin && coin_prev) coin_pending = true;       // tap secco = una moneta
    coin_prev = coin;
    if (coin_pending) in_202 |= 0x04;                  // COIN1 (attivo alto)

    in_201 = v;
}

// ================================================================ dl2.meta
// ⚠️ MAPPA FRAME -> CLIP: il pack ha TRE framerate diversi (misurati su tutti i
// file, non dedotti): 209 clip a 23.976, 1 a 29.97 (l'ATTRACT!), 2 a 47.95.
// Il rapporto e' quindi PER CLIP: un rapporto globale 4/5 desincronizzerebbe
// proprio l'attract = la prima cosa che si vede accendendo il cabinato.
//   clip   = ultimo con ld_start <= frame_LD        (ricerca binaria su dl2.meta)
//   indice = round((frame_LD - ld_start) * (fps_n/fps_d) / (30000/1001))
// Formula gia' validata offline su 346 frame realmente cercati dal ROM (346/346).
typedef struct {
    int32_t   ld_start;      // primo frame laserdisc coperto da questo clip
    int32_t   nfr;           // frame nel .mjpg
    int32_t   fps_n, fps_d;  // framerate DI QUESTO clip
    char      name[16];      // "dl2-00721"
    uint32_t *pairs;         // nfr x (off,len) dentro <name>.mjpg — PSRAM, lazy
    uint8_t   bad;           // .idx mancante/corrotto: non ritentare mai
} dl2_clip_t;

// ⭐ IN PSRAM A RUNTIME, non statico: in un binario multi-gioco i buffer di UN
// gioco non devono pesare sulla .bss di TUTTI ([[fm_c_lfo_pm_table_128kb_ram]]).
static dl2_clip_t *g_clip  = nullptr;
static int         g_nclip = 0;
static int         g_frames_tot = 0;

static bool dl2_load_meta(void)
{
    if (!g_clip) {
        g_clip = (dl2_clip_t *)heap_caps_malloc(sizeof(dl2_clip_t) * DL2_MAX_CLIPS,
                                                MALLOC_CAP_SPIRAM);
        if (!g_clip) return false;
    }
    memset(g_clip, 0, sizeof(dl2_clip_t) * DL2_MAX_CLIPS);
    g_nclip = 0; g_frames_tot = 0;

    FILE *f = fopen(DL2_META, "r");
    if (!f) return false;
    char line[128];
    while (g_nclip < DL2_MAX_CLIPS && fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
        dl2_clip_t c;
        memset(&c, 0, sizeof c);
        char nm[32];
        if (sscanf(line, "%d %31s %d %d %d",
                   (int *)&c.ld_start, nm, (int *)&c.nfr,
                   (int *)&c.fps_n, (int *)&c.fps_d) != 5) continue;
        if (c.nfr <= 0 || c.fps_n <= 0 || c.fps_d <= 0) continue;
        strncpy(c.name, nm, sizeof(c.name) - 1);
        g_frames_tot += c.nfr;
        g_clip[g_nclip++] = c;
    }
    fclose(f);
    return g_nclip > 0;
}

// ultimo clip con ld_start <= f (ricerca binaria; dl2.meta e' ordinata: verificato)
static int dl2_find_clip(int f)
{
    if (g_nclip <= 0 || f < g_clip[0].ld_start) return -1;
    int lo = 0, hi = g_nclip - 1;
    while (lo < hi) {
        int mid = (lo + hi + 1) >> 1;
        if (g_clip[mid].ld_start <= f) lo = mid; else hi = mid - 1;
    }
    return lo;
}

// frame laserdisc -> indice dentro il clip. int64: (2600 * 48000 * 1001) sfora i 32 bit.
static int dl2_local_index(const dl2_clip_t *c, int f)
{
    int64_t d = (int64_t)f - c->ld_start; if (d < 0) d = 0;
    int64_t num = d * c->fps_n * DISC_FPS_D;
    int64_t den = (int64_t)c->fps_d * DISC_FPS_N;
    int     ix  = (int)((num + den / 2) / den);          // round, non floor
    if (ix < 0) ix = 0;
    if (ix >= c->nfr) ix = c->nfr - 1;
    return ix;
}

// .idx caricato alla PRIMA richiesta del clip e tenuto per sempre (PSRAM).
// Tutti insieme sarebbero 35930*8 = 281 KB: caricarli al boot costerebbe 212
// fopen su una directory da 637 file, e l'avvio del ROM e' gia' lento di suo.
// Formato (build_dl2_video_176x240.py): [u32 nfr][ (off,len) x nfr ].
static bool dl2_clip_idx(dl2_clip_t *c)
{
    if (c->pairs) return true;
    if (c->bad)   return false;
    char p[64];
    snprintf(p, sizeof p, DL2_DIR "/%s.idx", c->name);
    FILE *f = fopen(p, "rb");
    if (!f) { c->bad = 1; return false; }
    uint32_t nfr = 0;
    if (fread(&nfr, 4, 1, f) != 1 || nfr == 0 || nfr > 4096) {
        fclose(f); c->bad = 1; return false;
    }
    uint32_t *pr = (uint32_t *)heap_caps_malloc((size_t)nfr * 8, MALLOC_CAP_SPIRAM);
    if (!pr || fread(pr, 8, nfr, f) != nfr) {
        if (pr) heap_caps_free(pr);
        fclose(f); c->bad = 1; return false;
    }
    fclose(f);
    if ((int)nfr < c->nfr) c->nfr = (int)nfr;   // l'.idx vero vince su dl2.meta
    c->pairs = pr;
    return true;
}

// ---- cache LRU di .mjpg APERTI ----
// Senza, ogni cambio clip rifa la fopen, che su SD scandisce la directory (637
// file in /dl2) = freeze di secondi. Stessa cura di DL (OpenCache) e Space Ace.
// ⚠️ CHIAMATA SOLO DAL TASK VIDEO: le FILE* non sono thread-safe.
static FILE     *g_ocf[DL2_OC_N];
static int       g_ocw[DL2_OC_N];
static uint32_t  g_ocu[DL2_OC_N], g_octick;
// posizione corrente dentro il file, per NON rifare la fseek quando la lettura
// e' gia' sequenziale (i frame sono CONTIGUI): su CABINATONE la fseek per-frame
// costava una lseek FATFS + il buffer stdio buttato. -1 = sconosciuta.
static long      g_ocoff[DL2_OC_N];

static int dl2_open_mjpg(int ci)
{
    g_octick++;
    for (int i = 0; i < DL2_OC_N; i++)
        if (g_ocf[i] && g_ocw[i] == ci) { g_ocu[i] = g_octick; return i; }
    int lru = 0;                                  // miss: slot libera, altrimenti LRU
    for (int i = 0; i < DL2_OC_N; i++) {
        if (!g_ocf[i]) { lru = i; break; }
        if (g_ocu[i] < g_ocu[lru]) lru = i;
    }
    if (g_ocf[lru]) fclose(g_ocf[lru]);
    char p[64];
    snprintf(p, sizeof p, DL2_DIR "/%s.mjpg", g_clip[ci].name);
    g_ocf[lru]   = fopen(p, "rb");
    g_ocw[lru]   = g_ocf[lru] ? ci : -1;
    g_ocu[lru]   = g_octick;
    g_ocoff[lru] = -1;                            // posizione ancora sconosciuta
    return g_ocf[lru] ? lru : -1;
}

// ================================================================ AUDIO
// ---- 1. PC SPEAKER ----
// libvxt chiama questa callback SOLO quando la porta 0x61 cambia stato (non per
// campione), passando la frequenza del canale 2 del PIT. Ce la teniamo e l'onda
// quadra la generiamo noi con un accumulatore INTERO: `vxtu_ppi_generate_sample`
// avrebbe fatto una divisione double PER OGNI CAMPIONE (32000 al secondo), e sul
// P4 i double sono software ([[esp32p4_no_hardware_double]]) — e' la stessa
// lezione che sul PIT valeva il 51% del tempo di emulazione.
static volatile uint32_t g_spk_hz = 0;

static void dl2_speaker_cb(struct vxt_peripheral *p, double hz, void *ud)
{
    (void)p; (void)ud;
    g_spk_hz = (hz > 20.0 && hz < 20000.0) ? (uint32_t)hz : 0;
}

// ---- 2. MUSICA/VOCE DELLA SCENA (<clip>.pcm) ----
// Un .pcm per clip, s16le stereo 32 kHz. Il campione corrispondente al frame
// locale `ix` si ricava dal FPS DI QUEL CLIP (il pack ne ha tre diversi):
//     sample = ix * fps_den * 32000 / fps_num
typedef struct { int ci; FILE *fp; long off; uint32_t lru; } dl2_aslot_t;
static dl2_aslot_t g_a[DL2_AC_N];
static uint32_t    g_a_tick;
static uint8_t     g_a_none[DL2_MAX_CLIPS];   // clip senza .pcm: non ritentare la fopen

static int16_t *g_apf     = nullptr;          // buffer di prefetch (RAM INTERNA)
static int      g_apf_ci  = -1;               // clip a cui appartiene
static int64_t  g_apf_pos = -1;               // primo campione contenuto
static int      g_apf_len;

static inline int64_t dl2_frame_to_sample(const dl2_clip_t *c, int ix)
{
    if (ix < 0) ix = 0;
    return (int64_t)ix * c->fps_d * 32000 / c->fps_n;
}

static dl2_aslot_t *dl2_apcm_get(int ci)
{
    if (ci < 0 || ci >= g_nclip || g_a_none[ci]) return nullptr;
    for (int i = 0; i < DL2_AC_N; i++)
        if (g_a[i].fp && g_a[i].ci == ci) { g_a[i].lru = ++g_a_tick; return &g_a[i]; }
    int vi = 0;
    for (int i = 0; i < DL2_AC_N; i++) {
        if (!g_a[i].fp) { vi = i; break; }
        if (g_a[i].lru < g_a[vi].lru) vi = i;
    }
    dl2_aslot_t *s = &g_a[vi];
    if (s->fp) { fclose(s->fp); s->fp = nullptr; }
    char p[64];
    snprintf(p, sizeof p, DL2_DIR "/%s.pcm", g_clip[ci].name);
    FILE *f = fopen(p, "rb");
    if (!f) { g_a_none[ci] = 1; return nullptr; }   // una fopen fallita costa una
    s->fp = f; s->ci = ci; s->off = 0;              // scan directory: MAI in loop
    s->lru = ++g_a_tick;
    return s;
}

// ⚠️ BLOCCHI GROSSI, MAI 256 campioni per volta. Lezione di Space Ace: il task
// audio leggeva 1 KB ogni 8 ms e collideva col video sul lock FATFS -> underrun
// I2S (audio che gracchia) e video fermo. Il problema era la FREQUENZA degli
// accessi, non il volume. Qui si legge 16 KB ogni 128 ms: la SD si tocca 16
// volte meno e il buffer assorbe il jitter di una collisione col task video.
static void dl2_apcm_fetch(int ci, int64_t pos, int16_t *dst, int n)
{
    if (!g_apf) { memset(dst, 0, (size_t)n * 4); return; }
    if (g_apf_ci != ci || g_apf_pos < 0 || pos < g_apf_pos || pos + n > g_apf_pos + g_apf_len) {
        dl2_aslot_t *s = dl2_apcm_get(ci);
        if (!s) { memset(dst, 0, (size_t)n * 4); return; }
        long off = (long)(pos * 4);
        if (off != s->off) { fseek(s->fp, off, SEEK_SET); s->off = off; }
        size_t got = fread(g_apf, 4, DL2_APF_N, s->fp);
        s->off += (long)got * 4;
        if (got < (size_t)DL2_APF_N)                       // oltre la fine del clip
            memset(&g_apf[2 * got], 0, ((size_t)DL2_APF_N - got) * 4);
        g_apf_ci = ci; g_apf_pos = pos; g_apf_len = DL2_APF_N;
    }
    int off = (int)(pos - g_apf_pos);
    memcpy(dst, &g_apf[2 * off], (size_t)n * 4);
}

static void dl2_close_all(void)
{
    for (int i = 0; i < DL2_OC_N; i++) {
        if (g_ocf[i]) fclose(g_ocf[i]);
        g_ocf[i] = nullptr; g_ocw[i] = -1; g_ocu[i] = 0; g_ocoff[i] = -1;
    }
    for (int i = 0; i < DL2_AC_N; i++) {
        if (g_a[i].fp) fclose(g_a[i].fp);
        g_a[i].fp = nullptr; g_a[i].ci = -1; g_a[i].off = 0; g_a[i].lru = 0;
    }
    if (g_apf) { heap_caps_free(g_apf); g_apf = nullptr; }
    g_apf_ci = -1; g_apf_pos = -1; g_apf_len = 0;
    if (g_clip) {
        for (int i = 0; i < g_nclip; i++)
            if (g_clip[i].pairs) { heap_caps_free(g_clip[i].pairs); g_clip[i].pairs = nullptr; }
    }
}

// ================================================================ schermo
static uint8_t *g_in  = nullptr;    // bitstream JPEG (PSRAM)
static uint8_t *g_out = nullptr;    // frame 176x240 RGB565 (PSRAM)
// ⛔ NON statico: un oggetto JPEGDEC pesa **17.884 byte di .bss** (misurato con
// `nm` sull'oggetto compilato, non stimato). In un binario multi-gioco quella
// RAM la pagherebbero TUTTI i giochi per sempre, anche chi non entra mai in DL2
// ([[fm_c_lfo_pm_table_128kb_ram]]: 197 KB di .bss avevano ucciso il PC Engine).
// Allocato entrando nel player e liberato uscendo. Resta in RAM INTERNA: e' il
// buffer di lavoro del decoder, in PSRAM rallenterebbe ogni frame.
// NB: laserdisc.cpp e spaceace.cpp ne hanno ancora uno STATICO ciascuno (~36 KB
// in due) — stessa cura applicabile, ma non si toccano giochi che funzionano.
static JPEGDEC *g_jdec = nullptr;

static void ld_window(bool full) {
    video.waitDMA();
    if (full) video.setAddrWindow(TFT_HW_X, 0,        TFT_FB_W, LD_FB_H);
    else      video.setAddrWindow(TFT_HW_X, TFT_HW_Y, TFT_FB_W, TFT_FB_H);
}

// N righe centrate nella finestra del player. Il pannello e' montato ruotato:
// la riga di testo corre lungo l'asse da 240 px, e Y[k] la posiziona lungo
// l'asse da 320 px. Stessa geometria di ld_screen() in spaceace.cpp.
//
// ⚠️ COSTO: per ogni striscia di 8 righe solo UNA colonna di 8 pixel di ciascun
// carattere puo' finirci dentro (il testo e' ruotato di 90 gradi), quindi invece
// di provare tutti i caratteri si calcola direttamente QUALI 8 colonne cadono
// nella striscia: stessi pixel, ~30x meno lavoro
// ([[feedback_no_costly_calls_in_render]]).
static void dl2_lines(const char *const *L, const uint16_t *C, const int *Y, int n)
{
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        for (int k = 0; k < n; k++) {
            const char *s = L[k];
            if (!s || !*s) continue;
            const int len = (int)strlen(s);
            int x0 = (LD_FB_H - len * 8) / 2;
            if (x0 < 4) x0 = 4;
            // y_l = (LD_FB_H-1) - p, con p = indice del pixel lungo la riga di
            // testo. Serve y_l in [sy, sy+8) => p in [LD_FB_H-8-sy, LD_FB_H-1-sy].
            const int p_lo = LD_FB_H - 8 - sy, p_hi = LD_FB_H - 1 - sy;
            for (int p = p_lo; p <= p_hi; p++) {
                const int q = p - x0;
                if (q < 0 || q >= len * 8) continue;
                const int idx  = char_to_idx(s[q >> 3]);
                const int c    = q & 7;
                const int y_in = (LD_FB_H - 1 - p) - sy;      // 0..7
                uint16_t *row_px = g_strip + y_in * TFT_FB_W;
                for (int r = 0; r < 8; r++) {
                    unsigned char row = pgm_read_byte(&FONT8[idx][r]);
                    if (!(row & (0x80 >> c))) continue;
                    const int x_l = Y[k] + r;
                    if (x_l < 0 || x_l >= TFT_FB_W) continue;
                    row_px[x_l] = C[k];
                }
            }
        }
        video.write(g_strip, TFT_FB_W * 8);
    }
}

// ---- overlay ruotato DENTRO il frame (il 176x240 e' pre-ruotato) — da ace ----
static inline void ld_px_vis(uint16_t *img, int X, int Y, uint16_t col) {
    if ((unsigned)X >= 240u || (unsigned)Y >= 176u) return;
    img[(size_t)(239 - X) * LD_W + Y] = col;
}
static void ld_text_vis(uint16_t *img, int x, int y, const char *s, uint16_t col) {
    for (int i = 0; s[i]; i++) {
        int idx = char_to_idx(s[i]);
        for (int r = 0; r < 8; r++) {
            unsigned char row = pgm_read_byte(&FONT8[idx][r]);
            for (int c = 0; c < 8; c++)
                if (row & (0x80 >> c)) ld_px_vis(img, x + i * 8 + c, y + r, col);
        }
    }
}

// -------------------------------------------------- barra volume (IDENTICA a DL/ace)
// Video letterboxed: 176px centrati in 320 -> bande nere ai window cols 0..71 e
// 248..319. ld_px_vis copre solo l'area video, quindi la barra si scrive DIRETTA
// in g_strip dentro ld_blit, nella banda ALTA.
#define VOLBAR_LEN   112
#define VOLBAR_ROW0  ((240 - VOLBAR_LEN) / 2)
#define VOLBAR_COL0  3
#define VOLBAR_THK   16
#define VOLBAR_RED   10
static int g_volbar_fill = -1;                  // -1 = off; 0..VOLBAR_LEN

static void ld_blit(void) {
    const uint16_t *img = (const uint16_t *)g_out;
    for (int s = 0; s < LD_FB_H / 8; s++) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        for (int i = 0; i < 8; i++) {
            int src_y = s * 8 + i; if (src_y < 0 || src_y >= LD_H) continue;
            memcpy(g_strip + i * TFT_FB_W + LD_X_OFF,
                   img + (size_t)src_y * LD_W, (size_t)LD_W * sizeof(uint16_t));
        }
        if (g_volbar_fill >= 0) {
            for (int i = 0; i < 8; i++) {
                int pos = (s * 8 + i) - VOLBAR_ROW0;
                if (pos < 0 || pos >= VOLBAR_LEN) continue;
                int asx = VOLBAR_LEN - 1 - pos;
                for (int c = 0; c < VOLBAR_THK; c++) {
                    uint16_t cc;
                    if (asx == 0 || asx == VOLBAR_LEN - 1 || c == 0 || c == VOLBAR_THK - 1) cc = 0xFFFF;
                    else if (asx < g_volbar_fill && asx >= VOLBAR_LEN - VOLBAR_RED)         cc = 0x00F8;
                    else if (asx < g_volbar_fill)                                          cc = 0xE007;
                    else                                                                    cc = 0x0000;
                    g_strip[i * TFT_FB_W + VOLBAR_COL0 + c] = cc;
                }
            }
        }
        video.write(g_strip, TFT_FB_W * 8);
    }
}

static void dl2_update_volbar(void) {
    if (!input_is_vol_mode_active()) { g_volbar_fill = -1; return; }
    int f = (VOLBAR_LEN * (31 - laserdisc_current_volume())) / 30;
    if (f < 0) f = 0; else if (f > VOLBAR_LEN) f = VOLBAR_LEN;
    g_volbar_fill = f;
}

// ================================================================ decode + frame
static int dl2_jpeg_draw(JPEGDRAW *p) {
    uint16_t *dst = (uint16_t *)g_out;
    for (int row = 0; row < p->iHeight; row++) {
        int y = p->y + row; if ((unsigned)y >= (unsigned)LD_H) continue;
        int x0 = p->x; if (x0 < 0 || x0 >= LD_W) continue;
        int w = p->iWidth; if (x0 + w > LD_W) w = LD_W - x0;
        if (w > 0) memcpy(dst + (size_t)y * LD_W + x0,
                          p->pPixels + (size_t)row * p->iWidth, (size_t)w * 2);
    }
    return 1;
}

// ============================ AIUTI GRAFICI: FRECCIA / SPADA
// Il ROM scrive esattamente 5 parole, sempre su riga 2 e riempite di spazi
// (misurato su una partita intera nel simulatore: SWORD 113, RIGHT 84, LEFT 70,
// DOWN 26, UP 20 su 313 mosse). NESSUNA DIAGONALE.
//
// ⚠️ RISCRITTO IN ARITMETICA INTERA, non copiato da `dl_arrow_in` di
// laserdisc.cpp: quella usa i `double` per normalizzare le diagonali, che qui
// non esistono. Sul P4 i double sono software ([[esp32p4_no_hardware_double]]) e
// questa funzione gira ~18.600 volte per simbolo (41x41 pixel x 5 test) = un
// millisecondo buttato a ogni frame. Non servendo le diagonali, il fattore di
// normalizzazione e' sempre 1: asta `4|s| < R`, punta `20|s| < 19(R-t)`.
#if DL2_MOVE_SYMBOLS
enum { MV_NONE = 0, MV_UP, MV_DOWN, MV_LEFT, MV_RIGHT, MV_SWORD };

// la riga di testo e' una mossa? (confronto senza spazi)
static int dl2_move_of(const char *s)
{
    char w[12]; int n = 0;
    for (const char *c = s; *c && n < 11; c++) if (*c != ' ') w[n++] = *c;
    w[n] = 0;
    if (!strcmp(w, "UP"))    return MV_UP;
    if (!strcmp(w, "DOWN"))  return MV_DOWN;
    if (!strcmp(w, "LEFT"))  return MV_LEFT;
    if (!strcmp(w, "RIGHT")) return MV_RIGHT;
    if (!strcmp(w, "SWORD")) return MV_SWORD;
    return MV_NONE;
}

// pieno della FRECCIA verso (dx,dy) — asta + punta triangolare, solo cardinali
static inline bool dl2_arrow_in(int x, int y, int R, int dx, int dy)
{
    int t = x * dx + y * dy;          // >0 = verso la punta
    int s = x * dy - y * dx;          // distanza dall'asse
    int a = s < 0 ? -s : s;
    if (t >= -R && t <= 0) return (4 * a < R);                 // asta (larga R/4)
    if (t > 0 && t <= R)   return (20 * a < 19 * (R - t));     // punta (0.95)
    return false;
}

// pieno della SPADA: lama verticale + guardia + impugnatura (come DL)
static inline bool dl2_sword_in(int x, int y, int R)
{
    if (x >= -3       && x <= 3       && y >= -R    && y <= R - 9) return true;
    if (x >= -(R - 8) && x <= (R - 8) && y >=  2    && y <= 7)     return true;
    if (x >= -6       && x <= 6       && y >= R - 9 && y <= R - 3) return true;
    return false;
}

// contorno ~2px: pixel interno che ha un vicino esterno entro 2px
static void dl2_symbol(uint16_t *b, int cx, int cy, int R, int mv, uint16_t col)
{
    int dx = 0, dy = 0;
    switch (mv) {
        case MV_UP:    dy = -1; break;
        case MV_DOWN:  dy =  1; break;
        case MV_LEFT:  dx = -1; break;
        case MV_RIGHT: dx =  1; break;
        case MV_SWORD: break;
        default: return;
    }
    for (int y = -R; y <= R; y++)
        for (int x = -R; x <= R; x++) {
            bool in = (mv == MV_SWORD) ? dl2_sword_in(x, y, R)
                                       : dl2_arrow_in(x, y, R, dx, dy);
            if (!in) continue;
            bool bd = false;
            for (int k = 1; k <= 2 && !bd; k++) {
                if (mv == MV_SWORD)
                    bd = !dl2_sword_in(x-k,y,R) || !dl2_sword_in(x+k,y,R) ||
                         !dl2_sword_in(x,y-k,R) || !dl2_sword_in(x,y+k,R);
                else
                    bd = !dl2_arrow_in(x-k,y,R,dx,dy) || !dl2_arrow_in(x+k,y,R,dx,dy) ||
                         !dl2_arrow_in(x,y-k,R,dx,dy) || !dl2_arrow_in(x,y+k,R,dx,dy);
            }
            if (bd) ld_px_vis(b, cx + x, cy + y, col);
        }
}
#endif // DL2_MOVE_SYMBOLS

// ---- HUD in cima al video, impaginazione identica a dl_draw_scoreboard di
//      Dragon's Lair (x=4 y=3 scala 1, DENTRO il frame). Il VOLUME non si
//      disegna qui: su SPINNERINO ci pensa la BARRA nella banda nera (ld_blit),
//      che e' come si comportano gia' DL e Space Ace.
#if DL2_HUD
static void dl2_draw_hud(uint16_t *dst)
{
    char line[40];
    if (g_hud_ingame)
        snprintf(line, sizeof line, "%06lu  DIRK %d  CR %02d",
                 (unsigned long)g_hud_score, g_hud_lives, g_hud_credit);
    else if (g_hud_credit > 0)
        snprintf(line, sizeof line, "CREDIT %02d  PRESS START", g_hud_credit);
    else if (g_hud_played)
        snprintf(line, sizeof line, "GAME OVER  CREDIT %02d", g_hud_credit);
    else
        snprintf(line, sizeof line, "CREDIT %02d  INSERT COIN", g_hud_credit);
    ld_text_vis(dst, 4, 3, line, 0xFFFF);
}
#endif

// ---- le 3 righe dell'overlay LDP-1450, disegnate DENTRO il frame ----
// E' qui che DL2 mostra titolo, INSERT COIN, LEVEL/LIVES e — cosa che interessa
// molto — il PROMPT DELLA MOSSA ("LEFT", "SWORD"...). Essendo ROM-driven, gli
// aiuti sono gratis: li disegna il gioco stesso.
// Posizione = formula vera di daphne ldp1000.cpp su superficie 640x480,
// riscalata sull'area visibile 240x176 di SPINNERINO:
//   x = 20 + tx*7        y = 20 + ty*5 + riga*38
// Con le posizioni realmente usate da DL2 (X=0x20 sempre, Y=0x1E titolo,
// Y=0x2F INSERT COIN / LEVEL / prompt mosse) le tre righe cadono a y = 62/76/90
// e 93/107/121: distanziate 14 px, font 8 px -> non si sovrappongono.
static inline int dl2_txt_x(void)       { return (20 + ldp.tx * 7) * 240 / 640; }
static inline int dl2_txt_y(int line)   { return (20 + ldp.ty * 5 + line * 38) * 176 / 480; }

static void dl2_draw_ldp_text(uint16_t *img)
{
    if (!ldp.text_on) return;
    char l[16];
    for (int i = 0; i < 3; i++) {
        strncpy(l, ldp.text[i], sizeof l - 1);   // copia locale: le scrive l'altro core
        l[sizeof l - 1] = 0;
        if (!l[0]) continue;
#if DL2_MOVE_SYMBOLS
        // se la riga E' una mossa, al suo posto va il SIMBOLO (stile DL).
        // Le altre righe (LEVEL, LIVES, CREDIT, titolo...) restano testo.
        int mv = dl2_move_of(l);
        if (mv != MV_NONE) {
            dl2_symbol(img, DL2_HINT_CX, DL2_HINT_CY, DL2_HINT_R, mv, 0xFFFF);
            continue;
        }
#endif
#if DL2_HUD
        // LEVEL / n LIVES / CREDIT n e il marquee "INSERT ... COIN(S)" non si
        // disegnano piu' in mezzo al filmato: quei valori sono nell'HUD in cima.
        // Titolo, tabella record e iniziali (TL_ALTRO) restano come sono.
        if (dl2_text_class(l, NULL) != TL_ALTRO) continue;
#endif
        ld_text_vis(img, dl2_txt_x(), dl2_txt_y(i), l, 0xFFFF);
    }
}

// Mostra il frame laserdisc `f`. Ritorna true se ha disegnato.
static bool dl2_show(int f)
{
    int ci = dl2_find_clip(f);
    if (ci < 0) { g_e_clip++; return false; }
    dl2_clip_t *c = &g_clip[ci];
    if (!dl2_clip_idx(c)) { g_e_idx++; return false; }
    int local = dl2_local_index(c, f);
    if (local < 0 || local >= c->nfr) { g_e_range++; return false; }

    video.waitDMA();                            // reap SPI DMA prima di toccare la SD
    int sl = dl2_open_mjpg(ci);
    if (sl < 0) { g_e_read++; return false; }

    uint32_t off = c->pairs[2 * local], len = c->pairs[2 * local + 1];
    if (!len || len > LD_IN_MAX) { g_e_range++; return false; }
    // i frame sono CONTIGUI: in riproduzione sequenziale la fseek e' un no-op che
    // costa comunque una lseek FATFS e butta il buffer stdio (lezione CABINATONE).
    if (g_ocoff[sl] != (long)off) {
        if (fseek(g_ocf[sl], (long)off, SEEK_SET) != 0) { g_ocoff[sl] = -1; g_e_read++; return false; }
    }
    if (fread(g_in, 1, len, g_ocf[sl]) != len) { g_ocoff[sl] = -1; g_e_read++; return false; }
    g_ocoff[sl] = (long)(off + len);

    int ok = 0;
    if (g_jdec->openRAM(g_in, (int)len, dl2_jpeg_draw)) {
        g_jdec->setPixelType(RGB565_BIG_ENDIAN);
        ok = g_jdec->decode(0, 0, 0);
        g_jdec->close();
    }
    if (!ok) { g_e_dec++; return false; }

    dl2_draw_ldp_text((uint16_t *)g_out);
#if DL2_HUD
    dl2_draw_hud((uint16_t *)g_out);
#endif
    dl2_update_volbar();                        // il fill; la barra la disegna ld_blit
    ld_blit();
    return true;
}

// ================================================================ diagnostica
static const uint16_t C_YELLOW = 0xE0FF;   // byte-swapped, come DL/Space Ace
static const uint16_t C_CYAN   = 0xFF07;
static const uint16_t C_GREEN  = 0xE007;
static const uint16_t C_GREY   = 0xEF7B;
static const uint16_t C_RED    = 0x00F8;

static const int DL2_DIAG_Y[10] = { 16, 52, 76, 100, 124, 156, 180, 204, 236, 288 };

// Pagina di stato. Con DL2_VIDEO 0 e' cio' che si vede al posto del filmato; con
// DL2_VIDEO 1 la disegna solo la RETE DI SICUREZZA (nessun frame da oltre 1 s).
// ⭐ E' proprio la lezione di CABINATONE: lo strumento NON deve spegnersi nel
// guasto che misura. Li' il tachimetro era dentro il frame video e a video fermo
// si congelava, per questo non si e' mai riusciti a leggere EMU in partita.
static void dl2_diag_page(bool packok)
{
    char l1[32], l2[32], l3[32], l4[32], t[3][32], l9[32];
    snprintf(l1, sizeof l1, "LDP %-5s F %05d",
             ldp.state == LDP_PLAY ? "PLAY" :
             (ldp.state == LDP_PAUSE ? "PAUSE" : "STOP"), ldp.frame);
    snprintf(l2, sizeof l2, "SEEK %05d %-8s", ldp.seek_target, ldp.last_cmd);
    snprintf(l3, sizeof l3, "EMU %lu BOOT %lu",
             (unsigned long)g_emu_khz, (unsigned long)g_boot_khz);
    snprintf(l4, sizeof l4, "RAM %luK PS %luK",
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             (unsigned long)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
    for (int i = 0; i < 3; i++) snprintf(t[i], sizeof t[i], "%s", ldp.text[i]);
    // ⭐ i modi di fallire, contati: su CABINATONE hanno chiuso in UN flash una
    // caccia in cui erano gia' fallite 6 ipotesi. CL=clip non trovato IX=idx
    // RG=fuori range RD=lettura DE=decode. Devono restare a ZERO.
    snprintf(l9, sizeof l9, "FPS %lu E %lu%lu%lu%lu%lu",
             (unsigned long)g_v_fps, (unsigned long)g_e_clip, (unsigned long)g_e_idx,
             (unsigned long)g_e_range, (unsigned long)g_e_read, (unsigned long)g_e_dec);

    const char *L[10] = { "DRAGONS LAIR II", l1, l2, l3, l4,
                          t[0], t[1], t[2], l9,
                          packok ? "START 3S MENU" : "PACK VIDEO MANCANTE" };
    const uint16_t C[10] = { C_YELLOW, C_CYAN, C_CYAN, C_GREEN, C_GREEN,
                             C_YELLOW, C_YELLOW, C_YELLOW, C_GREEN,
                             packok ? C_GREY : C_RED };
    dl2_lines(L, C, DL2_DIAG_Y, 10);
}

// Testo SCALATO nella strip (mappa ruotata come dl2_lines). IDENTICA a
// ld_line_strip di spaceace.cpp e a dl_select_difficulty di laserdisc.cpp:
// x_l = y_p + r*scale, y_l = 239 - x_p.
static void dl2_line_strip(int sy, const char *s, int y_p, int scale, uint16_t col)
{
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

// ---- schermata di CARICAMENTO con barra di avanzamento ----
// Il ROM ci mette ~43 s a bootare e finche' non fa il primo SEARCH non c'e'
// niente da mostrare. Prima al suo posto c'era la pagina diagnostica: informativa
// ma incomprensibile. Qui c'e' una barra come negli altri player.
// ⭐ L'avanzamento e' VERO: cicli emulati / cicli che il boot costa (misurati).
// Se la scheda e' piu' lenta la barra ci mette di piu', ma non "bara" mai.
static void dl2_loading_screen(void)
{
    uint32_t kc  = g_emu_kc;
    int      pct = (int)((uint64_t)kc * 100 / DL2_BOOT_KCYCLES);
    if (pct > 99) pct = 99;                    // il 100% lo da' il primo SEARCH

    char l3[24];
    snprintf(l3, sizeof l3, "%d%%", pct);

    // La barra corre lungo l'asse da 240 px, alla riga Y=210 dell'asse da 320.
    const int BX = 20, BW = 200, BY = 210, BH = 10;
    const int riemp = BW * pct / 100;

    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        // ⭐ Titolo GRANDE e giallo (scala 3), come le schermate di DL/Space Ace.
        // "DRAGONS LAIR" e "II" su due righe: a scala 3 una riga da 15 caratteri
        // sarebbe 360 px contro i 240 disponibili, quindi non ci starebbe.
        dl2_line_strip(sy, "DRAGONS LAIR", 96,  2, C_YELLOW);
        dl2_line_strip(sy, "II",           130, 3, C_YELLOW);
        dl2_line_strip(sy, "CARICAMENTO",  180, 1, C_CYAN);
        dl2_line_strip(sy, l3,             240, 1, C_GREY);
        // barra: cornice bianca + riempimento verde, sull'asse lungo
        for (int i = 0; i < 8; i++) {
            const int pos = (sy + i);                      // 0..239 = asse lungo
            const int off = (LD_FB_H - 1 - pos) - BX;      // 0..BW dentro la barra
            if (off < 0 || off >= BW) continue;
            uint16_t *row_px = g_strip + i * TFT_FB_W;
            for (int t = 0; t < BH; t++) {
                const int x_l = BY + t;
                if (x_l < 0 || x_l >= TFT_FB_W) continue;
                const bool bordo = (off == 0 || off == BW - 1 || t == 0 || t == BH - 1);
                row_px[x_l] = bordo ? 0xFFFF : (off < riemp ? C_GREEN : 0x0000);
            }
        }
        video.write(g_strip, TFT_FB_W * 8);
    }
}

// ============================================================ SCELTA DIFFICOLTA'
// Impaginazione, scale e colori IDENTICI a DL e Space Ace (titolo scala 2 giallo,
// "SCEGLI DIFFICOLTA" scala 1 ciano, voci bianche, uscita grigia; conferma
// "MODALITA" scala 2 bianco + modalita' scala 3 verde).
// ⚠️ DL2 NON ha una scelta difficolta' nel ROM: le vite di partenza sono 10,
// fissate nell'EEPROM. FACILE non e' quindi un'impostazione del gioco ma un
// CHEAT nostro (vedi DL2_EASY_LIVES in dl2_rom_task).
static volatile bool g_easy = false;
static bool ld_want_exit(uint8_t b);      // definita piu' sotto (uscita al menu)

static void dl2_diff_screen(void)
{
    for (int sy = 0; sy < LD_FB_H; sy += 8) {
        memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
        dl2_line_strip(sy, "DRAGONS LAIR II",   92,  2, C_YELLOW);
        dl2_line_strip(sy, "SCEGLI DIFFICOLTA", 138, 1, C_CYAN);
        dl2_line_strip(sy, "SU = FACILE",       166, 1, 0xFFFF);
        dl2_line_strip(sy, "GIU/FIRE = NORMALE",184, 1, 0xFFFF);
        dl2_line_strip(sy, "START 3s = ESCI",   208, 1, C_GREY);
        video.write(g_strip, TFT_FB_W * 8);
    }
}

// SU = FACILE (vite illimitate) · GIU/FIRE = NORMALE (10 vite, come l'EEPROM).
// Ritorna true se si gioca, false se si esce (START 3s). Stesso flusso di ace.
static bool dl2_select_difficulty(void)
{
    bool released = false;
    for (;;) {
        uint8_t b = g_input->buttons_get();
        // il FIRE con cui si e' entrati nel gioco non deve scegliere da solo
        if (!(b & (BUTTON_FIRE | BUTTON_START | BUTTON_UP | BUTTON_DOWN))) released = true;
        dl2_diff_screen();
        if (ld_want_exit(b)) return false;                  // START 3s -> menu
        if (released && (b & BUTTON_UP))                     { g_easy = true;  break; }
        if (released && (b & (BUTTON_DOWN | BUTTON_FIRE)))   { g_easy = false; break; }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
    {   // CONFERMA: modalita' in VERDE, grande e centrata (identico a DL/ace)
        const char *mode = g_easy ? "FACILE" : "NORMALE";
        for (int sy = 0; sy < LD_FB_H; sy += 8) {
            memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
            dl2_line_strip(sy, "MODALITA", 96,  2, 0xFFFF);
            dl2_line_strip(sy, mode,       140, 3, C_GREEN);
            video.write(g_strip, TFT_FB_W * 8);
        }
        vTaskDelay(pdMS_TO_TICKS(1200));
    }
    while (g_input->buttons_get() & (BUTTON_UP | BUTTON_DOWN | BUTTON_FIRE | BUTTON_START))
        vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

static void dl2_message(const char *l1, const char *l2, uint16_t col)
{
    const char *L[3] = { "DRAGONS LAIR II", l1, l2 };
    const uint16_t C[3] = { C_YELLOW, col, C_GREY };
    const int Y[3] = { 130, 160, 185 };
    dl2_lines(L, C, Y, 3);
}

// ---------------------------------------------------------------- uscita (= spaceace.cpp)
static bool ld_want_exit(uint8_t b) {
    static uint32_t t0 = 0;
    if (input_consume_back_to_menu()) { t0 = 0; return true; }
    if (b & BUTTON_START) {
        uint32_t now = millis();
        if (t0 == 0) t0 = now ? now : 1;
        else if (now - t0 >= 3000) { t0 = 0; return true; }
    } else t0 = 0;
    return false;
}

// ================================================================ TASK AUDIO (core0)
// Struttura presa da ace_audio_task: blocchi da 256 campioni, `laserdisc_audio_write`
// BLOCCANTE = il clock dei 32 kHz, volume del firmware + soft-limiter.
// Le due sorgenti hanno costi di natura OPPOSTA, ed e' per questo che
// DL2_AUDIO_SCENE esiste come interruttore separato:
//   PC SPEAKER = solo aritmetica, NON tocca la SD
//   SCENA .pcm = legge dalla SD, quindi CONTENDE col task video sul lock FATFS
static volatile bool g_aud_running = false;

static void dl2_audio_task(void *arg)
{
    (void)arg;
    laserdisc_audio_begin();
    static int16_t buf[256 * 2];      // uscita mixata
    static int16_t sc[256 * 2];       // scena
    uint32_t spk_phase = 0;
    int64_t  pos = 0;
    int      pos_ci = -1;

    while (!g_stop) {
        // --- scena: segue il frame che il ROM sta mostrando ---
        int f  = g_ld_frame;
        int ci = (f >= 0) ? dl2_find_clip(f) : -1;
        // `pairs` lo alloca il TASK VIDEO (dl2_clip_idx): qui si legge soltanto.
        // Se non c'e' ancora, si resta muti per un blocco e basta.
        bool playing = (ldp.state == LDP_PLAY) && ci >= 0 && g_clip && g_clip[ci].pairs;

#if !DL2_AUDIO_SCENE
        (void)playing; (void)pos; (void)pos_ci;
        memset(sc, 0, sizeof sc);              // bisezione: audio di scena escluso
#else
        if (!playing) {
            memset(sc, 0, sizeof sc);          // disco fermo = nessun audio di scena
            pos_ci = -1;                       // al riavvio si risincronizza
        } else {
            int64_t want = dl2_frame_to_sample(&g_clip[ci], dl2_local_index(&g_clip[ci], f));
            // risincronizza solo su cambio clip o salto vero (SEARCH); altrimenti
            // lettura sequenziale = nessun fseek per blocco
            int64_t d = pos - want; if (d < 0) d = -d;
            if (ci != pos_ci || d > 3200) { pos = want; pos_ci = ci; }
            dl2_apcm_fetch(ci, pos, sc, 256);
            pos += 256;
        }
#endif
        // --- PC speaker: onda quadra a g_spk_hz, tutto in interi ---
        uint32_t hz  = g_spk_hz;
        uint32_t inc = hz ? (uint32_t)(((uint64_t)hz << 16) / 32000) : 0;
        // il bip delle mosse (881 Hz) ha un guadagno suo, piu' basso degli altri
        // effetti. Deciso QUI, una volta per blocco da 256 campioni: dentro il
        // ciclo per-campione sarebbe lavoro ripetuto 32000 volte al secondo.
        const int32_t gain_spk = (hz >= DL2_MOVE_HZ_LO && hz <= DL2_MOVE_HZ_HI)
                                 ? DL2_GAIN_MOVE : DL2_GAIN_SPK;

        // volume del firmware (1 = max, >=30 = muto), come DL e Space Ace
        int vs  = laserdisc_current_volume();
        int vol = (vs >= 30) ? 0 : (31 - vs);        // 0..30
        // fondo scala in ingresso al limiter, con il guadagno corrente: scala col
        // volume, cosi' a barra bassa il limiter e' inerte.
        int32_t in_max = (int32_t)(((int64_t)32767 * (DL2_GAIN_SCENE + DL2_GAIN_SPK) >> 3)
                                   * vol * DL2_TRIM_PCT / (30 * 100));
        if (in_max <= DL2_LIM_T) in_max = DL2_LIM_T + 1;

        for (int i = 0; i < 256; i++) {
            int32_t spk = 0;
            if (inc) { spk_phase += inc;
                       spk = (spk_phase & 0x8000) ? DL2_SPK_AMP : -DL2_SPK_AMP; }
            // il bip e' mono (va su entrambi i canali), la scena resta STEREO
            for (int ch = 0; ch < 2; ch++) {
                int32_t v = ((int32_t)sc[2 * i + ch] * DL2_GAIN_SCENE
                             + spk * gain_spk) >> 3;
                v = (v * vol * DL2_TRIM_PCT) / (30 * 100);
                int32_t a = (v < 0) ? -v : v;
                if (a > DL2_LIM_T) {                 // compressione dei soli picchi
                    a = DL2_LIM_T + ((a - DL2_LIM_T) * (32767 - DL2_LIM_T)) / (in_max - DL2_LIM_T);
                    if (a > 32767) a = 32767;
                    v = (v < 0) ? -a : a;
                }
                buf[2 * i + ch] = (int16_t)v;
            }
        }
        laserdisc_audio_write(buf, 256);   // bloccante: e' questo che scandisce i 32 kHz
    }
    laserdisc_audio_end();
    g_aud_running = false;
    vTaskDelete(nullptr);
}

// ================================================================ EMULAZIONE (core0)
static vxt_system *dl2_sys = nullptr;

// Crea ROM, RAM e periferiche. Gira nel task VIDEO (CPU1) PRIMA di lanciare il
// task del ROM, cosi' gli errori si possono mostrare a schermo (lo SPI e' suo).
static const char *dl2_rom_setup(void)
{
    memset(&ldp, 0, sizeof ldp);
    ldp.frame = 1;
    ldp.state = LDP_STOP;
    strcpy(ldp.last_cmd, "-");
    rxq_head = rxq_tail = 0;
    in_201 = 0xFF; in_202 = 0x01;
    coin_pending = coin_prev = false;
    nv_opcode = 0xFF; nv_old = 0; nv_data = 0; nv_address = 0;
    nv_addr_count = nv_bit_count = 0;
    g_ld_frame = -1; g_emu_kc = 0; g_emu_khz = 0; g_boot_khz = 0;
    g_hud_score = 0; g_hud_lives = 0; g_hud_level = 0; g_hud_credit = 0;
    g_hud_ingame = 0; g_hud_played = 0;

    // EEPROM: immagine configurata (10 vite, 1 moneta = 1 partita), la STESSA
    // usata nella validazione offline. Con l'EEPROM azzerata il ROM legge
    // impostazioni spazzatura e si comporta diversamente da quanto validato.
    memset(eeprom, 0, sizeof eeprom);
    memcpy(eeprom, dl2_nvram_default, sizeof dl2_nvram_default);

    // ROM: copia scrivibile, le patch vanno applicate prima di installarlo.
    // ⚠️ PSRAM, non RAM INTERNA come su CABINATONE. E' l'unica differenza voluta
    // rispetto alla fonte, e il motivo e' misurato: qui il player tiene gia' in
    // interna 32+16+8 KB di stack, i 17,9 KB di JPEGDEC e i 16 KB di prefetch
    // audio; aggiungerci 64 KB porterebbe il PICCO a ~157 KB contro i ~114 KB
    // che il linker dichiara liberi ([[feedback_audio_muto_e_ram_interna]]: la
    // somma si fa PRIMA di flashare, e chi alloca per ultimo — l'audio — e'
    // quello che resta a secco senza dare nessun sintomo).
    // Il costo e' nullo: questo buffer viene toccato TRE volte in sequenza
    // (memcpy -> patch -> device_fill) e subito liberato, non sta in nessun
    // percorso caldo. Con PSRAM-prima il picco scende a ~90 KB.
    dl2_ram_rom = (uint8_t *)heap_caps_malloc(0x10000, MALLOC_CAP_SPIRAM);
    if (!dl2_ram_rom) dl2_ram_rom = (uint8_t *)heap_caps_malloc(0x10000, MALLOC_CAP_INTERNAL);
    if (!dl2_ram_rom) return "ROM 64K NON ALLOCATA";
    memcpy(dl2_ram_rom, dl2_rom, 0x10000);
    dl2_patch_rom(dl2_ram_rom);

    // ⛔ NON spezzare la RAM ne' forzarla in RAM INTERNA: PROVATO SU CABINATONE
    // E PEGGIORA (boot 43 -> 58 s). Unico blocco, PSRAM.
    struct vxt_peripheral *ram = vxtu_memory_create(&dl2_alloc, 0x0, 0xF0000, false);
    if (!ram) { heap_caps_free(dl2_ram_rom); dl2_ram_rom = nullptr; return "RAM 960K NON ALLOCATA"; }
    struct vxt_peripheral *prom = vxtu_memory_create(&dl2_alloc, DL2_ROM_BASE, 0x10000, true);
    struct vxt_peripheral *pic  = vxtu_pic_create(&dl2_alloc);
    struct vxt_peripheral *pit  = vxtu_pit_create(&dl2_alloc);
    struct vxt_peripheral *ppi  = vxtu_ppi_create(&dl2_alloc);
    struct vxt_peripheral *uart = vxtu_uart_create(&dl2_alloc, 0x2F8, 3);   // COM2/IRQ3
    struct vxt_peripheral *dl2  = dl2io_create(&dl2_alloc);

#if DL2_AUDIO
    // PC speaker: libvxt avvisa quando la porta 0x61 cambia, passando la
    // frequenza del canale 2 del PIT. L'onda quadra la genera il task audio.
    vxtu_ppi_set_speaker_callback(ppi, &dl2_speaker_cb, NULL);
    g_spk_hz = 0;
#endif

    vxtu_memory_device_fill(prom, dl2_ram_rom, 0x10000);
    // la copia sorgente del ROM ha finito il suo lavoro: liberarla restituisce
    // 64 KB di RAM INTERNA, che e' la risorsa contesa.
    heap_caps_free(dl2_ram_rom);
    dl2_ram_rom = nullptr;

    ldp.uart = uart;
    static struct vxtu_uart_interface uif;
    uif.config = NULL; uif.data = &uart_data_cb; uif.ready = &uart_ready_cb; uif.udata = NULL;
    vxtu_uart_set_callbacks(uart, &uif);

    struct vxt_peripheral *devs[] = { ram, prom, pic, pit, ppi, uart, dl2, NULL };
    dl2_sys = vxt_system_create(&dl2_alloc, DL2_CPU_HZ, devs);
    if (!dl2_sys) return "VXT SYSTEM CREATE KO";
    if (vxt_system_initialize(dl2_sys) != VXT_NO_ERROR) {
        vxt_system_destroy(dl2_sys); dl2_sys = nullptr;
        return "VXT INITIALIZE KO";
    }
    vxt_system_reset(dl2_sys);
    return nullptr;
}

static void dl2_rom_task(void *arg)
{
    (void)arg;
    // pacing: il disco avanza a 29.97 fps quando e' in PLAY
    int64_t cycles = 0, next_frame = DL2_CPU_HZ * (int64_t)DISC_FPS_D / DISC_FPS_N;
    const int64_t frame_cycles = next_frame;
    int64_t t0 = esp_timer_get_time();
    const int64_t boot_t0 = t0;
    int  yield_ctr = 0;
    bool easy_armed = false;          // il ROM ha inizializzato il contatore vite
    bool paced = false;               // il tempo reale conta solo dal primo SEARCH
    int64_t pace_cycles0 = 0;
    // ⭐ DUE numeri, non uno. La media da inizio task non basta: appena il pacing
    // entra in funzione (primo SEARCH) scende verso DL2_CPU_HZ e nasconde la
    // capacita' vera ([[feedback_misurare_anche_cosa_non_cerchi]]).
    //   BOOT = cicli/s fino al primo SEARCH, congelati = capacita' REALE col
    //          pacing disattivato -> e' il tetto di DL2_CPU_HZ.
    //   EMU  = velocita' ISTANTANEA (finestra 250 ms): dice se la scheda tiene
    //          ADESSO, in attract o in partita.
    // ⚠️ BOOT resta OTTIMISTA: durante il boot il task video non decodifica
    // niente. Non alzare DL2_CPU_HZ fino al tetto (errore gia' fatto).
    int64_t  ui_cyc0 = 0; uint32_t ui_ms0 = millis();

    while (!g_stop) {
        struct vxt_step st = vxt_system_step(dl2_sys, 2000);
        if (st.err != VXT_NO_ERROR) break;
        cycles += st.cycles;
        g_emu_kc = (uint32_t)(cycles / 1000);
        rx_flush();

        // ⚠️ Il disco avanza sui CICLI EMULATI, non sul tempo reale. Agganciarlo
        // a esp_timer lo farebbe correre avanti rispetto al ROM: le scene
        // passerebbero oltre la finestra della mossa e il gioco sarebbe rotto.
        if (cycles >= next_frame) {
            next_frame += frame_cycles;
            if (ldp.state == LDP_PLAY) {
                ldp.frame++;
                if (ldp.repeat_to && ldp.frame >= ldp.repeat_to) {
                    ldp.state = LDP_PAUSE; ldp.repeat_to = 0;
                }
            }
            // ⭐ punteggio e vite dalla RAM emulata, 30 volte al secondo,
            // agganciati all'avanzamento del disco: NON per istruzione (sarebbero
            // due letture in mezzo al ciclo caldo dell'interprete).
            uint32_t sc = (uint32_t)vxt_system_read_byte(dl2_sys, DL2_SCORE_ADDR) |
                          ((uint32_t)vxt_system_read_byte(dl2_sys, DL2_SCORE_ADDR + 1) << 8);
            g_hud_score = sc / DL2_SCORE_DIV;

            uint8_t lv = vxt_system_read_byte(dl2_sys, DL2_LIVES_ADDR);
            // ---- FACILE = vite illimitate ----
            // DL2 non ha difficolta' nel ROM: si ricarica il contatore vite in
            // RAM (0x156AC, lo stesso che l'HUD legge e che combacia con tutti e
            // 18 gli annunci del ROM).
            // ⚠️ ARMATO, non incondizionato: fra l'annuncio di LEVEL e
            // l'inizializzazione del giocatore il contatore vale ancora 0 per
            // circa mezzo secondo (misurato su CABINATONE). Scrivendoci sopra in
            // quel transitorio si andrebbe a pestare l'inizializzazione del ROM.
            // Quindi si comincia a ricaricare SOLO dopo aver visto il contatore
            // gia' inizializzato (>0) in questa partita.
            if (!g_hud_ingame)   easy_armed = false;
            else if (lv > 0)     easy_armed = true;
            if (g_easy && easy_armed && lv < DL2_EASY_LIVES) {
                vxt_system_write_byte(dl2_sys, DL2_LIVES_ADDR, DL2_EASY_LIVES);
                lv = DL2_EASY_LIVES;
            }
            g_hud_lives = (int)lv;
        }
        // pubblica il frame al task video: solo dopo il primo SEARCH, altrimenti
        // il player e' a frame 1 e mostrerebbe nero invece della schermata d'attesa
        if (ldp.seek_target) g_ld_frame = ldp.frame;

        // ---- PACING: SOLO DOPO il primo SEARCH ----
        // Durante il boot del ROM non c'e' NIENTE da sincronizzare (il disco e'
        // fermo): frenare li' non serve e allungherebbe l'avvio. Cosi' BOOT resta
        // una MISURA VERA della velocita' della scheda invece del freno impostato.
        if (!paced && ldp.seek_target) {
            paced = true;
            pace_cycles0 = cycles;
            { int64_t ms = (esp_timer_get_time() - boot_t0) / 1000;
              if (ms > 0) g_boot_khz = (uint32_t)(cycles / ms); }
            t0 = esp_timer_get_time();
        }

        // ⚠️ CEDERE SEMPRE LA CPU, non solo quando siamo in anticipo. Se il P4
        // non tiene il clock siamo SEMPRE in ritardo: senza uno yield
        // incondizionato il task non dorme mai, l'idle di core0 muore di fame e
        // il watchdog resetta la scheda.
        int64_t want_us = (cycles - pace_cycles0) * 1000000LL / DL2_CPU_HZ;
        int64_t now_us  = esp_timer_get_time() - t0;
        if (paced && want_us > now_us + 2000) {
            vTaskDelay(pdMS_TO_TICKS((want_us - now_us) / 1000));
        } else if (++yield_ctr >= 8) {
            yield_ctr = 0;
            vTaskDelay(1);
        }
        // se siamo in ritardo cronico non si accumula debito all'infinito
        if (paced && now_us > want_us + 500000) { t0 = esp_timer_get_time() - want_us; }

        uint8_t b = g_input->buttons_get();
        dl2_read_inputs(b);
        if (ld_want_exit(b)) { g_stop = true; break; }

        { uint32_t now = millis(), dms = now - ui_ms0;
          if (dms >= 250) { g_emu_khz = (uint32_t)((cycles - ui_cyc0) / dms);
                            ui_cyc0 = cycles; ui_ms0 = now; } }
    }
    g_rom_running = false;
    vTaskDelete(nullptr);
}

// ================================================================ VIDEO (CPU1)
static bool dl2_video_init(const char **why)
{
    if (!g_in)   g_in   = (uint8_t *)heap_caps_malloc(LD_IN_MAX,               MALLOC_CAP_SPIRAM);
    if (!g_out)  g_out  = (uint8_t *)heap_caps_malloc((size_t)LD_W * LD_H * 2, MALLOC_CAP_SPIRAM);
    if (!g_jdec) g_jdec = new JPEGDEC();
    if (!g_in || !g_out || !g_jdec) { *why = "ALLOC BUFFER VIDEO"; return false; }
#if DL2_AUDIO
    // ⭐ prefetch audio in RAM INTERNA: e' letto/scritto 250 volte al secondo.
    // Allocato qui e liberato in dl2_close_all — non statico
    // ([[fm_c_lfo_pm_table_128kb_ram]]: i buffer di UN gioco non si pagano sempre).
    // ⚠️ Se non c'e' posto NON si spegne tutto l'audio: dl2_apcm_fetch produce
    // silenzio di scena e il PC speaker continua a suonare
    // ([[feedback_audio_muto_e_ram_interna]], punto 4).
    if (!g_apf) g_apf = (int16_t *)heap_caps_malloc((size_t)DL2_APF_N * 4, MALLOC_CAP_INTERNAL);
    g_apf_ci = -1; g_apf_pos = -1; g_apf_len = 0;
    memset(g_a_none, 0, sizeof g_a_none);
    for (int i = 0; i < DL2_AC_N; i++) { g_a[i].fp = nullptr; g_a[i].ci = -1; g_a[i].off = 0; g_a[i].lru = 0; }
    g_a_tick = 0;
#endif
    for (int i = 0; i < DL2_OC_N; i++) { g_ocf[i] = nullptr; g_ocw[i] = -1; g_ocu[i] = 0; g_ocoff[i] = -1; }
    g_e_clip = g_e_idx = g_e_range = g_e_read = g_e_dec = 0;
    g_v_fps = 0; g_volbar_fill = -1;
    return true;
}

static void dl2_play_task(void *arg)
{
    (void)arg;
    ld_window(true);

    const char *why = nullptr;
    bool packok = false;

    if (!(sd_is_mounted() ? true : sd_mount())) why = "SD NON MONTATA";
    else if (!dl2_video_init(&why)) { /* why gia' impostato */ }
    else if (!dl2_load_meta())      why = "MANCA DL2.META";
    else                            packok = true;

    if (!packok) {
        while (!g_stop) {
            dl2_message(why ? why : "PACK VIDEO MANCANTE", "SERVE /DL2 SU SD", C_RED);
            if (ld_want_exit(g_input->buttons_get())) break;
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        goto uscita;
    }

    // scelta difficolta' PRIMA di avviare il ROM (stesso flusso di Space Ace):
    // cosi' i ~43 s di boot cominciano a scorrere mentre l'utente ha gia' scelto.
    if (!dl2_select_difficulty()) goto uscita;    // START 3s = torna al menu

    why = dl2_rom_setup();
    if (why) {
        while (!g_stop) {
            dl2_message(why, "EMULATORE NON AVVIATO", C_RED);
            if (ld_want_exit(g_input->buttons_get())) break;
            vTaskDelay(pdMS_TO_TICKS(60));
        }
        goto uscita;
    }

    g_rom_running = true;
    xTaskCreatePinnedToCore(dl2_rom_task, "dl2_rom", DL2_ROM_STACK, nullptr, 5, nullptr, 0);
#if DL2_AUDIO
    // prio 6 = sopra il ROM, ma sta quasi sempre fermo dentro laserdisc_audio_write
    // (bloccante su I2S): dorme e non puo' affamare l'emulazione.
    g_aud_running = true;
    xTaskCreatePinnedToCore(dl2_audio_task, "dl2_aud", 8192, nullptr, 6, nullptr, 0);
#endif

    {
        // VIDEO: SOLO decode+blit, guidato da g_ld_frame (che il ROM avanza su core0).
        // ⚠️ Si ridisegna anche quando cambia il TESTO e non il frame: le schermate
        // LEVEL / n LIVES / INSERT COIN stanno su FERMO-IMMAGINE, senza questo
        // trigger comparirebbero solo al frame successivo (che puo' non arrivare mai).
        int      shown_f   = -2;
        uint32_t shown_seq = 0xFFFFFFFFu;
        uint32_t frames = 0, fps_ms0 = millis();
        // ⛔ NON "ultimo frame disegnato", ma "ultima volta che eravamo IN PARI".
        // DL2 vive di FERMO-IMMAGINE (SEARCH -> STILL): con "ultimo frame
        // disegnato" la rete di sicurezza scattava su ogni pausa e ridisegnava la
        // pagina diagnostica SOPRA il filmato — e ci restava, perche' finche' il
        // disco e' fermo `shown_f` non cambia e dl2_show non viene piu' chiamata.
        // Non avere niente da disegnare NON e' un guasto: il guasto e' avere
        // qualcosa da disegnare e non riuscirci.
        uint32_t last_ok = millis();
        bool     started = false;

        while (!g_stop) {
            int f = g_ld_frame;
            uint32_t seq = g_text_seq;

            if (f < 0) {                                   // ROM ancora in boot
                // schermata di caricamento con barra (con DL2_VIDEO 0 resta la
                // pagina diagnostica, che serve a leggere BOOT/EMU)
#if DL2_VIDEO
                if (millis() - last_ok > 250) { dl2_loading_screen(); last_ok = millis(); }
#else
                if (millis() - last_ok > 500) { dl2_diag_page(true);  last_ok = millis(); }
#endif
                vTaskDelay(pdMS_TO_TICKS(50));
                continue;
            }
#if DL2_VIDEO
            if (f != shown_f || seq != shown_seq) {
                if (dl2_show(f)) {
                    shown_f = f; shown_seq = seq; frames++; last_ok = millis();
                    if (!started) { started = true; fps_ms0 = millis(); frames = 0; }
                } else {
                    // c'e' qualcosa di nuovo e NON riusciamo a mostrarlo: e' questo
                    // il caso che la rete di sicurezza deve intercettare. Si cede
                    // comunque la CPU, altrimenti il ritentativo diventa busy-loop.
                    vTaskDelay(pdMS_TO_TICKS(4));
                }
            } else {
                vTaskDelay(pdMS_TO_TICKS(4));              // in pari: cedi
                last_ok = millis();
            }
            { uint32_t dt = millis() - fps_ms0;
              if (dt >= 1000) { g_v_fps = frames * 1000 / dt; frames = 0; fps_ms0 = millis(); } }

            // ⭐ RETE DI SICUREZZA: se per oltre 1 s c'e' un frame nuovo da mostrare
            // e non ci si riesce, lo stato va scritto DIRETTAMENTE sul pannello.
            // Su CABINATONE il tachimetro stava dentro il frame video e a video
            // piantato si congelava — lo strumento si spegneva proprio nel guasto
            // che doveva misurare.
            if (millis() - last_ok > 1000) { dl2_diag_page(true); last_ok = millis(); }
#else
            (void)shown_f; (void)shown_seq; (void)frames; (void)fps_ms0; (void)started;
            if (millis() - last_ok > 250) { dl2_diag_page(true); last_ok = millis(); }
            vTaskDelay(pdMS_TO_TICKS(20));
#endif
        }
    }

    // ferma ROM e audio PRIMA di distruggere il sistema e chiudere i file che
    // stanno usando. Il task audio puo' essere fermo dentro una write bloccante
    // su I2S: gli si lascia tempo di uscire (un blocco = 8 ms).
    g_stop = true;
    for (int i = 0; i < 200 && (g_rom_running || g_aud_running); i++) vTaskDelay(pdMS_TO_TICKS(10));
    // vxt_system_destroy distrugge anche TUTTE le periferiche installate
    // (system.c:171) -> i 960 KB di PSRAM tornano indietro e si puo' rientrare in
    // DL2 dal menu senza riavviare la scheda.
    if (dl2_sys) { vxt_system_destroy(dl2_sys); dl2_sys = nullptr; }
    dl2_close_all();

uscita:
    if (dl2_ram_rom) { heap_caps_free(dl2_ram_rom); dl2_ram_rom = nullptr; }
    // i 17,9 KB del decoder tornano alla RAM INTERNA appena si esce dal player
    if (g_jdec) { delete g_jdec; g_jdec = nullptr; }
    // pulisci a nero l'intera finestra player e torna alla finestra menu
    memset(g_strip, 0, (size_t)TFT_FB_W * 8 * sizeof(uint16_t));
    for (int s = 0; s < LD_FB_H / 8; s++) video.write(g_strip, TFT_FB_W * 8);
    ld_window(false);
    g_done = true;
    vTaskDelete(nullptr);
}

// ---------------------------------------------------------------- entry
bool dl2_play(Input *input, unsigned short *out_strip) {
    g_strip = (uint16_t *)out_strip;
    g_input = input;
    g_stop  = false;
    g_done  = false;

    if (xTaskCreatePinnedToCore(dl2_play_task, "dl2_play", LD_TASK_STACK,
                                nullptr, 2, nullptr, 1) != pdPASS)
        return false;

    while (!g_done) {
        input->updateBluetooth();
        vTaskDelay(pdMS_TO_TICKS(8));
    }
    return true;
}
