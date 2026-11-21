#ifndef VEXAPH_BRAIN_EVAL_PAWN_H
#define VEXAPH_BRAIN_EVAL_PAWN_H

#include "core/board.h"

#define PAWN_HASH_SIZE 65536

typedef struct {
  int mg;
  int eg;
  U64 passed_white;
  U64 passed_black;
} PawnEval;

typedef struct {
  uint64_t probes;
  uint64_t hits;
  int size;
} PawnHashStats;

void clear_pawn_hash(void);
PawnHashStats pawn_hash_stats(void);
const PawnEval *eval_pawns(const Board *b);

#endif
