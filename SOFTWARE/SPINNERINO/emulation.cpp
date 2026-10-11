#include "Arduino.h"
#include "emulation/emulation.h"
#include "emulation/input.h"
#include "machines/machineBase.h"
#include "esp_task_wdt.h"
#include "prof.h"        // strumento di misura (ARKANOID 2): vedi prof.h

// including of "../machines/machineBase.h" in "emulation.h" not possible
TaskHandle_t emulationTaskHandle;
extern machineBase *currentMachine;
extern Input input;

// ATTRACT FIX (bug "dopo qualche minuto non entra piu' nei giochi, resta sul
// logo"): lo stack da 16KB del task emulazione e' allocato STATICAMENTE (BSS,
// DRAM interna) una volta sola, NON con xTaskCreatePinnedToCore ad ogni avvio.
// Con l'alloc dinamica, ogni rotazione dell'attract faceva malloc+free di 16KB;
// dopo molte rotazioni la heap interna si FRAMMENTA e l'alloc non trova piu'
// 16KB CONTIGUI -> xTaskCreate fallisce -> emulationTaskHandle resta NULL (il
// ritorno non era controllato) -> l'emulazione non gira -> il gioco non parte
// (schermo fermo sul logo, l'anti-freeze a 20s ruota al successivo) e nemmeno
// FIRE lo avvia. Stack statico = nessuna dipendenza da heap/frammentazione: la
// creazione non puo' piu' fallire.
#define EMU_TASK_STACK_BYTES 16384
static StackType_t  emu_task_stack[EMU_TASK_STACK_BYTES];   // StackType_t = uint8_t su ESP-IDF (byte)
static StaticTask_t emu_task_tcb;

void emulation_start() {
  currentMachine->reset();
  // Priority 1 instead of 2 to give Bluetooth more CPU time.
  // Stack 16KB (era 4KB): Bubble Bobble genera l'audio FM REALE (ym2203_update_one
  // + ym3526_update_one + PSG) dentro run_frame, su QUESTO task. update_one vuole
  // ~8KB (il task audio era dimensionato cosi'); con 4KB rischio overflow.
  emulationTaskHandle = xTaskCreateStaticPinnedToCore(
      emulation_task, "emulation task", EMU_TASK_STACK_BYTES, NULL, 1,
      emu_task_stack, &emu_task_tcb, ARDUINO_RUNNING_CORE == 0 ? 1 : 0);
}

void emulation_stop() {
  if (emulationTaskHandle == NULL)
    return;

  input.disable(); // disable input read from nunchuck

  vTaskDelete(emulationTaskHandle);
  emulationTaskHandle = NULL;
  // Lascia un paio di tick: garantisce che lo scheduler dell'altro core abbia
  // tolto dalla CPU il task appena cancellato PRIMA che lo stack statico possa
  // essere riusato da un nuovo emulation_start (ri-creazione sugli stessi buffer).
  vTaskDelay(2);
  currentMachine->reset();  // clear sound output

  input.enable(); // enable input read from nunchuck
}

void emulation_notifyGive() {
  if (emulationTaskHandle == NULL)
    return;

  xTaskNotifyGive(emulationTaskHandle);
}

void emulation_task(void *p) {
  // Watchdog already disabled globally via esp_task_wdt_deinit() in setup()

  while(1) {
    emulation_frame();

    // Small yield to allow Bluetooth stack to process
    taskYIELD();
  }
}

