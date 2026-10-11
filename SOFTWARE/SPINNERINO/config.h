#ifndef _CONFIG_H_
#define _CONFIG_H_

// ============================================================================
// SPINNERINO P4 - ESP32-P4-WIFI6 (Waveshare) standalone arcade port
// Hardware:
//   ESP32-P4 dual-core RISC-V 360MHz + ESP32-C6 onboard
//   32MB PSRAM in-package, 32MB Flash
//   ST7789 SPI 2.4" 320x240 (landscape, montato fisicamente in portrait)
//   ES8311 + NS4150B audio codec/amp onboard (I2C+I2S)
//   microSD SDIO 3.0 (slot 0, usata dai giochi LASERDISC; ROM arcade in flash .h)
//   Encoder rotativo EC11 + COIN + START fisici
//   ROM: arkangc (bootleg Game Corp, NO MCU 68705) compilata in flash
// ============================================================================

#ifndef ARDUINO_RUNNING_CORE
  #define ARDUINO_RUNNING_CORE 0
#endif

#ifndef TFT_SPICLK
  #define TFT_SPICLK 80000000      // 80 MHz: ST7789 stabile su P4 con HSPI
#endif

// ============================================================================
// BOARD: Waveshare ESP32-P4-WIFI6 (header 40-pin, ROM in flash)
// ============================================================================
#define SPINNERINO_P4_BOARD

// --- ST7789 SPI 320x240 landscape (cablato sul GPIO header 40-pin) ---
#define TFT_MOSI        21
#define TFT_SCLK        20
#define TFT_CS          22
#define TFT_DC          23
#define TFT_RST         26
#define TFT_BL          27       // backlight PWM (LEDC), active HIGH
#define TFT_BL_LEVEL    HIGH

// Pannello ST7789 (no ILI9341)
//#define USE_ILI9341

// MADCTL landscape MV+MX = 0x60 (display fisico portrait, sw rotation)
#ifdef USE_ILI9341
  #define TFT_MAC       0x28
#else
  #define TFT_MAC       0x60
#endif
// ATTENZIONE: il display e' montato in PORTRAIT fisicamente, con MAC=0x60 (MV).
// Quindi gli assi RENDERING e DISPLAY FISICO sono SCAMBIATI.
// Su molti pannelli generic, INVON inverte i colori. Definisci TFT_INVERT
// solo per pannelli IPS che lo richiedono.
//#define TFT_INVERT

#define TFT_HW_X             0
#define TFT_HW_Y             8       // shift HW finale
#define TFT_FB_W             320
#define TFT_FB_H             232     // 240 - TFT_HW_Y
#define TFT_X_OFFSET         32      // padding alto/basso display fisico
#define TFT_Y_OFFSET         0       // 0 = max dx (centrato con MX mirror)
#define TFT_PANEL_X_OFFSET   0
#define TFT_PANEL_Y_OFFSET   0
#define TFT_PANEL_OFFSET     0
#define TFT_SCREEN_W         320
#define TFT_SCREEN_H         240

// --- Giochi LANDSCAPE (es. Green Beret 240-wide) resi come FULL33 ---
// FULL33 mostra Green Beret tenendo il pannello in PORTRAIT (MADCTL senza MV) e
// aprendo una finestra larga 240: il gioco riempie tutta la larghezza fisica del
// pannello, a piena risoluzione, senza schiacciare. SPINNERINO normalmente gira
// trasposto (MADCTL 0x60); quando parte un gioco isLandscape() commutiamo il
// pannello in portrait con questo MADCTL, poi ripristiniamo 0x60 all'uscita.
//   Le rotazioni PORTRAIT valide ST7789 sono 0x00 (0deg) e 0xC0 (180deg, =MX+MY).
//   0x40 (solo MX) e' una RIFLESSIONE -> specchia in orizzontale (game_x). 0x00
//   toglie lo specchio mantenendo il verticale (confermato on-device 2026-06-18).
//   KNOB: se capovolto verticalmente, provare 0xC0; se di nuovo specchiato, 0x40/0x80.
#define GB_PORTRAIT_MADCTL   0x00
#define GB_PORTRAIT_X        0       // offset X della finestra 240 nel pannello 240
#define GB_PORTRAIT_Y        16      // (320-288)/2 = 16, centra i 288 px verticali

