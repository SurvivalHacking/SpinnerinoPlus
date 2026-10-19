#include "emulation/audio.h"
#include "prof.h"        // strumento di misura (ARKANOID 2): vedi prof.h
#ifdef ENABLE_SPACE
  #include "machines/spaceinvaders/spaceinvaders_samples.h"
#endif
#ifdef ENABLE_MOONPATROL
  #include "machines/moonpatrol/moonpatrol.h"
  extern moonpatrol *g_moonpatrol_instance;
#endif
#ifdef ENABLE_ARKANOID2
  #include "machines/arkanoid2/arkanoid2.h"
  #include "machines/arkanoid2/ym2203/fm.h"
#endif
#ifdef ENABLE_GYRUSS
  #include "machines/gyruss/gyruss.h"   // per i8039_sample() (drums)
#endif
#ifdef ENABLE_COMMANDO
  #include "machines/commando/commando.h"  // per ym_regs/fm_keyed (FM approx)
#endif
#ifdef ENABLE_GNG
  #include "machines/gng/gng.h"            // per ym_regs/fm_keyed (FM approx 2x YM2203)
#endif
#ifdef ENABLE_BOBLBOBL
  #include "machines/boblbobl/boblbobl.h"  // audio FEDELE: ring buffer (audioPop)
#endif
#ifdef ENABLE_GOINDOL
  #include "machines/goindol/goindol.h"    // audio FEDELE YM2203: ring buffer (audioPop)
#endif
#ifdef ENABLE_DKONG3
  #include "machines/dkong3/dkong3.h"      // audio NES (2x RP2A03): ring buffer (audioPop)
#endif
#ifdef ENABLE_SUPXEVS
  #include "machines/supxevs/supxevs.h"    // audio NES (RP2A03): ring buffer (audioPop)
#endif
#ifdef ENABLE_TEMPEST
  #include "machines/tempest/tempest.h"    // 2 POKEY: ring buffer (audioPop)
#endif
#ifdef ENABLE_XEVIOUS
  #include "machines/xevious/xevious.h"    // esplosioni: campione armato dal 54XX
#endif
#ifdef ENABLE_FANTASY
  #include "machines/fantasy/fantasy.h"          // voci snk6502
  #include "machines/fantasy/fantasy_snd_rom.h"  // waveform ROM
  #include "machines/fantasy/fantasy_voce.h"     // VOCE: 12 campioni 32 kHz (flash)
#endif
#ifdef ENABLE_NIBBLER
  #include "machines/nibbler/nibbler.h"           // voci snk6502 + crunch
  #include "machines/nibbler/nibbler_snd_rom.h"   // waveform ROM
  #include "machines/nibbler/nibbler_crunch.h"    // sample crunch
#endif
#ifdef ENABLE_ROADFIGHTER
  #include "machines/roadfighter/roadfighter.h"   // per roadf_dac_sample() (DAC engine/crash)
#endif

// SPINNERINO P4: ES8311 codec + NS4150B amp via I2C + I2S nuova API IDF v5
#include "driver/i2c_master.h"
#include "driver/i2s_std.h"

// Jingle splash: PCM 16-bit mono @ 32 kHz (generato da convert_splash_audio.py)
#include "splash_audio.h"

// ES8311 register set (porting da driver GALAGONE 4.3 P4)
#define ES8311_REG_RESET        0x00
#define ES8311_REG_CLK_MANAGER1 0x01
#define ES8311_REG_CLK_MANAGER6 0x06
#define ES8311_REG_CLK_MANAGER8 0x08
#define ES8311_REG_SDP_IN       0x09
#define ES8311_REG_SDP_OUT      0x0A
#define ES8311_REG_SYSTEM_0B    0x0B
#define ES8311_REG_SYSTEM_0C    0x0C
#define ES8311_REG_SYSTEM       0x0D
#define ES8311_REG_SYSTEM_0E    0x0E
#define ES8311_REG_CHIP_PWR     0x10
#define ES8311_REG_ANALOG_PWR   0x11
#define ES8311_REG_SYSTEM_12    0x12
#define ES8311_REG_SYSTEM_13    0x13
#define ES8311_REG_ADC_CTRL     0x14
#define ES8311_REG_DAC_VOL      0x32

static i2c_master_dev_handle_t es8311_dev = NULL;
static i2s_chan_handle_t       i2s_tx_handle = NULL;

static esp_err_t es8311_write_reg(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_master_transmit(es8311_dev, buf, 2, 100);
}

static void es8311_codec_init() {
  // 1. Reset
  es8311_write_reg(ES8311_REG_RESET, 0x1F); delay(20);
  es8311_write_reg(ES8311_REG_RESET, 0x00); delay(20);
  // 2. Clock manager (slave, MCLK auto-detect 256xfs)
  es8311_write_reg(ES8311_REG_CLK_MANAGER1, 0x30);
  es8311_write_reg(0x02, 0x00); es8311_write_reg(0x03, 0x10);
  es8311_write_reg(0x04, 0x10); es8311_write_reg(0x05, 0x00);
  es8311_write_reg(ES8311_REG_CLK_MANAGER6, 0x03);
  es8311_write_reg(0x07, 0x00); es8311_write_reg(ES8311_REG_CLK_MANAGER8, 0xFF);
  // 3. SDP I2S 16-bit
  es8311_write_reg(ES8311_REG_SDP_IN,  0x00);
  es8311_write_reg(ES8311_REG_SDP_OUT, 0x00);
  // 4. Power management reset
  es8311_write_reg(ES8311_REG_SYSTEM_0B, 0x00);
  es8311_write_reg(ES8311_REG_SYSTEM_0C, 0x00);
  // 5. Power up chip + analog
  es8311_write_reg(ES8311_REG_CHIP_PWR,   0x1F);
  es8311_write_reg(ES8311_REG_ANALOG_PWR, 0x7F); delay(10);
  // 6. Start CSM
  es8311_write_reg(ES8311_REG_RESET, 0x80); delay(10);
  // 7. Enable DAC output
  es8311_write_reg(ES8311_REG_SYSTEM,    0x01);
  es8311_write_reg(ES8311_REG_SYSTEM_0E, 0x02);
  es8311_write_reg(ES8311_REG_SYSTEM_12, 0x00);
  es8311_write_reg(ES8311_REG_SYSTEM_13, 0x10);
  es8311_write_reg(ES8311_REG_ADC_CTRL,  0x1A);
  // 8. Start clocking
  es8311_write_reg(ES8311_REG_CLK_MANAGER1, 0x3F);
  // 9. DAC volume 0dB
  es8311_write_reg(ES8311_REG_DAC_VOL, 0xBF);
  Serial.println("[AUDIO] ES8311 codec initialized");
}

void Audio::init() {
  // SPINNERINO P4: ES8311 + NS4150B (I2S nuova API obbligatoria su P4)
  // 32 kHz 16-bit stereo come MAX98357A del sorgente S3 originale (compatibile rendering AY)

  // PA amplifier ON
  pinMode(ES8311_PA_PIN, OUTPUT);
  digitalWrite(ES8311_PA_PIN, HIGH);
  Serial.printf("[AUDIO] PA amp ON (GPIO %d)\n", ES8311_PA_PIN);

  // I2C master bus
  i2c_master_bus_config_t bus_cfg = {};
  bus_cfg.i2c_port = I2C_NUM_0;
  bus_cfg.sda_io_num = (gpio_num_t)I2C_SDA;
  bus_cfg.scl_io_num = (gpio_num_t)I2C_SCL;
  bus_cfg.clk_source = I2C_CLK_SRC_DEFAULT;
  bus_cfg.glitch_ignore_cnt = 7;
  i2c_master_bus_handle_t i2c_bus;
  i2c_new_master_bus(&bus_cfg, &i2c_bus);

  i2c_device_config_t dev_cfg = {};
  dev_cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  dev_cfg.device_address = ES8311_I2C_ADDR;
  dev_cfg.scl_speed_hz = 100000;
  i2c_master_bus_add_device(i2c_bus, &dev_cfg, &es8311_dev);

  // I2S TX nuova API (legacy non funziona su P4)
  i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan_cfg.dma_desc_num = 6;
  chan_cfg.dma_frame_num = 256;
  i2s_new_channel(&chan_cfg, &i2s_tx_handle, NULL);

  i2s_std_config_t std_cfg = {
    .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(32000),
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
      .mclk = (gpio_num_t)I2S_MCLK,
      .bclk = (gpio_num_t)I2S_BCLK,
      .ws   = (gpio_num_t)I2S_LRC,
      .dout = (gpio_num_t)I2S_DOUT,
      .din  = I2S_GPIO_UNUSED,
      .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
    },
  };
  std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
  i2s_channel_init_std_mode(i2s_tx_handle, &std_cfg);
  i2s_channel_enable(i2s_tx_handle);
  delay(10);

  // Init codec DOPO che I2S sta generando MCLK
  es8311_codec_init();
  Serial.println("[AUDIO] init complete (32kHz 16bit stereo)");

  generateSinusWave(256, sinusWaveBuffer, sizeof(sinusWaveBuffer)  / 2 );
}

void Audio::start(machineBase *machineBase) {
  currentMachine = machineBase;
  machineType = currentMachine->machineType();
  mario_lpf = 0;

  // Reset stato audio per-machine residuale (Galaxian VCO sweep, ecc.)
  // cosi' quando si torna al menu boot non resta hum/click.
  gal_last_pitch    = 0xFF;
  gal_pitch_active  = 0;
  gal_tone_cnt      = 0;
  gal_tone_toggle   = 1;
  gal_lfo_acc       = 0;
  for (int i = 0; i < 3; i++) {
    gal_fs_cnt[i]    = 0;
    gal_fs_toggle[i] = 1;
  }
    
#if defined(SND_I2S_DIGITAL)
  // MAX98357A: sample rate changes not needed (always 24kHz)
#elif !defined(WORKAROUND_I2S_APLL_PROBLEM)
  signed char machineType = currentMachine->machineType();
  i2s_set_sample_rates(I2S_NUM_0, (machineType == MCH_DKONG || machineType == MCH_DKONGJR || machineType == MCH_MARIOBROS) ? 11765 : 24000);
#endif
}

void Audio::volumeUpDown(bool up, bool down) {
  // Edge detection è già gestita in input.cpp, qui basta applicare
  if (up && volumeSetting > 1)
    volumeSetting--;
  if (down && volumeSetting < 30)
    volumeSetting++;
}

// ============================================================================
// PONTE AUDIO PER IL PLAYER LASERDISC (laserdisc.cpp)
// Sta QUI perche' i2s_tx_handle e' static in questo file, e perche'
// laserdisc.cpp NON deve includere emulation/audio.h (tira dentro le machine
// con le ROM -> la scheda non boota). Il player legge i .pcm dalla SD e li
// scrive lui sull'I2S con write BLOCCANTE: e' il CLOCK di riproduzione.
// Due writer sullo stesso canale = audio corrotto -> mentre il player gira,
// Audio::transmit() si sfila (bypass).
//
// ⛔ NIENTE i2s_set_sample_rates() QUI. E' l'API del driver LEGACY: sul canale
// nuovo (i2s_channel_*) e' un NO-OP (vedi il commento a ~riga 866) e al LINK
// trascina dentro il driver vecchio -> la scheda NON BOOTA (schermo bianco,
// nemmeno i checkpoint di setup). E non serve: l'I2S e' gia' inizializzato a
// I2S_STD_CLK_DEFAULT_CONFIG(32000) = 32 kHz nativi, cioe' esattamente il rate
// dei .pcm. Costato una serata.
// ============================================================================
static volatile bool s_ld_bypass = false;

void laserdisc_audio_begin(void) {
  s_ld_bypass = true;
  vTaskDelay(pdMS_TO_TICKS(20));      // lascia finire un transmit() in corso
}

void laserdisc_audio_end(void) { s_ld_bypass = false; }

// BLOCCANTE (portMAX_DELAY): aspetta spazio nel DMA -> pacing a tempo reale.
void laserdisc_audio_write(const int16_t *stereo, int frames) {
  if (!i2s_tx_handle || frames <= 0) return;
  size_t written = 0;
  i2s_channel_write(i2s_tx_handle, stereo, (size_t)frames * 2 * sizeof(int16_t),
                    &written, portMAX_DELAY);
}

