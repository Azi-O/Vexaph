#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface/uci.h"
#include "platform/platform.h"

int main(int argc, char **argv) {
  int ms = argc > 1 ? atoi(argv[1]) : 1000;
  int hash = argc > 2 ? atoi(argv[2]) : 64;
  init_engine_once();
  Searcher *s = searcher_create(hash);
  volatile int stop = 0;
  searcher_set_stop_flag(s, &stop);
  printf("fen\tdepth\tseldepth\tnodes\tnps\ttime\tscore\tbestmove\n");
  for (int i = 0; i < BENCH_FEN_COUNT; i++) {
    Board b;
    board_create(&b, BENCH_FENS[i]);
    searcher_new_game(s);
    SearchLimits l;
    memset(&l, 0, sizeof(l));
    l.movetime = ms;
    l.has_movetime = true;
    SearchInfo r;
    double t0 = now_ms();
    searcher_search(s, &b, &l, NULL, NULL, &r);
    double wall = now_ms() - t0;
    char sc[48], mv[16];
    score_to_uci(r.score, sc);
    if (r.best_move) move_to_uci(r.best_move, mv); else snprintf(mv, sizeof(mv), "0000");
    printf("%d\t%d\t%d\t%" PRIu64 "\t%" PRIu64 "\t%.0f\t%s\t%s\n", i + 1, r.depth, r.seldepth, r.nodes, (uint64_t)((double)r.nodes * 1000.0 / (wall < 1 ? 1 : wall)), wall, sc, mv);
    fflush(stdout);
    board_free(&b);
  }
  searcher_destroy(s);
  return 0;
}
