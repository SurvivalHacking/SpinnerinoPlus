// ============================================================================
// gfx_cache.cpp — vedi gfx_cache.h per il perche'.
// Portato da gngeo: roms.c (init_sprite_cache, costruzione della mappa dei
// settori) e video.c (get_cached_sprite_ptr).
// ============================================================================
#include "gfx_cache.h"

#include <string.h>
#include <stdio.h>
#include "esp_heap_caps.h"
#if GFX_CACHE_MISURA
#include "esp_timer.h"
#endif
// ADATTATO A SPINNERINO: l'API SD qui e' emulation/sd_card.h.
// Il resto (f_open + cammino della catena FAT con disk_read) e'
// FatFs di ESP-IDF, identico sulle due schede: non si tocca.
#include "emulation/sd_card.h"

// ⚠️ Questo file sta in root, quindi l'IDE lo compila SEMPRE — anche adesso che
// nessun gioco lo usa ancora. Se gli header di FATFS non sono raggiungibili
// nella versione del core Arduino installata, senza questa guardia si
// romperebbe la compilazione di TUTTI i giochi. Con la guardia il modulo si
// riduce a due funzioni che dicono "non disponibile", e il chiamante mostra un
// errore leggibile invece di non compilare.
#if defined(__has_include)
#  if __has_include("ff.h") && __has_include("diskio.h")
#    define NG_HA_FATFS 1
#  endif
#endif
#ifndef NG_HA_FATFS
#  define NG_HA_FATFS 0
#endif

#if NG_HA_FATFS
extern "C" {
#include "ff.h"
#include "diskio.h"
}
#else
#warning "gfx_cache: ff.h/diskio.h non trovati — la cache GFX da SD e' disattivata"
bool gfx_cache_init(GfxCache *c, const char *, uint32_t, uint32_t)
{
    memset(c, 0, sizeof(*c));
    printf("gfx_cache: FATFS non disponibile in questa build\n");
    return false;
}
void gfx_cache_free(GfxCache *c) { memset(c, 0, sizeof(*c)); }
const uint8_t *gfx_cache_miss(GfxCache *c, uint32_t) { return c->data; }
#endif

#if NG_HA_FATFS

