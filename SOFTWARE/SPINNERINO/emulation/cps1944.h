#pragma once
// ============================================================================
// cps1944.h — 1944: The Loop Master (Capcom CPS-2, 1944u USA 000620).
//
// Porting da CABINATONE\cps1944.cpp. ⚠️ NON e' un `machineBase` come gli altri
// giochi della griglia GIOCHI ARCADE: e' un PLAYER AUTONOMO con task, video e
// audio propri — la stessa forma dei giochi laserdisc.
// Il motivo e' architetturale, non una scelta di comodo: gli altri giochi della
// griglia sono galagino-style e disegnano su richiesta una riga per volta
// (`render_row`), mentre un CPS-2 compone tilemap e sprite in un framebuffer.
// Riscriverlo dentro `render_row` sarebbe una RISCRITTURA, non un porting.
//
// ⛔ I DATI STANNO SU SD, NON IN FLASH — e non e' una preferenza:
//     gfx 20,00 MB + QSound 8,00 MB + programma 3,00 MB + Z80 0,31 MB
//   = 31,31 MB, contro una partizione APP che in TUTTO e' 30,00 MB (e ne ha
//   gia' 12,29 occupati dal firmware). Anche svuotandola del tutto ne
//   mancherebbero 1,31. Misurato il 2026-08-09, non stimato.
//   Vanno in `/sdcard/1944/`: p.bin, pdec.bin, gfx.bin, m.bin, q.bin.
//
// ⭐⭐ DUE immagini del programma, non una: il CPS-2 tiene separati lo spazio
//   OPCODE e quello DATI — il 68000 preleva le ISTRUZIONI dalla ROM decifrata
//   (`pdec.bin`) ma legge i DATI da quella cifrata (`p.bin`). Anche i vettori
//   di reset arrivano dallo spazio opcode.
// ============================================================================

class Input;

// Avvia il gioco. Bloccante: torna quando l'utente tiene START 3 s.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna false se non e' partito (SD o dati mancanti): in quel caso ha gia'
// mostrato a schermo il motivo.
bool cps1944_play(Input *input, unsigned short *out_strip);
