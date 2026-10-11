// ============================================================================
// dl_game.cpp — Motore Dragon's Lair. Porting fedele di dirksimple_lair.lua.
//   (setup/start_game/choose_next_scene/kill_player/game_over/start_sequence/
//    check_actions/check_timeout/tick). Dati: dl_scenes.h (generati).
// ============================================================================
#include <string.h>
#include "dl_game.h"
#include "dl_scenes.h"

// ------- cvar (come DirkSimple, default) -------
static int        g_starting_lives = 5;   // impostabile: NORMALE=5, FACILE=10
static const bool CVAR_INFINITE_LIVES = false;
static const bool CVAR_GOD_MODE = false;

// ------- stato scene_manager -------
static bool     sm_init = false;
static bool     sm_infinite_lives = false;
static int      lives_left = 5;
static int      current_score = 0;
static int32_t  last_seek = 0;
static bool     completed_intro = false;
static int      cur_scene = -1;         // indice in DL_SCENES (-1 = nessuna)
static int      cur_seq   = -1;         // indice sequenza nella scena
static int32_t  seq_tick_offset = 0;
static int32_t  unserialize_offset = 0;
static int32_t  cur_seq_ticks = 0;
static int      current_row = 1;        // 1-based (come lua)
static int      current_cycle = 1;      // 1-based
static bool     chosen[16][3];
static int      total_failed = 0;
static const char *failed[8];
static bool     rerunning_failures = false;

static const dl_action_t *accepted = 0; // azione accettata (NULL = nessuna)
static bool     sm_game_over = false;   // true = partita finita (attende moneta in attract)

static uint32_t         cur_ticks = 0;
static const dl_inputs_t *cur_in = 0;
static uint32_t         sound_cooldown = 0;

static const int N_COLS = 3;            // ogni riga ha 3 scene

// ---------------------------------------------------------------- util
static int scene_index(const char *name)
{
    for (int i = 0; i < DL_N_SCENES; i++)
        if (strcmp(DL_SCENES[i].name, name) == 0) return i;
    return -1;
}
static int seq_index(int scene, const char *name)
{
    if (scene < 0) return -1;
    const dl_scene_t *S = &DL_SCENES[scene];
    for (int i = 0; i < S->n_seqs; i++)
        if (strcmp(S->seqs[i].name, name) == 0) return i;
    return -1;
}
static const dl_sequence_t *cur_sequence(void)
{
    if (cur_scene < 0 || cur_seq < 0) return 0;
    return &DL_SCENES[cur_scene].seqs[cur_seq];
}
static const char *cur_scene_name(void)
{
    return (cur_scene >= 0) ? DL_SCENES[cur_scene].name : "";
}
static bool input_pressed(int code) { return cur_in && (cur_in->pressed & (uint16_t)(1u << code)); }

static void play_sound(int snd)
{
    if (sound_cooldown < cur_ticks) {         // max un suono ogni 200ms
        sound_cooldown = cur_ticks + 200;
        dl_cb_play_sound(snd);
    }
}

// ---------------------------------------------------------------- forward
static void start_sequence(int seq_idx);
static void start_scene(int scene_idx, bool is_resurrection);
static void choose_next_scene(bool is_resurrection);
static void kill_player(void);
static void check_timeout(void);

static void start_attract_mode(bool after_losing)
{
    start_scene(scene_index("attract_mode"), after_losing);
}

static void game_over_complete(void) { start_attract_mode(true); }

static void game_over(bool won)
{
    if (!won) sm_game_over = true;         // vite finite: mostra GAME OVER in attract
    if (won) {
        start_attract_mode(false);
    } else {
        int go = seq_index(cur_scene, "game_over");
        if (go >= 0) start_sequence(go);
        else         start_attract_mode(true);
    }
}

// ---------------------------------------------------------------- setup/start
static void setup_scene_manager(void)
{
    sm_init = true;
    sm_infinite_lives = false;
    lives_left = g_starting_lives;
    current_score = 0;
    last_seek = 0;
    completed_intro = false;
    cur_scene = -1; cur_seq = -1;
    cur_seq_ticks = 0;
    seq_tick_offset = 0;
    unserialize_offset = 0;
    current_row = 1;
    current_cycle = 1;
    total_failed = 0;
    rerunning_failures = false;
    memset(chosen, 0, sizeof(chosen));
    for (int i = 0; i < 8; i++) failed[i] = 0;
    sound_cooldown = 0;   // ⭐ FIX: l'orologio (total_ms) riparte da 0 ad ogni rilancio;
                          // se non azzero il cooldown resta un timestamp stale della
                          // partita precedente -> play_sound() bloccato per minuti ->
                          // effetti coin/start/mosse muti al 2° avvio (musica/voce OK).
}

static void start_game(void)
{
    setup_scene_manager();
    sm_game_over = false;
    // (segreto arcade UP+LEFT=vite infinite DISATTIVATO: poteva scattare per sbaglio
    //  tenendo il joystick mentre si preme START -> vite mai finite)
    sm_infinite_lives = false;
    choose_next_scene(false);
}

