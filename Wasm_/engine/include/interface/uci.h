#ifndef VEXAPH_UCI_H
#define VEXAPH_UCI_H

#include "core/core.h"
#include "brain/brain.h"
#include "book/book.h"

#define ENGINE_NAME "Vexaph"
#define ENGINE_AUTHOR "Vexaph Team"
#define ENGINE_VERSION "1.0"
#define BENCH_FEN_COUNT 12

extern const char *const BENCH_FENS[BENCH_FEN_COUNT];

typedef void (*UciWrite)(const char *line, void *ud);

typedef struct {
  int Hash;
  int MoveOverhead;
  int Threads;
  bool OwnBook;
  int BookVariety;
  char EvalFile[1024];
  bool UseNNUE;
  int SkillLevel;
  bool LimitStrength;
  int Elo;
} UciOptions;

typedef struct UciEngine {
  UciWrite write;
  void *ud;
  Board board;
  Searcher *searcher;
  UciOptions options;
  bool quit;
  bool book_loaded;
  bool nnue_tried;
  volatile int own_stop;
  const char *eco_dir_hint;
  const char *argv0;
} UciEngine;

void init_engine_once(void);
UciEngine *uci_create(UciWrite write, void *ud, volatile int *stop_flag);
void uci_destroy(UciEngine *e);
void uci_set_paths(UciEngine *e, const char *eco_dir_hint, const char *argv0);
void uci_handle_line(UciEngine *e, const char *line);
void uci_ensure_book(UciEngine *e);
void uci_ensure_nnue(UciEngine *e);
uint64_t uci_cmd_perft(UciEngine *e, int depth);
uint64_t uci_cmd_bench(UciEngine *e, int depth);

#endif
