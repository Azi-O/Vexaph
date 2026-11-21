#include <stdlib.h>
#include "core/zobrist.h"

U64 piece_keys[12][64];
U64 enpassant_keys[65];
U64 castle_keys[16];
U64 side_key;

static bool zobrist_ready = false;

static int cmp_u64(const void *a, const void *b) {
  U64 x = *(const U64 *)a, y = *(const U64 *)b;
  return x < y ? -1 : (x > y ? 1 : 0);
}

#define ZOBRIST_COUNT (12 * 64 + 64 + 16 + 1)

void init_zobrist(void) {
  if (zobrist_ready) return;
  SplitMix64 rng;
  splitmix64_seed(&rng, 0x5EED0BADC0DE1234ULL);
  U64 seen[ZOBRIST_COUNT];
  int n = 0;

#define UNIQUE(dst) do { \
    U64 v = splitmix64_next(&rng); \
    for (;;) { \
      bool dup = (v == 0); \
      for (int i = 0; !dup && i < n; i++) if (seen[i] == v) dup = true; \
      if (!dup) break; \
      v = splitmix64_next(&rng); \
    } \
    seen[n++] = v; \
    (dst) = v; \
  } while (0)

  for (int pc = 0; pc < 12; pc++)
    for (int sq = 0; sq < 64; sq++) UNIQUE(piece_keys[pc][sq]);
  for (int sq = 0; sq < 64; sq++) UNIQUE(enpassant_keys[sq]);
  enpassant_keys[NO_SQ] = 0;
  for (int i = 0; i < 16; i++) UNIQUE(castle_keys[i]);
  UNIQUE(side_key);
#undef UNIQUE
  zobrist_ready = true;
}

bool zobrist_all_unique(void) {
  U64 all[ZOBRIST_COUNT];
  int n = 0;
  for (int pc = 0; pc < 12; pc++) for (int sq = 0; sq < 64; sq++) all[n++] = piece_keys[pc][sq];
  for (int sq = 0; sq < 64; sq++) all[n++] = enpassant_keys[sq];
  for (int i = 0; i < 16; i++) all[n++] = castle_keys[i];
  all[n++] = side_key;
  qsort(all, n, sizeof(U64), cmp_u64);
  for (int i = 1; i < n; i++) if (all[i] == all[i - 1]) return false;
  return true;
}
