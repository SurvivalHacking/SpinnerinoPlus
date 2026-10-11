// ============================================================================
// SPINNERINO SH - Input driver
//   EC11 quadrature encoder -> paddle X (0..255) + FIRE (SW)
//   1 pulsante START + auto-coin firmware all'avvio
//   Hold FIRE + ruota EC11 -> regola volume (sx=down, dx=up); release = save
//   Hold COIN (gamepad BT) 3s + D-pad SU/GIU -> volume; release COIN = save
//     (per i giochi della griglia GIOCHI ARCADE, dove FIRE = sparo continuo)
// ============================================================================
#include "emulation/input.h"
#include "machines/machineBase.h"
#include <Preferences.h>

// Per leggere ec11PulseHoldMs() dalla machine attiva
extern machineBase *currentMachine;

#ifdef UART_BRIDGE_INPUT
  // --- Bridge BLE dal ESP32-C6 companion (vedi config.h + cartella
  // C6_BLE_Bridge/). Il C6 fa da BLE HID host e invia pacchetti gamepad
  // [0xAA][buttons][flags][xor] sulla UART. Stesso protocollo di GALAGONE. ---
  static HardwareSerial BridgeSerial(1);
  // volatile: scritte da updateBluetooth() (core 0, main loop) e lette da
  // buttons_get() che viene chiamata anche dal task di emulazione (core 1).
  static volatile unsigned char uart_bridge_buttons   = 0;
  static volatile bool          uart_bridge_connected = false;
  static volatile bool          uart_bridge_y_pressed = false;
  #define BRIDGE_SYNC_GAMEPAD 0xAA
  #define BRIDGE_SYNC_CTL     0xA5   // frame control C6->P4 (qui ignorati)
  #define BRIDGE_SYNC_RESP    0x5A   // response P4->C6 (qui ignorati)
  #define BRIDGE_PKT_LEN      4
  #define BRIDGE_FLAG_Y       0x01
  #define BRIDGE_FLAG_CONN    0x02
#endif

static Preferences prefs;

// --- Stato paddle (esposto a machines/arkanoid/arkanoid.cpp) ---
// IMPORTANTE: arkangc check boot a $042D vuole D018=0 al primo read,
// altrimenti stampa "BAD HARDWARE". MAME inizia IPT_DIAL a 0 e poi accumula.
volatile int16_t ec11_paddle_x = 0;
// Dial counter free-running (wraps mod 256) per giochi con dial relativo
// (es. Gigas). A differenza di ec11_paddle_x che e' clamped 0..255 per
// paddle assoluto stile Arkanoid, qui non clampiamo: il game legge il
// counter due volte e calcola il delta autonomamente.
volatile uint8_t ec11_dial_counter = 0;
static volatile uint8_t ec11_ab_prev = 0;

// Step encoder regolabili per-gioco dal menu IMPOSTAZIONI (sensibilita' EC11).
// Valori a livello 5 (default di fabbrica) = i vecchi costanti hardcoded:
//   paddle_x step = EC11_PADDLE_STEP (4), dial_counter step = 6.
// ec11_apply_sensitivity(level) li riscala quando si avvia un gioco.
volatile int g_ec11_paddle_step = EC11_PADDLE_STEP;   // base 4 (Arkanoid paddle)
volatile int g_ec11_dial_step   = 6;                  // base 6 (dial free-running)

void ec11_apply_sensitivity(int level) {
  if (level < 1)  level = 1;
  if (level > 10) level = 10;
  int ps = (EC11_PADDLE_STEP * level) / 5;  if (ps < 1) ps = 1;
  int ds = (6 * level) / 5;                 if (ds < 1) ds = 1;
  g_ec11_paddle_step = ps;
  g_ec11_dial_step   = ds;
}

// Quando true (durante regolazione volume), gli step encoder NON aggiornano
// il paddle: il pulse viene comunque generato per il consumo lato volume.
static volatile bool paddle_locked = false;

// Sequenza COIN -> gap -> START attivata alla pressione di START
//   0..80 ms   : BUTTON_COIN attivo
//   80..160 ms : gap
//   160..400 ms: BUTTON_START
static uint32_t start_seq_t0 = 0;        // millis() di inizio, 0 = idle

