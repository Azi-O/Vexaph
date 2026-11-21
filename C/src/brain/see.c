#include "brain/see.h"
#include "core/movegen.h"

static int least_valuable(const Board *b, U64 attackers, int side, int *lva_sq) {
  int off = side == WHITE ? 0 : 6;
  U64 mine = attackers & b->occupancies[side];
  if (!mine) return -1;
  for (int pt = 0; pt < 6; pt++) {
    U64 subset = mine & b->bitboards[pt + off];
    if (subset) {
      *lva_sq = __builtin_ctzll(subset);
      return pt;
    }
  }
  return -1;
}

int see_value(const Board *b, int move) {
  int gain[40];
  if (move & MOVE_CASTLING) return 0;

  int src = move & 63;
  int dst = (move >> 6) & 63;
  int piece = (move >> 12) & 15;
  int promoted = (move >> 16) & 15;
  int mover = piece < 6 ? WHITE : BLACK;

  int victim_value = 0;
  U64 occ = b->occupancies[BOTH];
  if (move & MOVE_ENPASSANT) {
    victim_value = SEE_VALUE[0];
    int cap_sq = mover == WHITE ? dst + 8 : dst - 8;
    occ ^= SQ_BB(cap_sq);
  } else {
    int target = b->mailbox[dst];
    if (target != NO_PIECE) victim_value = SEE_VALUE[PIECE_TYPE[target]];
  }

  int attacker_val = SEE_VALUE[PIECE_TYPE[piece]];
  if (promoted != 0) {
    victim_value += SEE_VALUE[PIECE_TYPE[promoted]] - SEE_VALUE[0];
    attacker_val = SEE_VALUE[PIECE_TYPE[promoted]];
  }

  const U64 *bbs = b->bitboards;
  U64 rook_like = bbs[W_ROOK] | bbs[B_ROOK] | bbs[W_QUEEN] | bbs[B_QUEEN];
  U64 bishop_like = bbs[W_BISHOP] | bbs[B_BISHOP] | bbs[W_QUEEN] | bbs[B_QUEEN];

  U64 attadef = attackers_to(b, dst, occ, WHITE) | attackers_to(b, dst, occ, BLACK);
  U64 from_bb = SQ_BB(src);
  int lva_sq = -1;

  int d = 0;
  gain[0] = victim_value;

  for (;;) {
    d++;
    gain[d] = attacker_val - gain[d - 1];
    int a = -gain[d - 1], c = gain[d];
    if ((a > c ? a : c) < 0) break;

    attadef &= ~from_bb;
    occ &= ~from_bb;
    attadef |= (get_bishop_attacks(dst, occ) & bishop_like & occ) | (get_rook_attacks(dst, occ) & rook_like & occ);
    attadef &= occ;

    int side = (d & 1) ? (mover ^ 1) : mover;
    int pt = least_valuable(b, attadef, side, &lva_sq);
    if (pt < 0) break;
    attacker_val = SEE_VALUE[pt];
    from_bb = SQ_BB(lva_sq);
  }

  while (--d > 0) {
    int x = -gain[d - 1], y = gain[d];
    gain[d - 1] = -(x > y ? x : y);
  }
  return gain[0];
}

bool see_ge(const Board *b, int move, int threshold) { return see_value(b, move) >= threshold; }
