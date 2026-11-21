#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface/uci.h"
#include "platform/platform.h"

const char *const BENCH_FENS[BENCH_FEN_COUNT] = {
  "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1",
  "r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1",
  "8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - - 0 1",
  "r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1",
  "rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8",
  "r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10",
  "r1bq1rk1/pp2ppbp/2np1np1/8/3NP3/2N1BP2/PPPQ2PP/R3KB1R w KQ - 2 9",
  "2rq1rk1/pp1bppbp/3p1np1/8/3NP3/1BN1BP2/PPPQ2PP/2KR3R w - - 3 12",
  "8/5pk1/6p1/8/5P2/6P1/5K2/8 w - - 0 1",
  "6k1/5ppp/8/8/8/8/5PPP/3R2K1 w - - 0 1",
  "4k3/8/8/8/8/8/4P3/4K3 w - - 0 1",
  "r2q1rk1/ppp2ppp/2n1bn2/3pp3/1b2P3/2NP1N2/PPP1BPPP/R1BQ1RK1 w - - 4 8",
};

#define MAX_TOKENS 4096
#define LINE_CAP 16384

void init_engine_once(void) {
  static bool done = false;
  if (done) return;
  init_core();
  init_brain();
  done = true;
}

static bool js_parse_int(const char *s, long long *out) {
  if (!s) return false;
  while (*s && isspace((unsigned char)*s)) s++;
  const char *p = s;
  if (*p == '+' || *p == '-') p++;
  if (!isdigit((unsigned char)*p)) return false;
  *out = strtoll(s, NULL, 10);
  return true;
}

static void emit(UciEngine *e, const char *line) {
  e->write(line, e->ud);
}

