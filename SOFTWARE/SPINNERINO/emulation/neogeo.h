#pragma once
// ============================================================================
// neogeo.h — NEO GEO (SNK, 1990). Giochi: METAL SLUG (set `mslug`, NGH-201) e
// METAL SLUG 2 (set `mslug2`, NGH-241).
//
// Porting da CABINATONE\neogeo.cpp, dove il gioco e' COMPLETO e confermato
// funzionante sul device dall'utente ("gioco perfetto a livello grafico",
// "fps perfetti!!") con audio YM2610.
//
// ⚠️ NON e' un `machineBase`: e' un PLAYER AUTONOMO con task, video e audio
// propri — la stessa forma di 1944, Ghouls e Out Run. Il motivo e'
// architetturale: gli altri giochi della griglia sono galagino-style e
// disegnano una riga alla volta su richiesta (`render_row`), mentre il NeoGeo
// compone fix layer, sprite e backdrop dentro un framebuffer 352x256.
//
// ⛔ I DATI STANNO SU SD, in `/sdcard/neogeo/` — prodotti offline da
//    romconv_neogeo.py (gli STESSI byte di CABINATONE):
//      bios/sp.bin      131.072  BIOS di sistema MVS (il 68000 parte QUI)
//      bios/sfix.bin    131.072  fix layer del BIOS
//      bios/lo.bin      131.072  LUT dello zoom verticale
//      bios/sm1.bin     131.072  ROM del Z80 di sistema
//      mslug/p.bin    2.097.152  68000, con le DUE META' GIA' SCAMBIATE
//      mslug/m.bin      131.072  Z80 sonoro
//      mslug/s.bin      131.072  fix layer del gioco
//      mslug/v.bin    8.388.608  campioni ADPCM (YM2610)
//      mslug/c.ctile 16.777.216  sprite gia' convertiti in 4bpp packed
//      mslug/c.cusage    32.772  2 bit per tile: quali tile sono trasparenti
//    Totale 26,78 MB. In flash non ci starebbe nemmeno un decimo: la partizione
//    dell'app e' 30 MB e ne sono gia' occupati 13,4.
//
//    METAL SLUG 2 ha gli stessi sei file nella cartella `mslug2/`, ma p.bin e'
//    3 MB e c.ctile **32 MB**: 43,31 MB in tutto, che nei 32 MB di PSRAM non
//    entrano. Gli sprite restano quindi sulla SD e li legge `gfx_cache` a
//    banchi da 4 KB (`spr_cache` nella tabella NG_GAMES) — lo stesso meccanismo
//    gia' confermato sul device da 1944.
//    ⭐ In piu' vuole `mslug2/sram.bin` (65.536 byte, backup RAM gia' "vissuta"):
//    senza, scatta la protezione SNK e il gioco si ferma sulla schermata
//    "ORIGINAL SNK BOARDS ONLY". Il file e' OPZIONALE per il firmware (chi non
//    ce l'ha parte azzerato), ma per Metal Slug 2 e' obbligatorio.
//
// Hardware: 68000 @ 12 MHz · Z80 @ 4 MHz · YM2610 (FM 4ch + SSG 3ch + ADPCM-A
// 6ch + ADPCM-B) · video 320x224 a 59,19 Hz.
// ============================================================================

class Input;

// Avvia un gioco NEO GEO. Bloccante: torna quando l'utente tiene START 3 s.
// `set` = nome della cartella sulla SD e riga della tabella NG_GAMES dentro
// neogeo.cpp ("mslug", "mslug2", …).
// ⭐ Si passa il NOME e non un indice di proposito: la tabella del menu e quella
// dell'emulatore sono due elenchi distinti e un indice li accoppierebbe in
// silenzio — lo stesso disallineamento che la griglia GIOCHI ARCADE paga con
// tre liste parallele da tenere in ordine.
// out_strip = la strip di rendering del menu (TFT_FB_W*8), riusata dal player.
// Ritorna false se non e' nemmeno partito (set sconosciuto o task non creato).
bool neogeo_play(Input *input, unsigned short *out_strip, const char *set);
