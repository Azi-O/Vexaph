#include <stdlib.h>
#include <string.h>
#include "brain/eval.h"
#include "brain/eval_tables.h"
#include "brain/eval_pawn.h"
#include "brain/kpk.h"
#include "brain/nnue.h"

static _Thread_local int side_mg[2];
static _Thread_local int side_eg[2];
static _Thread_local int king_attack_units[2];
static _Thread_local int king_attackers[2];
static _Thread_local U64 attack_map[2];
static _Thread_local U64 att_nk[2];
static _Thread_local U64 att_n[2];
static _Thread_local U64 att_b[2];
static _Thread_local U64 att_r[2];
static _Thread_local U64 att_q[2];

static U64 FWD_EQ[2][8];
static int16_t IMB_CACHE[9 * 9 * 729 * 4];
static bool eval_ready = false;

void init_eval(void) {
  if (eval_ready) return;
#ifdef VEXAPH_FIX_FWD_EQ
  for (int r = 0; r < 8; r++) {
    U64 w = 0, bl = 0;
    for (int i = 0; i <= r; i++) w |= rank_masks[i];
    for (int i = r; i < 8; i++) bl |= rank_masks[i];
    FWD_EQ[WHITE][r] = w;
    FWD_EQ[BLACK][r] = bl;
  }
#endif
  for (size_t i = 0; i < sizeof(IMB_CACHE) / sizeof(IMB_CACHE[0]); i++) IMB_CACHE[i] = 32767;
  init_eval_tables();
  init_kpk();
  eval_ready = true;
}

static inline U64 pawn_attack_bb(U64 pawns, int color) {
  if (color == WHITE) return ((pawns >> 7) & NOT_A_FILE) | ((pawns >> 9) & NOT_H_FILE);
  return ((pawns << 7) & NOT_H_FILE) | ((pawns << 9) & NOT_A_FILE);
}

static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int iabs(int a) { return a < 0 ? -a : a; }

