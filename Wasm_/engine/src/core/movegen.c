#include <string.h>
#include <ctype.h>
#include "core/movegen.h"
#include "core/zobrist.h"

bool is_square_attacked(const Board *b, int sq, int by_side) {
  const U64 *bb = b->bitboards;
  U64 occ = b->occupancies[BOTH];
  if (by_side == WHITE) {
    if (pawn_attacks[BLACK][sq] & bb[W_PAWN]) return true;
    if (knight_attacks[sq] & bb[W_KNIGHT]) return true;
    if (king_attacks[sq] & bb[W_KING]) return true;
    if (get_bishop_attacks(sq, occ) & (bb[W_BISHOP] | bb[W_QUEEN])) return true;
    if (get_rook_attacks(sq, occ) & (bb[W_ROOK] | bb[W_QUEEN])) return true;
  } else {
    if (pawn_attacks[WHITE][sq] & bb[B_PAWN]) return true;
    if (knight_attacks[sq] & bb[B_KNIGHT]) return true;
    if (king_attacks[sq] & bb[B_KING]) return true;
    if (get_bishop_attacks(sq, occ) & (bb[B_BISHOP] | bb[B_QUEEN])) return true;
    if (get_rook_attacks(sq, occ) & (bb[B_ROOK] | bb[B_QUEEN])) return true;
  }
  return false;
}

bool in_check(const Board *b) {
  int side = b->side;
  return is_square_attacked(b, b->king_sq[side], side ^ 1);
}

U64 attackers_to(const Board *b, int sq, U64 occ, int by_side) {
  const U64 *bb = b->bitboards;
  if (by_side == WHITE) {
    return (pawn_attacks[BLACK][sq] & bb[W_PAWN]) |
           (knight_attacks[sq] & bb[W_KNIGHT]) |
           (king_attacks[sq] & bb[W_KING]) |
           (get_bishop_attacks(sq, occ) & (bb[W_BISHOP] | bb[W_QUEEN])) |
           (get_rook_attacks(sq, occ) & (bb[W_ROOK] | bb[W_QUEEN]));
  }
  return (pawn_attacks[WHITE][sq] & bb[B_PAWN]) |
         (knight_attacks[sq] & bb[B_KNIGHT]) |
         (king_attacks[sq] & bb[B_KING]) |
         (get_bishop_attacks(sq, occ) & (bb[B_BISHOP] | bb[B_QUEEN])) |
         (get_rook_attacks(sq, occ) & (bb[B_ROOK] | bb[B_QUEEN]));
}

static void add_promotions(MoveList *list, int src, int dst, int piece, int capture, int side) {
  int base = side == WHITE ? W_KNIGHT : B_KNIGHT;
  movelist_add(list, encode_move(src, dst, piece, base + 3, capture, 0, 0, 0));
  movelist_add(list, encode_move(src, dst, piece, base + 2, capture, 0, 0, 0));
  movelist_add(list, encode_move(src, dst, piece, base + 1, capture, 0, 0, 0));
  movelist_add(list, encode_move(src, dst, piece, base, capture, 0, 0, 0));
}

static void gen_pawn_moves(const Board *b, MoveList *list, int mode) {
  int side = b->side;
  const U64 *bbs = b->bitboards;
  U64 occ_all = b->occupancies[BOTH];
  U64 enemy = b->occupancies[side ^ 1];
  int piece = side == WHITE ? W_PAWN : B_PAWN;
  int forward = side == WHITE ? -8 : 8;
  int start_rank = side == WHITE ? 6 : 1;
  int promo_rank = side == WHITE ? 1 : 6;
  U64 pawns = bbs[piece];
  int ep = b->enpassant;

  while (pawns) {
    int src = __builtin_ctzll(pawns);
    pawns &= pawns - 1;
    int rank = src >> 3;
    int dst = src + forward;
    if (dst < 0 || dst > 63) continue;

    if (!(occ_all & SQ_BB(dst))) {
      if (rank == promo_rank) {
        if (mode != 2) add_promotions(list, src, dst, piece, 0, side);
      } else if (mode != 1) {
        movelist_add(list, encode_move(src, dst, piece, 0, 0, 0, 0, 0));
        if (rank == start_rank && !(occ_all & SQ_BB(dst + forward)))
          movelist_add(list, encode_move(src, dst + forward, piece, 0, 0, 1, 0, 0));
      }
    }

    if (mode != 2) {
      U64 caps = pawn_attacks[side][src] & enemy;
      while (caps) {
        int t = __builtin_ctzll(caps);
        caps &= caps - 1;
        if (rank == promo_rank) add_promotions(list, src, t, piece, 1, side);
        else movelist_add(list, encode_move(src, t, piece, 0, 1, 0, 0, 0));
      }
      if (ep != NO_SQ && (pawn_attacks[side][src] & SQ_BB(ep)))
        movelist_add(list, encode_move(src, ep, piece, 0, 1, 0, 1, 0));
    }
  }
}

