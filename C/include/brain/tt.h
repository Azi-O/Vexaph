#ifndef VEXAPH_BRAIN_TT_H
#define VEXAPH_BRAIN_TT_H

#include "core/types.h"

enum { BOUND_NONE = 0, BOUND_UPPER = 1, BOUND_LOWER = 2, BOUND_EXACT = 3 };
#define CLUSTER_SIZE 4
#define BYTES_PER_CLUSTER 64

typedef struct {
  uint64_t a;
  uint64_t b;
} TTEntry;

typedef struct {
  TTEntry *entries;
  uint64_t cluster_count;
  uint64_t cluster_mask;
  int age;
  int size_mb;
  int hit_score;
  int hit_eval;
  int hit_depth;
  int hit_bound;
  int hit_move;
} TranspositionTable;

int score_to_tt(int score, int ply);
int score_from_tt(int score, int ply);

void tt_init(TranspositionTable *t, int size_mb);
void tt_free(TranspositionTable *t);
void tt_resize(TranspositionTable *t, int size_mb);
void tt_clear(TranspositionTable *t);
void tt_new_search(TranspositionTable *t);
void tt_attach(TranspositionTable *t, const TranspositionTable *shared);
bool tt_probe(TranspositionTable *t, U64 hash, int ply);
void tt_store(TranspositionTable *t, U64 hash, int depth, int score, int eval_score, int bound, int move, int ply);
int tt_hashfull(const TranspositionTable *t);

#endif
