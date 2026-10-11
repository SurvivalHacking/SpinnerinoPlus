// GENERATO DA mk_dl2start.py — non modificare a mano.
// Il blocco hardware e dl2_rom_setup sono ESTRATTI ALLA LETTERA da dl2.cpp.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#define nullptr NULL

#if defined(__cplusplus) && !defined(_Static_assert)
  #define _Static_assert static_assert
#endif
#include <vxt/vxt.h>
#include <vxt/vxtu.h>

/* ---- ambiente SPINNERINO simulato ---- */
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40

#define DL2_CPU_HZ       1180000
#define DISC_FPS_N       30000
#define DISC_FPS_D       1001
#define DL2_ROM_BASE     0xF0000
#define DL2_SCORE_ADDR   0x156B0
#define DL2_SCORE_DIV    1
#define DL2_LIVES_ADDR   0x156AC
#define DL2_AUDIO        0
#ifndef DL2_START_HOLD_MS
  #define DL2_START_HOLD_MS 1200
#endif

#define MALLOC_CAP_SPIRAM   0
#define MALLOC_CAP_INTERNAL 0
static void *heap_caps_realloc(void *p, size_t n, int c) { (void)c; return realloc(p, n); }
static void *heap_caps_malloc(size_t n, int c) { (void)c; return malloc(n); }
static void  heap_caps_free(void *p) { free(p); }

static uint32_t g_millis_v = 0;
static uint32_t millis(void) { return g_millis_v; }
static int  input_is_vol_mode_active(void) { return 0; }

#include "dl2_rom.h"
#include "dl2_nvram.h"

/* ATTENZIONE: NIENTE dichiarazioni qui delle variabili di stato, degli enum TL
   o di dl2_text_class: sono GIA' dentro il blocco estratto, che parte da
   dl2_alloc e arriva fino a dl2_read_inputs. Dichiararle di nuovo =
   redefinition, ed e' l'errore che questo banco ha fatto al primo giro.
   NB: mai una sequenza asterisco-barra dentro un commento generato. */
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

static vxt_system *dl2_sys = NULL;

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

/* ---- sequenza del firmware SPINNERINO (input.cpp:521), riprodotta alla lettera:
       0..80 ms COIN · 80..160 gap · 160..400 START ---- */
static uint8_t bottoni(uint32_t t_ms, uint32_t t_press)
{
    if (t_ms < t_press) return 0;
    uint32_t dt = t_ms - t_press;
    if (dt <  80) return BUTTON_COIN;
    if (dt < 160) return 0;
    if (dt < 400) return BUTTON_START;
    return 0;
}

int main(int argc, char **argv)
{
    uint32_t t_press = (argc > 1) ? (uint32_t)atoi(argv[1]) : 80000;
    uint32_t t_end   = (argc > 2) ? (uint32_t)atoi(argv[2]) : 130000;

    const char *err = dl2_rom_setup();
    if (err) { printf("KO setup: %s\n", err); return 2; }

    int64_t cycles = 0, next_frame = DL2_CPU_HZ * (int64_t)DISC_FPS_D / DISC_FPS_N;
    const int64_t frame_cycles = next_frame;
    double t_credit = -1, t_level = -1, t_search = -1;
    int    credit_visto = 0;

    for (;;) {
        struct vxt_step st = vxt_system_step(dl2_sys, 2000);
        if (st.err != VXT_NO_ERROR) { printf("KO: errore CPU\n"); return 3; }
        cycles += st.cycles;
        rx_flush();

        if (cycles >= next_frame) {
            next_frame += frame_cycles;
            if (ldp.state == LDP_PLAY) {
                ldp.frame++;
                if (ldp.repeat_to && ldp.frame >= ldp.repeat_to) {
                    ldp.state = LDP_PAUSE; ldp.repeat_to = 0;
                }
            }
        }
        if (ldp.seek_target) g_ld_frame = ldp.frame;

        /* il tempo che vede il firmware e' quello EMULATO: sul device il pacing
           tiene emulazione e tempo reale agganciati, quindi e' la stessa cosa */
        g_millis_v = (uint32_t)(cycles * 1000 / DL2_CPU_HZ);
        double t = g_millis_v / 1000.0;

        dl2_read_inputs(bottoni(g_millis_v, t_press));

        if (t_search < 0 && ldp.seek_target)          t_search = t;
        if (!credit_visto && g_hud_credit > 0)      { credit_visto = 1; t_credit = t; }
        if (t_level < 0 && g_hud_level > 0)         { t_level = t; break; }
        if (g_millis_v > t_end) break;
    }

    printf("  primo SEARCH   : %s\n", t_search < 0 ? "MAI" : "");
    if (t_search >= 0) printf("  primo SEARCH   : %.1f s\n", t_search);
    printf("  CREDIT visto   : %s", credit_visto ? "" : "MAI\n");
    if (credit_visto) printf("%.1f s  (%.2f s dopo la pressione)\n",
                             t_credit, t_credit - t_press / 1000.0);
    if (t_level >= 0) {
        printf("  >>> PARTITA AVVIATA a %.1f s  (LEVEL %d)\n", t_level, g_hud_level);
        return 0;
    }
    printf("  >>> PARTITA NON AVVIATA entro %.0f s\n", t_end / 1000.0);
    return 1;
}