void Audio::transmit() {
#if !defined(SND_I2S_DIGITAL) && !defined(SND_DIFF) && !defined(SND_LEFT_CHANNEL)
  // No audio hardware - skip entirely
  return;
#else
  if (s_ld_bypass) {                  // il player laserdisc e' l'unico writer
    vTaskDelay(pdMS_TO_TICKS(5));
    return;
  }
  size_t bytesOut = 0;
  do {
    i2s_channel_write(i2s_tx_handle, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);
    // ⭐ Quanti byte l'I2S ha davvero preso: e' l'unico modo di vedere DA FUORI
    // se il task audio sta in pari. L'atteso e' 500 blocchi/s (32 kHz / 64
    // frame per blocco); molto sotto = task audio in ritardo, ed e' QUELLA la
    // diagnosi, non gli fps ([[feedback_task_audio_indietro_watchdog]]).
    prof_aud_byte((unsigned int)bytesOut);

    // render the next audio chunk if data has actually been sent
    if(bytesOut) {
      // Jingle splash: ha priorita' su tutto (anche sul mute), unico writer.
      if (playMelody) {
        melody_render_buffer();
        continue;
      }
      if (muted) {
        // Mute totale: riempi il buffer con zero senza chiamare i render
        // della machine (evita residual hum/click quando torni al menu boot).
        memset(snd_buffer, 0, sizeof(snd_buffer));
        continue;
      }
      if (currentMachine->hasNamcoAudio())
        namco_render_buffer();
      else if(
#ifdef ENABLE_MRDO
        machineType == MCH_MRDO ||
#endif
#ifdef ENABLE_LADYBUG
        machineType == MCH_LADYBUG ||
#endif
#ifdef ENABLE_GREENBERET
        machineType == MCH_GREENBERET ||
#endif
#ifdef ENABLE_GIGAS
        machineType == MCH_GIGAS ||
#endif
#ifdef ENABLE_GIGAS2
        machineType == MCH_GIGAS2 ||
#endif
#ifdef ENABLE_ROADFIGHTER
        machineType == MCH_ROADFIGHTER ||
#endif
        false)
        sn76489_render_buffer();
#ifdef ENABLE_BAGMAN
      else if(machineType == MCH_BAGMAN)
        discrete_render_buffer();
#endif
#ifdef ENABLE_GALAXIAN
      else if(machineType == MCH_GALAXIAN)
        galaxian_render_buffer();
#endif
#ifdef ENABLE_MOONCRESTA
      else if(machineType == MCH_MOONCRESTA)
        galaxian_render_buffer();
#endif
#ifdef ENABLE_SPACE
      else if(machineType == MCH_SPACE)
        spaceinvaders_render_buffer();
#endif
#ifdef ENABLE_PHOENIX
      else if(machineType == MCH_PHOENIX)
        phoenix_render_buffer();
#endif
#ifdef ENABLE_SBRKOUT
      else if(machineType == MCH_SBRKOUT)
        sbrkout_render_buffer();
#endif
#ifdef ENABLE_FANTASY
      else if(machineType == MCH_FANTASY)
        fantasy_render_buffer();
#endif
#ifdef ENABLE_NIBBLER
      else if(machineType == MCH_NIBBLER)
        nibbler_render_buffer();
#endif
#ifdef ENABLE_BOMBBEE
      else if(machineType == MCH_BOMBBEE)
        bombbee_render_buffer();
#endif
#ifdef ENABLE_BOBLBOBL
      else if(machineType == MCH_BOBLBOBL)
        boblbobl_render_buffer();
#endif
#ifdef ENABLE_GOINDOL
      else if(machineType == MCH_GOINDOL)
        goindol_render_buffer();
#endif
#ifdef ENABLE_DKONG3
      else if(machineType == MCH_DKONG3)
        dkong3_render_buffer();
#endif
#ifdef ENABLE_PANG
      else if(machineType == MCH_PANG)
        pang_render_buffer();
#endif
#ifdef ENABLE_SUPXEVS
      else if(machineType == MCH_SUPXEVS)
        supxevs_render_buffer();
#endif
#ifdef ENABLE_TEMPEST
      else if(machineType == MCH_TEMPEST)
        tempest_render_buffer();
#endif
      else
        ay_render_buffer();
    }
  } while(bytesOut);
#endif // audio hardware available
}

#ifdef ENABLE_PANG
// Pang: l'OKI6295 (ADPCM) + YM2413 (OPLL musica) sono generati dentro la
// machine (renderAudio), gia' mixati e clampati a ~+/-500 (livello valueToBuffer).
// Il bridge pang_audio_fill e' definito in SPINNERINO.ino (dove pang.h e' incluso).
extern void pang_audio_fill(int16_t *out, int frames);
void Audio::pang_render_buffer(void) {
  int16_t buf[64];
  pang_audio_fill(buf, 64);
  for (int i = 0; i < 64; i++)
    valueToBuffer(i, (short)buf[i]);
}
#endif // ENABLE_PANG

