#include "core/types.h"

const char *const SQUARE_NAMES[64] = {
  "a8", "b8", "c8", "d8", "e8", "f8", "g8", "h8",
  "a7", "b7", "c7", "d7", "e7", "f7", "g7", "h7",
  "a6", "b6", "c6", "d6", "e6", "f6", "g6", "h6",
  "a5", "b5", "c5", "d5", "e5", "f5", "g5", "h5",
  "a4", "b4", "c4", "d4", "e4", "f4", "g4", "h4",
  "a3", "b3", "c3", "d3", "e3", "f3", "g3", "h3",
  "a2", "b2", "c2", "d2", "e2", "f2", "g2", "h2",
  "a1", "b1", "c1", "d1", "e1", "f1", "g1", "h1",
};

const char PIECE_CHARS[13] = "PNBRQKpnbrqk";
const int PIECE_VALUE[13] = {100, 320, 330, 500, 900, 20000, 100, 320, 330, 500, 900, 20000, 0};
const int SEE_VALUE[6] = {100, 320, 330, 500, 900, 20000};
const int8_t PIECE_COLOR[13] = {0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2};
const int8_t PIECE_TYPE[13] = {0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 6};

uint8_t CASTLE_MASK[64];

void init_types(void) {
  for (int i = 0; i < 64; i++) CASTLE_MASK[i] = 15;
  CASTLE_MASK[a1] = 15 & ~WQ;
  CASTLE_MASK[e1] = 15 & ~(WK | WQ);
  CASTLE_MASK[h1] = 15 & ~WK;
  CASTLE_MASK[a8] = 15 & ~BQ;
  CASTLE_MASK[e8] = 15 & ~(BK | BQ);
  CASTLE_MASK[h8] = 15 & ~BK;
}

void splitmix64_seed(SplitMix64 *r, U64 seed) { r->state = seed; }

U64 splitmix64_next(SplitMix64 *r) {
  r->state += 0x9E3779B97F4A7C15ULL;
  U64 z = r->state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
  return z ^ (z >> 31);
}

U64 splitmix64_sparse(SplitMix64 *r) {
  U64 a = splitmix64_next(r);
  U64 b = splitmix64_next(r);
  U64 c = splitmix64_next(r);
  return a & b & c;
}
