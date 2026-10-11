#pragma once
// ============================================================================
// outrun.h — OUT RUN (Sega 1986, hardware "Sega Out Run", set `outrun` rev B).
//
// Porting da CABINATONE\outrun.cpp, dove il gioco e' COMPLETO e confermato
// funzionante sul device (video + input + audio Z80/YM2151/SegaPCM).
//
// ⚠️ NON e' un `machineBase` come gli altri giochi della griglia GIOCHI ARCADE:
// e' un PLAYER AUTONOMO con task, video e audio propri — la stessa forma di
// 1944 (cps1944.h) e di Ghouls (ghouls.h). Il motivo e' architetturale: gli
// altri giochi della griglia sono galagino-style e disegnano una riga alla
// volta su richiesta (`render_row`), mentre Out Run compone due tilemap, la
// strada, gli sprite scalati e il testo dentro un framebuffer. Riscriverlo
// dentro `render_row` sarebbe una RISCRITTURA, non un porting
// ([[feedback_porta_non_riprogettare]]).
//
// ⛔ I DATI STANNO SU SD, in `/sdcard/outrun/` — 7 file, 2,53 MB in tutto,
//    prodotti offline da romconv_outrun.py (gli STESSI byte di CABINATONE,
//    MD5 verificati 7/7):
//      main.bin    393.216  68000 principale, gia' interlacciato big-endian
//      sub.bin     393.216  68000 secondario (generatore della strada)
//      tiles.bin   196.608  3 bitplane da 0x10000 = 8192 tile 8x8
//      sprites.bin 1.048.576 ROM sprite, dword little-endian, 16 banchi
//      road.bin     65.536  due mappe strada 512x256 a 2bpp, piani separati
//      snd.bin      32.768  Z80 sonoro (epr-10187)
//      pcm.bin     524.288  campioni SegaPCM, 8 banchi da 64 KB
//
// Hardware: DUE 68000 @ 10 MHz (main + sub) · Z80 @ 4 MHz · YM2151 @ 4 MHz ·
// SegaPCM 315-5218 @ 4 MHz (16 canali). Video 320x224 a 60,0543 Hz.
// ⚠️ La LOGICA del gioco gira a 30 Hz (com'era sull'originale: i Sega Super
//    Scaler aggiornano la logica ogni due vblank): si disegna un frame ogni
//    due, mentre le CPU restano cadenzate a 60 vblank/s. Non e' un
//    compromesso, e' FEDELTA'.
// ============================================================================

class Input;

// Avvia il gioco. Bloccante: torna quando l'utente tiene START 3 s.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna false se non e' partito (SD o dati mancanti): in quel caso ha gia'
// mostrato a schermo il motivo.
bool outrun_play(Input *input, unsigned short *out_strip);
