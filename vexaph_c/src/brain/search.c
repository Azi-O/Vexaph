#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brain/search.h"
#include "brain/eval.h"
#include "brain/nnue.h"
#include "brain/eval_pawn.h"
#include "brain/see.h"
#include "core/movegen.h"
#include "platform/platform.h"
#ifndef __EMSCRIPTEN__
#include <pthread.h>
#define VEXAPH_SMP 1
#endif

static int8_t lmr_table[64 * 64];
static int16_t LMP_LIMIT[2][16];

#define ASPIRATION_START_DEPTH 5
#define DELTA_MARGIN 200
#define PROBCUT_MARGIN 190
#define PROBCUT_IMPROVING 55
#define SINGULAR_MIN_DEPTH 7
#define MAX_DOUBLE_EXT 6

void init_search(void) {
  static bool done = false;
  if (done) return;
  for (int d = 1; d < 64; d++)
    for (int m = 1; m < 64; m++)
      lmr_table[d * 64 + m] = (int8_t)floor(0.75 + (log((double)d) * log((double)m)) / 2.25);
  for (int d = 0; d < 16; d++) {
    LMP_LIMIT[1][d] = (int16_t)(3 + d * d);
    LMP_LIMIT[0][d] = (int16_t)((3 + d * d) >> 1);
  }
  done = true;
}

static void ss_reset(SearchStack *ss) {
  int n = MAX_PLY + 8;
  for (int i = 0; i < n; i++) {
    ss->move[i] = 0;
    ss->eval[i] = NO_SCORE;
    ss->piece[i] = NO_PIECE;
    ss->to[i] = 0;
    ss->dext[i] = 0;
  }
}

static Searcher *searcher_alloc(void) {
  Searcher *s = calloc(1, sizeof(Searcher));
  if (!s) abort();
  hist_init(&s->hist);
  ss_reset(&s->ss);
  tm_reset(&s->tm);
  s->pickers = calloc((size_t)(MAX_PLY + 2) * 2, sizeof(MovePicker));
  s->qpickers = calloc((size_t)(MAX_PLY + 2), sizeof(MovePicker));
  s->pcpickers = calloc((size_t)(MAX_PLY + 2), sizeof(MovePicker));
  if (!s->pickers || !s->qpickers || !s->pcpickers) abort();
  s->stop_flag = &s->own_stop;
  s->own_stop = 0;
  s->move_overhead = 30;
  s->skill_level = 20;
  s->limit_strength = false;
  s->uci_elo = 1320;
  s->rng = 0x9E3779B97F4A7C15ULL;
  return s;
}

Searcher *searcher_create(int hash_mb) {
  Searcher *s = searcher_alloc();
  tt_init(&s->tt, hash_mb);
  return s;
}

static void searcher_free_common(Searcher *s) {
  hist_free(&s->hist);
  free(s->pickers);
  free(s->qpickers);
  free(s->pcpickers);
  if (s->nn) nnue_stack_free(s->nn);
  if (s->own_board) {
    board_free(s->own_board);
    free(s->own_board);
  }
  free(s);
}

void searcher_destroy(Searcher *s) {
  if (!s) return;
  searcher_set_threads(s, 1);
  tt_free(&s->tt);
  searcher_free_common(s);
}

void searcher_set_stop_flag(Searcher *s, volatile int *flag) { s->stop_flag = flag; }
void searcher_request_stop(Searcher *s) { __atomic_store_n(s->stop_flag, 1, __ATOMIC_SEQ_CST); }
void searcher_clear_stop(Searcher *s) { __atomic_store_n(s->stop_flag, 0, __ATOMIC_SEQ_CST); }
void searcher_resize_hash(Searcher *s, int mb) { tt_resize(&s->tt, mb); }
void searcher_clear_hash(Searcher *s) { tt_clear(&s->tt); }

void searcher_new_game(Searcher *s) {
  tt_clear(&s->tt);
  hist_clear(&s->hist);
  for (int i = 0; i < s->n_helpers; i++) hist_clear(&s->helpers[i]->hist);
  s->tm.has_last_search = false;
  clear_pawn_hash();
}

int searcher_repetition_score(const Searcher *s, int ply) {
  return (ply & 1) == 0 ? s->root_draw : -s->root_draw;
}

static inline bool stop_requested(const Searcher *s) {
  return __atomic_load_n(s->stop_flag, __ATOMIC_RELAXED) != 0;
}

uint64_t searcher_total_nodes(const Searcher *s) {
  uint64_t n = __atomic_load_n(&s->nodes, __ATOMIC_RELAXED);
  for (int i = 0; i < s->n_helpers; i++) n += __atomic_load_n(&s->helpers[i]->nodes, __ATOMIC_RELAXED);
  return n;
}

static void check_stop(Searcher *s) {
  if (s->completed_depth < 1) return;
  if (stop_requested(s)) {
    s->stopped = true;
    return;
  }
  if (s->is_helper) return;
  if ((s->max_nodes > 0 && searcher_total_nodes(s) >= (uint64_t)s->max_nodes) ||
      tm_hard_limit_reached(&s->tm)) {
    s->stopped = true;
  }
}

static inline bool do_move(Searcher *s, int move, int ply) {
  if (s->use_nnue) nnue_push_move(s->nn, ply + 1, s->board, move);
  return make_move(s->board, move);
}

static inline void do_null(Searcher *s, int ply) {
  if (s->use_nnue) nnue_push_null(s->nn, ply + 1);
  make_null_move(s->board);
}

static inline int search_eval(Searcher *s, int ply) {
  return evaluate_with(s->board, s->use_nnue ? s->nn : NULL, ply);
}

static inline bool root_excluded(const Searcher *s, int move) {
  for (int i = 0; i < s->root_excl_n; i++) if (s->root_excl[i] == move) return true;
  return false;
}

