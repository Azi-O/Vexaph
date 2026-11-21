#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface/uci.h"
#include "platform/platform.h"

static int g_pass, g_fail, g_case_fail;
static char g_name[160];

#define CHECK(cond) do { if (!(cond)) { g_case_fail++; printf("        %s:%d: %s\n", __FILE__, __LINE__, #cond); } } while (0)
#define CHECK_EQ(a, b) do { long long _a = (long long)(a), _b = (long long)(b); if (_a != _b) { g_case_fail++; printf("        %s:%d: %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, _a, _b); } } while (0)
#define CHECK_STR(a, b) do { if (strcmp((a), (b)) != 0) { g_case_fail++; printf("        %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static double t_start;
static void begin(const char *n) { snprintf(g_name, sizeof(g_name), "%s", n); g_case_fail = 0; t_start = now_ms(); }
static void end(void) {
  if (g_case_fail) { g_fail++; printf("  FAIL  %s\n", g_name); }
  else { g_pass++; printf("  PASS  %s  (%.0f ms)\n", g_name, now_ms() - t_start); }
}

static uint32_t lcg_s;
static void lcg_seed(uint32_t s) { lcg_s = s; }
static double lcg_next(void) { lcg_s = lcg_s * 1664525u + 1013904223u; return lcg_s / 4294967296.0; }

typedef struct { char **lines; int n, cap; } Lines;
static void lines_add(const char *l, void *ud) {
  Lines *L = ud;
  if (L->n == L->cap) { L->cap = L->cap ? L->cap * 2 : 64; L->lines = realloc(L->lines, sizeof(char *) * (size_t)L->cap); }
  L->lines[L->n++] = strdup(l);
}
static void lines_clear(Lines *L) { for (int i = 0; i < L->n; i++) free(L->lines[i]); L->n = 0; }
static bool lines_has_prefix(Lines *L, const char *p) { for (int i = 0; i < L->n; i++) if (!strncmp(L->lines[i], p, strlen(p))) return true; return false; }
static bool lines_has_sub(Lines *L, const char *p) { for (int i = 0; i < L->n; i++) if (strstr(L->lines[i], p)) return true; return false; }
static bool lines_has_exact(Lines *L, const char *p) { for (int i = 0; i < L->n; i++) if (!strcmp(L->lines[i], p)) return true; return false; }
static const char *lines_last_prefix(Lines *L, const char *p) { for (int i = L->n - 1; i >= 0; i--) if (!strncmp(L->lines[i], p, strlen(p))) return L->lines[i]; return NULL; }
static int lines_count_prefix(Lines *L, const char *p) { int c = 0; for (int i = 0; i < L->n; i++) if (!strncmp(L->lines[i], p, strlen(p))) c++; return c; }

static void legal_list(Board *b, MoveList *ml) { ml->count = generate_legal_moves(b, ml->moves); }

static bool is_legal_uci(Board *b, const char *u) {
  static MoveList ml;
  legal_list(b, &ml);
  char s[16];
  for (int i = 0; i < ml.count; i++) { move_to_uci(ml.moves[i], s); if (!strcmp(s, u)) return true; }
  return false;
}

static int find_move(Board *b, const char *u) {
  static MoveList ml;
  legal_list(b, &ml);
  char s[16];
  for (int i = 0; i < ml.count; i++) { move_to_uci(ml.moves[i], s); if (!strcmp(s, u)) return ml.moves[i]; }
  return 0;
}

static const char *BOARD_FENS[] = {
  START_FEN,
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
  "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
  "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  "rnbqkbnr/pppp1ppp/8/4p3/3P4/8/PPP1PPPP/RNBQKBNR b KQkq d3 0 2",
  "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3",
  "4k3/8/8/8/8/8/4P3/4K3 b - - 12 40",
  "8/P7/8/8/8/8/p7/k6K w - - 0 1",
  "r1bqkb1r/pppp1ppp/2n2n2/4p2Q/2B1P3/8/PPPP1PPP/RNB1K1NR w KQkq - 4 4",
  "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
  "rnbqkbnr/pppppppp/8/8/8/5N2/PPPPPPPP/RNBQKB1R b KQkq - 1 1",
  "2r3k1/pp3ppp/8/3p4/3P4/8/PP3PPP/2R3K1 b - - 5 25",
};
#define NBF (int)(sizeof(BOARD_FENS) / sizeof(BOARD_FENS[0]))

static void t_board_roundtrip(void) {
  begin("board: FEN parse -> export round trip (14 positions)");
  for (int i = 0; i < NBF; i++) {
    Board b; board_create(&b, BOARD_FENS[i]);
    char f[256]; board_export_fen(&b, f, sizeof(f));
    CHECK_STR(f, BOARD_FENS[i]);
    board_free(&b);
  }
  end();
}