void Audio::ay_render_buffer(void) {
  char AY = 2; 
  char AY_INC = 8;
  char AY_VOL = 5;

#ifdef ENABLE_FROGGER
  if (machineType == MCH_FROGGER) { AY = 1; AY_INC = 9; AY_VOL = 11; }
#endif
#ifdef ENABLE_ANTEATER
  if (machineType == MCH_ANTEATER) { AY = 2; AY_INC = 9; AY_VOL = 5; }
#endif
#ifdef ENABLE_TIMEPLT
  if (machineType == MCH_TIMEPLT) { AY = 2; AY_INC = 9; AY_VOL = 5; }
#endif
#ifdef ENABLE_BOMBJACK
  if (machineType == MCH_BOMBJACK) { AY = 3; AY_INC = 8; AY_VOL = 10; }
#endif
#ifdef ENABLE_GYRUSS
  // AY_INC = velocita'/pitch dei 5 AY. Il valore "MAME teorico" a 32kHz sarebbe 7,
  // ma l'output effettivo della scheda e' piu' basso -> servono valori molto piu'
  // alti (a orecchio). 16 = molto piu' veloce (regolabile: su=piu' veloce).
  // AY_INC = FREQUENZA/pitch dei suoni AY (musica E effetti, insert-coin incluso).
  // NON e' la velocita' di esecuzione. Originale = 9. Alzarlo rende solo i suoni
  // piu' acuti. Per il TEMPO/velocita' vedi l'emulazione CPU (gyruss.cpp run_frame).
  if (machineType == MCH_GYRUSS) { AY = 5; AY_INC = 9; AY_VOL = 4; }  // tutti e 5 gli AY; AY_VOL=4 per non clippare
#endif
#ifdef ENABLE_TUTANKHM
  if (machineType == MCH_TUTANKHM) { AY = 2; AY_INC = 7; AY_VOL = 5; }
#endif
#ifdef ENABLE_MOTORACE
  if (machineType == MCH_MOTORACE) { AY = 2; AY_INC = 5; AY_VOL = 5; }
#endif
#ifdef ENABLE_MOONPATROL
  if (machineType == MCH_MOONPATROL) { AY = 2; AY_INC = 5; AY_VOL = 7; }
#endif
#ifdef ENABLE_SCRAMBLE
  if (machineType == MCH_SCRAMBLE) { AY = 2; AY_INC = 8; AY_VOL = 5; }
#endif
#ifdef ENABLE_SUPERCOBRA
  // Super Cobra: stessa scheda audio Konami di Scramble (2x AY @ 1.789 MHz).
  if (machineType == MCH_SUPERCOBRA) { AY = 2; AY_INC = 8; AY_VOL = 5; }
#endif
#ifdef ENABLE_TIMEPLT
  // Time Pilot: 2x AY-3-8910 @ 1.789 MHz (stesso clock di Scramble) -> AY_INC=8.
  if (machineType == MCH_TIMEPLT) { AY = 2; AY_INC = 8; AY_VOL = 5; }
#endif
#ifdef ENABLE_BRUBBER
  if (machineType == MCH_BRUBBER) { AY = 2; AY_INC = 5; AY_VOL = 5; }
#endif
#ifdef ENABLE_PBACTION
  // Pinball Action: 3x AY-3-8910 @ 1.5 MHz (12MHz/8). AY_INC teorico = 1.5M/(8*32k) ~ 6.
  // AY_VOL=4 per non clippare con 9 canali (come Gyruss a 5 chip).
  if (machineType == MCH_PBACTION) { AY = 3; AY_INC = 6; AY_VOL = 4; }
#endif
#ifdef ENABLE_ARKANOID
  // Arkanoid bootleg arkangc: 1x AY-3-8910 con pin clock = 12MHz XTAL / 4 = 3 MHz
  // (MAME taito/arkanoid.cpp:1382 + bootleg() linea 1409 sostituisce con AY8910).
  // AY-3-8910 STANDARD ha divider interno /8 (MAME ay8910.cpp:1342).
  // AY_INC = pin_clock / (8 * SAMPLE_RATE) = 3000000/(8*32000) = 11.7 -> 12
  // (sample rate 32 kHz, vedi commento in init() su MAX98357A).
  // AY_VOL=1 perche' usiamo ay_dac_log[] gia' scalata (vedi render path sotto).
  if (machineType == MCH_ARKANOID) { AY = 1; AY_INC = 12; AY_VOL = 1; }
#endif
#ifdef ENABLE_ARKANOID2
  // Arkanoid 2 bootleg arknoid2b: 1x YM2203 PSG @ 3 MHz (MAME tnzs.cpp).
  // YM2203 ha step counter /4 (vs /8 di AY-3-8910 standard, MAME ay8910.cpp).
  // A parita' di clock pin avanza 2x piu' veloce → AY_INC raddoppiato:
  //   3000000 / (4 * 32000) = 23.4 → 23
  // Riusa ay_dac_log[] + envelope path di Arkanoid 1 (registri 0..13 identici).
  // SPINNERINO P4: FM (YM2203) disabilitato (ym2203_chip=nullptr).
  // AY_VOL=1 era per bilanciare il mix FM; senza FM serve volume PSG pieno.
  if (machineType == MCH_ARKANOID2) { AY = 1; AY_INC = 23; AY_VOL = 5; }
#endif
#ifdef ENABLE_COMMANDO
  // Commando: 2x YM2203, SSG/PSG @ 1.5 MHz, divider /4 → 1.5M/(4*32k) ≈ 12.
  // soundregs[0..15]=chip0, [16..31]=chip1. FM mixato a parte (blocco sotto).
  if (machineType == MCH_COMMANDO) { AY = 2; AY_INC = 12; AY_VOL = 5; }
#endif
#ifdef ENABLE_GNG
  // GnG: 2x YM2203, SSG @ 1.5 MHz (come Commando). FM mixato a parte (blocco sotto).
  if (machineType == MCH_GNG) { AY = 2; AY_INC = 12; AY_VOL = 5; }
#endif
#ifdef ENABLE_POPEYE
  // Popeye: 1x AY-3-8910. soundregs[0..13]. (musica gestita anche dal pacing
  // run_frame in popeye.cpp).
  if (machineType == MCH_POPEYE) { AY = 1; AY_INC = 8; AY_VOL = 6; }
#endif

  for(char ay = 0; ay < AY; ay++) {
    int ay_off = 16 * ay;
    for(char c = 0; c < 3; c++) {
      ay_period[ay][c] = currentMachine->soundregs[ay_off + 2 * c] + 256 * (currentMachine->soundregs[ay_off + 2 * c + 1] & 15);
      ay_enable[ay][c] = (((currentMachine->soundregs[ay_off + 7] >> c) & 1) | ((currentMachine->soundregs[ay_off + 7] >> (c + 2)) & 2)) ^ 3;
      // ⛔ MotoRace (19/10): il rumore NON si zittisce piu'. Il «drone» che
      // copriva la musica era la BATTERIA (rumore + inviluppo 0x09) suonata a
      // volume 15 fisso: ora MotoRace passa dal ramo con l'inviluppo di Moon
      // Patrol (stessa scheda Irem M52), come sul GALAGONE.
      { uint8_t vreg = currentMachine->soundregs[ay_off + 8 + c];
        // Per giochi che usano envelope (BombJack, Arkanoid) preserva il bit 4
        // (envelope-mode flag) cosi' il render path puo' distinguerlo.
        bool keep_env_bit =
#ifdef ENABLE_BOMBJACK
          (machineType == MCH_BOMBJACK) ||
#endif
#ifdef ENABLE_ARKANOID
          (machineType == MCH_ARKANOID) ||
#endif
#ifdef ENABLE_ARKANOID2
          (machineType == MCH_ARKANOID2) ||
#endif
          false;
        ay_volume[ay][c] = keep_env_bit ? (vreg & 0x1F)
                                        : ((vreg & 0x10) ? 15 : (vreg & 0x0f));
      }
    }
    ay_period[ay][3] = currentMachine->soundregs[ay_off + 6] & 0x1f;

#if defined(ENABLE_BOMBJACK) || defined(ENABLE_ARKANOID) || defined(ENABLE_ARKANOID2)
    if (
#ifdef ENABLE_BOMBJACK
        machineType == MCH_BOMBJACK ||
#endif
#ifdef ENABLE_ARKANOID
        machineType == MCH_ARKANOID ||
#endif
#ifdef ENABLE_ARKANOID2
        machineType == MCH_ARKANOID2 ||
#endif
        false) {
      ay_envelope_period[ay] = currentMachine->soundregs[ay_off + 11] + 256 * currentMachine->soundregs[ay_off + 12];
      uint8_t new_shape = currentMachine->soundregs[ay_off + 13];
      if (new_shape != ay_envelope_shape[ay]) {
        ay_envelope_shape[ay] = new_shape & 0x0F;
        ay_envelope_counter[ay] = 0;
        ay_envelope_step[ay] = (ay_envelope_shape[ay] < 4 || (ay_envelope_shape[ay] >= 8 && ay_envelope_shape[ay] < 12)) ? 0 : 15;
        ay_envelope_holding[ay] = 0;
      }
    }
#endif
  }

  // ── Arkanoid 2: FM YM2203 square-wave approximation (port da P4 multi).
  // Decode FNUM/BLOCK -> phase increment, mixato come square wave nel loop.
#ifdef ENABLE_ARKANOID2
  static uint32_t gng_fm_phase_sq[1][3] = { {0,0,0} };
  uint32_t gng_fm_inc[1][3] = { {0,0,0} };
  int      gng_fm_vol[1][3] = { {0,0,0} };
  if (machineType == MCH_ARKANOID2) {
    Arkanoid2 *a = (Arkanoid2*)currentMachine;
    for (int ch = 0; ch < 3; ch++) {
      if (!a->fm_keyed[0][ch]) continue;
      uint8_t lo = a->ym_regs[0][0xA0 + ch];
      uint8_t hi = a->ym_regs[0][0xA4 + ch];
      int block = (hi >> 3) & 7;
      int fnum  = ((hi & 7) << 8) | lo;
      if (!fnum) continue;
      // f = fnum * 20833 / 2^(20-block); phase_inc = f * 2^32 / 32000
      uint64_t inc64 = (uint64_t)fnum * 20833ULL;
      inc64 = (inc64 << (12 + block)) / 32000ULL;
      gng_fm_inc[0][ch] = (uint32_t)inc64;
      uint8_t tl = a->ym_regs[0][0x4C + ch] & 0x7F;
      gng_fm_vol[0][ch] = (127 - tl) >> 3;
    }
  }
#endif

  // ── Commando / GnG: 2x YM2203 FM square-wave approximation (stesso schema, 2 chip).
#if defined(ENABLE_COMMANDO) || defined(ENABLE_GNG)
  static uint32_t cmd_fm_phase_sq[2][3] = { {0,0,0}, {0,0,0} };
  uint32_t cmd_fm_inc[2][3] = { {0,0,0}, {0,0,0} };
  int      cmd_fm_vol[2][3] = { {0,0,0}, {0,0,0} };
#ifdef ENABLE_COMMANDO
  if (machineType == MCH_COMMANDO) {
    commando *c = (commando*)currentMachine;
    for (int chip = 0; chip < 2; chip++) {
      for (int ch = 0; ch < 3; ch++) {
        if (!c->fm_keyed[chip][ch]) continue;
        uint8_t lo = c->ym_regs[chip][0xA0 + ch];
        uint8_t hi = c->ym_regs[chip][0xA4 + ch];
        int block = (hi >> 3) & 7;
        int fnum  = ((hi & 7) << 8) | lo;
        if (!fnum) continue;
        uint64_t inc64 = (uint64_t)fnum * 20833ULL;
        inc64 = (inc64 << (12 + block)) / 32000ULL;
        cmd_fm_inc[chip][ch] = (uint32_t)inc64;
        uint8_t tl = c->ym_regs[chip][0x4C + ch] & 0x7F;
        cmd_fm_vol[chip][ch] = (127 - tl) >> 3;
      }
    }
  }
#endif
#ifdef ENABLE_GNG
  if (machineType == MCH_GNG) {
    gng *g = (gng*)currentMachine;
    for (int chip = 0; chip < 2; chip++) {
      for (int ch = 0; ch < 3; ch++) {
        if (!g->fm_keyed[chip][ch]) continue;
        uint8_t lo = g->ym_regs[chip][0xA0 + ch];
        uint8_t hi = g->ym_regs[chip][0xA4 + ch];
        int block = (hi >> 3) & 7;
        int fnum  = ((hi & 7) << 8) | lo;
        if (!fnum) continue;
        uint64_t inc64 = (uint64_t)fnum * 20833ULL;
        inc64 = (inc64 << (12 + block)) / 32000ULL;
        cmd_fm_inc[chip][ch] = (uint32_t)inc64;
        uint8_t tl = g->ym_regs[chip][0x4C + ch] & 0x7F;
        cmd_fm_vol[chip][ch] = (127 - tl) >> 3;
      }
    }
  }
#endif
#endif

  for(int i = 0; i < 64; i++) {
    short value = 0;

#ifdef ENABLE_GYRUSS
    // Drums Gyruss: steppa l'i8039 N istruzioni per OGNI campione di output
    // (32 kHz) -> il DAC viene campionato regolarmente = niente crackle.
    // N = velocita'/pitch dei drums: piu' alto = piu' veloci. (8039@8MHz/15 ~
    // 11-16 istr/campione a tempo reale; 18 per compensare il "troppo lento").
    if (machineType == MCH_GYRUSS) {
      gyruss *gyr = (gyruss*)currentMachine;
      gyr->service_i8039_irq();                       // assert IRQ 8039 se latchato
      for (int k = 0; k < 18; k++) gyr->step_i8039();
    }
#endif

#if defined(ENABLE_DKONG) || defined(ENABLE_DKONGJR)
    if(machineType == MCH_DKONG || machineType == MCH_DKONGJR) {
      dkong *dkongMachine = (dkong*)currentMachine;
      // ⛔⛔⛔ IL DAC DELL'8048 E' A 8 BIT **SENZA SEGNO**: a riposo sta a 128,
      // non a zero. Passandolo cosi' com'e' a valueToBuffer (che moltiplica per
      // 128/volumeSetting, x42,7 al volume di fabbrica) il centro finiva a
      // **+4.188** e ci restava: il segnale NON attraversava mai lo zero.
      // Misurato il 2026-08-28 con `_verify_gfx/dkong_harness/dkcmp.c` del
      // CABINATONE, applicando le due catene allo STESSO flusso di byte:
      //     sorgente   DC +4188, RMS 3060, picco 10880, 0,0% sotto lo zero
      //     centrato   DC    -3, RMS 3444, picco  8192, 49,5% sotto
      // Cioe' centrando si ottiene PIU' segnale utile con un picco MINORE.
      // Il difetto si sente in tre modi: meta' dinamica buttata, il pompaggio
      // dell'accoppiamento in alternata sui transitori (l'attacco e la coda
      // della melodia della morte) e un click a ogni interruzione del flusso.
      // Referto dell'utente: «il sorgente si sente male».
      // ⭐ Il valore di riposo e' **128**, non 0: cosi' quando la coda e' vuota
      // il centraggio qui sotto lo porta a zero (silenzio vero) invece che a
      // -128, e i campioni PCM continuano a suonare centrati.
      // ⭐⭐⭐ RICAMPIONATORE (2026-08-28). Prima si leggevano **32 byte per
      // blocco da 64, ognuno suonato DUE volte** (`i/2`): la sorgente veniva
      // letta a **16.000 campioni/s**. Ma il DAC di Donkey Kong gira a
      // **11.765 Hz**, e non e' una stima:
      //   · lo dice questo stesso file, nel ramo che chiamerebbe
      //     `i2s_set_sample_rates(..., 11765)` — un numero tarato su hardware;
      //   · ed e' stato MISURATO facendo girare `rom2.bin` col core i8048 del
      //     progetto e contando i **cicli macchina** (non le istruzioni: sul
      //     MCS-48 sono 1 o 2 ciascuna) fra due scritture sulla porta 1:
      //     **34,2-34,8 cicli per campione**, che a 400.000 cicli/s (6 MHz / 15)
      //     fanno **11.505-11.685 Hz**. Due strade indipendenti entro l'1,6%.
      // Il porting suonava quindi **1,36x piu' veloce e acuto**.
      // ✅ Qui si ricampiona invece di ritoccare costanti: un accumulatore a
      // virgola fissa avanza nella sorgente di 11.765 passi ogni 32.000
      // campioni d'uscita ([[feedback_adattare_il_ritmo_non_le_costanti]]).
      // ⚠️ Governa **insieme** il buffer dell'8048 E i tre canali PCM: sono
      // registrati allo stesso ritmo, e farne avanzare uno solo li sfaserebbe.
      // ⭐ Sostituisce ENTRAMBI i rami del vecchio `#ifdef`: quello attivo
      // (raddoppio 1:2) e quello spento, che non incrementava mai `rptr` e
      // avrebbe suonato per sempre i primi 64 byte.
      //   passo = 11765 * 65536 / 32000 = 24094  ->  11.764,6 Hz (-0,003%)
      static const uint32_t DK_STEP16 = ((uint32_t)11765 << 16) / 32000;
      static uint32_t dk_acc = 0;
      static uint8_t  dk_idx = 0;

      value = 128;
      if(dkongMachine->dkong_audio_rptr != dkongMachine->dkong_audio_wptr)
        value = dkongMachine->dkong_audio_transfer_buffer[dkongMachine->dkong_audio_rptr][dk_idx];

      for(char j = 0; j < 3; j++)
        if(dkongMachine->dkong_sample_cnt[j])
          value += *dkongMachine->dkong_sample_ptr[j] >> (2 - j);

      // l'avanzamento della sorgente, tutto in un punto solo
      dk_acc += DK_STEP16;
      while (dk_acc >= 0x10000u) {
        dk_acc -= 0x10000u;
        for(char j = 0; j < 3; j++)
          if(dkongMachine->dkong_sample_cnt[j]) {
            dkongMachine->dkong_sample_ptr[j]++;
            dkongMachine->dkong_sample_cnt[j]--;
          }
        if (++dk_idx >= 64) {
          dk_idx = 0;
          if(dkongMachine->dkong_audio_rptr != dkongMachine->dkong_audio_wptr)
            dkongMachine->dkong_audio_rptr = (dkongMachine->dkong_audio_rptr + 1) & DKONG_AUDIO_QUEUE_MASK;
        }
      }

      // ⭐ IL CENTRAGGIO, e va fatto QUI: dopo aver sommato i campioni PCM, che
      // sono gia' CON SEGNO. Toglierlo prima sposterebbe anche quelli.
      value -= 128;
    } else
#endif
#ifdef ENABLE_MARIOBROS
    if(machineType == MCH_MARIOBROS) {
      // Come sul CABINATONE: l'MCU sonora gira QUI, a cicli veri per ogni
      // campione, e si legge il livello del DAC (vedi mariobros.cpp).
      // Prima si suonavano i byte scritti sul DAC come campioni a 16 kHz fissi:
      // il M58715 non li scrive a ritmo costante -> gracchio.
      value = ((mariobros*)currentMachine)->audio_sample();
    } else
#endif
    if(
#ifdef ENABLE_FROGGER
       machineType == MCH_FROGGER ||
#endif
#ifdef ENABLE_1942
       machineType == MCH_1942 || 
#endif
#ifdef ENABLE_ANTEATER
       machineType == MCH_ANTEATER || 
#endif
#ifdef ENABLE_TIMEPLT
       machineType == MCH_TIMEPLT || 
#endif
#ifdef ENABLE_GYRUSS
       machineType == MCH_GYRUSS ||
#endif
#ifdef ENABLE_TUTANKHM
       machineType == MCH_TUTANKHM ||
#endif
       // ⛔ MOON PATROL non passa piu' di qui (25/09): ha il ramo suo, sotto,
       // con l'INVILUPPO — la sua batteria e' rumore + inviluppo 0x09.
       // ⛔ MOTORACE neppure (19/10): stessa scheda, stessa batteria (cmd 0x9C).
#ifdef ENABLE_SCRAMBLE
       machineType == MCH_SCRAMBLE ||
#endif
#ifdef ENABLE_SUPERCOBRA
       machineType == MCH_SUPERCOBRA ||
#endif
#ifdef ENABLE_TIMEPLT
       machineType == MCH_TIMEPLT ||
#endif
#ifdef ENABLE_BRUBBER
       machineType == MCH_BRUBBER ||
#endif
#ifdef ENABLE_PBACTION
       machineType == MCH_PBACTION ||
#endif
#ifdef ENABLE_COMMANDO
       machineType == MCH_COMMANDO ||   // 2x YM2203 SSG: tono+noise (coin/typing/musica)
#endif
#ifdef ENABLE_GNG
       machineType == MCH_GNG ||        // 2x YM2203 SSG (tono+noise); FM mixato a parte
#endif
#ifdef ENABLE_POPEYE
       machineType == MCH_POPEYE ||     // 1x AY-3-8910
#endif
       false
    ) {
      for(char ay = 0; ay < AY; ay++) {
        if(ay_period[ay][3]) {
          audio_cnt[ay][3] += AY_INC;
          if(audio_cnt[ay][3] > ay_period[ay][3]) {
            audio_cnt[ay][3] -= ay_period[ay][3];
            ay_noise_rng[ay] ^= (((ay_noise_rng[ay] & 1) ^ ((ay_noise_rng[ay] >> 3) & 1)) << 17);
            ay_noise_rng[ay] >>= 1;
          }
        }
        for(char c = 0; c < 3; c++) {
          if((ay_period[ay][c] || (ay_enable[ay][c] & 2)) && ay_volume[ay][c] && ay_enable[ay][c]) {
            short bit = 1;
            if(ay_enable[ay][c] & 1) bit &= (audio_toggle[ay][c] > 0) ? 1 : 0;
            if(ay_enable[ay][c] & 2) bit &= (ay_noise_rng[ay] & 1) ? 1 : 0;
            if(bit == 0) bit = -1;
            value += AY_VOL * bit * ay_volume[ay][c];
            audio_cnt[ay][c] += AY_INC;
            if(audio_cnt[ay][c] > ay_period[ay][c]) {
              audio_cnt[ay][c] -= ay_period[ay][c];
              audio_toggle[ay][c] = -audio_toggle[ay][c];
            }
          }
        }
      }
#ifdef ENABLE_GYRUSS
      // Drums Gyruss: somma il campione DAC dell'i8039 al mix dei 5 AY.
      // DC blocker (high-pass ~40Hz) per togliere l'offset di riposo dell'8039
      // -> niente pop/distorsione costante. Scala (3) = volume drums (regolabile).
      if (machineType == MCH_GYRUSS) {
        static int dc = 0;
        int s = ((gyruss*)currentMachine)->i8039_sample();  // -128..+127
        dc += (s - dc) >> 7;                                // media lenta = livello DC
        value += (s - dc) * 3;                              // componente AC * volume
      }
#endif
    }
#if defined(ENABLE_MOONPATROL) || defined(ENABLE_MOTORACE)
    // >>> MP_BATTERIA (25/09) -------------------------------------------------
    // ⭐ MOON PATROL: il ramo AY generico (qui sopra) con UNA sola aggiunta,
    // l'INVILUPPO. Toni e rumore sono copiati riga per riga dal ramo generico:
    // la MUSICA resta com'era. La BATTERIA del gioco e' rumore + inviluppo di
    // forma 0x09 (discesa e poi silenzio): il ramo generico l'inviluppo non lo
    // emula e un canale in modo inviluppo suonava a volume 15 FISSO = rumore
    // pieno e continuo al posto dei colpi.
    // Portato dal CABINATONE (mpatrol.cpp), dove l'utente l'ha giudicato
    // «perfetto», con le tre cose che li' sono state MISURATE:
    //   1) un passo ogni 16*EP cicli di clock (datasheet, ay8910.cpp): un colpo
    //      con EP=0x200 si spegne in 137 ms;
    //   2) «hold»: senza alternate si resta sul valore d'ARRIVO (discesa -> 0);
    //   3) curva LOGARITMICA del DAC dell'AY (~3 dB a gradino) sui soli livelli
    //      d'inviluppo: il colpo e' secco, niente coda che impasta.
    // La ripartenza avviene a OGNI scrittura del registro 13 (contatore in
    // soundregs[14] / [30], lo incrementa moonpatrol.cpp).
    // ⭐ 19/10: ci passa anche MOTORACE (stessa scheda Irem M52, la batteria
    // della musica di partenza e' il cmd 0x9C: rumore + inviluppo 0x09, EP
    // 0x0400). Sul GALAGONE la stessa cura e' stata provata: «funziona».
    else if (
#ifdef ENABLE_MOONPATROL
             machineType == MCH_MOONPATROL ||
#endif
#ifdef ENABLE_MOTORACE
             machineType == MCH_MOTORACE ||
#endif
             false) {
      // DAC dell'AY (tabella AYUMI/MAME), 8190 = 15 x 546
      static const short AY_LOG[16] = { 0, 82, 118, 172, 251, 373, 528, 879,
                                        1037, 1679, 2393, 3054, 4034, 5203, 6598, 8190 };
      // cicli di clock dell'AY (3,579545 MHz / 4) per campione a 32 kHz, in 16.16
      static const uint64_t MP_CLK_PER_CAMPIONE = ((uint64_t)894886 << 16) / 32000;
      static uint64_t env_acc[2] = { 0, 0 };
      static int      env_pos[2] = { 0, 0 }, env_dir[2] = { -1, -1 };
      static bool     env_stop[2] = { true, true };
      static uint8_t  env_visto[2] = { 0, 0 };
      for(char ay = 0; ay < AY; ay++) {
        const unsigned char *r = currentMachine->soundregs + 16 * ay;
        // ripartenza dell'inviluppo a ogni scrittura del registro 13
        if (r[14] != env_visto[ay]) {
          env_visto[ay] = r[14];
          const uint8_t sh = r[13] & 0x0F;
          env_dir[ay]  = (sh & 0x04) ? +1 : -1;
          env_pos[ay]  = (sh & 0x04) ? 0 : 15;
          env_stop[ay] = false;
          env_acc[ay]  = 0;
        }
        // avanzamento: un passo ogni 16 x EP cicli di clock
        if (!env_stop[ay]) {
          const uint32_t ep = (uint32_t)(r[11] | (r[12] << 8));
          const uint64_t soglia = (uint64_t)(16u * (ep ? ep : 1u)) << 16;
          env_acc[ay] += MP_CLK_PER_CAMPIONE;
          while (!env_stop[ay] && env_acc[ay] >= soglia) {
            env_acc[ay] -= soglia;
            env_pos[ay] += env_dir[ay];
            if (env_pos[ay] < 0 || env_pos[ay] > 15) {
              const uint8_t sh = r[13] & 0x0F;
              if (!(sh & 0x08))      { env_pos[ay] = 0; env_stop[ay] = true; }
              else if (sh & 0x01)    { env_pos[ay] = (sh & 0x02) ? ((env_dir[ay] > 0) ? 0 : 15)
                                                                 : ((env_dir[ay] > 0) ? 15 : 0);
                                       env_stop[ay] = true; }
              else if (sh & 0x02)    { env_dir[ay] = -env_dir[ay]; env_pos[ay] += env_dir[ay]; }
              else                   env_pos[ay] = (env_dir[ay] > 0) ? 0 : 15;
            }
          }
        }
        // rumore: il ramo generico, con UNA differenza. Col periodo 0 quello
        // FERMA il generatore (il canale da' un valore costante, un «tic» sordo);
        // sul chip vero 0 vale 1 (datasheet, ay8910.cpp) = il rumore piu' acuto.
        // E Moon Patrol scrive proprio 0x00 in R6 per la batteria (misurato).
        {
          const unsigned short np = ay_period[ay][3] ? ay_period[ay][3] : 1;
          audio_cnt[ay][3] += AY_INC;
          if(audio_cnt[ay][3] > np) {
            audio_cnt[ay][3] -= np;
            ay_noise_rng[ay] ^= (((ay_noise_rng[ay] & 1) ^ ((ay_noise_rng[ay] >> 3) & 1)) << 17);
            ay_noise_rng[ay] >>= 1;
          }
        }
        for(char c = 0; c < 3; c++) {
          // livello: fisso come nel ramo generico, oppure dall'inviluppo
          const bool env = (r[8 + c] & 0x10) != 0;
          const int  ampiezza = env ? (AY_VOL * 15 * AY_LOG[env_pos[ay]] + 4095) / 8190
                                    : AY_VOL * (r[8 + c] & 0x0f);
          if((ay_period[ay][c] || (ay_enable[ay][c] & 2)) && ampiezza && ay_enable[ay][c]) {
            short bit = 1;
            if(ay_enable[ay][c] & 1) bit &= (audio_toggle[ay][c] > 0) ? 1 : 0;
            if(ay_enable[ay][c] & 2) bit &= (ay_noise_rng[ay] & 1) ? 1 : 0;
            if(bit == 0) bit = -1;
            value += bit * ampiezza;
            audio_cnt[ay][c] += AY_INC;
            if(audio_cnt[ay][c] > ay_period[ay][c]) {
              audio_cnt[ay][c] -= ay_period[ay][c];
              audio_toggle[ay][c] = -audio_toggle[ay][c];
            }
          }
        }
      }
    }
    // <<< MP_BATTERIA ---------------------------------------------------------
#endif
#ifdef ENABLE_BOMBJACK
    else if (machineType == MCH_BOMBJACK) {
      for(char ay = 0; ay < AY; ay++) {
        if (!ay_envelope_holding[ay] && ay_envelope_period[ay] > 0) {
          ay_envelope_counter[ay] += AY_INC;
          if (ay_envelope_counter[ay] >= ay_envelope_period[ay]) {
            ay_envelope_counter[ay] -= ay_envelope_period[ay];
            if (ay_envelope_shape[ay] < 8) {
              ay_envelope_step[ay]++;
              if (ay_envelope_step[ay] > 15) {
                ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 0 : 15; 
                if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1; 
              }
            } else {
              ay_envelope_step[ay]--;
              if (ay_envelope_step[ay] < 0) {
                ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 15 : 0; 
                if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1; 
              }
            }
          }
        }
        if(ay_period[ay][3]) {
          audio_cnt[ay][3] += AY_INC;
          if(audio_cnt[ay][3] > ay_period[ay][3]) {
            audio_cnt[ay][3] -= ay_period[ay][3];
            uint32_t b = (((ay_noise_rng[ay] >> 0) ^ (ay_noise_rng[ay] >> 3)) & 1);
            ay_noise_rng[ay] = (ay_noise_rng[ay] >> 1) | (b << 16);
          }
        }
        for(char c = 0; c < 3; c++) {
          if(ay_period[ay][c] && ay_enable[ay][c]) {
            int cur_v = (ay_volume[ay][c] & 0x10) ? ay_envelope_step[ay] : (ay_volume[ay][c] & 0x0F);
            if (cur_v > 0) {
              short bit = 1;
              if(ay_enable[ay][c] & 1) bit &= (audio_toggle[ay][c] > 0) ? 1:0;
              if(ay_enable[ay][c] & 2) bit &= (ay_noise_rng[ay] & 1) ? 1:0;
              if(bit == 0) bit = -1;
              value += AY_VOL * bit * cur_v;
            }
            audio_cnt[ay][c] += AY_INC;
            if(audio_cnt[ay][c] > ay_period[ay][c]) {
              audio_cnt[ay][c] -= ay_period[ay][c];
              audio_toggle[ay][c] = -audio_toggle[ay][c];
            }
          }
        }
      }
      value = value / 3;
    }
#endif
#if defined(ENABLE_ARKANOID) || defined(ENABLE_ARKANOID2)
    else if (
        false
#ifdef ENABLE_ARKANOID
        || machineType == MCH_ARKANOID
#endif
#ifdef ENABLE_ARKANOID2
        // YM2203 PSG (registri 0..13 binario-compatibili con AY-3-8910).
        // FM channels (reg 0x20+) ignorati: arknoid2 usa principalmente PSG.
        || machineType == MCH_ARKANOID2
#endif
    ) {
      // Path AY dedicato Arkanoid: tabella DAC logaritmica + while loop counter.
      // DAC AY-3-8910 (MAME ay8910.cpp:720, AYUMI ayumi.c:7-23): 16 step
      // logaritmici ~3 dB/step, scalati a max 120 (3 canali sommati = 360 max).
      // Headroom volutamente alto per evitare clipping al gain stage MAX98357A
      // (default GAIN=+9dB amplifica gia' ~2.8x in analogico).
      static const short ay_dac_log[16] = {
          0,   1,   2,   3,   4,   5,   8,  13,
         15,  25,  35,  45,  59,  76,  97, 120
      };
      for(char ay = 0; ay < AY; ay++) {
        // Envelope generator (shape attack/decay/hold, MAME ay8910.cpp)
        if (!ay_envelope_holding[ay] && ay_envelope_period[ay] > 0) {
          ay_envelope_counter[ay] += AY_INC;
          while (ay_envelope_counter[ay] >= ay_envelope_period[ay]) {
            ay_envelope_counter[ay] -= ay_envelope_period[ay];
            if (ay_envelope_shape[ay] < 8) {
              ay_envelope_step[ay]++;
              if (ay_envelope_step[ay] > 15) {
                ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 0 : 15;
                if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1;
              }
            } else {
              ay_envelope_step[ay]--;
              if (ay_envelope_step[ay] < 0) {
                ay_envelope_step[ay] = (ay_envelope_shape[ay] & 1) ? 15 : 0;
                if (ay_envelope_shape[ay] & 2) ay_envelope_holding[ay] = 1;
              }
            }
          }
        }
        // Noise LFSR (while: clocca finche' il counter copre AY_INC step)
        if (ay_period[ay][3]) {
          audio_cnt[ay][3] += AY_INC;
          while (audio_cnt[ay][3] >= ay_period[ay][3]) {
            audio_cnt[ay][3] -= ay_period[ay][3];
            uint32_t b = (((ay_noise_rng[ay] >> 0) ^ (ay_noise_rng[ay] >> 3)) & 1);
            ay_noise_rng[ay] = (ay_noise_rng[ay] >> 1) | (b << 16);
          }
        }
        // 3 canali tone
        for(char c = 0; c < 3; c++) {
          if (ay_period[ay][c] && ay_enable[ay][c]) {
            int cur_v = (ay_volume[ay][c] & 0x10) ? ay_envelope_step[ay]
                                                  : (ay_volume[ay][c] & 0x0F);
            if (cur_v > 0) {
              short bit = 1;
              if (ay_enable[ay][c] & 1) bit &= (audio_toggle[ay][c] > 0) ? 1 : 0;
              if (ay_enable[ay][c] & 2) bit &= (ay_noise_rng[ay] & 1) ? 1 : 0;
              if (bit == 0) bit = -1;
              value += bit * ay_dac_log[cur_v & 0x0F];
            }
            audio_cnt[ay][c] += AY_INC;
            // WHILE (non if): a period basso devo togglare piu' volte per
            // sample, altrimenti perdo i toggle e genero subarmoniche.
            while (audio_cnt[ay][c] >= ay_period[ay][c]) {
              audio_cnt[ay][c] -= ay_period[ay][c];
              audio_toggle[ay][c] = -audio_toggle[ay][c];
            }
          }
        }
      }
    }
#endif // ENABLE_ARKANOID || ENABLE_ARKANOID2
#ifdef ENABLE_ARKANOID2
    // Arkanoid 2: FM YM2203 square-wave (port P4 multi audio.cpp:730-746).
    if (machineType == MCH_ARKANOID2) {
      int gain = 4;
      int fm_sum = 0;
      for (int ch = 0; ch < 3; ch++) {
        int v = gng_fm_vol[0][ch];
        if (!v) continue;
        gng_fm_phase_sq[0][ch] += gng_fm_inc[0][ch];
        short bit = (gng_fm_phase_sq[0][ch] & 0x80000000UL) ? 1 : -1;
        fm_sum += bit * v * gain;
      }
      if (fm_sum >  400) fm_sum =  400;
      if (fm_sum < -400) fm_sum = -400;
      value += fm_sum;
    }
#endif
#if defined(ENABLE_COMMANDO) || defined(ENABLE_GNG)
    // Commando / GnG: FM YM2203 square-wave, 2 chip x 3 canali.
    if (machineType == MCH_COMMANDO || machineType == MCH_GNG) {
      int gain = (machineType == MCH_GNG) ? 4 : 2;  // GnG: FM piu' rado -> gain 4
      int fm_sum = 0;
      for (int chip = 0; chip < 2; chip++) {
        for (int ch = 0; ch < 3; ch++) {
          int v = cmd_fm_vol[chip][ch];
          if (!v) continue;
          cmd_fm_phase_sq[chip][ch] += cmd_fm_inc[chip][ch];
          short bit = (cmd_fm_phase_sq[chip][ch] & 0x80000000UL) ? 1 : -1;
          fm_sum += bit * v * gain;
        }
      }
      if (fm_sum >  400) fm_sum =  400;
      if (fm_sum < -400) fm_sum = -400;
      value += fm_sum;
    }
#endif
#ifdef ENABLE_MOONPATROL
    // MSM5205 ADPCM = le ESPLOSIONI dei massi (26/09). Il campione esce dalla
    // coda di moonpatrol.cpp a 4 kHz, non dal valore istantaneo: il M6803 gira
    // a blocchi di un quadro. /2: rispetto a un canale AY e' ~2 dB sotto il
    // rapporto del CABINATONE (x8 contro 8190/3), per non tosare col resto.
    if (machineType == MCH_MOONPATROL && g_moonpatrol_instance) {
      value += g_moonpatrol_instance->msm_campione() / 2;
    }
#endif
    valueToBuffer(i, value);
  }
}

