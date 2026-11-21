#ifndef VEXAPH_BRAIN_NNUE_H
#define VEXAPH_BRAIN_NNUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "core/board.h"

#define NNUE_DEFAULT_FILE "nn-190f102a22c3.nnue"
#define NNUE_HALF_DIMS 512
#define NNUE_FEATURES 45056
#define NNUE_BUCKETS 8
#define NNUE_STACK_SIZE (MAX_PLY + 16)
#define NNUE_MAX_CHAIN 6

typedef struct {
  int8_t pc;
  int8_t from;
  int8_t to;
} NnueDirty;

typedef struct {
  int16_t acc[2][NNUE_HALF_DIMS];
  int32_t psqt[2][NNUE_BUCKETS];
  NnueDirty d[3];
  int8_t nd;
  int8_t king_color;
  uint8_t computed[2];
} NnueEntry;

typedef struct NnueStack {
  NnueEntry e[NNUE_STACK_SIZE];
} NnueStack;

bool nnue_load(const char *path, char *err, size_t err_cap);
void nnue_unload(void);
bool nnue_loaded(void);
const char *nnue_loaded_path(void);
void nnue_set_enabled(bool on);
bool nnue_enabled(void);
bool nnue_active(void);
const char *nnue_simd_name(void);

bool nnue_autoload(const char *argv0, char *msg, size_t cap);
const char *nnue_find_file(const char *name, const char *argv0, char *out, size_t cap);

NnueStack *nnue_stack_create(void);
void nnue_stack_free(NnueStack *st);
void nnue_stack_reset_root(NnueStack *st);
void nnue_push_move(NnueStack *st, int ply, const Board *b, int move);
void nnue_push_null(NnueStack *st, int ply);

int nnue_raw_stack(NnueStack *st, const Board *b, int ply);
int nnue_raw_board(const Board *b);
int nnue_raw_reference(const Board *b);

#endif
