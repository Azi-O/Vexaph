#ifndef VEXAPH_CORE_BITBOARD_H
#define VEXAPH_CORE_BITBOARD_H

#include "core/types.h"

#define FILE_A_BB 0x0101010101010101ULL
#define FILE_H_BB (FILE_A_BB << 7)
#define NOT_A_FILE (~FILE_A_BB)
#define NOT_H_FILE (~FILE_H_BB)
#define NOT_AB_FILE (~(FILE_A_BB | (FILE_A_BB << 1)))
#define NOT_GH_FILE (~((FILE_A_BB << 6) | (FILE_A_BB << 7)))
#define RANK_8_BB 0xFFULL
#define LIGHT_SQUARES_BB 0xAA55AA55AA55AA55ULL
#define DARK_SQUARES_BB 0x55AA55AA55AA55AAULL

extern const U64 ROOK_MAGICS[64];
extern const U64 BISHOP_MAGICS[64];
extern const uint8_t ROOK_INDEX_BITS_INIT[64];
extern const uint8_t BISHOP_INDEX_BITS_INIT[64];

extern U64 pawn_attacks[2][64];
extern U64 knight_attacks[64];
extern U64 king_attacks[64];

extern U64 rook_masks[64];
extern U64 bishop_masks[64];
extern U64 rook_magics[64];
extern U64 bishop_magics[64];
extern int rook_shifts[64];
extern int bishop_shifts[64];
extern uint8_t rook_index_bits[64];
extern uint8_t bishop_index_bits[64];
extern U64 *rook_table[64];
extern U64 *bishop_table[64];

extern U64 file_masks[8];
extern U64 rank_masks[8];
extern U64 diag_masks[64];
extern U64 anti_diag_masks[64];
extern U64 adjacent_file_masks[8];
extern U64 king_zone_masks[64];
extern U64 king_zone_front_masks[2][64];
extern U64 passed_pawn_masks[2][64];
extern U64 isolated_pawn_masks[64];
extern U64 forward_file_masks[2][64];
extern U64 pawn_support_masks[2][64];
extern U64 outpost_masks[2][64];
extern uint8_t manhattan_distance[64 * 64];
extern uint8_t chebyshev_distance[64 * 64];

static inline U64 get_rook_attacks(int sq, U64 occ) {
  return rook_table[sq][((occ & rook_masks[sq]) * rook_magics[sq]) >> rook_shifts[sq]];
}

static inline U64 get_bishop_attacks(int sq, U64 occ) {
  return bishop_table[sq][((occ & bishop_masks[sq]) * bishop_magics[sq]) >> bishop_shifts[sq]];
}

static inline U64 get_queen_attacks(int sq, U64 occ) {
  return get_rook_attacks(sq, occ) | get_bishop_attacks(sq, occ);
}

U64 bishop_attacks_on_the_fly(int sq, U64 block);
U64 rook_attacks_on_the_fly(int sq, U64 block);
void init_bitboards(void);
bool bitboards_initialized(void);
void bitboards_force_magic_search(int enable);
int bitboards_magic_fallbacks(void);

#endif
