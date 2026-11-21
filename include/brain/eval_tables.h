#ifndef VEXAPH_BRAIN_EVAL_TABLES_H
#define VEXAPH_BRAIN_EVAL_TABLES_H

#include <stdint.h>

#define BACKWARD_OPEN_EXTRA_EG 4
#define BACKWARD_OPEN_EXTRA_MG 6
extern const int BACKWARD_PAWN_EG[8];
extern const int BACKWARD_PAWN_MG[8];
#define BAD_BISHOP_EG 5
#define BAD_BISHOP_MG 3
extern const int BISHOP_MOBILITY_EG[14];
extern const int BISHOP_MOBILITY_MG[14];
#define BISHOP_OUTPOST_EG 10
#define BISHOP_OUTPOST_MG 15
#define BISHOP_PAIR_EG 50
#define BISHOP_PAIR_MG 30
extern const int CANDIDATE_EG[8];
extern const int CANDIDATE_MG[8];
extern const uint8_t CENTER_DISTANCE[64];
extern const int DOUBLED_PAWN_EG[8];
extern const int DOUBLED_PAWN_MG[8];
extern const int EG_BISHOP_TABLE[64];
extern const int EG_KING_TABLE[64];
extern const int EG_KNIGHT_TABLE[64];
extern const int EG_PAWN_TABLE[64];
extern const int EG_QUEEN_TABLE[64];
extern const int EG_ROOK_TABLE[64];
extern const int EG_VALUE[6];
extern const int GAME_PHASE_INC[6];
extern const int IMB_OURS[6][6];
#define IMB_SCALE_DEN 256
#define IMB_SCALE_NUM 4
extern const int IMB_THEIRS[6][6];
#define ISOLATED_OPEN_EXTRA_EG 4
#define ISOLATED_OPEN_EXTRA_MG 8
extern const int ISOLATED_PAWN_EG[8];
extern const int ISOLATED_PAWN_MG[8];
#define KD_SAFE_CHECK_B 5
#define KD_SAFE_CHECK_N 7
#define KD_SAFE_CHECK_Q 10
#define KD_SAFE_CHECK_R 8
#define KD_WEAK_SQUARE 3
extern const int KING_ATTACK_WEIGHT[6];
#define KING_OPEN_FILE_PENALTY_MG 18
extern const int KING_SAFETY_TABLE[100];
#define KING_SEMI_OPEN_FILE_PENALTY_MG 8
extern const int KNIGHT_MOBILITY_EG[9];
extern const int KNIGHT_MOBILITY_MG[9];
#define KNIGHT_OUTPOST_EG 20
#define KNIGHT_OUTPOST_MG 30
extern const int MG_BISHOP_TABLE[64];
extern const int MG_KING_TABLE[64];
extern const int MG_KNIGHT_TABLE[64];
extern const int MG_PAWN_TABLE[64];
extern const int MG_QUEEN_TABLE[64];
extern const int MG_ROOK_TABLE[64];
extern const int MG_VALUE[6];
extern const int PASSED_KING_PROXIMITY_WEIGHT[8];
extern const int PASSED_PAWN_EG[8];
extern const int PASSED_PAWN_MG[8];
extern const int PASSED_PROTECTED_EG[8];
extern const int PASSED_PROTECTED_MG[8];
extern const int PAWN_SHIELD_MG[7];
extern const int PHALANX_EG[8];
extern const int PHALANX_MG[8];
extern const int QUEEN_MOBILITY_EG[28];
extern const int QUEEN_MOBILITY_MG[28];
extern const int ROOK_MOBILITY_EG[15];
extern const int ROOK_MOBILITY_MG[15];
#define ROOK_OPEN_FILE_EG 10
#define ROOK_OPEN_FILE_MG 25
#define ROOK_SEMI_OPEN_FILE_EG 8
#define ROOK_SEMI_OPEN_FILE_MG 12
#define ROOK_SEVENTH_EG 30
#define ROOK_SEVENTH_MG 15
#define SCALE_BASE_PAWNS 36
#define SCALE_NORMAL 64
#define SCALE_NO_PAWNS_ROOK_EDGE 24
#define SCALE_NO_PAWNS_SMALL_EDGE 12
#define SCALE_ONE_PAWN 48
#define SCALE_OPPOSITE_BISHOPS 36
#define SCALE_PER_PAWN 7
#define SCALE_ROOK_FLANK 36
extern const int SHELTER_BY_DIST[8];
#define SHELTER_MISSING -10
#define STORM_BLOCKED_SHIFT 2
extern const int STORM_BY_DIST[8];
extern const int SUPPORTED_EG[8];
extern const int SUPPORTED_MG[8];
#define TEMPO_BONUS 12
#define THREAT_MINOR_MAJOR_EG 24
#define THREAT_MINOR_MAJOR_MG 30
#define THREAT_PUSH_EG 12
#define THREAT_PUSH_MG 18
#define THREAT_ROOK_QUEEN_EG 20
#define THREAT_ROOK_QUEEN_MG 26
#define TOTAL_PHASE 24

extern int MG_TABLE[12][64];
extern int EG_TABLE[12][64];
extern const int *const MG_PST[6];
extern const int *const EG_PST[6];
void init_eval_tables(void);

#endif
