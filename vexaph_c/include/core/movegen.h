#ifndef VEXAPH_CORE_MOVEGEN_H
#define VEXAPH_CORE_MOVEGEN_H

#include "core/board.h"

bool is_square_attacked(const Board *b, int sq, int by_side);
bool in_check(const Board *b);
U64 attackers_to(const Board *b, int sq, U64 occ, int by_side);

void generate_moves(const Board *b, MoveList *list);
void generate_captures(const Board *b, MoveList *list);
void generate_quiets(const Board *b, MoveList *list);

bool make_move(Board *b, int move);
void unmake_move(Board *b);
void make_null_move(Board *b);
void unmake_null_move(Board *b);

int move_to_uci(int move, char *out);
int parse_move(Board *b, const char *str);
int generate_legal_moves(Board *b, int *out);

uint64_t perft(Board *b, int depth);

typedef struct {
  uint64_t total;
  int count;
  char moves[MAX_MOVES][8];
  uint64_t nodes[MAX_MOVES];
} PerftDivide;

void perft_divide(Board *b, int depth, PerftDivide *out);

#endif
