#ifndef INPUT_H
#define INPUT_H

#include <Arduino.h>
#include <functional>
#include "../config.h"
#ifdef NUNCHUCK_INPUT
  #include "nunchuck.h"
#endif

// a total of 7 button is needed for most games
#define BUTTON_LEFT  0x01
#define BUTTON_RIGHT 0x02
#define BUTTON_UP    0x04
#define BUTTON_DOWN  0x08
#define BUTTON_FIRE  0x10
#define BUTTON_START 0x20
#define BUTTON_COIN  0x40
#define BUTTON_EXTRA 0x80

class Input {
public:
  void init();
  void enable();
  void disable();
  void updateBluetooth(void);  // Call from main loop to update Bluetooth controller
  unsigned char buttons_get(void);
  bool button_y_pressed(void);  // Y/Triangle on BT gamepad (for Tutankham flash bomb)
  // Solo i tasti che arrivano dal GAMEPAD BT (ponte C6), SENZA i pulsanti del
  // mobile. ⚠️ Serve a chi deve tenere DISTINTI i tasti fisici: buttons_get()
  // mette in OR il tasto A (GPIO2) e il pulsante dell'encoder (GPIO48) nello
  // stesso bit BUTTON_FIRE, quindi da li' non si puo' sapere quale dei due e'
  // premuto. Chi vuole tre azioni separate legge i due GPIO da se' e ci somma
  // QUESTO per il pad. Ritorna 0 se il ponte non c'e'.
  unsigned char bridge_buttons(void);
  char demoSoundsOff() { return 0; }
  bool isBluetoothEnabled();   // Check if Bluetooth is currently enabled
 
  typedef std::function<void(bool up, bool down)> THandlerVolume;
  Input& onVolumeUpDown(THandlerVolume fn);

  typedef std::function<void(bool up, bool down)> THandlerBrightness;
  Input& onBrightnessUpDown(THandlerBrightness fn);

  typedef std::function<void(void)> THandlerDoReset;
  Input& onDoReset(THandlerDoReset fn);

  typedef std::function<void(void)> THandlerDoAttractReset;
  Input& onDoAttractReset(THandlerDoAttractReset fn);

  // Chiamato all'uscita dalla modalita' regolazione volume (release START)
  // per persistere il nuovo valore in NVS.
  typedef std::function<void(void)> THandlerVolumeCommit;
  Input& onVolumeCommit(THandlerVolumeCommit fn);

private:
  THandlerVolume _volume_callback;
  THandlerBrightness _brightness_callback;
  THandlerDoReset _doReset_callback;
  THandlerDoAttractReset _doAttractReset_callback;
  THandlerVolumeCommit _volume_commit_callback;
  unsigned char input_states_last;
  int virtual_coin_state;
  unsigned long virtual_coin_timer;
  unsigned long reset_timer;
  unsigned long _vol_repeat_time = 0;
  unsigned long _vol_repeat_delay = 0;
  unsigned long _brt_repeat_time = 0;
  unsigned long _brt_repeat_delay = 0;
};

#endif
