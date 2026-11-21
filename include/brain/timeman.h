#ifndef VEXAPH_BRAIN_TIMEMAN_H
#define VEXAPH_BRAIN_TIMEMAN_H

#include <stdbool.h>
#include <stdint.h>

#define MOVE_OVERHEAD_MS 30

typedef struct {
  int depth;
  int64_t nodes;
  int64_t movetime;
  int64_t wtime, btime, winc, binc;
  int movestogo;
  bool has_depth, has_nodes, has_movetime, has_wtime, has_btime, has_winc, has_binc, has_movestogo;
  bool infinite;
} SearchLimits;

typedef struct {
  bool infinite;
  bool has_movetime;
  double movetime;
  bool has_wtime, has_btime;
  double wtime, btime, winc, binc;
  double movestogo;
  double fullmove;
  double phase;
} TimeParams;

typedef struct {
  double start_time;
  double opt_time;
  double max_time;
  double base_opt_time;
  bool infinite;
  bool fixed_movetime;
  int move_overhead;
  int prev_best_move;
  double best_move_changes;
  int stable_iterations;
  double prev_score;
  bool has_prev_score;
  double last_search_score;
  bool has_last_search;
  double extension_factor;
  bool panic;
} TimeManager;

typedef struct {
  bool has_effort;
  double effort;
  bool has_root_eval;
  double root_eval;
} TimeInfo;

void tm_reset(TimeManager *t);
void tm_init(TimeManager *t, const TimeParams *params, int side, int overhead);
double tm_elapsed(const TimeManager *t);
bool tm_hard_limit_reached(const TimeManager *t);
bool tm_soft_limit_reached(const TimeManager *t);
void tm_update_iteration(TimeManager *t, int depth, int best_move, int score, const TimeInfo *info);
void tm_finish_search(TimeManager *t, int score);

#endif