// --- Audio ES8311 + NS4150B amp (onboard) ---
//   Pin DIVERSI da MAX98357A: I2S MCLK richiesto, PA enable separato
#define I2C_SDA          7        // I2C ES8311
#define I2C_SCL          8
#define ES8311_I2C_ADDR  0x18
#define I2S_MCLK         13       // master clock (richiesto da ES8311 in slave)
#define I2S_BCLK         12       // SCLK
#define I2S_LRC          10       // LRCK / WS
#define I2S_DOUT          9       // ESP32 TX -> codec DSDIN
#define ES8311_PA_PIN    53       // amplifier enable (HIGH = ON)
#define SND_I2S_DIGITAL           // segnala driver digitale (no DAC interno)

// --- Input: EC11 encoder + COIN + START (header 40-pin) ---
#define USE_EC11_PADDLE
#define EC11_PIN_A       32       // quadrature A (ISR)
#define EC11_PIN_B       33       // quadrature B
#define EC11_PIN_SW      48       // FIRE / lancio palla / SELECT in menu
// ⛔⛔ RIGA MORTA, NON CABLARE NIENTE QUI. `BTN_COIN` non ha nessun `pinMode`
// (quindi niente INPUT_PULLUP) e NESSUNO lo legge: l'unico `digitalRead` che
// gli somiglia, in `scramble.cpp`, usa il nome DIVERSO `BTN_COIN_PIN`, che non
// e' definito da nessuna parte -> ramo morto. Un pulsante saldato sul 46 non
// farebbe niente. Il gettone lo mette la sequenza automatica di START (47), il
// tasto select del pad BLE, e in Out Run l'encoder (48).
// ⚠️ Ci sono cascato scrivendo la guida ai collegamenti: ho preso il numero da
// qui senza cercare CHI LEGGE — [[feedback_tasto_esiste_ma_non_arriva]].
#define BTN_COIN         46       // (non usato: vedi sopra)
#define BTN_START        47       // START (sequenza auto-coin come sorgente)

#define EC11_PADDLE_STEP 4
#define AUTO_COIN_FRAMES 30       // ~0.5s a 60 fps

// --- Joystick digitale 4 direzioni + 2 pulsanti A/B (header 40-pin) ---
// Controller fisico AGGIUNTIVO, funziona IN PARALLELO a EC11/START e al
// gamepad BT (i bit vengono messi in OR, nessuno esclude l'altro).
// Tutti ATTIVO-BASSO: pin -> GND quando premuto, pull-up interno (INPUT_PULLUP).
//   A -> BUTTON_FIRE (sparo principale)     B -> BUTTON_EXTRA (2o tasto)
//   LEFT/RIGHT muovono anche il paddle/dial (giochi a racchetta del carosello).
#define USE_DIGITAL_JOYSTICK
#define JOY_PIN_UP       5
#define JOY_PIN_DOWN     49
#define JOY_PIN_LEFT     50
#define JOY_PIN_RIGHT    28
#define BTN_A_PIN        2        // -> BUTTON_FIRE
#define BTN_B_PIN        4        // -> BUTTON_EXTRA

// --- Bluetooth via ESP32-C6 companion (UART bridge) ---
// Il C6 onboard esegue il firmware in C6_BLE_Bridge/ (NimBLE HID host) e
// invia al P4 pacchetti gamepad [0xAA][buttons][flags][xor] @115200 8N1.
// Cablaggio: C6 U0TXD (TX) --ponticello--> P4 GPIO3 (RX). Solo ricezione.
// Per il solo gamepad BT serve un filo solo (C6-TX -> GPIO3).
#define UART_BRIDGE_INPUT
#define UART_BRIDGE_RX_PIN  3       // P4 RX <- C6 U0TXD (ponticello)
#define UART_BRIDGE_TX_PIN  -1      // il P4 solo riceve dal C6 (no TX)
#define UART_BRIDGE_BAUD    115200

