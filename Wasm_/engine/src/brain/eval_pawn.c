#include <string.h>
#include "brain/eval_pawn.h"
#include "brain/eval_tables.h"

static _Thread_local U64 ph_keys[PAWN_HASH_SIZE];
static _Thread_local int32_t ph_mg[PAWN_HASH_SIZE];
static _Thread_local int32_t ph_eg[PAWN_HASH_SIZE];
static _Thread_local U64 ph_passed_w[PAWN_HASH_SIZE];
static _Thread_local U64 ph_passed_b[PAWN_HASH_SIZE];

static _Thread_local uint64_t probes = 0;
static _Thread_local uint64_t hits = 0;
static _Thread_local PawnEval result;

void clear_pawn_hash(void) {
  memset(ph_keys, 0, sizeof(ph_keys));
  memset(ph_mg, 0, sizeof(ph_mg));
  memset(ph_eg, 0, sizeof(ph_eg));
  memset(ph_passed_w, 0, sizeof(ph_passed_w));
  memset(ph_passed_b, 0, sizeof(ph_passed_b));
  probes = 0;
  hits = 0;
}

PawnHashStats pawn_hash_stats(void) {
  PawnHashStats s = {probes, hits, PAWN_HASH_SIZE};
  return s;
}

static _Thread_local int acc_mg, acc_eg;
static _Thread_local U64 acc_passed;

static void eval_side(int color, U64 us, U64 them) {
  acc_mg = 0;
  acc_eg = 0;
  acc_passed = 0;
  int forward = color == WHITE ? -8 : 8;
  int enemy = color ^ 1;
  U64 bb = us;

  while (bb) {
    int sq = __builtin_ctzll(bb);
    bb &= bb - 1;
    int f = sq & 7;
    int rr = color == WHITE ? 7 - (sq >> 3) : sq >> 3;
    int stop = sq + forward;
    if (stop < 0) stop = 0;
    if (stop > 63) stop = 63;

    bool doubled = (forward_file_masks[color][sq] & us) != 0;
    bool isolated = (isolated_pawn_masks[sq] & us) == 0;
    bool open_file = (forward_file_masks[color][sq] & them) == 0;
    bool supported = (pawn_attacks[enemy][sq] & us) != 0;
    bool phalanx = f < 7 && (us & SQ_BB(sq + 1)) != 0;
    bool passed = (passed_pawn_masks[color][sq] & them) == 0 && !doubled;

    if (passed) {
      acc_mg += PASSED_PAWN_MG[rr];
      acc_eg += PASSED_PAWN_EG[rr];
      acc_passed |= SQ_BB(sq);
      if (supported || phalanx || (f > 0 && (us & SQ_BB(sq - 1)) != 0)) {
        acc_mg += PASSED_PROTECTED_MG[rr];
        acc_eg += PASSED_PROTECTED_EG[rr];
      }
    } else if (open_file && !doubled &&
               count_bits(outpost_masks[color][sq] & them) <= count_bits(pawn_support_masks[color][sq] & us)) {
      acc_mg += CANDIDATE_MG[rr];
      acc_eg += CANDIDATE_EG[rr];
    }

    if (doubled) {
      acc_mg -= DOUBLED_PAWN_MG[f];
      acc_eg -= DOUBLED_PAWN_EG[f];
    }

    if (isolated) {
      acc_mg -= ISOLATED_PAWN_MG[f] + (open_file ? ISOLATED_OPEN_EXTRA_MG : 0);
      acc_eg -= ISOLATED_PAWN_EG[f] + (open_file ? ISOLATED_OPEN_EXTRA_EG : 0);
    } else if (!passed &&
               (pawn_support_masks[color][sq] & us) == 0 &&
               (pawn_attacks[color][stop] & them) != 0) {
      acc_mg -= BACKWARD_PAWN_MG[f] + (open_file ? BACKWARD_OPEN_EXTRA_MG : 0);
      acc_eg -= BACKWARD_PAWN_EG[f] + (open_file ? BACKWARD_OPEN_EXTRA_EG : 0);
    }

    if (phalanx) {
      acc_mg += PHALANX_MG[rr];
      acc_eg += PHALANX_EG[rr];
    }
    if (supported) {
      acc_mg += SUPPORTED_MG[rr];
      acc_eg += SUPPORTED_EG[rr];
    }
  }
}

const PawnEval *eval_pawns(const Board *b) {
  U64 key = b->pawn_hash_key;
  int idx = (int)(key & 0xFFFF);
  probes++;
  if (ph_keys[idx] == key && (key != 0 || ph_mg[idx] == 0)) {
    hits++;
    result.mg = ph_mg[idx];
    result.eg = ph_eg[idx];
    result.passed_white = ph_passed_w[idx];
    result.passed_black = ph_passed_b[idx];
    return &result;
  }

  U64 wp = b->bitboards[W_PAWN];
  U64 bp = b->bitboards[B_PAWN];

  eval_side(WHITE, wp, bp);
  int w_mg = acc_mg, w_eg = acc_eg;
  U64 w_passed = acc_passed;
  eval_side(BLACK, bp, wp);
  int b_mg = acc_mg, b_eg = acc_eg;
  U64 b_passed = acc_passed;

  int mg = w_mg - b_mg;
  int eg = w_eg - b_eg;

  ph_keys[idx] = key;
  ph_mg[idx] = mg;
  ph_eg[idx] = eg;
  ph_passed_w[idx] = w_passed;
  ph_passed_b[idx] = b_passed;

  result.mg = mg;
  result.eg = eg;
  result.passed_white = w_passed;
  result.passed_black = b_passed;
  return &result;
}