void emulation_frame() {
  // It may happen that the emulation runs too slow. It will then miss the
  // vblank notification and in turn will miss a frame and significantly
  // slow down. This risk is only given with Galaga as the emulation of
  // all three CPUs takes nearly 13ms. The 60hz vblank rate is in turn 
  // 16.6 ms.  
#if 0
  static int counter;
  static unsigned long time = millis();
  
  if (counter % 10 == 0) {
    // good time: 160ms
    unsigned long now = millis();
    printf("%2d: %dms\n", (int)currentMachine->machineType(),  now - time);
    time = now;
  }
  counter++;
#endif
   
  // FREE-RUN: l'emulazione gira a cadenza reale FISSA (wall-clock), NON agganciata
  // al render. Necessario quando il render e' piu' lento del periodo (es. pbaction
  // 3 layer, PANG 384x240 2-passaggi < 60fps): se l'emulazione fosse gated dal
  // render (ulTaskNotifyTake sotto), gameplay E musica rallenterebbero in
  // proporzione. Il display aggiorna piu' lento (tearing minimo, VRAM live).
  //
  // Pacing a TEMPO ASSOLUTO (target += periodo, spin con vTaskDelay(1)):
  // auto-correttivo e INDIPENDENTE dal tick rate FreeRTOS. Il pacing RELATIVO
  // precedente (vTaskDelay((16667-dt)/1000)) faceva girare PANG lento (delay in
  // tick soggetto ad arrotondamento/tick-rate). Il vTaskDelay(1) alimenta IDLE0
  // (WDT); il do-while ne garantisce SEMPRE almeno uno anche se run_frame sfora.
  // Vedi feedback_multi_step_emulation. Solo i giochi con freeRunEmulation()=true.
  static uint32_t freeRunNextUs = 0;
  if (currentMachine->freeRunEmulation() && currentMachine->game_started) {
    // ⭐ Il tempo di run_frame e' l'UNICO numero che dice se a essere in ritardo
    // e' l'emulazione invece del video: gira sull'ALTRO core e non compare in
    // nessuna misura del ciclo video ([[feedback_ottimizzare_contando_accessi]]
    // caso 5). Spento (ARK2_PROF 0) queste due righe non esistono.
    prof_emu_inizio();
    currentMachine->run_frame();
    prof_emu_fine();
    if (freeRunNextUs == 0) freeRunNextUs = micros();
    freeRunNextUs += currentMachine->freeRunPeriodUs();
    if ((int32_t)(micros() - freeRunNextUs) > 100000) freeRunNextUs = micros(); // resync se molto indietro
    do {
      vTaskDelay(1);                          // >=1 tick: cede CPU + alimenta IDLE0 (WDT)
    } while ((int32_t)(micros() - freeRunNextUs) < 0);
    return;
  }

  prof_emu_inizio();
  currentMachine->run_frame();
  prof_emu_fine();

  // Wait for signal from video task to emulate a 60Hz frame rate. Don't do
  // this unless the game has actually started to speed up the boot process
  // a little bit.
  if(currentMachine->game_started) {
    ulTaskNotifyTake(1, portMAX_DELAY);
    // Give Bluetooth stack time to process after each frame
    vTaskDelay(1);
  }
  else {
    freeRunNextUs = 0;               // reset pacing free-run per il prossimo gioco
    vTaskDelay(1); // give a millisecond delay to make the watchdog happy
  }
}

unsigned char OpZ80_INL(unsigned short Addr) {
  return currentMachine->opZ80(Addr);
}

void OutZ80(unsigned short Port, unsigned char Value) {
  currentMachine->outZ80(Port, Value);
}
  
unsigned char InZ80(unsigned short Port) {
  return currentMachine->inZ80(Port);
}

void WrZ80(unsigned short Addr, unsigned char Value) {
  currentMachine->wrZ80(Addr, Value);
}

unsigned char RdZ80(unsigned short Addr) {
  return currentMachine->rdZ80(Addr);
}

void PatchZ80(Z80 *R) {
}

void i8048_port_write(i8048_state_S *state, unsigned char port, unsigned char pos) {
  currentMachine->wrI8048_port(state, port, pos);
}

unsigned char i8048_port_read(i8048_state_S *state, unsigned char port) {
  return currentMachine->rdI8048_port(state, port);
}

unsigned char i8048_rom_read(i8048_state_S *state, unsigned short addr) {
  return currentMachine->rdI8048_rom(state, addr);
}

unsigned char i8048_xdm_read(i8048_state_S *state, unsigned char addr) {
  return currentMachine->rdI8048_xdm(state, addr);
}

void i8048_xdm_write(i8048_state_S *state, unsigned char addr, unsigned char data) {
  currentMachine->wrI8048_xdm(state, addr, data);
}

#if defined(ENABLE_TEMPEST) || defined(ENABLE_SBRKOUT)
// ── M6502 (fake6502) dispatcher — quando Tempest o Sbrkout e' attivo ──
// fake6502.c dichiara extern senza extern "C" → linkage C++. Manteniamo C++.
IRAM_ATTR uint8_t read6502(uint16_t address) {
  return currentMachine->rd6502((unsigned short)address);
}
IRAM_ATTR void write6502(uint16_t address, uint8_t value) {
  currentMachine->wr6502((unsigned short)address, (unsigned char)value);
}
IRAM_ATTR uint8_t opread6502(uint16_t address) {
  return currentMachine->op6502((unsigned short)address);
}
#endif // ENABLE_TEMPEST || ENABLE_SBRKOUT