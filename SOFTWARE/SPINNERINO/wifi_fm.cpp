// ============================================================================
// wifi_fm.cpp — FILE MANAGER WIFI, lato P4: schermata + server dei file su SD
//
// Il Wi-Fi e la pagina web stanno sul C6 (C6_BLE_Bridge/fm_wifi.cpp). Il P4
// non ha rete: riceve dal C6 richieste gia' masticate ("apri questo file",
// "scrivi questi 8 KB all'offset X", "dammi l'elenco di /roms") sulla UART
// veloce delle piste SDIO e le esegue sulla microSD. Protocollo: fm_link.h.
//
// Tutto cio' che serve (UART, buffer) si prende in init() e si rende in
// close(): fuori dalla schermata non resta nulla in RAM.
// ============================================================================
#include <Arduino.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>

#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"

#include "config.h"
#include "emulation/wifi_fm.h"
#include "emulation/video.h"
#include "emulation/sd_card.h"
#include "emulation/font8.h"

extern Video video;

#define FM_UART        UART_NUM_2        // UART1 = gamepad BT (input.cpp)
#define FM_UART_RX_BUF 20480             // > DUE pacchetti WRITE interi: il C6 ne tiene 2 in viaggio
#define FM_UART_TX_BUF 12288             // > un pacchetto di risposta READ

// ---------------------------------------------------------------------------
// Disegno: stessa rotazione software del carosello (vedi boot_menu.cpp). Le
// coordinate sono PORTRAIT 240x320; area visibile x_p 8..239.
// ---------------------------------------------------------------------------
static inline void px(unsigned short *strip, int sy, int x_p, int y_p, unsigned short c)
{
  int x_l = y_p;
  int y_l = 239 - x_p;
  if (x_l < 0 || x_l >= TFT_FB_W || y_l < 0 || y_l >= TFT_FB_H) return;
  int y_in = y_l - sy;
  if (y_in < 0 || y_in >= 8) return;
  strip[y_in * TFT_FB_W + x_l] = c;
}

// Minuscole 8x8: FONT8 ha solo le MAIUSCOLE (char_to_idx() converte), ma qui
// la password e' "spinnerino" minuscolo e a schermo deve leggersi COSI' com'e',
// altrimenti chi la copia scrive SPINNERINO e la rete la rifiuta. Valgono solo
// per questa schermata: gli altri menu restano tutti maiuscoli.
static const unsigned char LOWER8[26][8] PROGMEM = {
  {0x00,0x00,0x3C,0x06,0x3E,0x66,0x3E,0x00}, // a
  {0x60,0x60,0x7C,0x66,0x66,0x66,0x7C,0x00}, // b
  {0x00,0x00,0x3C,0x60,0x60,0x60,0x3C,0x00}, // c
  {0x06,0x06,0x3E,0x66,0x66,0x66,0x3E,0x00}, // d
  {0x00,0x00,0x3C,0x66,0x7E,0x60,0x3C,0x00}, // e
  {0x1C,0x30,0x7C,0x30,0x30,0x30,0x30,0x00}, // f
  {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x3C}, // g
  {0x60,0x60,0x7C,0x66,0x66,0x66,0x66,0x00}, // h
  {0x18,0x00,0x38,0x18,0x18,0x18,0x3C,0x00}, // i
  {0x0C,0x00,0x1C,0x0C,0x0C,0x0C,0x6C,0x38}, // j
  {0x60,0x60,0x66,0x6C,0x78,0x6C,0x66,0x00}, // k
  {0x38,0x18,0x18,0x18,0x18,0x18,0x3C,0x00}, // l
  {0x00,0x00,0x76,0x7F,0x6B,0x6B,0x63,0x00}, // m
  {0x00,0x00,0x7C,0x66,0x66,0x66,0x66,0x00}, // n
  {0x00,0x00,0x3C,0x66,0x66,0x66,0x3C,0x00}, // o
  {0x00,0x00,0x7C,0x66,0x66,0x7C,0x60,0x60}, // p
  {0x00,0x00,0x3E,0x66,0x66,0x3E,0x06,0x06}, // q
  {0x00,0x00,0x7C,0x66,0x60,0x60,0x60,0x00}, // r
  {0x00,0x00,0x3E,0x60,0x3C,0x06,0x7C,0x00}, // s
  {0x30,0x30,0x7C,0x30,0x30,0x30,0x1C,0x00}, // t
  {0x00,0x00,0x66,0x66,0x66,0x66,0x3E,0x00}, // u
  {0x00,0x00,0x66,0x66,0x66,0x3C,0x18,0x00}, // v
  {0x00,0x00,0x63,0x6B,0x6B,0x7F,0x36,0x00}, // w
  {0x00,0x00,0x66,0x3C,0x18,0x3C,0x66,0x00}, // x
  {0x00,0x00,0x66,0x66,0x66,0x3E,0x06,0x3C}, // y
  {0x00,0x00,0x7E,0x0C,0x18,0x30,0x7E,0x00}, // z
};

