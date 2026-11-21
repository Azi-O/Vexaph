#ifndef VEXAPH_BRAIN_SEE_H
#define VEXAPH_BRAIN_SEE_H

#include "core/board.h"

int see_value(const Board *b, int move);
bool see_ge(const Board *b, int move, int threshold);

#endif
