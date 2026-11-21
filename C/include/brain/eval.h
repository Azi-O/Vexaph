#ifndef VEXAPH_BRAIN_EVAL_H
#define VEXAPH_BRAIN_EVAL_H

#include "core/board.h"
#include "brain/nnue.h"

#define NNUE_SCALE_BASE 4900
#define NNUE_SCALE_NPM_DIV 1000000
#define NNUE_SCALE_DIV 10000
#define NNUE_CP_LIMIT 20000

typedef struct {
  int score;
  int pawn_mg;
  int pawn_eg;
  int white_mobility_mg;
  int black_mobility_mg;
  int white_king_units;
  int black_king_units;
  int classical;
  int nnue_raw;
  bool nnue_active;
} EvalTrace;

void init_eval(void);
int evaluate(const Board *b);
int evaluate_classical(const Board *b);
int evaluate_with(const Board *b, NnueStack *st, int ply);
EvalTrace evaluate_trace(const Board *b);

#endif