void Audio::sn76489_render_buffer(void) {
    int sn_inc = 11;  // SN_CLOCK / SAMPLE_RATE (tarato @24kHz come FULL33)

#ifdef ENABLE_GREENBERET
    // SPINNERINO P4: l'I2S gira davvero a 32kHz (i2s_set_sample_rates(24000) e'
    // un no-op sul nuovo driver i2s_channel). FULL33 usava sn_inc=11 @24kHz; a
    // 32kHz reali serve 11*24000/32000 = 8.25 -> 8, altrimenti i toni escono
    // ~1.33x troppo acuti (melodie un'ottava-ish piu' alte del sorgente). 8
    // traspone tutto uniformemente: intervalli preservati, pitch corretta.
    // NB: gigas/gigas2/ladybug/mrdo hanno lo stesso offset latente 32k-vs-24k.
    if (machineType == MCH_GREENBERET) sn_inc = 8;
#endif
#ifdef ENABLE_ROADFIGHTER
    // roadf: SN76489A @ 1.79 MHz (XTAL 14.318/8) -> note molto piu' basse di gigas.
    // Valore di ORIGINALE; se il pitch risulta acuto in WIFI (I2S 32k reali, vedi
    // nota GreenBeret) provare 4 (= 5*24/32).
    if (machineType == MCH_ROADFIGHTER) sn_inc = 5;
#endif

    // 4 chip max (Gigas), MrDo/Ladybug/GreenBeret usano solo i primi 1-2.
    static uint32_t noise_lfsr[4] = {0x4000, 0x4000, 0x4000, 0x4000};

    // Per Gigas con 4 chip mixati mono, riduci gain per chip per evitare
    // saturazione (gain*4 vs gain*2 dei 2-chip games).
    bool fourChip = false;
#ifdef ENABLE_GIGAS
    if (machineType == MCH_GIGAS) fourChip = true;
#endif
#ifdef ENABLE_GIGAS2
    if (machineType == MCH_GIGAS2) fourChip = true;
#endif
    int chips = fourChip ? 4 : 2;
    int gainPerCh = fourChip ? 3 : 6;   // 6 per 2-chip, 3 per 4-chip (anti-clip)

    // Volumi con hold
    int vol[4][4];
    for (int chip = 0; chip < chips; chip++) {
        for (int c = 0; c < 4; c++) {
            if (currentMachine->sn_hold[chip][c] > 0) {
                vol[chip][c] = currentMachine->sn_min_volume[chip][c];
                currentMachine->sn_hold[chip][c]--;
                if (currentMachine->sn_hold[chip][c] == 0)
                    currentMachine->sn_min_volume[chip][c] = currentMachine->sn_volume[chip][c];
            } else {
                vol[chip][c] = currentMachine->sn_volume[chip][c];
                currentMachine->sn_min_volume[chip][c] = currentMachine->sn_volume[chip][c];
            }
        }
    }

    for (int i = 0; i < 64; i++) {
        short sample = 0;

        for (int chip = 0; chip < chips; chip++) {
            for (int c = 0; c < 4; c++) {
                int period = currentMachine->sn_period[chip][c];

                if (vol[chip][c] < 15 && period > 0) {
                    sn_counter[chip][c] -= sn_inc;

                    while (sn_counter[chip][c] <= 0) {
                        if (c == 3) {  // Noise channel
                            uint32_t feedback = (noise_lfsr[chip] & 0x01) ^ ((noise_lfsr[chip] & 0x02) >> 1);
                            noise_lfsr[chip] = (noise_lfsr[chip] >> 1) | (feedback << 14);
                            sn_toggle[chip][c] = (noise_lfsr[chip] & 0x01) ? 1 : -1;
                            sn_counter[chip][c] += (period << 3);  // Rallenta noise
                        } else {  // Tone
                            sn_counter[chip][c] += period;
                            sn_toggle[chip][c] = -sn_toggle[chip][c];
                        }
                    }
                    sample += sn_toggle[chip][c] * (15 - vol[chip][c]) * gainPerCh;
                }
            }
        }
#ifdef ENABLE_ROADFIGHTER
        // DAC 8-bit R2R (motore/crash). dac_sample = signed -128..127, latchato.
        if (machineType == MCH_ROADFIGHTER)
            sample += ((Roadfighter*)currentMachine)->roadf_dac_sample() * 2;
#endif
        valueToBuffer(i, sample);
    }
}