static void do_interrupt(int itr)
{
    if (itr == ITR_GAME_OVER_COMPLETE) game_over_complete();
    else if (itr == ITR_START_GAME)    start_game();
}

// ---------------------------------------------------------------- sequence/scene
static void start_sequence(int seq_idx)
{
    accepted = 0;                                  // (lua: accepted_input = nil)
    cur_seq = seq_idx;
    const dl_sequence_t *q = cur_sequence();
    if (!q) return;
    if (q->start_ms < 0) {                          // noseek: continua a riprodurre
        seq_tick_offset += cur_seq_ticks;
    } else {
        if (q->single_frame) dl_cb_show_still(q->start_ms);
        else                 dl_cb_start_clip(q->start_ms);
        last_seek = q->start_ms;
        seq_tick_offset = 0;
        unserialize_offset = 0;
    }
}

static void start_scene(int scene_idx, bool is_resurrection)
{
    cur_scene = scene_idx;
    int s = seq_index(scene_idx, is_resurrection ? "start_dead" : "start_alive");
    if (s < 0) s = seq_index(scene_idx, "start_alive");   // fallback difensivo
    if (s < 0) s = 0;
    start_sequence(s);
}

// ---------------------------------------------------------------- kill/choose
static void kill_player(void)
{
    if (!CVAR_INFINITE_LIVES && !sm_infinite_lives) {
        if (lives_left > 0) lives_left--;
    }
    if (lives_left == 0) game_over(false);
    else                 choose_next_scene(true);
}

static void choose_next_scene(bool is_resurrection)
{
    // segna la scena corrente come riuscita/fallita e avanza gli indici
    if (cur_scene >= 0) {
        const char *nm = cur_scene_name();
        if (strcmp(nm, "the_dragons_lair") == 0) {
            // niente
        } else if (strcmp(nm, "introduction") == 0) {
            completed_intro = true;
        } else if (rerunning_failures) {
            if (!is_resurrection) {                 // battuta: togli dalla lista fallite
                for (int i = 0; i < total_failed - 1; i++) failed[i] = failed[i + 1];
                if (total_failed > 0) failed[total_failed - 1] = 0;
                if (total_failed > 0) total_failed--;
            }
            if (total_failed == 0) rerunning_failures = false;
        } else {
            if (is_resurrection) {
                if (total_failed < 8) failed[total_failed++] = nm;
            }
            current_row++;
            if (current_row > DL_N_ROWS) {
                current_row = 1;
                if (current_cycle < N_COLS) current_cycle++;
            }
        }
    }

    // scelta della prossima scena
    if (!completed_intro) {
        start_scene(scene_index("introduction"), is_resurrection);
    } else if (rerunning_failures && total_failed > 0) {
        start_scene(scene_index(failed[0]), is_resurrection);
    } else if (strcmp(cur_scene_name(), "the_dragons_lair") == 0) {
        if (is_resurrection) start_scene(scene_index("the_dragons_lair"), true);
        else                 game_over(true);
    } else {
        if (current_row == DL_N_ROWS) {
            if (current_cycle == N_COLS && total_failed > 0) {
                rerunning_failures = true;
                start_scene(scene_index(failed[0]), is_resurrection);
            } else {
                start_scene(scene_index(DL_ROWS[current_row - 1][current_cycle - 1]), is_resurrection);
            }
        } else {
            const char *elig[N_COLS]; int cols[N_COLS], ne = 0;
            for (int i = 0; i < N_COLS; i++) {
                if (!chosen[current_row - 1][i]) { elig[ne] = DL_ROWS[current_row - 1][i]; cols[ne] = i; ne++; }
            }
            if (ne == 0) {                          // difensivo: tutte scelte -> resetta riga
                for (int i = 0; i < N_COLS; i++) { chosen[current_row - 1][i] = false; elig[i] = DL_ROWS[current_row - 1][i]; cols[i] = i; }
                ne = N_COLS;
            }
            int choice = (int)(cur_ticks % (uint32_t)ne);
            chosen[current_row - 1][cols[choice]] = true;
            start_scene(scene_index(elig[choice]), is_resurrection);
        }
    }
}

// ---------------------------------------------------------------- actions/timeout
static bool check_actions(void)
{
    if (cur_in && cur_in->coinslot) play_sound(SND_COINSLOT);
    if (accepted) return true;                      // ignora input fino a fine sequenza

    const dl_sequence_t *q = cur_sequence();
    if (q && q->actions && q->n_actions) {
        for (int i = 0; i < q->n_actions; i++) {
            const dl_action_t *a = &q->actions[i];
            if (cur_seq_ticks >= a->from_ms && cur_seq_ticks <= a->to_ms) {
                bool kills = (a->next_seq >= 0) && DL_SCENES[cur_scene].seqs[a->next_seq].kills_player;
                if (CVAR_GOD_MODE && a->next_seq >= 0 && !kills) {
                    accepted = a; return true;
                } else if (input_pressed(a->input)) {
                    accepted = a;
                    if (a->input != IN_START) play_sound(SND_ACCEPT);
                    return true;
                }
            }
        }
    }
    // input sbagliato -> buzz (non in attract)
    if (!accepted) {
        if (input_pressed(IN_UP) || input_pressed(IN_DOWN) || input_pressed(IN_LEFT) ||
            input_pressed(IN_RIGHT) || input_pressed(IN_ACTION)) {
            if (strcmp(cur_scene_name(), "attract_mode") != 0) play_sound(SND_REJECT);
        }
    }
    return false;
}

