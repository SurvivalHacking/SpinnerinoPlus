#pragma once
// ============================================================================
// ghouls.h — Ghouls'n Ghosts (Capcom 1988, hardware CPS-1, set `ghouls`).
//
// Porting da CABINATONE\ghouls.cpp. ⚠️ NON e' un `machineBase` come gli altri
// giochi della griglia GIOCHI ARCADE: e' un PLAYER AUTONOMO con task, video e
// audio propri — la stessa forma di 1944 (cps1944.h) e dei giochi laserdisc.
// Il motivo e' architetturale, non una scelta di comodo: gli altri giochi della
// griglia sono galagino-style e disegnano su richiesta una riga per volta
// (`render_row`), mentre un CPS-1 compone tre tilemap e gli sprite in un
// framebuffer. Riscriverlo dentro `render_row` sarebbe una RISCRITTURA, non un
// porting ([[feedback_porta_non_riprogettare]]).
//
// ⛔ I DATI STANNO SU SD, in `/sdcard/ghouls/`:
//     code.bin  1.048.576  (68000, gia' interlacciato big-endian)
//     gfx.bin   3.145.728  (region grafica GREZZA, interlacciata)
//     snd.bin      98.304  (Z80 sonoro: 0x0000-0x7FFF + 2 banchi da 16 KB)
//   = 4,09 MB, prodotti offline da romconv_ghouls.py.
//
// ⚠️ NON e' come 1944, che li tiene su SD perche' 31 MB non ci starebbero mai:
//   qui 4 MB in flash ci starebbero. Restano su SD lo stesso per DUE motivi
//   misurati: (1) la partizione APP e' 30 MB e ne sono gia' occupati 12,6, ma
//   soprattutto (2) gli altri porting "da SD" di questo binario (1944, DL, Space
//   Ace, DL2) usano gia' quella strada e il converter produce gia' quei file.
//
// Hardware: 68000 @10 MHz + Z80 @3.579545 + YM2151. Niente OKI6295: il set
// `ghouls` non ha ROM di campioni.
// ============================================================================

class Input;

// Avvia il gioco. Bloccante: torna quando l'utente tiene START 3 s.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna false se non e' partito (SD o dati mancanti): in quel caso ha gia'
// mostrato a schermo il motivo.
bool ghouls_play(Input *input, unsigned short *out_strip);
