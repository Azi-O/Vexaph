#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "interface/uci.h"
int main(void) {
  init_engine_once();
  char line[512];
  int n = 0, bad = 0;
  while (fgets(line, sizeof(line), stdin)) {
    char *tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = 0;
    int expected = atoi(tab + 1);
    Board b;
    board_create(&b, line);
    int got = evaluate(&b);
    n++;
    if (got != expected) { bad++; if (bad <= 10) printf("MISMATCH %s js=%d c=%d\n", line, expected, got); }
    board_free(&b);
  }
  printf("eval compared %d positions, mismatches %d\n", n, bad);
  return bad ? 1 : 0;
}