// Hold lungo START (>= 3s) -> torna al menu boot.
// Esposto via input_consume_back_to_menu() per essere consumato dal loop.
static uint32_t btn_start_hold_t0 = 0;
static volatile bool back_to_menu_request = false;
static const uint32_t BACK_TO_MENU_HOLD_MS = 3000;

bool input_consume_back_to_menu() {
  bool v = back_to_menu_request;
  back_to_menu_request = false;
  return v;
}

// Vol mode: si attiva quando FIRE viene tenuto premuto >= 3 secondi (per
// evitare conflitti con il fire normale in-game). In vol mode la rotazione
// EC11 cambia il volume e i bottoni LEFT/RIGHT/FIRE NON vengono inoltrati al
// gioco. Si disattiva al rilascio di FIRE.
//
// volatile OBBLIGATORIO: buttons_get() viene chiamata da PIU' CORE per i giochi
// che pollano l'input dal core di emulazione (Arkanoid 2 legge la porta MCU ad
// alta frequenza nel suo run_frame su core 1, oltre al main loop su core 0).
// Senza volatile il core di emulazione vede vol_mode_active in modo NON coerente
// → spesso false → consuma ec11_step_pulse nel ramo non-volume e lo SCARTA →
// il volume non si regola. Stessa convenzione di paddle_locked/ec11_* qui sopra.
static volatile bool vol_mode_active = false;
static volatile bool prev_fire_pressed = false;
static volatile uint32_t fire_press_t0 = 0;
// Debounce del RILASCIO di FIRE (solo per la vol mode). Il core di emulazione di
// alcuni giochi (Goindol) polla la porta FIRE in loop strettissimo -> digitalRead
// del pin EC11_SW campionato migliaia di volte/frame. Un singolo sample HIGH
// spurio (switch INPUT_PULLUP, filo lungo, niente debounce HW) azzererebbe
// fire_press_t0 -> il timer dei 3s non arriva MAI -> vol mode non si attiva.
static volatile uint32_t fire_low_since  = 0;    // millis del primo livello alto (0 = pressed)
static volatile bool     fire_pressed_db = false;// FIRE debounced (usato solo dalla vol mode)
static volatile int8_t vol_accum_shared = 0;   // accumulo step encoder per il volume
static const uint32_t VOL_HOLD_MS = 3000;

// --- Vol mode via COIN tenuto 3s (gamepad BT, griglia GIOCHI ARCADE) ---
// Nei giochi joystick FIRE viene tenuto premuto durante il gioco (autofire/
// sparo continuo), quindi non puo' fungere da trigger volume come per i giochi
// paddle. Per questi giochi il trigger e' COIN tenuto >= 3s; una volta in vol
// mode il D-pad SU/GIU regola il volume (SU = alza, GIU = abbassa). Si esce e
// si salva al rilascio del COIN. Tutte volatile per coerenza multi-core (vedi
// nota sopra su vol_mode_active).
static volatile bool     vol_mode_by_coin  = false;  // true = vol mode attivato da COIN
static volatile uint32_t coin_press_t0     = 0;      // millis() inizio press COIN
static volatile bool     prev_coin_pressed = false;
static volatile uint32_t vol_btn_last      = 0;      // ultimo step D-pad (repeat)
static const uint32_t COIN_VOL_HOLD_MS  = 3000;      // hold COIN per entrare in vol mode
static const uint32_t VOL_BTN_REPEAT_MS = 150;       // auto-ripetizione D-pad SU/GIU

bool input_is_vol_mode_active(void) { return vol_mode_active; }

// ⛔ 10/10: `currentMachine` NON torna a nullptr all'uscita da un gioco (la usa
// anche il task audio): resta sull'ULTIMA machine giocata. La guardia
// `disableVolMode()` quindi seguiva l'utente fuori dal gioco — referto: «se
// avvio prima TEMPEST non funziona piu' la regolazione del volume sui LASER
// DISC» (stesso difetto dopo MotoRace e Road Fighter, anche nei menu). La
// guardia vale solo mentre una machine GIRA: la alza start_machine(), la
// abbassa return_to_menu() (SPINNERINO.ino).
static volatile bool machine_in_corso = false;
void input_set_machine_in_corso(bool on) { machine_in_corso = on; }

