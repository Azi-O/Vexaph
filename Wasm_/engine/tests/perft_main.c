#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "core/core.h"
#include "platform/platform.h"

typedef struct { const char *name; const char *fen; int n; uint64_t exp[7]; } Case;

int main(int argc, char **argv) {
  init_core();
  if (argc > 1 && strcmp(argv[1], "magic") == 0) {
    return 0;
  }
  static const Case cases[] = {
    {"startpos", START_FEN, 5, {20, 400, 8902, 197281, 4865609}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, {48, 2039, 97862, 4085603}},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, {14, 191, 2812, 43238, 674624}},
    {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, {6, 264, 9467, 422333}},
    {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, {44, 1486, 62379, 2103487}},
    {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, {46, 2079, 89890, 3894594}},
  };
  int all_ok = 1;
  for (size_t c = 0; c < sizeof(cases) / sizeof(cases[0]); c++) {
    Board b;
    board_create(&b, cases[c].fen);
    for (int d = 1; d <= cases[c].n; d++) {
      double t0 = now_ms();
      uint64_t got = perft(&b, d);
      double ms = now_ms() - t0; if (ms < 1) ms = 1;
      int ok = got == cases[c].exp[d - 1];
      if (!ok) all_ok = 0;
      printf("%-10s depth %d: %9llu  %s  %d ms  %llu nps\n", cases[c].name, d, (unsigned long long)got,
             ok ? "OK" : "FAIL", (int)(ms + 0.5), (unsigned long long)(got * 1000.0 / ms));
    }
    board_free(&b);
  }
  printf(all_ok ? "PERFT SUITE: ALL PASSED\n" : "PERFT SUITE: FAILURES\n");
  return all_ok ? 0 : 1;
}
