#include <stdlib.h>
#include <string.h>
#include "core/bitboard.h"

U64 pawn_attacks[2][64];
U64 knight_attacks[64];
U64 king_attacks[64];
U64 rook_masks[64];
U64 bishop_masks[64];
U64 rook_magics[64];
U64 bishop_magics[64];
int rook_shifts[64];
int bishop_shifts[64];
uint8_t rook_index_bits[64];
uint8_t bishop_index_bits[64];
U64 *rook_table[64];
U64 *bishop_table[64];
U64 file_masks[8];
U64 rank_masks[8];
U64 diag_masks[64];
U64 anti_diag_masks[64];
U64 adjacent_file_masks[8];
U64 king_zone_masks[64];
U64 king_zone_front_masks[2][64];
U64 passed_pawn_masks[2][64];
U64 isolated_pawn_masks[64];
U64 forward_file_masks[2][64];
U64 pawn_support_masks[2][64];
U64 outpost_masks[2][64];
uint8_t manhattan_distance[64 * 64];
uint8_t chebyshev_distance[64 * 64];

static bool initialized = false;
static int force_search = 0;
static int fallbacks = 0;
static SplitMix64 magic_rng;

static void init_leaper_attacks(void) {
  for (int sq = 0; sq < 64; sq++) {
    U64 bb = SQ_BB(sq);
    pawn_attacks[WHITE][sq] = ((bb >> 7) & NOT_A_FILE) | ((bb >> 9) & NOT_H_FILE);
    pawn_attacks[BLACK][sq] = ((bb << 7) & NOT_H_FILE) | ((bb << 9) & NOT_A_FILE);
    knight_attacks[sq] =
      ((bb >> 17) & NOT_H_FILE) | ((bb >> 15) & NOT_A_FILE) |
      ((bb >> 10) & NOT_GH_FILE) | ((bb >> 6) & NOT_AB_FILE) |
      ((bb << 17) & NOT_A_FILE) | ((bb << 15) & NOT_H_FILE) |
      ((bb << 10) & NOT_AB_FILE) | ((bb << 6) & NOT_GH_FILE);
    king_attacks[sq] =
      (bb >> 8) | ((bb >> 9) & NOT_H_FILE) | ((bb >> 7) & NOT_A_FILE) |
      ((bb >> 1) & NOT_H_FILE) |
      (bb << 8) | ((bb << 9) & NOT_A_FILE) |
      ((bb << 7) & NOT_H_FILE) | ((bb << 1) & NOT_A_FILE);
  }
}

U64 bishop_attacks_on_the_fly(int sq, U64 block) {
  U64 attacks = 0;
  int tr = sq >> 3, tf = sq & 7;
  for (int rr = tr + 1, ff = tf + 1; rr <= 7 && ff <= 7; rr++, ff++) {
    U64 s = SQ_BB(rr * 8 + ff); attacks |= s; if (block & s) break;
  }
  for (int rr = tr + 1, ff = tf - 1; rr <= 7 && ff >= 0; rr++, ff--) {
    U64 s = SQ_BB(rr * 8 + ff); attacks |= s; if (block & s) break;
  }
  for (int rr = tr - 1, ff = tf + 1; rr >= 0 && ff <= 7; rr--, ff++) {
    U64 s = SQ_BB(rr * 8 + ff); attacks |= s; if (block & s) break;
  }
  for (int rr = tr - 1, ff = tf - 1; rr >= 0 && ff >= 0; rr--, ff--) {
    U64 s = SQ_BB(rr * 8 + ff); attacks |= s; if (block & s) break;
  }
  return attacks;
}

U64 rook_attacks_on_the_fly(int sq, U64 block) {
  U64 attacks = 0;
  int tr = sq >> 3, tf = sq & 7;
  for (int rr = tr + 1; rr <= 7; rr++) {
    U64 s = SQ_BB(rr * 8 + tf); attacks |= s; if (block & s) break;
  }
  for (int rr = tr - 1; rr >= 0; rr--) {
    U64 s = SQ_BB(rr * 8 + tf); attacks |= s; if (block & s) break;
  }
  for (int ff = tf + 1; ff <= 7; ff++) {
    U64 s = SQ_BB(tr * 8 + ff); attacks |= s; if (block & s) break;
  }
  for (int ff = tf - 1; ff >= 0; ff--) {
    U64 s = SQ_BB(tr * 8 + ff); attacks |= s; if (block & s) break;
  }
  return attacks;
}

