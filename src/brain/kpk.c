#include <stdlib.h>
#include <string.h>
#include "brain/kpk.h"

enum { KPK_INVALID = 0, KPK_UNKNOWN = 1, KPK_DRAW = 2, KPK_WIN = 4 };
#define TABLE_SIZE (2 * 24 * 64 * 64)

static int KING_STEPS[64][8];
static int KING_STEP_COUNT[64];
static uint8_t *table = NULL;

static void init_steps(void) {
  for (int s = 0; s < 64; s++) {
    int n = 0;
    int f = s & 7, r = s >> 3;
    for (int df = -1; df <= 1; df++) {
      for (int dr = -1; dr <= 1; dr++) {
        if (df == 0 && dr == 0) continue;
        int nf = f + df, nr = r + dr;
        if (nf < 0 || nf > 7 || nr < 0 || nr > 7) continue;
        KING_STEPS[s][n++] = nr * 8 + nf;
      }
    }
    KING_STEP_COUNT[s] = n;
  }
}

static int dist(int a, int b) {
  int df = (a & 7) - (b & 7); if (df < 0) df = -df;
  int dr = (a >> 3) - (b >> 3); if (dr < 0) dr = -dr;
  return df > dr ? df : dr;
}

static bool adjacent(int a, int b) { return dist(a, b) <= 1 && a != b; }

static bool pawn_hits(int psq, int sq) {
  int f = psq & 7;
  if (f > 0 && psq + 7 == sq) return true;
  if (f < 7 && psq + 9 == sq) return true;
  return false;
}

static int index_of(int wk, int bk, int stm, int psq) {
  return wk | (bk << 6) | (stm << 12) | ((psq & 7) << 13) | ((6 - (psq >> 3)) << 15);
}

static int initial_state(int wk, int bk, int stm, int psq) {
  if (wk == bk || adjacent(wk, bk) || wk == psq || bk == psq) return KPK_INVALID;
  if (stm == 0 && pawn_hits(psq, bk)) return KPK_INVALID;

  if (stm == 0 && (psq >> 3) == 6) {
    int front = psq + 8;
    if (wk != front && (dist(bk, front) > 1 || dist(wk, front) == 1)) return KPK_WIN;
  }

  if (stm == 1) {
    int free_n = 0;
    for (int i = 0; i < KING_STEP_COUNT[bk]; i++) {
      int t = KING_STEPS[bk][i];
      if (adjacent(t, wk) || pawn_hits(psq, t)) continue;
      free_n++;
    }
    if (free_n == 0) return KPK_DRAW;
    if (adjacent(bk, psq) && !adjacent(psq, wk)) return KPK_DRAW;
  }
  return KPK_UNKNOWN;
}

static int classify(const uint8_t *t, int wk, int bk, int stm, int psq) {
  int good = stm == 0 ? KPK_WIN : KPK_DRAW;
  int bad = stm == 0 ? KPK_DRAW : KPK_WIN;
  int r = KPK_INVALID;
  if (stm == 0) {
    for (int i = 0; i < KING_STEP_COUNT[wk]; i++) {
      int s = KING_STEPS[wk][i];
      if (adjacent(s, bk) || s == psq) continue;
      r |= t[index_of(s, bk, 1, psq)];
    }
    int rank = psq >> 3;
    if (rank < 6) {
      int s1 = psq + 8;
      if (s1 != wk && s1 != bk) {
        r |= t[index_of(wk, bk, 1, s1)];
        if (rank == 1) {
          int s2 = s1 + 8;
          if (s2 != wk && s2 != bk) r |= t[index_of(wk, bk, 1, s2)];
        }
      }
    }
  } else {
    for (int i = 0; i < KING_STEP_COUNT[bk]; i++) {
      int s = KING_STEPS[bk][i];
      if (adjacent(s, wk) || pawn_hits(psq, s)) continue;
      r |= t[index_of(wk, s, 0, psq)];
    }
  }
  if (r & good) return good;
  if (r & KPK_UNKNOWN) return KPK_UNKNOWN;
  return bad;
}

void init_kpk(void) {
  if (table) return;
  init_steps();
  uint8_t *t = malloc(TABLE_SIZE);
  for (int i = 0; i < TABLE_SIZE; i++) {
    int rank_code = (i >> 15) & 7;
    if (rank_code > 5) { t[i] = KPK_INVALID; continue; }
    int psq = (6 - rank_code) * 8 + ((i >> 13) & 3);
    t[i] = (uint8_t)initial_state(i & 63, (i >> 6) & 63, (i >> 12) & 1, psq);
  }
  bool changed = true;
  while (changed) {
    changed = false;
    for (int i = 0; i < TABLE_SIZE; i++) {
      if (t[i] != KPK_UNKNOWN) continue;
      int rank_code = (i >> 15) & 7;
      int psq = (6 - rank_code) * 8 + ((i >> 13) & 3);
      int r = classify(t, i & 63, (i >> 6) & 63, (i >> 12) & 1, psq);
      if (r != KPK_UNKNOWN) {
        t[i] = (uint8_t)r;
        changed = true;
      }
    }
  }
  table = t;
}

static int to_internal(int engine_sq, bool white) {
  int file = engine_sq & 7;
  int row = engine_sq >> 3;
  int rank = white ? 7 - row : row;
  return rank * 8 + file;
}

static int mirror_file(int s) { return (s & 56) | (7 - (s & 7)); }

bool kpk_is_win(int strong_king, int strong_pawn, int weak_king, bool strong_to_move, bool strong_is_white) {
  if (!table) init_kpk();
  int wk = to_internal(strong_king, strong_is_white);
  int psq = to_internal(strong_pawn, strong_is_white);
  int bk = to_internal(weak_king, strong_is_white);
  if ((psq & 7) > 3) {
    wk = mirror_file(wk);
    psq = mirror_file(psq);
    bk = mirror_file(bk);
  }
  return table[index_of(wk, bk, strong_to_move ? 0 : 1, psq)] == KPK_WIN;
}

int kpk_win_count(void) {
  if (!table) init_kpk();
  int n = 0;
  for (int i = 0; i < TABLE_SIZE; i++) if (table[i] == KPK_WIN) n++;
  return n;
}