void input_reset_button_state() {
  // Reset di tutti gli state interni dei bottoni quando si rientra nel menu.
  start_seq_t0 = 0;
  btn_start_hold_t0 = 0;
  back_to_menu_request = false;
  // ⚠️ ANCHE il vol mode (per questo la funzione sta QUI, dopo le variabili).
  // Si entra nei giochi PREMENDO FIRE, lo stesso tasto che tenuto 3s apre la
  // regolazione volume: se fire_press_t0 resta armato (o il vol mode e' rimasto
  // acceso), in vol mode buttons_get NON legge piu' joystick/FIRE -> il gioco
  // appena avviato non risponde a NULLA. Visto sul player laserdisc: schermata
  // difficolta' bloccata.
  vol_mode_active  = false;
  vol_mode_by_coin = false;
  fire_press_t0    = 0;
  fire_pressed_db  = false;
  fire_low_since   = 0;
  coin_press_t0    = 0;
}

// Per generare BUTTON_LEFT/RIGHT "virtuali" durante menu/attract (1 frame
// per ogni step encoder), oltre al paddle continuo
static volatile int8_t ec11_step_pulse = 0;   // -1/0/+1 consumato dal main

// Hold direzionale dell'EC11 (per-machine via machineBase::ec11PulseHoldMs).
// Dopo ogni click EC11, BUTTON_LEFT o BUTTON_RIGHT resta settato per N
// millisecondi (tipicamente 100 ms = ~6 frame) per giochi che leggono IN1
// molte volte per frame dal Z80 (Space Invaders, Galaga). Per Arkanoid e
// altri giochi paddle, ec11PulseHoldMs() ritorna 0 -> nessun hold attivo.
static uint32_t pulse_hold_until = 0;
static int8_t   pulse_held_sign  = 0;

// --- Quadrature decoder (table-driven, glitch-safe) ---
// new_state = (A<<1)|B, key = (prev<<2)|new
// table: +1 = CW, -1 = CCW, 0 = invalid/no-change
static const int8_t QDEC_TABLE[16] = {
   0, -1, +1,  0,
  +1,  0,  0, -1,
  -1,  0,  0, +1,
   0, +1, -1,  0
};

static IRAM_ATTR void ec11_isr() {
  uint8_t a = digitalRead(EC11_PIN_A) ? 1 : 0;
  uint8_t b = digitalRead(EC11_PIN_B) ? 1 : 0;
  uint8_t cur = (a << 1) | b;
  uint8_t key = (ec11_ab_prev << 2) | cur;
  int8_t d = QDEC_TABLE[key & 0x0F];
  ec11_ab_prev = cur;

  if (d != 0) {
    if (!paddle_locked) {
      int16_t v = ec11_paddle_x + (int16_t)d * g_ec11_paddle_step;
      if (v < 0)   v = 0;
      if (v > 255) v = 255;
      ec11_paddle_x = v;
      // Dial free-running: wrappa mod 256. Step regolabile per-gioco (default 6
      // vs 4 del paddle) per dare piu' "spinta" ai giochi con dial relativo.
      // Congelato (insieme al paddle) quando paddle_locked = true, cioe' in vol
      // mode: cosi' regolando il volume il paddle dei giochi a dial (Arkanoid 2,
      // Gigas, sbrkout) NON vola via.
      ec11_dial_counter = (uint8_t)(ec11_dial_counter + d * g_ec11_dial_step);
    }
    ec11_step_pulse = d;   // sempre: in vol mode viene consumato per il volume
  }
}

