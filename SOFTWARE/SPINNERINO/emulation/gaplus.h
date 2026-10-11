#pragma once
// ============================================================================
// gaplus.h — GAPLUS (Namco 1984, set `gaplus`) per SPINNERINO PLUS.
//
// Porting da CABINATONE\gaplus.cpp, dove il gioco e' stato verificato contro
// MAME (titolo a 0 pixel di scarto, a parte le stelle dello screenshot vecchio)
// e confermato dall'utente al primo flash. ⚠️ NON e' un `machineBase` come gli
// altri giochi della griglia GIOCHI ARCADE: e' un PLAYER AUTONOMO con task,
// video e audio propri, come 1944, Ghouls e Out Run — perche' cosi' e' il
// porting che gira ([[feedback_porta_non_riprogettare]]): compone un
// framebuffer, non disegna una riga alla volta.
//
// ⛔ I DATI STANNO SU SD, in `/sdcard/gaplus/` (182.252 byte), prodotti da
//    CONVERTITORI\romconv_gaplus.py — gli STESSI file del CABINATONE:
//      main.bin 24576 · sub.bin 24576 · sub2.bin 8192 · tiles.bin 8192
//      sprites.bin 49152 · cmap.bin 1668 · wave.bin 256 · stars.bin 2002
//      bang.bin ~63 KB
//
// Hardware: 3 x MC6809E @ 1,536 MHz, Namco 15XX + campione «bang»,
// 56XX/58XX + custom #3. Quadro 288x224 ROT90 (verticale).
// ============================================================================

class Input;

// Avvia il gioco. Bloccante: torna quando l'utente tiene START 3 s.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna false se il task non e' partito; se mancano i dati sulla SD lo dice a
// schermo e torna quando si esce.
bool gaplus_play(Input *input, unsigned short *out_strip);
