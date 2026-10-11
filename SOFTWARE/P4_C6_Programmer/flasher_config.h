/*
 * Default che nel progetto originale (ESP-IDF) arrivano da Kconfig.
 * In ambiente Arduino li fissiamo qui a mano, con gli stessi valori
 * di default della libreria upstream.
 */
#pragma once

#ifndef SERIAL_FLASHER_RESET_INVERT
#define SERIAL_FLASHER_RESET_INVERT 0
#endif

#ifndef SERIAL_FLASHER_BOOT_INVERT
#define SERIAL_FLASHER_BOOT_INVERT 0
#endif

#ifndef SERIAL_FLASHER_RESET_HOLD_TIME_MS
#define SERIAL_FLASHER_RESET_HOLD_TIME_MS 100
#endif

#ifndef SERIAL_FLASHER_BOOT_HOLD_TIME_MS
#define SERIAL_FLASHER_BOOT_HOLD_TIME_MS 50
#endif

#ifndef SERIAL_FLASHER_WRITE_BLOCK_RETRIES
#define SERIAL_FLASHER_WRITE_BLOCK_RETRIES 3
#endif

/*
 * Livello di log della libreria (stampato via printf, quindi visibile nel
 * Serial Monitor a 115200 baud).
 *
 * NOTA: a livello DEBUG la libreria stampa il dump esadecimale COMPLETO di
 * ogni blocco dati (4096 byte -> ~12000 caratteri stampati, per ~180
 * blocchi in una scrittura completa) -- utilissimo per diagnosticare
 * problemi di connessione, ma genera un volume di traffico sulla console
 * seriale che da solo rallenta parecchio l'intera operazione. Una volta
 * risolti i problemi di connessione, meglio tenerlo a INFO (niente dump
 * byte-per-byte, restano comunque connessione/chip/inizio-scrittura).
 * Rimettere ESP_LOADER_LOG_DEBUG solo se serve di nuovo diagnosticare nel
 * dettaglio un problema di protocollo.
 */
#ifndef SERIAL_FLASHER_LOG_LEVEL
#define SERIAL_FLASHER_LOG_LEVEL ESP_LOADER_LOG_INFO
#endif