static U64 mask_bishop_attacks(int sq) {
  U64 attacks = 0;
  int tr = sq >> 3, tf = sq & 7;
  for (int rr = tr + 1, ff = tf + 1; rr <= 6 && ff <= 6; rr++, ff++) attacks |= SQ_BB(rr * 8 + ff);
  for (int rr = tr + 1, ff = tf - 1; rr <= 6 && ff >= 1; rr++, ff--) attacks |= SQ_BB(rr * 8 + ff);
  for (int rr = tr - 1, ff = tf + 1; rr >= 1 && ff <= 6; rr--, ff++) attacks |= SQ_BB(rr * 8 + ff);
  for (int rr = tr - 1, ff = tf - 1; rr >= 1 && ff >= 1; rr--, ff--) attacks |= SQ_BB(rr * 8 + ff);
  return attacks;
}

static U64 mask_rook_attacks(int sq) {
  U64 attacks = 0;
  int tr = sq >> 3, tf = sq & 7;
  for (int rr = tr + 1; rr <= 6; rr++) attacks |= SQ_BB(rr * 8 + tf);
  for (int rr = tr - 1; rr >= 1; rr--) attacks |= SQ_BB(rr * 8 + tf);
  for (int ff = tf + 1; ff <= 6; ff++) attacks |= SQ_BB(tr * 8 + ff);
  for (int ff = tf - 1; ff >= 1; ff--) attacks |= SQ_BB(tr * 8 + ff);
  return attacks;
}

typedef struct {
  U64 magic;
  int index_bits;
  U64 *table;
} MagicResult;

static bool build_magic_table(int sq, bool is_bishop, U64 magic, int index_bits, MagicResult *out) {
  U64 mask = is_bishop ? bishop_masks[sq] : rook_masks[sq];
  int relevant = count_bits(mask);
  uint32_t subsets = 1u << relevant;
  int shift = 64 - index_bits;
  size_t size = (size_t)1 << index_bits;
  U64 *table = calloc(size, sizeof(U64));
  uint8_t *used = calloc(size, 1);
  U64 sub = 0;
  for (uint32_t i = 0; i < subsets; i++) {
    U64 att = is_bishop ? bishop_attacks_on_the_fly(sq, sub) : rook_attacks_on_the_fly(sq, sub);
    size_t idx = (size_t)((sub * magic) >> shift);
    if (!used[idx]) { used[idx] = 1; table[idx] = att; }
    else if (table[idx] != att) { free(table); free(used); return false; }
    sub = (sub - mask) & mask;
  }
  free(used);
  out->magic = magic;
  out->index_bits = index_bits;
  out->table = table;
  return true;
}

#define MAGIC_TRIES_PER_SIZE 2000

static MagicResult find_magic(int sq, bool is_bishop) {
  U64 mask = is_bishop ? bishop_masks[sq] : rook_masks[sq];
  int relevant = count_bits(mask);
  uint32_t subsets = 1u << relevant;
  U64 *occupancies = malloc(subsets * sizeof(U64));
  U64 *attacks = malloc(subsets * sizeof(U64));
  U64 sub = 0;
  for (uint32_t i = 0; i < subsets; i++) {
    occupancies[i] = sub;
    attacks[i] = is_bishop ? bishop_attacks_on_the_fly(sq, sub) : rook_attacks_on_the_fly(sq, sub);
    sub = (sub - mask) & mask;
  }
  MagicResult res = {0, 0, NULL};
  int index_bits = relevant;
  for (;;) {
    size_t table_size = (size_t)1 << index_bits;
    int shift = 64 - index_bits;
    U64 *vals = malloc(table_size * sizeof(U64));
    uint8_t *used = malloc(table_size);
    for (int attempt = 0; attempt < MAGIC_TRIES_PER_SIZE; attempt++) {
      U64 magic = splitmix64_sparse(&magic_rng);
      if (count_bits((mask * magic) & 0xFF00000000000000ULL) < 6) continue;
      memset(used, 0, table_size);
      bool ok = true;
      for (uint32_t i = 0; i < subsets; i++) {
        size_t idx = (size_t)((occupancies[i] * magic) >> shift);
        if (!used[idx]) { used[idx] = 1; vals[idx] = attacks[i]; }
        else if (vals[idx] != attacks[i]) { ok = false; break; }
      }
      if (ok) {
        for (size_t i = 0; i < table_size; i++) if (!used[i]) vals[i] = 0;
        res.magic = magic;
        res.index_bits = index_bits;
        res.table = vals;
        free(used); free(occupancies); free(attacks);
        return res;
      }
    }
    free(vals); free(used);
    index_bits++;
  }
}

