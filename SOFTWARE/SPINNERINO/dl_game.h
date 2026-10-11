#pragma once
// ============================================================================
// dl_game.h — Motore Dragon's Lair (macchina a stati, IDENTICO al cabinato).
//   Porting fedele della logica DirkSimple (dirksimple_lair.lua). Nessuna
//   emulazione ROM: scena -> riproduci clip -> nella finestra giusta leggi il
//   tasto -> scena successiva o morte. Dati in dl_scenes.h (generati dal file
//   ufficiale). Il motore chiama le callback video/suono qui sotto.
// ============================================================================
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// codici input e interrupt (usati anche da dl_scenes.h). NON duplicarli altrove.
enum { IN_NONE = 0, IN_UP, IN_DOWN, IN_LEFT, IN_RIGHT, IN_UPLEFT, IN_UPRIGHT,
       IN_DOWNLEFT, IN_DOWNRIGHT, IN_ACTION, IN_START };
enum { ITR_NONE = 0, ITR_GAME_OVER_COMPLETE, ITR_START_GAME };

// codici input (devono combaciare con dl_scenes.h: IN_UP..IN_START = 1..10)
typedef struct {
    uint16_t pressed;         // bit (1<<code) = tasto premuto ORA (fronte)
    uint16_t held;            // bit (1<<code) = tasto tenuto
    uint8_t  coinslot;        // 1 = COIN premuto ora (solo suono, credito e' esterno)
} dl_inputs_t;

// suoni (beep del cabinato)
enum { SND_COINSLOT = 0, SND_ACCEPT, SND_REJECT };

// --- callback che il player (daphne_poc.cpp) deve fornire ---
void dl_cb_start_clip(int32_t start_ms);   // cerca il frame per start_ms e RIPRODUCI
void dl_cb_show_still(int32_t start_ms);   // cerca il frame e mostra FERMO (pausa)
void dl_cb_play_sound(int snd);            // riproduci un beep (SND_*)

// --- API del motore ---
void dl_game_reset(void);                                  // stato iniziale (attract)
// total_ms = tempo monotono (ms); clip_ms = ms trascorsi dall'ultimo start_clip/show_still
void dl_game_tick(uint32_t total_ms, uint32_t clip_ms, const dl_inputs_t *in);

// stato per lo scoreboard / gestione credito
int  dl_game_score(void);       // punteggio corrente
int  dl_game_lives(void);       // vite (Dirk) rimaste
int  dl_game_in_attract(void);  // 1 = siamo in attract (nessuna partita in corso)
int  dl_game_gameover(void);    // 1 = partita finita (vite esaurite), attende moneta
const char *dl_game_scene_name(void);  // nome scena corrente (diagnostica)
int  dl_game_seq_ms(void);      // ms trascorsi nella sequenza corrente (diagnostica)
int  dl_game_move_hint(void);   // aiuto: codice IN_* della mossa consigliata ORA (0 = nessuna)
void dl_game_set_lives(int n);  // vite iniziali (NORMALE=5, FACILE=10). Chiamare prima di reset/tick

#ifdef __cplusplus
}
#endif