static bool direct_check(const Board *b, int move) {
  int side = b->side;
  int ksq = b->king_sq[side ^ 1];
  int to = (move >> 6) & 63;
  int pt = PIECE_TYPE[(move >> 12) & 15];
  U64 kbb = SQ_BB(ksq);
  switch (pt) {
    case 0: return (pawn_attacks[side][to] & kbb) != 0;
    case 1: return (knight_attacks[to] & kbb) != 0;
    case 2: return (get_bishop_attacks(to, b->occupancies[2]) & kbb) != 0;
    case 3: return (get_rook_attacks(to, b->occupancies[2]) & kbb) != 0;
    case 4: return ((get_bishop_attacks(to, b->occupancies[2]) | get_rook_attacks(to, b->occupancies[2])) & kbb) != 0;
    default: return false;
  }
}

static int victim_type(const Board *b, int move) {
  if (move & MOVE_ENPASSANT) return 0;
  int t = b->mailbox[(move >> 6) & 63];
  return t == NO_PIECE ? 0 : PIECE_TYPE[t];
}

static void update_pv(Searcher *s, int ply, int move) {
  int row = ply * MAX_PLY;
  int next_row = (ply + 1) * MAX_PLY;
  s->pv_table[row + ply] = move;
  int next_len = s->pv_length[ply + 1];
  for (int j = ply + 1; j < next_len; j++) s->pv_table[row + j] = s->pv_table[next_row + j];
  s->pv_length[ply] = next_len > ply + 1 ? next_len : ply + 1;
}

static int qsearch(Searcher *s, int alpha, int beta, int ply) {
  Board *board = s->board;
  SearchStack *ss = &s->ss;
  bool is_pv = beta - alpha > 1;
  s->pv_length[ply] = ply;

  s->nodes++;
  if (ply > s->seldepth) s->seldepth = ply;
  if ((s->nodes & 511) == 0) check_stop(s);
  if (s->stopped) return 0;

  if (board->fifty >= 100 || board_is_material_draw(board)) return 0;
  if (board_is_repetition(board)) return searcher_repetition_score(s, ply);
  if (ply >= MAX_PLY - 1) return search_eval(s, ply);

  bool in_chk = in_check(board);
  TranspositionTable *tt = &s->tt;
  U64 hash = board->hash_key;

  int tt_move = 0;
  bool tt_hit = false;
  int tt_eval = NO_SCORE;
  if (tt_probe(tt, hash, ply)) {
    tt_hit = true;
    tt_move = tt->hit_move;
    tt_eval = tt->hit_eval;
    int bd = tt->hit_bound;
    int sc = tt->hit_score;
    if (!is_pv && board->fifty < 90 &&
        (bd == BOUND_EXACT || (bd == BOUND_LOWER && sc >= beta) || (bd == BOUND_UPPER && sc <= alpha))) {
      return sc;
    }
  }

  int best_score;
  int stand_pat = NO_SCORE;
  int raw_eval = NO_SCORE;
  if (in_chk) {
    best_score = -INFINITE_SCORE;
  } else {
    raw_eval = (tt_hit && tt_eval != NO_SCORE) ? tt_eval : search_eval(s, ply);
    stand_pat = raw_eval;
    best_score = stand_pat;
    if (tt_hit && tt->hit_score > -MATE_IN_MAX && tt->hit_score < MATE_IN_MAX) {
      int bd = tt->hit_bound;
      int sc = tt->hit_score;
      if (bd == BOUND_EXACT || (bd == BOUND_LOWER && sc > best_score) || (bd == BOUND_UPPER && sc < best_score))
        best_score = sc;
    }
    if (best_score >= beta) {
      if (!tt_hit) tt_store(tt, hash, 0, best_score, raw_eval, BOUND_LOWER, 0, ply);
      return best_score;
    }
    if (best_score > alpha) alpha = best_score;
  }

  int p1p = ply >= 1 ? ss->piece[ply - 1] : NO_PIECE;
  int p1t = ply >= 1 ? ss->to[ply - 1] : 0;
  int p2p = ply >= 2 ? ss->piece[ply - 2] : NO_PIECE;
  int p2t = ply >= 2 ? ss->to[ply - 2] : 0;

  MovePicker *picker = &s->qpickers[ply];
  picker_init(picker, board, tt_move, ply, &s->hist, p1p, p1t, p2p, p2t, !in_chk);

  int orig_alpha = alpha;
  int best_move = 0;
  int legal = 0;
  int move;

  while ((move = picker_next(picker)) != 0) {
    bool is_promo = ((move >> 16) & 15) != 0;

    if (!in_chk && !is_promo && best_score > -MATE_IN_MAX) {
      int victim = (move & MOVE_ENPASSANT) ? 100 : SEE_VALUE[victim_type(board, move)];
      if (stand_pat + victim + DELTA_MARGIN <= alpha) continue;
    }

    if (!do_move(s, move, ply)) continue;
    legal++;
    ss->move[ply] = move;
    ss->piece[ply] = (int8_t)((move >> 12) & 15);
    ss->to[ply] = (int8_t)((move >> 6) & 63);

    int score = -qsearch(s, -beta, -alpha, ply + 1);
    unmake_move(board);
    if (s->stopped) return 0;

    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        best_move = move;
        alpha = score;
        update_pv(s, ply, move);
        if (alpha >= beta) break;
      }
    }
  }

  if (in_chk && legal == 0) return -MATE_SCORE + ply;

  if (best_score >= beta && best_score < MATE_IN_MAX && best_score > -MATE_IN_MAX)
    best_score = (best_score + beta) >> 1;

  int bound = best_score >= beta ? BOUND_LOWER : (best_score > orig_alpha ? BOUND_EXACT : BOUND_UPPER);
  tt_store(tt, hash, 0, best_score, raw_eval, bound, best_move, ply);
  return best_score;
}