static void t_board_invalid(void) {
  begin("board: invalid FEN is rejected and board is left intact");
  Board b; board_create(&b, START_FEN);
  char before[256], after[256];
  board_export_fen(&b, before, sizeof(before));
  const char *bad[] = {"", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1", "8/8/8/8/8/8/8/8 w - - 0 1", "rnbqkbnr/pppppppp/9/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1", "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1"};
  for (int i = 0; i < 5; i++) {
    CHECK(!board_parse_fen(&b, bad[i]));
    board_export_fen(&b, after, sizeof(after));
    CHECK_STR(after, before);
  }
  board_free(&b);
  end();
}

static void strip_letters(const char *in, char *out) {
  int o = 0;
  for (; *in; in++) if ((*in >= 'a' && *in <= 'z') || (*in >= 'A' && *in <= 'Z') || *in == '.') out[o++] = *in;
  out[o] = 0;
}

static bool find_line(const char *text, const char *prefix, char *out) {
  const char *p = text;
  while (*p) {
    const char *e = strchr(p, '\n');
    size_t len = e ? (size_t)(e - p) : strlen(p);
    if (len >= strlen(prefix) && !strncmp(p, prefix, strlen(prefix))) { memcpy(out, p, len); out[len] = 0; return true; }
    if (!e) break;
    p = e + 1;
  }
  return false;
}

static void t_board_text(void) {
  begin("board: text board orientation");
  Board b; board_create(&b, START_FEN);
  char s[4096], line[256], l2[256];
  board_to_string(&b, s, sizeof(s));
  CHECK(find_line(s, " 8 |", line)); strip_letters(line, l2); CHECK_STR(l2, "rnbqkbnr");
  CHECK(find_line(s, " 1 |", line)); strip_letters(line, l2); CHECK_STR(l2, "RNBQKBNR");
  CHECK(strstr(s, "a  b  c  d  e  f  g  h") != NULL);
  Board b2; board_create(&b2, "4k3/8/8/8/4P3/8/8/4K3 w - - 0 1");
  board_to_string(&b2, s, sizeof(s));
  CHECK(find_line(s, " 4 |", line)); strip_letters(line, l2); CHECK_STR(l2, "....P...");
  board_free(&b); board_free(&b2);
  end();
}

static void t_board_playouts(void) {
  begin("board: random playouts keep FEN/hash/mailbox consistent, undo restores (400 games)");
  lcg_seed(12345);
  int positions = 0;
  static MoveList ml;
  for (int game = 0; game < 400; game++) {
    Board b; board_create(&b, BOARD_FENS[game % NBF]);
    char start[256]; board_export_fen(&b, start, sizeof(start));
    int made = 0;
    for (int ply = 0; ply < 90; ply++) {
      legal_list(&b, &ml);
      if (ml.count == 0) break;
      int m = ml.moves[(int)(lcg_next() * ml.count)];
      if (!make_move(&b, m)) { CHECK(0); break; }
      made++; positions++;
      char f[256], f2[256];
      board_export_fen(&b, f, sizeof(f));
      Board r; board_create(&r, f);
      board_export_fen(&r, f2, sizeof(f2));
      CHECK_STR(f2, f);
      board_free(&r);
      CHECK(b.hash_key == board_generate_hash(&b));
      CHECK(b.pawn_hash_key == board_generate_pawn_hash(&b));
      for (int sq = 0; sq < 64; sq++) {
        int pc = b.mailbox[sq];
        for (int k = 0; k < 12; k++) CHECK((((b.bitboards[k] >> sq) & 1) != 0) == (pc == k));
      }
      if (g_case_fail > 5) break;
    }
    for (int i = 0; i < made; i++) unmake_move(&b);
    char end_fen[256]; board_export_fen(&b, end_fen, sizeof(end_fen));
    CHECK_STR(end_fen, start);
    CHECK(b.hash_key == board_generate_hash(&b));
    board_free(&b);
    if (g_case_fail > 5) break;
  }
  CHECK(positions > 10000);
  end();
}

static void t_board_special(void) {
  begin("board: promotion, en passant and castling produce correct FEN");
  char f[256];
  Board b; board_create(&b, "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
  CHECK(make_move(&b, find_move(&b, "e5f6")));
  board_export_fen(&b, f, sizeof(f));
  CHECK_STR(f, "rnbqkbnr/ppp1p1pp/5P2/3p4/8/8/PPPP1PPP/RNBQKBNR b KQkq - 0 3");
  Board p; board_create(&p, "8/P7/8/8/8/8/p7/k6K w - - 0 1");
  CHECK(make_move(&p, find_move(&p, "a7a8q")));
  board_export_fen(&p, f, sizeof(f));
  CHECK_STR(f, "Q7/8/8/8/8/8/p7/k6K b - - 0 1");
  Board c; board_create(&c, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  CHECK(make_move(&c, find_move(&c, "e1g1")));
  board_export_fen(&c, f, sizeof(f));
  CHECK_STR(f, "r3k2r/8/8/8/8/8/8/R4RK1 b kq - 1 1");
  board_free(&b); board_free(&p); board_free(&c);
  end();
}

static void t_perft(void) {
  begin("engine: perft reference numbers (start, kiwipete, position 3)");
  Board b;
  board_create(&b, START_FEN); CHECK_EQ(perft(&b, 4), 197281); board_free(&b);
  board_create(&b, "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"); CHECK_EQ(perft(&b, 3), 97862); board_free(&b);
  board_create(&b, "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1"); CHECK_EQ(perft(&b, 4), 43238); board_free(&b);
  end();
}

static UciEngine *new_uci(Lines *L) {
  UciEngine *e = uci_create(lines_add, L, NULL);
  uci_set_paths(e, "data/eco", NULL);
  return e;
}

static void t_uci_handshake(void) {
  begin("uci: handshake, options, isready, position, go depth, legal bestmove");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "uci");
  CHECK(lines_has_exact(&L, "uciok"));
  CHECK(lines_has_prefix(&L, "id name Vexaph 1.0"));
  CHECK(lines_has_sub(&L, "option name OwnBook type check default true"));
  uci_handle_line(e, "isready");
  CHECK(lines_has_exact(&L, "readyok"));
  uci_handle_line(e, "position startpos moves e2e4 e7e5 g1f3");
  uci_handle_line(e, "go depth 6");
  const char *bm = lines_last_prefix(&L, "bestmove");
  CHECK(bm != NULL);
  if (bm) {
    char mv[16]; sscanf(bm, "bestmove %15s", mv);
    Board b; board_create(&b, START_FEN);
    const char *seq[] = {"e2e4", "e7e5", "g1f3"};
    for (int i = 0; i < 3; i++) CHECK(make_move(&b, find_move(&b, seq[i])));
    CHECK(is_legal_uci(&b, mv));
    board_free(&b);
  }
  CHECK(lines_count_prefix(&L, "info depth") >= 5);
  for (int i = 0; i < L.n; i++) if (!strncmp(L.lines[i], "info depth", 10)) CHECK(strstr(L.lines[i], " nodes ") && strstr(L.lines[i], " nps "));
  lines_clear(&L); uci_destroy(e);
  end();
}

static void t_uci_book(void) {
  begin("uci: go movetime from the start position answers from the opening book");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "isready");
  uci_handle_line(e, "position startpos");
  uci_handle_line(e, "go movetime 200");
  CHECK(lines_has_prefix(&L, "info string book move"));
  const char *bm = lines_last_prefix(&L, "bestmove");
  CHECK(bm != NULL);
  Board b; board_create(&b, START_FEN);
  char mv[16] = "";
  if (bm) sscanf(bm, "bestmove %15s", mv);
  CHECK(is_legal_uci(&b, mv));
  board_free(&b); lines_clear(&L); uci_destroy(e);
  end();
}

static void t_uci_ownbook(void) {
  begin("uci: OwnBook false forces a real search; depth/nodes/infinite never use the book");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "isready");
  uci_handle_line(e, "setoption name OwnBook value false");
  uci_handle_line(e, "position startpos");
  uci_handle_line(e, "go movetime 150");
  CHECK(!lines_has_prefix(&L, "info string book move"));
  CHECK(lines_has_prefix(&L, "info depth"));
  uci_handle_line(e, "setoption name OwnBook value true");
  lines_clear(&L);
  uci_handle_line(e, "go depth 4");
  CHECK(!lines_has_prefix(&L, "info string book move"));
  lines_clear(&L);
  uci_handle_line(e, "go nodes 2000");
  CHECK(!lines_has_prefix(&L, "info string book move"));
  lines_clear(&L); uci_destroy(e);
  end();
}

static void t_uci_opening(void) {
  begin("uci: opening command reports ECO and tracks progress");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "position startpos moves e2e4 e7e5 g1f3 b8c6 f1b5 a7a6");
  uci_handle_line(e, "opening");
  const char *l = lines_last_prefix(&L, "info string opening");
  CHECK(l != NULL);
  if (l) { CHECK(strstr(l, "C68") || strstr(l, "C70") || strstr(l, "Ruy Lopez")); CHECK(strstr(l, "exact true") != NULL); }
  lines_clear(&L); uci_destroy(e);
  end();
}