// carattere 8x8 ingrandito `k` volte (minuscole comprese)
static void chr(unsigned short *strip, int sy, int x_p, int y_p, char ch,
                unsigned short c, int k)
{
  const bool low = (ch >= 'a' && ch <= 'z');
  const unsigned char *glifo = low ? LOWER8[ch - 'a'] : FONT8[char_to_idx(ch)];
  for (int r = 0; r < 8; r++) {
    unsigned char row = pgm_read_byte(&glifo[r]);
    if (!row) continue;
    for (int b = 0; b < 8; b++)
      if (row & (0x80 >> b))
        for (int dy = 0; dy < k; dy++)
          for (int dx = 0; dx < k; dx++)
            px(strip, sy, x_p + b * k + dx, y_p + r * k + dy, c);
  }
}

static const int X0 = 14, XW = 226;      // area utile, come la schermata TEST

static void txt_c(unsigned short *strip, int sy, int y_p, const char *s,
                  unsigned short c, int k = 1)
{
  // la strip copre solo 8 righe landscape = 8 colonne portrait: si salta
  // subito la scritta se non puo' toccarle (y_p e' la x landscape, sempre
  // dentro). Il test vero lo fa px(); qui basta non sbagliare la x.
  int w = (int)strlen(s) * 8 * k;
  int x = X0 + (XW - w) / 2;
  if (x < X0) x = X0;
  for (int i = 0; s[i]; i++) chr(strip, sy, x + i * 8 * k, y_p, s[i], c, k);
}

static void barra(unsigned short *strip, int sy, int y_p, uint32_t fatti, uint32_t tot,
                  unsigned short bordo, unsigned short pieno)
{
  const int BW = 200, BH = 12, BX = X0 + (XW - BW) / 2;
  int n = tot ? (int)((uint64_t)fatti * (BW - 2) / tot) : 0;
  if (n > BW - 2) n = BW - 2;
  for (int x = 0; x < BW; x++) { px(strip, sy, BX + x, y_p, bordo); px(strip, sy, BX + x, y_p + BH - 1, bordo); }
  for (int y = 0; y < BH; y++) { px(strip, sy, BX, y_p + y, bordo); px(strip, sy, BX + BW - 1, y_p + y, bordo); }
  for (int y = 1; y < BH - 1; y++)
    for (int x = 0; x < n; x++) px(strip, sy, BX + 1 + x, y_p + y, pieno);
}

// "12.3 MB" / "456 KB" / "78 B"
static void fmt_size(char *o, size_t n, uint32_t b)
{
  if (b >= 1024u * 1024u)
    snprintf(o, n, "%lu.%lu MB", (unsigned long)(b >> 20), (unsigned long)(((b & 0xFFFFF) * 10) >> 20));
  else if (b >= 1024u)
    snprintf(o, n, "%lu KB", (unsigned long)(b >> 10));
  else
    snprintf(o, n, "%lu B", (unsigned long)b);
}