// ============================================================================
// microSD (SDMMC 4-bit) — richiesta dai giochi LASERDISC (video/audio da SD)
// ============================================================================
// Waveshare ESP32-P4-WIFI6: la microSD e' cablata sui pin IO-MUX dello SLOT 0.
// Il C6 onboard (ESP-Hosted) userebbe lo SLOT 1: i due slot sono INDIPENDENTI e
// l'accesso parallelo e' supportato (esp-hosted Kconfig: "Slot 0 connects to the
// MicroSD Card slot / Slot 1 connects to the on-board ESP32-C6"). Qui il C6 e'
// comunque solo un bridge UART (vedi sopra), quindi nessun driver tocca l'SDIO.
//
// ⚠️ TRAPPOLA: SDMMC_HOST_DEFAULT() ha .slot = SDMMC_HOST_SLOT_1. VA FORZATO a
// SDMMC_HOST_SLOT_0, altrimenti la SD finisce sui pin del C6 e non monta.
// I pin sotto sono IO-MUX fissi dello slot 0: non sono rimappabili.
#define USE_SDCARD
#define SD_SLOT           0        // SDMMC_HOST_SLOT_0 (IO-MUX, NON il default!)
#define SD_PIN_CLK        43
#define SD_PIN_CMD        44
#define SD_PIN_D0         39
#define SD_PIN_D1         40
#define SD_PIN_D2         41
#define SD_PIN_D3         42
#define SD_BUS_WIDTH      4
#define SD_LDO_CHANNEL    4        // alimentazione via LDO on-chip (ESP_LDO_VO4)
#define SD_MOUNT_POINT    "/sdcard"
#define SD_MAX_FILES      12       // accoppiato alle cache LRU handle del player
#define SD_FREQ_KHZ       SDMMC_FREQ_HIGHSPEED   // 40 MHz

// LED non disponibile (board Waveshare non ha LED utente standard)
// #define LED_PIN

// ============================================================================
// GAME SELECTION
// ============================================================================
#define ENABLE_ARKANOID
#define ENABLE_TEMPEST       // Tempest (Atari 1981, set tempest3): 6502 + AVG vettoriale + 2 POKEY + manopola.
                             // Motore nuovo da MAME (tempest_core.h + avg.cpp), provato sul banco _verify_gfx/tempest