static void gen_piece_moves(const Board *b, MoveList *list, int mode) {
  int side = b->side;
  const U64 *bbs = b->bitboards;
  U64 occ_all = b->occupancies[BOTH];
  U64 own = b->occupancies[side];
  U64 enemy = b->occupancies[side ^ 1];
  int off = side == WHITE ? 0 : 6;

  for (int type = KNIGHT; type <= QUEEN; type++) {
    U64 bb = bbs[type + off];
    while (bb) {
      int src = __builtin_ctzll(bb);
      bb &= bb - 1;
      U64 targets;
      switch (type) {
        case KNIGHT: targets = knight_attacks[src]; break;
        case BISHOP: targets = get_bishop_attacks(src, occ_all); break;
        case ROOK: targets = get_rook_attacks(src, occ_all); break;
        default: targets = get_queen_attacks(src, occ_all); break;
      }
      targets &= ~own;
      if (mode == 1) targets &= enemy; else if (mode == 2) targets &= ~enemy;
      while (targets) {
        int t = __builtin_ctzll(targets);
        targets &= targets - 1;
        movelist_add(list, encode_move(src, t, type + off, 0, (enemy & SQ_BB(t)) != 0, 0, 0, 0));
      }
    }
  }

  U64 bb = bbs[W_KING + off];
  if (bb) {
    int src = __builtin_ctzll(bb);
    U64 targets = king_attacks[src] & ~own;
    if (mode == 1) targets &= enemy; else if (mode == 2) targets &= ~enemy;
    while (targets) {
      int t = __builtin_ctzll(targets);
      targets &= targets - 1;
      movelist_add(list, encode_move(src, t, W_KING + off, 0, (enemy & SQ_BB(t)) != 0, 0, 0, 0));
    }
  }
}

static void gen_castling(const Board *b, MoveList *list) {
  U64 occ = b->occupancies[BOTH];
  if (b->side == WHITE) {
    if (b->castle & WK) {
      if (!(occ & (SQ_BB(f1) | SQ_BB(g1))) &&
          !is_square_attacked(b, e1, BLACK) && !is_square_attacked(b, f1, BLACK) &&
          !is_square_attacked(b, g1, BLACK))
        movelist_add(list, encode_move(e1, g1, W_KING, 0, 0, 0, 0, 1));
    }
    if (b->castle & WQ) {
      if (!(occ & (SQ_BB(b1) | SQ_BB(c1) | SQ_BB(d1))) &&
          !is_square_attacked(b, e1, BLACK) && !is_square_attacked(b, d1, BLACK) &&
          !is_square_attacked(b, c1, BLACK))
        movelist_add(list, encode_move(e1, c1, W_KING, 0, 0, 0, 0, 1));
    }
  } else {
    if (b->castle & BK) {
      if (!(occ & (SQ_BB(f8) | SQ_BB(g8))) &&
          !is_square_attacked(b, e8, WHITE) && !is_square_attacked(b, f8, WHITE) &&
          !is_square_attacked(b, g8, WHITE))
        movelist_add(list, encode_move(e8, g8, B_KING, 0, 0, 0, 0, 1));
    }
    if (b->castle & BQ) {
      if (!(occ & (SQ_BB(b8) | SQ_BB(c8) | SQ_BB(d8))) &&
          !is_square_attacked(b, e8, WHITE) && !is_square_attacked(b, d8, WHITE) &&
          !is_square_attacked(b, c8, WHITE))
        movelist_add(list, encode_move(e8, c8, B_KING, 0, 0, 0, 0, 1));
    }
  }
}

void generate_moves(const Board *b, MoveList *list) {
  list->count = 0;
  gen_pawn_moves(b, list, 0);
  gen_piece_moves(b, list, 0);
  gen_castling(b, list);
}

void generate_captures(const Board *b, MoveList *list) {
  list->count = 0;
  gen_pawn_moves(b, list, 1);
  gen_piece_moves(b, list, 1);
}