// ---------------------------------------------------------------------------
// La mappa dei settori: per ogni banco, l'LBA del suo primo settore.
// Si cammina la catena FAT A MANO leggendo i settori della FAT con disk_read,
// invece di usare f_lseek. ⭐ E' la scelta di gngeo, motivata nel suo codice:
// f_lseek sbaglia il cluster ai confini e un banco su 4 finiva altrove.
// ---------------------------------------------------------------------------
static bool costruisci_mappa(GfxCache *c, const char *path)
{
    FIL fil;
    if (f_open(&fil, path, FA_READ) != FR_OK) {
        printf("gfx_cache: non apro %s\n", path);
        return false;
    }
    FATFS *fs = fil.obj.fs;
    const DWORD dim   = (DWORD)fil.obj.objsize;
    const DWORD sclust = fil.obj.sclust;
    const WORD  csize = fs->csize;                 // cluster in SETTORI
    // ⛔ NON usare FF_MAX_SS: e' il settore MASSIMO ammesso dalla
    // configurazione, non quello vero. In ESP-IDF vale 4096, mentre la SD ha
    // settori da 512 — e con 4096 usciva "1 settore per banco" invece di 8,
    // l'indice nella FAT finiva altrove e la catena si leggeva come spazzatura
    // ("catena FAT rotta al banco 16, cluster 94741925"). FATFS tiene il valore
    // vero in `fs->ssize`, che pero' esiste SOLO se il range e' variabile.
#if FF_MAX_SS != FF_MIN_SS
    const UINT  ssize = fs->ssize;
#else
    const UINT  ssize = FF_MAX_SS;
#endif
    c->pdrv = fs->pdrv;
    if (ssize == 0 || (c->bank_size % ssize)) {
        printf("gfx_cache: settore %u incompatibile col banco %lu\n",
               (unsigned)ssize, (unsigned long)c->bank_size);
        f_close(&fil);
        return false;
    }

    c->sect_per_bank = c->bank_size / ssize;
    const uint32_t banchi_per_cluster = (uint32_t)(csize * ssize) / c->bank_size;
    c->n_bank = dim / c->bank_size;

    printf("gfx_cache: %s  %lu B  cluster %u sett  banco %lu B -> %lu banchi, "
           "%lu sett/banco, %lu banchi/cluster\n",
           path, (unsigned long)dim, (unsigned)csize,
           (unsigned long)c->bank_size, (unsigned long)c->n_bank,
           (unsigned long)c->sect_per_bank, (unsigned long)banchi_per_cluster);

    if (banchi_per_cluster < 1) {
        // un banco a cavallo di due cluster non sarebbe contiguo sul disco
        printf("gfx_cache: cluster (%u B) piu' piccolo del banco (%lu B)\n",
               (unsigned)(csize * ssize), (unsigned long)c->bank_size);
        f_close(&fil);
        return false;
    }
    if (dim % c->bank_size) {
        printf("gfx_cache: il file non e' multiplo del banco\n");
        f_close(&fil);
        return false;
    }

    c->sector_map = (uint32_t *)heap_caps_calloc(c->n_bank, sizeof(uint32_t),
                                                 MALLOC_CAP_SPIRAM);
    // il buffer per leggere la FAT deve poter fare DMA -> RAM interna
    uint8_t *fat_buf = (uint8_t *)heap_caps_aligned_alloc(128,ssize,
                                                          MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    if (!c->sector_map || !fat_buf) {
        printf("gfx_cache: memoria esaurita per la mappa\n");
        if (fat_buf) heap_caps_free(fat_buf);
        f_close(&fil);
        return false;
    }

    uint32_t banco = 0;
    DWORD clust = sclust;
    while (banco < c->n_bank) {
        if (clust < 2 || clust >= fs->n_fatent) {
            printf("gfx_cache: catena FAT rotta al banco %lu (cluster %lu)\n",
                   (unsigned long)banco, (unsigned long)clust);
            heap_caps_free(fat_buf); f_close(&fil);
            return false;
        }
        // primo settore del cluster (ff.h: clst2sect)
        const DWORD base = fs->database + (DWORD)fs->csize * (clust - 2);
        for (uint32_t b = 0; b < banchi_per_cluster && banco < c->n_bank; b++, banco++)
            c->sector_map[banco] = (uint32_t)(base + b * c->sect_per_bank);

        // passo al cluster seguente leggendo la voce FAT a mano
        DWORD next;
        if (fs->fs_type == FS_FAT32) {
            const DWORD sett = fs->fatbase + (clust / (ssize / 4));
            if (disk_read(c->pdrv, fat_buf, sett, 1) != RES_OK) {
                printf("gfx_cache: lettura FAT fallita al settore %lu\n",
                       (unsigned long)sett);
                heap_caps_free(fat_buf); f_close(&fil);
                return false;
            }
            const DWORD idx = clust % (ssize / 4);
            next = (((DWORD)fat_buf[idx * 4 + 3] << 24) |
                    ((DWORD)fat_buf[idx * 4 + 2] << 16) |
                    ((DWORD)fat_buf[idx * 4 + 1] <<  8) |
                     (DWORD)fat_buf[idx * 4 + 0]) & 0x0FFFFFFF;
        } else if (fs->fs_type == FS_FAT16) {
            const DWORD sett = fs->fatbase + (clust / (ssize / 2));
            if (disk_read(c->pdrv, fat_buf, sett, 1) != RES_OK) {
                heap_caps_free(fat_buf); f_close(&fil);
                return false;
            }
            const DWORD idx = clust % (ssize / 2);
            next = ((DWORD)fat_buf[idx * 2 + 1] << 8) | (DWORD)fat_buf[idx * 2 + 0];
        } else {
            printf("gfx_cache: serve FAT16 o FAT32 (fs_type %d). "
                   "exFAT non e' gestito.\n", (int)fs->fs_type);
            heap_caps_free(fat_buf); f_close(&fil);
            return false;
        }
        clust = next;
    }

    printf("gfx_cache: mappa costruita, banco 0 -> settore %lu, "
           "banco %lu -> settore %lu\n",
           (unsigned long)c->sector_map[0], (unsigned long)(c->n_bank - 1),
           (unsigned long)c->sector_map[c->n_bank - 1]);
    heap_caps_free(fat_buf);
    f_close(&fil);
    return true;
}


bool gfx_cache_init(GfxCache *c, const char *path,
                    uint32_t bank_size, uint32_t cache_bytes)
{
    memset(c, 0, sizeof(*c));
    c->bank_size = bank_size;
    // log2 se e' potenza di 2 (4096 lo e'): serve al path caldo, vedi gfx_cache.h
    if (bank_size && (bank_size & (bank_size - 1)) == 0)
        for (uint8_t s = 0; s < 32; s++) if ((1u << s) == bank_size) { c->bank_shift = s; break; }
    if (!sd_is_mounted()) { printf("gfx_cache: SD non montata\n"); return false; }
    if (!costruisci_mappa(c, path)) { gfx_cache_free(c); return false; }

    // il bounce deve poter fare DMA: la cache sta in PSRAM, disk_read no
    c->bounce = (uint8_t *)heap_caps_aligned_alloc(128,bank_size,
                                                   MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    c->ptr = (uint8_t **)heap_caps_calloc(c->n_bank, sizeof(uint8_t *), MALLOC_CAP_SPIRAM);
    if (!c->bounce || !c->ptr) {
        printf("gfx_cache: memoria esaurita (bounce/ptr)\n");
        gfx_cache_free(c);
        return false;
    }

    // quanta cache: si parte dal tetto chiesto e si dimezza finche' entra.
    // ⚠️ NESSUNA soglia minima inventata qui: quanto sia sensato scendere lo
    // decide il CHIAMANTE, che sa quanti accessi fa. Un minimo arbitrario in
    // questa funzione la farebbe fallire in casi in cui andrebbe benissimo.
    uint32_t voluti = cache_bytes - (cache_bytes % bank_size);
    if (voluti > c->n_bank * bank_size) voluti = c->n_bank * bank_size;
    while (voluti >= bank_size) {
        c->data = (uint8_t *)heap_caps_malloc(voluti, MALLOC_CAP_SPIRAM);
        if (c->data) break;
        // ⚠️ si scende del 25%, non della META': dimezzare da 16 MB avrebbe dato
        // 8, cioe' MENO dei 12 che entravano — un ripiego che peggiora.
        voluti = (uint32_t)((uint64_t)voluti * 3 / 4);
        voluti -= voluti % bank_size;
    }
    if (!c->data) { printf("gfx_cache: niente PSRAM per la cache\n"); gfx_cache_free(c); return false; }

    c->n_slot = voluti / bank_size;
    c->slot_bank = (int32_t *)heap_caps_malloc(c->n_slot * sizeof(int32_t), MALLOC_CAP_SPIRAM);
    if (!c->slot_bank) { gfx_cache_free(c); return false; }
    for (uint32_t i = 0; i < c->n_slot; i++) c->slot_bank[i] = -1;

    c->evict = 0;
    c->ok = true;
    printf("gfx_cache: %lu slot da %lu B = %.1f MB residenti su %.1f MB (%.0f%%)\n",
           (unsigned long)c->n_slot, (unsigned long)bank_size,
           voluti / 1048576.0, c->n_bank * (double)bank_size / 1048576.0,
           100.0 * c->n_slot / c->n_bank);
    return true;
}


void gfx_cache_free(GfxCache *c)
{
    if (c->data)       heap_caps_free(c->data);
    if (c->ptr)        heap_caps_free(c->ptr);
    if (c->slot_bank)  heap_caps_free(c->slot_bank);
    if (c->sector_map) heap_caps_free(c->sector_map);
    if (c->bounce)     heap_caps_free(c->bounce);
    memset(c, 0, sizeof(*c));
}


// get_cached_sprite_ptr di gngeo (video.c), con lo stesso sfratto round-robin.
const uint8_t *gfx_cache_miss(GfxCache *c, uint32_t bank)
{
    c->miss++;
    if (bank >= c->n_bank) return c->data;      // fuori dal file: si evita il botto

    const uint32_t slot = c->evict;
    if (++c->evict >= c->n_slot) c->evict = 0;

    // chi stava qui prima non e' piu' residente
    const int32_t vecchio = c->slot_bank[slot];
    if (vecchio >= 0) c->ptr[vecchio] = nullptr;

    uint8_t *dest = c->data + (size_t)slot * c->bank_size;
    bool letto = false;
#if GFX_CACHE_MISURA
    // ⚠️ Il cronometro avvolge SOLO le letture: la memcpy dal bounce e il resto
    // non sono tempo di SD, e metterceli dentro gonfierebbe la voce che serve a
    // decidere ([[feedback_ottimizzare_contando_accessi]] caso 7: il primo
    // controllo di un profilatore e' che misuri il paziente e non il termometro).
    const int64_t t_sd0 = esp_timer_get_time();
#endif
    for (int prova = 0; prova < 3 && !letto; prova++)
        if (disk_read(c->pdrv, c->bounce, c->sector_map[bank],
                      c->sect_per_bank) == RES_OK) letto = true;
#if GFX_CACHE_MISURA
    c->us_miss += (uint32_t)(esp_timer_get_time() - t_sd0);
#endif
    if (letto) {
        memcpy(dest, c->bounce, c->bank_size);
        c->slot_bank[slot] = (int32_t)bank;
        c->ptr[bank] = dest;            /* residente solo se la lettura e' RIUSCITA */
        return dest;
    } else {
        // ⛔ Un banco NON letto non va pubblicato come residente: prima
        // `ptr[bank] = dest` veniva eseguito anche dopo un errore, quindi
        // gfx_cache_bank() lo trovava valido, contava un HIT e restituiva 4096
        // byte di zeri **senza mai piu' riprovare** fino allo sfratto. Nel
        // NeoGeo il colore 0 e' trasparente: un solo errore transitorio
        // diventava schermo nero permanente.
        // meglio un banco nero che dati a caso: l'immagine lo mostra e si vede
        memset(dest, 0, c->bank_size);
        if (++c->err <= 10)
            printf("gfx_cache: lettura fallita, banco %lu (errori %lu)\n",
                   (unsigned long)bank, (unsigned long)c->err);
        c->slot_bank[slot] = -1;        /* slot LIBERO: al prossimo giro si riprova */
    }
    return dest;
}

#endif  // NG_HA_FATFS