static void eval_pieces(const Board *b, int color, U64 occ) {
  const U64 *bb = b->bitboards;
  int off = color == WHITE ? 0 : 6;
  int enemy = color ^ 1;
  int eoff = color == WHITE ? 6 : 0;

  U64 our_pawns = bb[W_PAWN + off];
  U64 their_pawns = bb[W_PAWN + eoff];
  U64 their_pawn_att = pawn_attack_bb(their_pawns, enemy);
  U64 mobility_area = ~(our_pawns | bb[W_KING + off] | their_pawn_att);

  int enemy_king_sq = b->king_sq[enemy];
  U64 zone = king_zone_masks[enemy_king_sq];

  int mg = 0, eg = 0;
  int units = 0, attackers = 0;
  U64 all_att = 0;
  U64 n_att = 0, b_att = 0, r_att = 0, q_att = 0;

  U64 pieces = bb[W_KNIGHT + off];
  while (pieces) {
    int sq = __builtin_ctzll(pieces);
    pieces &= pieces - 1;
    U64 att = knight_attacks[sq];
    all_att |= att;
    n_att |= att;
    int mob = count_bits(att & mobility_area);
    mg += KNIGHT_MOBILITY_MG[mob];
    eg += KNIGHT_MOBILITY_EG[mob];

    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    if (rr >= 3 && rr <= 5 &&
        (outpost_masks[color][sq] & their_pawns) == 0 &&
        (pawn_attacks[enemy][sq] & our_pawns) != 0) {
      mg += KNIGHT_OUTPOST_MG;
      eg += KNIGHT_OUTPOST_EG;
    }

    U64 z = att & zone;
    if (z) {
      attackers++;
      units += KING_ATTACK_WEIGHT[1] + count_bits(z);
    }
  }

  pieces = bb[W_BISHOP + off];
  int bishop_count = 0;
  while (pieces) {
    int sq = __builtin_ctzll(pieces);
    pieces &= pieces - 1;
    bishop_count++;
    U64 att = get_bishop_attacks(sq, occ);
    all_att |= att;
    b_att |= att;
    int mob = count_bits(att & mobility_area);
    mg += BISHOP_MOBILITY_MG[mob];
    eg += BISHOP_MOBILITY_EG[mob];

    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    if (rr >= 3 && rr <= 5 &&
        (outpost_masks[color][sq] & their_pawns) == 0 &&
        (pawn_attacks[enemy][sq] & our_pawns) != 0) {
      mg += BISHOP_OUTPOST_MG;
      eg += BISHOP_OUTPOST_EG;
    }

    U64 same_color = (SQ_BB(sq) & LIGHT_SQUARES_BB) ? LIGHT_SQUARES_BB : DARK_SQUARES_BB;
    int blockers = count_bits(our_pawns & same_color);
    mg -= BAD_BISHOP_MG * blockers;
    eg -= BAD_BISHOP_EG * blockers;

    U64 z = att & zone;
    if (z) {
      attackers++;
      units += KING_ATTACK_WEIGHT[2] + count_bits(z);
    }
  }
  if (bishop_count >= 2) {
    mg += BISHOP_PAIR_MG;
    eg += BISHOP_PAIR_EG;
  }

  pieces = bb[W_ROOK + off];
  while (pieces) {
    int sq = __builtin_ctzll(pieces);
    pieces &= pieces - 1;
    U64 att = get_rook_attacks(sq, occ);
    all_att |= att;
    r_att |= att;
    int mob = count_bits(att & mobility_area);
    mg += ROOK_MOBILITY_MG[mob];
    eg += ROOK_MOBILITY_EG[mob];

    int f = sq & 7;
    U64 file_bb = file_masks[f];
    if ((file_bb & our_pawns) == 0) {
      if ((file_bb & their_pawns) == 0) {
        mg += ROOK_OPEN_FILE_MG;
        eg += ROOK_OPEN_FILE_EG;
      } else {
        mg += ROOK_SEMI_OPEN_FILE_MG;
        eg += ROOK_SEMI_OPEN_FILE_EG;
      }
    }

    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    if (rr == 6) {
      bool enemy_back = color == WHITE ? (enemy_king_sq >> 3) == 0 : (enemy_king_sq >> 3) == 7;
      U64 seventh = color == WHITE ? rank_masks[1] : rank_masks[6];
      if (enemy_back || (their_pawns & seventh) != 0) {
        mg += ROOK_SEVENTH_MG;
        eg += ROOK_SEVENTH_EG;
      }
    }

    U64 z = att & zone;
    if (z) {
      attackers++;
      units += KING_ATTACK_WEIGHT[3] + count_bits(z);
    }
  }

  pieces = bb[W_QUEEN + off];
  while (pieces) {
    int sq = __builtin_ctzll(pieces);
    pieces &= pieces - 1;
    U64 att = get_rook_attacks(sq, occ) | get_bishop_attacks(sq, occ);
    all_att |= att;
    q_att |= att;
    int mob = count_bits(att & mobility_area);
    mg += QUEEN_MOBILITY_MG[mob];
    eg += QUEEN_MOBILITY_EG[mob];

    U64 z = att & zone;
    if (z) {
      attackers++;
      units += KING_ATTACK_WEIGHT[4] + count_bits(z);
    }
  }

  U64 nk = all_att | pawn_attack_bb(our_pawns, color);
  att_nk[color] = nk;
  att_n[color] = n_att; att_b[color] = b_att; att_r[color] = r_att; att_q[color] = q_att;
  attack_map[color] = nk | king_attacks[b->king_sq[color]];
  side_mg[color] = mg;
  side_eg[color] = eg;
  king_attack_units[color] = units;
  king_attackers[color] = attackers;
}

#define THREAT_PAWN_MG 48
#define THREAT_PAWN_EG 32
#define THREAT_HANGING_MG 24
#define THREAT_HANGING_EG 18
#define SPACE_DIVISOR 24