void generate_quiets(const Board *b, MoveList *list) {
  list->count = 0;
  gen_pawn_moves(b, list, 2);
  gen_piece_moves(b, list, 2);
  gen_castling(b, list);
}

static void castle_rook(int dst, int *rook_src, int *rook_dst, int *rook) {
  if (dst == g1) { *rook_src = h1; *rook_dst = f1; *rook = W_ROOK; }
  else if (dst == c1) { *rook_src = a1; *rook_dst = d1; *rook = W_ROOK; }
  else if (dst == g8) { *rook_src = h8; *rook_dst = f8; *rook = B_ROOK; }
  else { *rook_src = a8; *rook_dst = d8; *rook = B_ROOK; }
}

bool make_move(Board *b, int move) {
  UndoInfo *u = board_push_undo(b);
  int side = b->side;
  int src = move & 63;
  int dst = (move >> 6) & 63;
  int piece = (move >> 12) & 15;
  int promoted = (move >> 16) & 15;

  u->move = move;
  u->captured_piece = NO_PIECE;
  u->castle = b->castle;
  u->enpassant = b->enpassant;
  u->fifty = b->fifty;
  u->hash_key = b->hash_key;
  u->pawn_hash_key = b->pawn_hash_key;

  U64 hash = b->hash_key;
  U64 phash = b->pawn_hash_key;

  hash ^= enpassant_keys[b->enpassant];
  hash ^= castle_keys[b->castle];
  b->enpassant = NO_SQ;

  bool is_pawn = piece == W_PAWN || piece == B_PAWN;

  take_piece(b, piece, src);
  hash ^= piece_keys[piece][src];
  if (is_pawn) phash ^= piece_keys[piece][src];

  if (move & MOVE_ENPASSANT) {
    int cap_sq = side == WHITE ? dst + 8 : dst - 8;
    int cap_piece = side == WHITE ? B_PAWN : W_PAWN;
    take_piece(b, cap_piece, cap_sq);
    hash ^= piece_keys[cap_piece][cap_sq];
    phash ^= piece_keys[cap_piece][cap_sq];
    u->captured_piece = cap_piece;
  } else if (move & MOVE_CAPTURE) {
    int cap_piece = b->mailbox[dst];
    take_piece(b, cap_piece, dst);
    hash ^= piece_keys[cap_piece][dst];
    if (cap_piece == W_PAWN || cap_piece == B_PAWN) phash ^= piece_keys[cap_piece][dst];
    u->captured_piece = cap_piece;
  }

  int placed = promoted != 0 ? promoted : piece;
  put_piece(b, placed, dst);
  hash ^= piece_keys[placed][dst];
  if (placed == W_PAWN || placed == B_PAWN) phash ^= piece_keys[placed][dst];

  if (move & MOVE_DOUBLE) {
    int ep_sq = side == WHITE ? dst + 8 : dst - 8;
    int enemy_pawn = side == WHITE ? B_PAWN : W_PAWN;
    if (pawn_attacks[side][ep_sq] & b->bitboards[enemy_pawn]) {
      b->enpassant = ep_sq;
      hash ^= enpassant_keys[ep_sq];
    }
  }

  if (move & MOVE_CASTLING) {
    int rs, rd, rk;
    castle_rook(dst, &rs, &rd, &rk);
    take_piece(b, rk, rs);
    put_piece(b, rk, rd);
    hash ^= piece_keys[rk][rs] ^ piece_keys[rk][rd];
  }

  b->castle &= CASTLE_MASK[src] & CASTLE_MASK[dst];
  hash ^= castle_keys[b->castle];

  if (is_pawn || u->captured_piece != NO_PIECE) b->fifty = 0;
  else b->fifty++;

  b->side = side ^ 1;
  hash ^= side_key;
  b->hash_key = hash;
  b->pawn_hash_key = phash;
  b->ply++;

  if (is_square_attacked(b, b->king_sq[side], side ^ 1)) {
    unmake_move(b);
    return false;
  }
  return true;
}