static int negamax(Searcher *s, int depth, int alpha, int beta, int ply, bool cut_node, int excluded) {
  Board *board = s->board;
  SearchStack *ss = &s->ss;
  HistoryTables *hist = &s->hist;
  TranspositionTable *tt = &s->tt;
  bool is_pv = beta - alpha > 1;
  bool is_root = ply == 0;
  s->pv_length[ply] = ply;

  if (!is_root) {
    if (board->fifty >= 100 || board_is_material_draw(board)) return 0;
    if (board_is_repetition(board)) return searcher_repetition_score(s, ply);
    if (ply >= MAX_PLY - 1) return search_eval(s, ply);

    if (alpha < -MATE_SCORE + ply) alpha = -MATE_SCORE + ply;
    if (beta > MATE_SCORE - ply - 1) beta = MATE_SCORE - ply - 1;
    if (alpha >= beta) return alpha;
  } else {
    s->iter_best = 0;
  }

  bool in_chk = in_check(board);

  if (in_chk) depth++;
  if (depth <= 0) return qsearch(s, alpha, beta, ply);

  s->nodes++;
  if (ply > s->seldepth) s->seldepth = ply;
  if ((s->nodes & 511) == 0) check_stop(s);
  if (s->stopped) return 0;

  int side = board->side;
  U64 hash = board->hash_key;
  int tt_move = 0;
  bool tt_hit = false;
  int tt_score = 0;
  int tt_eval = NO_SCORE;
  int tt_bound = BOUND_NONE;
  int tt_depth = 0;
  if (tt_probe(tt, hash, ply)) {
    tt_hit = true;
    tt_move = tt->hit_move;
    tt_score = tt->hit_score;
    tt_eval = tt->hit_eval;
    tt_bound = tt->hit_bound;
    tt_depth = tt->hit_depth;
    if (excluded == 0 && !is_pv && tt_depth >= depth && board->fifty < 90 &&
        (tt_bound == BOUND_EXACT ||
         (tt_bound == BOUND_LOWER && tt_score >= beta) ||
         (tt_bound == BOUND_UPPER && tt_score <= alpha))) {
      if (tt_score >= beta && tt_move != 0 && !(tt_move & (MOVE_CAPTURE | 0xF0000)) && ply >= 1)
        hist_update_history(hist, side, tt_move, history_bonus(depth) >> 1);
      return tt_score;
    }
  }
  bool tt_move_is_capture = tt_move != 0 && (tt_move & MOVE_CAPTURE) != 0;

  int raw_eval = NO_SCORE;
  int static_eval = NO_SCORE;
  int eval_score = NO_SCORE;
  int corr_idx = hist_corr_index(side, board->pawn_hash_key);
  if (!in_chk) {
    if (excluded != 0 && ss->eval[ply] != NO_SCORE) {
      static_eval = ss->eval[ply];
      raw_eval = static_eval - hist_get_corr(hist, corr_idx);
    } else {
      raw_eval = (tt_hit && tt_eval != NO_SCORE) ? tt_eval : search_eval(s, ply);
      static_eval = raw_eval + hist_get_corr(hist, corr_idx);
    }
    eval_score = static_eval;

    if (tt_hit && tt_score > -MATE_IN_MAX && tt_score < MATE_IN_MAX) {
      if (tt_bound == BOUND_EXACT ||
          (tt_bound == BOUND_LOWER && tt_score > eval_score) ||
          (tt_bound == BOUND_UPPER && tt_score < eval_score))
        eval_score = tt_score;
    }
  }
  ss->eval[ply] = (int16_t)static_eval;

  bool improving = false;
  if (!in_chk) {
    if (ply >= 2 && ss->eval[ply - 2] != NO_SCORE) improving = static_eval > ss->eval[ply - 2];
    else if (ply >= 4 && ss->eval[ply - 4] != NO_SCORE) improving = static_eval > ss->eval[ply - 4];
  }

  bool prev_is_null = ply >= 1 && ss->move[ply - 1] == 0;
  int p1p = ply >= 1 ? ss->piece[ply - 1] : NO_PIECE;
  int p1t = ply >= 1 ? ss->to[ply - 1] : 0;
  int p2p = ply >= 2 ? ss->piece[ply - 2] : NO_PIECE;
  int p2t = ply >= 2 ? ss->to[ply - 2] : 0;
  int dext = ply > 0 ? ss->dext[ply - 1] : 0;

  int pidx = excluded != 0 ? ply + MAX_PLY + 2 : ply;

  if (!is_pv && !in_chk) {
    if (depth <= 3 && alpha < MATE_IN_MAX &&
        eval_score + 180 + 130 * depth + (improving ? 50 : 0) < alpha) {
      int v = qsearch(s, alpha - 1, alpha, ply);
      if (s->stopped) return 0;
      if (v < alpha) return v;
    }

    if (depth <= 8 && beta > -MATE_IN_MAX && eval_score < MATE_IN_MAX &&
        eval_score - (68 * depth - (improving ? 48 : 0)) >= beta) {
      return (2 * beta + eval_score) / 3;
    }

    if (excluded == 0 && depth >= 3 && eval_score >= beta && !prev_is_null && !is_root &&
        static_eval >= beta - 10 * depth + 100 &&
        !(ply < s->nmp_min_ply && side == s->nmp_side) &&
        !(tt_hit && tt_bound == BOUND_UPPER && tt_score < beta)) {
      U64 king_bb = side == WHITE ? board->bitboards[W_KING] : board->bitboards[B_KING];
      U64 pawn_bb = side == WHITE ? board->bitboards[W_PAWN] : board->bitboards[B_PAWN];
      if ((board->occupancies[side] & ~(king_bb | pawn_bb)) != 0) {
        int R = 3 + (depth / 4);
        int extra = (eval_score - beta) / 160;
        R += extra < 3 ? extra : 3;
        ss->move[ply] = 0;
        ss->piece[ply] = NO_PIECE;
        ss->to[ply] = 0;
        do_null(s, ply);
        int nscore = -negamax(s, depth - 1 - R, -beta, -beta + 1, ply + 1, !cut_node, 0);
        unmake_null_move(board);
        if (s->stopped) return 0;
        if (nscore >= beta) {
          int ret = nscore >= MATE_IN_MAX ? beta : nscore;
          if (s->nmp_min_ply != 0 || depth < 10) return ret;
          s->nmp_min_ply = ply + (((depth - R) * 3) / 4) + 1;
          s->nmp_side = side;
          int v = negamax(s, depth - R, beta - 1, beta, ply, false, 0);
          s->nmp_min_ply = 0;
          if (s->stopped) return 0;
          if (v >= beta) return ret;
        }
      }
    }

    int prob_beta = beta + PROBCUT_MARGIN - (improving ? PROBCUT_IMPROVING : 0);
    if (excluded == 0 && depth >= 5 && abs(beta) < MATE_IN_MAX &&
        !(tt_hit && tt_depth >= depth - 3 && tt_score < prob_beta)) {
      MovePicker *pp = &s->pcpickers[ply];
      picker_init(pp, board, tt_move, ply, hist, p1p, p1t, p2p, p2t, true);
      int pm;
      while ((pm = picker_next(pp)) != 0) {
        int sv = pp->see_known ? pp->see : see_value(board, pm);
        if (sv < prob_beta - static_eval) continue;
        if (!do_move(s, pm, ply)) continue;
        ss->move[ply] = pm;
        ss->piece[ply] = (int8_t)((pm >> 12) & 15);
        ss->to[ply] = (int8_t)((pm >> 6) & 63);
        int value = -qsearch(s, -prob_beta, -prob_beta + 1, ply + 1);
        if (value >= prob_beta && !s->stopped) {
          value = -negamax(s, depth - 4, -prob_beta, -prob_beta + 1, ply + 1, !cut_node, 0);
        }
        unmake_move(board);
        if (s->stopped) return 0;
        if (value >= prob_beta) {
          tt_store(tt, hash, depth - 3, value, raw_eval, BOUND_LOWER, pm, ply);
          return value;
        }
      }
    }
  }

  if ((is_pv || cut_node) && depth >= 4 && tt_move == 0) depth--;

  MovePicker *picker = &s->pickers[pidx];
  int first_move = is_root && s->root_first != 0 ? s->root_first : tt_move;
  picker_init(picker, board, first_move, ply, hist, p1p, p1t, p2p, p2t, false);
  int quiets[64];
  int tried_caps[32];
  int quiet_count = 0;
  int cap_count = 0;

  int orig_alpha = alpha;
  int best_score = -INFINITE_SCORE;
  int best_move = 0;
  int legal = 0;
  bool skip_quiets = false;
  int move;

  while ((move = picker_next(picker)) != 0) {
    int stage = picker->stage_of;
    bool is_capture = (move & MOVE_CAPTURE) != 0;
    bool is_promo = ((move >> 16) & 15) != 0;
    bool is_quiet = !is_capture && !is_promo;

    if (move == excluded) continue;
    if (is_root && s->root_excl_n != 0 && root_excluded(s, move)) continue;
    if (skip_quiets && is_quiet) continue;

    int h_score;
    if (is_quiet) {
      h_score = hist_get_history(hist, side, move) + hist_get_cont(hist, p1p, p1t, move) + hist_get_cont(hist, p2p, p2t, move);
    } else if (is_capture) {
      h_score = hist_get_cap(hist, move, victim_type(board, move));
    } else {
      h_score = 0;
    }

    if (!is_root && best_score > -MATE_IN_MAX && legal > 0) {
      int r_est = lmr_table[(depth > 63 ? 63 : depth) * 64 + (legal + 1 > 63 ? 63 : legal + 1)];
      int lmr_depth = depth - 1 - r_est + (h_score / 6000);
      if (lmr_depth < 0) lmr_depth = 0;

      if (is_quiet) {
        if (!is_pv && depth <= 8 && quiet_count >= LMP_LIMIT[improving ? 1 : 0][depth]) {
          skip_quiets = true;
          continue;
        }
        if (!in_chk && !direct_check(board, move)) {
          if (!is_pv && lmr_depth <= 4 && h_score < -2800 * (lmr_depth + 1)) continue;
          if (!is_pv && lmr_depth <= 7 &&
              static_eval + 95 + 105 * lmr_depth + (h_score / 256) <= alpha) {
            skip_quiets = true;
            continue;
          }
          if (depth <= 6 && see_value(board, move) < -(26 * lmr_depth * lmr_depth + 20)) continue;
        }
      } else if (!is_promo) {
        if (!in_chk && !direct_check(board, move)) {
          if (stage == ST_BAD || !picker->see_known) {
            if (depth <= 6 && see_value(board, move) < -(90 * depth) - (h_score / 32)) continue;
          }
          if (!is_pv && lmr_depth <= 5 &&
              static_eval + 200 + SEE_VALUE[victim_type(board, move)] + 90 * lmr_depth <= alpha) continue;
        }
      }
    }

    int extension = 0;
    if (!is_root && depth >= SINGULAR_MIN_DEPTH && move == tt_move && excluded == 0 && tt_hit &&
        tt_bound != BOUND_UPPER && tt_bound != BOUND_NONE && tt_depth >= depth - 3 &&
        tt_score > -MATE_IN_MAX && tt_score < MATE_IN_MAX) {
      int sbeta = tt_score - ((depth * (is_pv ? 18 : 14)) >> 3);
      int sv = negamax(s, (depth - 1) >> 1, sbeta - 1, sbeta, ply, cut_node, move);
      if (s->stopped) return 0;
      if (sv < sbeta) {
        extension = 1;
        if (!is_pv && dext < MAX_DOUBLE_EXT && sv < sbeta - 24 - 4 * depth) extension = 2;
        else if (is_pv && dext < MAX_DOUBLE_EXT && sv < sbeta - 90) extension = 2;
      } else if (sbeta >= beta) {
        return sbeta;
      } else if (tt_score >= beta) {
        extension = -1;
      } else if (cut_node) {
        extension = -1;
      }
    }

    if (!do_move(s, move, ply)) continue;
    legal++;

    bool gives_check = in_check(board);

    ss->move[ply] = move;
    ss->piece[ply] = (int8_t)((move >> 12) & 15);
    ss->to[ply] = (int8_t)((move >> 6) & 63);
    ss->dext[ply] = (int8_t)(dext + (extension > 1 ? 1 : 0));
    if (is_quiet) { if (quiet_count < 64) quiets[quiet_count] = move; quiet_count++; }
    else if (is_capture) { if (cap_count < 32) tried_caps[cap_count++] = move; }

    uint64_t nodes_before = s->nodes;
    int new_depth = depth - 1 + extension;
    int score;

    if (legal == 1) {
      score = -negamax(s, new_depth, -beta, -alpha, ply + 1, is_pv ? false : !cut_node, 0);
    } else {
      int r = 0;
      bool reducible = is_quiet || stage == ST_BAD;
      if (depth >= 3 && legal >= (is_pv ? 3 : 2) && reducible) {
        r = lmr_table[(depth > 63 ? 63 : depth) * 64 + (legal > 63 ? 63 : legal)];
        if (is_pv) r--;
        if (cut_node) r++;
        if (improving) r--;
        if (gives_check) r--;
        if (in_chk) r--;
        if (tt_move_is_capture) r++;
        int hr = h_score / 6000;
        if (hr > 2) hr = 2; else if (hr < -2) hr = -2;
        r -= hr;
        if (r > new_depth - 1) r = new_depth - 1;
        if (r < 0) r = 0;
      }

      bool child_cut = is_pv ? true : !cut_node;
      score = -negamax(s, new_depth - r, -alpha - 1, -alpha, ply + 1, child_cut, 0);
      if (score > alpha && r > 0 && !s->stopped) {
        int d2 = new_depth;
        if (score > best_score + 55 + 2 * new_depth) d2++;
        score = -negamax(s, d2, -alpha - 1, -alpha, ply + 1, child_cut, 0);
      }
      if (is_pv && score > alpha && score < beta && !s->stopped) {
        score = -negamax(s, new_depth, -beta, -alpha, ply + 1, false, 0);
      }
    }

    unmake_move(board);
    if (s->stopped) return 0;

    if (is_root) s->root_move_nodes[move & 4095] += (double)(s->nodes - nodes_before);

    if (score > best_score) {
      best_score = score;
      if (score > alpha) {
        best_move = move;
        alpha = score;
        update_pv(s, ply, move);
        if (is_root) { s->iter_best = move; s->iter_best_score = score; }

        if (alpha >= beta) {
          int bonus = history_bonus(depth + (best_score > beta + 90 ? 1 : 0));
          int malus = history_malus(depth);
          if (is_quiet) {
            hist_store_killer(hist, ply, move);
            hist_set_counter_move(hist, p1p, p1t, move);
            hist_update_history(hist, side, move, bonus);
            hist_update_cont(hist, p1p, p1t, move, bonus);
            hist_update_cont(hist, p2p, p2t, move, bonus);
            int n = quiet_count > 64 ? 64 : quiet_count;
            for (int i = 0; i < n; i++) {
              int q = quiets[i];
              if (q == move) continue;
              hist_update_history(hist, side, q, -malus);
              hist_update_cont(hist, p1p, p1t, q, -malus);
              hist_update_cont(hist, p2p, p2t, q, -malus);
            }
          } else if (is_capture) {
            hist_update_cap(hist, move, victim_type(board, move), bonus);
          }
          for (int i = 0; i < cap_count; i++) {
            int c = tried_caps[i];
            if (c == move) continue;
            hist_update_cap(hist, c, victim_type(board, c), -malus);
          }
          break;
        }
      }
    }
  }

  if (legal == 0) return excluded != 0 ? alpha : (in_chk ? -MATE_SCORE + ply : 0);

  if (best_move == 0 && ply >= 2 && depth >= 3 && !prev_is_null && p1p != NO_PIECE &&
      !(ss->move[ply - 1] & (MOVE_CAPTURE | 0xF0000))) {
    hist_update_cont(hist, p2p, p2t, ss->move[ply - 1], history_bonus(depth) >> 1);
  }

  int bound = best_score >= beta ? BOUND_LOWER : (best_score > orig_alpha ? BOUND_EXACT : BOUND_UPPER);

  if (excluded == 0 && !(is_root && s->root_excl_n != 0)) {
    if (!in_chk && best_score > -MATE_IN_MAX && best_score < MATE_IN_MAX &&
        (best_move == 0 || !(best_move & (MOVE_CAPTURE | 0xF0000))) &&
        !(bound == BOUND_LOWER && best_score <= raw_eval) &&
        !(bound == BOUND_UPPER && best_score >= raw_eval)) {
      hist_update_corr(hist, corr_idx, best_score - raw_eval, depth);
    }
    tt_store(tt, hash, depth, best_score, raw_eval, bound, best_move, ply);
  }
  return best_score;
}

