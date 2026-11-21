#include <stdlib.h>
#include <string.h>
#include "brain/movepick.h"
#include "brain/see.h"
#include "core/movegen.h"

void hist_init(HistoryTables *h) {
  memset(h, 0, sizeof(*h));
  h->cont_history = calloc((size_t)12 * 64 * 12 * 64, sizeof(int16_t));
  if (!h->cont_history) abort();
}

void hist_free(HistoryTables *h) {
  free(h->cont_history);
  h->cont_history = NULL;
}

void hist_clear(HistoryTables *h) {
  memset(h->history, 0, sizeof(h->history));
  memset(h->cap_history, 0, sizeof(h->cap_history));
  memset(h->cont_history, 0, (size_t)12 * 64 * 12 * 64 * sizeof(int16_t));
  memset(h->counter_moves, 0, sizeof(h->counter_moves));
  memset(h->killers, 0, sizeof(h->killers));
  memset(h->corr, 0, sizeof(h->corr));
}

void hist_clear_killers(HistoryTables *h) { memset(h->killers, 0, sizeof(h->killers)); }

void hist_age(HistoryTables *h) {
  for (size_t i = 0; i < sizeof(h->history) / sizeof(h->history[0]); i++) h->history[i] -= h->history[i] >> 2;
  for (size_t i = 0; i < sizeof(h->cap_history) / sizeof(h->cap_history[0]); i++) h->cap_history[i] -= h->cap_history[i] >> 2;
  size_t n = (size_t)12 * 64 * 12 * 64;
  for (size_t i = 0; i < n; i++) h->cont_history[i] -= h->cont_history[i] >> 2;
}

int hist_gravity(int entry, int bonus) {
  int b = bonus > MAX_HISTORY ? MAX_HISTORY : (bonus < -MAX_HISTORY ? -MAX_HISTORY : bonus);
  int ab = b < 0 ? -b : b;
  return entry + b - ((entry * ab) / MAX_HISTORY);
}

void hist_update_history(HistoryTables *h, int side, int move, int bonus) {
  int idx = (side * 64 + (move & 63)) * 64 + ((move >> 6) & 63);
  h->history[idx] = (int16_t)hist_gravity(h->history[idx], bonus);
}

int hist_get_history(const HistoryTables *h, int side, int move) {
  return h->history[(side * 64 + (move & 63)) * 64 + ((move >> 6) & 63)];
}

void hist_update_cap(HistoryTables *h, int move, int victim_pt, int bonus) {
  int idx = (((move >> 12) & 15) * 64 + ((move >> 6) & 63)) * 6 + victim_pt;
  h->cap_history[idx] = (int16_t)hist_gravity(h->cap_history[idx], bonus);
}

int hist_get_cap(const HistoryTables *h, int move, int victim_pt) {
  return h->cap_history[(((move >> 12) & 15) * 64 + ((move >> 6) & 63)) * 6 + victim_pt];
}

static inline int cont_index(int prev_piece, int prev_to, int piece, int to) {
  return ((prev_piece * 64 + prev_to) * 12 + piece) * 64 + to;
}

void hist_update_cont(HistoryTables *h, int prev_piece, int prev_to, int move, int bonus) {
  if (prev_piece == NO_PIECE) return;
  int idx = cont_index(prev_piece, prev_to, (move >> 12) & 15, (move >> 6) & 63);
  h->cont_history[idx] = (int16_t)hist_gravity(h->cont_history[idx], bonus);
}

int hist_get_cont(const HistoryTables *h, int prev_piece, int prev_to, int move) {
  if (prev_piece == NO_PIECE) return 0;
  return h->cont_history[cont_index(prev_piece, prev_to, (move >> 12) & 15, (move >> 6) & 63)];
}

void hist_store_killer(HistoryTables *h, int ply, int move) {
  int i = ply * 2;
  if (h->killers[i] != move) {
    h->killers[i + 1] = h->killers[i];
    h->killers[i] = move;
  }
}