// ---------------------------------------------------------------------------
// Operazioni sulla SD
// ---------------------------------------------------------------------------
// percorsi di lavoro: ne' sullo stack ne' fra i globali (RAM interna), si
// prende in PSRAM con la schermata e si rende all'uscita
#define S_TMP_N (FM_MAX_PATH + 300)
static char *s_tmp = nullptr;

// crea tutte le cartelle che mancano lungo `full` (escluso l'ultimo pezzo)
static void mkdirs_parent(char *full)
{
  const size_t base = strlen(SD_MOUNT_POINT);
  for (char *p = full + base + 1; *p; p++) {
    if (*p != '/') continue;
    *p = 0;
    mkdir(full, 0775);                   // EEXIST va benissimo
    *p = '/';
  }
}

// Cancella file o cartella con tutto il contenuto. `path` viene allungato e
// riaccorciato sul posto: niente nomi sullo stack, la ricorsione resta leggera
// (lo stack del loop e' di pochi KB).
static bool rm_tree(char *path, size_t cap, int depth)
{
  struct stat st;
  if (stat(path, &st) != 0) return false;
  if (!S_ISDIR(st.st_mode)) return unlink(path) == 0;
  if (depth > 16) return false;
  const size_t L = strlen(path);
  for (;;) {
    DIR *d = opendir(path);
    if (!d) return false;
    bool trovato = false;
    struct dirent *e;
    while ((e = readdir(d)) != nullptr) {
      if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
      if (L + 2 + strlen(e->d_name) > cap) { closedir(d); return false; }
      path[L] = '/';
      strcpy(path + L + 1, e->d_name);
      trovato = true;
      break;
    }
    closedir(d);                         // si cancella a cartella CHIUSA
    if (!trovato) break;
    bool r = rm_tree(path, cap, depth + 1);
    path[L] = 0;
    if (!r) return false;
  }
  return rmdir(path) == 0;
}

