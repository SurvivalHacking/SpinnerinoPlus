#ifndef _SD_CARD_H_
#define _SD_CARD_H_

// ============================================================================
// sd_card.h — microSD SDMMC slot 0 per SPINNERINO P4 (giochi LASERDISC)
// ============================================================================
#include <stdint.h>
#include <stddef.h>

enum {
  SD_ST_NONE = 0,     // mai tentato
  SD_ST_OK,           // montata
  SD_ST_LDO_FAIL,     // LDO on-chip non inizializzato
  SD_ST_MOUNT_FAIL,   // card assente / non leggibile / FAT non valida
};

// Monta la microSD (idempotente: la seconda chiamata e' un no-op).
// Lazy: chiamala solo quando serve davvero (all'ingresso nel menu LASERDISC),
// NON in setup() — se non ci sono giochi da SD non ha senso pagarne il costo.
bool sd_mount(void);

bool sd_is_mounted(void);
int  sd_status(void);
const char *sd_status_str(void);   // messaggio per overlay a schermo

// true se la cartella esiste sulla SD (es. "/sdcard/dl")
bool sd_dir_exists(const char *path);

#endif // _SD_CARD_H_