void hist_set_counter_move(HistoryTables *h, int prev_piece, int prev_to, int move) {
  if (prev_piece == NO_PIECE) return;
  h->counter_moves[prev_piece * 64 + prev_to] = move;
}

int hist_get_counter_move(const HistoryTables *h, int prev_piece, int prev_to) {
  if (prev_piece == NO_PIECE) return 0;
  return h->counter_moves[prev_piece * 64 + prev_to];
}

int hist_corr_index(int side, U64 pawn_key) { return side * CORR_SIZE + (int)(pawn_key & 0x3FFF); }

int hist_get_corr(const HistoryTables *h, int idx) { return h->corr[idx] / CORR_GRAIN; }

void hist_update_corr(HistoryTables *h, int idx, int diff, int depth) {
  int target = (diff > 150 ? 150 : (diff < -150 ? -150 : diff)) * CORR_GRAIN;
  int w = depth > 15 ? 32 : 2 * depth + 2;
  int v = h->corr[idx];
  v += ((target - v) * w) >> 8;
  h->corr[idx] = (int16_t)(v > CORR_LIMIT ? CORR_LIMIT : (v < -CORR_LIMIT ? -CORR_LIMIT : v));
}

int history_bonus(int depth) {
  int d = depth > 14 ? 14 : depth;
  int b = 300 * d - 250;
  return b > 2400 ? 2400 : (b < 40 ? 40 : b);
}

int history_malus(int depth) {
  int d = depth > 14 ? 14 : depth;
  int b = 240 * d - 200;
  return b > 1800 ? 1800 : (b < 30 ? 30 : b);
}

void picker_init(MovePicker *p, const Board *board, int tt_move, int ply, const HistoryTables *hist,
                 int prev1_piece, int prev1_to, int prev2_piece, int prev2_to, bool captures_only) {
  MoveList *list = &p->list;
  if (captures_only) generate_captures(board, list);
  else generate_moves(board, list);

  p->board = board;
  p->hist = hist;
  p->side = board->side;
  p->p1p = prev1_piece; p->p1t = prev1_to;
  p->p2p = prev2_piece; p->p2t = prev2_to;
  p->skip_bad = captures_only;
  p->k1 = captures_only ? 0 : hist->killers[ply * 2];
  p->k2 = captures_only ? 0 : hist->killers[ply * 2 + 1];
  p->counter = captures_only ? 0 : hist_get_counter_move(hist, prev1_piece, prev1_to);
  p->tt_move = tt_move;
  p->tt_ok = false;

  int count = list->count;
  int nc = 0, nq = 0, nb = 0;
  const int8_t *mailbox = board->mailbox;

  for (int i = 0; i < count; i++) {
    int m = list->moves[i];
    if (m == tt_move) { p->tt_ok = true; continue; }
    if (m & (MOVE_CAPTURE | 0xF0000)) {
      int promoted = (m >> 16) & 15;
      int attacker = SEE_VALUE[PIECE_TYPE[(m >> 12) & 15]];
      int victim_pt = 0;
      if (!(m & MOVE_ENPASSANT) && (m & MOVE_CAPTURE)) victim_pt = PIECE_TYPE[mailbox[(m >> 6) & 63]];
      int victim = (m & MOVE_CAPTURE) ? SEE_VALUE[victim_pt] : 0;
      if (promoted != 0) {
        if (PIECE_TYPE[promoted] == 4) {
          p->caps[nc] = m;
          p->cap_scores[nc++] = 8 * SEE_VALUE[4] + 8 * victim;
        } else {
          p->bad[nb] = m;
          p->bad_scores[nb++] = PIECE_TYPE[promoted] * 16 + victim;
        }
      } else {
        p->caps[nc] = m;
        p->cap_scores[nc++] = 8 * victim - (attacker >> 5) + (hist_get_cap(hist, m, victim_pt) >> 4);
      }
    } else {
      p->quiets[nq++] = m;
    }
  }
  p->n_caps = nc; p->cap_i = 0;
  p->n_quiets = nq; p->quiet_i = 0; p->quiets_scored = false;
  p->n_bad = nb; p->bad_i = 0;
  p->stage = ST_TT;
}