// ---------------------------------------------------------------------------
// WifiFileMenu
// ---------------------------------------------------------------------------
void WifiFileMenu::init(Input *input, unsigned short *out_strip)
{
  _input     = input;
  _out_strip = out_strip;
  _seq = 0; _hb_t = 0; _st_t = 0; _render_t = 0;
  memset(&_st, 0, sizeof _st);
  _wfd = -1; _rfd = -1; _wpos = _wsize = _rsize = _rpos = 0;
  _upload = true; _wfail = false; _op_t = 0;
  _speed_t0 = millis(); _speed_b0 = 0; _kbs = 0;
  _n_in = _n_out = _n_del = 0;
  _err[0] = 0; _err_t = 0;

  sd_mount();

  _rxbuf = (uint8_t *)heap_caps_malloc(FM_MAX_PAYLOAD, MALLOC_CAP_SPIRAM);
  _txbuf = (uint8_t *)heap_caps_malloc(FM_MAX_PAYLOAD, MALLOC_CAP_SPIRAM);
  s_tmp  = (char *)heap_caps_malloc(S_TMP_N, MALLOC_CAP_SPIRAM);
  _path  = (char *)heap_caps_malloc(FM_MAX_PATH + 1, MALLOC_CAP_SPIRAM);
  if (_path) _path[0] = 0;
  _rx.buf = _rxbuf;
  _rx.reset();

  uart_config_t cfg = {};
  cfg.baud_rate  = FM_LINK_BAUD;
  cfg.data_bits  = UART_DATA_8_BITS;
  cfg.parity     = UART_PARITY_DISABLE;
  cfg.stop_bits  = UART_STOP_BITS_1;
  cfg.flow_ctrl  = UART_HW_FLOWCTRL_DISABLE;
  cfg.source_clk = UART_SCLK_DEFAULT;
  bool uart_ok = uart_driver_install(FM_UART, FM_UART_RX_BUF, FM_UART_TX_BUF, 0, NULL, 0) == ESP_OK;
  if (uart_ok) {
    uart_param_config(FM_UART, &cfg);
    uart_set_pin(FM_UART, FM_P4_TX_PIN, FM_P4_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    gpio_pullup_en((gpio_num_t)FM_P4_RX_PIN);   // C6 non ancora pronto = linea a riposo
    uart_flush_input(FM_UART);
  }
  _open = uart_ok && _rxbuf && _txbuf && s_tmp && _path;
  if (!_open) snprintf(_err, sizeof _err, "ERRORE: MEMORIA O UART");
}

void WifiFileMenu::close()
{
  close_w(true);                         // upload interrotto: niente file a meta'
  close_r();
  if (uart_is_driver_installed(FM_UART)) uart_driver_delete(FM_UART);
  gpio_reset_pin((gpio_num_t)FM_P4_TX_PIN);
  gpio_reset_pin((gpio_num_t)FM_P4_RX_PIN);
  if (_rxbuf) { heap_caps_free(_rxbuf); _rxbuf = nullptr; }
  if (_txbuf) { heap_caps_free(_txbuf); _txbuf = nullptr; }
  if (s_tmp)  { heap_caps_free(s_tmp);  s_tmp  = nullptr; }
  if (_path)  { heap_caps_free(_path);  _path  = nullptr; }
  _open = false;
}

void WifiFileMenu::send(uint8_t type, uint8_t seq, const uint8_t *data, uint16_t len)
{
  uint8_t hdr[FM_HDR_LEN], tail[2];
  fm_frame(type, seq, data, len, hdr, tail);
  uart_write_bytes(FM_UART, (const char *)hdr, FM_HDR_LEN);
  if (len) uart_write_bytes(FM_UART, (const char *)data, len);
  uart_write_bytes(FM_UART, (const char *)tail, 2);
}

void WifiFileMenu::ok(const uint8_t *data, uint16_t len) { send(FM_T_OK, _rx.seq, data, len); }

void WifiFileMenu::fail(const char *msg)
{
  send(FM_T_ERR, _rx.seq, (const uint8_t *)msg, strlen(msg));
  strlcpy(_err, msg, sizeof _err);
  _err_t = millis();
}

// Percorso dal C6 ("/roms/x.zip", senza terminatore) -> "/sdcard/roms/x.zip".
// Rifiuta "..", caratteri vietati dalla FAT e la radice (salvo allow_root).
bool WifiFileMenu::make_path(const uint8_t *p, int n, char *out, bool allow_root)
{
  if (n < 1 || n > FM_MAX_PATH || p[0] != '/') return false;
  while (n > 1 && p[n - 1] == '/') n--;  // "/roms/" -> "/roms"
  if (n == 1 && !allow_root) return false;
  for (int i = 0; i < n; i++) {
    uint8_t c = p[i];
    if (c < 0x20 || c == '\\' || c == ':' || c == '*' || c == '?' ||
        c == '"' || c == '<' || c == '>' || c == '|') return false;
    if (c == '/' && i + 1 < n && p[i + 1] == '/') return false;
    if (c == '.' && p[i - 1] == '/' && i + 1 < n && p[i + 1] == '.' &&
        (i + 2 == n || p[i + 2] == '/')) return false;
  }
  const size_t base = strlen(SD_MOUNT_POINT);
  memcpy(out, SD_MOUNT_POINT, base);
  memcpy(out + base, p, n);
  out[base + n] = 0;
  if (n == 1) out[base] = 0;             // radice = "/sdcard"
  return true;
}

void WifiFileMenu::close_w(bool remove_it)
{
  if (_wfd < 0) return;
  ::close(_wfd);
  _wfd = -1;
  if (remove_it && _path && _path[0]) {
    char full[FM_MAX_PATH + 16];
    if (make_path((const uint8_t *)_path, strlen(_path), full)) unlink(full);
  }
}

void WifiFileMenu::close_r()
{
  if (_rfd < 0) return;
  ::close(_rfd);
  _rfd = -1;
}

void WifiFileMenu::handle()
{
  const uint8_t *p = _rxbuf;
  const uint16_t n = _rx.len;
  char full[FM_MAX_PATH + 16];

  switch (_rx.type) {
    case FM_T_STATUS:
      if (n >= sizeof(FmStatus)) memcpy(&_st, p, sizeof(FmStatus));
      _st_t = millis();
      return;

    case FM_T_INFO: {
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      uint64_t tot = 0, lib = 0;
      if (esp_vfs_fat_info(SD_MOUNT_POINT, &tot, &lib) != ESP_OK) { fail("SD ILLEGGIBILE"); return; }
      uint8_t o[8];
      fm_put_u32(o, (uint32_t)(tot >> 10));
      fm_put_u32(o + 4, (uint32_t)(lib >> 10));
      ok(o, 8);
      return;
    }

    case FM_T_LIST: {
      if (n < 3 || !make_path(p + 2, n - 2, full, true)) { fail("PERCORSO NON VALIDO"); return; }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      const uint16_t primo = fm_get_u16(p);
      DIR *d = opendir(full);
      if (!d) { fail("CARTELLA INESISTENTE"); return; }
      uint8_t *o = _txbuf;
      int pos = 1, idx = 0;
      o[0] = 0;                           // "altri": 1 se l'elenco continua
      const size_t fl = strlen(full);
      struct dirent *e;
      while ((e = readdir(d)) != nullptr) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (idx++ < primo) continue;
        const bool dir = (e->d_type == DT_DIR);
        uint32_t size = 0;
        if (!dir && fl + 2 + strlen(e->d_name) < S_TMP_N) {
          struct stat st;
          snprintf(s_tmp, S_TMP_N, "%s/%s", full, e->d_name);
          if (stat(s_tmp, &st) == 0) size = (uint32_t)st.st_size;
        }
        char riga[300];
        int l = snprintf(riga, sizeof riga, "%c\t%lu\t%s\n", dir ? 'D' : 'F',
                         (unsigned long)size, e->d_name);
        if (l <= 0 || l >= (int)sizeof riga) continue;
        if (pos + l > FM_MAX_DATA) { o[0] = 1; break; }   // il resto alla prossima
        memcpy(o + pos, riga, l);
        pos += l;
      }
      closedir(d);
      ok(o, pos);
      return;
    }

    case FM_T_OPEN_W: {
      if (n < 6 || !make_path(p + 4, n - 4, full)) { fail("PERCORSO NON VALIDO"); return; }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      close_w(true);                      // un upload precedente rimasto a meta'
      mkdirs_parent(full);
      _wfd = ::open(full, O_WRONLY | O_CREAT | O_TRUNC, 0664);
      if (_wfd < 0) { fail("IMPOSSIBILE CREARE IL FILE"); return; }
      _wsize = fm_get_u32(p);
      _wpos  = 0;
      _wfail = false;
      memcpy(_path, p + 4, n - 4); _path[n - 4] = 0;
      _upload = true;
      _op_t = millis(); _speed_t0 = _op_t; _speed_b0 = 0; _kbs = 0;
      ok();
      return;
    }

    case FM_T_WRITE: {
      if (_wfail) { fail("SD PIENA O ERRORE SCRITTURA"); return; }
      if (_wfd < 0 || n < 4) { fail("NESSUN FILE APERTO"); return; }
      const uint32_t off = fm_get_u32(p);
      const uint32_t len = n - 4;
      if (off + len <= _wpos && off < _wpos) { ok(); return; }  // pezzo ripetuto: c'e' gia'
      // In anticipo: un pezzo prima di questo si e' perso sul filo (il C6 ne
      // tiene 2 in viaggio). Niente risposta: il C6 non riceve l'OK del pezzo
      // perso, va in timeout e li rispedisce tutti e due in ordine.
      if (off > _wpos) return;
      if (off != _wpos) { fail("PEZZO FUORI POSTO"); close_w(true); return; }
      // OK SUBITO, la SD dopo: il C6 spedisce il pezzo seguente e i suoi
      // ~20 ms sul filo passano mentre la SD scrive questo (il driver lo
      // raccoglie nel buffer di ricezione, ci stanno due pezzi). _rxbuf
      // resta intatto: il prossimo feed() arriva solo dopo questo return.
      // Se la scrittura fallisce lo si dice al WRITE o al CLOSE seguente,
      // e il file a meta' si cancella.
      ok();
      if (::write(_wfd, p + 4, len) != (ssize_t)len) {
        _wfail = true;
        strlcpy(_err, "SD PIENA O ERRORE SCRITTURA", sizeof _err);
        _err_t = millis();
        close_w(true);
        return;
      }
      _wpos += len;
      _op_t = millis();
      return;
    }

    case FM_T_CLOSE_W: {
      if (_wfail) { fail("SD PIENA O ERRORE SCRITTURA"); return; }
      if (_wfd < 0) { fail("NESSUN FILE APERTO"); return; }
      const bool giusto = (_wpos == _wsize);
      if (::fsync(_wfd) != 0 || ::close(_wfd) != 0) {
        _wfd = -1;
        fail("ERRORE IN CHIUSURA");
        return;
      }
      _wfd = -1;
      if (!giusto) {
        if (make_path((const uint8_t *)_path, strlen(_path), full)) unlink(full);
        fail("FILE INCOMPLETO");
        return;
      }
      _n_in++;
      ok();
      return;
    }

    case FM_T_ABORT_W:
      close_w(true);
      ok();
      return;

    case FM_T_OPEN_R: {
      if (!make_path(p, n, full)) { fail("PERCORSO NON VALIDO"); return; }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      close_r();
      struct stat st;
      if (stat(full, &st) != 0 || S_ISDIR(st.st_mode)) { fail("FILE INESISTENTE"); return; }
      _rfd = ::open(full, O_RDONLY);
      if (_rfd < 0) { fail("IMPOSSIBILE APRIRE IL FILE"); return; }
      _rsize = (uint32_t)st.st_size;
      _rpos  = 0;
      memcpy(_path, p, n); _path[n] = 0;
      _upload = false;
      _op_t = millis(); _speed_t0 = _op_t; _speed_b0 = 0; _kbs = 0;
      uint8_t o[4];
      fm_put_u32(o, _rsize);
      ok(o, 4);
      return;
    }

    case FM_T_READ: {
      if (_rfd < 0 || n < 6) { fail("NESSUN FILE APERTO"); return; }
      const uint32_t off = fm_get_u32(p);
      uint32_t quanti = fm_get_u16(p + 4);
      if (quanti > FM_MAX_DATA) quanti = FM_MAX_DATA;
      if (lseek(_rfd, off, SEEK_SET) < 0) { fail("ERRORE DI LETTURA"); return; }
      ssize_t r = ::read(_rfd, _txbuf, quanti);
      if (r < 0) { fail("ERRORE DI LETTURA"); return; }
      _rpos = off + r;
      _op_t = millis();
      if (_rpos >= _rsize && r > 0) _n_out++;
      ok(_txbuf, (uint16_t)r);
      return;
    }

    case FM_T_CLOSE_R:
      close_r();
      ok();
      return;

    case FM_T_DELETE: {
      if (!make_path(p, n, full)) { fail("PERCORSO NON VALIDO"); return; }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      strlcpy(s_tmp, full, S_TMP_N);
      if (!rm_tree(s_tmp, S_TMP_N - 1, 0)) { fail("CANCELLAZIONE NON RIUSCITA"); return; }
      _n_del++;
      ok();
      return;
    }

    case FM_T_MKDIR: {
      if (!make_path(p, n, full)) { fail("PERCORSO NON VALIDO"); return; }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      mkdirs_parent(full);
      if (mkdir(full, 0775) != 0 && errno != EEXIST) { fail("CARTELLA NON CREATA"); return; }
      ok();
      return;
    }

    case FM_T_RENAME: {
      const uint8_t *z = (const uint8_t *)memchr(p, 0, n);
      if (!z) { fail("PERCORSO NON VALIDO"); return; }
      const int n1 = z - p;
      char full2[FM_MAX_PATH + 16];
      if (!make_path(p, n1, full) || !make_path(z + 1, n - n1 - 1, full2)) {
        fail("PERCORSO NON VALIDO");
        return;
      }
      if (!sd_mount()) { fail("SD NON PRESENTE"); return; }
      struct stat st;
      if (stat(full2, &st) == 0) { fail("NOME GIA USATO"); return; }
      if (rename(full, full2) != 0) { fail("RINOMINA NON RIUSCITA"); return; }
      ok();
      return;
    }

    default:
      return;                             // tipo sconosciuto: si ignora
  }
}