static void eval_threats(const Board *b, int color) {
  const U64 *bb = b->bitboards;
  int off = color == WHITE ? 0 : 6;
  int eoff = color == WHITE ? 6 : 0;
  int enemy = color ^ 1;
  U64 their_pawns = bb[W_PAWN + eoff];
  U64 our_pawns = bb[W_PAWN + off];
  U64 their_target = b->occupancies[enemy] & ~(their_pawns | bb[W_KING + eoff]);
  U64 our_pawn_att = pawn_attack_bb(our_pawns, color);
  U64 their_pawn_att = pawn_attack_bb(their_pawns, enemy);
  int pawn_hits = count_bits(our_pawn_att & their_target);
  int hanging = count_bits(attack_map[color] & their_target & ~attack_map[enemy]);
  int mg = pawn_hits * THREAT_PAWN_MG + hanging * THREAT_HANGING_MG;
  int eg = pawn_hits * THREAT_PAWN_EG + hanging * THREAT_HANGING_EG;

  U64 their_major = bb[W_ROOK + eoff] | bb[W_QUEEN + eoff];
  int mm = count_bits((att_n[color] | att_b[color]) & their_major);
  if (mm != 0) { mg += mm * THREAT_MINOR_MAJOR_MG; eg += mm * THREAT_MINOR_MAJOR_EG; }
  int rq = count_bits(att_r[color] & bb[W_QUEEN + eoff]);
  if (rq != 0) { mg += rq * THREAT_ROOK_QUEEN_MG; eg += rq * THREAT_ROOK_QUEEN_EG; }

  U64 occ = b->occupancies[BOTH];
  U64 push, dbl;
  if (color == WHITE) {
    push = (our_pawns >> 8) & ~occ;
    dbl = ((push & rank_masks[5]) >> 8) & ~occ;
  } else {
    push = (our_pawns << 8) & ~occ;
    dbl = ((push & rank_masks[2]) << 8) & ~occ;
  }
  U64 safe_push = (push | dbl) & ~their_pawn_att & (attack_map[color] | ~attack_map[enemy]);
  if (safe_push) {
    int pt = count_bits(pawn_attack_bb(safe_push, color) & their_target);
    if (pt != 0) { mg += pt * THREAT_PUSH_MG; eg += pt * THREAT_PUSH_EG; }
  }

  side_mg[color] += mg;
  side_eg[color] += eg;

  int r0 = color == WHITE ? 6 : 1;
  int dir = color == WHITE ? -1 : 1;
  U64 area = (file_masks[2] | file_masks[3] | file_masks[4] | file_masks[5]) &
             (rank_masks[r0] | rank_masks[r0 + dir] | rank_masks[r0 + 2 * dir]);
  U64 safe = area & ~our_pawns & ~their_pawn_att;
  int pieces = count_bits(bb[W_KNIGHT + off] | bb[W_BISHOP + off] | bb[W_ROOK + off] | bb[W_QUEEN + off]);
  if (pieces >= 2) {
    U64 behind = color == WHITE
      ? ((our_pawns << 8) | (our_pawns << 16) | (our_pawns << 24))
      : ((our_pawns >> 8) | (our_pawns >> 16) | (our_pawns >> 24));
    int bonus = count_bits(safe) + count_bits(behind & safe & ~attack_map[enemy]);
    int blocked = count_bits(our_pawns & (color == WHITE ? (their_pawns << 8) : (their_pawns >> 8)));
    int weight = pieces - 1 + (blocked > 6 ? 6 : blocked);
    side_mg[color] += (bonus * weight * weight) / SPACE_DIVISOR;
  }
}

static bool rook_behind_passer(const Board *b, int color, int sq) {
  U64 rooks = b->bitboards[W_ROOK + (color == WHITE ? 0 : 6)];
  int f = sq & 7;
  while (rooks) {
    int rs = __builtin_ctzll(rooks);
    rooks &= rooks - 1;
    if ((rs & 7) == f && (color == WHITE ? rs > sq : rs < sq)) return true;
  }
  return false;
}