static MagicResult load_magic(int sq, bool is_bishop) {
  MagicResult r;
  if (!force_search) {
    U64 m = is_bishop ? BISHOP_MAGICS[sq] : ROOK_MAGICS[sq];
    int bits = is_bishop ? BISHOP_INDEX_BITS_INIT[sq] : ROOK_INDEX_BITS_INIT[sq];
    if (build_magic_table(sq, is_bishop, m, bits, &r)) return r;
  }
  fallbacks++;
  return find_magic(sq, is_bishop);
}

static void init_magics(void) {
  splitmix64_seed(&magic_rng, 0xC0FFEE1234567ULL);
  for (int sq = 0; sq < 64; sq++) {
    bishop_masks[sq] = mask_bishop_attacks(sq);
    rook_masks[sq] = mask_rook_attacks(sq);
  }
  for (int sq = 0; sq < 64; sq++) {
    MagicResult bm = load_magic(sq, true);
    bishop_magics[sq] = bm.magic;
    bishop_shifts[sq] = 64 - bm.index_bits;
    bishop_index_bits[sq] = (uint8_t)bm.index_bits;
    bishop_table[sq] = bm.table;

    MagicResult rm = load_magic(sq, false);
    rook_magics[sq] = rm.magic;
    rook_shifts[sq] = 64 - rm.index_bits;
    rook_index_bits[sq] = (uint8_t)rm.index_bits;
    rook_table[sq] = rm.table;
  }
}

static void init_masks(void) {
  for (int f = 0; f < 8; f++) file_masks[f] = FILE_A_BB << f;
  for (int rk = 0; rk < 8; rk++) rank_masks[rk] = RANK_8_BB << (rk * 8);

  for (int f = 0; f < 8; f++) {
    U64 m = 0;
    if (f > 0) m |= file_masks[f - 1];
    if (f < 7) m |= file_masks[f + 1];
    adjacent_file_masks[f] = m;
  }

  for (int sq = 0; sq < 64; sq++) {
    int rk = sq >> 3, f = sq & 7;
    U64 d = 0, ad = 0;
    for (int s = 0; s < 64; s++) {
      int r2 = s >> 3, f2 = s & 7;
      if (r2 - f2 == rk - f) d |= SQ_BB(s);
      if (r2 + f2 == rk + f) ad |= SQ_BB(s);
    }
    diag_masks[sq] = d;
    anti_diag_masks[sq] = ad;

    king_zone_masks[sq] = king_attacks[sq] | SQ_BB(sq);
    for (int color = 0; color < 2; color++) {
      U64 zone = king_zone_masks[sq];
      int fr = color == WHITE ? rk - 2 : rk + 2;
      if (fr >= 0 && fr <= 7) {
        int lo = f - 1 < 0 ? 0 : f - 1;
        int hi = f + 1 > 7 ? 7 : f + 1;
        for (int ff = lo; ff <= hi; ff++) zone |= SQ_BB(fr * 8 + ff);
      }
      king_zone_front_masks[color][sq] = zone;
    }

    U64 adj = adjacent_file_masks[f];
    isolated_pawn_masks[sq] = adj;
    for (int color = 0; color < 2; color++) {
      U64 ahead_same = 0, ahead_adj = 0, support = 0;
      for (int r2 = 0; r2 < 8; r2++) {
        bool ahead = color == WHITE ? r2 < rk : r2 > rk;
        bool behind_or_same = color == WHITE ? r2 >= rk : r2 <= rk;
        for (int f2 = 0; f2 < 8; f2++) {
          U64 s2 = SQ_BB(r2 * 8 + f2);
          int df = f2 > f ? f2 - f : f - f2;
          if (ahead && f2 == f) ahead_same |= s2;
          if (ahead && df == 1) ahead_adj |= s2;
          if (behind_or_same && df == 1) support |= s2;
        }
      }
      forward_file_masks[color][sq] = ahead_same;
      passed_pawn_masks[color][sq] = ahead_same | ahead_adj;
      outpost_masks[color][sq] = ahead_adj;
      pawn_support_masks[color][sq] = support;
    }
  }
}

static void init_distances(void) {
  for (int a = 0; a < 64; a++) {
    for (int b = 0; b < 64; b++) {
      int dr = (a >> 3) - (b >> 3); if (dr < 0) dr = -dr;
      int df = (a & 7) - (b & 7); if (df < 0) df = -df;
      manhattan_distance[a * 64 + b] = (uint8_t)(dr + df);
      chebyshev_distance[a * 64 + b] = (uint8_t)(dr > df ? dr : df);
    }
  }
}

void init_bitboards(void) {
  if (initialized) return;
  init_leaper_attacks();
  init_magics();
  init_masks();
  init_distances();
  initialized = true;
}

bool bitboards_initialized(void) { return initialized; }
void bitboards_force_magic_search(int enable) { force_search = enable; }
int bitboards_magic_fallbacks(void) { return fallbacks; }