void unmake_move(Board *b) {
  UndoInfo *u = &b->undo_stack[--b->hist_ply];
  int move = u->move;
  int src = move & 63;
  int dst = (move >> 6) & 63;
  int piece = (move >> 12) & 15;
  int promoted = (move >> 16) & 15;

  b->side ^= 1;
  int side = b->side;
  b->ply--;

  int placed = promoted != 0 ? promoted : piece;
  take_piece(b, placed, dst);
  put_piece(b, piece, src);

  if (move & MOVE_ENPASSANT) {
    int cap_sq = side == WHITE ? dst + 8 : dst - 8;
    put_piece(b, u->captured_piece, cap_sq);
  } else if (move & MOVE_CAPTURE) {
    put_piece(b, u->captured_piece, dst);
  }

  if (move & MOVE_CASTLING) {
    int rs, rd, rk;
    castle_rook(dst, &rs, &rd, &rk);
    take_piece(b, rk, rd);
    put_piece(b, rk, rs);
  }

  b->castle = u->castle;
  b->enpassant = u->enpassant;
  b->fifty = u->fifty;
  b->hash_key = u->hash_key;
  b->pawn_hash_key = u->pawn_hash_key;
}

void make_null_move(Board *b) {
  UndoInfo *u = board_push_undo(b);
  u->move = 0;
  u->captured_piece = NO_PIECE;
  u->castle = b->castle;
  u->enpassant = b->enpassant;
  u->fifty = b->fifty;
  u->hash_key = b->hash_key;
  u->pawn_hash_key = b->pawn_hash_key;

  b->hash_key ^= enpassant_keys[b->enpassant];
  b->enpassant = NO_SQ;
  b->side ^= 1;
  b->hash_key ^= side_key;
  b->fifty++;
  b->ply++;
}

void unmake_null_move(Board *b) {
  UndoInfo *u = &b->undo_stack[--b->hist_ply];
  b->side ^= 1;
  b->ply--;
  b->castle = u->castle;
  b->enpassant = u->enpassant;
  b->fifty = u->fifty;
  b->hash_key = u->hash_key;
  b->pawn_hash_key = u->pawn_hash_key;
}

int move_to_uci(int move, char *out) {
  if (move == 0) { strcpy(out, "0000"); return 4; }
  int src = move & 63;
  int dst = (move >> 6) & 63;
  int promoted = (move >> 16) & 15;
  out[0] = SQUARE_NAMES[src][0];
  out[1] = SQUARE_NAMES[src][1];
  out[2] = SQUARE_NAMES[dst][0];
  out[3] = SQUARE_NAMES[dst][1];
  int n = 4;
  if (promoted != 0) out[n++] = "nbrq"[(promoted % 6) - 1];
  out[n] = 0;
  return n;
}

int parse_move(Board *b, const char *str) {
  size_t len = strlen(str);
  if (len < 4 || len > 7) return 0;
  char wanted[8];
  for (size_t i = 0; i < len; i++) wanted[i] = (char)tolower((unsigned char)str[i]);
  wanted[len] = 0;
  MoveList list;
  generate_moves(b, &list);
  for (int i = 0; i < list.count; i++) {
    int m = list.moves[i];
    char u[8];
    move_to_uci(m, u);
    if (strcmp(u, wanted) != 0) continue;
    if (make_move(b, m)) {
      unmake_move(b);
      return m;
    }
  }
  return 0;
}

int generate_legal_moves(Board *b, int *out) {
  MoveList list;
  generate_moves(b, &list);
  int n = 0;
  for (int i = 0; i < list.count; i++) {
    int m = list.moves[i];
    if (make_move(b, m)) {
      unmake_move(b);
      out[n++] = m;
    }
  }
  return n;
}

static uint64_t perft_rec(Board *b, int depth) {
  if (depth == 0) return 1;
  MoveList list;
  generate_moves(b, &list);
  uint64_t nodes = 0;
  int count = list.count;
  for (int i = 0; i < count; i++) {
    int m = list.moves[i];
    if (!make_move(b, m)) continue;
    nodes += depth == 1 ? 1 : perft_rec(b, depth - 1);
    unmake_move(b);
  }
  return nodes;
}

uint64_t perft(Board *b, int depth) { return perft_rec(b, depth); }

void perft_divide(Board *b, int depth, PerftDivide *out) {
  MoveList list;
  generate_moves(b, &list);
  out->total = 0;
  out->count = 0;
  for (int i = 0; i < list.count; i++) {
    int m = list.moves[i];
    if (!make_move(b, m)) continue;
    uint64_t nodes = depth <= 1 ? 1 : perft_rec(b, depth - 1);
    unmake_move(b);
    move_to_uci(m, out->moves[out->count]);
    out->nodes[out->count] = nodes;
    out->count++;
    out->total += nodes;
  }
}
