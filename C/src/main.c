#include <inttypes.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "interface/uci.h"
#include "platform/platform.h"

static pthread_mutex_t out_mu = PTHREAD_MUTEX_INITIALIZER;

typedef struct {
  volatile int stop_flag;
  volatile int searching;
} Shared;

static Shared g_shared;

static void stdout_write(const char *line, void *ud) {
  (void)ud;
  pthread_mutex_lock(&out_mu);
  fputs(line, stdout);
  fputc('\n', stdout);
  fflush(stdout);
  pthread_mutex_unlock(&out_mu);
}

static void worker_write(const char *line, void *ud) {
  stdout_write(line, ud);
  if (!strncmp(line, "bestmove", 8)) {
    __atomic_store_n(&g_shared.searching, 0, __ATOMIC_SEQ_CST);
    __atomic_store_n(&g_shared.stop_flag, 0, __ATOMIC_SEQ_CST);
  }
}

typedef struct QNode {
  char *line;
  struct QNode *next;
} QNode;

typedef struct {
  pthread_mutex_t mu;
  pthread_cond_t cv;
  QNode *head, *tail;
  int busy;
  int done;
  UciEngine *engine;
} Queue;

static void *worker_main(void *arg) {
  Queue *q = arg;
  for (;;) {
    pthread_mutex_lock(&q->mu);
    while (!q->head) pthread_cond_wait(&q->cv, &q->mu);
    QNode *n = q->head;
    q->head = n->next;
    if (!q->head) q->tail = NULL;
    q->busy = 1;
    pthread_mutex_unlock(&q->mu);

    uci_handle_line(q->engine, n->line);
    free(n->line);
    free(n);

    pthread_mutex_lock(&q->mu);
    q->busy = 0;
    int quit = q->engine->quit;
    if (quit) q->done = 1;
    pthread_cond_broadcast(&q->cv);
    pthread_mutex_unlock(&q->mu);
    if (quit) return NULL;
  }
}

static void enqueue(Queue *q, const char *line) {
  QNode *n = malloc(sizeof(QNode));
  n->line = strdup(line);
  n->next = NULL;
  pthread_mutex_lock(&q->mu);
  if (q->tail) q->tail->next = n;
  else q->head = n;
  q->tail = n;
  pthread_cond_signal(&q->cv);
  pthread_mutex_unlock(&q->mu);
}

static void wait_finished(Queue *q, int timeout_ms) {
  double t0 = now_ms();
  for (;;) {
    pthread_mutex_lock(&q->mu);
    int fin = q->done || (!q->head && !q->busy);
    pthread_mutex_unlock(&q->mu);
    if (fin || now_ms() - t0 > timeout_ms) return;
    usleep(2000);
  }
}

static void first_token(const char *line, char *out, size_t cap, char *second, size_t cap2) {
  const char *p = line;
  size_t o = 0;
  while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') p++;
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && o + 1 < cap) out[o++] = *p++;
  out[o] = 0;
  while (*p == ' ' || *p == '\t') p++;
  o = 0;
  while (*p && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n' && o + 1 < cap2) second[o++] = *p++;
  second[o] = 0;
}

static int run_uci(const char *argv0, const char *eco_hint) {
  Queue q;
  memset(&q, 0, sizeof(q));
  pthread_mutex_init(&q.mu, NULL);
  pthread_cond_init(&q.cv, NULL);
  q.engine = uci_create(worker_write, NULL, &g_shared.stop_flag);
  uci_set_paths(q.engine, eco_hint, argv0);
  pthread_t th;
  pthread_create(&th, NULL, worker_main, &q);

  char *line = NULL;
  size_t cap = 0;
  ssize_t len;
  while ((len = getline(&line, &cap, stdin)) >= 0) {
    char cmd[64], second[64];
    first_token(line, cmd, sizeof(cmd), second, sizeof(second));
    if (!strcmp(cmd, "stop")) {
      if (__atomic_load_n(&g_shared.searching, __ATOMIC_SEQ_CST)) __atomic_store_n(&g_shared.stop_flag, 1, __ATOMIC_SEQ_CST);
      continue;
    }
    if (!strcmp(cmd, "isready") && __atomic_load_n(&g_shared.searching, __ATOMIC_SEQ_CST)) {
      stdout_write("readyok", NULL);
      continue;
    }
    if (!strcmp(cmd, "quit")) {
      if (__atomic_load_n(&g_shared.searching, __ATOMIC_SEQ_CST)) __atomic_store_n(&g_shared.stop_flag, 1, __ATOMIC_SEQ_CST);
      enqueue(&q, line);
      wait_finished(&q, 1000);
      fflush(stdout);
      _exit(0);
    }
    if (!strcmp(cmd, "go") && strcmp(second, "perft") != 0) __atomic_store_n(&g_shared.searching, 1, __ATOMIC_SEQ_CST);
    enqueue(&q, line);
  }
  if (__atomic_load_n(&g_shared.searching, __ATOMIC_SEQ_CST)) __atomic_store_n(&g_shared.stop_flag, 1, __ATOMIC_SEQ_CST);
  wait_finished(&q, 3000);
  fflush(stdout);
  _exit(0);
}