#define CONTEMPT_MAX 25
#define CONTEMPT_AHEAD 80
#define CONTEMPT_BEHIND 120

static int contempt_for(int score) {
  if (score >= MATE_IN_MAX || score <= -MATE_IN_MAX) return 0;
  if (score > CONTEMPT_AHEAD) {
    int v = (score - CONTEMPT_AHEAD) / 6;
    return -(v < CONTEMPT_MAX ? v : CONTEMPT_MAX);
  }
  if (score < -CONTEMPT_BEHIND) {
    int v = (-score - CONTEMPT_BEHIND) / 6;
    return v < CONTEMPT_MAX ? v : CONTEMPT_MAX;
  }
  return 0;
}

static int round_ms(double ms) {
  int v = (int)floor(ms + 0.5);
  return v < 1 ? 1 : v;
}

static void fill_info(Searcher *s, SearchInfo *info) {
  info->nodes = searcher_total_nodes(s);
  info->time = round_ms(tm_elapsed(&s->tm));
  info->nps = (info->nodes * 1000) / (uint64_t)info->time;
  info->hashfull = tt_hashfull(&s->tt);
}

static const int SKIP_SIZE[20] = {1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 4, 4, 4, 4, 4, 4, 4};
static const int SKIP_PHASE[20] = {0, 1, 0, 1, 2, 3, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6, 7};