#define ENABLE_ARKANOID2     // attivato: 2x Z80 + YM2203 + Seta X1-001
#define ENABLE_GIGAS      // disabilitato: bloccato in self-test (non risolvibile senza disassembler ROM)
#define ENABLE_GIGAS2        // Gigas Mark II (gigasm2b bootleg) — clone HW identico a gigasb
#define ENABLE_GOINDOL       // Goindol (SunA 1987: 2x Z80 + YM2203, dial/spinner) — gemello HW di gigas
#define ENABLE_PHOENIX       // Phoenix (Amstar/Centuri 1980: Z80 + 2 layer tilemap, audio TMS3617 NON emulato)
#define ENABLE_SPACE         // Space Invaders (8080 su Z80, bitmap 224x256, sample WAV)
#define ENABLE_GALAGA        // Galaga (3x Z80 + Namco WSG + sample boom + starfield)
#define ENABLE_MOTORACE      // MotoRace USA (Z80 + M6803 + 2x AY-3-8910, scroll BG)
#define ENABLE_SBRKOUT       // Super Breakout (Atari 1978) — 6502 + mono + paddle
#define ENABLE_GALAXIAN      // Galaxian (Namco/Midway 1979: Z80 + tile/sprite + starfield + audio discreto)
#define ENABLE_BOMBBEE       // Bomb Bee (Namco 1979: 8080 su Z80 + paddle) — da SPINNERINO ORIGINALE
#define ENABLE_ROADFIGHTER   // Road Fighter (Konami 1984: M6809 KONAMI-1 + Z80 audio, sterzo) — da SPINNERINO ORIGINALE
#define ENABLE_PANG          // Pang / Buster Bros (Mitchell 1989: Z80 + KABUKI + OKI6295 + YM2413, paddle) — da SPINNERINO ORIGINALE
// --- Giochi JOYSTICK (menu griglia "GIOCHI ARCADE", non nel carosello paddle) ---
#define ENABLE_XEVIOUS       // Xevious (Namco 1982: 3x Z80 + Namco WSG/06xx-51xx) — portato da GALAGONE 2.8VPE
#define ENABLE_GYRUSS        // Gyruss (Konami 1983: Z80 main + M6809 sub + Z80 audio + 5x AY) — da GALAGONE 2.8VPE
#define ENABLE_PACMAN        // Pac-Man (Namco 1980: Z80 + Namco WSG) — da CONSOLE_QUADRA_NANO
#define ENABLE_1942          // 1942 (Capcom 1984: Z80 main + Z80 audio + 2x AY-3-8910) — da CONSOLE_QUADRA_NANO
#define ENABLE_PBACTION      // Pinball Action (Tehkan 1985: Z80 main + Z80 audio + 3x AY + CTC) — da CONSOLE_QUADRA_NANO
#define ENABLE_BOMBJACK      // Bomb Jack (Tehkan 1984: Z80 main + Z80 audio + 3x AY) — da FULL33
// --- Nuovi porting da FULL33 (griglia GIOCHI ARCADE) 2026-06-17 ---
#define ENABLE_DKONGJR       // Donkey Kong Jr (Nintendo 1982: Z80 + i8048 sound + samples) — base dkong
#define ENABLE_FROGGER       // Frogger (Konami/Sega 1981: 2x Z80 + AY) — verticale, scroll corsie
#define ENABLE_GREENBERET    // Green Beret (Konami 1985: Z80 + SN76489) — orizzontale, scroll k005849
#define ENABLE_LADYBUG       // Lady Bug (Universal 1981: Z80 + SN76489) — verticale
#define ENABLE_MOONPATROL    // Moon Patrol (Irem 1982: Z80 + M6803 + 2x AY) — orizzontale, parallax
#define ENABLE_MARIOBROS     // Mario Bros (Nintendo 1983: Z80 + i8048 + DAC) — orizzontale
#define ENABLE_MSPACMAN      // Ms. Pac-Man (Namco/Midway 1981: Z80, decode switching) — base pacman, GFX da FULL33
#define ENABLE_COMMANDO      // Commando (Capcom 1985: 2x Z80 + 2x YM2203) — verticale, GFX flash da PAOLO P4
#define ENABLE_DKONG         // Donkey Kong (Nintendo 1981: Z80 + i8048 + samples) — classe base già nel target
#define ENABLE_BRUBBER       // Burnin' Rubber (Data East 1982: 2x 6502 DECO + 2x AY) — verticale, da CONSOLE_QUADRA_NANO
#define ENABLE_SCRAMBLE      // Scramble (Konami 1981: 2x Z80 + 2x AY + stars) — galagino-rotated, da CONSOLE_QUADRA_NANO
#define ENABLE_SUPERCOBRA    // Super Cobra (Konami 1981: 2x Z80 + 2x AY + stars + scroll) — eredita da scramble, da galagino-main
#define ENABLE_DKONG3        // Donkey Kong 3 (Nintendo 1983: Z80 + 2x RP2A03 NES) — da galagino-main. FASE 1: video (audio NES in fase 2)
#define ENABLE_POPEYE        // Popeye (Nintendo 1982: Z80 + AY) — landscape 512->240, già nel target
#define ENABLE_FANTASY       // Fantasy (Rock-Ola/SNK 1981: 6502 + snk6502 waveform) — verticale, da FULL33
#define ENABLE_NIBBLER       // Nibbler (Rock-Ola/SNK 1982: 6502 + snk6502 + crunch) — verticale, da FULL33
#define ENABLE_MOONCRESTA    // Moon Cresta (Nichibutsu 1980: Z80 + tile/sprite + starfield + gfx bank) — HW Galaxian, dati+logica da PAOLO 3.5
#define ENABLE_TIMEPLT       // Time Pilot (Konami 1982: Z80 main + Z80 sound + 2x AY) — portrait, sprite multiplex, da FULL33
#define ENABLE_GNG           // Ghosts'n Goblins (Capcom 1985: M6809 + Z80 + 2x YM2203) — landscape, da GALAGONE 3.5 S3 (impl. corretta)
#define ENABLE_BOBLBOBL      // Bubble Bobble (Taito 1986: 3x Z80 + YM2203 + YM3526) — bootleg boblbobl senza MCU, landscape
#define ENABLE_GAPLUS        // Gaplus (Namco 1984: 3x M6809 + 15XX + 56XX/58XX) — player AUTONOMO da CABINATONE, dati su SD /gaplus/, ultima cella di GIOCHI ARCADE
#define ENABLE_SUPXEVS       // Vs. Super Xevious (Namco 1986) — ⚠️ NON e' l'Xevious Namco: e' un Nintendo VS. UniSystem (RP2A03 + PPU RP2C04), cioe' un NES da sala giochi. Landscape 256x240, motore in machines/supxevs/nes_vs.cpp
// #define SINGLE_MACHINE    // multi-machine via menu

