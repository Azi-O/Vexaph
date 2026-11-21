#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "core/board.h"
#include "core/zobrist.h"

void board_init(Board *b) {
  memset(b, 0, sizeof(*b));
  for (int i = 0; i < 64; i++) b->mailbox[i] = NO_PIECE;
  b->king_sq[0] = NO_SQ;
  b->king_sq[1] = NO_SQ;
  b->side = WHITE;
  b->enpassant = NO_SQ;
  b->fullmove = 1;
  b->start_side = WHITE;
  b->undo_cap = INITIAL_UNDO_CAPACITY;
  b->undo_stack = calloc((size_t)b->undo_cap, sizeof(UndoInfo));
}

void board_free(Board *b) {
  free(b->undo_stack);
  b->undo_stack = NULL;
  b->undo_cap = 0;
}

void board_reset(Board *b) {
  memset(b->bitboards, 0, sizeof(b->bitboards));
  memset(b->occupancies, 0, sizeof(b->occupancies));
  for (int i = 0; i < 64; i++) b->mailbox[i] = NO_PIECE;
  b->king_sq[0] = NO_SQ;
  b->king_sq[1] = NO_SQ;
  b->side = WHITE;
  b->enpassant = NO_SQ;
  b->castle = 0;
  b->fifty = 0;
  b->ply = 0;
  b->hist_ply = 0;
  b->hash_key = 0;
  b->pawn_hash_key = 0;
  b->fullmove = 1;
  b->start_side = WHITE;
}

UndoInfo *board_push_undo(Board *b) {
  if (b->hist_ply >= b->undo_cap) {
    int ncap = b->undo_cap * 2;
    UndoInfo *n = realloc(b->undo_stack, (size_t)ncap * sizeof(UndoInfo));
    if (!n) abort();
    memset(n + b->undo_cap, 0, (size_t)(ncap - b->undo_cap) * sizeof(UndoInfo));
    b->undo_stack = n;
    b->undo_cap = ncap;
  }
  return &b->undo_stack[b->hist_ply++];
}

U64 board_generate_hash(const Board *b) {
  U64 h = 0;
  for (int sq = 0; sq < 64; sq++) {
    int pc = b->mailbox[sq];
    if (pc != NO_PIECE) h ^= piece_keys[pc][sq];
  }
  h ^= castle_keys[b->castle];
  if (b->enpassant != NO_SQ) h ^= enpassant_keys[b->enpassant];
  if (b->side == BLACK) h ^= side_key;
  return h;
}

U64 board_generate_pawn_hash(const Board *b) {
  U64 h = 0;
  for (int sq = 0; sq < 64; sq++) {
    int pc = b->mailbox[sq];
    if (pc == W_PAWN || pc == B_PAWN) h ^= piece_keys[pc][sq];
  }
  return h;
}

typedef struct {
  U64 bitboards[12];
  U64 occupancies[3];
  int8_t mailbox[64];
  int king_sq[2];
  int side, enpassant, castle, fifty, ply, hist_ply;
  U64 hash_key, pawn_hash_key;
  int fullmove, start_side;
} BoardSnapshot;

static void snapshot_save(const Board *b, BoardSnapshot *s) {
  memcpy(s->bitboards, b->bitboards, sizeof(s->bitboards));
  memcpy(s->occupancies, b->occupancies, sizeof(s->occupancies));
  memcpy(s->mailbox, b->mailbox, sizeof(s->mailbox));
  s->king_sq[0] = b->king_sq[0]; s->king_sq[1] = b->king_sq[1];
  s->side = b->side; s->enpassant = b->enpassant; s->castle = b->castle;
  s->fifty = b->fifty; s->ply = b->ply; s->hist_ply = b->hist_ply;
  s->hash_key = b->hash_key; s->pawn_hash_key = b->pawn_hash_key;
  s->fullmove = b->fullmove; s->start_side = b->start_side;
}

static void snapshot_restore(Board *b, const BoardSnapshot *s) {
  memcpy(b->bitboards, s->bitboards, sizeof(s->bitboards));
  memcpy(b->occupancies, s->occupancies, sizeof(s->occupancies));
  memcpy(b->mailbox, s->mailbox, sizeof(s->mailbox));
  b->king_sq[0] = s->king_sq[0]; b->king_sq[1] = s->king_sq[1];
  b->side = s->side; b->enpassant = s->enpassant; b->castle = s->castle;
  b->fifty = s->fifty; b->ply = s->ply; b->hist_ply = s->hist_ply;
  b->hash_key = s->hash_key; b->pawn_hash_key = s->pawn_hash_key;
  b->fullmove = s->fullmove; b->start_side = s->start_side;
}