static void helper_iterate(Searcher *h) {
  h->nodes = 0;
  h->seldepth = 0;
  h->stopped = false;
  h->completed_depth = 0;
  h->nmp_min_ply = 0;
  h->root_excl_n = 0;
  ss_reset(&h->ss);
  hist_clear_killers(&h->hist);
  hist_age(&h->hist);
  memset(h->pv_table, 0, sizeof(h->pv_table));
  memset(h->pv_length, 0, sizeof(h->pv_length));
  h->root_first = 0;
  h->root_draw = 0;
  if (h->use_nnue) nnue_stack_reset_root(h->nn);

  int prev_score = 0;
  int best_move = 0;
  for (int depth = 1; depth <= h->helper_max_depth; depth++) {
    if (depth > 1) {
      int i = h->helper_id % 20;
      if (((depth + SKIP_PHASE[i]) / SKIP_SIZE[i]) % 2) continue;
    }
    int alpha = -INFINITE_SCORE;
    int beta = INFINITE_SCORE;
    int delta = 0;
    if (depth >= ASPIRATION_START_DEPTH) {
      delta = 10 + (prev_score * prev_score) / 12000;
      alpha = prev_score - delta > -INFINITE_SCORE ? prev_score - delta : -INFINITE_SCORE;
      beta = prev_score + delta < INFINITE_SCORE ? prev_score + delta : INFINITE_SCORE;
    }
    h->root_draw = depth >= 4 ? contempt_for(prev_score) : 0;
    h->root_first = best_move;
    int fail_highs = 0;
    int score = 0;
    for (;;) {
      int fh = fail_highs < 2 ? fail_highs : 2;
      int search_depth = depth - fh > 1 ? depth - fh : 1;
      score = negamax(h, search_depth, alpha, beta, 0, false, 0);
      if (h->stopped) break;
      if (score <= alpha) {
        beta = (alpha + beta) >> 1;
        alpha = score - delta > -INFINITE_SCORE ? score - delta : -INFINITE_SCORE;
        fail_highs = 0;
      } else if (score >= beta) {
        beta = score + delta < INFINITE_SCORE ? score + delta : INFINITE_SCORE;
        fail_highs++;
        if (h->iter_best != 0) h->root_first = h->iter_best;
      } else {
        break;
      }
      delta += (delta >> 1) + 4;
      if (alpha <= -1500) alpha = -INFINITE_SCORE;
      if (beta >= 1500) beta = INFINITE_SCORE;
    }
    if (h->stopped) break;
    h->completed_depth = depth;
    prev_score = score;
    if (h->pv_length[0] > 0 && h->pv_table[0] != 0) best_move = h->pv_table[0];
    if (stop_requested(h)) break;
  }
}

