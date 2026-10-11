// ============================================================================
// fm_link.h — protocollo del FILE MANAGER WIFI fra ESP32-C6 e ESP32-P4
//
// ⛔ ESISTE IN DUE COPIE IDENTICHE, una per sketch:
//      SPINNERINO/emulation/fm_link.h          (lato P4)
//      SPINNERINO/C6_BLE_Bridge/fm_link.h      (lato C6)
//    Arduino non condivide header fra due sketch: se ne cambi una, copia
//    l'altra (e ricompila ENTRAMBI i firmware), altrimenti i due chip parlano
//    due lingue diverse.
//
// IL FILO
// -------
// Nessun filo nuovo: si usano le piste SDIO che sulla Waveshare ESP32-P4-WIFI6
// collegano gia' il P4 al C6 (le stesse che il firmware di fabbrica usa per
// ESP-Hosted). Qui il C6 non fa ESP-Hosted, quindi sono libere e diventano una
// UART veloce, instradata via GPIO matrix su entrambi i chip:
//
//     segnale SDIO   pin P4          pin C6
//     D0             GPIO14 (RX) <-- GPIO20 (TX)
//     D1             GPIO15 (TX) --> GPIO21 (RX)
//
// Il gamepad Bluetooth NON passa di qui: resta sul suo filo di sempre
// (C6 GPIO16 -> P4 GPIO3, 115200), che non viene toccato.
//
// QUANDO E' ATTIVO
// ----------------
// Solo con la schermata "FILE MANAGER WIFI" aperta sul P4. Il P4 manda
// FM_T_WIFI_ON ogni secondo (fa da battito cardiaco): il C6 accende la rete,
// e la spegne da solo se per FM_HEARTBEAT_MS non sente piu' il P4 (uscita dalla
// schermata, riavvio, gioco avviato). Durante i giochi il Wi-Fi e' SPENTO.
//
// IL PACCHETTO (uguale nei due versi)
// -----------------------------------
//   [0xA5][0x5A][tipo][seq][len lo][len hi][payload: len byte][crc lo][crc hi]
//   crc = CRC16-CCITT (poly 0x1021, init 0xFFFF) su tipo..fine payload.
//   Un pacchetto con CRC sbagliato si scarta in silenzio: chi ha chiesto non
//   riceve risposta, va in timeout e RIPETE. Per questo WRITE e READ portano
//   l'OFFSET nel file: ripetere lo stesso pezzo non lo scrive due volte.
//
// CHI COMANDA
// -----------
// Il C6 (che riceve le richieste dal browser) CHIEDE, il P4 (che ha la SD)
// RISPONDE con lo stesso `seq`. Unica eccezione: FM_T_WIFI_ON parte dal P4 e
// il C6 risponde con FM_T_STATUS.
// ============================================================================
#pragma once
#include <stdint.h>

#define FM_LINK_BAUD      4000000   // divisore INTERO su entrambi: 80M/20 (PLL_F80M su P4 e C6)
                                    // ⚠️ 5000000 (80M/16) PROVATO il 07/10/2026: 370 KB/s contro 390
                                    // a 4M -> il limite e' il Wi-Fi del C6, non il filo. Non rialzare.
#define FM_P4_RX_PIN      14        // <- C6 GPIO20 (SDIO D0)
#define FM_P4_TX_PIN      15        // -> C6 GPIO21 (SDIO D1)
#define FM_C6_TX_PIN      20
#define FM_C6_RX_PIN      21

#define FM_SYNC0          0xA5
#define FM_SYNC1          0x5A
#define FM_HDR_LEN        6         // sync0 sync1 tipo seq lenlo lenhi
#define FM_MAX_DATA       8192      // byte di file per WRITE / READ
#define FM_MAX_PAYLOAD    (FM_MAX_DATA + 16)
#define FM_MAX_PATH       255       // percorso relativo alla radice della SD

#define FM_HEARTBEAT_MS   4000      // C6: niente WIFI_ON per 4 s -> rete spenta

// Nome e password della rete creata dal C6 (password: minimo 8 caratteri)
#define FM_AP_SSID        "SPINNERINO"
#define FM_AP_PASS        "spinnerino"
#define FM_AP_IP          "192.168.4.1"