#ifdef ENABLE_BOMBBEE
// ============================================================================
// Bomb Bee (Namco custom sound) — port di namco/warpwarp_a.cpp.
// Due segnali sommati: music (contatore mascherato) + sound (toni/rumore).
// Sintesi interna a 192 kHz (= clock/3/2/16) con downsample 8:1 a 24 kHz.
// Envelope esponenziale (decay) su entrambi i canali.
//
// soundregs: [0]=sound_w(0x6002) [1]=music1(0x6010) [2]=music2(0x6020)
//            [3]=trigger sound_w  [4]=trigger music2_w (settati in wrZ80)
// ============================================================================
void Audio::bombbee_render_buffer(void) {
  static bool     init = false;
  static int16_t  decay_lut[256];           // ampiezza per (volume>>7)
  static uint8_t  s_latch = 0, m1 = 0, m2 = 0;
  static int32_t  s_vol = 0, m_vol = 0;     // 0..0x7fff
  static int32_t  s_vfrac = 0, m_vfrac = 0; // accumulatore decay (milli)
  static int      s_dec = 178, m_dec = 57;  // decremento milli per sample 192k
  static uint16_t noise = 0;
  static int32_t  mcarry = 0, vcarry = 0;
  static uint32_t mcount = 0, vcount = 0;
  static int16_t  s_sig = 0, m_sig = 0;     // +amp / -amp / 0

  const int CLK16H = 192000;   // clock/3/2/16
  const int CLK1V  = 8000;     // clock/3/2/384

  if (!init) {
    // ampiezza bipolare ~±150 max, decadimento esponenziale come MAME m_decay[]
    for (int i = 0; i < 256; i++)
      decay_lut[i] = (int16_t)(150.0 / exp((255 - i) / 32.0) + 0.5);
    init = true;
  }

  // trigger envelope su scrittura (consuma i flag settati in wrZ80)
  if (currentMachine->soundregs[3]) {
    s_latch = currentMachine->soundregs[0] & 0x0f;
    s_vol = 0x7fff; noise = 0;
    s_dec = (s_latch & 8) ? 178 : 89;        // decay rapido/lento (sound)
    currentMachine->soundregs[3] = 0;
  }
  if (currentMachine->soundregs[4]) {
    m_vol = 0x7fff;
    m_dec = (currentMachine->soundregs[2] & 0x10) ? 178 : 57; // rapido/lento (music)
    currentMachine->soundregs[4] = 0;
  }
  m1 = currentMachine->soundregs[1] & 0x3f;  // frequenza music
  m2 = currentMachine->soundregs[2] & 0x3f;  // forma d'onda + gate

  for (int i = 0; i < 64; i++) {
    for (int k = 0; k < 8; k++) {            // 8 step 192k per campione 24k
      int16_t damp_m = decay_lut[m_vol >> 7];
      int16_t damp_s = decay_lut[s_vol >> 7];

      // ---- music: contatore a frequenza CLK16H/(4*(64-m1)) ----
      mcarry -= CLK16H / (4 * (64 - m1));
      while (mcarry < 0) {
        mcarry += CLK16H;
        mcount++;
        bool on = (mcount & ~m2 & 15) != 0;
        if ((m2 & 32) && (noise & 0x8000)) on = true;  // gate da rumore
        m_sig = on ? damp_m : -damp_m;
      }

      // ---- sound + noise: clock 1V = 8 kHz ----
      vcarry -= CLK1V;
      while (vcarry < 0) {
        vcarry += CLK16H;
        vcount++;
        if ((vcount & 3) == 2) {             // LFSR rumore: bit0 = bit0 ^ !bit10
          if ((noise & 1) == ((noise >> 10) & 1)) noise = (noise << 1) | 1;
          else                                     noise = (noise << 1);
        }
        bool on;
        switch (s_latch & 7) {
          case 0: on = vcount & 0x04; break;            // 4V
          case 1: on = vcount & 0x08; break;            // 8V
          case 2: on = vcount & 0x10; break;            // 16V
          case 3: on = vcount & 0x20; break;            // 32V
          case 4: on = !(vcount & 0x01) && !(vcount & 0x10); break;  // TONE1
          case 5: on = !(vcount & 0x02) && !(vcount & 0x20); break;  // TONE2
          case 6: on = !(vcount & 0x04) && !(vcount & 0x40); break;  // TONE3
          default: on = noise & 0x8000; break;          // NOISE
        }
        s_sig = on ? damp_s : -damp_s;
      }

      // ---- decay envelope (decremento volume) ----
      if (s_vol > 0) { s_vfrac += s_dec; while (s_vfrac >= 1000) { s_vfrac -= 1000; if (s_vol > 0) s_vol--; } }
      if (m_vol > 0) { m_vfrac += m_dec; while (m_vfrac >= 1000) { m_vfrac -= 1000; if (m_vol > 0) m_vol--; } }
    }
    valueToBuffer(i, (short)(s_sig + m_sig));
  }
}
#endif // ENABLE_BOMBBEE


void Audio::namco_render_buffer(void) {
  // parse all three wsg channels
  for(char ch = 0; ch < 3; ch++) {  
    snd_wave[ch] = currentMachine->waveRom(currentMachine->soundregs[ch * 5 + 0x05] & 0x07);
    snd_freq[ch] = (ch == 0) ? currentMachine->soundregs[0x10] : 0; //5050-5054, 5056-5059, 505b-505e
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x11] << 4;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x12] << 8;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x13] << 12;
    snd_freq[ch] += currentMachine->soundregs[ch * 5 + 0x14] << 16;        
    snd_volume[ch] = currentMachine->soundregs[ch * 5 + 0x15]; //5055, 505a, 505f
  }

#ifdef ENABLE_XEVIOUS
  // ⭐ XEVIOUS — BLASTER (la bomba a terra, tasto B): l'utente lo sentiva basso.
  // Non e' un campione: lo fa il WSG, gli stessi canali di musica e zapper, e
  // alzare il guadagno generale alzerebbe anche quelli.
  // QUALE voce sia il blaster e' stato **MISURATO** su CABINATONE con
  // `_verify_gfx\xevious_harness\mk_xevblast.py`: due partite identiche in tutto
  // tranne il tasto B, e si confrontano le terne (canale, forma d'onda, volume)
  // che si accendono. Una sola compare col blaster e mai senza:
  //   **canale 1, forma d'onda 1, volume 14** (385 frame su 18 bombe, ~0,36 s).
  // Il volume del registro e' gia' 14 su 15: dentro il chip non c'e' spazio, il
  // moltiplicatore e' l'unica leva. Si applica al VOLUME, una volta per blocco.
  // ⚠️ Qui il bersaglio del mix e' +/-512 (vedi valueToBuffer): col boost il
  // caso peggiore del solo WSG passa da 315 a 511 — al limite, ma dentro. Se
  // per giunta parte anche un'esplosione (+/-254) si sfora, come gia' succedeva
  // prima del boost: e' un picco breve e raro, non lo stato normale.
  if(machineType == MCH_XEVIOUS &&
     (currentMachine->soundregs[1 * 5 + 0x05] & 0x07) == 1)
    snd_volume[1] *= 3;
#endif

  // render first buffer contents
  for(int i = 0; i < 64; i++) {
    short value = 0;

    // add up to three wave signals
    if(snd_volume[0]) value += snd_volume[0] * snd_wave[0][(snd_cnt[0] >> 13) & 0x1f];
    if(snd_volume[1]) value += snd_volume[1] * snd_wave[1][(snd_cnt[1] >> 13) & 0x1f];
    if(snd_volume[2]) value += snd_volume[2] * snd_wave[2][(snd_cnt[2] >> 13) & 0x1f];

    snd_cnt[0] += snd_freq[0];
    snd_cnt[1] += snd_freq[1];
    snd_cnt[2] += snd_freq[2];
    
#ifdef ENABLE_GALAGA
    if(machineType == MCH_GALAGA) {
      galaga *galagaMachine = (galaga*)currentMachine;

      if(galagaMachine->snd_boom_cnt) {
        value += *galagaMachine->snd_boom_ptr * 3;

        if(galagaMachine->snd_boom_cnt & 1)
          galagaMachine->snd_boom_ptr++;

        galagaMachine->snd_boom_cnt--;
      }
    }
#endif
#ifdef ENABLE_XEVIOUS
    // ⭐ Xevious: le esplosioni (navetta e bersagli) le fa il Namco 54XX, che
    // non emuliamo — ma i suoi COMANDI arrivano, e `xevious::namco_write()` li
    // riconosce dai primi due byte del blocco e arma il campione giusto.
    // Stesso schema di Galaga qui sopra, non uno nuovo: il puntatore avanza
    // ogni DUE campioni, cioe' il campione sta a 16 kHz e il buffer a 32, e il
    // guadagno e' 3 come li', cosi' il livello e' quello del WSG.
    if(machineType == MCH_XEVIOUS) {
      xevious *xevMachine = (xevious*)currentMachine;

      if(xevMachine->snd_boom_cnt) {
        // guadagno 2 e non 3 come Galaga: sul device le due esplosioni
        // coprivano il resto (referto utente 2026-08-10, «abbassa leggermente»)
        value += *xevMachine->snd_boom_ptr * 2;

        if(xevMachine->snd_boom_cnt & 1)
          xevMachine->snd_boom_ptr++;

        xevMachine->snd_boom_cnt--;
      }
    }
#endif
    valueToBuffer(i, value);
  }
}