#ifdef VEXAPH_SMP
typedef struct {
  pthread_t th;
  bool started;
} SmpThread;

static void *helper_thread_main(void *arg) {
  helper_iterate((Searcher *)arg);
  return NULL;
}
#endif

void searcher_set_threads(Searcher *s, int n) {
#ifdef VEXAPH_SMP
  if (n < 1) n = 1;
  if (n > 256) n = 256;
  int want = n - 1;
  if (want == s->n_helpers) return;
  for (int i = want; i < s->n_helpers; i++) searcher_free_common(s->helpers[i]);
  if (want == 0) {
    free(s->helpers);
    free(s->smp_threads);
    s->helpers = NULL;
    s->smp_threads = NULL;
    s->n_helpers = 0;
    return;
  }
  Searcher **nh = realloc(s->helpers, (size_t)want * sizeof(*nh));
  if (!nh) abort();
  s->helpers = nh;
  for (int i = s->n_helpers; i < want; i++) {
    Searcher *h = searcher_alloc();
    h->is_helper = true;
    h->helper_id = i;
    h->stop_flag = &s->smp_stop;
    h->move_overhead = s->move_overhead;
    tt_attach(&h->tt, &s->tt);
    s->helpers[i] = h;
  }
  free(s->smp_threads);
  s->smp_threads = calloc((size_t)want, sizeof(SmpThread));
  if (!s->smp_threads) abort();
  s->n_helpers = want;
#else
  (void)s;
  (void)n;
#endif
}

int searcher_get_threads(const Searcher *s) { return s->n_helpers + 1; }

static void smp_start(Searcher *s, const Board *board, int max_depth) {
#ifdef VEXAPH_SMP
  if (s->n_helpers == 0) return;
  __atomic_store_n(&s->smp_stop, 0, __ATOMIC_SEQ_CST);
  SmpThread *th = (SmpThread *)s->smp_threads;
  pthread_attr_t attr;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, (size_t)16 * 1024 * 1024);
  for (int i = 0; i < s->n_helpers; i++) {
    Searcher *h = s->helpers[i];
    tt_attach(&h->tt, &s->tt);
    if (h->own_board) board_free(h->own_board);
    else h->own_board = calloc(1, sizeof(Board));
    if (!h->own_board) abort();
    board_clone(h->own_board, board);
    h->board = h->own_board;
    h->max_nodes = 0;
    h->use_nnue = s->use_nnue;
    if (h->use_nnue && !h->nn) h->nn = nnue_stack_create();
    h->helper_max_depth = max_depth;
    h->stopped = false;
    h->nodes = 0;
    th[i].started = pthread_create(&th[i].th, &attr, helper_thread_main, h) == 0;
  }
  pthread_attr_destroy(&attr);
#else
  (void)s;
  (void)board;
  (void)max_depth;
#endif
}

static void smp_stop_all(Searcher *s) {
#ifdef VEXAPH_SMP
  if (s->n_helpers == 0) return;
  __atomic_store_n(&s->smp_stop, 1, __ATOMIC_SEQ_CST);
  SmpThread *th = (SmpThread *)s->smp_threads;
  for (int i = 0; i < s->n_helpers; i++) {
    if (th[i].started) {
      pthread_join(th[i].th, NULL);
      th[i].started = false;
    }
  }
  __atomic_store_n(&s->smp_stop, 0, __ATOMIC_SEQ_CST);
#else
  (void)s;
#endif
}

