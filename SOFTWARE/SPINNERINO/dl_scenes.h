#pragma once
// GENERATO da gen_dl_scenes.py dal file ufficiale DirkSimple. NON modificare a mano.
// Logica/dati IDENTICI al cabinato (scene, mosse, finestre, morti). Tempi in ms.
// Gli enum IN_*/ITR_* sono in dl_game.h (incluso PRIMA di questo file).
#include <stdint.h>
#include "dl_game.h"

typedef struct { uint8_t input; int32_t from_ms, to_ms; int16_t next_seq; uint8_t interrupt; int16_t points; } dl_action_t;
typedef struct { const char* name; int32_t start_ms; uint8_t single_frame, kills_player;
                 int32_t timeout_when; int16_t timeout_next; uint8_t timeout_interrupt; int16_t timeout_points;
                 const dl_action_t* actions; uint8_t n_actions; } dl_sequence_t;
typedef struct { const char* name; const dl_sequence_t* seqs; uint8_t n_seqs; } dl_scene_t;

static const dl_action_t ACT_attract_mode_attract_movie[] = {
  { IN_START, 0, 60000, -1, ITR_START_GAME, 0 },
};
static const dl_action_t ACT_attract_mode_insert_coins[] = {
  { IN_START, 0, 60000, -1, ITR_START_GAME, 0 },
};
static const dl_action_t ACT_flaming_ropes_enter_room[] = {
  { IN_RIGHT, 0, 1245, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1245, 2130, 5, ITR_NONE, 251 },
  { IN_RIGHT, 2130, 4260, 11, ITR_NONE, 0 },
  { IN_UP, 0, 2490, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2490, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_platform_sliding[] = {
  { IN_RIGHT, 0, 1835, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1835, 2884, 5, ITR_NONE, 251 },
  { IN_LEFT, 1835, 2884, 11, ITR_NONE, 0 },
  { IN_UP, 0, 2884, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2884, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_rope1[] = {
  { IN_RIGHT, 0, 1081, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1081, 1835, 6, ITR_NONE, 379 },
  { IN_UP, 0, 1835, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1835, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1835, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_rope2[] = {
  { IN_RIGHT, 0, 1081, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1081, 1835, 7, ITR_NONE, 495 },
  { IN_UP, 0, 1835, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1835, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1835, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_rope3[] = {
  { IN_RIGHT, 0, 852, 11, ITR_NONE, 0 },
  { IN_RIGHT, 852, 1704, 8, ITR_NONE, 915 },
  { IN_UP, 0, 1769, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1769, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1769, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_bower_enter_room[] = {
  { IN_UP, 0, 2035, 5, ITR_NONE, 379 },
};
static const dl_action_t ACT_alice_room_enter_room[] = {
  { IN_RIGHT, 0, 2064, 6, ITR_NONE, 379 },
  { IN_UP, 0, 2064, 4, ITR_NONE, 0 },
  { IN_DOWN, 1131, 2032, 5, ITR_NONE, 0 },
  { IN_LEFT, 1131, 2032, 5, ITR_NONE, 0 },
};
static const dl_action_t ACT_wind_room_enter_room[] = {
  { IN_RIGHT, 7406, 8126, 5, ITR_NONE, 379 },
  { IN_UP, 5964, 8126, 4, ITR_NONE, 0 },
};
static const dl_action_t ACT_vestibule_enter_room[] = {
  { IN_RIGHT, 1966, 3998, 4, ITR_NONE, 251 },
  { IN_DOWN, 1966, 3998, 4, ITR_NONE, 251 },
  { IN_UP, 1966, 3998, 5, ITR_NONE, 0 },
  { IN_LEFT, 2490, 3965, 5, ITR_NONE, 0 },
};
static const dl_action_t ACT_vestibule_stagger[] = {
  { IN_RIGHT, 0, 950, 6, ITR_NONE, 251 },
  { IN_LEFT, 0, 950, 5, ITR_NONE, 0 },
  { IN_UP, 0, 950, 5, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_short_enter_room[] = {
  { IN_LEFT, 2818, 5014, 6, ITR_NONE, 0 },
  { IN_LEFT, 5014, 5341, 7, ITR_NONE, 3255 },
  { IN_LEFT, 5341, 5669, 5, ITR_NONE, 0 },
  { IN_LEFT, 5702, 6029, 7, ITR_NONE, 3255 },
  { IN_LEFT, 6029, 6357, 6, ITR_NONE, 0 },
  { IN_LEFT, 6357, 6685, 7, ITR_NONE, 3255 },
  { IN_RIGHT, 2818, 7209, 6, ITR_NONE, 0 },
  { IN_UP, 2818, 7209, 6, ITR_NONE, 0 },
  { IN_DOWN, 2818, 7209, 6, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_enter_room[] = {
  { IN_LEFT, 2818, 5014, 8, ITR_NONE, 0 },
  { IN_LEFT, 5014, 5341, 9, ITR_NONE, 3255 },
  { IN_LEFT, 5341, 5669, 8, ITR_NONE, 0 },
  { IN_LEFT, 5702, 6029, 9, ITR_NONE, 3255 },
  { IN_LEFT, 6029, 6357, 7, ITR_NONE, 0 },
  { IN_LEFT, 6357, 6685, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 2818, 6750, 8, ITR_NONE, 0 },
  { IN_UP, 2818, 6750, 8, ITR_NONE, 0 },
  { IN_DOWN, 2818, 6750, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_second_jump_set[] = {
  { IN_RIGHT, 0, 2458, 8, ITR_NONE, 0 },
  { IN_RIGHT, 2458, 2785, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 2785, 3113, 7, ITR_NONE, 0 },
  { IN_RIGHT, 3146, 3473, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 3473, 3801, 7, ITR_NONE, 0 },
  { IN_RIGHT, 3801, 4129, 9, ITR_NONE, 3255 },
  { IN_LEFT, 0, 4293, 8, ITR_NONE, 0 },
  { IN_UP, 0, 4293, 8, ITR_NONE, 0 },
  { IN_DOWN, 0, 4293, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_third_jump_set[] = {
  { IN_LEFT, 0, 2458, 7, ITR_NONE, 0 },
  { IN_LEFT, 2458, 2785, 9, ITR_NONE, 3255 },
  { IN_LEFT, 2785, 3113, 7, ITR_NONE, 0 },
  { IN_LEFT, 3146, 3473, 9, ITR_NONE, 3255 },
  { IN_LEFT, 3473, 3801, 7, ITR_NONE, 0 },
  { IN_LEFT, 3801, 4129, 9, ITR_NONE, 3255 },
  { IN_LEFT, 4162, 5571, 8, ITR_NONE, 0 },
  { IN_RIGHT, 0, 5571, 8, ITR_NONE, 0 },
  { IN_UP, 0, 5571, 8, ITR_NONE, 0 },
  { IN_DOWN, 0, 5571, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_enter_room[] = {
  { IN_UP, 2228, 3244, 4, ITR_NONE, 495 },
  { IN_ACTION, 2228, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 2228, 3178, 11, ITR_NONE, 0 },
  { IN_RIGHT, 2228, 3178, 11, ITR_NONE, 0 },
  { IN_LEFT, 2228, 3178, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_jumped_skulls[] = {
  { IN_ACTION, 688, 1278, 5, ITR_NONE, 915 },
  { IN_UP, 918, 1278, 12, ITR_NONE, 0 },
  { IN_DOWN, 0, 1311, 11, ITR_NONE, 0 },
  { IN_LEFT, 668, 1278, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_attacked_first_hand[] = {
  { IN_UP, 1049, 2195, 6, ITR_NONE, 495 },
  { IN_DOWN, 1049, 2163, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1049, 2163, 13, ITR_NONE, 0 },
  { IN_LEFT, 1049, 2163, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_jumped_slime[] = {
  { IN_ACTION, 590, 1180, 7, ITR_NONE, 915 },
  { IN_UP, 590, 1147, 12, ITR_NONE, 0 },
  { IN_DOWN, 0, 1212, 13, ITR_NONE, 0 },
  { IN_RIGHT, 590, 1147, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_attacked_second_hand[] = {
  { IN_LEFT, 426, 1835, 8, ITR_NONE, 495 },
  { IN_DOWN, 0, 1835, 13, ITR_NONE, 0 },
  { IN_RIGHT, 360, 1835, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_enter_crypt[] = {
  { IN_ACTION, 688, 1835, 9, ITR_NONE, 495 },
  { IN_UP, 1081, 1835, 14, ITR_NONE, 0 },
  { IN_DOWN, 688, 1835, 14, ITR_NONE, 0 },
  { IN_RIGHT, 1081, 1835, 14, ITR_NONE, 0 },
  { IN_LEFT, 1081, 1835, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_enter_room[] = {
  { IN_RIGHT, 3801, 4522, 4, ITR_NONE, 495 },
  { IN_UP, 3801, 4522, 11, ITR_NONE, 0 },
  { IN_LEFT, 3801, 4522, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_second_fire[] = {
  { IN_LEFT, 721, 1343, 5, ITR_NONE, 495 },
  { IN_UP, 721, 1343, 11, ITR_NONE, 0 },
  { IN_RIGHT, 721, 1343, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_third_fire[] = {
  { IN_RIGHT, 1212, 1835, 6, ITR_NONE, 495 },
  { IN_UP, 852, 1802, 11, ITR_NONE, 0 },
  { IN_LEFT, 1212, 1835, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_fourth_fire[] = {
  { IN_LEFT, 1311, 1966, 7, ITR_NONE, 495 },
  { IN_UP, 1049, 1966, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1311, 1966, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_brick_wall[] = {
  { IN_LEFT, 1311, 1868, 8, ITR_NONE, 1326 },
  { IN_UP, 950, 1835, 12, ITR_NONE, 0 },
  { IN_RIGHT, 950, 1835, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_fifth_fire[] = {
  { IN_LEFT, 721, 1376, 9, ITR_NONE, 495 },
  { IN_UP, 393, 1409, 11, ITR_NONE, 0 },
  { IN_RIGHT, 721, 1376, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_giddy_goons_enter_room[] = {
  { IN_ACTION, 2392, 3113, 4, ITR_NONE, 379 },
  { IN_UP, 1507, 2392, 10, ITR_NONE, 0 },
  { IN_RIGHT, 2392, 3113, 8, ITR_NONE, 0 },
  { IN_LEFT, 2392, 3113, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_giddy_goons_kills_first_goon[] = {
  { IN_RIGHT, 1114, 1835, 5, ITR_NONE, 1326 },
  { IN_UP, 885, 1802, 10, ITR_NONE, 0 },
  { IN_LEFT, 1114, 1835, 9, ITR_NONE, 0 },
  { IN_ACTION, 1114, 1835, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_giddy_goons_climbs_stairs[] = {
  { IN_ACTION, 1475, 2130, 6, ITR_NONE, 3255 },
  { IN_UP, 0, 2130, 6, ITR_NONE, 3255 },
  { IN_UP, 1475, 2130, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2163, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 2163, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_giddy_goons_kill_upper_goons[] = {
  { IN_UP, 852, 1540, 7, ITR_NONE, 0 },
  { IN_LEFT, 852, 1540, 7, ITR_NONE, 0 },
  { IN_DOWN, 0, 1606, 11, ITR_NONE, 0 },
  { IN_ACTION, 786, 1573, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_tentacle_room_enter_room[] = {
  { IN_ACTION, 2687, 3965, 4, ITR_NONE, 49 },
};
static const dl_action_t ACT_tentacle_room_kills_first_tentacle[] = {
  { IN_UP, 1409, 2327, 5, ITR_NONE, 379 },
};
static const dl_action_t ACT_tentacle_room_jump_to_weapon_rack[] = {
  { IN_RIGHT, 1180, 1933, 6, ITR_NONE, 495 },
  { IN_DOWN, 0, 1901, 11, ITR_NONE, 0 },
  { IN_ACTION, 0, 1901, 11, ITR_NONE, 0 },
  { IN_LEFT, 1180, 1933, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_tentacle_room_jump_to_door[] = {
  { IN_DOWN, 492, 1409, 7, ITR_NONE, 915 },
  { IN_RIGHT, 492, 1409, 7, ITR_NONE, 915 },
  { IN_UP, 492, 1409, 13, ITR_NONE, 0 },
  { IN_LEFT, 0, 1442, 11, ITR_NONE, 0 },
  { IN_ACTION, 0, 1475, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_tentacle_room_jump_to_stairs[] = {
  { IN_LEFT, 0, 1966, 11, ITR_NONE, 0 },
  { IN_LEFT, 1966, 2720, 8, ITR_NONE, 1326 },
  { IN_UP, 0, 2720, 12, ITR_NONE, 0 },
  { IN_DOWN, 0, 2720, 11, ITR_NONE, 0 },
  { IN_ACTION, 0, 2720, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_tentacle_room_jump_to_table[] = {
  { IN_UP, 360, 2195, 9, ITR_NONE, 1939 },
  { IN_RIGHT, 360, 2195, 9, ITR_NONE, 1939 },
  { IN_LEFT, 0, 2228, 11, ITR_NONE, 0 },
  { IN_ACTION, 0, 2228, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2228, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_tilting_room_enter_room[] = {
  { IN_DOWN, 3768, 4489, 4, ITR_NONE, 1939 },
  { IN_LEFT, 2785, 4489, 7, ITR_NONE, 0 },
  { IN_RIGHT, 3768, 4489, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_tilting_room_jumps_back[] = {
  { IN_UP, 328, 885, 7, ITR_NONE, 0 },
  { IN_UP, 885, 1540, 5, ITR_NONE, 2675 },
  { IN_LEFT, 328, 1573, 7, ITR_NONE, 0 },
  { IN_DOWN, 328, 1573, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_tilting_room_jumps_forward[] = {
  { IN_LEFT, 492, 1049, 6, ITR_NONE, 1939 },
  { IN_UP, 492, 1049, 9, ITR_NONE, 0 },
  { IN_DOWN, 328, 1049, 8, ITR_NONE, 0 },
  { IN_RIGHT, 328, 1049, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_throne_room_enter_room[] = {
  { IN_RIGHT, 1966, 2720, 4, ITR_NONE, 1326 },
  { IN_UP, 1606, 3834, 9, ITR_NONE, 0 },
  { IN_DOWN, 1606, 3834, 9, ITR_NONE, 0 },
  { IN_LEFT, 1606, 3834, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_throne_room_first_jump[] = {
  { IN_UP, 0, 688, 5, ITR_NONE, 3255 },
  { IN_RIGHT, 0, 688, 5, ITR_NONE, 3255 },
  { IN_LEFT, 0, 688, 9, ITR_NONE, 0 },
  { IN_DOWN, 0, 688, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_throne_room_second_jump[] = {
  { IN_RIGHT, 131, 885, 6, ITR_NONE, 2675 },
  { IN_LEFT, 0, 885, 8, ITR_NONE, 0 },
  { IN_UP, 0, 885, 9, ITR_NONE, 0 },
  { IN_DOWN, 0, 885, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_throne_room_on_throne[] = {
  { IN_RIGHT, 3408, 4358, 7, ITR_NONE, 1939 },
  { IN_LEFT, 3408, 4358, 9, ITR_NONE, 0 },
  { IN_LEFT, 3408, 4096, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_first_boulders[] = {
  { IN_LEFT, 0, 1016, 5, ITR_NONE, 379 },
  { IN_UP, 0, 1016, 18, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1016, 18, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_second_boulders[] = {
  { IN_RIGHT, 950, 1901, 6, ITR_NONE, 379 },
  { IN_UP, 950, 1901, 19, ITR_NONE, 0 },
  { IN_LEFT, 950, 1901, 19, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_third_boulders[] = {
  { IN_LEFT, 1049, 1999, 7, ITR_NONE, 379 },
  { IN_UP, 1049, 1999, 20, ITR_NONE, 0 },
  { IN_RIGHT, 1049, 1999, 20, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_fourth_boulders[] = {
  { IN_RIGHT, 950, 1966, 8, ITR_NONE, 379 },
  { IN_UP, 950, 1966, 21, ITR_NONE, 0 },
  { IN_LEFT, 950, 1966, 21, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_first_rapids[] = {
  { IN_UP, 3932, 4522, 22, ITR_NONE, 0 },
  { IN_UP, 4522, 5145, 9, ITR_NONE, 495 },
  { IN_RIGHT, 3736, 4555, 9, ITR_NONE, 495 },
  { IN_RIGHT, 4555, 5145, 22, ITR_NONE, 0 },
  { IN_LEFT, 4391, 5177, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_second_rapids[] = {
  { IN_UP, 1212, 1835, 22, ITR_NONE, 0 },
  { IN_UP, 1835, 2490, 10, ITR_NONE, 495 },
  { IN_RIGHT, 1212, 2613, 22, ITR_NONE, 0 },
  { IN_LEFT, 1081, 1901, 10, ITR_NONE, 495 },
  { IN_LEFT, 1901, 2490, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_third_rapids[] = {
  { IN_UP, 1311, 1802, 22, ITR_NONE, 0 },
  { IN_UP, 1802, 2458, 11, ITR_NONE, 495 },
  { IN_RIGHT, 1016, 1868, 11, ITR_NONE, 495 },
  { IN_RIGHT, 1868, 2458, 22, ITR_NONE, 0 },
  { IN_LEFT, 1147, 2490, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_fourth_rapids[] = {
  { IN_UP, 1343, 2163, 22, ITR_NONE, 0 },
  { IN_UP, 2163, 2785, 12, ITR_NONE, 495 },
  { IN_RIGHT, 1606, 2818, 22, ITR_NONE, 0 },
  { IN_LEFT, 1343, 2163, 12, ITR_NONE, 495 },
  { IN_LEFT, 2163, 2785, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_first_whirlpools[] = {
  { IN_RIGHT, 3834, 5472, 13, ITR_NONE, 251 },
  { IN_UP, 3834, 5472, 23, ITR_NONE, 0 },
  { IN_LEFT, 3834, 5472, 23, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_second_whirlpools[] = {
  { IN_LEFT, 1409, 2720, 14, ITR_NONE, 251 },
  { IN_UP, 1409, 2720, 23, ITR_NONE, 0 },
  { IN_RIGHT, 1409, 2720, 23, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_third_whirlpools[] = {
  { IN_RIGHT, 1343, 2490, 15, ITR_NONE, 251 },
  { IN_UP, 1343, 2490, 23, ITR_NONE, 0 },
  { IN_LEFT, 1343, 2490, 23, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_fourth_whirlpools[] = {
  { IN_LEFT, 1442, 2720, 16, ITR_NONE, 251 },
  { IN_UP, 1442, 2720, 23, ITR_NONE, 0 },
  { IN_RIGHT, 1442, 2720, 23, ITR_NONE, 0 },
};
static const dl_action_t ACT_underground_river_bounce_to_chain[] = {
  { IN_UP, 3867, 5145, 17, ITR_NONE, 495 },
  { IN_DOWN, 3867, 5145, 24, ITR_NONE, 0 },
  { IN_RIGHT, 3867, 5145, 17, ITR_NONE, 495 },
  { IN_LEFT, 3867, 5145, 24, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_enter_room[] = {
  { IN_DOWN, 4882, 5145, 11, ITR_NONE, 0 },
  { IN_DOWN, 5145, 5931, 4, ITR_NONE, 251 },
  { IN_UP, 5177, 5964, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_red_ball[] = {
  { IN_DOWN, 852, 1081, 11, ITR_NONE, 0 },
  { IN_DOWN, 1081, 1835, 5, ITR_NONE, 379 },
  { IN_UP, 0, 1868, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_blue_ball[] = {
  { IN_DOWN, 885, 1212, 11, ITR_NONE, 0 },
  { IN_DOWN, 1212, 1933, 6, ITR_NONE, 379 },
  { IN_UP, 0, 1966, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_green_ball[] = {
  { IN_DOWN, 885, 1147, 11, ITR_NONE, 0 },
  { IN_DOWN, 1147, 1933, 7, ITR_NONE, 379 },
  { IN_UP, 0, 1966, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_orange_ball[] = {
  { IN_DOWN, 885, 1147, 11, ITR_NONE, 0 },
  { IN_DOWN, 1147, 1933, 8, ITR_NONE, 379 },
  { IN_UP, 0, 1966, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_purple_ball[] = {
  { IN_DOWN, 885, 1114, 11, ITR_NONE, 0 },
  { IN_DOWN, 1114, 1868, 9, ITR_NONE, 379 },
  { IN_UP, 0, 1901, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_rolling_balls_pit_in_ground[] = {
  { IN_UP, 0, 1414, 10, ITR_NONE, 379 },
};
static const dl_action_t ACT_black_knight_enter_room[] = {
  { IN_LEFT, 2687, 3506, 5, ITR_NONE, 1939 },
  { IN_RIGHT, 3146, 3539, 8, ITR_NONE, 0 },
  { IN_UP, 2687, 3572, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_black_knight_seq2[] = {
  { IN_LEFT, 2687, 3506, 5, ITR_NONE, 1939 },
  { IN_RIGHT, 3146, 3539, 8, ITR_NONE, 0 },
  { IN_UP, 2687, 3572, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_black_knight_seq3[] = {
  { IN_LEFT, 3113, 3867, 6, ITR_NONE, 1939 },
  { IN_UP, 3113, 3801, 10, ITR_NONE, 0 },
  { IN_RIGHT, 3473, 6849, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_black_knight_seq4[] = {
  { IN_RIGHT, 2458, 3211, 7, ITR_NONE, 2675 },
  { IN_LEFT, 2458, 3178, 8, ITR_NONE, 0 },
  { IN_UP, 2458, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 1966, 2687, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_enter_room[] = {
  { IN_UP, 1966, 2720, 5, ITR_NONE, 2191 },
  { IN_DOWN, 1966, 2720, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1966, 2720, 11, ITR_NONE, 0 },
  { IN_LEFT, 1966, 2720, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_seq2[] = {
  { IN_UP, 1966, 2720, 5, ITR_NONE, 2191 },
  { IN_DOWN, 1966, 2720, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1966, 2720, 11, ITR_NONE, 0 },
  { IN_LEFT, 1966, 2720, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_seq3[] = {
  { IN_ACTION, 1638, 2490, 6, ITR_NONE, 3255 },
  { IN_UP, 1638, 2490, 10, ITR_NONE, 0 },
  { IN_DOWN, 1638, 2490, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1835, 2720, 11, ITR_NONE, 0 },
  { IN_LEFT, 1638, 2490, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_seq4[] = {
  { IN_DOWN, 557, 1376, 7, ITR_NONE, 3255 },
  { IN_DOWNRIGHT, 557, 1376, 7, ITR_NONE, 3255 },
  { IN_UP, 0, 1409, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1409, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1409, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_seq5[] = {
  { IN_ACTION, 655, 1737, 8, ITR_NONE, 2191 },
  { IN_UP, 655, 1737, 12, ITR_NONE, 0 },
  { IN_RIGHT, 0, 262, 12, ITR_NONE, 0 },
  { IN_LEFT, 0, 1737, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_bubbling_cauldron_seq6[] = {
  { IN_RIGHT, 1802, 2916, 9, ITR_NONE, 1326 },
  { IN_UP, 0, 2949, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 2949, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_enter_room[] = {
  { IN_UP, 0, 2687, 5, ITR_NONE, 915 },
  { IN_DOWN, 2064, 2687, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2687, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_seq2[] = {
  { IN_UP, 0, 2687, 5, ITR_NONE, 915 },
  { IN_DOWN, 2064, 2687, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2687, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_seq3[] = {
  { IN_UPLEFT, 0, 1147, 6, ITR_NONE, 915 },
  { IN_UP, 0, 1147, 6, ITR_NONE, 915 },
  { IN_LEFT, 950, 2097, 6, ITR_NONE, 915 },
  { IN_DOWN, 0, 1147, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1147, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_seq4[] = {
  { IN_ACTION, 1737, 2458, 7, ITR_NONE, 2675 },
  { IN_RIGHT, 0, 2490, 11, ITR_NONE, 0 },
  { IN_UP, 1737, 2458, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 360, 11, ITR_NONE, 0 },
  { IN_LEFT, 328, 2785, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_seq5[] = {
  { IN_RIGHT, 360, 1442, 8, ITR_NONE, 915 },
  { IN_UP, 0, 1049, 11, ITR_NONE, 0 },
  { IN_UPRIGHT, 360, 1409, 8, ITR_NONE, 915 },
  { IN_DOWN, 0, 1442, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1147, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_catwalk_bats_seq6[] = {
  { IN_RIGHT, 885, 1442, 9, ITR_NONE, 3551 },
  { IN_UP, 0, 1475, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1475, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 1475, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_enter_room[] = {
  { IN_UP, 2458, 3473, 5, ITR_NONE, 495 },
  { IN_ACTION, 2458, 3408, 11, ITR_NONE, 0 },
  { IN_DOWN, 2458, 3408, 12, ITR_NONE, 0 },
  { IN_RIGHT, 2458, 3408, 12, ITR_NONE, 0 },
  { IN_LEFT, 2458, 3408, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq2[] = {
  { IN_UP, 2458, 3473, 5, ITR_NONE, 495 },
  { IN_ACTION, 2458, 3408, 11, ITR_NONE, 0 },
  { IN_DOWN, 2458, 3408, 12, ITR_NONE, 0 },
  { IN_RIGHT, 2458, 3408, 12, ITR_NONE, 0 },
  { IN_LEFT, 2458, 3408, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq3[] = {
  { IN_ACTION, 492, 918, 6, ITR_NONE, 2191 },
  { IN_UP, 557, 918, 13, ITR_NONE, 0 },
  { IN_DOWN, 0, 1278, 12, ITR_NONE, 0 },
  { IN_RIGHT, 492, 918, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq4[] = {
  { IN_UP, 1147, 2228, 7, ITR_NONE, 495 },
  { IN_DOWN, 1147, 2261, 12, ITR_NONE, 0 },
  { IN_RIGHT, 1147, 2228, 14, ITR_NONE, 0 },
  { IN_LEFT, 1147, 2228, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq5[] = {
  { IN_ACTION, 688, 1114, 8, ITR_NONE, 2191 },
  { IN_UP, 688, 1114, 13, ITR_NONE, 0 },
  { IN_DOWN, 0, 1180, 14, ITR_NONE, 0 },
  { IN_LEFT, 688, 1114, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq6[] = {
  { IN_RIGHT, 492, 1835, 9, ITR_NONE, 495 },
  { IN_DOWN, 0, 1835, 14, ITR_NONE, 0 },
  { IN_LEFT, 492, 2327, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_crypt_creeps_reversed_seq7[] = {
  { IN_ACTION, 754, 1835, 10, ITR_NONE, 495 },
  { IN_RIGHT, 754, 1835, 15, ITR_NONE, 0 },
  { IN_LEFT, 754, 1835, 15, ITR_NONE, 0 },
  { IN_DOWN, 754, 1835, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_electric_cage_and_geyser_enter_room[] = {
  { IN_UP, 557, 2916, 5, ITR_NONE, 915 },
  { IN_DOWN, 2490, 2916, 9, ITR_NONE, 0 },
  { IN_RIGHT, 2327, 2916, 9, ITR_NONE, 0 },
  { IN_LEFT, 2327, 2916, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_electric_cage_and_geyser_seq2[] = {
  { IN_UP, 557, 2916, 5, ITR_NONE, 915 },
  { IN_DOWN, 2490, 2916, 9, ITR_NONE, 0 },
  { IN_RIGHT, 2327, 2916, 9, ITR_NONE, 0 },
  { IN_LEFT, 2327, 2916, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_electric_cage_and_geyser_seq3[] = {
  { IN_UP, 0, 393, 6, ITR_NONE, 1326 },
  { IN_UP, 393, 623, 8, ITR_NONE, 0 },
  { IN_UP, 623, 1049, 6, ITR_NONE, 1326 },
  { IN_UP, 1081, 1311, 8, ITR_NONE, 0 },
  { IN_UP, 1311, 1769, 6, ITR_NONE, 1326 },
  { IN_DOWN, 0, 1769, 9, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1769, 9, ITR_NONE, 0 },
  { IN_LEFT, 0, 1769, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_electric_cage_and_geyser_seq4[] = {
  { IN_LEFT, 3113, 3506, 7, ITR_NONE, 2191 },
  { IN_LEFT, 3506, 3998, 10, ITR_NONE, 0 },
  { IN_LEFT, 3998, 4391, 7, ITR_NONE, 2191 },
  { IN_LEFT, 4424, 4915, 10, ITR_NONE, 0 },
  { IN_LEFT, 4915, 5341, 7, ITR_NONE, 2191 },
  { IN_RIGHT, 1933, 5341, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_reversed_enter_room[] = {
  { IN_RIGHT, 2785, 5014, 8, ITR_NONE, 0 },
  { IN_RIGHT, 5014, 5341, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 5374, 5702, 10, ITR_NONE, 0 },
  { IN_RIGHT, 5702, 6029, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 6029, 6357, 10, ITR_NONE, 0 },
  { IN_RIGHT, 6390, 6717, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 6717, 9208, 8, ITR_NONE, 0 },
  { IN_LEFT, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_LEFT, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_LEFT, 6783, 9208, 8, ITR_NONE, 0 },
  { IN_UP, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_UP, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_UP, 6783, 9208, 8, ITR_NONE, 0 },
  { IN_DOWN, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_DOWN, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_DOWN, 6783, 9208, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_reversed_seq2[] = {
  { IN_RIGHT, 2785, 5014, 8, ITR_NONE, 0 },
  { IN_RIGHT, 5014, 5341, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 5374, 5702, 10, ITR_NONE, 0 },
  { IN_RIGHT, 5702, 6029, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 6029, 6357, 10, ITR_NONE, 0 },
  { IN_RIGHT, 6390, 6717, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 6717, 9208, 8, ITR_NONE, 0 },
  { IN_LEFT, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_LEFT, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_LEFT, 6783, 9208, 8, ITR_NONE, 0 },
  { IN_UP, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_UP, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_UP, 6783, 9208, 8, ITR_NONE, 0 },
  { IN_DOWN, 2785, 4915, 8, ITR_NONE, 0 },
  { IN_DOWN, 4915, 6750, 10, ITR_NONE, 0 },
  { IN_DOWN, 6783, 9208, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_reversed_seq3[] = {
  { IN_LEFT, 0, 360, 9, ITR_NONE, 3255 },
  { IN_LEFT, 360, 688, 10, ITR_NONE, 0 },
  { IN_LEFT, 688, 1016, 9, ITR_NONE, 3255 },
  { IN_LEFT, 1049, 1376, 10, ITR_NONE, 0 },
  { IN_LEFT, 1376, 1704, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 0, 1802, 10, ITR_NONE, 0 },
  { IN_UP, 0, 1802, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 1802, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_falling_platform_long_reversed_seq4[] = {
  { IN_RIGHT, 0, 2523, 8, ITR_NONE, 0 },
  { IN_RIGHT, 2523, 2851, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 2851, 3178, 10, ITR_NONE, 0 },
  { IN_RIGHT, 3211, 3539, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 3539, 3867, 10, ITR_NONE, 0 },
  { IN_RIGHT, 3867, 4194, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 4227, 4719, 10, ITR_NONE, 0 },
  { IN_RIGHT, 4719, 5669, 8, ITR_NONE, 0 },
  { IN_LEFT, 0, 2949, 8, ITR_NONE, 0 },
  { IN_LEFT, 2949, 4719, 10, ITR_NONE, 0 },
  { IN_LEFT, 4719, 5636, 8, ITR_NONE, 0 },
  { IN_UP, 0, 2949, 8, ITR_NONE, 0 },
  { IN_UP, 2949, 4719, 10, ITR_NONE, 0 },
  { IN_UP, 4719, 5636, 8, ITR_NONE, 0 },
  { IN_DOWN, 0, 2949, 8, ITR_NONE, 0 },
  { IN_DOWN, 2949, 4719, 10, ITR_NONE, 0 },
  { IN_DOWN, 4719, 5636, 8, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_enter_room[] = {
  { IN_LEFT, 0, 1245, 11, ITR_NONE, 0 },
  { IN_LEFT, 1245, 2130, 5, ITR_NONE, 379 },
  { IN_LEFT, 2130, 4260, 11, ITR_NONE, 0 },
  { IN_UP, 0, 2490, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2490, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_seq2[] = {
  { IN_LEFT, 0, 1245, 11, ITR_NONE, 0 },
  { IN_LEFT, 1245, 2130, 5, ITR_NONE, 379 },
  { IN_LEFT, 2130, 4260, 11, ITR_NONE, 0 },
  { IN_UP, 0, 2490, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2490, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_seq3[] = {
  { IN_LEFT, 0, 1114, 11, ITR_NONE, 0 },
  { IN_LEFT, 1114, 1835, 6, ITR_NONE, 495 },
  { IN_RIGHT, 0, 1835, 11, ITR_NONE, 0 },
  { IN_UP, 0, 1835, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1835, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_seq4[] = {
  { IN_LEFT, 0, 1081, 11, ITR_NONE, 0 },
  { IN_LEFT, 1081, 1835, 7, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1835, 11, ITR_NONE, 0 },
  { IN_UP, 0, 1835, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1835, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_seq5[] = {
  { IN_LEFT, 0, 852, 11, ITR_NONE, 0 },
  { IN_LEFT, 852, 1704, 8, ITR_NONE, 0 },
  { IN_UP, 0, 1737, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1737, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1737, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flaming_ropes_reversed_seq10[] = {
  { IN_LEFT, 0, 1835, 11, ITR_NONE, 0 },
  { IN_LEFT, 1835, 2884, 5, ITR_NONE, 379 },
  { IN_UP, 0, 2884, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 2884, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flattening_staircase_enter_room[] = {
  { IN_LEFT, 2032, 2753, 5, ITR_NONE, 495 },
};
static const dl_action_t ACT_flattening_staircase_seq2[] = {
  { IN_LEFT, 2032, 2753, 5, ITR_NONE, 495 },
};
static const dl_action_t ACT_flattening_staircase_seq3[] = {
  { IN_ACTION, 1475, 2195, 6, ITR_NONE, 1939 },
};
static const dl_action_t ACT_flattening_staircase_seq4[] = {
  { IN_LEFT, 754, 1475, 7, ITR_NONE, 915 },
  { IN_UPLEFT, 754, 1475, 7, ITR_NONE, 915 },
  { IN_DOWN, 0, 1507, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1507, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_flattening_staircase_seq5[] = {
  { IN_LEFT, 1966, 3178, 8, ITR_NONE, 1326 },
  { IN_RIGHT, 1966, 3178, 11, ITR_NONE, 0 },
  { IN_UP, 1966, 3178, 11, ITR_NONE, 0 },
  { IN_DOWN, 0, 1278, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_enter_room[] = {
  { IN_UPLEFT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_UPLEFT, 3768, 4489, 5, ITR_NONE, 915 },
  { IN_LEFT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_LEFT, 3768, 4489, 5, ITR_NONE, 915 },
  { IN_UP, 3801, 4489, 11, ITR_NONE, 0 },
  { IN_RIGHT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_RIGHT, 3768, 4456, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq2[] = {
  { IN_UPLEFT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_UPLEFT, 3768, 4489, 5, ITR_NONE, 915 },
  { IN_LEFT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_LEFT, 3768, 4489, 5, ITR_NONE, 915 },
  { IN_UP, 3801, 4489, 11, ITR_NONE, 0 },
  { IN_RIGHT, 3441, 3768, 11, ITR_NONE, 0 },
  { IN_RIGHT, 3768, 4456, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq3[] = {
  { IN_RIGHT, 0, 721, 11, ITR_NONE, 0 },
  { IN_RIGHT, 721, 1245, 6, ITR_NONE, 915 },
  { IN_UP, 721, 1278, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 721, 11, ITR_NONE, 0 },
  { IN_LEFT, 721, 1278, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq4[] = {
  { IN_LEFT, 0, 1311, 11, ITR_NONE, 0 },
  { IN_LEFT, 1311, 1966, 7, ITR_NONE, 495 },
  { IN_RIGHT, 0, 1311, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1311, 1966, 13, ITR_NONE, 0 },
  { IN_UP, 852, 1966, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq5[] = {
  { IN_RIGHT, 0, 1311, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1311, 1966, 8, ITR_NONE, 495 },
  { IN_LEFT, 0, 1311, 11, ITR_NONE, 0 },
  { IN_LEFT, 1311, 1966, 13, ITR_NONE, 0 },
  { IN_UP, 1049, 1966, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq6[] = {
  { IN_RIGHT, 0, 1245, 11, ITR_NONE, 0 },
  { IN_RIGHT, 1245, 1802, 9, ITR_NONE, 1939 },
  { IN_UP, 950, 1769, 12, ITR_NONE, 0 },
  { IN_LEFT, 950, 1769, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_flying_horse_reversed_seq7[] = {
  { IN_RIGHT, 0, 786, 11, ITR_NONE, 0 },
  { IN_RIGHT, 786, 1442, 10, ITR_NONE, 495 },
  { IN_LEFT, 0, 786, 11, ITR_NONE, 0 },
  { IN_LEFT, 786, 1442, 13, ITR_NONE, 0 },
  { IN_UP, 393, 1475, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_giant_bat_enter_room[] = {
  { IN_ACTION, 0, 1016, 5, ITR_NONE, 1326 },
  { IN_RIGHT, 0, 1016, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_giant_bat_seq2[] = {
  { IN_ACTION, 0, 1016, 5, ITR_NONE, 1326 },
  { IN_RIGHT, 0, 1016, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_giant_bat_seq3[] = {
  { IN_LEFT, 819, 1376, 6, ITR_NONE, 2191 },
  { IN_RIGHT, 0, 1409, 12, ITR_NONE, 0 },
};
static const dl_action_t ACT_giant_bat_seq4[] = {
  { IN_UPLEFT, 1704, 2327, 7, ITR_NONE, 1326 },
  { IN_UP, 1704, 2327, 7, ITR_NONE, 1326 },
  { IN_LEFT, 1704, 2327, 7, ITR_NONE, 1326 },
  { IN_DOWN, 0, 2359, 12, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2359, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_giant_bat_seq5[] = {
  { IN_ACTION, 1737, 2327, 8, ITR_NONE, 3551 },
  { IN_DOWN, 590, 1147, 12, ITR_NONE, 0 },
  { IN_LEFT, 1737, 2327, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_enter_room[] = {
  { IN_UP, 0, 1573, 9, ITR_NONE, 0 },
  { IN_UP, 1573, 1933, 5, ITR_NONE, 4026 },
  { IN_UP, 1933, 3047, 9, ITR_NONE, 0 },
  { IN_UP, 3047, 3408, 5, ITR_NONE, 4026 },
  { IN_UP, 3408, 4522, 9, ITR_NONE, 0 },
  { IN_UP, 4555, 4915, 5, ITR_NONE, 4026 },
  { IN_UP, 4915, 6029, 9, ITR_NONE, 0 },
  { IN_LEFT, 0, 6095, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 6095, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_seq2[] = {
  { IN_UP, 0, 1573, 9, ITR_NONE, 0 },
  { IN_UP, 1573, 1933, 5, ITR_NONE, 4026 },
  { IN_UP, 1933, 3047, 9, ITR_NONE, 0 },
  { IN_UP, 3047, 3408, 5, ITR_NONE, 4026 },
  { IN_UP, 3408, 4522, 9, ITR_NONE, 0 },
  { IN_UP, 4555, 4915, 5, ITR_NONE, 4026 },
  { IN_UP, 4915, 6029, 9, ITR_NONE, 0 },
  { IN_LEFT, 0, 6095, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 6095, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_seq3[] = {
  { IN_ACTION, 2982, 3572, 6, ITR_NONE, 2191 },
  { IN_RIGHT, 0, 3572, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 3473, 11, ITR_NONE, 0 },
  { IN_DOWN, 3244, 3604, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_seq4[] = {
  { IN_DOWN, 1573, 2916, 7, ITR_NONE, 1326 },
  { IN_UP, 0, 2916, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2916, 11, ITR_NONE, 0 },
  { IN_LEFT, 2261, 2916, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_seq5[] = {
  { IN_UP, 1475, 2982, 8, ITR_NONE, 915 },
  { IN_DOWN, 1671, 2982, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2982, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 2982, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_reversed_enter_room[] = {
  { IN_UP, 0, 721, 9, ITR_NONE, 0 },
  { IN_UP, 721, 1016, 5, ITR_NONE, 4750 },
  { IN_UP, 1016, 2195, 9, ITR_NONE, 0 },
  { IN_UP, 2228, 2523, 5, ITR_NONE, 4750 },
  { IN_UP, 2523, 3703, 9, ITR_NONE, 0 },
  { IN_UP, 3703, 3998, 5, ITR_NONE, 4750 },
  { IN_UP, 3998, 4391, 9, ITR_NONE, 0 },
  { IN_LEFT, 0, 4489, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 4489, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_reversed_seq2[] = {
  { IN_UP, 0, 721, 9, ITR_NONE, 0 },
  { IN_UP, 721, 1016, 5, ITR_NONE, 4750 },
  { IN_UP, 1016, 2195, 9, ITR_NONE, 0 },
  { IN_UP, 2228, 2523, 5, ITR_NONE, 4750 },
  { IN_UP, 2523, 3703, 9, ITR_NONE, 0 },
  { IN_UP, 3703, 3998, 5, ITR_NONE, 4750 },
  { IN_UP, 3998, 4391, 9, ITR_NONE, 0 },
  { IN_LEFT, 0, 4489, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 4489, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_reversed_seq3[] = {
  { IN_ACTION, 3047, 3604, 6, ITR_NONE, 2191 },
  { IN_DOWN, 3047, 3637, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2720, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 2720, 11, ITR_NONE, 0 },
  { IN_UP, 0, 3604, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_reversed_seq4[] = {
  { IN_DOWN, 2163, 2982, 7, ITR_NONE, 1326 },
  { IN_UP, 0, 2982, 11, ITR_NONE, 0 },
  { IN_RIGHT, 2163, 2982, 11, ITR_NONE, 0 },
  { IN_LEFT, 0, 2982, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_grim_reaper_reversed_seq5[] = {
  { IN_UP, 1606, 2556, 8, ITR_NONE, 915 },
  { IN_DOWN, 1606, 3015, 11, ITR_NONE, 0 },
  { IN_RIGHT, 0, 3047, 11, ITR_NONE, 0 },
  { IN_LEFT, 1606, 3015, 11, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_enter_room[] = {
  { IN_LEFT, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_UP, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_UPLEFT, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_DOWN, 459, 2130, 19, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2163, 18, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq2[] = {
  { IN_LEFT, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_UP, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_UPLEFT, 459, 2130, 5, ITR_NONE, 1939 },
  { IN_DOWN, 459, 2130, 19, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2163, 18, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq3[] = {
  { IN_RIGHT, 3572, 4424, 6, ITR_NONE, 1326 },
  { IN_LEFT, 3572, 4424, 18, ITR_NONE, 0 },
  { IN_DOWN, 3572, 4424, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq4[] = {
  { IN_RIGHT, 2654, 3473, 7, ITR_NONE, 1326 },
  { IN_LEFT, 2654, 3473, 18, ITR_NONE, 0 },
  { IN_DOWN, 2654, 3473, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq5[] = {
  { IN_RIGHT, 1212, 2064, 8, ITR_NONE, 1326 },
  { IN_LEFT, 0, 2097, 18, ITR_NONE, 0 },
  { IN_DOWN, 0, 2097, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq6[] = {
  { IN_RIGHT, 3834, 4784, 9, ITR_NONE, 1326 },
  { IN_LEFT, 3834, 4784, 18, ITR_NONE, 0 },
  { IN_DOWN, 0, 4817, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq7[] = {
  { IN_RIGHT, 1409, 2130, 10, ITR_NONE, 2191 },
  { IN_LEFT, 1409, 2130, 18, ITR_NONE, 0 },
  { IN_DOWN, 1409, 2130, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq8[] = {
  { IN_UP, 459, 2195, 11, ITR_NONE, 3255 },
  { IN_ACTION, 459, 2195, 11, ITR_NONE, 3255 },
};
static const dl_action_t ACT_lizard_king_seq9[] = {
  { IN_ACTION, 819, 1540, 12, ITR_NONE, 0 },
  { IN_DOWN, 819, 1540, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq10[] = {
  { IN_ACTION, 328, 950, 13, ITR_NONE, 0 },
  { IN_DOWN, 328, 950, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq11[] = {
  { IN_ACTION, 164, 983, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq12[] = {
  { IN_ACTION, 492, 918, 15, ITR_NONE, 0 },
  { IN_RIGHT, 0, 918, 19, ITR_NONE, 0 },
  { IN_DOWN, 492, 918, 15, ITR_NONE, 0 },
  { IN_LEFT, 492, 918, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_lizard_king_seq13[] = {
  { IN_ACTION, 557, 1081, 16, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_enter_room[] = {
  { IN_ACTION, 3965, 5931, 5, ITR_NONE, 1326 },
  { IN_UP, 4260, 5931, 15, ITR_NONE, 0 },
  { IN_DOWN, 3539, 5833, 16, ITR_NONE, 0 },
  { IN_DOWN, 5833, 5931, 17, ITR_NONE, 0 },
  { IN_RIGHT, 3965, 5931, 17, ITR_NONE, 0 },
  { IN_LEFT, 3965, 5931, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq2[] = {
  { IN_ACTION, 3965, 5931, 5, ITR_NONE, 1326 },
  { IN_UP, 4260, 5931, 15, ITR_NONE, 0 },
  { IN_DOWN, 3539, 5833, 16, ITR_NONE, 0 },
  { IN_DOWN, 5833, 5931, 17, ITR_NONE, 0 },
  { IN_RIGHT, 3965, 5931, 17, ITR_NONE, 0 },
  { IN_LEFT, 3965, 5931, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq3[] = {
  { IN_RIGHT, 2720, 3670, 17, ITR_NONE, 0 },
  { IN_DOWN, 2720, 3670, 17, ITR_NONE, 0 },
  { IN_UP, 0, 2720, 17, ITR_NONE, 0 },
  { IN_UP, 2720, 3670, 6, ITR_NONE, 1326 },
  { IN_LEFT, 0, 3703, 17, ITR_NONE, 0 },
  { IN_ACTION, 0, 3703, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq4[] = {
  { IN_UP, 295, 1606, 7, ITR_NONE, 2191 },
  { IN_UPLEFT, 295, 1606, 7, ITR_NONE, 2191 },
  { IN_ACTION, 295, 1573, 17, ITR_NONE, 0 },
  { IN_LEFT, 295, 1573, 14, ITR_NONE, 0 },
  { IN_RIGHT, 295, 1573, 16, ITR_NONE, 0 },
  { IN_DOWN, 295, 1573, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq5[] = {
  { IN_UP, 1016, 1540, 8, ITR_NONE, 2675 },
  { IN_RIGHT, 1016, 1540, 8, ITR_NONE, 2675 },
  { IN_UPRIGHT, 1016, 1540, 8, ITR_NONE, 2675 },
  { IN_DOWN, 1016, 1540, 17, ITR_NONE, 0 },
  { IN_LEFT, 1016, 1540, 16, ITR_NONE, 0 },
  { IN_ACTION, 1016, 1540, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq6[] = {
  { IN_UP, 852, 1933, 9, ITR_NONE, 1326 },
  { IN_LEFT, 852, 1933, 14, ITR_NONE, 0 },
  { IN_DOWN, 852, 1933, 15, ITR_NONE, 0 },
  { IN_RIGHT, 1278, 1933, 17, ITR_NONE, 0 },
  { IN_ACTION, 1278, 1933, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq7[] = {
  { IN_UP, 655, 1606, 10, ITR_NONE, 1326 },
  { IN_LEFT, 655, 1573, 14, ITR_NONE, 0 },
  { IN_RIGHT, 655, 1573, 16, ITR_NONE, 0 },
  { IN_DOWN, 655, 1573, 17, ITR_NONE, 0 },
  { IN_ACTION, 655, 1573, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq8[] = {
  { IN_UPLEFT, 1475, 1933, 18, ITR_NONE, 0 },
  { IN_UPLEFT, 1933, 2621, 11, ITR_NONE, 1326 },
  { IN_UP, 1475, 1933, 18, ITR_NONE, 0 },
  { IN_UP, 1933, 2621, 11, ITR_NONE, 1326 },
  { IN_LEFT, 1475, 2621, 18, ITR_NONE, 0 },
  { IN_RIGHT, 1475, 2621, 18, ITR_NONE, 0 },
  { IN_DOWN, 1475, 2621, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq9[] = {
  { IN_UP, 1114, 2720, 12, ITR_NONE, 1326 },
  { IN_RIGHT, 1114, 2753, 18, ITR_NONE, 0 },
  { IN_LEFT, 1114, 2753, 18, ITR_NONE, 0 },
  { IN_DOWN, 1114, 2753, 17, ITR_NONE, 0 },
  { IN_ACTION, 1114, 2753, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_mudmen_seq10[] = {
  { IN_UP, 2818, 4686, 13, ITR_NONE, 1326 },
  { IN_RIGHT, 2818, 4686, 13, ITR_NONE, 1326 },
  { IN_DOWN, 2818, 3998, 17, ITR_NONE, 0 },
  { IN_DOWN, 3998, 7897, 16, ITR_NONE, 0 },
  { IN_LEFT, 3998, 7897, 17, ITR_NONE, 0 },
  { IN_ACTION, 3310, 7209, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_enter_room[] = {
  { IN_LEFT, 0, 1835, 5, ITR_NONE, 1326 },
  { IN_UP, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_DOWN, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_RIGHT, 1606, 1999, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq2[] = {
  { IN_LEFT, 0, 1835, 5, ITR_NONE, 1326 },
  { IN_UP, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_DOWN, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_RIGHT, 1606, 1999, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq3[] = {
  { IN_UP, 885, 2032, 6, ITR_NONE, 1939 },
  { IN_RIGHT, 0, 2064, 15, ITR_NONE, 0 },
  { IN_LEFT, 885, 2032, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq4[] = {
  { IN_RIGHT, 1081, 2458, 7, ITR_NONE, 2191 },
  { IN_LEFT, 1507, 2425, 15, ITR_NONE, 0 },
  { IN_UP, 1081, 2458, 15, ITR_NONE, 0 },
  { IN_DOWN, 1343, 2458, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq5[] = {
  { IN_UP, 492, 1769, 8, ITR_NONE, 2675 },
  { IN_DOWN, 0, 1835, 15, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1835, 15, ITR_NONE, 0 },
  { IN_LEFT, 0, 1835, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq6[] = {
  { IN_LEFT, 655, 2097, 9, ITR_NONE, 3255 },
  { IN_RIGHT, 1114, 2130, 16, ITR_NONE, 0 },
  { IN_ACTION, 1114, 2130, 16, ITR_NONE, 0 },
  { IN_UP, 1114, 2130, 16, ITR_NONE, 0 },
  { IN_DOWN, 1114, 2130, 16, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq7[] = {
  { IN_UP, 1245, 1933, 10, ITR_NONE, 3551 },
  { IN_RIGHT, 0, 1933, 16, ITR_NONE, 0 },
  { IN_LEFT, 1245, 1933, 17, ITR_NONE, 0 },
  { IN_DOWN, 0, 1933, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq8[] = {
  { IN_ACTION, 2490, 3211, 11, ITR_NONE, 4026 },
  { IN_DOWN, 0, 3244, 15, ITR_NONE, 0 },
  { IN_LEFT, 2490, 3211, 18, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq9[] = {
  { IN_RIGHT, 721, 1475, 12, ITR_NONE, 5000 },
  { IN_DOWN, 0, 1540, 15, ITR_NONE, 0 },
  { IN_LEFT, 885, 1507, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_seq10[] = {
  { IN_UP, 754, 2064, 13, ITR_NONE, 4750 },
  { IN_RIGHT, 0, 2097, 15, ITR_NONE, 0 },
  { IN_LEFT, 0, 2097, 15, ITR_NONE, 0 },
  { IN_DOWN, 0, 2097, 16, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_enter_room[] = {
  { IN_RIGHT, 0, 1868, 5, ITR_NONE, 1939 },
  { IN_UP, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_LEFT, 1606, 1999, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq2[] = {
  { IN_RIGHT, 0, 1868, 5, ITR_NONE, 1939 },
  { IN_UP, 1606, 1999, 15, ITR_NONE, 0 },
  { IN_LEFT, 1606, 1999, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq3[] = {
  { IN_UP, 885, 2032, 6, ITR_NONE, 2191 },
  { IN_LEFT, 0, 2064, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq4[] = {
  { IN_LEFT, 1081, 2425, 7, ITR_NONE, 2675 },
  { IN_RIGHT, 1507, 2392, 15, ITR_NONE, 0 },
  { IN_UP, 1081, 2425, 15, ITR_NONE, 0 },
  { IN_DOWN, 1081, 2425, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq5[] = {
  { IN_UP, 492, 1737, 8, ITR_NONE, 3255 },
  { IN_DOWN, 0, 1737, 15, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1737, 15, ITR_NONE, 0 },
  { IN_LEFT, 0, 1737, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq6[] = {
  { IN_RIGHT, 655, 2064, 9, ITR_NONE, 3551 },
  { IN_LEFT, 1114, 2130, 16, ITR_NONE, 0 },
  { IN_UP, 1081, 2097, 16, ITR_NONE, 0 },
  { IN_DOWN, 1081, 2097, 16, ITR_NONE, 0 },
  { IN_ACTION, 1081, 2097, 16, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq7[] = {
  { IN_UP, 1212, 1933, 10, ITR_NONE, 4026 },
  { IN_LEFT, 0, 1933, 16, ITR_NONE, 0 },
  { IN_RIGHT, 1212, 1901, 17, ITR_NONE, 0 },
  { IN_DOWN, 0, 1933, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq8[] = {
  { IN_ACTION, 2490, 3211, 11, ITR_NONE, 4026 },
  { IN_DOWN, 0, 3342, 15, ITR_NONE, 0 },
  { IN_RIGHT, 2490, 3310, 18, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq9[] = {
  { IN_LEFT, 721, 1507, 12, ITR_NONE, 5000 },
  { IN_DOWN, 0, 1311, 15, ITR_NONE, 0 },
  { IN_RIGHT, 885, 1507, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_yellow_brick_road_reversed_seq10[] = {
  { IN_UP, 754, 2064, 13, ITR_NONE, 4750 },
  { IN_RIGHT, 0, 2097, 15, ITR_NONE, 0 },
  { IN_LEFT, 0, 2097, 15, ITR_NONE, 0 },
  { IN_DOWN, 0, 2097, 16, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_enter_room[] = {
  { IN_RIGHT, 2884, 4260, 5, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq2[] = {
  { IN_RIGHT, 2884, 4260, 5, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq3[] = {
  { IN_LEFT, 426, 1278, 6, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq4[] = {
  { IN_UP, 950, 1507, 7, ITR_NONE, 2191 },
};
static const dl_action_t ACT_robot_knight_seq5[] = {
  { IN_LEFT, 1147, 2163, 8, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq6[] = {
  { IN_RIGHT, 1212, 2097, 9, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq7[] = {
  { IN_LEFT, 393, 1016, 10, ITR_NONE, 1939 },
};
static const dl_action_t ACT_robot_knight_seq8[] = {
  { IN_RIGHT, 197, 754, 11, ITR_NONE, 4026 },
};
static const dl_action_t ACT_robot_knight_seq9[] = {
  { IN_UP, 1606, 2687, 14, ITR_NONE, 0 },
  { IN_ACTION, 1606, 2687, 12, ITR_NONE, 2191 },
};
static const dl_action_t ACT_robot_knight_reversed_enter_room[] = {
  { IN_LEFT, 3473, 4358, 5, ITR_NONE, 2191 },
  { IN_UP, 3473, 7766, 13, ITR_NONE, 0 },
  { IN_DOWN, 3473, 4358, 13, ITR_NONE, 0 },
  { IN_RIGHT, 3473, 4358, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq2[] = {
  { IN_LEFT, 3473, 4358, 5, ITR_NONE, 2191 },
  { IN_UP, 3473, 7766, 13, ITR_NONE, 0 },
  { IN_DOWN, 3473, 4358, 13, ITR_NONE, 0 },
  { IN_RIGHT, 3473, 4358, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq3[] = {
  { IN_RIGHT, 557, 1278, 6, ITR_NONE, 2191 },
  { IN_UP, 557, 1278, 13, ITR_NONE, 0 },
  { IN_DOWN, 557, 1278, 13, ITR_NONE, 0 },
  { IN_LEFT, 557, 1278, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq4[] = {
  { IN_UP, 950, 1573, 7, ITR_NONE, 2675 },
  { IN_DOWN, 950, 1573, 13, ITR_NONE, 0 },
  { IN_RIGHT, 950, 1573, 13, ITR_NONE, 0 },
  { IN_LEFT, 950, 1573, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq5[] = {
  { IN_RIGHT, 1245, 2130, 8, ITR_NONE, 2191 },
  { IN_UP, 1245, 2130, 13, ITR_NONE, 0 },
  { IN_DOWN, 1245, 2130, 13, ITR_NONE, 0 },
  { IN_LEFT, 1245, 2130, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq6[] = {
  { IN_LEFT, 1212, 2097, 9, ITR_NONE, 2191 },
  { IN_UP, 1212, 2097, 13, ITR_NONE, 0 },
  { IN_DOWN, 1212, 2097, 13, ITR_NONE, 0 },
  { IN_RIGHT, 1212, 2097, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq7[] = {
  { IN_RIGHT, 393, 1016, 10, ITR_NONE, 2191 },
  { IN_UP, 393, 1016, 13, ITR_NONE, 0 },
  { IN_DOWN, 393, 1016, 13, ITR_NONE, 0 },
  { IN_LEFT, 393, 1016, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq8[] = {
  { IN_LEFT, 197, 754, 11, ITR_NONE, 4750 },
  { IN_UP, 197, 754, 13, ITR_NONE, 0 },
  { IN_RIGHT, 197, 754, 13, ITR_NONE, 0 },
  { IN_DOWN, 197, 754, 13, ITR_NONE, 0 },
};
static const dl_action_t ACT_robot_knight_reversed_seq9[] = {
  { IN_UP, 1835, 2687, 14, ITR_NONE, 0 },
  { IN_ACTION, 1835, 2687, 12, ITR_NONE, 2675 },
};
static const dl_action_t ACT_fire_room_enter_room[] = {
  { IN_RIGHT, 2884, 3506, 5, ITR_NONE, 915 },
  { IN_LEFT, 2884, 3473, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_fire_room_seq2[] = {
  { IN_RIGHT, 2884, 3506, 5, ITR_NONE, 915 },
  { IN_LEFT, 2884, 3473, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_fire_room_seq3[] = {
  { IN_UP, 0, 328, 9, ITR_NONE, 0 },
  { IN_UP, 1147, 2097, 6, ITR_NONE, 1326 },
  { IN_LEFT, 0, 328, 9, ITR_NONE, 0 },
  { IN_LEFT, 1147, 2097, 6, ITR_NONE, 1326 },
  { IN_DOWN, 1147, 2195, 10, ITR_NONE, 0 },
  { IN_RIGHT, 1147, 2195, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_fire_room_seq4[] = {
  { IN_LEFT, 655, 1966, 7, ITR_NONE, 915 },
  { IN_UP, 655, 1966, 9, ITR_NONE, 0 },
  { IN_DOWN, 655, 1966, 7, ITR_NONE, 915 },
  { IN_DOWNLEFT, 655, 1966, 7, ITR_NONE, 915 },
  { IN_RIGHT, 0, 1966, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_fire_room_seq5[] = {
  { IN_LEFT, 197, 1638, 8, ITR_NONE, 1326 },
  { IN_UP, 0, 1638, 9, ITR_NONE, 0 },
  { IN_DOWN, 0, 1638, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 1638, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_enter_room[] = {
  { IN_ACTION, 2228, 3080, 5, ITR_NONE, 915 },
  { IN_LEFT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_UP, 1507, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 2654, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_seq2[] = {
  { IN_ACTION, 2228, 3080, 5, ITR_NONE, 915 },
  { IN_LEFT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_UP, 1507, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 2654, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_seq3[] = {
  { IN_ACTION, 1933, 2458, 6, ITR_NONE, 1939 },
  { IN_UP, 1147, 2425, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 1147, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2490, 10, ITR_NONE, 0 },
  { IN_LEFT, 0, 2490, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_seq4[] = {
  { IN_LEFT, 0, 950, 10, ITR_NONE, 0 },
  { IN_LEFT, 950, 2687, 7, ITR_NONE, 1326 },
  { IN_DOWN, 0, 2720, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2720, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_seq5[] = {
  { IN_ACTION, 1049, 1835, 8, ITR_NONE, 1326 },
};
static const dl_action_t ACT_smithee_seq6[] = {
  { IN_ACTION, 5210, 6259, 9, ITR_NONE, 915 },
  { IN_RIGHT, 5210, 6259, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_enter_room[] = {
  { IN_ACTION, 1475, 2195, 11, ITR_NONE, 0 },
  { IN_ACTION, 2195, 3113, 5, ITR_NONE, 1326 },
  { IN_LEFT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_UP, 1507, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 2654, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_seq2[] = {
  { IN_ACTION, 1475, 2195, 11, ITR_NONE, 0 },
  { IN_ACTION, 2195, 3113, 5, ITR_NONE, 1326 },
  { IN_LEFT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 3113, 10, ITR_NONE, 0 },
  { IN_UP, 1507, 3178, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 2654, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_seq3[] = {
  { IN_ACTION, 852, 1933, 12, ITR_NONE, 0 },
  { IN_ACTION, 1933, 2458, 6, ITR_NONE, 2191 },
  { IN_UP, 1147, 2425, 10, ITR_NONE, 0 },
  { IN_DOWN, 0, 1147, 10, ITR_NONE, 0 },
  { IN_RIGHT, 0, 2490, 10, ITR_NONE, 0 },
  { IN_LEFT, 0, 2490, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_seq4[] = {
  { IN_RIGHT, 0, 950, 10, ITR_NONE, 0 },
  { IN_RIGHT, 950, 2720, 7, ITR_NONE, 1326 },
  { IN_DOWN, 0, 2753, 10, ITR_NONE, 0 },
  { IN_LEFT, 0, 2753, 10, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_seq5[] = {
  { IN_ACTION, 229, 983, 14, ITR_NONE, 0 },
  { IN_ACTION, 983, 1802, 8, ITR_NONE, 1326 },
  { IN_UP, 1049, 1868, 14, ITR_NONE, 0 },
  { IN_DOWN, 1049, 1868, 14, ITR_NONE, 0 },
  { IN_LEFT, 1049, 1868, 14, ITR_NONE, 0 },
  { IN_RIGHT, 1049, 1868, 14, ITR_NONE, 0 },
};
static const dl_action_t ACT_smithee_reversed_seq6[] = {
  { IN_ACTION, 5177, 6226, 9, ITR_NONE, 915 },
  { IN_RIGHT, 5210, 6259, 15, ITR_NONE, 0 },
  { IN_LEFT, 5210, 6259, 15, ITR_NONE, 0 },
};
static const dl_action_t ACT_snake_room_enter_room[] = {
  { IN_ACTION, 1966, 2687, 5, ITR_NONE, 495 },
  { IN_LEFT, 1966, 2687, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_snake_room_seq2[] = {
  { IN_ACTION, 1966, 2687, 5, ITR_NONE, 495 },
  { IN_LEFT, 1966, 2687, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_snake_room_seq3[] = {
  { IN_ACTION, 918, 1638, 6, ITR_NONE, 2675 },
  { IN_RIGHT, 918, 1638, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_snake_room_seq4[] = {
  { IN_ACTION, 1049, 1868, 10, ITR_NONE, 49 },
  { IN_LEFT, 1016, 1835, 9, ITR_NONE, 0 },
};
static const dl_action_t ACT_snake_room_seq9[] = {
  { IN_UP, 0, 721, 7, ITR_NONE, 1939 },
  { IN_UPRIGHT, 0, 721, 7, ITR_NONE, 1939 },
  { IN_RIGHT, 0, 721, 7, ITR_NONE, 1939 },
};
static const dl_action_t ACT_the_dragons_lair_enter_room[] = {
  { IN_LEFT, 3932, 4850, 5, ITR_NONE, 1326 },
};
static const dl_action_t ACT_the_dragons_lair_seq2[] = {
  { IN_LEFT, 3932, 4850, 5, ITR_NONE, 1326 },
};
static const dl_action_t ACT_the_dragons_lair_seq3[] = {
  { IN_LEFT, 10584, 11141, 6, ITR_NONE, 1939 },
  { IN_UP, 10584, 11141, 22, ITR_NONE, 0 },
  { IN_RIGHT, 10584, 11141, 22, ITR_NONE, 0 },
  { IN_ACTION, 10584, 11141, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq4[] = {
  { IN_LEFT, 6980, 8585, 7, ITR_NONE, 1326 },
};
static const dl_action_t ACT_the_dragons_lair_seq5[] = {
  { IN_DOWNLEFT, 22020, 22807, 8, ITR_NONE, 2191 },
  { IN_LEFT, 22020, 22807, 8, ITR_NONE, 2191 },
  { IN_DOWN, 22020, 22807, 8, ITR_NONE, 2191 },
  { IN_ACTION, 22020, 22807, 17, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq6[] = {
  { IN_UP, 6554, 7373, 19, ITR_NONE, 0 },
  { IN_DOWN, 6554, 7373, 9, ITR_NONE, 2191 },
  { IN_RIGHT, 6554, 7373, 20, ITR_NONE, 0 },
  { IN_LEFT, 6554, 7373, 9, ITR_NONE, 2191 },
  { IN_DOWNLEFT, 6554, 7373, 9, ITR_NONE, 2191 },
  { IN_ACTION, 6554, 7373, 20, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq7[] = {
  { IN_DOWN, 4653, 5308, 22, ITR_NONE, 0 },
  { IN_RIGHT, 4653, 5308, 10, ITR_NONE, 3255 },
  { IN_LEFT, 4653, 5308, 22, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq8[] = {
  { IN_RIGHT, 2654, 3670, 11, ITR_NONE, 2191 },
  { IN_UP, 2654, 3670, 11, ITR_NONE, 2191 },
  { IN_UPRIGHT, 2654, 3670, 11, ITR_NONE, 2191 },
};
static const dl_action_t ACT_the_dragons_lair_seq9[] = {
  { IN_ACTION, 393, 1475, 12, ITR_NONE, 3551 },
  { IN_RIGHT, 393, 1475, 12, ITR_NONE, 3551 },
};
static const dl_action_t ACT_the_dragons_lair_seq10[] = {
  { IN_ACTION, 2687, 3768, 13, ITR_NONE, 4026 },
  { IN_UP, 2687, 3768, 19, ITR_NONE, 0 },
  { IN_DOWN, 2687, 3768, 19, ITR_NONE, 0 },
  { IN_RIGHT, 2687, 3768, 19, ITR_NONE, 0 },
  { IN_LEFT, 2687, 3768, 19, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq11[] = {
  { IN_LEFT, 2425, 3899, 14, ITR_NONE, 4750 },
  { IN_UP, 2425, 3899, 14, ITR_NONE, 4750 },
  { IN_UPLEFT, 2425, 3899, 14, ITR_NONE, 4750 },
  { IN_RIGHT, 2425, 3899, 21, ITR_NONE, 0 },
};
static const dl_action_t ACT_the_dragons_lair_seq12[] = {
  { IN_ACTION, 1114, 2228, 15, ITR_NONE, 5000 },
};

static const dl_sequence_t SEQ_attract_mode[] = {
  { "start_alive", -1, 0, 0, 0, 2, ITR_NONE, 0, 0, 0 },
  { "start_dead", 5830, 1, 0, 3000, 2, ITR_NONE, 0, 0, 0 },
  { "attract_movie", 7000, 0, 0, 43000, 3, ITR_NONE, 0, ACT_attract_mode_attract_movie, 1 },
  { "insert_coins", 6200, 1, 0, 5000, 2, ITR_NONE, 0, ACT_attract_mode_insert_coins, 1 },
};
static const dl_sequence_t SEQ_introduction[] = {
  { "start_dead", 50718, 0, 0, 2032, 2, ITR_NONE, 0, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 2, ITR_NONE, 0, 0, 0 },
  { "castle_exterior", 53096, 0, 0, 5767, 3, ITR_NONE, 0, 0, 0 },
  { "exit_room", 69654, 0, 0, 2776, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_flaming_ropes[] = {
  { "game_over", 160495, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 139891, 0, 0, 2167, 3, ITR_NONE, 0, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 142227, 0, 0, 2228, 4, ITR_NONE, 0, ACT_flaming_ropes_enter_room, 5 },
  { "platform_sliding", -1, 0, 0, 2621, 11, ITR_NONE, 0, ACT_flaming_ropes_platform_sliding, 5 },
  { "rope1", 147732, 0, 0, 2228, 10, ITR_NONE, 0, ACT_flaming_ropes_rope1, 5 },
  { "rope2", -1, 0, 0, 2228, 10, ITR_NONE, 0, ACT_flaming_ropes_rope2, 5 },
  { "rope3", -1, 0, 0, 1507, 9, ITR_NONE, 0, ACT_flaming_ropes_rope3, 5 },
  { "exit_room", -1, 0, 0, 792, -1, ITR_NONE, 0, 0, 0 },
  { "misses_landing", 155490, 0, 1, 1917, -1, ITR_NONE, 0, 0, 0 },
  { "burns_hands", 157408, 0, 0, 1583, 11, ITR_NONE, 0, 0, 0 },
  { "fall_to_death", 158993, 0, 1, 1417, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_bower[] = {
  { "game_over", 385220, 0, 0, 3650, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 372957, 0, 0, 2366, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 376002, 0, 0, 1773, 4, ITR_NONE, 0, ACT_bower_enter_room, 1 },
  { "trapped_in_wall", 381007, 0, 1, 1792, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", -1, 0, 0, 3051, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_alice_room[] = {
  { "game_over", 766226, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 753880, 0, 0, 2334, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 756257, 0, 0, 2022, 5, ITR_NONE, 0, ACT_alice_room_enter_room, 4 },
  { "drinks_potion", 760220, 0, 1, 4086, -1, ITR_NONE, 0, 0, 0 },
  { "burned_to_death", 764724, 0, 1, 1375, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", -1, 0, 0, 1943, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_wind_room[] = {
  { "game_over", 369495, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 354606, 0, 0, 2376, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 356941, 0, 0, 8159, 4, ITR_NONE, 0, ACT_wind_room_enter_room, 2 },
  { "sucked_in", 366492, 0, 1, 3002, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", -1, 0, 0, 557, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_vestibule[] = {
  { "game_over", 86045, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 163998, 0, 0, 2084, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 72407, 0, 0, 3998, 5, ITR_NONE, 0, ACT_vestibule_enter_room, 4 },
  { "stagger", -1, 0, 0, 668, 5, ITR_NONE, 0, ACT_vestibule_stagger, 3 },
  { "fell_to_death", 80665, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", -1, 0, 0, 1922, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_falling_platform_short[] = {
  { "game_over", 639641, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 610612, 0, 0, 2376, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 612989, 0, 0, 6881, 4, ITR_NONE, 0, ACT_falling_platform_short_enter_room, 9 },
  { "crash_landing", 628755, 0, 1, 3335, -1, ITR_NONE, 0, 0, 0 },
  { "missed_jump", 632091, 0, 1, 2501, -1, ITR_NONE, 0, 0, 0 },
  { "fell_to_death", 633426, 0, 1, 1166, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", 634594, 0, 0, 5003, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_falling_platform_long[] = {
  { "game_over", 639641, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 610612, 0, 0, 2376, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 612989, 0, 0, 6816, 4, ITR_NONE, 49, ACT_falling_platform_long_enter_room, 9 },
  { "second_jump_set", -1, 0, 0, 4293, 5, ITR_NONE, 1939, ACT_falling_platform_long_second_jump_set, 9 },
  { "third_jump_set", -1, 0, 0, 4293, 6, ITR_NONE, 0, ACT_falling_platform_long_third_jump_set, 10 },
  { "crash_landing", -1, 0, 1, 2032, -1, ITR_NONE, 0, 0, 0 },
  { "missed_jump", 632091, 0, 1, 2501, -1, ITR_NONE, 0, 0, 0 },
  { "fell_to_death", 633426, 0, 1, 1166, -1, ITR_NONE, 0, 0, 0 },
  { "exit_room", 634594, 0, 0, 5070, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_crypt_creeps[] = {
  { "game_over", 495830, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 470555, 0, 0, 2334, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 472891, 0, 0, 3244, 11, ITR_NONE, 0, ACT_crypt_creeps_enter_room, 5 },
  { "jumped_skulls", -1, 0, 0, 1311, 12, ITR_NONE, 0, ACT_crypt_creeps_jumped_skulls, 4 },
  { "attacked_first_hand", -1, 0, 0, 2195, 13, ITR_NONE, 0, ACT_crypt_creeps_attacked_first_hand, 4 },
  { "jumped_slime", -1, 0, 0, 1212, 12, ITR_NONE, 0, ACT_crypt_creeps_jumped_slime, 4 },
  { "attacked_second_hand", -1, 0, 0, 1835, 13, ITR_NONE, 0, ACT_crypt_creeps_attacked_second_hand, 3 },
  { "enter_crypt", -1, 0, 0, 1835, 14, ITR_NONE, 0, ACT_crypt_creeps_enter_crypt, 5 },
  { "exit_room", -1, 0, 0, 4257, -1, ITR_NONE, 0, 0, 0 },
  { "overpowered_by_skulls", 489240, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "eaten_by_skulls", 490199, 0, 1, 541, -1, ITR_NONE, 0, 0, 0 },
  { "crushed_by_hand", 490742, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
  { "eaten_by_slime", 491701, 0, 1, 1375, -1, ITR_NONE, 0, 0, 0 },
  { "captured_by_ghouls", 493494, 0, 1, 2292, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_flying_horse[] = {
  { "game_over", 435812, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 409327, 0, 0, 2084, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 411704, 0, 0, 4522, 11, ITR_NONE, 0, ACT_flying_horse_enter_room, 3 },
  { "second_fire", -1, 0, 0, 1376, 11, ITR_NONE, 0, ACT_flying_horse_second_fire, 3 },
  { "third_fire", -1, 0, 0, 1835, 11, ITR_NONE, 0, ACT_flying_horse_third_fire, 3 },
  { "fourth_fire", -1, 0, 0, 1966, 11, ITR_NONE, 0, ACT_flying_horse_fourth_fire, 3 },
  { "brick_wall", -1, 0, 0, 1868, 12, ITR_NONE, 0, ACT_flying_horse_brick_wall, 3 },
  { "fifth_fire", -1, 0, 0, 1409, 11, ITR_NONE, 0, ACT_flying_horse_fifth_fire, 3 },
  { "exit_room", -1, 0, 0, 5041, -1, ITR_NONE, 0, 0, 0 },
  { "burned_to_death", 434352, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "hit_pillar", 429681, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "hit_brick_wall", 431683, 0, 1, 2292, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_giddy_goons[] = {
  { "game_over", 252212, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 228396, 0, 0, 2084, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 230732, 0, 0, 3146, 8, ITR_NONE, 0, ACT_giddy_goons_enter_room, 4 },
  { "kills_first_goon", -1, 0, 0, 1835, 8, ITR_NONE, 0, ACT_giddy_goons_kills_first_goon, 4 },
  { "climbs_stairs", -1, 0, 0, 2163, 12, ITR_NONE, 0, ACT_giddy_goons_climbs_stairs, 5 },
  { "kill_upper_goons", -1, 0, 0, 1606, 7, ITR_NONE, 0, ACT_giddy_goons_kill_upper_goons, 4 },
  { "exit_room", -1, 0, 0, 1842, -1, ITR_NONE, 0, 0, 0 },
  { "knife_in_back", 245580, 0, 1, 2084, -1, ITR_NONE, 0, 0, 0 },
  { "shoves_off_edge", 247749, 0, 1, 2720, -1, ITR_NONE, 0, 0, 0 },
  { "fall_to_death", 250752, 0, 1, 1041, -1, ITR_NONE, 0, 0, 0 },
  { "fight_off_one_before_swarm", 241743, 0, 1, 3544, -1, ITR_NONE, 0, 0, 0 },
  { "swarm_of_goons", 244579, 0, 1, 708, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_tentacle_room[] = {
  { "game_over", 116910, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 89507, 0, 0, 2084, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 91843, 0, 0, 3965, 10, ITR_NONE, 0, ACT_tentacle_room_enter_room, 1 },
  { "kills_first_tentacle", -1, 0, 0, 2359, 11, ITR_NONE, 0, ACT_tentacle_room_kills_first_tentacle, 1 },
  { "jump_to_weapon_rack", -1, 0, 0, 1901, 11, ITR_NONE, 0, ACT_tentacle_room_jump_to_weapon_rack, 4 },
  { "jump_to_door", -1, 0, 0, 1442, 13, ITR_NONE, 0, ACT_tentacle_room_jump_to_door, 5 },
  { "jump_to_stairs", -1, 0, 0, 2720, 12, ITR_NONE, 0, ACT_tentacle_room_jump_to_stairs, 5 },
  { "jump_to_table", -1, 0, 0, 2228, 11, ITR_NONE, 0, ACT_tentacle_room_jump_to_table, 5 },
  { "exit_room", -1, 0, 0, 1066, -1, ITR_NONE, 0, 0, 0 },
  { "left_tentacle_grabs", 107525, 0, 1, 2918, -1, ITR_NONE, 0, 0, 0 },
  { "squeeze_to_death", 110528, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "two_front_war", 112530, 0, 1, 3002, -1, ITR_NONE, 0, 0, 0 },
  { "squeeze_to_death_by_door", 116034, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_tilting_room[] = {
  { "game_over", 850184, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 833293, 0, 0, 2251, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 835670, 0, 0, 4456, 7, ITR_NONE, 0, ACT_tilting_room_enter_room, 3 },
  { "jumps_back", -1, 0, 0, 1573, 8, ITR_NONE, 0, ACT_tilting_room_jumps_back, 4 },
  { "jumps_forward", -1, 0, 0, 786, 8, ITR_NONE, 0, ACT_tilting_room_jumps_forward, 4 },
  { "exit_room", -1, 0, 0, 1400, -1, ITR_NONE, 0, 0, 0 },
  { "catches_fire", 846639, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "falls_to_death", 848141, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "wrong_door", 843887, 0, 1, 2710, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_throne_room[] = {
  { "game_over", 872624, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 853646, 0, 0, 2334, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 856024, 0, 0, 2753, 9, ITR_NONE, 0, ACT_throne_room_enter_room, 4 },
  { "first_jump", -1, 0, 0, 688, 9, ITR_NONE, 0, ACT_throne_room_first_jump, 4 },
  { "second_jump", -1, 0, 0, 885, 9, ITR_NONE, 0, ACT_throne_room_second_jump, 4 },
  { "on_throne", -1, 0, 0, 4293, 10, ITR_NONE, 0, ACT_throne_room_on_throne, 3 },
  { "exit_room", -1, 0, 0, 1932, -1, ITR_NONE, 0, 0, 0 },
  { "electrified_sword", 866576, 0, 1, 2835, -1, ITR_NONE, 0, 0, 0 },
  { "electrified_floor", 869579, 0, 1, 1041, -1, ITR_NONE, 0, 0, 0 },
  { "electrified_throne", 870830, 0, 1, 1750, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_underground_river[] = {
  { "game_over", 1004672, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 939732, 0, 0, 2334, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 942068, 0, 0, 2654, 4, ITR_NONE, 0, 0, 0 },
  { "first_boulders", 950326, 0, 0, 1016, 18, ITR_NONE, 0, ACT_underground_river_first_boulders, 3 },
  { "second_boulders", -1, 0, 0, 1933, 19, ITR_NONE, 0, ACT_underground_river_second_boulders, 3 },
  { "third_boulders", -1, 0, 0, 2032, 20, ITR_NONE, 0, ACT_underground_river_third_boulders, 3 },
  { "fourth_boulders", -1, 0, 0, 1966, 21, ITR_NONE, 0, ACT_underground_river_fourth_boulders, 3 },
  { "first_rapids", -1, 0, 0, 5210, 22, ITR_NONE, 0, ACT_underground_river_first_rapids, 5 },
  { "second_rapids", -1, 0, 0, 2523, 22, ITR_NONE, 0, ACT_underground_river_second_rapids, 5 },
  { "third_rapids", -1, 0, 0, 2490, 22, ITR_NONE, 0, ACT_underground_river_third_rapids, 5 },
  { "fourth_rapids", -1, 0, 0, 2818, 22, ITR_NONE, 0, ACT_underground_river_fourth_rapids, 5 },
  { "first_whirlpools", -1, 0, 0, 5505, 23, ITR_NONE, 0, ACT_underground_river_first_whirlpools, 3 },
  { "second_whirlpools", -1, 0, 0, 2720, 23, ITR_NONE, 0, ACT_underground_river_second_whirlpools, 3 },
  { "third_whirlpools", -1, 0, 0, 2490, 23, ITR_NONE, 0, ACT_underground_river_third_whirlpools, 3 },
  { "fourth_whirlpools", -1, 0, 0, 2720, 23, ITR_NONE, 0, ACT_underground_river_fourth_whirlpools, 3 },
  { "bounce_to_chain", -1, 0, 0, 5177, 24, ITR_NONE, 0, ACT_underground_river_bounce_to_chain, 4 },
  { "exit_room", -1, 0, 0, 3065, -1, ITR_NONE, 0, 0, 0 },
  { "boulders_crash", 992118, 0, 1, 1000, -1, ITR_NONE, 0, 0, 0 },
  { "boulders_crash2", 993119, 0, 1, 541, -1, ITR_NONE, 0, 0, 0 },
  { "boulders_crash3", 994120, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
  { "boulders_crash4", 995121, 0, 1, 541, -1, ITR_NONE, 0, 0, 0 },
  { "rapids_crash", 996122, 0, 1, 2376, -1, ITR_NONE, 0, 0, 0 },
  { "whirlpools_crash", 998625, 0, 1, 2668, -1, ITR_NONE, 0, 0, 0 },
  { "miss_chain", 1002503, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_rolling_balls[] = {
  { "game_over", 1104731, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 1079873, 0, 0, 2334, 3, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 1082250, 0, 0, 5964, 12, ITR_NONE, 0, ACT_rolling_balls_enter_room, 3 },
  { "red_ball", -1, 0, 0, 1868, 12, ITR_NONE, 0, ACT_rolling_balls_red_ball, 3 },
  { "blue_ball", -1, 0, 0, 1966, 12, ITR_NONE, 0, ACT_rolling_balls_blue_ball, 3 },
  { "green_ball", -1, 0, 0, 1966, 12, ITR_NONE, 0, ACT_rolling_balls_green_ball, 3 },
  { "orange_ball", -1, 0, 0, 1966, 12, ITR_NONE, 0, ACT_rolling_balls_orange_ball, 3 },
  { "purple_ball", -1, 0, 0, 1901, 12, ITR_NONE, 0, ACT_rolling_balls_purple_ball, 3 },
  { "pit_in_ground", -1, 0, 0, 1901, 12, ITR_NONE, 0, ACT_rolling_balls_pit_in_ground, 1 },
  { "exit_room", -1, 0, 0, 2320, -1, ITR_NONE, 0, 0, 0 },
  { "small_ball_crushes", 1103688, 0, 1, 1041, -1, ITR_NONE, 0, 0, 0 },
  { "big_ball_crushes", 1102979, 0, 1, 749, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_black_knight[] = {
  { "game_over", 1076286, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 1056432, 0, 0, 2042, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 1058768, 0, 0, 3539, 9, ITR_NONE, 0, ACT_black_knight_enter_room, 3 },
  { "seq2", 1058768, 0, 0, 3539, 9, ITR_NONE, 0, ACT_black_knight_seq2, 3 },
  { "seq3", -1, 0, 0, 3867, 10, ITR_NONE, 0, ACT_black_knight_seq3, 3 },
  { "seq4", -1, 0, 0, 3244, 10, ITR_NONE, 0, ACT_black_knight_seq4, 4 },
  { "seq5", -1, 0, 0, 2445, -1, ITR_NONE, 0, 0, 0 },
  { "seq6", 1071864, 0, 1, 1417, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 1073866, 0, 1, 2376, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 1074701, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_bubbling_cauldron[] = {
  { "game_over", 224809, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 205039, 0, 0, 2084, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 207375, 0, 0, 2753, 11, ITR_NONE, 0, ACT_bubbling_cauldron_enter_room, 4 },
  { "seq2", 207375, 0, 0, 2753, 11, ITR_NONE, 0, ACT_bubbling_cauldron_seq2, 4 },
  { "seq3", -1, 0, 0, 2523, 10, ITR_NONE, 0, ACT_bubbling_cauldron_seq3, 5 },
  { "seq4", -1, 0, 0, 1409, 11, ITR_NONE, 0, ACT_bubbling_cauldron_seq4, 5 },
  { "seq5", -1, 0, 0, 1737, 12, ITR_NONE, 0, ACT_bubbling_cauldron_seq5, 4 },
  { "seq6", -1, 0, 0, 2949, 11, ITR_NONE, 0, ACT_bubbling_cauldron_seq6, 3 },
  { "seq7", -1, 0, 0, 1140, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 219888, 0, 1, 1417, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 221389, 0, 1, 1417, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", 223641, 0, 1, 1125, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_catwalk_bats[] = {
  { "game_over", 518645, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 499751, 0, 0, 2042, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 502128, 0, 0, 2687, 11, ITR_NONE, 0, ACT_catwalk_bats_enter_room, 3 },
  { "seq2", 502128, 0, 0, 2687, 11, ITR_NONE, 0, ACT_catwalk_bats_seq2, 3 },
  { "seq3", -1, 0, 0, 1147, 11, ITR_NONE, 0, ACT_catwalk_bats_seq3, 5 },
  { "seq4", -1, 0, 0, 2490, 10, ITR_NONE, 0, ACT_catwalk_bats_seq4, 5 },
  { "seq5", -1, 0, 0, 1475, 11, ITR_NONE, 0, ACT_catwalk_bats_seq5, 5 },
  { "seq6", -1, 0, 0, 1212, 11, ITR_NONE, 0, ACT_catwalk_bats_seq6, 4 },
  { "seq7", -1, 0, 0, 2958, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 516601, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 514098, 0, 1, 2501, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_crypt_creeps_reversed[] = {
  { "game_over", 795463, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 769729, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 772065, 0, 0, 3473, 12, ITR_NONE, 0, ACT_crypt_creeps_reversed_enter_room, 5 },
  { "seq2", 772065, 0, 0, 3473, 12, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq2, 5 },
  { "seq3", -1, 0, 0, 918, 13, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq3, 4 },
  { "seq4", -1, 0, 0, 2228, 14, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq4, 4 },
  { "seq5", -1, 0, 0, 1147, 13, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq5, 4 },
  { "seq6", -1, 0, 0, 1835, 14, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq6, 3 },
  { "seq7", -1, 0, 0, 1835, 15, ITR_NONE, 0, ACT_crypt_creeps_reversed_seq7, 4 },
  { "seq8", -1, 0, 0, 4453, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 788414, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", 789374, 0, 1, 541, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 789916, 0, 1, 582, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 790917, 0, 1, 1333, -1, ITR_NONE, 0, 0, 0 },
  { "seq13", 792418, 0, 1, 2543, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_electric_cage_and_geyser[] = {
  { "game_over", 1126419, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 1108276, 0, 0, 2292, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 1110570, 0, 0, 2916, 9, ITR_NONE, 0, ACT_electric_cage_and_geyser_enter_room, 4 },
  { "seq2", 1110570, 0, 0, 2916, 9, ITR_NONE, 0, ACT_electric_cage_and_geyser_seq2, 4 },
  { "seq3", -1, 0, 0, 1507, 9, ITR_NONE, 0, ACT_electric_cage_and_geyser_seq3, 8 },
  { "seq4", 1115366, 0, 0, 5374, 10, ITR_NONE, 0, ACT_electric_cage_and_geyser_seq4, 6 },
  { "seq5", 1120872, 0, 0, 714, -1, ITR_NONE, 0, 0, 0 },
  { "seq6", 1121915, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 1123374, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 1124918, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_falling_platform_long_reversed[] = {
  { "game_over", 935812, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 907283, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 909577, 0, 0, 9241, 5, ITR_NONE, 124, ACT_falling_platform_long_reversed_enter_room, 16 },
  { "seq2", 909577, 0, 0, 9241, 5, ITR_NONE, 124, ACT_falling_platform_long_reversed_seq2, 16 },
  { "seq3", -1, 0, 0, 1802, 6, ITR_NONE, 2191, ACT_falling_platform_long_reversed_seq3, 8 },
  { "seq4", -1, 0, 0, 5702, 7, ITR_NONE, 0, ACT_falling_platform_long_reversed_seq4, 17 },
  { "seq5", -1, 0, 1, 1671, -1, ITR_NONE, 0, 0, 0 },
  { "seq6", 930056, 0, 1, 819, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 931224, 0, 0, 5070, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 928721, 0, 1, 1041, 8, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_flaming_ropes_reversed[] = {
  { "game_over", 542752, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 522106, 0, 0, 2084, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 524442, 0, 0, 2523, 12, ITR_NONE, 0, ACT_flaming_ropes_reversed_enter_room, 5 },
  { "seq2", 524442, 0, 0, 2523, 12, ITR_NONE, 0, ACT_flaming_ropes_reversed_seq2, 5 },
  { "seq3", 529948, 0, 0, 1583, 10, ITR_NONE, 0, ACT_flaming_ropes_reversed_seq3, 5 },
  { "seq4", -1, 0, 0, 1573, 10, ITR_NONE, 0, ACT_flaming_ropes_reversed_seq4, 5 },
  { "seq5", -1, 0, 0, 1501, 9, ITR_NONE, 0, ACT_flaming_ropes_reversed_seq5, 5 },
  { "seq6", -1, 0, 0, 2057, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 537622, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 539624, 0, 1, 3085, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 541209, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", -1, 0, 0, 2621, 11, ITR_NONE, 0, ACT_flaming_ropes_reversed_seq10, 4 },
};
static const dl_sequence_t SEQ_flattening_staircase[] = {
  { "game_over", 278363, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 255757, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 259594, 0, 0, 2425, 9, ITR_NONE, 0, ACT_flattening_staircase_enter_room, 1 },
  { "seq2", 259594, 0, 0, 2425, 9, ITR_NONE, 0, ACT_flattening_staircase_seq2, 1 },
  { "seq3", -1, 0, 0, 2228, 10, ITR_NONE, 0, ACT_flattening_staircase_seq3, 1 },
  { "seq4", -1, 0, 0, 1507, 7, ITR_NONE, 915, ACT_flattening_staircase_seq4, 4 },
  { "seq5", -1, 0, 0, 3211, 9, ITR_NONE, 0, ACT_flattening_staircase_seq5, 4 },
  { "seq6", -1, 0, 0, 2014, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 270939, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 272941, 0, 1, 1166, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 274442, 0, 1, 4044, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 274442, 0, 1, 4044, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_flying_horse_reversed[] = {
  { "game_over", 707917, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 681391, 0, 0, 2209, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 683726, 0, 0, 4522, 11, ITR_NONE, 0, ACT_flying_horse_reversed_enter_room, 7 },
  { "seq2", 683726, 0, 0, 4522, 11, ITR_NONE, 0, ACT_flying_horse_reversed_seq2, 7 },
  { "seq3", -1, 0, 0, 1278, 11, ITR_NONE, 0, ACT_flying_horse_reversed_seq3, 5 },
  { "seq4", -1, 0, 0, 1966, 11, ITR_NONE, 0, ACT_flying_horse_reversed_seq4, 5 },
  { "seq5", -1, 0, 0, 1966, 11, ITR_NONE, 0, ACT_flying_horse_reversed_seq5, 5 },
  { "seq6", -1, 0, 0, 1802, 12, ITR_NONE, 0, ACT_flying_horse_reversed_seq6, 4 },
  { "seq7", -1, 0, 0, 1475, 11, ITR_NONE, 0, ACT_flying_horse_reversed_seq7, 5 },
  { "seq8", -1, 0, 0, 5008, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 701744, 0, 1, 1834, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", 703746, 0, 1, 2692, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 706416, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_giant_bat[] = {
  { "game_over", 607150, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 587255, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 591259, 0, 0, 1016, 10, ITR_NONE, 0, ACT_giant_bat_enter_room, 2 },
  { "seq2", 591259, 0, 0, 1016, 10, ITR_NONE, 0, ACT_giant_bat_seq2, 2 },
  { "seq3", -1, 0, 0, 1409, 12, ITR_NONE, 0, ACT_giant_bat_seq3, 2 },
  { "seq4", -1, 0, 0, 2359, 11, ITR_NONE, 0, ACT_giant_bat_seq4, 5 },
  { "seq5", -1, 0, 0, 2359, 13, ITR_NONE, 0, ACT_giant_bat_seq5, 3 },
  { "seq6", -1, 0, 0, 1442, 9, ITR_NONE, 49, 0, 0 },
  { "seq7", -1, 0, 0, 1757, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 603104, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 605106, 0, 0, 754, 12, ITR_NONE, 0, 0, 0 },
  { "seq10", 605940, 0, 1, 1208, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 601603, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_grim_reaper[] = {
  { "game_over", 351102, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 320238, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 327537, 0, 0, 5800, 9, ITR_NONE, 0, ACT_grim_reaper_enter_room, 9 },
  { "seq2", 327537, 0, 0, 5800, 9, ITR_NONE, 0, ACT_grim_reaper_seq2, 9 },
  { "seq3", 333668, 0, 0, 3604, 10, ITR_NONE, 0, ACT_grim_reaper_seq3, 4 },
  { "seq4", -1, 0, 0, 2916, 11, ITR_NONE, 0, ACT_grim_reaper_seq4, 4 },
  { "seq5", -1, 0, 0, 2982, 11, ITR_NONE, 0, ACT_grim_reaper_seq5, 4 },
  { "seq6", -1, 0, 0, 673, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 343845, 0, 1, 2292, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 347181, 0, 1, 2418, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 349601, 0, 1, 1458, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_grim_reaper_reversed[] = {
  { "game_over", 829789, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 798925, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 807850, 0, 0, 4227, 9, ITR_NONE, 0, ACT_grim_reaper_reversed_enter_room, 9 },
  { "seq2", 807850, 0, 0, 4227, 9, ITR_NONE, 0, ACT_grim_reaper_reversed_seq2, 9 },
  { "seq3", 812355, 0, 0, 3604, 10, ITR_NONE, 0, ACT_grim_reaper_reversed_seq3, 5 },
  { "seq4", -1, 0, 0, 2982, 11, ITR_NONE, 0, ACT_grim_reaper_reversed_seq4, 4 },
  { "seq5", -1, 0, 0, 2589, 11, ITR_NONE, 0, ACT_grim_reaper_reversed_seq5, 4 },
  { "seq6", -1, 0, 0, 875, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 822532, 0, 1, 2292, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 825785, 0, 1, 2459, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 828288, 0, 1, 1458, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_lizard_king[] = {
  { "game_over", 750376, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 711421, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 713756, 0, 0, 2163, 17, ITR_NONE, 0, ACT_lizard_king_enter_room, 5 },
  { "seq2", 713756, 0, 0, 2163, 17, ITR_NONE, 0, ACT_lizard_king_seq2, 5 },
  { "seq3", -1, 0, 0, 4456, 19, ITR_NONE, 0, ACT_lizard_king_seq3, 3 },
  { "seq4", -1, 0, 0, 3506, 19, ITR_NONE, 0, ACT_lizard_king_seq4, 3 },
  { "seq5", -1, 0, 0, 2097, 19, ITR_NONE, 0, ACT_lizard_king_seq5, 3 },
  { "seq6", -1, 0, 0, 4817, 19, ITR_NONE, 0, ACT_lizard_king_seq6, 3 },
  { "seq7", -1, 0, 0, 2163, 19, ITR_NONE, 0, ACT_lizard_king_seq7, 3 },
  { "seq8", -1, 0, 0, 2228, 19, ITR_NONE, 0, ACT_lizard_king_seq8, 2 },
  { "seq9", -1, 0, 0, 1573, 17, ITR_NONE, 0, ACT_lizard_king_seq9, 2 },
  { "seq10", -1, 0, 0, 950, 13, ITR_NONE, 0, ACT_lizard_king_seq10, 2 },
  { "seq11", -1, 0, 0, 1016, 14, ITR_NONE, 0, ACT_lizard_king_seq11, 1 },
  { "seq12", -1, 0, 0, 918, 15, ITR_NONE, 0, ACT_lizard_king_seq12, 4 },
  { "seq13", -1, 0, 0, 1114, 16, ITR_NONE, 0, ACT_lizard_king_seq13, 1 },
  { "seq14", -1, 0, 0, 5113, -1, ITR_NONE, 0, 0, 0 },
  { "seq15", 745955, 0, 1, 1000, -1, ITR_NONE, 0, 0, 0 },
  { "seq16", 746956, 0, 1, 3419, -1, ITR_NONE, 0, 0, 0 },
  { "seq17", 747874, 0, 1, 2501, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_mudmen[] = {
  { "game_over", 1052929, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 1008134, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 1010470, 0, 0, 5964, 17, ITR_NONE, 0, ACT_mudmen_enter_room, 6 },
  { "seq2", 1010470, 0, 0, 5964, 17, ITR_NONE, 0, ACT_mudmen_seq2, 6 },
  { "seq3", -1, 0, 0, 3703, 6, ITR_NONE, 1326, ACT_mudmen_seq3, 6 },
  { "seq4", -1, 0, 0, 1606, 17, ITR_NONE, 0, ACT_mudmen_seq4, 6 },
  { "seq5", -1, 0, 0, 1540, 17, ITR_NONE, 0, ACT_mudmen_seq5, 6 },
  { "seq6", -1, 0, 0, 1933, 17, ITR_NONE, 0, ACT_mudmen_seq6, 5 },
  { "seq7", -1, 0, 0, 1638, 17, ITR_NONE, 0, ACT_mudmen_seq7, 5 },
  { "seq8", -1, 0, 0, 2654, 18, ITR_NONE, 0, ACT_mudmen_seq8, 7 },
  { "seq9", -1, 0, 0, 2720, 17, ITR_NONE, 0, ACT_mudmen_seq9, 5 },
  { "seq10", -1, 0, 0, 4391, 17, ITR_NONE, 0, ACT_mudmen_seq10, 6 },
  { "seq11", -1, 0, 0, 3421, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 1044504, 0, 1, 4420, -1, ITR_NONE, 0, 0, 0 },
  { "seq13", 1040500, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
  { "seq14", 1051427, 0, 1, 1500, -1, ITR_NONE, 0, 0, 0 },
  { "seq15", 1048925, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq16", 1042502, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_yellow_brick_road[] = {
  { "game_over", 201452, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 163998, 0, 0, 2084, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 166334, 0, 0, 1868, 15, ITR_NONE, 0, ACT_yellow_brick_road_enter_room, 4 },
  { "seq2", 166334, 0, 0, 1868, 15, ITR_NONE, 0, ACT_yellow_brick_road_seq2, 4 },
  { "seq3", -1, 0, 0, 2064, 14, ITR_NONE, 0, ACT_yellow_brick_road_seq3, 3 },
  { "seq4", -1, 0, 0, 2458, 15, ITR_NONE, 0, ACT_yellow_brick_road_seq4, 4 },
  { "seq5", -1, 0, 0, 1835, 15, ITR_NONE, 0, ACT_yellow_brick_road_seq5, 4 },
  { "seq6", -1, 0, 0, 2130, 16, ITR_NONE, 0, ACT_yellow_brick_road_seq6, 5 },
  { "seq7", -1, 0, 0, 1933, 17, ITR_NONE, 0, ACT_yellow_brick_road_seq7, 4 },
  { "seq8", -1, 0, 0, 3244, 18, ITR_NONE, 0, ACT_yellow_brick_road_seq8, 3 },
  { "seq9", -1, 0, 0, 1540, 15, ITR_NONE, 0, ACT_yellow_brick_road_seq9, 3 },
  { "seq10", -1, 0, 0, 2097, 19, ITR_NONE, 0, ACT_yellow_brick_road_seq10, 4 },
  { "seq11", -1, 0, 0, 1225, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 187188, 0, 1, 1041, -1, ITR_NONE, 0, 0, 0 },
  { "seq14", 190191, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq15", 192193, 0, 1, 3335, -1, ITR_NONE, 0, 0, 0 },
  { "seq16", 195530, 0, 1, 1083, -1, ITR_NONE, 0, 0, 0 },
  { "seq17", 197031, 0, 1, 1834, -1, ITR_NONE, 0, 0, 0 },
  { "seq18", 199033, 0, 1, 2543, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_yellow_brick_road_reversed[] = {
  { "game_over", 583793, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 546214, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 548550, 0, 0, 1868, 15, ITR_NONE, 0, ACT_yellow_brick_road_reversed_enter_room, 3 },
  { "seq2", 548550, 0, 0, 1868, 15, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq2, 3 },
  { "seq3", -1, 0, 0, 2064, 14, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq3, 2 },
  { "seq4", -1, 0, 0, 2163, 15, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq4, 4 },
  { "seq5", -1, 0, 0, 1737, 15, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq5, 4 },
  { "seq6", -1, 0, 0, 2097, 16, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq6, 5 },
  { "seq7", -1, 0, 0, 1933, 17, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq7, 4 },
  { "seq8", -1, 0, 0, 3244, 18, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq8, 3 },
  { "seq9", -1, 0, 0, 1540, 15, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq9, 3 },
  { "seq10", -1, 0, 0, 2097, 19, ITR_NONE, 0, ACT_yellow_brick_road_reversed_seq10, 4 },
  { "seq11", -1, 0, 0, 1693, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 569404, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
  { "seq14", 572407, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "seq15", 574409, 0, 1, 3293, -1, ITR_NONE, 0, 0, 0 },
  { "seq16", 577745, 0, 1, 1083, -1, ITR_NONE, 0, 0, 0 },
  { "seq17", 579247, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "seq18", 581249, 0, 1, 2376, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_robot_knight[] = {
  { "game_over", 466676, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 439357, 0, 0, 2167, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 441693, 0, 0, 4293, 13, ITR_NONE, 0, ACT_robot_knight_enter_room, 1 },
  { "seq2", 441693, 0, 0, 4293, 13, ITR_NONE, 0, ACT_robot_knight_seq2, 1 },
  { "seq3", -1, 0, 0, 1311, 13, ITR_NONE, 0, ACT_robot_knight_seq3, 1 },
  { "seq4", -1, 0, 0, 1540, 13, ITR_NONE, 0, ACT_robot_knight_seq4, 1 },
  { "seq5", -1, 0, 0, 2163, 13, ITR_NONE, 0, ACT_robot_knight_seq5, 1 },
  { "seq6", -1, 0, 0, 2130, 13, ITR_NONE, 0, ACT_robot_knight_seq6, 1 },
  { "seq7", -1, 0, 0, 1049, 13, ITR_NONE, 0, ACT_robot_knight_seq7, 1 },
  { "seq8", -1, 0, 0, 754, 13, ITR_NONE, 0, ACT_robot_knight_seq8, 1 },
  { "seq9", -1, 0, 0, 2687, 14, ITR_NONE, 0, ACT_robot_knight_seq9, 2 },
  { "seq10", -1, 0, 0, 3441, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 463715, 0, 1, 1875, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 465717, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_robot_knight_reversed[] = {
  { "game_over", 903780, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 876085, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 878421, 0, 0, 4391, 13, ITR_NONE, 0, ACT_robot_knight_reversed_enter_room, 4 },
  { "seq2", 878421, 0, 0, 4391, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq2, 4 },
  { "seq3", -1, 0, 0, 1311, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq3, 4 },
  { "seq4", -1, 0, 0, 1606, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq4, 4 },
  { "seq5", -1, 0, 0, 2163, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq5, 4 },
  { "seq6", -1, 0, 0, 2130, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq6, 4 },
  { "seq7", -1, 0, 0, 1049, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq7, 4 },
  { "seq8", -1, 0, 0, 754, 13, ITR_NONE, 0, ACT_robot_knight_reversed_seq8, 4 },
  { "seq9", -1, 0, 0, 2720, 14, ITR_NONE, 0, ACT_robot_knight_reversed_seq9, 2 },
  { "seq10", -1, 0, 0, 3203, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 900443, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 902445, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_fire_room[] = {
  { "game_over", 405782, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 388806, 0, 0, 2167, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 391142, 0, 0, 3539, 10, ITR_NONE, 0, ACT_fire_room_enter_room, 2 },
  { "seq2", 391142, 0, 0, 3539, 10, ITR_NONE, 0, ACT_fire_room_seq2, 2 },
  { "seq3", -1, 0, 0, 2163, 10, ITR_NONE, 0, ACT_fire_room_seq3, 6 },
  { "seq4", -1, 0, 0, 1966, 10, ITR_NONE, 0, ACT_fire_room_seq4, 5 },
  { "seq5", -1, 0, 0, 1638, 10, ITR_NONE, 0, ACT_fire_room_seq5, 4 },
  { "seq6", -1, 0, 0, 2913, -1, ITR_NONE, 0, 0, 0 },
  { "seq7", 403321, 0, 1, 1180, 10, ITR_NONE, 0, 0, 0 },
  { "seq8", 404822, 0, 1, 1000, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_smithee[] = {
  { "game_over", 316734, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 281950, 0, 0, 2376, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 285411, 0, 0, 3113, 11, ITR_NONE, 0, ACT_smithee_enter_room, 5 },
  { "seq2", 285411, 0, 0, 3113, 11, ITR_NONE, 0, ACT_smithee_seq2, 5 },
  { "seq3", -1, 0, 0, 2490, 12, ITR_NONE, 0, ACT_smithee_seq3, 5 },
  { "seq4", -1, 0, 0, 2720, 13, ITR_NONE, 0, ACT_smithee_seq4, 4 },
  { "seq5", -1, 0, 0, 1835, 14, ITR_NONE, 0, ACT_smithee_seq5, 1 },
  { "seq6", -1, 0, 0, 6259, 15, ITR_NONE, 0, ACT_smithee_seq6, 2 },
  { "seq7", -1, 0, 0, 4269, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 306057, 0, 1, 1458, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 307559, 0, 1, 833, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", 308560, 0, 1, 1875, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 311646, 0, 1, 1083, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 312730, 0, 1, 958, -1, ITR_NONE, 0, 0, 0 },
  { "seq13", 314065, 0, 1, 2626, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_smithee_reversed[] = {
  { "game_over", 677929, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 643102, 0, 0, 2084, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 646564, 0, 0, 3178, 11, ITR_NONE, 0, ACT_smithee_reversed_enter_room, 6 },
  { "seq2", 646564, 0, 0, 3178, 11, ITR_NONE, 0, ACT_smithee_reversed_seq2, 6 },
  { "seq3", -1, 0, 0, 2490, 12, ITR_NONE, 0, ACT_smithee_reversed_seq3, 6 },
  { "seq4", -1, 0, 0, 2753, 13, ITR_NONE, 0, ACT_smithee_reversed_seq4, 4 },
  { "seq5", -1, 0, 0, 1802, 14, ITR_NONE, 0, ACT_smithee_reversed_seq5, 6 },
  { "seq6", -1, 0, 0, 6226, 15, ITR_NONE, 0, ACT_smithee_reversed_seq6, 3 },
  { "seq7", -1, 0, 0, 3987, -1, ITR_NONE, 0, 0, 0 },
  { "seq8", 667210, 0, 1, 1417, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", 668711, 0, 1, 791, -1, ITR_NONE, 0, 0, 0 },
  { "seq10", 669712, 0, 1, 1542, -1, ITR_NONE, 0, 0, 0 },
  { "seq11", 672799, 0, 1, 1125, -1, ITR_NONE, 0, 0, 0 },
  { "seq12", 673883, 0, 1, 1041, -1, ITR_NONE, 0, 0, 0 },
  { "seq13", 675260, 0, 1, 2668, -1, ITR_NONE, 0, 0, 0 },
};
static const dl_sequence_t SEQ_snake_room[] = {
  { "game_over", 135970, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 120538, 0, 0, 2376, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 122874, 0, 0, 2720, 9, ITR_NONE, 0, ACT_snake_room_enter_room, 2 },
  { "seq2", 122874, 0, 0, 2720, 9, ITR_NONE, 0, ACT_snake_room_seq2, 2 },
  { "seq3", -1, 0, 0, 1671, 9, ITR_NONE, 0, ACT_snake_room_seq3, 2 },
  { "seq4", -1, 0, 0, 1835, 10, ITR_NONE, 49, ACT_snake_room_seq4, 2 },
  { "seq5", -1, 0, 0, 3145, -1, ITR_NONE, 0, 0, 0 },
  { "seq6", 133384, 0, 0, 1671, 9, ITR_NONE, 0, 0, 0 },
  { "seq7", 135386, 0, 1, 874, -1, ITR_NONE, 0, 0, 0 },
  { "seq9", -1, 0, 0, 721, 8, ITR_NONE, 0, ACT_snake_room_seq9, 3 },
};
static const dl_sequence_t SEQ_the_dragons_lair[] = {
  { "game_over", 1307642, 0, 0, 3503, -1, ITR_GAME_OVER_COMPLETE, 0, 0, 0 },
  { "start_dead", 1198324, 0, 0, 2334, 4, ITR_NONE, 49, 0, 0 },
  { "start_alive", -1, 0, 0, 0, 3, ITR_NONE, 49, 0, 0 },
  { "enter_room", 1200660, 0, 0, 4882, 22, ITR_NONE, 0, ACT_the_dragons_lair_enter_room, 1 },
  { "seq2", 1200660, 0, 0, 4882, 22, ITR_NONE, 0, ACT_the_dragons_lair_seq2, 1 },
  { "seq3", -1, 0, 0, 11207, 22, ITR_NONE, 0, ACT_the_dragons_lair_seq3, 4 },
  { "seq4", -1, 0, 0, 8618, 22, ITR_NONE, 0, ACT_the_dragons_lair_seq4, 1 },
  { "seq5", -1, 0, 0, 22872, 17, ITR_NONE, 0, ACT_the_dragons_lair_seq5, 4 },
  { "seq6", -1, 0, 0, 7406, 20, ITR_NONE, 0, ACT_the_dragons_lair_seq6, 6 },
  { "seq7", -1, 0, 0, 5341, 22, ITR_NONE, 0, ACT_the_dragons_lair_seq7, 3 },
  { "seq8", -1, 0, 0, 3670, 22, ITR_NONE, 0, ACT_the_dragons_lair_seq8, 3 },
  { "seq9", -1, 0, 0, 1475, 18, ITR_NONE, 0, ACT_the_dragons_lair_seq9, 2 },
  { "seq10", -1, 0, 0, 3768, 19, ITR_NONE, 0, ACT_the_dragons_lair_seq10, 5 },
  { "seq11", -1, 0, 0, 3899, 21, ITR_NONE, 0, ACT_the_dragons_lair_seq11, 4 },
  { "seq12", -1, 0, 0, 2228, 19, ITR_NONE, 0, ACT_the_dragons_lair_seq12, 1 },
  { "seq13", -1, 0, 0, 18501, 16, ITR_NONE, 0, 0, 0 },
  { "endgame", 1294087, 1, 0, 10000, -1, ITR_NONE, 0, 0, 0 },
  { "seq14", 1296589, 0, 1, 2501, -1, ITR_NONE, 0, 0, 0 },
  { "seq15", 1299092, 0, 1, 2001, -1, ITR_NONE, 0, 0, 0 },
  { "seq16", 1301427, 0, 1, 1583, -1, ITR_NONE, 0, 0, 0 },
  { "seq17", 1303096, 0, 1, 2543, -1, ITR_NONE, 0, 0, 0 },
  { "seq18", 1305598, 0, 1, 2042, -1, ITR_NONE, 0, 0, 0 },
  { "seq19", -1, 0, 1, 1638, -1, ITR_NONE, 0, 0, 0 },
};

static const dl_scene_t DL_SCENES[] = {
  { "attract_mode", SEQ_attract_mode, 4 },
  { "introduction", SEQ_introduction, 4 },
  { "flaming_ropes", SEQ_flaming_ropes, 12 },
  { "bower", SEQ_bower, 6 },
  { "alice_room", SEQ_alice_room, 7 },
  { "wind_room", SEQ_wind_room, 6 },
  { "vestibule", SEQ_vestibule, 7 },
  { "falling_platform_short", SEQ_falling_platform_short, 8 },
  { "falling_platform_long", SEQ_falling_platform_long, 10 },
  { "crypt_creeps", SEQ_crypt_creeps, 15 },
  { "flying_horse", SEQ_flying_horse, 13 },
  { "giddy_goons", SEQ_giddy_goons, 13 },
  { "tentacle_room", SEQ_tentacle_room, 14 },
  { "tilting_room", SEQ_tilting_room, 10 },
  { "throne_room", SEQ_throne_room, 11 },
  { "underground_river", SEQ_underground_river, 25 },
  { "rolling_balls", SEQ_rolling_balls, 13 },
  { "black_knight", SEQ_black_knight, 11 },
  { "bubbling_cauldron", SEQ_bubbling_cauldron, 13 },
  { "catwalk_bats", SEQ_catwalk_bats, 12 },
  { "crypt_creeps_reversed", SEQ_crypt_creeps_reversed, 16 },
  { "electric_cage_and_geyser", SEQ_electric_cage_and_geyser, 11 },
  { "falling_platform_long_reversed", SEQ_falling_platform_long_reversed, 11 },
  { "flaming_ropes_reversed", SEQ_flaming_ropes_reversed, 13 },
  { "flattening_staircase", SEQ_flattening_staircase, 13 },
  { "flying_horse_reversed", SEQ_flying_horse_reversed, 14 },
  { "giant_bat", SEQ_giant_bat, 14 },
  { "grim_reaper", SEQ_grim_reaper, 12 },
  { "grim_reaper_reversed", SEQ_grim_reaper_reversed, 12 },
  { "lizard_king", SEQ_lizard_king, 20 },
  { "mudmen", SEQ_mudmen, 19 },
  { "yellow_brick_road", SEQ_yellow_brick_road, 20 },
  { "yellow_brick_road_reversed", SEQ_yellow_brick_road_reversed, 20 },
  { "robot_knight", SEQ_robot_knight, 15 },
  { "robot_knight_reversed", SEQ_robot_knight_reversed, 15 },
  { "fire_room", SEQ_fire_room, 11 },
  { "smithee", SEQ_smithee, 16 },
  { "smithee_reversed", SEQ_smithee_reversed, 16 },
  { "snake_room", SEQ_snake_room, 11 },
  { "the_dragons_lair", SEQ_the_dragons_lair, 23 },
};
static const int DL_N_SCENES = 40;

static const char* DL_ROWS[13][3] = {
  { "flaming_ropes", "flaming_ropes_reversed", "bower" },
  { "flying_horse", "flying_horse_reversed", "alice_room" },
  { "crypt_creeps", "crypt_creeps_reversed", "underground_river" },
  { "falling_platform_short", "falling_platform_short", "vestibule" },
  { "rolling_balls", "electric_cage_and_geyser", "black_knight" },
  { "grim_reaper", "grim_reaper_reversed", "lizard_king" },
  { "smithee", "smithee_reversed", "wind_room" },
  { "tentacle_room", "snake_room", "bubbling_cauldron" },
  { "flattening_staircase", "giddy_goons", "fire_room" },
  { "yellow_brick_road", "yellow_brick_road_reversed", "catwalk_bats" },
  { "robot_knight", "robot_knight_reversed", "giant_bat" },
  { "throne_room", "tilting_room", "mudmen" },
  { "falling_platform_long", "falling_platform_long_reversed", "the_dragons_lair" },
};
static const int DL_N_ROWS = 13;

