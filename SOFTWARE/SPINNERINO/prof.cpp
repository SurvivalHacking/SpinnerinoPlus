// ============================================================================
// prof.cpp — lo strumento di misura di ARKANOID 2. Vedi prof.h per il perche'.
// ============================================================================
#include "prof.h"

#if ARK2_PROF

#include <Arduino.h>
#include <stdio.h>
#include <string.h>
#include "config.h"
#include "emulation/font8.h"
#include "emulation/video.h"

// ------------------------------------------------------------ la banda nera
// Il pannello e' scritto in strisce `out_strip` da TFT_FB_W x 8. Le coordinate
// di quel buffer NON sono quelle del giocatore: il monitor e' girato.
// La corrispondenza si legge in `draw_volume_overlay()` di SPINNERINO.ino, che
// e' gia' stata provata sul cabinato — la barra del volume sta «a filo del
// bordo ALTO» e occupa x_l 0..15:
//     x_l  (0..319) = riga VERTICALE del giocatore, 0 = IN ALTO
//     y_l  (0..223) = colonna del giocatore, ma AL CONTRARIO: px = 223 - y_l
// ⭐ Non l'ho ricavata ragionando: e' copiata dal pezzo che gia' funziona
// ([[feedback_porta_non_riprogettare]] e [[feedback_barra_volume_player_autonomi]],
// dove le coordinate di un overlay sono quelle del GIOCATORE e vanno rimappate).
//
// Il quadro di gioco e' largo `videoGameWidth()` = 256, centrato nei 320 del
// pannello: restano **32 px di banda nera** a x_l 0..31 (in alto per il
// giocatore) e altri 32 in fondo. Ci stanno tre righe di testo 8x8.
// ⚠️ La banda sta in FONDO per il giocatore (x_l 288..319), non in cima: le
// prime 16 righe (x_l 0..15) sono della BARRA DEL VOLUME, e due cose nello
// stesso posto vorrebbe dire vederne una sola quando serve l'altra.
#define PF_BANDA_X0   (TFT_FB_W - 32)   // 288: prima riga verticale della banda
#define PF_BANDA_H   32        // quanto e' alta la banda (px del giocatore)
#define PF_TESTO_Y    4        // margine prima della prima riga di testo
#define PF_RIGHE      3
#define PF_COL       28        // 28 caratteri x 8 px = 224 = la larghezza utile
#define PF_LARG     224        // px del giocatore in orizzontale

// giallo e bianco, BYTE-SWAPPED come tutto il resto di questo progetto
// (display SPI): 0xFFE0 -> 0xE0FF, 0xFFFF resta 0xFFFF.
#define PF_GIALLO 0xE0FF
#define PF_BIANCO 0xFFFF

static bool s_attivo = false;
static bool s_nuovo  = false;   // il testo e' cambiato dall'ultimo blit
static char s_riga[PF_RIGHE][PF_COL + 1] = { "", "", "" };

// ---- accumulatori del ciclo VIDEO (scritti e letti dallo stesso task) ----
static uint32_t s_ren_t0 = 0, s_blt_t0 = 0;
static uint32_t s_ren_acc = 0, s_blt_acc = 0, s_lav_acc = 0;
static uint32_t s_lav_pic = 0, s_quadri = 0;
static uint32_t s_t_sec = 0;

// ---- accumulatori scritti da ALTRI task ----------------------------------
// ⚠️ `volatile` e basta, niente sezione critica: sono somme che si leggono una
// volta al secondo e si azzerano. Una lettura sfasata di un quadro sposta il
// terzo decimale di una media su 60 campioni, e costerebbe piu' il lucchetto
// del dato ([[feedback_no_costly_calls_in_render]]: il termometro non deve
// costare piu' di cio' che misura).
static volatile uint32_t s_emu_acc = 0;    // us dentro run_frame
static volatile uint32_t s_emu_n   = 0;    // quadri emulati
static volatile uint32_t s_aud_byte = 0;   // byte consegnati all'I2S
static uint32_t s_emu_t0 = 0;