static int eval_king_shelter(const Board *b, int color) {
  int ksq = b->king_sq[color];
  int off = color == WHITE ? 0 : 6;
  U64 our_pawns = b->bitboards[W_PAWN + off];
  U64 their_pawns = b->bitboards[W_PAWN + (color == WHITE ? 6 : 0)];
  int krow = ksq >> 3;
  int kf = ksq & 7;
  U64 front = FWD_EQ[color][krow];
  int center = kf < 1 ? 1 : (kf > 6 ? 6 : kf);
  int score = 0;

  for (int f = center - 1; f <= center + 1; f++) {
    U64 fm = file_masks[f];
    U64 ours = our_pawns & fm & front;
    U64 theirs = their_pawns & fm & front;
    int our_row = -1;
    if (ours == 0) {
      score += SHELTER_MISSING;
    } else {
      our_row = color == WHITE ? (get_ms1b_index(ours) >> 3) : (get_ls1b_index(ours) >> 3);
      int d = iabs(our_row - krow);
      score += SHELTER_BY_DIST[d];
    }
    if (theirs != 0) {
      int their_row = color == WHITE ? (get_ms1b_index(theirs) >> 3) : (get_ls1b_index(theirs) >> 3);
      int d = iabs(their_row - krow);
      int storm = STORM_BY_DIST[d];
      if (our_row >= 0 && iabs(our_row - their_row) == 1) storm >>= STORM_BLOCKED_SHIFT;
      score -= storm;
    }
  }

  bool own_on_file = (file_masks[kf] & our_pawns) != 0;
  bool enemy_on_file = (file_masks[kf] & their_pawns) != 0;
  if (!own_on_file) score -= enemy_on_file ? KING_SEMI_OPEN_FILE_PENALTY_MG : KING_OPEN_FILE_PENALTY_MG;
  return score;
}

static int king_danger(const Board *b, int attacker, U64 occ) {
  int attackers = king_attackers[attacker];
  int defender = attacker ^ 1;
  int off = attacker == WHITE ? 0 : 6;
  bool has_queen = b->bitboards[W_QUEEN + off] != 0;
  if (attackers == 0 || (attackers < 2 && !has_queen)) return 0;

  int ksq = b->king_sq[defender];
  U64 zone = king_zone_masks[ksq];
  int units = king_attack_units[attacker];

  U64 weak = zone & attack_map[attacker] & ~att_nk[defender];
  units += KD_WEAK_SQUARE * count_bits(weak);

  U64 safe = ~(b->occupancies[attacker] | attack_map[defender]);
  U64 kn = knight_attacks[ksq];
  U64 bi = get_bishop_attacks(ksq, occ);
  U64 ro = get_rook_attacks(ksq, occ);
  if ((att_n[attacker] & kn & safe) != 0) units += KD_SAFE_CHECK_N;
  if ((att_b[attacker] & bi & safe) != 0) units += KD_SAFE_CHECK_B;
  if ((att_r[attacker] & ro & safe) != 0) units += KD_SAFE_CHECK_R;
  if ((att_q[attacker] & (bi | ro) & safe) != 0) units += KD_SAFE_CHECK_Q;

  if (!has_queen) units >>= 1;
  return KING_SAFETY_TABLE[units > 99 ? 99 : units];
}

static int imbalance_side(const int *us, const int *them) {
  int bonus = 0;
  for (int i = 0; i < 6; i++) {
    int n = us[i];
    if (n == 0) continue;
    int v = 0;
    for (int j = 0; j <= i; j++) v += IMB_OURS[i][j] * us[j] + IMB_THEIRS[i][j] * them[j];
    bonus += n * v;
  }
  return bonus;
}

static int material_imbalance(const Board *b) {
  const U64 *bb = b->bitboards;
  int wp = count_bits(bb[W_PAWN]), bp = count_bits(bb[B_PAWN]);
  int wn = count_bits(bb[W_KNIGHT]), bn = count_bits(bb[B_KNIGHT]);
  int wb = count_bits(bb[W_BISHOP]), bbn = count_bits(bb[B_BISHOP]);
  int wr = count_bits(bb[W_ROOK]), br = count_bits(bb[B_ROOK]);
  int wq = count_bits(bb[W_QUEEN]), bq = count_bits(bb[B_QUEEN]);
  bool cacheable = wp < 9 && bp < 9 && wn < 3 && bn < 3 && wb < 3 && bbn < 3 && wr < 3 && br < 3 && wq < 2 && bq < 2;
  int key = 0;
  if (cacheable) {
    key = wp + 9 * (bp + 9 * (wn + 3 * (bn + 3 * (wb + 3 * (bbn + 3 * (wr + 3 * (br + 3 * (wq + 2 * bq))))))));
    int c = IMB_CACHE[key];
    if (c != 32767) return c;
  }
  int cw[6] = {wb > 1 ? 1 : 0, wp, wn, wb, wr, wq};
  int cb[6] = {bbn > 1 ? 1 : 0, bp, bn, bbn, br, bq};
  int raw = imbalance_side(cw, cb) - imbalance_side(cb, cw);
  int v = (raw * IMB_SCALE_NUM) / IMB_SCALE_DEN;
  if (cacheable) IMB_CACHE[key] = (int16_t)v;
  return v;
}