static void check_timeout(void)
{
    for (int guard = 0; guard < 64; guard++) {      // il lua ricorre su timeout==0; qui iteriamo
        const dl_sequence_t *q = cur_sequence();
        if (!q) return;

        bool done = false;
        if (cur_seq_ticks >= q->timeout_when) done = true;
        else if (accepted && accepted->interrupt != ITR_NONE) done = true;
        else if (accepted && accepted->next_seq >= 0 &&
                 DL_SCENES[cur_scene].seqs[accepted->next_seq].start_ms != -1) done = true;
        if (!done) return;

        // esito = azione accettata, oppure il timeout della sequenza
        int32_t out_next; int out_itr, out_pts;
        if (accepted) { out_next = accepted->next_seq; out_itr = accepted->interrupt; out_pts = accepted->points; }
        else          { out_next = q->timeout_next;    out_itr = q->timeout_interrupt; out_pts = q->timeout_points; }

        if (out_pts) current_score += out_pts;

        bool killed_here = q->kills_player;
        if (out_itr != ITR_NONE) {
            do_interrupt(out_itr);
        } else if (out_next >= 0) {
            start_sequence((int)out_next);
        } else {                                     // nextsequence nil
            if (killed_here) kill_player();
            else             choose_next_scene(false);
        }

        // se la nuova sequenza ha timeout 0, va processata subito (hack del lua)
        const dl_sequence_t *nq = cur_sequence();
        if (nq && nq->timeout_when == 0) continue;
        return;
    }
}

// ---------------------------------------------------------------- API pubblica
void dl_game_reset(void)
{
    setup_scene_manager();
    sm_init = false;                                 // il primo tick fa partire l'attract
}

void dl_game_tick(uint32_t total_ms, uint32_t clip_ms, const dl_inputs_t *in)
{
    cur_ticks = total_ms;
    cur_in = in;
    if (!sm_init) setup_scene_manager();

    cur_seq_ticks = (int32_t)((int32_t)clip_ms + unserialize_offset) - seq_tick_offset;

    if (cur_seq < 0) start_attract_mode(false);      // nessuna sequenza -> attract

    check_actions();
    check_timeout();
}

int dl_game_score(void) { return current_score; }
int dl_game_lives(void) { return lives_left; }
int dl_game_in_attract(void)
{
    return (cur_scene < 0 || strcmp(cur_scene_name(), "attract_mode") == 0) ? 1 : 0;
}
int dl_game_gameover(void) { return sm_game_over ? 1 : 0; }
const char *dl_game_scene_name(void) { return cur_scene >= 0 ? DL_SCENES[cur_scene].name : "attract"; }
int dl_game_seq_ms(void) { return cur_seq_ticks; }
void dl_game_set_lives(int n) { if (n > 0) g_starting_lives = n; }   // NORMALE=5, FACILE=10

// Aiuto a schermo: mossa CONSIGLIATA nella sequenza corrente. Scorre le azioni la cui
// finestra e' attiva (con un piccolo anticipo LEAD = pre-mossa) e che NON portano a una
// sequenza che uccide; a parita' sceglie quella con piu' punti (= la mossa "canonica" da
// walkthrough). 0 = nessun aiuto (attract, mossa gia' presa, o la scelta giusta e' NON muoversi).
int dl_game_move_hint(void)
{
    if (cur_scene < 0 || strcmp(cur_scene_name(), "attract_mode") == 0) return 0;
    if (accepted) return 0;                          // mossa gia' accettata: nascondi l'aiuto
    const dl_sequence_t *q = cur_sequence();
    if (!q || !q->actions || !q->n_actions) return 0;
    const int LEAD = 500;                            // mostra la freccia ~500ms prima della finestra
    const dl_action_t *best = 0;
    for (int i = 0; i < q->n_actions; i++) {
        const dl_action_t *a = &q->actions[i];
        if (a->input == IN_START) continue;
        if (cur_seq_ticks < a->from_ms - LEAD) continue;
        if (cur_seq_ticks > a->to_ms) continue;
        bool kills = (a->next_seq >= 0) && DL_SCENES[cur_scene].seqs[a->next_seq].kills_player;
        if (kills) continue;                         // scarta le mosse che portano alla morte
        if (!best || a->points > best->points) best = a;
    }
    return best ? best->input : 0;
}