#ifdef USE_DIGITAL_JOYSTICK
// Joystick digitale 4 direzioni + 2 pulsanti fisici (header 40-pin), attivo-basso
// (premuto = LOW grazie a INPUT_PULLUP). Ritorna la maschera BUTTON_* da mettere
// in OR con gli altri input in buttons_get(). A = FIRE, B = EXTRA.
static inline unsigned char read_digital_joystick(void) {
  unsigned char b = 0;
  if (!digitalRead(JOY_PIN_UP))    b |= BUTTON_UP;
  if (!digitalRead(JOY_PIN_DOWN))  b |= BUTTON_DOWN;
  if (!digitalRead(JOY_PIN_LEFT))  b |= BUTTON_LEFT;
  if (!digitalRead(JOY_PIN_RIGHT)) b |= BUTTON_RIGHT;
  if (!digitalRead(BTN_A_PIN))     b |= BUTTON_FIRE;   // A = sparo principale
  if (!digitalRead(BTN_B_PIN))     b |= BUTTON_EXTRA;  // B = 2o tasto
  return b;
}
#endif

void Input::init() {
  virtual_coin_state = 0;
  virtual_coin_timer = 0;
  reset_timer = 0;
  input_states_last = 0;

  pinMode(EC11_PIN_A,  INPUT_PULLUP);
  pinMode(EC11_PIN_B,  INPUT_PULLUP);
  pinMode(EC11_PIN_SW, INPUT_PULLUP);
  pinMode(BTN_START,   INPUT_PULLUP);

#ifdef USE_DIGITAL_JOYSTICK
  // Joystick + A/B fisici (parallelo a EC11/START/BT). Attivo-basso.
  pinMode(JOY_PIN_UP,    INPUT_PULLUP);
  pinMode(JOY_PIN_DOWN,  INPUT_PULLUP);
  pinMode(JOY_PIN_LEFT,  INPUT_PULLUP);
  pinMode(JOY_PIN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_A_PIN,     INPUT_PULLUP);
  pinMode(BTN_B_PIN,     INPUT_PULLUP);
  Serial.println(F("[JOY] joystick digitale + A/B: GPIO U5 D49 L50 R28 A2 B4"));
#endif

  // stato iniziale quadrature
  uint8_t a = digitalRead(EC11_PIN_A) ? 1 : 0;
  uint8_t b = digitalRead(EC11_PIN_B) ? 1 : 0;
  ec11_ab_prev = (a << 1) | b;

  attachInterrupt(digitalPinToInterrupt(EC11_PIN_A), ec11_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(EC11_PIN_B), ec11_isr, CHANGE);

#ifdef UART_BRIDGE_INPUT
  // Pull-up sul pin RX PRIMA di begin: se il filo verso il C6 fa contatto
  // incerto la linea fluttua e la UART scarta tutto (framing error). begin()
  // aggancia la matrice GPIO ma lascia il pull-up attivo.
  pinMode(UART_BRIDGE_RX_PIN, INPUT_PULLUP);
  // UART verso il C6: buffer RX generoso per non perdere byte sotto carico.
  BridgeSerial.setRxBufferSize(2048);
  BridgeSerial.begin(UART_BRIDGE_BAUD, SERIAL_8N1, UART_BRIDGE_RX_PIN, UART_BRIDGE_TX_PIN);
  Serial.printf("[BT] UART bridge C6: RX=%d TX=%d @%d baud\n",
                UART_BRIDGE_RX_PIN, UART_BRIDGE_TX_PIN, UART_BRIDGE_BAUD);
#endif
}

void Input::enable()  {}
void Input::disable() {}