static void score_quiets(MovePicker *p) {
  const HistoryTables *hist = p->hist;
  for (int i = 0; i < p->n_quiets; i++) {
    int m = p->quiets[i];
    if (m == p->k1) p->quiet_scores[i] = SCORE_KILLER_1;
    else if (m == p->k2) p->quiet_scores[i] = SCORE_KILLER_2;
    else if (m == p->counter) p->quiet_scores[i] = SCORE_COUNTERMOVE;
    else p->quiet_scores[i] = hist_get_history(hist, p->side, m) +
                              hist_get_cont(hist, p->p1p, p->p1t, m) +
                              hist_get_cont(hist, p->p2p, p->p2t, m);
  }
  p->quiets_scored = true;
}

static int select_best(int32_t *moves, int32_t *scores, int i, int n) {
  int best = i, bs = scores[i];
  for (int j = i + 1; j < n; j++) {
    if (scores[j] > bs) { bs = scores[j]; best = j; }
  }
  int m = moves[best];
  moves[best] = moves[i]; scores[best] = scores[i];
  moves[i] = m; scores[i] = bs;
  return m;
}

int picker_next(MovePicker *p) {
  switch (p->stage) {
    case ST_TT:
      p->stage = ST_CAP;
      if (p->tt_ok) {
        p->score = SCORE_TT_MOVE;
        p->stage_of = ST_TT;
        p->see_known = false;
        return p->tt_move;
      }
      /* fallthrough */
    case ST_CAP:
      while (p->cap_i < p->n_caps) {
        int i = p->cap_i;
        int m = select_best(p->caps, p->cap_scores, i, p->n_caps);
        int bs = p->cap_scores[i];
        p->cap_i = i + 1;

        bool good = true;
        p->see_known = false;
        if (!(m & 0xF0000)) {
          int victim_pt = (m & MOVE_ENPASSANT) ? 0 : PIECE_TYPE[p->board->mailbox[(m >> 6) & 63]];
          if (SEE_VALUE[victim_pt] < SEE_VALUE[PIECE_TYPE[(m >> 12) & 15]]) {
            int s = see_value(p->board, m);
            p->see = s;
            p->see_known = true;
            good = s >= 0;
          }
        }
        if (good) {
          p->score = bs;
          p->stage_of = ST_CAP;
          return m;
        }
        if (!p->skip_bad) {
          p->bad[p->n_bad] = m;
          p->bad_scores[p->n_bad++] = bs - (1 << 20);
        }
      }
      p->stage = ST_QUIET;
      /* fallthrough */
    case ST_QUIET:
      if (p->skip_bad) { p->stage = ST_DONE; return 0; }
      if (!p->quiets_scored) score_quiets(p);
      if (p->quiet_i < p->n_quiets) {
        int i = p->quiet_i;
        int m = select_best(p->quiets, p->quiet_scores, i, p->n_quiets);
        p->quiet_i = i + 1;
        p->score = p->quiet_scores[i];
        p->stage_of = ST_QUIET;
        p->see_known = false;
        return m;
      }
      p->stage = ST_BAD;
      /* fallthrough */
    case ST_BAD:
      if (p->bad_i < p->n_bad) {
        int i = p->bad_i;
        int m = select_best(p->bad, p->bad_scores, i, p->n_bad);
        p->bad_i = i + 1;
        p->score = p->bad_scores[i];
        p->stage_of = ST_BAD;
        p->see_known = false;
        return m;
      }
      p->stage = ST_DONE;
      return 0;
    default:
      return 0;
  }
}
