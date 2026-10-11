// ============================================================================
// laserdisc.h — player video LASERDISC (Dragon's Lair) da microSD.
//
// FASE 2 del porting da CABINATONE (daphne_poc.cpp). Si arriva qui da
// LaserdiscMenu quando l'utente preme FIRE su un gioco disponibile.
//
// STEP 2a (questo): VIDEO-ONLY + BENCHMARK. Riproduce in loop il primo clip di
// /sdcard/dl misurando gli fps REALI, che si leggono in overlay sul video (sul
// P4 il Serial Monitor non funziona). Serve a chiudere il rischio n.1 del
// porting — la banda del bus SPI — con un numero misurato invece che stimato:
//   320x240x2 = 153600 B/frame; a 25 fps = 3,84 MB/s; SPI 80 MHz ~ 10 MB/s
//   teorici => ~38% del bus. Da verificare sul campo (SD + JPEG + blit).
// Niente audio, niente motore di gioco, niente framefile: quelli sono 2b/2c.
//
// Il video appare ruotato di 90 gradi ANTIORARI. NON si tocca l'orientamento
// del pannello: si usa la finestra del menu e si scrive nel FB il frame
// ruotato di 180 (vedi nota ORIENTAMENTO in laserdisc.cpp).
// ============================================================================
#pragma once

class Input;

// Riproduce il clip in loop finche' l'utente non tiene START 3s.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna true se ha riprodotto qualcosa, false se non e' partito (SD/JPEG/clip
// mancante): in quel caso ha gia' mostrato il motivo a schermo.
bool laserdisc_play(Input *input, unsigned short *out_strip);

// SPACE ACE = ROM-driven (Z80 + LD-V1000 + AY), player in spaceace.cpp.
// Stessa firma/uso di laserdisc_play: si arriva qui da LaserdiscMenu (FIRE su SPACE ACE).
bool spaceace_play(Input *input, unsigned short *out_strip);

// DRAGON'S LAIR II = ROM-driven pure, ma la scheda Leland e' un PC/XT: 8088
// emulato con libvxt + LDP-1450 su seriale. Player in dl2.cpp.
// FASE 1 (attuale): verifica del pack /sdcard/dl2 e uscita con START 3s.
bool dl2_play(Input *input, unsigned short *out_strip);