void Input::updateBluetooth(void) {
#ifdef UART_BRIDGE_INPUT
  // --- Demux dello stream UART dal C6 ---
  // 0xAA = pacchetto gamepad (4 byte fissi). 0xA5/0x5A = frame control/response
  // del protocollo esteso (web admin/OTA di GALAGONE): qui non li gestiamo, ma
  // li saltiamo in modo pulito leggendo il campo lunghezza cosi' non sfasano
  // lo stream gamepad. Il firmware C6 di SPINNERINO emette SOLO 0xAA: lo skip
  // e' solo una rete di sicurezza se si usasse il .bin completo di GALAGONE.
  while (BridgeSerial.available() > 0) {
    int peeked = BridgeSerial.peek();
    if (peeked < 0) break;
    uint8_t sync = (uint8_t)peeked;

    if (sync == BRIDGE_SYNC_GAMEPAD) {
      if (BridgeSerial.available() < BRIDGE_PKT_LEN) break;  // attendi il resto
      BridgeSerial.read();                       // consuma 0xAA
      uint8_t buttons = BridgeSerial.read();
      uint8_t flags   = BridgeSerial.read();
      uint8_t crc     = BridgeSerial.read();
      if (crc != (uint8_t)(BRIDGE_SYNC_GAMEPAD ^ buttons ^ flags)) continue;
      uart_bridge_buttons   = buttons;
      uart_bridge_y_pressed = (flags & BRIDGE_FLAG_Y) != 0;
      uart_bridge_connected = (flags & BRIDGE_FLAG_CONN) != 0;
      continue;
    }

    if (sync == BRIDGE_SYNC_CTL || sync == BRIDGE_SYNC_RESP) {
      // Frame: [sync][len][cmd][payload(len)][crc16 lo][crc16 hi].
      if (BridgeSerial.available() < 2) break;   // serve almeno sync+len
      BridgeSerial.read();                        // consuma sync
      int len = BridgeSerial.read();              // lunghezza payload
      if (len < 0 || len > 64) continue;          // garbage: riallinea
      int need = len + 3;                         // cmd + payload + crc16(2)
      while (need-- > 0 && BridgeSerial.available() > 0) BridgeSerial.read();
      continue;
    }

    // Byte fuori sync: scarta e riallinea.
    BridgeSerial.read();
  }

  // --- D-pad BT -> spinner (paddle/dial) ---
  // Tenendo SX/DX sul gamepad, il paddle si muove in continuo come ruotando
  // l'encoder fisico. Frame-rate independent: 1 passo (g_ec11_paddle_step)
  // ogni ~16 ms di hold. paddle_locked (vol mode) congela il movimento, come
  // per l'EC11. La sensibilita' del menu IMPOSTAZIONI vale anche per il BT
  // perche' riusa g_ec11_paddle_step / g_ec11_dial_step.
  static uint32_t bt_move_last = 0;
  uint32_t now = millis();
  int dir = 0;
  if      (uart_bridge_buttons & BUTTON_RIGHT) dir = +1;
  else if (uart_bridge_buttons & BUTTON_LEFT)  dir = -1;
  if (dir != 0 && !paddle_locked) {
    if (bt_move_last == 0 || (now - bt_move_last) >= 16) {
      bt_move_last = now;
      int16_t v = ec11_paddle_x + (int16_t)dir * g_ec11_paddle_step;
      if (v < 0)   v = 0;
      if (v > 255) v = 255;
      ec11_paddle_x = v;
      ec11_dial_counter = (uint8_t)(ec11_dial_counter + dir * g_ec11_dial_step);
    }
  } else {
    bt_move_last = 0;   // reset: al primo hold il movimento parte subito
  }
#endif

#ifdef USE_DIGITAL_JOYSTICK
  // Joystick fisico LEFT/RIGHT muove il paddle/dial come il D-pad BT, cosi' i
  // giochi paddle del carosello sono giocabili anche col joystick (non solo con
  // l'EC11). paddle_locked (vol mode) congela il movimento, come per EC11/BT.
  {
    static uint32_t joy_move_last = 0;
    uint32_t jnow = millis();
    int jdir = 0;
    if      (!digitalRead(JOY_PIN_RIGHT)) jdir = +1;
    else if (!digitalRead(JOY_PIN_LEFT))  jdir = -1;
    if (jdir != 0 && !paddle_locked) {
      if (joy_move_last == 0 || (jnow - joy_move_last) >= 16) {
        joy_move_last = jnow;
        int16_t v = ec11_paddle_x + (int16_t)jdir * g_ec11_paddle_step;
        if (v < 0)   v = 0;
        if (v > 255) v = 255;
        ec11_paddle_x = v;
        ec11_dial_counter = (uint8_t)(ec11_dial_counter + jdir * g_ec11_dial_step);
      }
    } else {
      joy_move_last = 0;
    }
  }
#endif
}

bool Input::button_y_pressed(void) {
#ifdef UART_BRIDGE_INPUT
  return uart_bridge_y_pressed;
#else
  return false;
#endif
}

