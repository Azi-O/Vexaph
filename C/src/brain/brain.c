#include "brain/brain.h"

void init_brain(void) {
  init_eval_tables();
  init_kpk();
  init_eval();
  init_search();
  clear_pawn_hash();
}