void prof_attiva(bool on) { s_attivo = on; }
bool prof_attivo(void)    { return s_attivo; }

void prof_emu_inizio(void) { if (s_attivo) s_emu_t0 = micros(); }
void prof_emu_fine(void)
{
    if (!s_attivo || !s_emu_t0) return;
    s_emu_acc += micros() - s_emu_t0;
    s_emu_n++;
    s_emu_t0 = 0;
}

void prof_ren_inizio(void) { if (s_attivo) s_ren_t0 = micros(); }
void prof_ren_fine(void)   { if (s_attivo && s_ren_t0) { s_ren_acc += micros() - s_ren_t0; s_ren_t0 = 0; } }
void prof_blt_inizio(void) { if (s_attivo) s_blt_t0 = micros(); }
void prof_blt_fine(void)   { if (s_attivo && s_blt_t0) { s_blt_acc += micros() - s_blt_t0; s_blt_t0 = 0; } }

void prof_aud_byte(unsigned int n) { if (s_attivo) s_aud_byte += n; }

// decimi senza `%f` ([[esp32_arduino_no_float_printf]]: con %f la stringa esce
// VUOTA su questa build e l'overlay non compare affatto)
static inline uint32_t dec_int(uint32_t us)  { return us / 1000; }
static inline uint32_t dec_frac(uint32_t us) { return (us % 1000) / 100; }

void prof_quadro(uint32_t lavoro_us)
{
    if (!s_attivo) return;

    s_lav_acc += lavoro_us;
    if (lavoro_us > s_lav_pic) s_lav_pic = lavoro_us;
    s_quadri++;

    const uint32_t ora = millis();
    if (s_t_sec == 0) { s_t_sec = ora; return; }
    if (ora - s_t_sec < 1000) return;

    const uint32_t ms   = ora - s_t_sec;
    const uint32_t n    = s_quadri ? s_quadri : 1;
    const uint32_t emun = s_emu_n ? s_emu_n : 1;

    const uint32_t fps  = s_quadri * 1000 / ms;
    const uint32_t ems  = s_emu_n  * 1000 / ms;
    const uint32_t lav  = s_lav_acc / n;                  // us medi per quadro
    const uint32_t ren  = s_ren_acc / n;
    const uint32_t blt  = s_blt_acc / n;
    const uint32_t emu  = s_emu_acc / emun;
    const uint32_t car  = lav * 100 / 16667;              // carico VIDEO, %
    const uint32_t cem  = emu * 100 / 16667;              // carico EMULAZIONE, %
    // un blocco = sizeof(snd_buffer) byte; l'atteso e' 500/s (32 kHz / 64 frame)
    const uint32_t aud  = s_aud_byte / 256 * 1000 / ms;   // 128 int16 = 256 byte

    // ⚠️ Righe di lunghezza FISSA: se una si accorcia, i caratteri della
    // precedente restano a schermo (la banda si ridisegna, non si cancella).
    snprintf(s_riga[0], sizeof s_riga[0], "FPS %2lu  CAR %3lu  PIC %2lu.%lu   ",
             (unsigned long)fps, (unsigned long)car,
             (unsigned long)dec_int(s_lav_pic), (unsigned long)dec_frac(s_lav_pic));
    snprintf(s_riga[1], sizeof s_riga[1], "REN %2lu.%lu BLT %2lu.%lu AUD %3lu ",
             (unsigned long)dec_int(ren), (unsigned long)dec_frac(ren),
             (unsigned long)dec_int(blt), (unsigned long)dec_frac(blt),
             (unsigned long)aud);
    snprintf(s_riga[2], sizeof s_riga[2], "EMU %2lu.%lu CEM %3lu EMS %2lu  ",
             (unsigned long)dec_int(emu), (unsigned long)dec_frac(emu),
             (unsigned long)cem, (unsigned long)ems);

    s_nuovo = true;

    s_ren_acc = s_blt_acc = s_lav_acc = 0;
    s_lav_pic = 0; s_quadri = 0;
    s_emu_acc = 0; s_emu_n = 0; s_aud_byte = 0;
    s_t_sec = ora;
}