void WifiFileMenu::pump()
{
  uint8_t tmp[1024];
  size_t avail = 0;
  while (uart_get_buffered_data_len(FM_UART, &avail) == ESP_OK && avail) {
    const int n = uart_read_bytes(FM_UART, tmp, avail < sizeof tmp ? avail : sizeof tmp, 0);
    if (n <= 0) break;
    for (int i = 0; i < n; i++)
      if (_rx.feed(tmp[i])) handle();
  }
}

void WifiFileMenu::update()
{
  _input->buttons_get();                  // tiene vivo il conteggio di START 3 s
  if (!_open) { delay(16); return; }

  const unsigned long t0 = millis();
  if (t0 - _hb_t >= 1000) {
    _hb_t = t0;
    send(FM_T_WIFI_ON, _seq++, nullptr, 0);
  }

  uint8_t tmp[1024];
  while (millis() - t0 < 30) {
    size_t avail = 0;
    uart_get_buffered_data_len(FM_UART, &avail);
    int n;
    if (avail == 0) n = uart_read_bytes(FM_UART, tmp, 1, pdMS_TO_TICKS(2));
    else n = uart_read_bytes(FM_UART, tmp, avail < sizeof tmp ? avail : sizeof tmp, 0);
    for (int i = 0; i < n; i++)
      if (_rx.feed(tmp[i])) handle();
  }

  // velocita': media sull'ultimo secondo
  const uint32_t fatti = _upload ? _wpos : _rpos;
  const unsigned long dt = millis() - _speed_t0;
  if (dt >= 1000) {
    _kbs = (fatti >= _speed_b0) ? (uint32_t)((uint64_t)(fatti - _speed_b0) * 1000 / dt / 1024) : 0;
    _speed_b0 = fatti;
    _speed_t0 = millis();
  }
}