static int parse_int_prefix(const char *s, long *out) {
  while (isspace((unsigned char)*s)) s++;
  char *end;
  long v = strtol(s, &end, 10);
  if (end == s) return 0;
  *out = v;
  return 1;
}

bool board_parse_fen(Board *b, const char *fen) {
  char buf[512];
  size_t n = strlen(fen);
  if (n >= sizeof(buf)) return false;
  memcpy(buf, fen, n + 1);

  char *parts[8];
  int np = 0;
  char *save = NULL;
  for (char *tok = strtok_r(buf, " \t\r\n\f\v", &save); tok && np < 8; tok = strtok_r(NULL, " \t\r\n\f\v", &save))
    parts[np++] = tok;
  if (np < 4) return false;

  BoardSnapshot snap;
  snapshot_save(b, &snap);
  board_reset(b);

#define FAIL() do { snapshot_restore(b, &snap); return false; } while (0)

  const char *p = parts[0];
  int rank = 0, file = 0;
  int rows = 1;
  for (const char *q = p; *q; q++) if (*q == '/') rows++;
  if (rows != 8) FAIL();
  for (; *p; p++) {
    char ch = *p;
    if (ch == '/') {
      if (file != 8) FAIL();
      rank++;
      file = 0;
      continue;
    }
    if (ch >= '1' && ch <= '8') {
      file += ch - '0';
    } else {
      const char *pos = strchr(PIECE_CHARS, ch);
      if (!pos || ch == '\0') FAIL();
      int pc = (int)(pos - PIECE_CHARS);
      if (file > 7) FAIL();
      put_piece(b, pc, rank * 8 + file);
      file++;
    }
  }
  if (file != 8) FAIL();

  if (count_bits(b->bitboards[W_KING]) != 1 || count_bits(b->bitboards[B_KING]) != 1) FAIL();

  if (strcmp(parts[1], "w") == 0) b->side = WHITE;
  else if (strcmp(parts[1], "b") == 0) b->side = BLACK;
  else FAIL();

  b->castle = 0;
  if (strcmp(parts[2], "-") != 0) {
    for (const char *c = parts[2]; *c; c++) {
      if (*c == 'K') b->castle |= WK;
      else if (*c == 'Q') b->castle |= WQ;
      else if (*c == 'k') b->castle |= BK;
      else if (*c == 'q') b->castle |= BQ;
      else FAIL();
    }
  }

  b->enpassant = NO_SQ;
  if (strcmp(parts[3], "-") != 0) {
    int idx = -1;
    for (int i = 0; i < 64; i++) if (strcmp(parts[3], SQUARE_NAMES[i]) == 0) { idx = i; break; }
    if (idx < 0) FAIL();
    b->enpassant = idx;
  }

  long v;
  b->fifty = 0;
  if (np > 4 && parse_int_prefix(parts[4], &v) && v >= 0) b->fifty = (int)(v > 100000 ? 100000 : v);
  b->fullmove = 1;
  if (np > 5 && parse_int_prefix(parts[5], &v) && v >= 1) b->fullmove = (int)(v > 100000 ? 100000 : v);
#undef FAIL

  b->ply = 0;
  b->hist_ply = 0;
  b->start_side = b->side;
  b->hash_key = board_generate_hash(b);
  b->pawn_hash_key = board_generate_pawn_hash(b);
  return true;
}

bool board_create(Board *b, const char *fen) {
  board_init(b);
  return board_parse_fen(b, fen);
}

void board_clone(Board *dst, const Board *src) {
  board_init(dst);
  BoardSnapshot s;
  snapshot_save(src, &s);
  snapshot_restore(dst, &s);
  while (dst->undo_cap < src->hist_ply) {
    dst->undo_cap *= 2;
  }
  dst->undo_stack = realloc(dst->undo_stack, (size_t)dst->undo_cap * sizeof(UndoInfo));
  memcpy(dst->undo_stack, src->undo_stack, (size_t)src->hist_ply * sizeof(UndoInfo));
}

int board_fullmove_number(const Board *b) {
  return b->fullmove + ((b->hist_ply + (b->start_side == BLACK ? 1 : 0)) >> 1);
}