static void t_uci_nomoves(void) {
  begin("uci: no legal moves return 0000 without crashing");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "position fen 7k/5KQ1/8/8/8/8/8/8 b - - 0 1");
  uci_handle_line(e, "go depth 3");
  CHECK(lines_has_exact(&L, "bestmove 0000"));
  lines_clear(&L);
  uci_handle_line(e, "position fen 7k/8/6K1/8/8/8/8/6Q1 b - - 0 1");
  uci_handle_line(e, "go depth 3");
  CHECK(lines_has_prefix(&L, "bestmove"));
  lines_clear(&L); uci_destroy(e);
  end();
}

static void t_uci_invalid(void) {
  begin("uci: invalid FEN / illegal move produce info strings and keep the engine alive");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "position fen not a fen");
  CHECK(lines_has_sub(&L, "invalid fen"));
  uci_handle_line(e, "position startpos moves e2e5");
  CHECK(lines_has_sub(&L, "illegal move"));
  uci_handle_line(e, "position startpos");
  uci_handle_line(e, "go depth 3");
  CHECK(lines_has_prefix(&L, "bestmove"));
  lines_clear(&L); uci_destroy(e);
  end();
}

static void t_search_repeat(void) {
  begin("search: depth, nodes, PV legality and repeatability");
  const char *fens[] = {
    "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4",
    "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
    "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
    "2rq1rk1/pp1bppbp/3p1np1/8/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 3 12",
  };
  for (int f = 0; f < 4; f++) {
    SearchInfo r[2];
    volatile int stop = 0;
    for (int k = 0; k < 2; k++) {
      Searcher *s = searcher_create(8);
      searcher_set_stop_flag(s, &stop);
      Board b; board_create(&b, fens[f]);
      SearchLimits l; memset(&l, 0, sizeof(l)); l.depth = 7; l.has_depth = true;
      searcher_search(s, &b, &l, NULL, NULL, &r[k]);
      board_free(&b); searcher_destroy(s);
    }
    CHECK_EQ(r[0].depth, 7);
    CHECK(r[0].nodes > 100);
    CHECK_EQ(r[0].nodes, r[1].nodes);
    CHECK_EQ(r[0].best_move, r[1].best_move);
    CHECK_EQ(r[0].score, r[1].score);
    Board b; board_create(&b, fens[f]);
    for (int i = 0; i < r[0].pv_len; i++) {
      char u[16]; move_to_uci(r[0].pv[i], u);
      CHECK(is_legal_uci(&b, u));
      CHECK(make_move(&b, r[0].pv[i]));
    }
    CHECK(r[0].pv_len >= 3);
    board_free(&b);
  }
  end();
}

static void t_search_movetime(void) {
  begin("search: honours movetime and the stop flag");
  volatile int stop = 0;
  Searcher *s = searcher_create(8);
  searcher_set_stop_flag(s, &stop);
  Board b; board_create(&b, "r1bqkb1r/pppp1ppp/2n2n2/4p3/2B1P3/5N2/PPPP1PPP/RNBQK2R w KQkq - 4 4");
  SearchLimits l; memset(&l, 0, sizeof(l)); l.movetime = 300; l.has_movetime = true;
  SearchInfo r;
  double t0 = now_ms();
  searcher_search(s, &b, &l, NULL, NULL, &r);
  double dt = now_ms() - t0;
  CHECK(dt < 900);
  CHECK(r.best_move != 0);
  stop = 1;
  memset(&l, 0, sizeof(l)); l.infinite = true;
  searcher_search(s, &b, &l, NULL, NULL, &r);
  stop = 0;
  CHECK(r.best_move != 0);
  board_free(&b); searcher_destroy(s);
  end();
}

static bool brute_mate_in(Board *b, int n);
static bool defender_loses(Board *b, int n);

static bool brute_mate_in(Board *b, int n) {
  MoveList *ml = malloc(sizeof(MoveList));
  legal_list(b, ml);
  bool result = false;
  if (n == 0) { free(ml); return false; }
  for (int i = 0; i < ml->count && !result; i++) {
    make_move(b, ml->moves[i]);
    bool all = true;
    MoveList *rep = malloc(sizeof(MoveList));
    legal_list(b, rep);
    if (rep->count == 0) all = in_check(b);
    else if (n == 1) all = false;
    else {
      for (int j = 0; j < rep->count; j++) {
        make_move(b, rep->moves[j]);
        bool ok = brute_mate_in(b, n - 1);
        unmake_move(b);
        if (!ok) { all = false; break; }
      }
    }
    free(rep);
    unmake_move(b);
    if (all) result = true;
  }
  free(ml);
  return result;
}

static bool defender_loses(Board *b, int n) {
  MoveList *rep = malloc(sizeof(MoveList));
  legal_list(b, rep);
  bool res = true;
  if (rep->count == 0) res = in_check(b);
  else if (n == 0) res = false;
  else {
    for (int i = 0; i < rep->count; i++) {
      make_move(b, rep->moves[i]);
      bool ok = brute_mate_in(b, n);
      unmake_move(b);
      if (!ok) { res = false; break; }
    }
  }
  free(rep);
  return res;
}

static void t_mates(void) {
  begin("search: forced mates found by brute force are found and verified (mate in 1 and 2)");
  lcg_seed(2024);
  int found[3] = {0, 0, 0};
  char puzzles[32][128]; int pn[32]; int np = 0;
  int tries = 0;
  MoveList *ml = malloc(sizeof(MoveList));
  while ((found[1] < 4 || found[2] < 4) && tries < 4000) {
    tries++;
    Board b; board_create(&b, START_FEN);
    int plies = 20 + (int)(lcg_next() * 40);
    bool ok = true;
    for (int i = 0; i < plies; i++) {
      legal_list(&b, ml);
      if (ml->count == 0) { ok = false; break; }
      make_move(&b, ml->moves[(int)(lcg_next() * ml->count)]);
    }
    if (ok) {
      legal_list(&b, ml);
      if (ml->count == 0) ok = false;
    }
    if (ok) {
      for (int n = 1; n <= 2; n++) {
        if (found[n] >= 4) continue;
        if (n == 2 && brute_mate_in(&b, 1)) continue;
        if (brute_mate_in(&b, n)) {
          found[n]++;
          board_export_fen(&b, puzzles[np], 128);
          pn[np++] = n;
          break;
        }
      }
    }
    board_free(&b);
  }
  free(ml);
  CHECK(np >= 6);
  int quick = 0, total2 = 0;
  for (int i = 0; i < np; i++) {
    Board b; board_create(&b, puzzles[i]);
    Searcher *s = searcher_create(8);
    volatile int stop = 0;
    searcher_set_stop_flag(s, &stop);
    SearchLimits l; memset(&l, 0, sizeof(l)); l.depth = pn[i] == 1 ? 4 : 10; l.has_depth = true;
    SearchInfo r;
    searcher_search(s, &b, &l, NULL, NULL, &r);
    if (pn[i] == 1) {
      CHECK(r.score >= MATE_IN_MAX);
      CHECK(make_move(&b, r.best_move));
      MoveList *m2 = malloc(sizeof(MoveList));
      legal_list(&b, m2);
      CHECK(m2->count == 0 && in_check(&b));
      free(m2);
    } else {
      total2++;
      if (r.score >= MATE_IN_MAX) {
        int plies = MATE_SCORE - r.score;
        int mate_n = (plies + 1) >> 1;
        CHECK(make_move(&b, r.best_move));
        if (mate_n <= 3) CHECK(defender_loses(&b, mate_n - 1));
        quick++;
      }
    }
    searcher_destroy(s); board_free(&b);
  }
  CHECK(quick >= (total2 + 1) / 2);
  printf("        verified %d generated mates (mate-in-1: %d, mate-in-2: %d, mate-in-2 solved at depth 10: %d/%d)\n", np, found[1], found[2], quick, total2);
  end();
}

