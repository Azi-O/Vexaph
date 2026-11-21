#ifndef VEXAPH_CORE_BOARD_H
#define VEXAPH_CORE_BOARD_H

#include "core/types.h"
#include "core/bitboard.h"

#define INITIAL_UNDO_CAPACITY 1024

typedef struct Board {
  U64 bitboards[12];
  U64 occupancies[3];
  int8_t mailbox[64];
  int king_sq[2];
  int side;
  int enpassant;
  int castle;
  int fifty;
  int ply;
  int hist_ply;
  U64 hash_key;
  U64 pawn_hash_key;
  int fullmove;
  int start_side;
  UndoInfo *undo_stack;
  int undo_cap;
} Board;

void board_init(Board *b);
void board_free(Board *b);
void board_reset(Board *b);
bool board_create(Board *b, const char *fen);
void board_clone(Board *dst, const Board *src);

static inline void put_piece(Board *b, int pc, int sq) {
  U64 bb = SQ_BB(sq);
  b->bitboards[pc] |= bb;
  b->occupancies[PIECE_COLOR[pc]] |= bb;
  b->occupancies[BOTH] |= bb;
  b->mailbox[sq] = (int8_t)pc;
  if (pc == W_KING) b->king_sq[WHITE] = sq;
  else if (pc == B_KING) b->king_sq[BLACK] = sq;
}

static inline void take_piece(Board *b, int pc, int sq) {
  U64 nb = ~SQ_BB(sq);
  b->bitboards[pc] &= nb;
  b->occupancies[PIECE_COLOR[pc]] &= nb;
  b->occupancies[BOTH] &= nb;
  b->mailbox[sq] = NO_PIECE;
}

UndoInfo *board_push_undo(Board *b);
U64 board_generate_hash(const Board *b);
U64 board_generate_pawn_hash(const Board *b);
bool board_parse_fen(Board *b, const char *fen);
int board_export_fen(const Board *b, char *out, size_t cap);
int board_fullmove_number(const Board *b);
int board_to_string(const Board *b, char *out, size_t cap);
int board_repetition_count(const Board *b);
bool board_is_repetition(const Board *b);
bool board_is_threefold(const Board *b);
bool board_is_fifty_move(const Board *b);
bool board_is_material_draw(const Board *b);
bool board_is_draw(const Board *b);

#endif