static int mop_up(const Board *b, int strong) {
  const U64 *bb = b->bitboards;
  int soff = strong == WHITE ? 0 : 6;
  int weak = strong ^ 1;
  int woff = weak == WHITE ? 0 : 6;

  if (b->occupancies[weak] != bb[W_KING + woff]) return 0;
  if ((bb[W_PAWN] | bb[B_PAWN]) != 0) return 0;

  U64 q = bb[W_QUEEN + soff], r = bb[W_ROOK + soff];
  int bn = count_bits(bb[W_BISHOP + soff]), nn = count_bits(bb[W_KNIGHT + soff]);
  bool sufficient = q != 0 || r != 0 || (bn >= 1 && nn >= 1) || bn >= 2;
  if (!sufficient) return 0;

  int wk = b->king_sq[weak];
  int sk = b->king_sq[strong];
  int bonus;

  if (q == 0 && r == 0 && bn == 1 && nn >= 1 && bn + nn == 2) {
    int bsq = get_ls1b_index(bb[W_BISHOP + soff]);
    bool light = (SQ_BB(bsq) & LIGHT_SQUARES_BB) != 0;
    int c1 = light ? 0 : 7;
    int c2 = light ? 63 : 56;
    int d = imin(manhattan_distance[wk * 64 + c1], manhattan_distance[wk * 64 + c2]);
    bonus = (14 - d) * 18;
  } else {
    bonus = CENTER_DISTANCE[wk] * 12;
  }
  bonus += (14 - manhattan_distance[wk * 64 + sk]) * 5;
  return bonus;
}

static int endgame_scale(const Board *b, int strong, int strong_passed) {
  const U64 *bb = b->bitboards;
  int soff = strong == WHITE ? 0 : 6;
  int woff = strong == WHITE ? 6 : 0;

  int s_pawns = count_bits(bb[W_PAWN + soff]);
  int s_n = count_bits(bb[W_KNIGHT + soff]), s_b = count_bits(bb[W_BISHOP + soff]);
  int s_r = count_bits(bb[W_ROOK + soff]), s_q = count_bits(bb[W_QUEEN + soff]);
  int w_n = count_bits(bb[W_KNIGHT + woff]), w_b = count_bits(bb[W_BISHOP + woff]);
  int w_r = count_bits(bb[W_ROOK + woff]), w_q = count_bits(bb[W_QUEEN + woff]);
  int w_pawns = count_bits(bb[W_PAWN + woff]);
  int s_mat = s_n * 3 + s_b * 3 + s_r * 5 + s_q * 9;
  int w_mat = w_n * 3 + w_b * 3 + w_r * 5 + w_q * 9;

  if (s_b == 1 && w_b == 1) {
    bool sl = (bb[W_BISHOP + soff] & LIGHT_SQUARES_BB) != 0;
    bool wl = (bb[W_BISHOP + woff] & LIGHT_SQUARES_BB) != 0;
    if (sl != wl) {
      if (s_n + w_n + s_r + w_r + s_q + w_q == 0) {
        int sc = 18 + 4 * strong_passed;
        return sc > SCALE_NORMAL ? SCALE_NORMAL : sc;
      }
      int sc = 22 + 3 * (s_pawns + s_n + s_b + s_r + s_q + 1);
      return sc > SCALE_NORMAL ? SCALE_NORMAL : sc;
    }
  }

  if (s_pawns == 0) {
    if (s_mat - w_mat <= 3) return s_mat < 5 ? 0 : (w_mat <= 3 ? 4 : 14);
    if (s_mat - w_mat <= 5 && w_mat > 0) return SCALE_NO_PAWNS_ROOK_EDGE;
    return SCALE_NORMAL;
  }

  if (s_n + s_r + s_q == 0 && s_b == 1 && w_mat == 0 && w_pawns == 0) {
    U64 pawns = bb[W_PAWN + soff];
    U64 fa = file_masks[0], fh = file_masks[7];
    bool on_a = (pawns & ~fa) == 0, on_h = (pawns & ~fh) == 0;
    if (on_a || on_h) {
      int promo = strong == WHITE ? (on_a ? 0 : 7) : (on_a ? 56 : 63);
      bool light_promo = (SQ_BB(promo) & LIGHT_SQUARES_BB) != 0;
      bool light_bishop = (bb[W_BISHOP + soff] & LIGHT_SQUARES_BB) != 0;
      if (light_promo != light_bishop && chebyshev_distance[b->king_sq[strong ^ 1] * 64 + promo] <= 1) return 0;
    }
  }

  if (s_pawns == 1 && w_pawns == 0 && s_mat == 0) return SCALE_ONE_PAWN;

  if (s_mat == 5 && w_mat == 5 && s_r == 1 && w_r == 1 && s_pawns - w_pawns <= 1) {
    U64 pawns = bb[W_PAWN + soff];
    U64 qs = file_masks[0] | file_masks[1] | file_masks[2] | file_masks[3];
    U64 ks = file_masks[4] | file_masks[5] | file_masks[6] | file_masks[7];
    if (((pawns & qs) != 0) != ((pawns & ks) != 0) &&
        (king_attacks[b->king_sq[strong ^ 1]] & bb[W_PAWN + woff]) != 0) return SCALE_ROOK_FLANK;
  }

  if (s_mat > 0) {
    int sc = SCALE_BASE_PAWNS + SCALE_PER_PAWN * s_pawns;
    if (sc < SCALE_NORMAL) return sc;
  }
  return SCALE_NORMAL;
}

