#ifndef VEXAPH_CORE_ZOBRIST_H
#define VEXAPH_CORE_ZOBRIST_H

#include "core/types.h"

extern U64 piece_keys[12][64];
extern U64 enpassant_keys[65];
extern U64 castle_keys[16];
extern U64 side_key;

void init_zobrist(void);
bool zobrist_all_unique(void);

#endif