// Solo i tasti del GAMEPAD BT, senza i pulsanti del mobile: vedi input.h.
// ⚠️ Aggiunta per Metal Slug, che ha bisogno di TRE azioni DISTINTE (spara,
// salta, granata) mentre buttons_get() fonde il tasto A (GPIO2) e il pulsante
// dell'encoder (GPIO48) nello stesso bit BUTTON_FIRE. Non cambia niente per gli
// altri giochi: e' una lettura in piu', nessun comportamento modificato.
unsigned char Input::bridge_buttons(void) {
#ifdef UART_BRIDGE_INPUT
  return uart_bridge_buttons;
#else
  return 0;
#endif
}

bool Input::isBluetoothEnabled() {
#ifdef UART_BRIDGE_INPUT
  return uart_bridge_connected;
#else
  return false;
#endif
}

unsigned char Input::buttons_get(void) {
  unsigned char states = 0;
  uint32_t now = millis();

  // ----- Stato pulsante FIRE (EC11 SW) -----
  bool fire_pressed = !digitalRead(EC11_PIN_SW);

  // Debounce del RILASCIO per la vol mode: crediamo al "rilasciato" solo dopo
  // FIRE_DB_MS continui di livello alto, cosi' un sample HIGH spurio (campionato
  // ad altissima frequenza dal core di emulazione su Goindol) NON azzera il timer
  // di hold. Il bottone FIRE in-game usa fire_pressed GREZZO (lo sparo resta reattivo).
  {
    static const uint32_t FIRE_DB_MS = 50;
    if (fire_pressed) {
      fire_low_since  = 0;
      fire_pressed_db = true;
    } else if (fire_pressed_db) {
      if (fire_low_since == 0) fire_low_since = now ? now : 1;
      if ((now - fire_low_since) >= FIRE_DB_MS) fire_pressed_db = false;
    }
  }

  // ----- Stato COIN dal gamepad BT (trigger vol mode hold 3s) -----
  // Usato solo per il vol mode dei giochi gamepad (griglia GIOCHI ARCADE). Non
  // leggiamo il pin fisico BTN_COIN: non e' cablato ne' configurato INPUT_PULLUP.
  bool coin_pressed = false;
#ifdef UART_BRIDGE_INPUT
  if (uart_bridge_buttons & BUTTON_COIN) coin_pressed = true;
#endif

  // Sensibilita' regolazione volume: 1 unita' di volume ogni N click encoder.
  // Aumenta per regolazione piu' lenta, diminuisci per piu' veloce.
  // vol_accum_shared e' volatile a file-scope (vedi nota in alto): condiviso tra
  // le chiamate da core diversi, non un local static per-core.
  static const int8_t VOL_STEP_THRESH = 4;

  // Tracking durata press FIRE per attivazione vol mode con hold >= 3 s
  // (usa il FIRE DEBOUNCED: vedi nota sopra, evita reset spurio del timer).
  if (fire_pressed_db) {
    if (fire_press_t0 == 0) fire_press_t0 = now ? now : 1;
  } else {
    fire_press_t0 = 0;
  }

  // Edge release: esci da vol mode e persisti il volume sul rilascio del
  // pulsante che l'ha attivata (FIRE per i giochi paddle, COIN per il gamepad).
  bool fire_released = (!fire_pressed_db && prev_fire_pressed);
  bool coin_released = (!coin_pressed && prev_coin_pressed);
  if (vol_mode_active &&
      (( vol_mode_by_coin && coin_released) ||
       (!vol_mode_by_coin && fire_released))) {
    vol_mode_active  = false;
    vol_mode_by_coin = false;
    paddle_locked    = false;
    vol_accum_shared = 0;             // reset accumulatore
    vol_btn_last     = 0;
    if (_volume_commit_callback) _volume_commit_callback();
  }
  prev_fire_pressed = fire_pressed_db;
  prev_coin_pressed = coin_pressed;

  // Attiva vol mode se FIRE tenuto premuto >= VOL_HOLD_MS (3 s),
  // ma solo se la machine corrente non lo disabilita esplicitamente
  // (es. MotoRace dove FIRE = acceleratore va tenuto a lungo).
  bool vol_mode_allowed = (machine_in_corso && currentMachine)
                          ? !currentMachine->disableVolMode() : true;
  if (vol_mode_allowed && fire_pressed_db && fire_press_t0 != 0 &&
      (now - fire_press_t0) >= VOL_HOLD_MS && !vol_mode_active) {
    vol_mode_active = true;
    paddle_locked   = true;
  }

  // Trigger COIN: tenuto premuto >= COIN_VOL_HOLD_MS -> vol mode "da gamepad"
  // (giochi della griglia GIOCHI ARCADE). Stessa guardia disableVolMode().
  if (coin_pressed) {
    if (coin_press_t0 == 0) coin_press_t0 = now ? now : 1;
  } else {
    coin_press_t0 = 0;
  }
  if (vol_mode_allowed && coin_pressed && coin_press_t0 != 0 &&
      (now - coin_press_t0) >= COIN_VOL_HOLD_MS && !vol_mode_active) {
    vol_mode_active  = true;
    vol_mode_by_coin = true;
    paddle_locked    = true;
  }

  // ----- Pulse encoder -----
  int8_t pulse = ec11_step_pulse;
  ec11_step_pulse = 0;

  // Aggiorna hold direzionale solo se la machine corrente lo richiede
  int hold_ms = currentMachine ? currentMachine->ec11HoldMsEff() : 0;
  if (pulse != 0 && hold_ms > 0) {
    pulse_hold_until = now + (uint32_t)hold_ms;
    pulse_held_sign  = (pulse > 0) ? 1 : -1;
  }

  if (vol_mode_active) {
    // Accumula gli step encoder; emette 1 cambio volume solo ogni
    // VOL_STEP_THRESH click (sensibilita' ridotta per regolazione fine).
    // dx (CW, pulse>0) = alza volume, sx (CCW, pulse<0) = abbassa.
    int8_t acc = vol_accum_shared + pulse;
    while (acc >= VOL_STEP_THRESH) {
      if (_volume_callback) _volume_callback(true, false);
      acc -= VOL_STEP_THRESH;
    }
    while (acc <= -VOL_STEP_THRESH) {
      if (_volume_callback) _volume_callback(false, true);
      acc += VOL_STEP_THRESH;
    }
    vol_accum_shared = acc;

#ifdef UART_BRIDGE_INPUT
    // D-pad SU/GIU del gamepad BT (griglia GIOCHI ARCADE): 1 passo di volume per
    // pressione, con auto-ripetizione ogni VOL_BTN_REPEAT_MS mentre si tiene
    // premuto. SU = alza, GIU = abbassa. UP/DOWN non raggiungono il gioco perche'
    // in vol mode il merge dei pulsanti BT (sotto) e' disabilitato.
    bool vup   = (uart_bridge_buttons & BUTTON_UP)   != 0;
    bool vdown = (uart_bridge_buttons & BUTTON_DOWN) != 0;
    if (vup || vdown) {
      if (vol_btn_last == 0 || (now - vol_btn_last) >= VOL_BTN_REPEAT_MS) {
        vol_btn_last = now;
        if (_volume_callback) _volume_callback(vup, vdown);
      }
    } else {
      vol_btn_last = 0;
    }
#endif
    // In vol mode NON inviare BUTTON_FIRE (evita lancio palla accidentale)
    // e NON usare il pulse come BUTTON_LEFT/RIGHT.
  } else {
    if (fire_pressed) states |= BUTTON_FIRE;
    // Pulse fresco: sempre inviato (giochi senza hold lo usano direttamente)
    if (pulse < 0)    states |= BUTTON_LEFT;
    if (pulse > 0)    states |= BUTTON_RIGHT;
    // Hold: attivo solo per machine con ec11PulseHoldMs() > 0 (SI, Galaga)
    if (hold_ms > 0 && (int32_t)(pulse_hold_until - now) > 0) {
      if (pulse_held_sign < 0) states |= BUTTON_LEFT;
      if (pulse_held_sign > 0) states |= BUTTON_RIGHT;
    }
  }

  // ----- Stato pulsante START + detection hold lungo (-> back to menu) -----
  // START del gamepad BT trattato come lo START fisico: guida la sequenza
  // coin->start e l'hold 3s per tornare al menu.
  bool start_pressed = !digitalRead(BTN_START);
#ifdef UART_BRIDGE_INPUT
  if (uart_bridge_buttons & BUTTON_START) start_pressed = true;
#endif

  // Hold tracker: parte al press, scatta a >= 4s
  if (start_pressed) {
    if (btn_start_hold_t0 == 0) btn_start_hold_t0 = now ? now : 1;
    if (now - btn_start_hold_t0 >= BACK_TO_MENU_HOLD_MS) {
      back_to_menu_request = true;
      btn_start_hold_t0 = 0;             // armato una volta sola per pressione
    }
  } else {
    btn_start_hold_t0 = 0;
  }

  // ----- Sequenza COIN -> gap -> START (alla pressione di START) -----
  if (start_pressed && start_seq_t0 == 0) {
    start_seq_t0 = now ? now : 1;
  }
  if (start_seq_t0 != 0) {
    uint32_t dt = now - start_seq_t0;
    if      (dt <  80)  states |= BUTTON_COIN;
    else if (dt < 160)  /* gap */;
    else if (dt < 400) {
      states |= BUTTON_START | BUTTON_EXTRA;
      // Centra il paddle alla prima entrata START (racchetta al centro)
      static bool paddle_centered = false;
      if (!paddle_centered) { ec11_paddle_x = 128; paddle_centered = true; }
    }
    else if (dt < 2000 && !start_pressed) start_seq_t0 = 0;
    else if (dt > 5000) start_seq_t0 = 0;
  }

#ifdef UART_BRIDGE_INPUT
  // Merge pulsanti BT (eccetto START, gia' gestito dalla sequenza sopra).
  // In vol mode NON inoltrare FIRE/LEFT/RIGHT, come per l'input fisico.
  if (!vol_mode_active) {
    states |= (uart_bridge_buttons &
               (BUTTON_FIRE | BUTTON_LEFT | BUTTON_RIGHT |
                BUTTON_UP | BUTTON_DOWN | BUTTON_COIN | BUTTON_EXTRA));
  }
#endif

#ifdef USE_DIGITAL_JOYSTICK
  // Joystick + A/B fisici in parallelo (come il merge BT sopra): in vol mode
  // NON inoltrare direzioni/FIRE/EXTRA, coerente con EC11 e gamepad BT.
  //
  // THROTTLE (fix "MotoRace rallentato" 2026-07-10): buttons_get() e' chiamata
  // MIGLIAIA di volte per frame dal core di emulazione (la Z80 di MotoRace polla
  // le porte input 0xD000/0xD001 in loop stretto). read_digital_joystick() fa 6
  // digitalRead: eseguirli ad OGNI chiamata quadruplicava il costo di buttons_get
  // (era 2 read) e faceva sforare run_frame() oltre i 16.6 ms -> gioco lento.
  // Campioniamo i GPIO al massimo 1 volta/ms (granularita' impercettibile) e
  // riusiamo la maschera in cache per le chiamate successive dello stesso ms.
  // volatile OBBLIGATORIO: cache condivisa tra i 2 core (vedi nota in alto).
  if (!vol_mode_active) {
    static volatile uint32_t      joy_last_ms = 0;
    static volatile unsigned char joy_cached  = 0;
    if (now != joy_last_ms) { joy_last_ms = now; joy_cached = read_digital_joystick(); }
    states |= joy_cached;
  }
#endif

  input_states_last = states;
  return states;
}

Input& Input::onVolumeUpDown(THandlerVolume fn)         { _volume_callback     = fn; return *this; }
Input& Input::onBrightnessUpDown(THandlerBrightness fn) { _brightness_callback = fn; return *this; }
Input& Input::onDoReset(THandlerDoReset fn)             { _doReset_callback    = fn; return *this; }
Input& Input::onDoAttractReset(THandlerDoAttractReset fn){_doAttractReset_callback = fn; return *this;}
Input& Input::onVolumeCommit(THandlerVolumeCommit fn)   { _volume_commit_callback = fn; return *this; }