static const char *join_args(int argc, char **argv, int from, char *buf, size_t cap) {
  size_t o = 0;
  buf[0] = 0;
  for (int i = from; i < argc; i++) {
    int w = snprintf(buf + o, cap - o, "%s%s", i > from ? " " : "", argv[i]);
    if (w < 0 || (size_t)w >= cap - o) break;
    o += (size_t)w;
  }
  return buf;
}

static long js_atoi(const char *s) {
  char *end;
  long v = strtol(s, &end, 10);
  return end == s ? 0 : v;
}

typedef struct {
  const char *name;
  const char *fen;
  int ncases;
  int depth[6];
  uint64_t expected[6];
} PerftCase;

static int cmd_perft(int argc, char **argv) {
  init_core();
  static const PerftCase suite[] = {
    {"startpos", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", 5, {1, 2, 3, 4, 5}, {20, 400, 8902, 197281, 4865609}},
    {"kiwipete", "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1", 4, {1, 2, 3, 4}, {48, 2039, 97862, 4085603}},
    {"position 3", "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1", 5, {1, 2, 3, 4, 5}, {14, 191, 2812, 43238, 674624}},
    {"position 4", "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1", 4, {1, 2, 3, 4}, {6, 264, 9467, 422333}},
    {"position 5", "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8", 4, {1, 2, 3, 4}, {44, 1486, 62379, 2103487}},
    {"position 6", "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10", 4, {1, 2, 3, 4}, {46, 2079, 89890, 3894594}},
  };
  if (argc > 0 && !strcmp(argv[0], "suite")) {
    int all_ok = 1;
    for (size_t s = 0; s < sizeof(suite) / sizeof(suite[0]); s++) {
      Board b;
      board_create(&b, suite[s].fen);
      for (int c = 0; c < suite[s].ncases; c++) {
        double t0 = now_ms();
        uint64_t got = perft(&b, suite[s].depth[c]);
        double ms = now_ms() - t0;
        if (ms < 1) ms = 1;
        int ok = got == suite[s].expected[c];
        if (!ok) all_ok = 0;
        char tail[96];
        if (ok) snprintf(tail, sizeof(tail), "  OK  ");
        else snprintf(tail, sizeof(tail), "  FAIL (expected %" PRIu64 ") ", suite[s].expected[c]);
        printf("%-10s depth %d: %9" PRIu64 "%s%.0f ms  %" PRIu64 " nps\n", suite[s].name, suite[s].depth[c], got, tail, ms, (uint64_t)((double)got * 1000.0 / ms));
      }
      board_free(&b);
    }
    puts(all_ok ? "PERFT SUITE: ALL PASSED" : "PERFT SUITE: FAILURES");
    return all_ok ? 0 : 1;
  }
  int depth = argc > 0 ? (int)js_atoi(argv[0]) : 0;
  if (!depth) depth = 5;
  char fen[1024];
  if (argc > 1) join_args(argc, argv, 1, fen, sizeof(fen));
  else snprintf(fen, sizeof(fen), "%s", START_FEN);
  Board b;
  board_create(&b, fen);
  char out[256];
  board_export_fen(&b, out, sizeof(out));
  printf("Perft depth %d | %s\n", depth, out);
  double t0 = now_ms();
  static PerftDivide res;
  perft_divide(&b, depth, &res);
  double ms = now_ms() - t0;
  if (ms < 1) ms = 1;
  for (int i = 0; i < res.count; i++) printf("%s: %" PRIu64 "\n", res.moves[i], res.nodes[i]);
  printf("\nNodes searched: %" PRIu64 "\n", res.total);
  printf("Time: %.0f ms | NPS: %" PRIu64 "\n", ms, (uint64_t)((double)res.total * 1000.0 / ms));
  board_free(&b);
  return 0;
}

static void console_write(const char *line, void *ud) {
  (void)ud;
  puts(line);
  fflush(stdout);
}

static int cmd_bench(int argc, char **argv, const char *argv0, const char *eco_hint) {
  int depth = argc > 0 ? (int)js_atoi(argv[0]) : 0;
  if (!depth) depth = 8;
  UciEngine *e = uci_create(console_write, NULL, NULL);
  uci_set_paths(e, eco_hint, argv0);
  uci_cmd_bench(e, depth);
  uci_destroy(e);
  return 0;
}

static void search_info(const SearchInfo *i, void *ud) {
  (void)ud;
  char sc[48], pv[4096];
  score_to_uci(i->score, sc);
  pv_to_uci(i->pv, i->pv_len, pv, sizeof(pv));
  printf("depth %d score %s nodes %" PRIu64 " nps %" PRIu64 " time %d pv %s\n", i->depth, sc, i->nodes, i->nps, i->time, pv);
  fflush(stdout);
}

static int cmd_search(int argc, char **argv, const char *argv0) {
  init_core();
  init_brain();
  char nmsg[1800];
  bool nok = nnue_autoload(argv0, nmsg, sizeof(nmsg));
  fprintf(stderr, "info string %s%s\n", nok ? "" : "ERROR: ", nmsg);
  int depth = argc > 0 ? (int)js_atoi(argv[0]) : 0;
  if (!depth) depth = 12;
  char fen[1024];
  if (argc > 1) join_args(argc, argv, 1, fen, sizeof(fen));
  else snprintf(fen, sizeof(fen), "%s", START_FEN);
  Board b;
  board_create(&b, fen);
  Searcher *s = searcher_create(16);
  volatile int stop = 0;
  searcher_set_stop_flag(s, &stop);
  SearchLimits l;
  memset(&l, 0, sizeof(l));
  l.depth = depth;
  l.has_depth = true;
  SearchInfo res;
  searcher_search(s, &b, &l, search_info, NULL, &res);
  char mv[16];
  if (res.best_move) move_to_uci(res.best_move, mv);
  else snprintf(mv, sizeof(mv), "0000");
  printf("bestmove %s\n", mv);
  searcher_destroy(s);
  board_free(&b);
  return 0;
}

static int cmd_eval(int argc, char **argv, const char *argv0) {
  init_core();
  init_brain();
  char nmsg[1800];
  bool nok = nnue_autoload(argv0, nmsg, sizeof(nmsg));
  fprintf(stderr, "info string %s%s\n", nok ? "" : "ERROR: ", nmsg);
  char fen[1024];
  if (argc > 0) join_args(argc, argv, 0, fen, sizeof(fen));
  else snprintf(fen, sizeof(fen), "%s", START_FEN);
  Board b;
  board_create(&b, fen);
  char bs[4096];
  board_to_string(&b, bs, sizeof(bs));
  puts(bs);
  EvalTrace tr = evaluate_trace(&b);
  printf("Static evaluation (side to move): %d cp\n", tr.score);
  if (tr.nnue_active) printf("  NNUE raw %d  classical %d\n", tr.nnue_raw, tr.classical);
  board_free(&b);
  return 0;
}

int main(int argc, char **argv) {
  const char *eco_hint = NULL;
  int a = 1;
  if (a + 1 < argc && !strcmp(argv[a], "--eco")) {
    eco_hint = argv[a + 1];
    a += 2;
  }
  const char *mode = a < argc ? argv[a] : NULL;
  int rest = argc - a - 1;
  char **rv = argv + a + 1;
  if (mode && !strcmp(mode, "perft")) return cmd_perft(rest, rv);
  if (mode && !strcmp(mode, "bench")) return cmd_bench(rest, rv, argv[0], eco_hint);
  if (mode && !strcmp(mode, "search")) return cmd_search(rest, rv, argv[0]);
  if (mode && !strcmp(mode, "eval")) return cmd_eval(rest, rv, argv[0]);
  return run_uci(argv[0], eco_hint);
}
