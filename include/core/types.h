#ifndef VEXAPH_CORE_TYPES_H
#define VEXAPH_CORE_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

typedef uint64_t U64;

#define ENGINE_NAME "Vexaph"
#define ENGINE_AUTHOR "Vexaph Team"
#define ENGINE_VERSION "1.0"

enum {
  a8 = 0, b8, c8, d8, e8, f8, g8, h8,
  a7, b7, c7, d7, e7, f7, g7, h7,
  a6, b6, c6, d6, e6, f6, g6, h6,
  a5, b5, c5, d5, e5, f5, g5, h5,
  a4, b4, c4, d4, e4, f4, g4, h4,
  a3, b3, c3, d3, e3, f3, g3, h3,
  a2, b2, c2, d2, e2, f2, g2, h2,
  a1, b1, c1, d1, e1, f1, g1, h1,
  NO_SQ = 64
};

enum {
  W_PAWN = 0, W_KNIGHT, W_BISHOP, W_ROOK, W_QUEEN, W_KING,
  B_PAWN, B_KNIGHT, B_BISHOP, B_ROOK, B_QUEEN, B_KING,
  NO_PIECE = 12
};

enum { PAWN = 0, KNIGHT, BISHOP, ROOK, QUEEN, KING };
enum { WHITE = 0, BLACK = 1, BOTH = 2 };
enum { WK = 1, WQ = 2, BK = 4, BQ = 8 };

#define MAX_PLY 128
#define MAX_MOVES 256
#define INFINITE_SCORE 32000
#define MATE_SCORE 31000
#define MATE_IN_MAX (MATE_SCORE - MAX_PLY)
#define NO_SCORE 32001
#define START_FEN "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"

#define MOVE_CAPTURE (1 << 20)
#define MOVE_DOUBLE (1 << 21)
#define MOVE_ENPASSANT (1 << 22)
#define MOVE_CASTLING (1 << 23)
#define NO_MOVE 0

extern const char *const SQUARE_NAMES[64];
extern const char PIECE_CHARS[13];
extern const int PIECE_VALUE[13];
extern const int SEE_VALUE[6];
extern const int8_t PIECE_COLOR[13];
extern const int8_t PIECE_TYPE[13];
extern uint8_t CASTLE_MASK[64];

#define SQ_BB(sq) (1ULL << (sq))

static inline int count_bits(U64 bb) { return __builtin_popcountll(bb); }
static inline int get_ls1b_index(U64 bb) { return bb ? __builtin_ctzll(bb) : -1; }
static inline int get_ms1b_index(U64 bb) { return bb ? 63 - __builtin_clzll(bb) : -1; }

static inline int encode_move(int src, int dst, int piece, int promoted, int capture,
                              int dbl, int ep, int castling) {
  return src | (dst << 6) | (piece << 12) | (promoted << 16) |
         (capture ? MOVE_CAPTURE : 0) | (dbl ? MOVE_DOUBLE : 0) |
         (ep ? MOVE_ENPASSANT : 0) | (castling ? MOVE_CASTLING : 0);
}

typedef struct {
  int32_t moves[MAX_MOVES];
  int32_t scores[MAX_MOVES];
  int count;
} MoveList;

static inline void movelist_add(MoveList *l, int m) { l->moves[l->count++] = m; }

typedef struct {
  int32_t move;
  int32_t captured_piece;
  int32_t castle;
  int32_t enpassant;
  int32_t fifty;
  U64 hash_key;
  U64 pawn_hash_key;
} UndoInfo;

typedef struct { U64 state; } SplitMix64;

void splitmix64_seed(SplitMix64 *r, U64 seed);
U64 splitmix64_next(SplitMix64 *r);
U64 splitmix64_sparse(SplitMix64 *r);

void init_types(void);

#endif