#ifdef ENABLE_SPACE
void Audio::spaceinvaders_render_buffer(void) {
  // Space Invaders discrete audio (based on MAME mw8080bw_a.cpp)
  //
  // soundregs[0] = port 3: UFO(0) Shot(1) Explosion(2) InvaderDie(3) ExtPlay(4)
  // soundregs[1] = port 5: Fleet1(0) Fleet2(1) Fleet3(2) Fleet4(3) UFOhit(4)

  uint8_t p3 = currentMachine->soundregs[0];
  uint8_t p5 = currentMachine->soundregs[1];

  // Fleet: pick highest active bit → tone frequency (Hz)
  // Original hardware: 555 timer ~33-55Hz, doubled for small speaker audibility
  const int fleet_freq[4] = { 110, 80, 74, 66 };
  int fleet_f = 0;
  for(int b = 3; b >= 0; b--) {
    if(p5 & (1 << b)) { fleet_f = fleet_freq[b]; break; }
  }

  for(int i = 0; i < 64; i++) {
    short value = 0;

    // ── Advance noise LFSR: 17-bit, taps 4+16, clock 7515 Hz ──
    si_noise_clock += 7515;
    while(si_noise_clock >= 24000) {
      si_noise_clock -= 24000;
      int bit = ((si_noise_rng >> 4) ^ (si_noise_rng >> 16)) & 1;
      si_noise_rng = ((si_noise_rng << 1) | bit) & 0x1FFFF;
      si_noise_out = (si_noise_rng >> 12) & 1;
    }

    // ── UFO: SN76477 – SLF triangle ~5.3Hz modulates VCO 1220-3700Hz ──
    if(p3 & 0x01) {
      // SLF triangle: full cycle = 24000/5.3 ≈ 4528 samples
      si_ufo_sweep = (si_ufo_sweep + 1) % 4528;
      int slf_pos = (si_ufo_sweep < 2264) ? si_ufo_sweep : (4528 - si_ufo_sweep);
      int vco_freq = 1220 + (int)((long)slf_pos * 2480 / 2264);
      // VCO square wave: counter += freq, toggle at 12000 (= 24kHz/2)
      si_ufo_cnt += vco_freq;
      if(si_ufo_cnt >= 12000) {
        si_ufo_cnt -= 12000;
        si_ufo_toggle = -si_ufo_toggle;
      }
      value += si_ufo_toggle * 60;
    } else {
      si_ufo_sweep = 0;
    }

    // ── SHOT: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x02) {
      if(!si_shot_playing) { si_shot_playing = 1; si_shot_pos = 0; si_shot_toggle = 0; }
      if((si_shot_pos >> 1) < si_sample_shot_LEN) {
        value += (signed char)pgm_read_byte(&si_sample_shot[si_shot_pos >> 1]) * 3;
        si_shot_pos++;
      }
    } else {
      si_shot_playing = 0;
    }

    // ── COIN INSERT: metallic clink (triggered via soundregs[2]) ──
    if(currentMachine->soundregs[2] && si_coin_timer == 0) {
      si_coin_timer = 360;  // ~15ms
      si_coin_env = 120;
      currentMachine->soundregs[2] = 0;
    }
    if(si_coin_timer > 0) {
      // Primary metallic tone: 4500Hz
      si_coin_cnt += 4500;
      if(si_coin_cnt >= 12000) {
        si_coin_cnt -= 12000;
        si_coin_toggle = -si_coin_toggle;
      }
      // Overtone for metallic ring: 9500Hz
      si_coin_cnt2 += 9500;
      if(si_coin_cnt2 >= 12000) {
        si_coin_cnt2 -= 12000;
        si_coin_toggle2 = -si_coin_toggle2;
      }
      value += (si_coin_toggle * si_coin_env + si_coin_toggle2 * (si_coin_env / 2)) / 2;
      si_coin_timer--;
      if((si_coin_timer % 12) == 0 && si_coin_env > 5) si_coin_env--;
    }

    // ── EXPLOSION: noise burst with slow decay (RC ~2.7s) ──
    if(p3 & 0x04) {
      if(si_explo_env == 0) si_explo_env = 120;  // init on trigger
      int noise = si_noise_out ? 1 : -1;
      value += noise * si_explo_env;
      // Slow decay: decrease envelope every ~50 samples (~2ms)
      si_explo_cnt++;
      if(si_explo_cnt >= 50) {
        si_explo_cnt = 0;
        if(si_explo_env > 15) si_explo_env--;
      }
    } else {
      si_explo_env = 0;
      si_explo_cnt = 0;
    }

    // ── INVADER DIE: original sample playback (12kHz samples, play each twice for 24kHz) ──
    if(p3 & 0x08) {
      if(!si_invhit_playing) { si_invhit_playing = 1; si_invhit_pos = 0; }
      if((si_invhit_pos >> 1) < si_sample_invhit_LEN) {
        value += (signed char)pgm_read_byte(&si_sample_invhit[si_invhit_pos >> 1]) * 3;
        si_invhit_pos++;
      }
    } else {
      si_invhit_playing = 0;
    }

    // ── FLEET MOVEMENT: low bass tone while any fleet bit set ──
    if(fleet_f > 0) {
      si_fleet_cnt += fleet_f;
      if(si_fleet_cnt >= 12000) {
        si_fleet_cnt -= 12000;
        si_fleet_toggle = -si_fleet_toggle;
      }
      value += si_fleet_toggle * 90;
    }

    // ── UFO HIT: descending warble tone ~2000Hz with ~15Hz modulation ──
    if(p5 & 0x10) {
      if(si_ufohit_freq == 0) si_ufohit_freq = 2000;  // init on trigger
      // Warble modulation at ~15Hz: amplitude ±200Hz
      si_ufohit_warble = (si_ufohit_warble + 1) % 1600;  // 24000/15 = 1600
      int warble_pos = (si_ufohit_warble < 800) ?
        (int)si_ufohit_warble : (int)(1600 - si_ufohit_warble);
      int mod_freq = si_ufohit_freq + (warble_pos * 400 / 800 - 200);
      if(mod_freq < 100) mod_freq = 100;
      si_ufohit_cnt += mod_freq;
      if(si_ufohit_cnt >= 12000) {
        si_ufohit_cnt -= 12000;
        si_ufohit_toggle = -si_ufohit_toggle;
      }
      value += si_ufohit_toggle * 80;
      // Descend (~2000→300 over ~1.5s = 36000 samples)
      if(si_ufohit_freq > 300) si_ufohit_freq--;
    } else {
      si_ufohit_freq = 0;
      si_ufohit_warble = 0;
    }

    // Clamp
    if(value > 500) value = 500;
    if(value < -500) value = -500;

    valueToBuffer(i, value);
  }
}
#endif // ENABLE_SPACE


void Audio::galaxian_render_buffer(void) {
  // Galaxian discrete sound hardware emulation (MAME galaxian_a.cpp)
  // SOUND_CLOCK = 18.432MHz/6/2 = 1.536MHz
  //
  // soundregs[0]    = VCO pitch (8-bit, written at 0x7800)
  // soundregs[1-4]  = LFO DAC bits (4-bit, 0x6004-0x6007)
  // soundregs[8-10] = FS1/FS2/FS3 background tone enables (0x6800-0x6802)
  // soundregs[11]   = HIT noise enable (0x6803)
  // soundregs[12]   = (unused, offset 4 not wired)
  // soundregs[13]   = FIRE shoot enable (0x6805, offset 5)
  // soundregs[14]   = VOL1 (0x6806, offset 6)
  // soundregs[15]   = VOL2 (0x6807, offset 7)
  // NOTE: No BGEN register — VCO is always active when pitch is audible

  int vco_pitch = currentMachine->soundregs[0];

  // VOL1/VOL2 control VCO output volume via resistor network
  int vol1 = currentMachine->soundregs[14];  // offset 6
  int vol2 = currentMachine->soundregs[15];  // offset 7
  int vco_vol = (vol1 || vol2) ? (20 + vol1 * 20 + vol2 * 20) : 25;

  // VCO frequency: freq = 1.536MHz / (16*(256-pitch)) = 96000 / (256-pitch) Hz
  // At 24kHz sample rate: half_period = (256-pitch) / 8 samples
  // Use 8-bit fixed-point (×32) for sub-sample precision to avoid detuned notes
  unsigned long half_period_fp = (unsigned long)(256 - vco_pitch) * 32;  // fixed-point ×256 / 8

  // Detect pitch sweeps (credit sound): VCO plays through R34 base path
  // when pitch is actively changing even without VOL1/VOL2.
  // gal_last_pitch / gal_pitch_active sono ora membri di Audio (vedi audio.h)
  // azzerati in Audio::start() per evitare residual hum quando torni al menu.
  if(vco_pitch != gal_last_pitch) {
    gal_pitch_active = 500;  // sustain ~20ms (just over 1 frame)
    gal_last_pitch = vco_pitch;
  }
  if(gal_pitch_active > 0) gal_pitch_active--;

  // VCO plays when: VOL is on (normal sounds) OR pitch is sweeping (credit sound)
  bool vco_on = (half_period_fp > 32) && (vol1 || vol2 || gal_pitch_active > 0);

  // FS1/FS2/FS3: 555 timer tones (frequencies from RC values)
  // FS1 ~130Hz, FS2 ~170Hz, FS3 ~230Hz
  // Half-periods at 24kHz sample rate
  static const int fs_period[3] = {92, 71, 52};

  // LFO: 4-bit DAC controls background march speed
  int lfo_val = currentMachine->soundregs[1] |
                (currentMachine->soundregs[2] << 1) |
                (currentMachine->soundregs[3] << 2) |
                (currentMachine->soundregs[4] << 3);

  unsigned long lfo_period = lfo_val ? (24000 / (lfo_val + 1)) : 0;
  unsigned long lfo_on_time = lfo_period * 6 / 10;  // 60% duty cycle

  // FS background march sound disabled — needs tuning
  bool fs_any = false; // currentMachine->soundregs[8] || currentMachine->soundregs[9] || currentMachine->soundregs[10];

  for(int i = 0; i < 64; i++) {
    short value = 0;

    // === VCO tone ===
    if(vco_on) {
      gal_tone_cnt += 256;  // increment by 1.0 in 8-bit fixed-point
      if(gal_tone_cnt >= half_period_fp) {
        gal_tone_cnt -= half_period_fp;
        gal_tone_toggle = -gal_tone_toggle;
      }
      value += gal_tone_toggle * vco_vol;
    }

    // === FS1, FS2, FS3: background march tones pulsed by LFO ===
    if(fs_any) {
      if(lfo_val > 0 && lfo_period > 0) {
        gal_lfo_acc++;
        if(gal_lfo_acc >= lfo_period) gal_lfo_acc = 0;

        bool lfo_gate = (gal_lfo_acc < lfo_on_time);

        if(lfo_gate) {
          int sweep = (int)(gal_lfo_acc * 50 / lfo_on_time);

          for(int fs = 0; fs < 3; fs++) {
            if(currentMachine->soundregs[8 + fs]) {
              int mod_period = fs_period[fs] * (100 - sweep) / 100;
              if(mod_period < 4) mod_period = 4;

              gal_fs_cnt[fs]++;
              if(gal_fs_cnt[fs] >= (unsigned long)mod_period) {
                gal_fs_cnt[fs] = 0;
                gal_fs_toggle[fs] = -gal_fs_toggle[fs];
              }

              int env = (gal_lfo_acc < lfo_on_time / 4)
                ? (int)(gal_lfo_acc * 30 / (lfo_on_time / 4))
                : 30;
              value += gal_fs_toggle[fs] * env;
            }
          }
        }
      } else {
        // LFO=0: continuous FS tones
        for(int fs = 0; fs < 3; fs++) {
          if(currentMachine->soundregs[8 + fs]) {
            gal_fs_cnt[fs]++;
            if(gal_fs_cnt[fs] >= (unsigned long)fs_period[fs]) {
              gal_fs_cnt[fs] = 0;
              gal_fs_toggle[fs] = -gal_fs_toggle[fs];
            }
            value += gal_fs_toggle[fs] * 25;
          }
        }
      }
    }

    // === HIT: explosion noise (LFSR, bandpass ~470Hz) ===
    if(currentMachine->soundregs[11]) {
      uint32_t b = ((gal_noise_rng >> 0) ^ (gal_noise_rng >> 3)) & 1;
      gal_noise_rng = (gal_noise_rng >> 1) | (b << 16);
      value += ((gal_noise_rng & 1) ? 90 : -90);
    }

    // === FIRE: shooting sound (offset 5, 555 astable ~2.7kHz + noise) ===
    if(currentMachine->soundregs[13]) {
      gal_fire_cnt++;
      if(gal_fire_cnt >= 9) {
        gal_fire_cnt = 0;
        uint32_t b = ((gal_fire_rng >> 0) ^ (gal_fire_rng >> 3)) & 1;
        gal_fire_rng = (gal_fire_rng >> 1) | (b << 16);
      }
      value += ((gal_fire_rng & 1) ? 70 : -70);
    }

    valueToBuffer(i, value);
  }
}


