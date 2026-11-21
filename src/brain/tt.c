#include <stdlib.h>
#include <string.h>
#include "brain/tt.h"

typedef struct {
  uint32_t key;
  int16_t score;
  int16_t eval;
  int32_t move;
  int8_t depth;
  uint8_t flags;
} TTData;

static inline TTData tte_read(const TTEntry *e) {
  uint64_t w1 = __atomic_load_n(&e->b, __ATOMIC_RELAXED);
  uint64_t w0 = __atomic_load_n(&e->a, __ATOMIC_RELAXED) ^ w1;
  TTData d;
  d.key = (uint32_t)w0;
  d.score = (int16_t)(uint16_t)(w0 >> 32);
  d.eval = (int16_t)(uint16_t)(w0 >> 48);
  d.move = (int32_t)(uint32_t)w1;
  d.depth = (int8_t)(uint8_t)(w1 >> 32);
  d.flags = (uint8_t)(w1 >> 40);
  return d;
}

static inline void tte_write(TTEntry *e, const TTData *d) {
  uint64_t w0 = (uint64_t)d->key | ((uint64_t)(uint16_t)d->score << 32) | ((uint64_t)(uint16_t)d->eval << 48);
  uint64_t w1 = (uint64_t)(uint32_t)d->move | ((uint64_t)(uint8_t)d->depth << 32) | ((uint64_t)d->flags << 40);
  __atomic_store_n(&e->a, w0 ^ w1, __ATOMIC_RELAXED);
  __atomic_store_n(&e->b, w1, __ATOMIC_RELAXED);
}

int score_to_tt(int score, int ply) {
  if (score >= MATE_IN_MAX) return score + ply;
  if (score <= -MATE_IN_MAX) return score - ply;
  return score;
}

int score_from_tt(int score, int ply) {
  if (score >= MATE_IN_MAX) return score - ply;
  if (score <= -MATE_IN_MAX) return score + ply;
  return score;
}

void tt_init(TranspositionTable *t, int size_mb) {
  memset(t, 0, sizeof(*t));
  tt_resize(t, size_mb);
}

void tt_free(TranspositionTable *t) {
  free(t->entries);
  t->entries = NULL;
}

void tt_attach(TranspositionTable *t, const TranspositionTable *shared) {
  t->entries = shared->entries;
  t->cluster_count = shared->cluster_count;
  t->cluster_mask = shared->cluster_mask;
  t->age = shared->age;
  t->size_mb = shared->size_mb;
}

void tt_resize(TranspositionTable *t, int size_mb) {
  int mb = size_mb < 1 ? 1 : (size_mb > 4096 ? 4096 : size_mb);
  uint64_t clusters = ((uint64_t)mb * 1024 * 1024) / BYTES_PER_CLUSTER;
  uint64_t pow2 = 1;
  while (pow2 * 2 <= clusters) pow2 *= 2;
  clusters = pow2;
  free(t->entries);
  t->size_mb = mb;
  t->cluster_count = clusters;
  t->cluster_mask = clusters - 1;
  t->entries = calloc((size_t)(clusters * CLUSTER_SIZE), sizeof(TTEntry));
  if (!t->entries) abort();
  t->age = 0;
}

void tt_clear(TranspositionTable *t) {
  memset(t->entries, 0, (size_t)(t->cluster_count * CLUSTER_SIZE) * sizeof(TTEntry));
  t->age = 0;
}

void tt_new_search(TranspositionTable *t) { t->age = (t->age + 1) & 63; }

bool tt_probe(TranspositionTable *t, U64 hash, int ply) {
  const TTEntry *base = t->entries + (size_t)((hash & 0xFFFFFFFFULL) & t->cluster_mask) * CLUSTER_SIZE;
  uint32_t key32 = (uint32_t)(hash >> 32);
  for (int i = 0; i < CLUSTER_SIZE; i++) {
    TTData d = tte_read(&base[i]);
    if (d.key == key32 && (d.flags & 3) != BOUND_NONE) {
      t->hit_score = score_from_tt(d.score, ply);
      t->hit_eval = d.eval;
      t->hit_depth = d.depth;
      t->hit_bound = d.flags & 3;
      t->hit_move = d.move;
      return true;
    }
  }
  return false;
}

void tt_store(TranspositionTable *t, U64 hash, int depth, int score, int eval_score, int bound, int move, int ply) {
  TTEntry *base = t->entries + (size_t)((hash & 0xFFFFFFFFULL) & t->cluster_mask) * CLUSTER_SIZE;
  uint32_t key32 = (uint32_t)(hash >> 32);

  TTEntry *slot = base;
  TTData sd;
  memset(&sd, 0, sizeof(sd));
  bool found = false;
  for (int i = 0; i < CLUSTER_SIZE; i++) {
    TTData d = tte_read(&base[i]);
    if (d.key == key32 && (d.flags & 3) != BOUND_NONE) {
      slot = &base[i];
      sd = d;
      found = true;
      break;
    }
  }

  if (!found) {
    int worst = 1 << 30;
    for (int i = 0; i < CLUSTER_SIZE; i++) {
      TTData d = tte_read(&base[i]);
      if ((d.flags & 3) == BOUND_NONE) { slot = &base[i]; worst = -(1 << 30); break; }
      int rel_age = (t->age - (d.flags >> 2) + 64) & 63;
      int value = d.depth - 8 * rel_age - ((d.flags & 3) == BOUND_EXACT ? 2 : 0);
      if (value < worst) { worst = value; slot = &base[i]; }
    }
  } else {
    if (move == 0) move = sd.move;
    int old_age = sd.flags >> 2;
    if (bound != BOUND_EXACT && depth + 4 < sd.depth && old_age == t->age) {
      sd.move = move;
      tte_write(slot, &sd);
      return;
    }
  }

  TTData nd;
  nd.key = key32;
  nd.score = (int16_t)score_to_tt(score, ply);
  nd.eval = (int16_t)eval_score;
  nd.depth = (int8_t)(depth > 127 ? 127 : (depth < -128 ? -128 : depth));
  nd.flags = (uint8_t)((t->age << 2) | bound);
  nd.move = move;
  tte_write(slot, &nd);
}

int tt_hashfull(const TranspositionTable *t) {
  uint64_t sample = t->cluster_count < 250 ? t->cluster_count : 250;
  uint64_t used = 0;
  for (uint64_t c = 0; c < sample; c++) {
    for (int i = 0; i < CLUSTER_SIZE; i++) {
      TTData d = tte_read(&t->entries[c * CLUSTER_SIZE + i]);
      if ((d.flags & 3) != BOUND_NONE && (d.flags >> 2) == t->age) used++;
    }
  }
  return (int)((used * 1000) / (sample * CLUSTER_SIZE));
}