// ---------------------------------------------------------------- il disegno
// Chiamata una volta per striscia. La striscia copre 8 COLONNE del giocatore
// (px = 223 - y_l, con y_l = row*8 + r): per ognuna si scrivono i 32 pixel
// verticali della banda. Sono 256 scritture per striscia, 7.168 per quadro.
void prof_banda(unsigned short *strip, int row)
{
    if (!s_attivo || !strip) return;

    for (int r = 0; r < 8; r++) {
        const int y_l = row * 8 + r;
        if (y_l >= PF_LARG) break;
        const int px = (PF_LARG - 1) - y_l;          // colonna del GIOCATORE
        unsigned short *linea = strip + r * TFT_FB_W;

        const int col = px >> 3;                     // quale carattere
        const int gx  = px & 7;                      // quale colonna del glifo
        if (col >= PF_COL) { for (int py = 0; py < PF_BANDA_H; py++) linea[PF_BANDA_X0 + py] = 0x0000; continue; }

        for (int py = 0; py < PF_BANDA_H; py++) {
            unsigned short c = 0x0000;
            const int dy = py - PF_TESTO_Y;
            if (dy >= 0 && dy < PF_RIGHE * 8) {
                const int riga = dy >> 3, gy = dy & 7;
                const char ch = s_riga[riga][col];
                if (ch) {
                    const unsigned char bits =
                        pgm_read_byte(&FONT8[char_to_idx(ch)][gy]);
                    if (bits & (0x80 >> gx)) c = (riga == 0) ? PF_GIALLO : PF_BIANCO;
                }
            }
            linea[PF_BANDA_X0 + py] = c;
        }
    }
}

// ---------------------------------------------------- percorso a finestra stretta
// Il pixel della banda in coordinate del GIOCATORE: `k` = riga verticale dentro
// la banda (0..31), `px` = colonna (0..223).
static inline unsigned short pf_pixel(int k, int px)
{
    const int dy = k - PF_TESTO_Y;
    if (dy < 0 || dy >= PF_RIGHE * 8) return 0x0000;
    const int col = px >> 3;
    if (col >= PF_COL) return 0x0000;
    const char ch = s_riga[dy >> 3][col];
    if (!ch) return 0x0000;
    const unsigned char bits = pgm_read_byte(&FONT8[char_to_idx(ch)][dy & 7]);
    if (!(bits & (0x80 >> (px & 7)))) return 0x0000;
    return (dy >> 3) == 0 ? PF_GIALLO : PF_BIANCO;
}

bool prof_banda_blit(unsigned short *strip)
{
    if (!s_attivo || !s_nuovo || !strip) return false;
    s_nuovo = false;

    extern Video video;
    // ⚠️ La finestra si cambia solo a DMA ferma: una transazione in volo
    // scriverebbe nella finestra sbagliata. Stessa precauzione di
    // `videoVolBarTop()`, che e' il pezzo da cui questo blit e' copiato.
    video.waitDMA();
    video.setAddrWindow(TFT_HW_X + PF_BANDA_X0, TFT_HW_Y, 32, PF_LARG);

    // 56 righe x 32 px = 1792 pixel per blocco: sta nel dma_buffer (2560) e
    // nella striscia di lavoro del chiamante (320x8 = 2560).
    const int PASSO = 56;
    for (int r0 = 0; r0 < PF_LARG; r0 += PASSO) {
        unsigned short *p = strip;
        for (int y_l = r0; y_l < r0 + PASSO; y_l++) {
            const int px = (PF_LARG - 1) - y_l;
            for (int k = 0; k < 32; k++) *p++ = pf_pixel(k, px);
        }
        video.write(strip, PASSO * 32);
    }
    video.waitDMA();
    return true;
}

#endif // ARK2_PROF