#ifdef ENABLE_PHOENIX
// ============================================================================
// PHOENIX discrete audio @ 24 kHz mono
//
// Mappa registri (settati da phoenix.cpp wrZ80):
//   soundregs[0] = sound A latch (effect 2: shoot/wing)
//                  bit 0-3 = level/sweep, bit 4-5 = freq base
//   soundregs[1] = sound B latch (effect 1: noise + melody)
//                  bit 0-3 = noise level, bit 4 = noise freq sel,
//                  bit 5 = noise filter, bit 6-7 = melody tune (0..3)
//   soundregs[2] = trigger A (consumato qui, set su edge da phoenix.cpp)
//   soundregs[3] = trigger B (idem)
//   soundregs[4] = melody trigger (idem, su cambio bit 6-7)
//
// Synth:
//   Effect 2 = square wave 200/400/800/1600 Hz × envelope decay 0.4s
//   Effect 1 = LFSR pseudo-noise periodo 588..6913 Hz × env decay 0.3s
//              + LPF 1-pole se filt attivo (timbro shield)
//   Melody  = 4 tune brevi (4-8 note) come square wave + ottava
// ============================================================================
void Audio::phoenix_render_buffer(void) {
  // Phoenix audio synth Galaxian-style approssimato. Tutti i fix risolti
  // durante porting P4_NEW_multi + FULL28 sono integrati qui:
  //  - FIRE/WING/SWOOP con FREQ_TABLE 4 valori in base a freq_sel (bit 4-5)
  //  - BGM Romance d'Amor 96 step CON SUSTAIN (0=mantieni, 0xFFFF=stop) ONE-SHOT
  //  - Boom esplosione navetta (pattern data==15 + freq_sel==0, cooldown 5s)
  //  - HIT esplosioni nemici (env esteso 8000 sample, LPF heavy >> 2)

  // tune3 MAME tms36xx.cpp voce 1 +1 ottava — 0=sustain, 0xFFFF=stop one-shot
  static const uint16_t TUNE_ROMANCE[] = {
    440, 0, 440, 0, 440, 0,        // A4 A4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain lungo]
    587, 0, 523, 0, 466, 0,        // D5 C5 Bb4
    466, 0, 440, 0, 392, 0,        // Bb4 A4 G4
    392, 0, 440, 0, 466, 0,        // G4 A4 Bb4
    440, 0, 466, 0, 440, 0,        // A4 Bb4 A4
    554, 0, 466, 0, 440, 0,        // C#5 Bb4 A4
    440, 0, 392, 0, 349, 0,        // A4 G4 F4
    349, 0, 330, 0, 294, 0,        // F4 E4 D4
    330, 0, 330, 0, 330, 0,        // E4 E4 E4
    330, 0, 349, 0, 330, 0,        // E4 F4 E4
    294, 0, 349, 0, 440, 0,        // D4 F4 A4
    587, 0,   0, 0,   0, 0,        // D5 [sustain finale]
    0xFFFF
  };
  static const uint16_t TUNE_FUR_ELISE[] = {
    659, 622, 659, 622, 659, 494, 587, 523, 440, 0xFFFF
  };
  static const uint16_t TUNE_WARNING[] = {
    523, 784, 523, 784, 523, 784, 523, 784, 0xFFFF
  };
  static const uint16_t* TUNES[4] = {
    TUNE_ROMANCE, TUNE_WARNING, TUNE_FUR_ELISE, TUNE_ROMANCE
  };

  static uint8_t  last_a = 0, last_b = 0;
  static int16_t  fire_env = 0, hit_env = 0;
  static int16_t  fire_cnt = 0;
  static uint16_t fire_freq = 0;
  static int8_t   fire_tog = 1;
  static bool     ph_initialized = false;

  uint8_t a = currentMachine->soundregs[0];
  uint8_t b = currentMachine->soundregs[1];

  if (!ph_initialized) {
    last_a = a; last_b = b;
    fire_env = hit_env = 0;
    ph_mel_active = false;
    ph_initialized = true;
    for (int i = 0; i < 64; i++) valueToBuffer(i, 0);
    return;
  }

  // Cooldown anti-re-trigger boom esplosione
  if (ph_expl_cooldown > 0) ph_expl_cooldown--;

  // ── Effect 2 (sound A): FIRE/WING/SWOOP + esplosione navetta player ──
  if (a != last_a) {
    uint8_t new_lvl  = a & 0x0F;
    uint8_t freq_sel = (a >> 4) & 0x03;
    last_a = a;
    // Esplosione navetta player: pattern stretto data==15 + freq_sel==0
    if (new_lvl == 15 && freq_sel == 0 && ph_expl_cooldown == 0) {
      ph_expl_env      = 12000;
      ph_expl_cooldown = 1875;       // ~5 sec cooldown
      ph_expl_lpf1 = 0;
      ph_expl_lpf2 = 0;
    } else if (new_lvl) {
      // FIRE (freq alta) / WING / SWOOP (freq bassa) in base a freq_sel
      static const uint16_t FREQ_TABLE[4] = { 800, 1200, 1700, 2400 };
      fire_freq = FREQ_TABLE[freq_sel];
      fire_env  = 3000;
    }
  }

  // ── Effect 1 (sound B): HIT esplosioni nemici + tune select ──
  if (b != last_b) {
    uint8_t old = last_b;
    last_b = b;
    uint8_t new_data = b & 0x0F;
    if (new_data && hit_env < 1500) {
      hit_env = 8000;
    }
    uint8_t old_tune = (old >> 6) & 0x03;
    uint8_t new_tune = (b   >> 6) & 0x03;
    if (new_tune != old_tune && new_tune != 0) {
      ph_mel_tune   = new_tune;
      ph_mel_idx    = 0;
      ph_mel_timer  = 1;
      ph_mel_active = true;
    }
    if (new_tune == 0 && old_tune != 0) {
      ph_mel_active = false;
      ph_mel_freq   = 0;
    }
  }

  for (int i = 0; i < 64; i++) {
    int32_t value = 0;

    // BOOM esplosione navetta player (noise + doppio LPF + decay quadratico)
    if (ph_expl_env > 0) {
      uint32_t bit = ((gal_noise_rng >> 0) ^ (gal_noise_rng >> 3)) & 1;
      gal_noise_rng = (gal_noise_rng >> 1) | (bit << 16);
      int32_t noise = (gal_noise_rng & 1) ? 246 : -246;
      ph_expl_lpf1 += (noise - ph_expl_lpf1) >> 2;
      ph_expl_lpf2 += (ph_expl_lpf1 - ph_expl_lpf2) >> 2;
      int32_t env_q = ((int32_t)ph_expl_env * ph_expl_env) / 12000;
      value += (ph_expl_lpf2 * env_q) / 3000;
      ph_expl_env--;
    }

    // FIRE/WING: square + noise + sweep VCO graduale (-1 ogni 4 sample)
    if (fire_env > 0) {
      static uint8_t sweep_div = 0;
      if (++sweep_div >= 4) {
        sweep_div = 0;
        if (fire_freq > 200) fire_freq -= 1;
      }
      fire_cnt++;
      uint16_t half_period = 24000 / (fire_freq * 2);
      if (half_period < 2) half_period = 2;
      if (fire_cnt >= half_period) {
        fire_cnt = 0;
        fire_tog = -fire_tog;
      }
      uint32_t bit = ((ph_lfsr >> 0) ^ (ph_lfsr >> 3)) & 1;
      ph_lfsr = (ph_lfsr >> 1) | (bit << 16);
      int16_t ns = (ph_lfsr & 1) ? 50 : -50;
      int16_t sq = fire_tog * 100;
      int16_t mix = sq + (ns >> 1);
      value += (mix * (int32_t)fire_env) / 3000;
      fire_env--;
    }

    // HIT esplosioni nemici (LPF heavy + envelope decay 8000 sample)
    if (hit_env > 0) {
      uint32_t bit = ((gal_noise_rng >> 0) ^ (gal_noise_rng >> 3)) & 1;
      gal_noise_rng = (gal_noise_rng >> 1) | (bit << 16);
      int16_t noise = (gal_noise_rng & 1) ? 180 : -180;
      ph_n_lpf += (noise - ph_n_lpf) >> 2;
      value += (ph_n_lpf * (int32_t)hit_env) / 8000;
      hit_env--;
    }

    // BGM Romance d'Amor (one-shot, 0=sustain, 0xFFFF=stop)
    if (ph_mel_active) {
      if (--ph_mel_timer == 0) {
        const uint16_t* t = TUNES[ph_mel_tune & 0x03];
        uint16_t f = t[ph_mel_idx];
        if (f == 0xFFFF) {
          ph_mel_active = false;
          ph_mel_freq   = 0;
        } else {
          if (f != 0) ph_mel_freq = f;
          ph_mel_idx++;
          if (ph_mel_idx >= 100) ph_mel_idx = 99;
          ph_mel_timer = 24000 / 5;   // ~200 ms per step
        }
      }
      if (ph_mel_freq) {
        ph_mel_phase += (uint32_t)ph_mel_freq * 65536U / 24000U;
        int16_t m  = (ph_mel_phase & 0x8000) ? 28 : -28;
        int16_t m2 = ((ph_mel_phase << 1) & 0x8000) ? 14 : -14;
        value += m + m2;
      }
    }

    if (value > 32767)  value = 32767;
    if (value < -32768) value = -32768;
    valueToBuffer(i, (short)value);
  }
}
#endif // ENABLE_PHOENIX


void Audio::generateSinusWave(int32_t amplitude, short* buffer, uint16_t length) {
  for (int i=0; i<length; ++i) {
    buffer[i] = int32_t(float(amplitude) * sin(2.0 * PI * (1.0 / length) * i));
  }
}

void Audio::discrete_render_buffer() {
  unsigned short duration = currentMachine->soundregs[0] + (currentMachine->soundregs[1] << 8);

  if (duration > 0)
    duration--;

  currentMachine->soundregs[0] = duration & 0x00ff;
  currentMachine->soundregs[1] = (duration & 0xff00) > 8;

  float frequency;
  switch (currentMachine->soundregs[2]) {
    case 0x3: frequency = A5_3; break;
    case 0x4: frequency = C6_4; break;
    case 0x5: frequency = F5_5; break;
    case 0x6: frequency = G5_6; break;
    case 0x7: frequency = E6_7; break;
    case 0x8: frequency = B6_8; break;
    case 0xE: frequency = D6_E; break;
    case 0xF: frequency = B5_F; break;
    case 0xB: frequency = XX_B; break;
  }

  unsigned short pause = currentMachine->soundregs[3];
  if (pause > 0)
    currentMachine->soundregs[3]--;

  float delta = 0;
  if (duration != 0 && pause == 0)
    delta = (frequency * (sizeof(sinusWaveBuffer) / 2)) / float(24000);
  
  for(int i = 0; i < 64; i++) {
    uint16_t pos = uint32_t(((i + 1) * delta) + positionLast) % (sizeof(sinusWaveBuffer) / 2);
    short value = sinusWaveBuffer[pos];

    if (i == 63)
      positionLast = pos;

    valueToBuffer(i, value);
  }
}

// ============================================================================
// Jingle splash — riproduce il PCM embedded (splash_audio.h): "Game Start"
// 16-bit mono @ 32 kHz. Suona via transmit() (audio_task), 64 sample/buffer.
// mel_phase = posizione corrente nel PCM (in sample mono).
// ============================================================================
void Audio::startSplashMelody() {
  mel_note  = 0;
  mel_buf   = 0;
  mel_phase = 0;          // posizione di lettura nel PCM
  playMelody = true;
}

void Audio::melody_render_buffer(void) {
  uint32_t pos = mel_phase;
  if (pos >= (uint32_t)SPLASH_AUDIO_LEN) {
    for (int i = 0; i < 64; i++) { snd_buffer[2*i] = 0; snd_buffer[2*i+1] = 0; }
    playMelody = false;
    return;
  }
  // Volume utente: 0..30 (mute a >=30). Scala lineare senza clipping.
  int vol = (volumeSetting >= 30) ? 0 : (31 - volumeSetting);
  // Attenuazione dedicata al solo jingle splash (NON tocca i giochi).
  // 100 = pieno, 50 = meta'. Abbassa/alza questo valore per regolarlo.
  const int SPLASH_GAIN_PCT = 50;
  for (int i = 0; i < 64; i++) {
    int16_t s = 0;
    if (pos < (uint32_t)SPLASH_AUDIO_LEN) {
      s = (int16_t)(((int32_t)splash_audio[pos] * vol * SPLASH_GAIN_PCT) / (30 * 100));
      pos++;
    }
    snd_buffer[2*i]     = s;
    snd_buffer[2*i + 1] = s;
  }
  mel_phase = pos;
}

void Audio::testSine(int hz, int amplitude) {
  // Riempie il buffer I2S con sinusoide pura a `hz` Hz, ampiezza `amplitude`
  // (0..32767). Bypassa qualsiasi emulazione: utile per testare il DAC HW.
  static float phase = 0.0f;
  const float TWO_PI_F = 6.28318530718f;
  const float phase_inc = TWO_PI_F * (float)hz / 32000.0f;   // sample rate fisso

#ifdef SND_I2S_DIGITAL
  size_t bytesOut = 0;
  do {
    i2s_channel_write(i2s_tx_handle, snd_buffer, sizeof(snd_buffer), &bytesOut, 0);
    if (bytesOut) {
      for (int i = 0; i < 64; i++) {
        int16_t s = (int16_t)((float)amplitude * sinf(phase));
        phase += phase_inc;
        if (phase >= TWO_PI_F) phase -= TWO_PI_F;
        snd_buffer[2 * i]     = s;
        snd_buffer[2 * i + 1] = s;
      }
    }
  } while (bytesOut);
#endif
}

#ifdef ENABLE_FANTASY
// Fantasy audio (Rock-Ola snk6502_sound_device, MAME-fedele, da FULL33).
// 3 voci tone con waveform 16-sample (build_waveform in fantasy.cpp) lette da
// fantasy_snd_rom (PROGMEM) + bomb LFSR. music_clock 41.6Hz (advance 577 samp).
// VOCE: livello dal CABINATONE (toni x6, voce x56/256 = equilibrio confermato
// dall'utente). Qui i toni entrano x1 -> la stessa proporzione sarebbe ~9/256,
// ma il picco della voce (32768 -> ~1150) supererebbe il tetto pulito di
// valueToBuffer (+/-768 al volume di fabbrica 3) e raspa. 7/256 = picco ~900
// nei rari colpi pieni, ~2 dB sotto il cabinato. Se va ritoccata: SOLO questo.
#define FAN_GAIN_VOCE 7
void Audio::fantasy_render_buffer(void) {
  fantasy *fan = (fantasy*)currentMachine;

  // i comandi della voce dal 6502 (voce_w in fantasy.cpp)
  const int vcmd = fan->voce_cmd;
  if (vcmd >= 0) {
    if (vcmd == 99) { fan->voce_idx = -1; fan->voce_attiva = false; }
    else if (vcmd < FAN_VOCI) { fan->voce_idx = vcmd; fan->voce_pos = 0; fan->voce_attiva = true; }
    fan->voce_cmd = -1;
  }

  for (int v = 0; v < FAN_NUM_VOICES; v++) {
    unsigned int step = 0;
    if (!fan->voices[v].mute) {
      unsigned int idx = (fan->voices[v].base + fan->voices[v].offset) & 0x1FFF;
      if (idx < 6144) {
        unsigned char romdata = pgm_read_byte_near(fantasy_snd_rom + idx);
        if (romdata != 0xFF) {
          int divisor = 256 - (int)romdata;
          if (divisor > 0) step = 698880U / (unsigned int)divisor;
        }
      }
    }
    fan->voices[v].sample_step = step;
  }

  static int      fan_advance_counter = 0;
  static uint32_t fan_bomb_lfsr = 0x12345;

  for (int i = 0; i < 64; i++) {
    int sum = 0;
    for (int v = 0; v < FAN_NUM_VOICES; v++) {
      if (!fan->voices[v].mute && fan->voices[v].sample_step) {
        fan->voices[v].sample_cur += fan->voices[v].sample_step;
        unsigned int sc = fan->voices[v].sample_cur;
        int idx0 = (sc >> 16) & 15;
        int idx1 = (idx0 + 1) & 15;
        int frac = sc & 0xFFFF;
        int v0 = (int)fan->voices[v].form[idx0];
        int v1 = (int)fan->voices[v].form[idx1];
        int interp = (v0 * (65536 - frac) + v1 * frac) >> 16;
        sum += interp;
      }
    }
    if (fan->bomb_active > 0) {
      uint32_t b = ((fan_bomb_lfsr >> 0) ^ (fan_bomb_lfsr >> 3)) & 1;
      fan_bomb_lfsr = (fan_bomb_lfsr >> 1) | (b << 16);
      int decay = fan->bomb_active * 800 / 6000;
      sum += (fan_bomb_lfsr & 1) ? decay : -decay;
      fan->bomb_active--;
    }
    if (fan->voce_idx >= 0) {
      const uint32_t *t = fantasy_voce_tab[fan->voce_idx];
      if (fan->voce_pos < t[1]) {
        sum += (fantasy_voce[t[0] + fan->voce_pos] * FAN_GAIN_VOCE) >> 8;
        fan->voce_pos++;
      } else { fan->voce_idx = -1; fan->voce_attiva = false; }
    }
    if (sum >  30000) sum =  30000;
    if (sum < -30000) sum = -30000;
    valueToBuffer(i, (short)sum);
  }

  // MAME fantasy_sound_device music_clock = 41.6Hz -> advance offset a quel
  // rate. SPINNERINO gira a 32kHz -> 32000/41.6 = 769 sample (FULL33 usava 577
  // perche' era a 24kHz). Se la musica fosse ancora lenta/veloce, ritarare qui.
  fan_advance_counter += 64;
  if (fan_advance_counter >= 769) {
    fan_advance_counter -= 769;
    for (int v = 0; v < FAN_NUM_VOICES; v++) {
      if (!fan->voices[v].mute) {
        fan->voices[v].offset = (fan->voices[v].offset + 1) & fan->voices[v].mask;
        if (v == 0 && fan->voices[v].offset == 0 && fan->sound0_stop_on_rollover) {
          fan->voices[v].mute = true;
        }
      }
    }
  }
}
#endif

