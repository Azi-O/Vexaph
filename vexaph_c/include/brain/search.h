#ifndef VEXAPH_BRAIN_SEARCH_H
#define VEXAPH_BRAIN_SEARCH_H

#include "core/board.h"
#include "brain/tt.h"
#include "brain/movepick.h"
#include "brain/timeman.h"
#include "brain/nnue.h"

typedef struct {
  int32_t move[MAX_PLY + 8];
  int16_t eval[MAX_PLY + 8];
  int8_t piece[MAX_PLY + 8];
  int8_t to[MAX_PLY + 8];
  int8_t dext[MAX_PLY + 8];
} SearchStack;

typedef struct {
  int max_depth;
  int depth;
  int seldepth;
  uint64_t nodes;
  int score;
  int best_move;
  int time;
  uint64_t nps;
  int hashfull;
  int pv[MAX_PLY];
  int pv_len;
  int ponder_move;
} SearchInfo;

typedef void (*InfoCallback)(const SearchInfo *info, void *ud);

typedef struct Searcher {
  TranspositionTable tt;
  HistoryTables hist;
  SearchStack ss;
  TimeManager tm;
  Board *board;
  MovePicker *pickers;
  MovePicker *qpickers;
  MovePicker *pcpickers;
  int pv_table[MAX_PLY * MAX_PLY];
  int pv_length[MAX_PLY + 2];
  volatile int *stop_flag;
  volatile int own_stop;
  uint64_t nodes;
  int seldepth;
  bool stopped;
  int completed_depth;
  int64_t max_nodes;
  int move_overhead;
  int root_first;
  int iter_best;
  int iter_best_score;
  double root_move_nodes[4096];
  int nmp_min_ply;
  int nmp_side;
  int root_draw;
  NnueStack *nn;
  bool use_nnue;
  bool is_helper;
  int helper_id;
  int helper_max_depth;
  Board *own_board;
  struct Searcher **helpers;
  int n_helpers;
  void *smp_threads;
  volatile int smp_stop;
  int skill_level;
  bool limit_strength;
  int uci_elo;
  uint64_t rng;
  int root_excl[8];
  int root_excl_n;
} Searcher;

void init_search(void);
Searcher *searcher_create(int hash_mb);
void searcher_destroy(Searcher *s);
void searcher_set_stop_flag(Searcher *s, volatile int *flag);
void searcher_request_stop(Searcher *s);
void searcher_clear_stop(Searcher *s);
void searcher_resize_hash(Searcher *s, int mb);
void searcher_clear_hash(Searcher *s);
void searcher_new_game(Searcher *s);
void searcher_set_threads(Searcher *s, int n);
int searcher_get_threads(const Searcher *s);
void searcher_set_strength(Searcher *s, int skill_level, bool limit_strength, int elo);
int searcher_effective_skill(const Searcher *s);
uint64_t searcher_total_nodes(const Searcher *s);
int searcher_repetition_score(const Searcher *s, int ply);
void searcher_search(Searcher *s, Board *board, const SearchLimits *limits, InfoCallback cb, void *ud, SearchInfo *out);

int score_to_uci(int score, char *out);
int pv_to_uci(const int *pv, int len, char *out, size_t cap);

#endif