#define KPK_WIN_SCORE 550
#define KPK_PROGRESS 30
#define UNSTOPPABLE_EG 420
#define OPPOSITION_EG 14
#define NO_RACE 99

static int kpk_eval(const Board *b) {
  bool white_strong = b->bitboards[W_PAWN] != 0;
  int strong = white_strong ? WHITE : BLACK;
  int pawn_sq = get_ls1b_index(b->bitboards[white_strong ? W_PAWN : B_PAWN]);
  bool win = kpk_is_win(b->king_sq[strong], pawn_sq, b->king_sq[strong ^ 1], b->side == strong, white_strong);
  if (!win) return 0;
  int progress = white_strong ? 7 - (pawn_sq >> 3) : pawn_sq >> 3;
  int score = KPK_WIN_SCORE + KPK_PROGRESS * progress;
  return b->side == strong ? score : -score;
}

static int fastest_unstoppable(const Board *b, int color, U64 passed) {
  int ek = b->king_sq[color ^ 1];
  U64 occ = b->occupancies[BOTH];
  int best = NO_RACE;
  while (passed) {
    int sq = __builtin_ctzll(passed);
    passed &= passed - 1;
    if ((forward_file_masks[color][sq] & occ) != 0) continue;
    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    int steps = 7 - rr;
    if (rr == 1) steps--;
    int promo = (color == WHITE ? 0 : 56) + (sq & 7);
    int dk = chebyshev_distance[ek * 64 + promo];
    bool caught = b->side == color ? dk <= steps : dk <= steps + 1;
    if (!caught && steps < best) best = steps;
  }
  return best;
}

static int pawn_ending_terms(const Board *b, U64 passed_w, U64 passed_b) {
  int eg = 0;
  int dw = fastest_unstoppable(b, WHITE, passed_w);
  int db = fastest_unstoppable(b, BLACK, passed_b);
  if (dw != NO_RACE || db != NO_RACE) {
    int w_eff = dw - (b->side == WHITE ? 1 : 0);
    int b_eff = db - (b->side == BLACK ? 1 : 0);
    if (w_eff < b_eff) eg += UNSTOPPABLE_EG;
    else if (b_eff < w_eff) eg -= UNSTOPPABLE_EG;
  }
  int wk = b->king_sq[WHITE], bk = b->king_sq[BLACK];
  int df = iabs((wk & 7) - (bk & 7));
  int dr = iabs((wk >> 3) - (bk >> 3));
  if ((df == 0 && dr == 2) || (dr == 0 && df == 2)) eg += b->side == WHITE ? -OPPOSITION_EG : OPPOSITION_EG;
  return eg;
}