static int ev(const char *fen) {
  Board b; board_create(&b, fen);
  int v = evaluate(&b);
  board_free(&b);
  return v;
}

static void mirror_fen(const char *fen, char *out) {
  char buf[256]; snprintf(buf, sizeof(buf), "%s", fen);
  char *pl = strtok(buf, " "); char *stm = strtok(NULL, " "); char *rest = strtok(NULL, "");
  char *rows[8]; int n = 0;
  for (char *r = strtok(pl, "/"); r && n < 8; r = strtok(NULL, "/")) rows[n++] = r;
  char res[256]; int o = 0;
  for (int i = n - 1; i >= 0; i--) {
    for (char *c = rows[i]; *c; c++) {
      char ch = *c;
      if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
      else if (ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
      res[o++] = ch;
    }
    if (i > 0) res[o++] = '/';
  }
  res[o] = 0;
  snprintf(out, 256, "%s %s %s", res, !strcmp(stm, "w") ? "b" : "w", rest ? rest : "");
}

static void t_kpk_basic(void) {
  begin("KPK bitbase: exact win count and textbook positions");
  CHECK_EQ(kpk_win_count(), 111282);
  CHECK(ev("4k3/8/4K3/4P3/8/8/8/8 w - - 0 1") > 500);
  CHECK(ev("4k3/8/4K3/4P3/8/8/8/8 b - - 0 1") < -500);
  CHECK(ev("3k4/8/3K4/3P4/8/8/8/8 w - - 0 1") > 500);
  CHECK(ev("3k4/8/3K4/3P4/8/8/8/8 b - - 0 1") < -500);
  CHECK(ev("4k3/4P3/4K3/8/8/8/8/8 w - - 0 1") > 500);
  CHECK_EQ(ev("4k3/4P3/4K3/8/8/8/8/8 b - - 0 1"), 0);
  CHECK_EQ(ev("k7/8/8/8/8/8/P7/K7 w - - 0 1"), 0);
  CHECK_EQ(ev("7k/8/8/8/8/8/7P/7K w - - 0 1"), 0);
  const char *fens[] = {"4k3/8/4K3/4P3/8/8/8/8 w - - 0 1", "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1", "8/8/2k5/8/2P5/2K5/8/8 b - - 0 1", "1k6/8/1K6/P7/8/8/8/8 w - - 0 1", "8/5k2/8/8/8/5KP1/8/8 w - - 0 1"};
  for (int i = 0; i < 5; i++) { char m[256]; mirror_fen(fens[i], m); CHECK_EQ(ev(fens[i]), ev(m)); }
  end();
}

static bool win_of(Board *bd) {
  if (count_bits(bd->occupancies[BOTH]) == 2) return false;
  bool wp = bd->bitboards[W_PAWN] != 0;
  int strong = wp ? WHITE : BLACK;
  int psq = get_ls1b_index(bd->bitboards[wp ? W_PAWN : B_PAWN]);
  return kpk_is_win(bd->king_sq[strong], psq, bd->king_sq[strong ^ 1], bd->side == strong, wp);
}

static void t_kpk_random(void) {
  begin("KPK bitbase agrees with one-step play from the move generator (6000 random positions)");
  lcg_seed(31337);
  int checked = 0;
  MoveList *ml = malloc(sizeof(MoveList));
  while (checked < 6000) {
    int wk = (int)(lcg_next() * 64), bk = (int)(lcg_next() * 64), p = (int)(lcg_next() * 64);
    if (wk == bk || wk == p || bk == p) continue;
    int prow = p >> 3;
    if (prow == 0 || prow == 7) continue;
    bool white_strong = lcg_next() < 0.5;
    bool strong_to_move = lcg_next() < 0.5;
    char rows[8][9];
    for (int r = 0; r < 8; r++) { memset(rows[r], '1', 8); rows[r][8] = 0; }
    rows[wk >> 3][wk & 7] = white_strong ? 'K' : 'k';
    rows[bk >> 3][bk & 7] = white_strong ? 'k' : 'K';
    rows[p >> 3][p & 7] = white_strong ? 'P' : 'p';
    char side = (strong_to_move == white_strong) ? 'w' : 'b';
    char fen[128]; int o = 0;
    for (int r = 0; r < 8; r++) {
      int run = 0;
      for (int f = 0; f < 8; f++) {
        if (rows[r][f] == '1') run++;
        else { if (run) fen[o++] = (char)('0' + run); run = 0; fen[o++] = rows[r][f]; }
      }
      if (run) fen[o++] = (char)('0' + run);
      if (r < 7) fen[o++] = '/';
    }
    o += snprintf(fen + o, sizeof(fen) - (size_t)o, " %c - - 0 1", side);
    Board b; board_create(&b, fen);
    int dx = abs((wk & 7) - (bk & 7)), dy = abs((wk >> 3) - (bk >> 3));
    if ((dx > dy ? dx : dy) <= 1) { board_free(&b); continue; }
    b.side ^= 1;
    bool other_in_check = in_check(&b);
    b.side ^= 1;
    if (other_in_check) { board_free(&b); continue; }
    int pawn_rank_own = white_strong ? 7 - prow : prow;
    if (strong_to_move && pawn_rank_own == 6) { board_free(&b); continue; }
    bool here = win_of(&b);
    legal_list(&b, ml);
    bool any = false, all = true;
    for (int i = 0; i < ml->count; i++) {
      make_move(&b, ml->moves[i]);
      bool k = win_of(&b);
      unmake_move(&b);
      if (k) any = true; else all = false;
    }
    bool expected = strong_to_move ? any : (ml->count > 0 && all);
    CHECK(here == expected);
    if (here != expected) { printf("        %s\n", fen); break; }
    checked++;
    board_free(&b);
  }
  free(ml);
  end();
}

static void t_endings(void) {
  begin("eval: KNN vs K is not a win; pure pawn ending with unstoppable passer is");
  CHECK(abs(ev("4k3/8/8/8/8/8/8/1NN1K3 w - - 0 1")) < 40);
  int far = ev("8/6kp/8/P7/8/8/5KP1/8 w - - 0 1");
  int near = ev("8/1k5p/8/P7/8/8/5KP1/8 w - - 0 1");
  CHECK(far - near > 250);
  end();
}

static void t_hanging(void) {
  begin("search: hanging queen is captured, own queen is not left en prise");
  volatile int stop = 0;
  Searcher *s = searcher_create(8);
  searcher_set_stop_flag(s, &stop);
  Board b; board_create(&b, "4k3/8/8/3q4/8/8/8/3RK3 w - - 0 1");
  SearchLimits l; memset(&l, 0, sizeof(l)); l.depth = 5; l.has_depth = true;
  SearchInfo r;
  searcher_search(s, &b, &l, NULL, NULL, &r);
  char u[16]; move_to_uci(r.best_move, u);
  CHECK_STR(u, "d1d5");
  board_free(&b);
  board_create(&b, "r1bqkbnr/pppp1ppp/2n5/4p3/4P3/5N2/PPPP1PPP/RNBQKB1R w KQkq - 2 3");
  Searcher *s2 = searcher_create(8); searcher_set_stop_flag(s2, &stop);
  l.depth = 6;
  searcher_search(s2, &b, &l, NULL, NULL, &r);
  CHECK(make_move(&b, r.best_move));
  Searcher *s3 = searcher_create(8); searcher_set_stop_flag(s3, &stop);
  l.depth = 3;
  SearchInfo reply;
  searcher_search(s3, &b, &l, NULL, NULL, &reply);
  CHECK(reply.score < 150);
  searcher_destroy(s); searcher_destroy(s2); searcher_destroy(s3); board_free(&b);
  end();
}

static void t_repetition(void) {
  begin("search: repetition scoring");
  Searcher *s = searcher_create(8);
  s->root_draw = -20;
  CHECK_EQ(searcher_repetition_score(s, 0), -20);
  CHECK_EQ(searcher_repetition_score(s, 1), 20);
  CHECK_EQ(searcher_repetition_score(s, 2), -20);
  searcher_destroy(s);
  end();
}

static void t_mate_uci(void) {
  begin("uci: mate scores are well-formed");
  Lines L = {0}; UciEngine *e = new_uci(&L);
  uci_handle_line(e, "position fen 6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1");
  uci_handle_line(e, "go depth 6");
  const char *last = lines_last_prefix(&L, "info depth");
  CHECK(last && strstr(last, "score mate 1"));
  CHECK(lines_has_exact(&L, "bestmove d1d8"));
  CHECK(MATE_SCORE > MATE_IN_MAX);
  int zero[1] = {0};
  char pv[64];
  pv_to_uci(zero, 1, pv, sizeof(pv));
  CHECK_STR(pv, "0000");
  lines_clear(&L); uci_destroy(e);
  end();
}

static BookManager *mgr_new(void) {
  BookManager *m = calloc(1, sizeof(BookManager));
  book_manager_init(m);
  return m;
}

static BookManager *shared;

static void t_eco_load(void) {
  begin("eco: all 6 ECO files load and every record is counted");
  BookStats st = book_manager_stats(shared);
  CHECK(st.ready);
  CHECK_EQ(st.nfiles, 6);
  for (int i = 0; i < st.nfiles; i++) {
    CHECK(st.files[i].ok);
    CHECK_EQ(st.files[i].rejected, 0);
    CHECK_EQ(st.files[i].accepted, st.files[i].total);
  }
  CHECK_EQ(st.records + st.duplicates, 15838);
  CHECK_EQ(st.indexed, st.records);
  CHECK_EQ(st.illegal, 0);
  CHECK_EQ(st.fen_mismatch, 0);
  printf("        records %d, positions %d, edges %d, classified %d, load %d ms\n", st.indexed, st.positions, st.edges, st.classified, st.load_ms);
  end();
}

static void play_san(Board *b, const char *line) {
  char **toks; char *arena;
  int n = tokenize_moves(line, &toks, &arena);
  for (int i = 0; i < n; i++) {
    int m = parse_san(b, toks[i]);
    CHECK(m != 0);
    if (m) CHECK(make_move(b, m));
  }
  free(toks); free(arena);
}

static void t_san(void) {
  begin("eco: SAN parser castling, promotion, en passant, disambiguation, annotations");
  char u[16];
  Board b; board_create(&b, "r3k2r/8/8/8/8/8/8/R3K2R w KQkq - 0 1");
  move_to_uci(parse_san(&b, "O-O"), u); CHECK_STR(u, "e1g1");
  move_to_uci(parse_san(&b, "O-O-O+"), u); CHECK_STR(u, "e1c1");
  board_free(&b);
  board_create(&b, "8/P7/8/8/8/8/p7/k6K w - - 0 1");
  move_to_uci(parse_san(&b, "a8=Q"), u); CHECK_STR(u, "a7a8q");
  move_to_uci(parse_san(&b, "a8=N"), u); CHECK_STR(u, "a7a8n");
  board_free(&b);
  board_create(&b, "rnbqkbnr/ppp1p1pp/8/3pPp2/8/8/PPPP1PPP/RNBQKBNR w KQkq f6 0 3");
  move_to_uci(parse_san(&b, "exf6"), u); CHECK_STR(u, "e5f6");
  board_free(&b);
  board_create(&b, "4k3/8/8/8/8/8/4K3/R6R w - - 0 1");
  move_to_uci(parse_san(&b, "Rad1"), u); CHECK_STR(u, "a1d1");
  move_to_uci(parse_san(&b, "Rhd1"), u); CHECK_STR(u, "h1d1");
  CHECK_EQ(parse_san(&b, "Rd1"), 0);
  CHECK_EQ(parse_san(&b, "Qd1"), 0);
  CHECK_EQ(parse_san(&b, "e5"), 0);
  board_free(&b);
  char **toks; char *arena;
  int n = tokenize_moves("1.e4 e5 2. Nf3 Nc6 3...a6 1-0", &toks, &arena);
  const char *exp[] = {"e4", "e5", "Nf3", "Nc6", "a6"};
  CHECK_EQ(n, 5);
  for (int i = 0; i < n && i < 5; i++) CHECK_STR(toks[i], exp[i]);
  free(toks); free(arena);
  end();
}

static void t_classify(void) {
  begin("eco: classification by move history");
  struct { const char *line, *eco, *name; } cases[] = {
    {"1. e4 e5 2. Nf3 Nc6 3. Bb5", "C60", "Ruy Lopez"},
    {"1. e4 c5 2. Nf3 d6 3. d4 cxd4 4. Nxd4 Nf6 5. Nc3 a6", "B9", "Najdorf"},
    {"1. d4 d5 2. c4 e6", "D", "Queen's Gambit"},
    {"1. e4 e6 2. d4 d5", "C0", "French"},
    {"1. d4 Nf6 2. c4 g6 3. Nc3 Bg7 4. e4 d6", "E7", "King's Indian"},
    {"1. c4", "A1", "English"},
  };
  for (int i = 0; i < 6; i++) {
    Board b; board_create(&b, START_FEN);
    play_san(&b, cases[i].line);
    OpeningInfo info; memset(&info, 0, sizeof(info));
    bool ok = book_manager_track(shared, &b, &info);
    CHECK(ok);
    if (ok) { CHECK(strncmp(info.eco, cases[i].eco, strlen(cases[i].eco)) == 0); CHECK(strstr(info.name, cases[i].name) != NULL); }
    board_free(&b);
  }
  end();
}

static void t_transpose(void) {
  begin("eco: transpositions reach the same classification");
  Board a, b; board_create(&a, START_FEN); board_create(&b, START_FEN);
  play_san(&a, "1. d4 Nf6 2. c4 e6 3. Nc3 Bb4");
  play_san(&b, "1. c4 Nf6 2. Nc3 e6 3. d4 Bb4");
  OpeningInfo ia, ib; memset(&ia, 0, sizeof(ia)); memset(&ib, 0, sizeof(ib));
  CHECK(book_manager_track(shared, &a, &ia) && book_manager_track(shared, &b, &ib));
  CHECK_STR(ia.eco, "E20");
  CHECK_STR(ia.eco, ib.eco);
  CHECK_STR(ia.name, ib.name);
  CHECK(ib.exact);
  board_free(&a); board_free(&b);
  end();
}

static void t_leave_theory(void) {
  begin("eco: classification keeps following the game after leaving theory");
  Board b; board_create(&b, START_FEN);
  play_san(&b, "1. e4 e5 2. Nf3 Nc6 3. Bb5 a6 4. Ba4 Nf6 5. O-O Be7 6. Re1 b5 7. Bb3 O-O 8. c3 d5 9. exd5 Nxd5 10. a4 h6 11. h3 g5");
  OpeningInfo info; memset(&info, 0, sizeof(info));
  CHECK(book_manager_track(shared, &b, &info));
  CHECK(!info.exact);
  CHECK(info.matched_ply < b.hist_ply);
  CHECK(info.eco[0] == 'C');
  board_free(&b);
  end();
}

static void t_unknown(void) {
  begin("eco: unknown / irregular start has no classification");
  OpeningInfo info; memset(&info, 0, sizeof(info));
  Board b; board_create(&b, START_FEN);
  CHECK(!book_manager_track(shared, &b, &info));
  board_free(&b);
  board_create(&b, "8/8/8/4k3/8/4K3/4P3/8 w - - 0 1");
  CHECK(!book_manager_track(shared, &b, &info));
  board_free(&b);
  board_create(&b, START_FEN);
  play_san(&b, "1. a3 h6 2. b3 g6 3. c3 f6 4. d3 e6");
  bool f = book_manager_track(shared, &b, &info);
  CHECK(!f || !info.exact);
  board_free(&b);
  end();
}

static BookManager *loaded_mgr(void) {
  BookManager *m = mgr_new();
  book_manager_load_dir(m, "data/eco");
  return m;
}

static void t_candidates(void) {
  begin("eco: book offers several legal moves from the start position");
  Board b; board_create(&b, START_FEN);
  static BookProbe hit;
  CHECK(book_manager_probe(shared, &b, &hit));
  CHECK(hit.ncands >= 3);
  double sum = 0;
  for (int i = 0; i < hit.ncands; i++) { CHECK(is_legal_uci(&b, hit.cands[i].uci)); sum += hit.cands[i].prob; }
  CHECK(is_legal_uci(&b, hit.uci));
  CHECK(fabs(sum - 1) < 1e-9);
  board_free(&b);
  end();
}

static void t_variety(void) {
  begin("eco: variety 0 is deterministic; variety > 0 produces different openings");
  BookManager *m = loaded_mgr();
  book_manager_configure(m, -1, 0, true);
  char first[8] = "";
  static BookProbe hit;
  int distinct0 = 0;
  for (int i = 0; i < 20; i++) {
    Board b; board_create(&b, START_FEN);
    CHECK(book_manager_probe(m, &b, &hit));
    if (i == 0) { snprintf(first, sizeof(first), "%s", hit.uci); distinct0 = 1; }
    else if (strcmp(first, hit.uci)) distinct0++;
    board_free(&b);
  }
  CHECK_EQ(distinct0, 1);
  book_manager_configure(m, -1, 1, true);
  char seen[64][8]; int ns = 0;
  for (int i = 0; i < 300; i++) {
    Board b; board_create(&b, START_FEN);
    CHECK(book_manager_probe(m, &b, &hit));
    bool dup = false;
    for (int k = 0; k < ns; k++) if (!strcmp(seen[k], hit.uci)) dup = true;
    if (!dup && ns < 64) snprintf(seen[ns++], 8, "%s", hit.uci);
    board_free(&b);
  }
  CHECK(ns >= 3);
  book_manager_free(m); free(m);
  end();
}

static void t_random_games(void) {
  begin("eco: book never plays an illegal move: 300 random book games");
  BookManager *m = loaded_mgr();
  book_manager_configure(m, -1, 1, true);
  static BookProbe hit;
  long moves_played = 0, total_len = 0;
  static char openings[2000][160]; int nop = 0;
  for (int g = 0; g < 300; g++) {
    Board b; board_create(&b, START_FEN);
    int n = 0;
    for (;;) {
      if (!book_manager_probe(m, &b, &hit)) break;
      CHECK(is_legal_uci(&b, hit.uci));
      CHECK(make_move(&b, hit.move));
      n++; moves_played++;
      if (n >= 80) { CHECK(0); break; }
    }
    total_len += n;
    OpeningInfo info; memset(&info, 0, sizeof(info));
    if (book_manager_track(m, &b, &info)) {
      char key[160]; snprintf(key, sizeof(key), "%s %s", info.eco, info.name);
      bool dup = false;
      for (int k = 0; k < nop; k++) if (!strcmp(openings[k], key)) { dup = true; break; }
      if (!dup && nop < 2000) snprintf(openings[nop++], 160, "%s", key);
    }
    board_free(&b);
  }
  double avg = (double)total_len / 300.0;
  CHECK(avg > 6);
  CHECK(nop > 40);
  printf("        %ld book moves, avg length %.1f plies, %d distinct named openings\n", moves_played, avg, nop);
  book_manager_free(m); free(m);
  end();
}

static void t_handoff(void) {
  begin("eco: book -> search hand-off");
  BookManager *m = loaded_mgr();
  book_manager_configure(m, -1, 0, true);
  Board b; board_create(&b, START_FEN);
  static BookProbe hit;
  int guard = 0;
  while (guard++ < 60 && book_manager_probe(m, &b, &hit)) CHECK(make_move(&b, hit.move));
  CHECK(guard > 3);
  CHECK(!book_manager_probe(m, &b, &hit));
  Searcher *s = searcher_create(8);
  volatile int stop = 0;
  searcher_set_stop_flag(s, &stop);
  SearchLimits l; memset(&l, 0, sizeof(l)); l.depth = 5; l.has_depth = true;
  SearchInfo r;
  searcher_search(s, &b, &l, NULL, NULL, &r);
  char u[16]; move_to_uci(r.best_move, u);
  CHECK(is_legal_uci(&b, u));
  searcher_destroy(s); board_free(&b); book_manager_free(m); free(m);
  end();
}

static void t_margin(void) {
  begin("eco: weak book moves are filtered by evaluation margin");
  Board b; board_create(&b, START_FEN);
  play_san(&b, "1. e4 e5");
  static BookPosition pos;
  BookEdge edges[5];
  const char *u[] = {"g1f3", "d1h5", "a2a4", "e1e2", "z9z9"};
  memset(edges, 0, sizeof(edges));
  for (int i = 0; i < 5; i++) { snprintf(edges[i].uci, sizeof(edges[i].uci), "%s", u[i]); edges[i].lines = 50; edges[i].quality = 100; edges[i].reach = 10; edges[i].ends = 1; }
  pos.edges = edges; pos.n = 5; pos.cap = 5;
  BookOptions opt = BOOK_DEFAULTS; opt.margin = 40;
  BookCandidate kept[MAX_MOVES];
  int nk = book_rank_candidates(&b, &pos, &opt, kept);
  bool has_bad = false;
  int best = -100000;
  for (int i = 0; i < nk; i++) { if (!strcmp(kept[i].uci, "z9z9")) has_bad = true; if (kept[i].score > best) best = kept[i].score; }
  CHECK(!has_bad);
  CHECK(nk >= 1 && nk < 4);
  for (int i = 0; i < nk; i++) CHECK(kept[i].score >= best - 40);
  board_free(&b);
  end();
}

static void t_errors(void) {
  begin("eco: missing file, invalid json and bad records are handled without throwing");
  const char *texts[ECO_FILE_COUNT];
  char *bufs[ECO_FILE_COUNT];
  for (int i = 0; i < ECO_FILE_COUNT; i++) {
    char path[512]; snprintf(path, sizeof(path), "data/eco/%s", ECO_FILES[i]);
    size_t len; bufs[i] = eco_read_file(path, &len);
    texts[i] = bufs[i];
  }
  texts[1] = NULL;
  texts[2] = "{ this is not json";
  BookManager *m = mgr_new();
  book_manager_load_texts(m, texts);
  BookStats st = book_manager_stats(m);
  CHECK(st.ready);
  CHECK(!st.files[1].ok); CHECK_STR(st.files[1].error, "file missing");
  CHECK(!st.files[2].ok); CHECK(strncmp(st.files[2].error, "invalid json", 12) == 0);
  CHECK(st.files[0].ok);
  book_manager_free(m); free(m);

  BookManager *em = mgr_new();
  const char *none[ECO_FILE_COUNT] = {0};
  book_manager_load_texts(em, none);
  BookStats es = book_manager_stats(em);
  CHECK(!es.ready);
  Board b; board_create(&b, START_FEN);
  static BookProbe hit;
  CHECK(!book_manager_probe(em, &b, &hit));
  OpeningInfo oi; memset(&oi, 0, sizeof(oi));
  CHECK(!book_manager_track(em, &b, &oi));
  board_free(&b); book_manager_free(em); free(em);

  const char *bad[ECO_FILE_COUNT] = {0};
  bad[0] = "{\"rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1\":{\"eco\":\"Z99\",\"name\":\"n\",\"moves\":\"1. e4\"},\"only three fields\":{\"eco\":\"A00\",\"name\":\"n\",\"moves\":\"1. e4\"},\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\":{\"eco\":\"B00 \",\"name\":\" King's Pawn \",\"moves\":\"1.e4\"}}";
  EcoCollection col;
  eco_collect(bad, &col);
  CHECK_EQ(col.statuses[0].accepted, 1);
  CHECK_EQ(col.statuses[0].rejected, 2);
  if (col.count) { CHECK_STR(col.records[0].eco, "B00"); CHECK_STR(col.records[0].name, "King's Pawn"); }
  eco_collection_free(&col);

  const char *dup[ECO_FILE_COUNT] = {0};
  const char *one = "{\"rnbqkbnr/pppppppp/8/8/4P3/8/PPPP1PPP/RNBQKBNR b KQkq - 0 1\":{\"eco\":\"B00\",\"name\":\"x\",\"moves\":\"1. e4\"}}";
  dup[0] = one; dup[1] = one;
  eco_collect(dup, &col);
  CHECK_EQ(col.count, 1);
  CHECK_EQ(col.duplicates, 1);
  eco_collection_free(&col);
  for (int i = 0; i < ECO_FILE_COUNT; i++) free(bufs[i]);
  end();
}

static void t_disabled(void) {
  begin("eco: disabled book (OwnBook false) returns nothing");
  BookManager *m = loaded_mgr();
  book_manager_configure(m, 0, 0, false);
  Board b; board_create(&b, START_FEN);
  static BookProbe hit;
  CHECK(!book_manager_probe(m, &b, &hit));
  book_manager_configure(m, 1, 0, false);
  CHECK(book_manager_probe(m, &b, &hit));
  board_free(&b); book_manager_free(m); free(m);
  end();
}

static void t_cached(void) {
  begin("eco: book load is cached, second load does not rebuild");
  double t0 = now_ms();
  BookStats again = book_manager_load_dir(shared, "data/eco");
  CHECK(now_ms() - t0 < 50);
  BookStats st = book_manager_stats(shared);
  CHECK_EQ(again.records, st.records);
  CHECK_EQ(ECO_FILE_COUNT, 6);
  end();
}


static bool nnue_ready(void) {
  if (nnue_loaded()) return true;
  char err[512];
  char found[1500];
  const char *path = nnue_find_file(NNUE_DEFAULT_FILE, NULL, found, sizeof(found));
  return path && nnue_load(path, err, sizeof(err));
}

static void t_nnue_load_errors(void) {
  begin("nnue: missing, garbage and truncated files are rejected with a message");
  char err[512];
  bool was = nnue_loaded();
  char keep[1200];
  snprintf(keep, sizeof(keep), "%s", nnue_loaded_path());
  err[0] = 0;
  CHECK(!nnue_load("no/such/file.nnue", err, sizeof(err)));
  CHECK(strstr(err, "cannot open") != NULL);
  FILE *f = fopen("build/garbage.nnue", "wb");
  if (f) { for (int i = 0; i < 400; i++) fputc(i * 7, f); fclose(f); }
  err[0] = 0;
  CHECK(!nnue_load("build/garbage.nnue", err, sizeof(err)));
  CHECK(err[0] != 0);
  f = fopen("build/short.nnue", "wb");
  if (f) { uint32_t h[3] = {0x7AF32F16u, 0x3E5AA6EEu, 0}; fwrite(h, 4, 3, f); fclose(f); }
  err[0] = 0;
  CHECK(!nnue_load("build/short.nnue", err, sizeof(err)));
  CHECK(err[0] != 0);
  CHECK_EQ(nnue_loaded(), was);
  if (was) CHECK_STR(nnue_loaded_path(), keep);
  remove("build/garbage.nnue");
  remove("build/short.nnue");
  end();
}

static void t_nnue_simd_vs_scalar(void) {
  begin("nnue: SIMD forward pass equals the scalar reference on random playouts");
  if (!nnue_ready()) { printf("        net not found, skipped\n"); end(); return; }
  lcg_seed(77);
  int checked = 0;
  for (int g = 0; g < 40; g++) {
    Board b; board_create(&b, BOARD_FENS[g % 13]);
    for (int ply = 0; ply < 80; ply++) {
      static MoveList ml;
      legal_list(&b, &ml);
      if (ml.count == 0) break;
      CHECK_EQ(nnue_raw_board(&b), nnue_raw_reference(&b));
      checked++;
      make_move(&b, ml.moves[(int)(lcg_next() * ml.count)]);
    }
    board_free(&b);
  }
  CHECK(checked > 1000);
  end();
}

static void t_nnue_incremental(void) {
  begin("nnue: incremental accumulator equals full refresh (moves, null moves, unmake)");
  if (!nnue_ready()) { printf("        net not found, skipped\n"); end(); return; }
  lcg_seed(2024);
  NnueStack *st = nnue_stack_create();
  int checked = 0;
  for (int g = 0; g < 60; g++) {
    Board b; board_create(&b, BOARD_FENS[g % 13]);
    nnue_stack_reset_root(st);
    int ply = 0;
    bool nullflag[NNUE_STACK_SIZE];
    for (int step = 0; step < 400; step++) {
      double r = lcg_next();
      if (r < 0.30 && ply > 0) {
        if (nullflag[ply]) unmake_null_move(&b); else unmake_move(&b);
        ply--;
      } else if (r < 0.36 && ply < 60 && !in_check(&b)) {
        nnue_push_null(st, ply + 1);
        make_null_move(&b);
        ply++;
        nullflag[ply] = true;
      } else if (ply < 60) {
        static MoveList ml;
        legal_list(&b, &ml);
        if (ml.count == 0) { if (ply > 0) { if (nullflag[ply]) unmake_null_move(&b); else unmake_move(&b); ply--; } continue; }
        int m = ml.moves[(int)(lcg_next() * ml.count)];
        nnue_push_move(st, ply + 1, &b, m);
        make_move(&b, m);
        ply++;
        nullflag[ply] = false;
      }
      if (lcg_next() < 0.6) {
        CHECK_EQ(nnue_raw_stack(st, &b, ply), nnue_raw_board(&b));
        checked++;
      }
    }
    while (ply > 0) { if (nullflag[ply]) unmake_null_move(&b); else unmake_move(&b); ply--; }
    board_free(&b);
  }
  CHECK(checked > 5000);
  nnue_stack_free(st);
  end();
}

static void t_nnue_search(void) {
  begin("nnue: search with NNUE, threads and skill level returns legal moves");
  if (!nnue_ready()) { printf("        net not found, skipped\n"); end(); return; }
  nnue_set_enabled(true);
  Searcher *s = searcher_create(16);
  volatile int stop = 0;
  searcher_set_stop_flag(s, &stop);
  for (int cfg = 0; cfg < 4; cfg++) {
    searcher_set_threads(s, cfg == 1 || cfg == 3 ? 3 : 1);
    searcher_set_strength(s, cfg >= 2 ? 3 : 20, false, 1500);
    for (int i = 0; i < 6; i++) {
      Board b; board_create(&b, BOARD_FENS[i]);
      SearchLimits l; memset(&l, 0, sizeof(l));
      l.depth = 7; l.has_depth = true;
      SearchInfo r;
      searcher_search(s, &b, &l, NULL, NULL, &r);
      char mv[16];
      CHECK(r.best_move != 0);
      if (r.best_move) { move_to_uci(r.best_move, mv); CHECK(is_legal_uci(&b, mv)); }
      CHECK_EQ(b.hist_ply, 0);
      board_free(&b);
    }
  }
  searcher_destroy(s);
  end();
}

static void t_nnue_scores(void) {
  begin("nnue: evaluation is sane (material, symmetry, hybrid endgames)");
  if (!nnue_ready()) { printf("        net not found, skipped\n"); end(); return; }
  nnue_set_enabled(true);
  Board a, b, c, d;
  board_create(&a, START_FEN);
  CHECK(abs(evaluate(&a)) < 60);
  board_create(&b, "rnb1kbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
  CHECK(evaluate(&b) > 500);
  board_create(&c, "4k3/8/8/8/8/8/8/KQ6 w - - 0 1");
  CHECK(evaluate(&c) > 500);
  board_create(&d, "4k3/8/8/8/8/8/8/4K3 w - - 0 1");
  CHECK_EQ(evaluate(&d), 0);
  nnue_set_enabled(false);
  CHECK_EQ(evaluate(&a), evaluate_classical(&a));
  nnue_set_enabled(true);
  board_free(&a); board_free(&b); board_free(&c); board_free(&d);
  end();
}

int main(void) {
  init_engine_once();
  printf("board\n");
  t_board_roundtrip(); t_board_invalid(); t_board_text(); t_board_playouts(); t_board_special();
  printf("engine\n");
  t_perft(); t_uci_handshake(); t_uci_book(); t_uci_ownbook(); t_uci_opening(); t_uci_nomoves(); t_uci_invalid();
  t_search_repeat(); t_search_movetime(); t_mates(); t_hanging(); t_kpk_basic(); t_kpk_random(); t_endings(); t_repetition(); t_mate_uci();
  printf("eco\n");
  shared = loaded_mgr();
  t_eco_load(); t_san(); t_classify(); t_transpose(); t_leave_theory(); t_unknown(); t_candidates(); t_variety(); t_random_games(); t_handoff(); t_margin(); t_errors(); t_disabled(); t_cached();
  printf("nnue\n");
  t_nnue_load_errors(); t_nnue_simd_vs_scalar(); t_nnue_incremental(); t_nnue_search(); t_nnue_scores();
  printf("\nTOTAL: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail ? 1 : 0;
}
