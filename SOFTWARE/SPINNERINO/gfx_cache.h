#pragma once
#include <stdint.h>
#include <stddef.h>

// ============================================================================
// ⭐ MISURA DEL TEMPO SPESO NEI MISS (cioe' a leggere dalla SD).
// A 1 `gfx_cache_miss()` accumula in `us_miss` i microsecondi passati dentro
// `disk_read`. Serve all'overlay di NEO GEO (`NG_MISURA` in neogeo.cpp): senza
// questo numero il tempo della SD sta dentro REN e non si puo' distinguere
// «il disegno e' lento» da «gli sprite arrivano dalla scheda» — che e' l'UNICA
// differenza fra METAL SLUG (grafica tutta in PSRAM) e METAL SLUG 2 (sprite
// letti dalla SD mentre si gioca).
// ⛔ Si accende e si spegne INSIEME a NG_MISURA. A 0 il campo non esiste e non
// c'e' una istruzione in piu' da nessuna parte: il raggio d'azione su 1944, che
// usa la stessa cache, e' ESATTAMENTE ZERO.
// ⚠️ Acceso costa due letture di `esp_timer_get_time()` **per MISS**, mai per
// hit: il percorso caldo (`gfx_cache_bank`, ~1.300 chiamate/quadro) non e'
// toccato ([[feedback_no_costly_calls_in_render]]: il termometro non deve
// costare piu' di cio' che misura).
// ============================================================================
// ⭐ RISPENTO il 2026-08-29 insieme a NG_MISURA, a taratura chiusa. A 0 il campo
// non esiste e su 1944 — che usa la stessa cache — non c'e' una istruzione in
// piu': raggio d'azione ZERO.
// ⭐ RIACCESO il 2026-10-08 insieme a NG_MISURA: PULSTAR «vanno ottimizzati gli
// fps». RISPENTO lo stesso giorno insieme a NG_MISURA (richiesta dell'utente).
#define GFX_CACHE_MISURA 0

// ============================================================================
// gfx_cache — cache a banchi di un file di GRAFICA che sta sulla SD e NON
// entra in PSRAM.
//
// PERCHE' ESISTE: 1944 (CPS-2) ha 20 MB di grafica; con gli 8 MB di campioni
// QSound, i 3 MB del programma (opcode + dati separati) e il resto si sfora i
// 32 MB di PSRAM della scheda. La grafica pero' si legge a BANCHI piccoli e
// molto ripetuti, quindi tenerne in memoria una parte basta.
//
// E' il meccanismo di **gngeo** (`roms.c` init_sprite_cache /
// `video.c` get_cached_sprite_ptr), portato alla lettera:
//   - banchi da `bank_size` byte (4096 = 32 tile da 128)
//   - `ptr[banco]` punta dentro la cache, oppure e' NULL se non e' residente
//   - sfratto ROUND-ROBIN: nessuna euristica, un indice che gira
//   - ⭐⭐ il MISS **non passa da FATFS**: si legge con `disk_read()` diretto
//     sui settori, usando una MAPPA costruita in avvio camminando a mano la
//     catena FAT. gngeo lo motiva nel codice: `f_lseek` sbaglia il cluster ai
//     confini (`while (ofs > bcs)` con `>` stretto) e un banco su quattro
//     finiva sui settori sbagliati.
//   - un BOUNCE buffer in RAM interna: `disk_read` vuole memoria DMA, la
//     cache sta in PSRAM.
//
// ⚠️ TRAPPOLA da ricordare al chiamante: dopo aver chiesto il banco, l'indice
//    del tile diventa LOCALE al banco (`tile & (tile_per_banco - 1)`). Tutto
//    quello che serve sull'indice GLOBALE (p.es. una mappa di tile vuoti) va
//    calcolato PRIMA.
// ============================================================================

struct GfxCache {
    uint8_t  *data;            // il buffer in PSRAM (n_slot * bank_size)
    uint8_t **ptr;             // per BANCO: dove sta, oppure nullptr
    int32_t  *slot_bank;       // per SLOT: quale banco ci sta, -1 = libero
    uint32_t *sector_map;      // per BANCO: primo settore (LBA) sulla SD
    uint8_t  *bounce;          // buffer DMA per disk_read
    uint32_t  bank_size;
    uint8_t   bank_shift;      // log2(bank_size), 0 se non e' potenza di 2
    uint32_t  n_bank;
    uint32_t  n_slot;
    uint32_t  sect_per_bank;
    uint32_t  evict;           // indice di sfratto, gira e basta
    uint8_t   pdrv;            // drive fisico FATFS
    bool      ok;
    // diagnostica: si leggono, non si stampano nel path caldo
    uint32_t  hit, miss, err;
#if GFX_CACHE_MISURA
    // microsecondi CUMULATIVI passati dentro disk_read. Chi legge fa la
    // DIFFERENZA fra due istanti: qui non si azzera mai, cosi' non serve
    // nessun lucchetto fra il task che scrive e quello che legge.
    uint32_t  us_miss;
#endif
};

// Prepara la cache su `path` (percorso FATFS, es. "/1944/gfx.bin").
// `cache_bytes` e' il tetto: viene arrotondato in giu' a un multiplo di
// bank_size, e se l'allocazione fallisce si riprova dimezzando.
// Ritorna false se la mappa dei settori non si puo' costruire: in quel caso
// NON si ripiega su f_lseek (vedi sopra, sbaglia), il chiamante deve dare un
// errore leggibile.
bool gfx_cache_init(GfxCache *c, const char *path,
                    uint32_t bank_size, uint32_t cache_bytes);

void gfx_cache_free(GfxCache *c);

// Il MISS: carica il banco e ritorna dove sta. Non inline: si chiama di rado.
const uint8_t *gfx_cache_miss(GfxCache *c, uint32_t bank);

// Il path CALDO: il banco che contiene l'offset `off`.
// Ritorna il puntatore all'INIZIO DEL BANCO; l'offset dentro il banco lo
// calcola il chiamante con `off & (bank_size - 1)`.
static inline const uint8_t *gfx_cache_bank(GfxCache *c, uint32_t off)
{
    // ⭐ shift invece di DIVISIONE: `bank_size` e' una variabile, quindi il
    // compilatore non puo' ottimizzarla da solo e usciva una divisione hardware
    // (decine di cicli) eseguita ~1.300 volte per frame, una per tile disegnato.
    const uint32_t bank = c->bank_shift ? (off >> c->bank_shift)
                                        : (off / c->bank_size);
    uint8_t *p = c->ptr[bank];
    if (p) { c->hit++; return p; }
    return gfx_cache_miss(c, bank);
}