void searcher_set_strength(Searcher *s, int skill_level, bool limit_strength, int elo) {
  s->skill_level = skill_level < 0 ? 0 : (skill_level > 20 ? 20 : skill_level);
  s->limit_strength = limit_strength;
  s->uci_elo = elo < 1320 ? 1320 : (elo > 3190 ? 3190 : elo);
}

int searcher_effective_skill(const Searcher *s) {
  if (s->limit_strength) {
    double e = (double)(s->uci_elo - 1320) / (3190 - 1320);
    int lv = (int)(((37.2473 * e - 40.8525) * e + 22.2543) * e - 0.311438);
    return lv < 0 ? 0 : (lv > 19 ? 19 : lv);
  }
  return s->skill_level;
}

static uint64_t next_rand(Searcher *s) {
  uint64_t x = s->rng;
  x ^= x >> 12;
  x ^= x << 25;
  x ^= x >> 27;
  s->rng = x;
  return x * 0x2545F4914F6CDD1DULL;
}

static int skill_pick(Searcher *s, int best_move, int best_score, int legal_root, int depth, int level) {
  if (best_score >= MATE_IN_MAX || best_score <= -MATE_IN_MAX) return best_move;
  int cm[4], cs[4];
  int n = 1;
  cm[0] = best_move;
  cs[0] = best_score;
  int want = legal_root < 4 ? legal_root : 4;
  s->root_excl_n = 0;
  while (n < want) {
    s->root_excl[s->root_excl_n++] = cm[n - 1];
    s->iter_best = 0;
    int sc = negamax(s, depth, -INFINITE_SCORE, INFINITE_SCORE, 0, false, 0);
    if (s->stopped || s->iter_best == 0) break;
    cm[n] = s->iter_best;
    cs[n] = sc;
    n++;
  }
  s->root_excl_n = 0;
  if (s->stopped || n < 2) return best_move;
  int weakness = 120 - 2 * level;
  int top = cs[0];
  int delta = top - cs[n - 1];
  if (delta > 100) delta = 100;
  int max_score = -INFINITE_SCORE;
  int pick = cm[0];
  for (int i = 0; i < n; i++) {
    int r = (int)(next_rand(s) % (uint64_t)weakness);
    int push = (weakness * (top - cs[i]) + delta * r) / 128;
    if (cs[i] + push >= max_score) {
      max_score = cs[i] + push;
      pick = cm[i];
    }
  }
  return pick;
}