#ifdef ENABLE_NIBBLER
// Nibbler audio (Rock-Ola snk6502, MAME-fedele, da FULL33). 3 voci tone
// waveform + bomb LFSR + sample crunch. Advance musica 60Hz.
void Audio::nibbler_render_buffer(void) {
  nibbler *nib = (nibbler*)currentMachine;

  for (int v = 0; v < NIB_NUM_VOICES; v++) {
    unsigned int step = 0;
    if (!nib->voices[v].mute) {
      unsigned int idx = (nib->voices[v].base + nib->voices[v].offset) & 0x1FFF;
      if (idx < 6144) {
        unsigned char romdata = pgm_read_byte_near(nibbler_snd_rom + idx);
        if (romdata != 0xFF) {
          int divisor = 256 - (int)romdata;
          if (divisor > 0) step = 698880U / (unsigned int)divisor;
        }
      }
    }
    nib->voices[v].sample_step = step;
  }

  static int      advance_counter = 0;
  static uint32_t bomb_lfsr = 0x12345;

  for (int i = 0; i < 64; i++) {
    int sum = 0;
    for (int v = 0; v < NIB_NUM_VOICES; v++) {
      if (!nib->voices[v].mute && nib->voices[v].sample_step) {
        nib->voices[v].sample_cur += nib->voices[v].sample_step;
        unsigned int sc = nib->voices[v].sample_cur;
        int idx0 = (sc >> 16) & 15;
        int idx1 = (idx0 + 1) & 15;
        int frac = sc & 0xFFFF;
        int v0 = (int)nib->voices[v].form[idx0];
        int v1 = (int)nib->voices[v].form[idx1];
        int interp = (v0 * (65536 - frac) + v1 * frac) >> 16;
        sum += interp;
      }
    }
    if (nib->bomb_active > 0) {
      uint32_t b = ((bomb_lfsr >> 0) ^ (bomb_lfsr >> 3)) & 1;
      bomb_lfsr = (bomb_lfsr >> 1) | (b << 16);
      int decay = nib->bomb_active * 800 / 6000;
      sum += (bomb_lfsr & 1) ? decay : -decay;
      nib->bomb_active--;
    }
    if (nib->crunch_pos >= 0 && nib->crunch_pos < NIBBLER_CRUNCH_LEN) {
      signed char sb = (signed char)pgm_read_byte_near(nibbler_crunch + nib->crunch_pos);
      sum += (int)sb * 3;
      nib->crunch_pos++;
      if (nib->crunch_pos >= NIBBLER_CRUNCH_LEN) nib->crunch_pos = -1;
    }
    if (sum >  30000) sum =  30000;
    if (sum < -30000) sum = -30000;
    valueToBuffer(i, (short)sum);
  }

  // Advance offset 60Hz. SPINNERINO 32kHz -> 32000/60 = 533 (FULL33 usava 400
  // a 24kHz). Se la musica fosse lenta/veloce, ritarare qui.
  advance_counter += 64;
  if (advance_counter >= 533) {
    advance_counter -= 533;
    for (int v = 0; v < NIB_NUM_VOICES; v++) {
      if (!nib->voices[v].mute) {
        nib->voices[v].offset = (nib->voices[v].offset + 1) & nib->voices[v].mask;
        if (v == 0 && nib->voices[v].offset == 0 && nib->sound0_stop_on_rollover) {
          nib->voices[v].mute = true;
        }
      }
    }
  }
}
#endif

#ifdef ENABLE_BOBLBOBL
// Bubble Bobble: audio FEDELE generato sul core emulazione (YM2203 fm.c +
// YM3526 OPL + PSG SSG) in un ring buffer; qui (task audio) lo svuotiamo.
// Il ring contiene mix ~16-bit; lo scaliamo a ~±512 per valueToBuffer (che
// applica volume 128/volumeSetting). Lo shift e' un punto di calibrazione.
void Audio::boblbobl_render_buffer(void) {
  boblbobl *bb = (boblbobl*)currentMachine;
  int16_t buf[64];
  int got = bb->audioPop(buf, 64);
  for (int i = 0; i < 64; i++) {
    // ring (~16-bit FM+OPL+SSG) -> valueToBuffer (gain 128/vol; a vol=5 il fondo
    // scala pulito e' value~1280). Volume generale ALTO: >>3 porta i medi a ~1000
    // (loud, proporzionale) e il clamp ±1280 = fondo scala pulito a vol default,
    // saturando solo i picchi piu' forti. Piu' forte dell'originale (che clampava
    // tutto a 512) MA senza il bug di saturazione (clamp 512 = tutto a fondo scala).
    // Leggermente piu' basso (-12.5% vs >>3 puro): (buf>>3)-(buf>>6) = (buf>>3)*7/8.
    int v = (i < got) ? ((buf[i] >> 3) - (buf[i] >> 6)) : 0;
    if (v >  1120) v =  1120;
    if (v < -1120) v = -1120;
    valueToBuffer(i, (short)v);
  }
}
#endif

#ifdef ENABLE_GOINDOL
// Goindol: il core emulazione genera l'audio YM2203 (FM, +SSG in 3b) in un ring
// buffer; qui (task audio) lo svuotiamo. Stessa pipeline di Bubble Bobble.
// Lo shift e' il punto di calibrazione del volume.
void Audio::goindol_render_buffer(void) {
  Goindol *g = (Goindol*)currentMachine;
  int16_t buf[64];
  int got = g->audioPop(buf, 64);
  for (int i = 0; i < 64; i++) {
    // Segnale PROPORZIONALE e sotto il tetto, altrimenti satura e la regolazione
    // volume non ha range (era >>3 + clamp 512 = sempre a fondo scala). Vedi nota
    // in boblbobl_render_buffer. >>5 + clamp ±2000. Calibrazione loudness.
    int v = (i < got) ? (buf[i] >> 5) : 0;
    if (v >  2000) v =  2000;
    if (v < -2000) v = -2000;
    valueToBuffer(i, (short)v);
  }
}
#endif

#ifdef ENABLE_DKONG3
// Donkey Kong 3: i 2x RP2A03 (APU NES) generano i sample (±500) in un ring buffer
// dentro run_frame; qui (task audio) lo svuotiamo. Sample a basso livello ->
// upscaling. Il moltiplicatore e' il punto di calibrazione del volume DK3.
void Audio::dkong3_render_buffer(void) {
  dkong3 *d = (dkong3*)currentMachine;
  int16_t buf[64];
  int got = d->audioPop(buf, 64);
  for (int i = 0; i < 64; i++) {
    // I sample arrivano gia' a buona ampiezza (mixer non-lineare + DC-block,
    // ±1800). Pass-through; il moltiplicatore e' il punto di calibrazione volume.
    int v = (i < got) ? buf[i] : 0;
    if (v >  2000) v =  2000;
    if (v < -2000) v = -2000;
    valueToBuffer(i, (short)v);
  }
}
#endif

#ifdef ENABLE_SUPXEVS
// Vs. Super Xevious: l'APU NES riempie un ring buffer dentro run_frame (motore
// nes_vs); qui, sul task audio, lo svuotiamo.
// ⚠️ Il motore produce **536 campioni per quadro = 32 kHz**, cioe' il rate vero
//    dell'I2S di questo progetto (I2S_STD_CLK_DEFAULT_CONFIG(32000), riga 146).
//    Erano 400 (24 kHz), copiati da dkong3: mancava un quarto dell'audio.
void Audio::supxevs_render_buffer(void) {
  supxevs *s = (supxevs*)currentMachine;
  int16_t buf[64];
  int got = s->audioPop(buf, 64);

  // ⛔ MARGINE VERSO valueToBuffer. Quella funzione fa `value * 128 /
  // volumeSetting` e il volume di fabbrica e' **3**: il guadagno e' ×42,7,
  // quindi il tetto pulito e' ±768. Il mixer dell'APU arriva a **1152 di
  // picco** — cioe' i picchi venivano tagliati di netto, e un taglio squadrato
  // e' esattamente un suono che raspa. Con `>>1` il picco scende a 576, sotto
  // il tetto anche al volume massimo.
  // Il clamp resta come rete di sicurezza, non come normale funzionamento.
  static short ultimo = 0;
  for (int i = 0; i < 64; i++) {
    int v;
    if (i < got) {
      v = buf[i] >> 1;
      ultimo = (short)v;
    } else {
      // Ring vuoto: si tiene l'ultimo campione con decadimento dolce invece di
      // scrivere zero (uno zero in mezzo all'onda e' un gradino, cioe' un
      // click). Ora che il rate e' giusto dovrebbe capitare di rado: se capita
      // spesso, l'emulazione non sta producendo i campioni in tempo.
      v = ultimo;
      ultimo = (short)(ultimo - (ultimo >> 3));
    }
    if (v >  700) v =  700;
    if (v < -700) v = -700;
    valueToBuffer(i, (short)v);
  }
}
#endif

#ifdef ENABLE_TEMPEST
// Tempest: i due POKEY girano nell'emulazione, ciclo per ciclo con la CPU, e
// il mix (gia' centrato, passa-alto 20 Hz, scala ~±1100: tp_mix_drain in
// machines/tempest/tempest_core.h) arriva qui per ring buffer. Stessa forma di
// supxevs_render_buffer, compresa la tenuta dell'ultimo campione se la coda si
// svuota (uno zero in mezzo all'onda e' un click).
void Audio::tempest_render_buffer(void) {
  Tempest *t = (Tempest*)currentMachine;
  int16_t buf[64];
  int got = t->audioPop(buf, 64);
  static short ultimo = 0;
  for (int i = 0; i < 64; i++) {
    int v;
    if (i < got) { v = buf[i]; ultimo = (short)v; }
    else         { v = ultimo; ultimo = (short)(ultimo - (ultimo >> 3)); }
    valueToBuffer(i, (short)v);
  }
}
#endif

void Audio::valueToBuffer(int index, short value) {
    // Mute completo al volume minimo (volumeSetting >= 30)
    int32_t expanded;
    if (volumeSetting >= 30) {
        expanded = 0;
    } else {
        // value is now in the range of +/- 512, expand with higher gain for MAX98357A
        expanded = (int32_t)value * 128 / volumeSetting;
    }

    // Clamp to 16-bit signed range to prevent clipping distortion
    if (expanded > 32767) expanded = 32767;
    if (expanded < -32767) expanded = -32767;

#ifdef SND_I2S_DIGITAL
    // MAX98357A: signed 16-bit stereo (same sample on L+R)
    int16_t sample = (int16_t)expanded;
    snd_buffer[2 * index]     = sample;  // Left
    snd_buffer[2 * index + 1] = sample;  // Right
#elif defined(SND_DIFF)
    // generate differential output
    snd_buffer[2 * index]   = 0x8000 + (int16_t)expanded;
    snd_buffer[2 * index + 1] = 0x8000 - (int16_t)expanded;
#else
    // work-around weird byte order bug, see
    // https://github.com/espressif/arduino-esp32/issues/8467#issuecomment-1656616015
    snd_buffer[index ^ 1]   = 0x8000 + (int16_t)expanded;
#endif
}

#ifdef ENABLE_SBRKOUT
// ============================================================================
// Super Breakout - DAC 1-bit (porting da MAME scanline_callback):
//   dac_bit = (videoram[0x391] & (scanline >> 2)) != 0
// scanline counter va 0..261 a rate 60*262 = 15720 Hz.
// videoram[0x391] e' aggiornato da Sbrkout::wr6502 -> soundregs[0].
// Audio sample rate = 24000 Hz. Upsample fix-point: scanline avanza ~0.655/sample.
// ============================================================================
static uint32_t sbr_scanline_acc = 0;
static int      sbr_scanline_v   = 0;

void Audio::sbrkout_render_buffer(void) {
  uint8_t pwm_byte = currentMachine->soundregs[0];

  // Silenzio quando nessun tono attivo (byte == 0): evita sgracchio costante.
  if (pwm_byte == 0) {
    for (int i = 0; i < 64; i++) valueToBuffer(i, 0);
    return;
  }

  for (int i = 0; i < 64; i++) {
    sbr_scanline_acc += 15720;
    while (sbr_scanline_acc >= 24000) {
      sbr_scanline_acc -= 24000;
      sbr_scanline_v = (sbr_scanline_v + 1);
      if (sbr_scanline_v >= 262) sbr_scanline_v = 0;
    }
    uint8_t sc4 = (uint8_t)(sbr_scanline_v >> 2);
    int bit = ((pwm_byte & sc4) != 0) ? 1 : 0;
    // Amplitude ~200: DEVE stare nella zona lineare di valueToBuffer
    // (value*128/volumeSetting poi clamp 32767). Sopra ~512 il segnale
    // satura ai volumi tipici (vol<=3) -> loudness max identica a prescindere.
    // 200 = volume Super Breakout realmente piu' basso, senza clipping.
    short value = bit ? 200 : -200;
    valueToBuffer(i, value);
  }
}
#endif // ENABLE_SBRKOUT