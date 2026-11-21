#include "core/core.h"

void init_core(void) {
  init_types();
  init_zobrist();
  init_bitboards();
}