// Config attract
#define MASTER_ATTRACT_MENU_TIMEOUT  40000      // 40 s prima dell'auto-avvio dal menu (come SPINNERINO ORIGINALE)
#define MASTER_ATTRACT_GAME_TIMEOUT  60000 * 3  // 3 min prima del ritorno al menu (come SPINNERINO ORIGINALE)

// Video full frame rate (P4 + 80 MHz SPI)
// #define VIDEO_HALF_RATE

// ============================================================================
// ⭐⭐⭐ LIBERA_BUFFER_ALL_USCITA — la RAM interna torna indietro a OGNI uscita
// ============================================================================
// Referto: *«ogni volta che si apre un gioco e si chiude e si passa al
// successivo si accumula buffer»*. Vero, e per due volte l'ho curato UN GIOCO
// ALLA VOLTA: prima PANG («l'unico allocatore enorme»), poi ARKANOID 2, ogni
// volta riscrivendo la stessa riga con un nome diverso. Il conto vero, fatto
// gioco per gioco, dice che di allocatori enormi ce n'erano SETTE:
//
//     PHOENIX        264 KB   (8 cache: 2 bitmap BG da 104 KB + rom + tile)
//     ARKANOID 2     153 KB   gia' curato
//     PANG            96 KB   gia' curato
//     BUBBLE BOBBLE   92 KB   (indexfb 64K + vram 8K + share1 6K + audio)
//     SPACE ACE       81 KB   (ace_z80mem 64K + apf 16K + audmix 1K)
//     TEMPEST         28 KB   (cache ROM CPU + vector)
//     GOINDOL          6 KB   ·  DRAGON'S LAIR  5 KB
//     ------------------------------------------------------------
//     ~475 KB su ~768 KB di RAM interna totale della scheda.
//
// ⛔ I giochi da SD (OUT RUN, 1944, GHOULS, NEO GEO, DL2) NON c'entrano: hanno
// tutti la loro `or_free()`/`ng_cleanup()`/`gh_free()` all'uscita e restituiscono
// tutto. Sono i giochi in FLASH del carosello a non restituire mai niente.
//
// ⛔⛔⛔ CORREZIONE (2026-09-05): la riga qui sopra ERA FALSA per 26.624 byte.
// La `tl_tab` del YM2151 (OUT RUN e GHOULS) restava in RAM INTERNA per sempre:
// `ym2151_shutdown()` liberava solo la struct del chip. Non era una svista: era
// un'eccezione scritta come VOLUTA in tre commenti — e nessuno l'ha mai
// confrontata con questo conto, che diceva l'opposto.
// Sintomo, arrivato dall'utente: «se avvio prima OUT RUN, i due METAL SLUG si
// rallentano, audio e video». Il framebuffer del Neo Geo (176 KB) va in RAM
// interna solo sopra una soglia: 26 KB in meno lo fanno ripiegare in PSRAM IN
// SILENZIO. Curato in `cpus/ym2151/ym2151.c` (`YM2151_LIBERA_TL_TAB`).
// 👉 Un conto delle risorse va confrontato con le ECCEZIONI scritte nei singoli
//    file: se un commento dice «questo lo teniamo apposta», il conto e' sbagliato
//    finche' non lo dice anche lui.
//
// ⭐ La cura non e' un altro `if (currentMachine == &qualcosa)`: e' un GANCIO
// nella classe base (`machineBase::freeBuffers()`) chiamato per TUTTI in
// `return_to_menu()`, l'uscita di ogni gioco. Chi non ha niente da restituire
// eredita il no-op e non se ne accorge ([[feedback_costante_del_primo_caso]]:
// la lista dei casi speciali che si allunga di uno per volta E' il difetto).
//
// ⚠️ INTERRUTTORE per bisezionare: mettere 0 e ricompilare rimette il
// comportamento di prima (solo PANG e ARKANOID 2 liberati a mano).
#define LIBERA_BUFFER_ALL_USCITA 1

#endif // _CONFIG_H_