void WifiFileMenu::draw_strip(int sy)
{
  memset(_out_strip, 0, TFT_FB_W * 8 * sizeof(unsigned short));
  const unsigned short BIANCO = 0xFFFF, AMBRA = 0x60FE, VERDE = 0xE007,
                       GRIGIO = 0x0842, CIANO = 0xFF07, ROSSO = 0x00F8;
  const unsigned long ora = millis();

  txt_c(_out_strip, sy, 6, "FILE MANAGER WIFI", AMBRA);

  txt_c(_out_strip, sy, 26, "COLLEGATI ALLA RETE", CIANO);
  txt_c(_out_strip, sy, 38, FM_AP_SSID, BIANCO, 2);
  txt_c(_out_strip, sy, 60, "PASSWORD", CIANO);
  txt_c(_out_strip, sy, 72, FM_AP_PASS, BIANCO, 2);
  txt_c(_out_strip, sy, 94, "E APRI NEL BROWSER", CIANO);
  txt_c(_out_strip, sy, 106, FM_AP_IP, BIANCO, 2);

  // ---- stato del C6
  const char *s1; unsigned short c1;
  char s2[32] = "";
  if (!_open) { s1 = _err; c1 = ROSSO; }
  else if (_st_t == 0 || ora - _st_t > 3500) {
    if (ora - _hb_t < 3000 && _st_t == 0 && _seq < 4) { s1 = "AVVIO RETE..."; c1 = AMBRA; }
    else {
      s1 = "C6 NON RISPONDE"; c1 = ROSSO;
      strcpy(s2, "AGGIORNA IL FIRMWARE C6");
    }
  } else if (_st.version != FM_PROTO_VERSION) {
    s1 = "FIRMWARE C6 DIVERSO"; c1 = ROSSO;
    strcpy(s2, "RIPROGRAMMA IL C6");
  } else if (!_st.ap_on) { s1 = "ACCENSIONE RETE..."; c1 = AMBRA; }
  else {
    s1 = "RETE ACCESA"; c1 = VERDE;
    if (_st.clients == 0) strcpy(s2, "NESSUN DISPOSITIVO");
    else snprintf(s2, sizeof s2, "DISPOSITIVI COLLEGATI: %d", _st.clients);
  }
  txt_c(_out_strip, sy, 134, s1, c1);
  if (s2[0]) txt_c(_out_strip, sy, 146, s2, c1 == ROSSO ? ROSSO : BIANCO);
  txt_c(_out_strip, sy, 160, sd_is_mounted() ? "SD PRONTA" : "SD NON PRESENTE",
        sd_is_mounted() ? VERDE : ROSSO);

  // ---- trasferimento in corso (o l'ultimo)
  const bool attivo = (_wfd >= 0) || (_rfd >= 0 && ora - _op_t < 2000);
  if (_path && _path[0]) {
    txt_c(_out_strip, sy, 182,
          attivo ? (_upload ? "RICEVO" : "INVIO") : (_upload ? "ULTIMO RICEVUTO" : "ULTIMO INVIATO"),
          attivo ? AMBRA : GRIGIO);
    // solo il nome, senza cartelle; se e' lungo se ne tiene la FINE
    const char *nome = strrchr(_path, '/');
    nome = nome ? nome + 1 : _path;
    const int maxc = XW / 8;
    const int l = strlen(nome);
    txt_c(_out_strip, sy, 194, l > maxc ? nome + (l - maxc) : nome, BIANCO);
    const uint32_t fatti = _upload ? _wpos : _rpos;
    const uint32_t tot   = _upload ? _wsize : _rsize;
    barra(_out_strip, sy, 208, fatti, tot, BIANCO, attivo ? VERDE : GRIGIO);
    char a[16], b[16], riga[40];
    fmt_size(a, sizeof a, fatti);
    fmt_size(b, sizeof b, tot);
    snprintf(riga, sizeof riga, "%s / %s", a, b);
    txt_c(_out_strip, sy, 224, riga, BIANCO);
    if (attivo) {
      snprintf(riga, sizeof riga, "%lu KB/S", (unsigned long)_kbs);
      txt_c(_out_strip, sy, 236, riga, CIANO);
    }
  } else {
    txt_c(_out_strip, sy, 194, "IN ATTESA DI FILE", GRIGIO);
  }

  char cnt[40];
  snprintf(cnt, sizeof cnt, "RICEVUTI %d  INVIATI %d", _n_in, _n_out);
  txt_c(_out_strip, sy, 256, cnt, GRIGIO);
  if (_err[0] && _open && ora - _err_t < 15000) txt_c(_out_strip, sy, 274, _err, ROSSO);

  txt_c(_out_strip, sy, 302, "START 3s ESCI", GRIGIO);
}

void WifiFileMenu::render()
{
  const unsigned long ora = millis();
  if (_render_t && ora - _render_t < 250) return;
  _render_t = ora ? ora : 1;
  const int n_strips = TFT_FB_H / 8;
  for (int s = 0; s < n_strips; s++) {
    draw_strip(s * 8);
    video.write(_out_strip, TFT_FB_W * 8);
    if (_open) pump();                   // un OK in attesa parte subito, non a fine schermo
  }
}
