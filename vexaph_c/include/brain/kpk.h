#ifndef VEXAPH_BRAIN_KPK_H
#define VEXAPH_BRAIN_KPK_H

#include "core/types.h"

void init_kpk(void);
bool kpk_is_win(int strong_king, int strong_pawn, int weak_king, bool strong_to_move, bool strong_is_white);
int kpk_win_count(void);

#endif