// ---- tipi: richieste C6 -> P4 ------------------------------------------------
// I percorsi viaggiano SENZA il punto di mount ("/roms/x.zip", non
// "/sdcard/roms/x.zip") e senza terminatore: la lunghezza e' quella del payload.
enum {
  FM_T_INFO     = 0x01,  // ()                     -> OK: u32 totKB, u32 liberiKB
  FM_T_LIST     = 0x02,  // u16 primo, percorso    -> OK: u8 altri, righe "D|F\t<byte>\t<nome>\n"
  FM_T_OPEN_W   = 0x03,  // u32 dimensione, perc.  -> OK  (crea le cartelle mancanti)
  FM_T_WRITE    = 0x04,  // u32 offset, dati       -> OK
  FM_T_CLOSE_W  = 0x05,  // ()                     -> OK  (ERR se la dimensione non torna)
  FM_T_ABORT_W  = 0x06,  // ()                     -> OK  (chiude e CANCELLA il file a meta')
  FM_T_OPEN_R   = 0x07,  // percorso               -> OK: u32 dimensione
  FM_T_READ     = 0x08,  // u32 offset, u16 quanti -> OK: dati
  FM_T_CLOSE_R  = 0x09,  // ()                     -> OK
  FM_T_DELETE   = 0x0A,  // percorso               -> OK  (cartelle: con tutto il contenuto)
  FM_T_MKDIR    = 0x0B,  // percorso               -> OK
  FM_T_RENAME   = 0x0C,  // vecchio \0 nuovo       -> OK
};

// ---- risposte P4 -> C6 -------------------------------------------------------
enum {
  FM_T_OK       = 0x80,  // payload secondo la richiesta
  FM_T_ERR      = 0x81,  // payload = messaggio di errore (testo)
};

// ---- servizio ----------------------------------------------------------------
enum {
  FM_T_WIFI_ON  = 0x90,  // P4 -> C6, ogni secondo: "tieni accesa la rete"
  FM_T_STATUS   = 0x91,  // C6 -> P4: struct FmStatus
};

#pragma pack(push, 1)
struct FmStatus {
  uint8_t  version;      // FM_PROTO_VERSION
  uint8_t  ap_on;        // rete accesa
  uint8_t  clients;      // dispositivi collegati alla rete
  uint8_t  pad_connected;// gamepad BT collegato (solo informativo)
};
#pragma pack(pop)
#define FM_PROTO_VERSION  1

static inline uint16_t fm_crc16(const uint8_t *p, uint32_t n, uint16_t crc = 0xFFFF)
{
  while (n--) {
    crc ^= (uint16_t)(*p++) << 8;
    for (int i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

static inline void fm_put_u16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static inline void fm_put_u32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
static inline uint16_t fm_get_u16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static inline uint32_t fm_get_u32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- ricevitore a stati (identico sui due lati) ----------------------------
// Si alimenta un byte alla volta; ritorna true quando un pacchetto completo e
// con CRC giusto e' pronto in `type/seq/len/buf`. Un pacchetto rotto o troppo
// lungo fa ripartire la caccia al sync dal byte successivo.
struct FmRx {
  uint8_t  st;
  uint8_t  type, seq;
  uint16_t len, got;
  uint16_t crc_rx;
  uint8_t *buf;          // almeno FM_MAX_PAYLOAD byte, lo alloca chi lo usa

  void reset() { st = 0; }

  bool feed(uint8_t c) {
    switch (st) {
      case 0: if (c == FM_SYNC0) st = 1; return false;
      case 1: st = (c == FM_SYNC1) ? 2 : (c == FM_SYNC0 ? 1 : 0); return false;
      case 2: type = c; st = 3; return false;
      case 3: seq = c;  st = 4; return false;
      case 4: len = c;  st = 5; return false;
      case 5:
        len |= (uint16_t)c << 8;
        if (len > FM_MAX_PAYLOAD) { st = 0; return false; }
        got = 0;
        st = len ? 6 : 7;
        return false;
      case 6:
        buf[got++] = c;
        if (got >= len) st = 7;
        return false;
      case 7: crc_rx = c; st = 8; return false;
      case 8: {
        crc_rx |= (uint16_t)c << 8;
        st = 0;
        uint8_t h[4] = { type, seq, (uint8_t)len, (uint8_t)(len >> 8) };
        uint16_t crc = fm_crc16(h, 4);
        crc = fm_crc16(buf, len, crc);
        return crc == crc_rx;
      }
    }
    st = 0;
    return false;
  }
};

// Costruisce l'intestazione e il CRC di un pacchetto. `hdr` 6 byte, `tail` 2.
static inline void fm_frame(uint8_t type, uint8_t seq, const uint8_t *data, uint16_t len,
                            uint8_t hdr[FM_HDR_LEN], uint8_t tail[2])
{
  hdr[0] = FM_SYNC0; hdr[1] = FM_SYNC1; hdr[2] = type; hdr[3] = seq;
  hdr[4] = (uint8_t)len; hdr[5] = (uint8_t)(len >> 8);
  uint16_t crc = fm_crc16(hdr + 2, 4);
  crc = fm_crc16(data, len, crc);
  tail[0] = (uint8_t)crc; tail[1] = (uint8_t)(crc >> 8);
}