static _Thread_local int pass_mg[2];
static _Thread_local int pass_eg[2];

static void eval_passed(const Board *b, int color, U64 passed, U64 occ, int own_k, int enemy_k) {
  int mg = 0, eg = 0;
  int enemy = color ^ 1;
  int forward = color == WHITE ? -8 : 8;
  U64 enemy_att = attack_map[enemy];
  U64 own_att = attack_map[color];
  const int8_t *mailbox = b->mailbox;

  while (passed) {
    int sq = __builtin_ctzll(passed);
    passed &= passed - 1;
    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    int stop = sq + forward;
    if (stop < 0) stop = 0;
    if (stop > 63) stop = 63;
    U64 stop_bb = SQ_BB(stop);
    int w = rr > 2 ? 5 * rr - 13 : 0;

    if (rr >= 3 && rook_behind_passer(b, color, sq)) eg += rr * 5;

    if (w > 0) {
      int de = chebyshev_distance[enemy_k * 64 + stop]; if (de > 5) de = 5;
      int dm = chebyshev_distance[own_k * 64 + stop]; if (dm > 5) dm = 5;
      eg += (int)(((de * 19) / 4.0 - dm * 2) * w / 4.5);
    }

    if (mailbox[stop] == NO_PIECE) {
      if (w > 0) {
        U64 ahead = forward_file_masks[color][sq];
        int k;
        if ((ahead & occ) == 0) {
          U64 path_att = ahead & enemy_att;
          k = path_att == 0 ? 15 : ((path_att & stop_bb) == 0 ? 9 : 3);
        } else {
          k = (stop_bb & enemy_att) == 0 ? 4 : 0;
        }
        if (k != 0 && (stop_bb & own_att) != 0) k += 3;
        eg += (k * w) / 3;
        mg += (k * w) / 8;
      }
    } else {
      mg -= rr * 2;
      eg -= rr * 4;
      int pc = mailbox[stop];
      if ((pc < 6 ? WHITE : BLACK) == enemy) eg -= rr * 2;
    }
  }
  pass_mg[color] = mg;
  pass_eg[color] = eg;
}

int evaluate_classical(const Board *b) {
  const U64 *bb = b->bitboards;
  U64 occ = b->occupancies[BOTH];

  int piece_count = count_bits(occ);
  if (piece_count <= 4 && board_is_material_draw(b)) return 0;
  if (piece_count == 3 && (bb[W_PAWN] | bb[B_PAWN]) != 0) return kpk_eval(b);
  if (piece_count == 4 && (count_bits(bb[W_KNIGHT]) == 2 || count_bits(bb[B_KNIGHT]) == 2)) return 0;

  int mg = 0, eg = 0, phase = 0;
  for (int pc = 0; pc < 12; pc++) {
    U64 pieces = bb[pc];
    if (!pieces) continue;
    const int *mg_t = MG_TABLE[pc];
    const int *eg_t = EG_TABLE[pc];
    int inc = GAME_PHASE_INC[pc % 6];
    while (pieces) {
      int sq = __builtin_ctzll(pieces);
      pieces &= pieces - 1;
      mg += mg_t[sq];
      eg += eg_t[sq];
      phase += inc;
    }
  }

  const PawnEval *pe = eval_pawns(b);
  mg += pe->mg;
  eg += pe->eg;
  U64 passed_w = pe->passed_white, passed_b = pe->passed_black;

  eval_pieces(b, WHITE, occ);
  eval_pieces(b, BLACK, occ);
  eval_threats(b, WHITE);
  eval_threats(b, BLACK);
  mg += side_mg[WHITE] - side_mg[BLACK];
  eg += side_eg[WHITE] - side_eg[BLACK];

  int ks_w = king_danger(b, WHITE, occ);
  int ks_b = king_danger(b, BLACK, occ);
  mg += ks_w;
  mg -= ks_b;
  eg += ks_w >> 3;
  eg -= ks_b >> 3;

  mg += eval_king_shelter(b, WHITE) - eval_king_shelter(b, BLACK);

  int wk = b->king_sq[WHITE], bk = b->king_sq[BLACK];
  eval_passed(b, WHITE, passed_w, occ, wk, bk);
  eval_passed(b, BLACK, passed_b, occ, bk, wk);
  mg += pass_mg[WHITE] - pass_mg[BLACK];
  eg += pass_eg[WHITE] - pass_eg[BLACK];

  int imb = material_imbalance(b);
  mg += imb;
  eg += imb;

  if (phase == 0) eg += pawn_ending_terms(b, passed_w, passed_b);

  if (phase <= 10) {
    int mw = mop_up(b, WHITE);
    int mb = mop_up(b, BLACK);
    eg += mw - mb;
    mg += mw - mb;
  }

  if (eg != 0) {
    int strong = eg > 0 ? WHITE : BLACK;
    int scale = endgame_scale(b, strong, count_bits(strong == WHITE ? passed_w : passed_b));
    if (scale != SCALE_NORMAL) eg = (eg * scale) / SCALE_NORMAL;
  }

  if (phase > TOTAL_PHASE) phase = TOTAL_PHASE;
  int score = (mg * phase + eg * (TOTAL_PHASE - phase)) / TOTAL_PHASE;

  return (b->side == WHITE ? score : -score) + TEMPO_BONUS;
}

