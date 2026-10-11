#ifndef WIFI_FM_H
#define WIFI_FM_H

#include <Arduino.h>
#include "input.h"
#include "fm_link.h"

// ============================================================================
// WifiFileMenu — schermata "FILE MANAGER WIFI". Si apre dalla voce omonima del
// carosello BootMenu (cornice CIANO).
//
// Mentre e' aperta:
//   - il P4 chiede al C6 di accendere la rete "SPINNERINO" (FM_T_WIFI_ON ogni
//     secondo, vedi fm_link.h);
//   - il C6 serve la pagina web su http://192.168.4.1 e gira al P4 le richieste
//     del browser (elenco, carica, scarica, cancella, rinomina, nuova cartella);
//   - il P4 le esegue sulla microSD e risponde.
// A schermo: nome rete, password, indirizzo, stato del C6 e il file in viaggio.
//
// All'uscita (START 3 s, gestita dal .ino) close() libera la UART e i buffer:
// fuori da questa schermata il file manager non costa un byte di RAM e il C6
// spegne il Wi-Fi da solo dopo FM_HEARTBEAT_MS.
//
// ⚠️ update() NON va seguita da delay(16) come negli altri menu: e' lei che
// tiene il ritmo (gira ~30 ms di fila sulla UART, ridisegna 4 volte al
// secondo). Con 16 ms di pausa per giro il trasferimento andrebbe a meta'.
// Anche render() serve la UART fra una striscia e l'altra, cosi' ridisegnare
// lo schermo non lascia il C6 ad aspettare l'OK.
// ============================================================================
class WifiFileMenu {
public:
  void init(Input *input, unsigned short *out_strip);
  void update();        // serve la UART del C6 per ~30 ms
  void render();        // ridisegna solo ogni 250 ms
  void close();         // libera UART e buffer (uscita dalla schermata)

private:
  Input          *_input;
  unsigned short *_out_strip;
  bool            _open;

  // collegamento col C6
  FmRx            _rx;
  uint8_t        *_rxbuf;          // FM_MAX_PAYLOAD (PSRAM)
  uint8_t        *_txbuf;          // FM_MAX_PAYLOAD (PSRAM)
  uint8_t         _seq;
  unsigned long   _hb_t;           // ultimo FM_T_WIFI_ON spedito
  unsigned long   _st_t;           // ultimo FM_T_STATUS ricevuto (0 = mai)
  FmStatus        _st;
  unsigned long   _render_t;

  // file in scrittura (upload dal browser)
  int             _wfd;
  uint32_t        _wpos, _wsize;
  // file in lettura (download verso il browser)
  int             _rfd;
  uint32_t        _rsize, _rpos;

  // cosa mostrare
  char           *_path;                    // file in viaggio (PSRAM, FM_MAX_PATH+1)
  bool            _upload;                  // true = arriva, false = parte
  bool            _wfail;                   // scrittura SD fallita DOPO l'OK: si dice al prossimo WRITE/CLOSE
  unsigned long   _op_t;                    // ultimo pezzo trasferito
  unsigned long   _speed_t0;
  uint32_t        _speed_b0;
  uint32_t        _kbs;                     // KB/s misurati
  int             _n_in, _n_out, _n_del;
  char            _err[40];
  unsigned long   _err_t;

  void send(uint8_t type, uint8_t seq, const uint8_t *data, uint16_t len);
  void ok(const uint8_t *data = nullptr, uint16_t len = 0);
  void fail(const char *msg);
  void handle();
  void pump();          // legge quello che c'e' sulla UART, senza aspettare
  bool make_path(const uint8_t *p, int n, char *out, bool allow_root = false);
  void close_w(bool remove_it);
  void close_r();
  void draw_strip(int strip_y);
};

#endif // WIFI_FM_H