int board_export_fen(const Board *b, char *out, size_t cap) {
  char tmp[160];
  int n = 0;
  for (int rank = 0; rank < 8; rank++) {
    int empty = 0;
    for (int file = 0; file < 8; file++) {
      int pc = b->mailbox[rank * 8 + file];
      if (pc == NO_PIECE) {
        empty++;
      } else {
        if (empty) { tmp[n++] = (char)('0' + empty); empty = 0; }
        tmp[n++] = PIECE_CHARS[pc];
      }
    }
    if (empty) tmp[n++] = (char)('0' + empty);
    if (rank < 7) tmp[n++] = '/';
  }
  tmp[n++] = ' ';
  tmp[n++] = b->side == WHITE ? 'w' : 'b';
  tmp[n++] = ' ';
  int c0 = n;
  if (b->castle & WK) tmp[n++] = 'K';
  if (b->castle & WQ) tmp[n++] = 'Q';
  if (b->castle & BK) tmp[n++] = 'k';
  if (b->castle & BQ) tmp[n++] = 'q';
  if (n == c0) tmp[n++] = '-';
  tmp[n++] = ' ';
  if (b->enpassant != NO_SQ) {
    tmp[n++] = SQUARE_NAMES[b->enpassant][0];
    tmp[n++] = SQUARE_NAMES[b->enpassant][1];
  } else {
    tmp[n++] = '-';
  }
  tmp[n] = 0;
  return snprintf(out, cap, "%s %d %d", tmp, b->fifty, board_fullmove_number(b));
}

int board_to_string(const Board *b, char *out, size_t cap) {
  size_t o = 0;
#define APPEND(...) do { if (o < cap) o += (size_t)snprintf(out + o, cap - o, __VA_ARGS__); } while (0)
  APPEND("\n");
  for (int rank = 0; rank < 8; rank++) {
    APPEND(" %d |", 8 - rank);
    for (int file = 0; file < 8; file++) {
      int pc = b->mailbox[rank * 8 + file];
      APPEND(" %c ", pc == NO_PIECE ? '.' : PIECE_CHARS[pc]);
    }
    APPEND("\n");
  }
  APPEND("    ------------------------\n");
  APPEND("     a  b  c  d  e  f  g  h\n\n");
  APPEND(" Side:      %s\n", b->side == WHITE ? "white" : "black");
  APPEND(" Enpassant: %s\n", b->enpassant != NO_SQ ? SQUARE_NAMES[b->enpassant] : "none");
  char c[5];
  int n = 0;
  if (b->castle & WK) c[n++] = 'K';
  if (b->castle & WQ) c[n++] = 'Q';
  if (b->castle & BK) c[n++] = 'k';
  if (b->castle & BQ) c[n++] = 'q';
  if (!n) c[n++] = '-';
  c[n] = 0;
  APPEND(" Castling:  %s\n", c);
  APPEND(" Fifty:     %d\n", b->fifty);
  APPEND(" Hash:      0x%016llx\n", (unsigned long long)b->hash_key);
#undef APPEND
  return (int)o;
}

int board_repetition_count(const Board *b) {
  int count = 0;
  int lowest = b->hist_ply - b->fifty;
  if (lowest < 0) lowest = 0;
  for (int j = b->hist_ply - 2; j >= lowest; j -= 2)
    if (b->undo_stack[j].hash_key == b->hash_key) count++;
  return count;
}

bool board_is_repetition(const Board *b) {
  int lowest = b->hist_ply - b->fifty;
  if (lowest < 0) lowest = 0;
  for (int j = b->hist_ply - 2; j >= lowest; j -= 2)
    if (b->undo_stack[j].hash_key == b->hash_key) return true;
  return false;
}

bool board_is_threefold(const Board *b) { return board_repetition_count(b) >= 2; }
bool board_is_fifty_move(const Board *b) { return b->fifty >= 100; }

bool board_is_material_draw(const Board *b) {
  const U64 *bb = b->bitboards;
  if (bb[W_PAWN] | bb[B_PAWN] | bb[W_ROOK] | bb[B_ROOK] | bb[W_QUEEN] | bb[B_QUEEN]) return false;
  int wn = count_bits(bb[W_KNIGHT]), bn = count_bits(bb[B_KNIGHT]);
  int wb = count_bits(bb[W_BISHOP]), bbn = count_bits(bb[B_BISHOP]);
  int minors = wn + bn + wb + bbn;
  if (minors <= 1) return true;
  if (wn == 0 && bn == 0 && wb == 1 && bbn == 1) {
    bool w_light = (bb[W_BISHOP] & LIGHT_SQUARES_BB) != 0;
    bool b_light = (bb[B_BISHOP] & LIGHT_SQUARES_BB) != 0;
    return w_light == b_light;
  }
  return false;
}

bool board_is_draw(const Board *b) {
  return board_is_fifty_move(b) || board_is_material_draw(b) || board_is_repetition(b);
}
