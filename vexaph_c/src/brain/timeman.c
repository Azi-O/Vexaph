#include <math.h>
#include "brain/timeman.h"
#include "core/types.h"
#include "platform/platform.h"

#define START_FRACTION 0.6
#define OVERRUN 2.4

static double clampd(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }
static double maxd(double a, double b) { return a > b ? a : b; }
static double mind(double a, double b) { return a < b ? a : b; }

void tm_reset(TimeManager *t) {
  t->start_time = now_ms();
  t->opt_time = 0;
  t->max_time = 0;
  t->base_opt_time = 0;
  t->infinite = true;
  t->fixed_movetime = false;
  t->move_overhead = MOVE_OVERHEAD_MS;
  t->prev_best_move = 0;
  t->best_move_changes = 0;
  t->stable_iterations = 0;
  t->prev_score = 0;
  t->has_prev_score = false;
  t->last_search_score = 0;
  t->has_last_search = false;
  t->extension_factor = 1.0;
  t->panic = false;
}

void tm_init(TimeManager *t, const TimeParams *params, int side, int overhead) {
  double keep_score = t->last_search_score;
  bool keep_has = t->has_last_search;
  tm_reset(t);
  t->last_search_score = keep_score;
  t->has_last_search = keep_has;
  t->move_overhead = overhead;
  t->start_time = now_ms();

  if (params->infinite) {
    t->infinite = true;
    return;
  }

  if (params->has_movetime && params->movetime > 0) {
    double v = maxd(1, params->movetime - overhead);
    t->opt_time = v;
    t->max_time = v;
    t->base_opt_time = v;
    t->infinite = false;
    t->fixed_movetime = true;
    return;
  }

  bool has_left = side == WHITE ? params->has_wtime : params->has_btime;
  double time_left = side == WHITE ? params->wtime : params->btime;
  double inc = side == WHITE ? params->winc : params->binc;

  if (!has_left || time_left <= 0) {
    t->infinite = true;
    return;
  }

  t->infinite = false;

  double fullmove = params->fullmove > 0 ? params->fullmove : 20;
  double mtg;
  if (params->movestogo > 0) mtg = mind(params->movestogo, 50);
  else mtg = clampd(floor(48 - 0.55 * fullmove + 0.5), 20, 42);

  double phase = params->phase;
  double phase_scale = 0.88 + 0.30 * clampd(phase / 24, 0, 1);

  double usable = maxd(1, time_left - overhead * 2);

  double opt = (usable / mtg) * phase_scale + inc * 0.75;
  if (inc > 0 && time_left > inc * 20) opt += inc * 0.15;
  opt = mind(opt, usable * 0.35);
  double max = mind(opt * 4.5, usable * 0.75);
  if (max < opt) max = opt;

  t->base_opt_time = maxd(1, opt);
  t->opt_time = t->base_opt_time;
  t->max_time = maxd(1, max);
}

double tm_elapsed(const TimeManager *t) { return now_ms() - t->start_time; }

bool tm_hard_limit_reached(const TimeManager *t) {
  if (t->infinite || t->max_time <= 0) return false;
  double e = tm_elapsed(t);
  if (e >= t->max_time) return true;
  if (t->fixed_movetime || t->panic) return false;
  return e >= mind(t->max_time, t->opt_time * OVERRUN);
}

bool tm_soft_limit_reached(const TimeManager *t) {
  if (t->infinite || t->opt_time <= 0) return false;
  if (t->fixed_movetime) return tm_elapsed(t) >= t->opt_time;
  return tm_elapsed(t) >= t->opt_time * START_FRACTION;
}

void tm_update_iteration(TimeManager *t, int depth, int best_move, int score, const TimeInfo *info) {
  if (t->infinite || t->fixed_movetime) {
    t->prev_best_move = best_move;
    t->prev_score = score;
    t->has_prev_score = true;
    return;
  }

  double factor = 1.0;

  if (depth >= 4) {
    if (best_move != t->prev_best_move) {
      t->best_move_changes = t->best_move_changes * 0.5 + 1;
      t->stable_iterations = 0;
    } else {
      t->best_move_changes *= 0.5;
      t->stable_iterations++;
    }
    factor *= 1.0 + mind(1.4, 0.6 * t->best_move_changes);

    if (t->stable_iterations >= 8) factor *= 0.70;
    else if (t->stable_iterations >= 5) factor *= 0.80;
    else if (t->stable_iterations >= 3) factor *= 0.90;

    if (t->has_prev_score) {
      double last = t->has_last_search ? t->last_search_score : t->prev_score;
      double falling = 1.0 + 0.010 * (last - score) + 0.006 * (t->prev_score - score);
      factor *= clampd(falling, 0.80, 1.60);
    }

    if (info && depth >= 8 && info->has_effort) {
      if (info->effort > 0.95) factor *= 0.70;
      else if (info->effort > 0.88) factor *= 0.82;
      else if (info->effort < 0.40) factor *= 1.12;
    }

    if (info && info->has_root_eval && depth >= 6) {
      double c = fabs(score - info->root_eval);
      factor *= 1.0 + mind(0.25, c / 1200);
    }

    double a = fabs((double)score);
    if (a >= 900 && t->stable_iterations >= 2) factor *= 0.80;
  }

  t->extension_factor = factor;
  t->opt_time = mind(t->max_time, t->base_opt_time * factor);

  t->prev_best_move = best_move;
  t->prev_score = score;
  t->has_prev_score = true;
}

void tm_finish_search(TimeManager *t, int score) {
  t->last_search_score = score;
  t->has_last_search = true;
}