void searcher_search(Searcher *s, Board *board, const SearchLimits *limits, InfoCallback cb, void *ud, SearchInfo *info) {
  s->board = board;
  board->ply = 0;
  s->use_nnue = nnue_active();
  if (s->use_nnue) {
    if (!s->nn) s->nn = nnue_stack_create();
    nnue_stack_reset_root(s->nn);
  }
  s->root_excl_n = 0;
  s->nodes = 0;
  s->seldepth = 0;
  s->stopped = false;
  s->completed_depth = 0;
  s->nmp_min_ply = 0;
  ss_reset(&s->ss);
  hist_clear_killers(&s->hist);
  hist_age(&s->hist);
  tt_new_search(&s->tt);
  memset(s->pv_table, 0, sizeof(s->pv_table));
  memset(s->pv_length, 0, sizeof(s->pv_length));
  s->root_first = 0;
  s->root_draw = 0;

  bool lim_depth = limits->has_depth && limits->depth > 0;
  bool lim_nodes = limits->has_nodes && limits->nodes > 0;
  bool lim_movetime = limits->has_movetime && limits->movetime > 0;
  int max_depth = lim_depth ? limits->depth : MAX_PLY - 1;
  if (max_depth > MAX_PLY - 1) max_depth = MAX_PLY - 1;
  int skill = searcher_effective_skill(s);
  bool skill_on = skill < 20 && !limits->infinite;
  if (skill_on && max_depth > skill + 1) max_depth = skill + 1;
  s->max_nodes = lim_nodes ? limits->nodes : 0;
  bool has_time_info = lim_movetime || limits->has_wtime || limits->has_btime;
  bool infinite_mode = limits->infinite || (!has_time_info && !lim_depth && !lim_nodes);

  int phase;
  {
    const U64 *bb = board->bitboards;
    phase = count_bits(bb[1] | bb[7]) + count_bits(bb[2] | bb[8]) + 2 * count_bits(bb[3] | bb[9]) + 4 * count_bits(bb[4] | bb[10]);
  }
  TimeParams tp;
  memset(&tp, 0, sizeof(tp));
  tp.infinite = infinite_mode || !has_time_info;
  tp.has_movetime = limits->has_movetime;
  tp.movetime = (double)limits->movetime;
  tp.has_wtime = limits->has_wtime;
  tp.has_btime = limits->has_btime;
  tp.wtime = (double)limits->wtime;
  tp.btime = (double)limits->btime;
  tp.winc = limits->has_winc ? (double)limits->winc : 0;
  tp.binc = limits->has_binc ? (double)limits->binc : 0;
  tp.movestogo = limits->has_movestogo ? (double)limits->movestogo : 0;
  tp.fullmove = board->fullmove + (board->hist_ply >> 1);
  tp.phase = phase;
  tm_init(&s->tm, &tp, board->side, s->move_overhead + (s->n_helpers > 0 ? 5 : 0));

  memset(info, 0, sizeof(*info));
  info->max_depth = max_depth;

  MoveList root_list;
  generate_moves(board, &root_list);
  int legal_root = 0;
  int first_legal = 0;
  for (int i = 0; i < root_list.count; i++) {
    if (make_move(board, root_list.moves[i])) {
      unmake_move(board);
      legal_root++;
      if (first_legal == 0) first_legal = root_list.moves[i];
    }
  }

  if (legal_root == 0) {
    info->best_move = 0;
    info->score = in_check(board) ? -MATE_SCORE : 0;
    info->depth = 0;
    info->pv_len = 0;
    return;
  }

  bool has_root_eval = !in_check(board);
  int root_eval = has_root_eval ? search_eval(s, 0) : 0;

  int best_move = first_legal;
  int score = 0;
  int prev_score = 0;
  int best_pv[MAX_PLY];
  int best_pv_len = 1;
  best_pv[0] = first_legal;

  smp_start(s, board, max_depth);

  for (int depth = 1; depth <= max_depth; depth++) {
    int alpha = -INFINITE_SCORE;
    int beta = INFINITE_SCORE;
    int delta = 0;

    if (depth >= ASPIRATION_START_DEPTH) {
      delta = 10 + (prev_score * prev_score) / 12000;
      alpha = prev_score - delta > -INFINITE_SCORE ? prev_score - delta : -INFINITE_SCORE;
      beta = prev_score + delta < INFINITE_SCORE ? prev_score + delta : INFINITE_SCORE;
    }

    s->root_draw = depth >= 4 ? contempt_for(prev_score) : 0;
    memset(s->root_move_nodes, 0, sizeof(s->root_move_nodes));
    s->root_first = best_move;
    uint64_t iter_start_nodes = s->nodes;
    int fail_highs = 0;

    for (;;) {
      int fh = fail_highs < 2 ? fail_highs : 2;
      int search_depth = depth - fh > 1 ? depth - fh : 1;
      score = negamax(s, search_depth, alpha, beta, 0, false, 0);
      if (s->stopped) break;

      if (score <= alpha) {
        beta = (alpha + beta) >> 1;
        alpha = score - delta > -INFINITE_SCORE ? score - delta : -INFINITE_SCORE;
        fail_highs = 0;
        if (depth >= 6) s->tm.panic = true;
      } else if (score >= beta) {
        beta = score + delta < INFINITE_SCORE ? score + delta : INFINITE_SCORE;
        fail_highs++;
        if (s->iter_best != 0) s->root_first = s->iter_best;
      } else {
        break;
      }
      delta += (delta >> 1) + 4;
      if (alpha <= -1500) alpha = -INFINITE_SCORE;
      if (beta >= 1500) beta = INFINITE_SCORE;
    }

    if (s->stopped) {
      if (s->iter_best != 0 && s->pv_length[0] > 0 && s->pv_table[0] == s->iter_best) {
        best_move = s->iter_best;
        best_pv_len = s->pv_length[0];
        for (int i = 0; i < best_pv_len; i++) best_pv[i] = s->pv_table[i];
        prev_score = s->iter_best_score;
      }
      break;
    }

    s->tm.panic = false;
    s->completed_depth = depth;
    prev_score = score;
    int len = s->pv_length[0];
    if (len > 0 && s->pv_table[0] != 0) {
      best_pv_len = len;
      for (int i = 0; i < len; i++) best_pv[i] = s->pv_table[i];
      best_move = best_pv[0];
    }

    info->depth = depth;
    info->seldepth = s->seldepth;
    info->score = score;
    fill_info(s, info);
    info->best_move = best_move;
    info->pv_len = best_pv_len;
    memcpy(info->pv, best_pv, sizeof(int) * (size_t)best_pv_len);
    if (cb) cb(info, ud);

    uint64_t iter_nodes = s->nodes - iter_start_nodes;
    TimeInfo ti;
    ti.has_effort = true;
    ti.effort = iter_nodes > 0 ? s->root_move_nodes[best_move & 4095] / (double)iter_nodes : 0;
    ti.has_root_eval = has_root_eval;
    ti.root_eval = root_eval;
    tm_update_iteration(&s->tm, depth, best_move, score, &ti);

    if (tm_soft_limit_reached(&s->tm)) break;
    if (stop_requested(s)) break;
    if (s->max_nodes > 0 && s->nodes >= (uint64_t)s->max_nodes) break;
    if (legal_root == 1 && !infinite_mode && !lim_depth) break;
    if (!infinite_mode && !limits->infinite && abs(score) >= MATE_IN_MAX &&
        depth >= (MATE_SCORE - abs(score)) + 2) break;
  }

  smp_stop_all(s);

  if (skill_on && legal_root > 1 && !s->stopped && !stop_requested(s) && s->completed_depth > 0) {
    s->rng ^= (uint64_t)(now_ms() * 1000.0) * 0x9E3779B97F4A7C15ULL + 0x1234567ULL;
    if (s->rng == 0) s->rng = 0x9E3779B97F4A7C15ULL;
    int pick = skill_pick(s, best_move, prev_score, legal_root, s->completed_depth, skill);
    if (pick != best_move) {
      best_move = pick;
      best_pv[0] = pick;
      best_pv_len = 1;
    }
  }

  tm_finish_search(&s->tm, prev_score);

  info->best_move = best_move;
  info->pv_len = best_pv_len;
  memcpy(info->pv, best_pv, sizeof(int) * (size_t)best_pv_len);
  fill_info(s, info);
  info->seldepth = s->seldepth;
  info->score = prev_score;
  if (info->depth == 0) info->depth = 1;
  info->ponder_move = best_pv_len > 1 ? best_pv[1] : 0;
}

int score_to_uci(int score, char *out) {
  if (score >= MATE_IN_MAX) return sprintf(out, "mate %d", ((MATE_SCORE - score) + 1) >> 1);
  if (score <= -MATE_IN_MAX) return sprintf(out, "mate %d", -((MATE_SCORE + score) >> 1));
  return sprintf(out, "cp %d", score);
}

int pv_to_uci(const int *pv, int len, char *out, size_t cap) {
  size_t o = 0;
  if (cap) out[0] = 0;
  for (int i = 0; i < len; i++) {
    char u[8];
    move_to_uci(pv[i], u);
    int n = snprintf(out + o, cap - o, "%s%s", i ? " " : "", u);
    if (n < 0 || (size_t)n >= cap - o) break;
    o += (size_t)n;
  }
  return (int)o;
}