static void emitf(UciEngine *e, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
#include <stdarg.h>
static void emitf(UciEngine *e, const char *fmt, ...) {
  char buf[LINE_CAP];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  emit(e, buf);
}

UciEngine *uci_create(UciWrite write, void *ud, volatile int *stop_flag) {
  init_engine_once();
  UciEngine *e = calloc(1, sizeof(UciEngine));
  e->write = write;
  e->ud = ud;
  board_create(&e->board, START_FEN);
  e->searcher = searcher_create(128);
  searcher_set_stop_flag(e->searcher, stop_flag ? stop_flag : &e->own_stop);
  e->options.Hash = 128;
  e->options.MoveOverhead = 30;
  e->options.Threads = 1;
  e->options.OwnBook = true;
  e->options.BookVariety = 70;
  snprintf(e->options.EvalFile, sizeof(e->options.EvalFile), "%s", NNUE_DEFAULT_FILE);
  e->options.UseNNUE = true;
  e->options.SkillLevel = 20;
  e->options.LimitStrength = false;
  e->options.Elo = 1320;
  nnue_set_enabled(true);
  e->quit = false;
  e->book_loaded = false;
  if (!g_book.options.margin) book_manager_init(&g_book);
  book_manager_configure(&g_book, 1, 0.7, true);
  return e;
}

void uci_destroy(UciEngine *e) {
  if (!e) return;
  searcher_destroy(e->searcher);
  board_free(&e->board);
  free(e);
}

void uci_set_paths(UciEngine *e, const char *eco_dir_hint, const char *argv0) {
  e->eco_dir_hint = eco_dir_hint;
  e->argv0 = argv0;
}

static bool uci_load_nnue(UciEngine *e) {
  char found[1500];
  char err[1600];
  e->nnue_tried = true;
  const char *path = nnue_find_file(e->options.EvalFile, e->argv0, found, sizeof(found));
  if (!path) {
    emitf(e, "info string ERROR: NNUE evaluation file '%s' not found (searched working directory, executable directory, ../, net/, VEXAPH_NNUE_DIR). %s", e->options.EvalFile, nnue_loaded() ? "Keeping the previously loaded network" : "Falling back to classical evaluation");
    return false;
  }
  if (nnue_loaded() && !strcmp(nnue_loaded_path(), path)) return true;
  if (!nnue_load(path, err, sizeof(err))) {
    emitf(e, "info string ERROR: %s. %s", err, nnue_loaded() ? "Keeping the previously loaded network" : "Falling back to classical evaluation");
    return false;
  }
  emitf(e, "info string NNUE evaluation using %s (%s)", path, nnue_simd_name());
  return true;
}

void uci_ensure_nnue(UciEngine *e) {
  if (e->nnue_tried || !e->options.UseNNUE) return;
  uci_load_nnue(e);
}

void uci_ensure_book(UciEngine *e) {
  if (e->book_loaded || !e->options.OwnBook) return;
  e->book_loaded = true;
  const char *dir = book_find_eco_dir(e->eco_dir_hint, e->argv0);
  BookStats st = book_manager_load_dir(&g_book, dir);
  if (!st.ready) {
    char buf[LINE_CAP];
    int o = snprintf(buf, sizeof(buf), "info string book unavailable");
    if (st.missing > 0) {
      o += snprintf(buf + o, sizeof(buf) - (size_t)o, " missing ");
      for (int i = 0; i < st.missing; i++) o += snprintf(buf + o, sizeof(buf) - (size_t)o, "%s%s", i ? "," : "", st.missing_names[i]);
    }
    emit(e, buf);
    return;
  }
  char bad[4096];
  int bo = 0;
  int nbad = 0;
  for (int i = 0; i < st.nfiles; i++) {
    if (!st.files[i].ok) {
      bo += snprintf(bad + bo, sizeof(bad) - (size_t)bo, "%s%s:%s", nbad ? "," : "", st.files[i].file, st.files[i].error);
      nbad++;
    }
  }
  char buf[LINE_CAP];
  int o = snprintf(buf, sizeof(buf), "info string book loaded records %d positions %d time %d", st.indexed, st.positions, st.load_ms);
  if (nbad) snprintf(buf + o, sizeof(buf) - (size_t)o, " errors %s", bad);
  emit(e, buf);
}

static void cmd_uci(UciEngine *e) {
  emit(e, "id name " ENGINE_NAME " " ENGINE_VERSION);
  emit(e, "id author " ENGINE_AUTHOR);
  emit(e, "option name Hash type spin default 128 min 1 max 4096");
  emit(e, "option name Clear Hash type button");
  emit(e, "option name Move Overhead type spin default 30 min 0 max 5000");
  emit(e, "option name Threads type spin default 1 min 1 max 256");
  emit(e, "option name OwnBook type check default true");
  emit(e, "option name Book Variety type spin default 70 min 0 max 100");
  emit(e, "option name EvalFile type string default " NNUE_DEFAULT_FILE);
  emit(e, "option name Use NNUE type check default true");
  emit(e, "option name Skill Level type spin default 20 min 0 max 20");
  emit(e, "option name UCI_LimitStrength type check default false");
  emit(e, "option name UCI_Elo type spin default 1320 min 1320 max 3190");
  emit(e, "uciok");
}

static void cmd_ucinewgame(UciEngine *e) {
  searcher_new_game(e->searcher);
  board_parse_fen(&e->board, START_FEN);
}

static int tok_index(char **t, int n, const char *s) {
  for (int i = 0; i < n; i++) if (!strcmp(t[i], s)) return i;
  return -1;
}

static void join_tokens(char **t, int from, int to, char *out, size_t cap) {
  size_t o = 0;
  out[0] = 0;
  for (int i = from; i < to; i++) {
    int w = snprintf(out + o, cap - o, "%s%s", i > from ? " " : "", t[i]);
    if (w < 0 || (size_t)w >= cap - o) break;
    o += (size_t)w;
  }
}

static void lower_inplace(char *s) {
  for (; *s; s++) *s = (char)tolower((unsigned char)*s);
}

static void cmd_setoption(UciEngine *e, char **t, int n) {
  int name_idx = tok_index(t, n, "name");
  if (name_idx < 0) return;
  int value_idx = tok_index(t, n, "value");
  char name[512], lname[512], value[512];
  join_tokens(t, name_idx + 1, value_idx >= 0 ? value_idx : n, name, sizeof(name));
  if (value_idx >= 0) join_tokens(t, value_idx + 1, n, value, sizeof(value));
  else value[0] = 0;
  snprintf(lname, sizeof(lname), "%s", name);
  lower_inplace(lname);
  long long v;

  if (!strcmp(lname, "hash")) {
    if (js_parse_int(value, &v) && v >= 1) {
      e->options.Hash = v > 4096 ? 4096 : (int)v;
      searcher_resize_hash(e->searcher, e->options.Hash);
    }
  } else if (!strcmp(lname, "clear hash")) {
    searcher_clear_hash(e->searcher);
  } else if (!strcmp(lname, "move overhead")) {
    if (js_parse_int(value, &v) && v >= 0) {
      e->options.MoveOverhead = v > 5000 ? 5000 : (int)v;
      e->searcher->move_overhead = e->options.MoveOverhead;
    }
  } else if (!strcmp(lname, "threads")) {
    if (js_parse_int(value, &v) && v >= 1) {
      e->options.Threads = v > 256 ? 256 : (int)v;
      searcher_set_threads(e->searcher, e->options.Threads);
    }
  } else if (!strcmp(lname, "evalfile")) {
    if (!value[0] || !strcmp(value, "<empty>")) snprintf(e->options.EvalFile, sizeof(e->options.EvalFile), "%s", NNUE_DEFAULT_FILE);
    else snprintf(e->options.EvalFile, sizeof(e->options.EvalFile), "%s", value);
    if (uci_load_nnue(e)) emit(e, "info string NNUE evaluation enabled");
  } else if (!strcmp(lname, "use nnue")) {
    char lv[512];
    snprintf(lv, sizeof(lv), "%s", value);
    lower_inplace(lv);
    e->options.UseNNUE = !strcmp(lv, "true");
    nnue_set_enabled(e->options.UseNNUE);
    if (e->options.UseNNUE) {
      if (!nnue_loaded()) uci_load_nnue(e);
      if (nnue_loaded()) emit(e, "info string NNUE evaluation enabled");
    } else {
      emit(e, "info string NNUE evaluation disabled, using classical evaluation");
    }
  } else if (!strcmp(lname, "skill level")) {
    if (js_parse_int(value, &v)) {
      e->options.SkillLevel = v < 0 ? 0 : (v > 20 ? 20 : (int)v);
      searcher_set_strength(e->searcher, e->options.SkillLevel, e->options.LimitStrength, e->options.Elo);
    }
  } else if (!strcmp(lname, "uci_limitstrength")) {
    char lv[512];
    snprintf(lv, sizeof(lv), "%s", value);
    lower_inplace(lv);
    e->options.LimitStrength = !strcmp(lv, "true");
    searcher_set_strength(e->searcher, e->options.SkillLevel, e->options.LimitStrength, e->options.Elo);
  } else if (!strcmp(lname, "uci_elo")) {
    if (js_parse_int(value, &v)) {
      e->options.Elo = v < 1320 ? 1320 : (v > 3190 ? 3190 : (int)v);
      searcher_set_strength(e->searcher, e->options.SkillLevel, e->options.LimitStrength, e->options.Elo);
    }
  } else if (!strcmp(lname, "ownbook")) {
    char lv[512];
    snprintf(lv, sizeof(lv), "%s", value);
    lower_inplace(lv);
    e->options.OwnBook = !strcmp(lv, "true");
    book_manager_configure(&g_book, e->options.OwnBook ? 1 : 0, 0, false);
  } else if (!strcmp(lname, "book variety")) {
    if (js_parse_int(value, &v)) {
      if (v < 0) v = 0;
      if (v > 100) v = 100;
      e->options.BookVariety = (int)v;
      book_manager_configure(&g_book, -1, e->options.BookVariety / 100.0, true);
    }
  } else {
    emitf(e, "info string unknown option: %s", name);
  }
}

static void cmd_position(UciEngine *e, char **t, int n) {
  int i = 1;
  if (i < n && !strcmp(t[i], "startpos")) {
    board_parse_fen(&e->board, START_FEN);
    i++;
  } else if (i < n && !strcmp(t[i], "fen")) {
    i++;
    int start = i;
    while (i < n && strcmp(t[i], "moves") != 0) i++;
    char fen[1024];
    join_tokens(t, start, i, fen, sizeof(fen));
    if (!board_parse_fen(&e->board, fen)) {
      emit(e, "info string invalid fen");
      return;
    }
  } else {
    return;
  }
  if (i < n && !strcmp(t[i], "moves")) {
    i++;
    for (; i < n; i++) {
      int m = parse_move(&e->board, t[i]);
      if (m == 0 || !make_move(&e->board, m)) {
        emitf(e, "info string illegal move: %s", t[i]);
        break;
      }
    }
  }
}

uint64_t uci_cmd_perft(UciEngine *e, int depth) {
  double t0 = now_ms();
  static PerftDivide res;
  perft_divide(&e->board, depth, &res);
  double ms = now_ms() - t0;
  if (ms < 1) ms = 1;
  for (int i = 0; i < res.count; i++) emitf(e, "%s: %" PRIu64, res.moves[i], res.nodes[i]);
  emit(e, "");
  emitf(e, "Nodes searched: %" PRIu64, res.total);
  emitf(e, "Time: %.0f ms, NPS: %" PRIu64, ms, (uint64_t)((double)res.total * 1000.0 / ms));
  return res.total;
}

typedef struct {
  UciEngine *e;
} InfoCtx;

static void print_info(const SearchInfo *info, void *ud) {
  UciEngine *e = ((InfoCtx *)ud)->e;
  char sc[48];
  char pv[4096];
  score_to_uci(info->score, sc);
  pv_to_uci(info->pv, info->pv_len, pv, sizeof(pv));
  emitf(e, "info depth %d seldepth %d score %s nodes %" PRIu64 " nps %" PRIu64 " hashfull %d time %d pv %s",
    info->depth, info->seldepth, sc, info->nodes, info->nps, info->hashfull, info->time, pv);
}

static void run_search(UciEngine *e, const SearchLimits *limits) {
  InfoCtx ctx = {e};
  SearchInfo res;
  searcher_search(e->searcher, &e->board, limits, print_info, &ctx, &res);
  searcher_clear_stop(e->searcher);
  char out[64];
  char mv[16];
  int o = 0;
  if (res.best_move) move_to_uci(res.best_move, mv);
  else snprintf(mv, sizeof(mv), "0000");
  o = snprintf(out, sizeof(out), "bestmove %s", mv);
  if (res.ponder_move) {
    move_to_uci(res.ponder_move, mv);
    snprintf(out + o, sizeof(out) - (size_t)o, " ponder %s", mv);
  }
  emit(e, out);
}

static bool try_book(UciEngine *e, const SearchLimits *l) {
  if (!e->options.OwnBook || l->infinite) return false;
  if (l->has_depth || l->has_nodes) return false;
  if (!l->has_movetime && !l->has_wtime && !l->has_btime) return false;
  uci_ensure_book(e);
  static BookProbe hit;
  hit.found = false;
  if (!book_manager_probe(&g_book, &e->board, &hit)) return false;
  static char msg[LINE_CAP];
  int o = snprintf(msg, sizeof(msg), "info string book move %s score %d", hit.uci, hit.score);
  if (hit.opening.found) o += snprintf(msg + o, sizeof(msg) - (size_t)o, " eco %s opening %s", hit.opening.eco, hit.opening.name);
  o += snprintf(msg + o, sizeof(msg) - (size_t)o, " candidates ");
  for (int i = 0; i < hit.ncands && (size_t)o < sizeof(msg) - 32; i++)
    o += snprintf(msg + o, sizeof(msg) - (size_t)o, "%s%s:%d", i ? "," : "", hit.cands[i].uci, hit.cands[i].lines);
  emit(e, msg);
  char out[64];
  int oo = snprintf(out, sizeof(out), "bestmove %s", hit.uci);
  if (hit.line_len > 1) {
    char mv[16];
    move_to_uci(hit.line[1], mv);
    snprintf(out + oo, sizeof(out) - (size_t)oo, " ponder %s", mv);
  }
  emit(e, out);
  return true;
}

static void cmd_go(UciEngine *e, char **t, int n) {
  SearchLimits l;
  memset(&l, 0, sizeof(l));
  for (int i = 1; i < n; i++) {
    const char *k = t[i];
    long long v = 0;
    bool ok = i + 1 < n && js_parse_int(t[i + 1], &v);
    if (!strcmp(k, "perft")) {
      uci_cmd_perft(e, ok && v ? (int)v : 1);
      return;
    } else if (!strcmp(k, "depth")) { if (ok) { l.depth = (int)v; l.has_depth = true; } i++; }
    else if (!strcmp(k, "movetime")) { if (ok) { l.movetime = v; l.has_movetime = true; } i++; }
    else if (!strcmp(k, "wtime")) { if (ok) { l.wtime = v; l.has_wtime = true; } i++; }
    else if (!strcmp(k, "btime")) { if (ok) { l.btime = v; l.has_btime = true; } i++; }
    else if (!strcmp(k, "winc")) { if (ok) { l.winc = v; l.has_winc = true; } i++; }
    else if (!strcmp(k, "binc")) { if (ok) { l.binc = v; l.has_binc = true; } i++; }
    else if (!strcmp(k, "movestogo")) { if (ok) { l.movestogo = (int)v; l.has_movestogo = true; } i++; }
    else if (!strcmp(k, "nodes")) { if (ok) { l.nodes = v; l.has_nodes = true; } i++; }
    else if (!strcmp(k, "infinite")) l.infinite = true;
    else if (!strcmp(k, "ponder")) l.infinite = true;
  }
  if (!l.has_depth && !l.has_movetime && !l.has_wtime && !l.has_btime && !l.has_nodes) l.infinite = true;
  uci_ensure_nnue(e);
  if (try_book(e, &l)) return;
  run_search(e, &l);
}

uint64_t uci_cmd_bench(UciEngine *e, int depth) {
  uci_ensure_nnue(e);
  uint64_t total = 0;
  double t0 = now_ms();
  for (int i = 0; i < BENCH_FEN_COUNT; i++) {
    board_parse_fen(&e->board, BENCH_FENS[i]);
    searcher_new_game(e->searcher);
    SearchLimits l;
    memset(&l, 0, sizeof(l));
    l.depth = depth;
    l.has_depth = true;
    SearchInfo r;
    searcher_search(e->searcher, &e->board, &l, NULL, NULL, &r);
    total += r.nodes;
    char sc[48], mv[16];
    score_to_uci(r.score, sc);
    if (r.best_move) move_to_uci(r.best_move, mv);
    else snprintf(mv, sizeof(mv), "0000");
    emitf(e, "Position %d/%d: %s", i + 1, BENCH_FEN_COUNT, BENCH_FENS[i]);
    emitf(e, "  bestmove %s score %s nodes %" PRIu64 " nps %" PRIu64, mv, sc, r.nodes, r.nps);
  }
  double ms = now_ms() - t0;
  if (ms < 1) ms = 1;
  emit(e, "");
  emit(e, "===========================");
  emitf(e, "Total time (ms) : %.0f", ms);
  emitf(e, "Nodes searched  : %" PRIu64, total);
  emitf(e, "Nodes/second    : %" PRIu64, (uint64_t)((double)total * 1000.0 / ms));
  board_parse_fen(&e->board, START_FEN);
  return total;
}

static void cmd_opening(UciEngine *e) {
  uci_ensure_book(e);
  OpeningInfo info;
  memset(&info, 0, sizeof(info));
  if (!book_manager_track(&g_book, &e->board, &info)) {
    emit(e, "info string opening unknown");
    return;
  }
  emitf(e, "info string opening %s %s matched_ply %d current_ply %d exact %s",
    info.eco, info.name, info.matched_ply, e->board.hist_ply, info.exact ? "true" : "false");
}

static void cmd_book(UciEngine *e) {
  uci_ensure_book(e);
  BookStats st = book_manager_stats(&g_book);
  emitf(e, "info string book ready %s records %d/%d positions %d edges %d classified %d load_ms %d",
    st.ready ? "true" : "false", st.indexed, st.records, st.positions, st.edges, st.classified, st.load_ms);
  for (int i = 0; i < st.nfiles; i++) {
    const EcoFileStatus *f = &st.files[i];
    if (f->error[0]) emitf(e, "info string book file %s ok %s accepted %d/%d error %s", f->file, f->ok ? "true" : "false", f->accepted, f->total, f->error);
    else emitf(e, "info string book file %s ok %s accepted %d/%d", f->file, f->ok ? "true" : "false", f->accepted, f->total);
  }
  if (!st.ready) return;
  static BookProbe probe;
  probe.found = false;
  if (!book_manager_probe(&g_book, &e->board, &probe)) {
    emit(e, "info string book no move for this position");
    return;
  }
  for (int i = 0; i < probe.ncands; i++) {
    const BookCandidate *c = &probe.cands[i];
    emitf(e, "info string book candidate %s lines %d score %d prob %.3f", c->uci, c->lines, c->score, c->prob);
  }
}

static void cmd_eval(UciEngine *e) {
  uci_ensure_nnue(e);
  EvalTrace tr = evaluate_trace(&e->board);
  emitf(e, "Static evaluation (side to move): %d cp", tr.score);
  if (tr.nnue_active) emitf(e, "  NNUE raw %d  classical %d", tr.nnue_raw, tr.classical);
  else emitf(e, "  NNUE inactive  classical %d", tr.classical);
  emitf(e, "  pawn structure   mg %d eg %d (white - black)", tr.pawn_mg, tr.pawn_eg);
  emitf(e, "  mobility mg      white %d black %d", tr.white_mobility_mg, tr.black_mobility_mg);
  emitf(e, "  king attack unit white %d black %d", tr.white_king_units, tr.black_king_units);
}

void uci_handle_line(UciEngine *e, const char *line) {
  char *copy = strdup(line);
  char *tokens[MAX_TOKENS];
  int n = 0;
  char *p = copy;
  while (*p && n < MAX_TOKENS) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    tokens[n++] = p;
    while (*p && !isspace((unsigned char)*p)) p++;
    if (*p) *p++ = 0;
  }
  if (n == 0) { free(copy); return; }
  const char *cmd = tokens[0];
  long long v;

  if (!strcmp(cmd, "uci")) cmd_uci(e);
  else if (!strcmp(cmd, "isready")) { uci_ensure_nnue(e); uci_ensure_book(e); emit(e, "readyok"); }
  else if (!strcmp(cmd, "ucinewgame")) cmd_ucinewgame(e);
  else if (!strcmp(cmd, "setoption")) cmd_setoption(e, tokens, n);
  else if (!strcmp(cmd, "position")) cmd_position(e, tokens, n);
  else if (!strcmp(cmd, "go")) cmd_go(e, tokens, n);
  else if (!strcmp(cmd, "perft")) uci_cmd_perft(e, n > 1 && js_parse_int(tokens[1], &v) && v ? (int)v : 1);
  else if (!strcmp(cmd, "bench")) uci_cmd_bench(e, n > 1 && js_parse_int(tokens[1], &v) && v ? (int)v : 8);
  else if (!strcmp(cmd, "eval")) cmd_eval(e);
  else if (!strcmp(cmd, "opening")) cmd_opening(e);
  else if (!strcmp(cmd, "book")) cmd_book(e);
  else if (!strcmp(cmd, "d")) {
    char bs[4096], fen[256];
    board_to_string(&e->board, bs, sizeof(bs));
    board_export_fen(&e->board, fen, sizeof(fen));
    emitf(e, "%s Fen: %s", bs, fen);
  }
  else if (!strcmp(cmd, "stop")) searcher_request_stop(e->searcher);
  else if (!strcmp(cmd, "quit")) e->quit = true;
  else emitf(e, "info string unknown command: %s", cmd);
  free(copy);
}