static int nnue_scale_cp(int raw, const Board *b) {
  int npm = 0;
  for (int pt = KNIGHT; pt <= QUEEN; pt++) npm += count_bits(b->bitboards[pt] | b->bitboards[pt + 6]) * PIECE_VALUE[pt];
  int v = (int)(((int64_t)raw * (NNUE_SCALE_BASE + npm / NNUE_SCALE_NPM_DIV)) / NNUE_SCALE_DIV);
  int fifty = b->fifty > 100 ? 100 : b->fifty;
  v = v * (100 - fifty) / 100;
  if (v > NNUE_CP_LIMIT) v = NNUE_CP_LIMIT;
  if (v < -NNUE_CP_LIMIT) v = -NNUE_CP_LIMIT;
  return v;
}

static bool hybrid_uses_classical(const Board *b, int *exact_score) {
  const U64 *bb = b->bitboards;
  int piece_count = count_bits(b->occupancies[BOTH]);
  if (piece_count <= 4 && board_is_material_draw(b)) { *exact_score = 0; return true; }
  if (piece_count == 3 && (bb[W_PAWN] | bb[B_PAWN]) != 0) { *exact_score = kpk_eval(b); return true; }
  if (piece_count == 4 && (count_bits(bb[W_KNIGHT]) == 2 || count_bits(bb[B_KNIGHT]) == 2)) { *exact_score = 0; return true; }
  if (count_bits(b->occupancies[WHITE]) == 1 || count_bits(b->occupancies[BLACK]) == 1) {
    *exact_score = evaluate_classical(b);
    return true;
  }
  return false;
}

int evaluate_with(const Board *b, NnueStack *st, int ply) {
  if (!nnue_active()) return evaluate_classical(b);
  int exact;
  if (hybrid_uses_classical(b, &exact)) return exact;
  int raw = st ? nnue_raw_stack(st, b, ply) : nnue_raw_board(b);
  return nnue_scale_cp(raw, b);
}

int evaluate(const Board *b) { return evaluate_with(b, NULL, 0); }

EvalTrace evaluate_trace(const Board *b) {
  EvalTrace t;
  t.classical = evaluate_classical(b);
  t.score = evaluate(b);
  const PawnEval *pe = eval_pawns(b);
  t.pawn_mg = pe->mg;
  t.pawn_eg = pe->eg;
  t.white_mobility_mg = side_mg[WHITE];
  t.black_mobility_mg = side_mg[BLACK];
  t.white_king_units = king_attack_units[WHITE];
  t.black_king_units = king_attack_units[BLACK];
  t.nnue_active = nnue_active();
  t.nnue_raw = t.nnue_active ? nnue_raw_board(b) : 0;
  return t;
}
