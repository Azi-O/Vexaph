#ifndef VEXAPH_BRAIN_MOVEPICK_H
#define VEXAPH_BRAIN_MOVEPICK_H

#include "core/board.h"

#define MAX_HISTORY 16384
#define SCORE_TT_MOVE (1 << 28)
#define SCORE_GOOD_CAPTURE (1 << 24)
#define SCORE_KILLER_1 (1 << 21)
#define SCORE_KILLER_2 ((1 << 21) - 1)
#define SCORE_COUNTERMOVE (1 << 20)
#define SCORE_BAD_CAPTURE (-(1 << 24))

enum { ST_TT = 0, ST_CAP = 1, ST_QUIET = 2, ST_BAD = 3, ST_DONE = 4 };

#define CORR_SIZE 16384
#define CORR_GRAIN 16
#define CORR_LIMIT (150 * CORR_GRAIN)

typedef struct {
  int16_t history[2 * 64 * 64];
  int16_t cap_history[12 * 64 * 6];
  int16_t *cont_history;
  int32_t counter_moves[12 * 64];
  int32_t killers[MAX_PLY * 2 + 4];
  int16_t corr[2 * CORR_SIZE];
} HistoryTables;

void hist_init(HistoryTables *h);
void hist_free(HistoryTables *h);
void hist_clear(HistoryTables *h);
void hist_clear_killers(HistoryTables *h);
void hist_age(HistoryTables *h);

int hist_gravity(int entry, int bonus);
void hist_update_history(HistoryTables *h, int side, int move, int bonus);
int hist_get_history(const HistoryTables *h, int side, int move);
void hist_update_cap(HistoryTables *h, int move, int victim_pt, int bonus);
int hist_get_cap(const HistoryTables *h, int move, int victim_pt);
void hist_update_cont(HistoryTables *h, int prev_piece, int prev_to, int move, int bonus);
int hist_get_cont(const HistoryTables *h, int prev_piece, int prev_to, int move);
void hist_store_killer(HistoryTables *h, int ply, int move);
void hist_set_counter_move(HistoryTables *h, int prev_piece, int prev_to, int move);
int hist_get_counter_move(const HistoryTables *h, int prev_piece, int prev_to);
int hist_corr_index(int side, U64 pawn_key);
int hist_get_corr(const HistoryTables *h, int idx);
void hist_update_corr(HistoryTables *h, int idx, int diff, int depth);

int history_bonus(int depth);
int history_malus(int depth);

typedef struct {
  MoveList list;
  int32_t caps[MAX_MOVES];
  int32_t cap_scores[MAX_MOVES];
  int32_t quiets[MAX_MOVES];
  int32_t quiet_scores[MAX_MOVES];
  int32_t bad[MAX_MOVES];
  int32_t bad_scores[MAX_MOVES];
  int n_caps, cap_i;
  int n_quiets, quiet_i;
  bool quiets_scored;
  int n_bad, bad_i;
  int stage;
  int tt_move;
  bool tt_ok;
  bool skip_bad;
  const Board *board;
  const HistoryTables *hist;
  int side;
  int k1, k2, counter;
  int p1p, p1t, p2p, p2t;
  int score;
  int stage_of;
  int see;
  bool see_known;
  int hist_score;
} MovePicker;

void picker_init(MovePicker *p, const Board *board, int tt_move, int ply, const HistoryTables *hist,
                 int prev1_piece, int prev1_to, int prev2_piece, int prev2_to, bool captures_only);
int picker_next(MovePicker *p);

#endif
