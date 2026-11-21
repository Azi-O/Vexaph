#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "interface/uci.h"
#include "platform/platform.h"

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#else
#ifndef VEXAPH_WASM_API_CHECK
#error "wasm_api.c is built only with Emscripten (or -DVEXAPH_WASM_API_CHECK for a native syntax check)"
#endif
#define EMSCRIPTEN_KEEPALIVE
#endif

#define OUT_CAP (1 << 20)

static Board g_board;
static Searcher *g_searcher;
static bool g_ready;
static volatile int g_stop;
static char g_out[OUT_CAP];

static int jesc(char *out, size_t cap, const char *s) {
  size_t o = 0;
  for (; *s && o + 7 < cap; s++) {
    unsigned char c = (unsigned char)*s;
    if (c == '"' || c == '\\') { out[o++] = '\\'; out[o++] = (char)c; }
    else if (c == '\n') { out[o++] = '\\'; out[o++] = 'n'; }
    else if (c == '\r') { out[o++] = '\\'; out[o++] = 'r'; }
    else if (c == '\t') { out[o++] = '\\'; out[o++] = 't'; }
    else if (c < 0x20) o += (size_t)snprintf(out + o, cap - o, "\\u%04x", c);
    else out[o++] = (char)c;
  }
  out[o] = 0;
  return (int)o;
}

static void fen_now(char *out, size_t cap) { board_export_fen(&g_board, out, cap); }

static const char *game_status(void) {
  static int moves[MAX_MOVES];
  int n = generate_legal_moves(&g_board, moves);
  if (n == 0) return in_check(&g_board) ? (g_board.side == WHITE ? "black_wins" : "white_wins") : "stalemate";
  if (board_is_fifty_move(&g_board)) return "draw_fifty";
  if (board_is_material_draw(&g_board)) return "draw_material";
  if (board_is_threefold(&g_board)) return "draw_repetition";
  return "ongoing";
}

static void ensure_ready(void);

EMSCRIPTEN_KEEPALIVE const char *wasm_init(int hash_mb) {
  double t0 = now_ms();
  init_engine_once();
  if (g_ready) {
    searcher_destroy(g_searcher);
    board_free(&g_board);
  }
  board_create(&g_board, START_FEN);
  g_searcher = searcher_create(hash_mb > 0 ? hash_mb : 128);
  searcher_set_stop_flag(g_searcher, &g_stop);
  book_manager_init(&g_book);
  g_ready = true;
  char fen[256];
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"init_ms\":%.0f,\"fen\":\"%s\"}", now_ms() - t0, fen);
  return g_out;
}

static void ensure_ready(void) {
  if (!g_ready) wasm_init(128);
}

EMSCRIPTEN_KEEPALIVE const char *wasm_new_game(void) {
  ensure_ready();
  searcher_new_game(g_searcher);
  board_parse_fen(&g_board, START_FEN);
  char fen[256];
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"fen\":\"%s\"}", fen);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_set_fen(const char *fen_in) {
  ensure_ready();
  bool ok = board_parse_fen(&g_board, fen_in);
  char fen[256];
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"ok\":%s,\"fen\":\"%s\"}", ok ? "true" : "false", fen);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_make_move(const char *uci_move) {
  ensure_ready();
  int m = parse_move(&g_board, uci_move);
  char fen[256];
  if (m == 0 || !make_move(&g_board, m)) {
    fen_now(fen, sizeof(fen));
    snprintf(g_out, sizeof(g_out), "{\"ok\":false,\"fen\":\"%s\",\"error\":\"illegal move\"}", fen);
    return g_out;
  }
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"fen\":\"%s\",\"in_check\":%s,\"status\":\"%s\"}", fen, in_check(&g_board) ? "true" : "false", game_status());
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_undo(void) {
  ensure_ready();
  char fen[256];
  if (g_board.hist_ply <= 0) {
    fen_now(fen, sizeof(fen));
    snprintf(g_out, sizeof(g_out), "{\"ok\":false,\"fen\":\"%s\"}", fen);
    return g_out;
  }
  unmake_move(&g_board);
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"fen\":\"%s\"}", fen);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_get_fen(void) {
  ensure_ready();
  char fen[256];
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"fen\":\"%s\",\"side\":\"%c\"}", fen, g_board.side == WHITE ? 'w' : 'b');
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_legal_moves(void) {
  ensure_ready();
  static int moves[MAX_MOVES];
  int n = generate_legal_moves(&g_board, moves);
  int o = snprintf(g_out, sizeof(g_out), "{\"moves\":[");
  for (int i = 0; i < n; i++) {
    char u[16];
    move_to_uci(moves[i], u);
    o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "%s\"%s\"", i ? "," : "", u);
  }
  snprintf(g_out + o, sizeof(g_out) - (size_t)o, "],\"status\":\"%s\"}", game_status());
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_set_hash(int mb) {
  ensure_ready();
  searcher_resize_hash(g_searcher, mb);
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"hash_mb\":%d}", g_searcher->tt.size_mb);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_stop(void) {
  ensure_ready();
  searcher_request_stop(g_searcher);
  snprintf(g_out, sizeof(g_out), "{\"ok\":true}");
  return g_out;
}

EMSCRIPTEN_KEEPALIVE volatile int *wasm_stop_flag_address(void) {
  return &g_stop;
}

static int stats_json(char *out, size_t cap, const BookStats *st) {
  int o = snprintf(out, cap, "{\"ready\":%s,\"records\":%d,\"indexed\":%d,\"illegal\":%d,\"fen_mismatch\":%d,\"positions\":%d,\"edges\":%d,\"classified\":%d,\"duplicates\":%d,\"rejected\":%d,\"load_ms\":%d,\"files\":[",
    st->ready ? "true" : "false", st->records, st->indexed, st->illegal, st->fen_mismatch, st->positions, st->edges, st->classified, st->duplicates, st->rejected, st->load_ms);
  for (int i = 0; i < st->nfiles; i++) {
    char e[512];
    jesc(e, sizeof(e), st->files[i].error);
    o += snprintf(out + o, cap - (size_t)o, "%s{\"file\":\"%s\",\"ok\":%s,\"total\":%d,\"accepted\":%d,\"rejected\":%d,\"error\":\"%s\"}",
      i ? "," : "", st->files[i].file, st->files[i].ok ? "true" : "false", st->files[i].total, st->files[i].accepted, st->files[i].rejected, e);
  }
  o += snprintf(out + o, cap - (size_t)o, "]}");
  return o;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_load_book(const char *a, const char *b, const char *c, const char *d, const char *e, const char *f) {
  ensure_ready();
  const char *texts[ECO_FILE_COUNT] = {a, b, c, d, e, f};
  book_manager_load_texts(&g_book, texts);
  BookStats st = book_manager_stats(&g_book);
  stats_json(g_out, sizeof(g_out), &st);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_load_book_dir(const char *dir) {
  ensure_ready();
  BookStats st = book_manager_load_dir(&g_book, dir);
  stats_json(g_out, sizeof(g_out), &st);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_book_stats(void) {
  BookStats st = book_manager_stats(&g_book);
  stats_json(g_out, sizeof(g_out), &st);
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_set_book(int enabled, double variety, int has_variety) {
  book_manager_configure(&g_book, enabled ? 1 : 0, variety, has_variety != 0);
  snprintf(g_out, sizeof(g_out), "{\"ok\":true,\"enabled\":%s}", g_book.enabled ? "true" : "false");
  return g_out;
}

static int opening_json(char *out, size_t cap, const OpeningInfo *info) {
  if (!info || !info->found) return snprintf(out, cap, "null");
  char name[512];
  jesc(name, sizeof(name), info->name);
  return snprintf(out, cap, "{\"eco\":\"%s\",\"name\":\"%s\",\"matched_ply\":%d,\"exact\":%s}", info->eco, name, info->matched_ply, info->exact ? "true" : "false");
}

EMSCRIPTEN_KEEPALIVE const char *wasm_opening(void) {
  ensure_ready();
  OpeningInfo info;
  memset(&info, 0, sizeof(info));
  book_manager_track(&g_book, &g_board, &info);
  opening_json(g_out, sizeof(g_out), &info);
  return g_out;
}

static void mate_fields(int score, bool *has_mate, int *mate) {
  *has_mate = false;
  *mate = 0;
  if (score >= MATE_IN_MAX) { *has_mate = true; *mate = ((MATE_SCORE - score) + 1) >> 1; }
  else if (score <= -MATE_IN_MAX) { *has_mate = true; *mate = -((MATE_SCORE + score) >> 1); }
}

static int pv_list_json(char *out, size_t cap, const int *pv, int len, char *flat, size_t flat_cap) {
  int o = snprintf(out, cap, "[");
  size_t fo = 0;
  flat[0] = 0;
  for (int i = 0; i < len; i++) {
    char u[16];
    move_to_uci(pv[i], u);
    o += snprintf(out + o, cap - (size_t)o, "%s\"%s\"", i ? "," : "", u);
    int w = snprintf(flat + fo, flat_cap - fo, "%s%s", i ? " " : "", u);
    if (w > 0 && (size_t)w < flat_cap - fo) fo += (size_t)w;
  }
  o += snprintf(out + o, cap - (size_t)o, "]");
  return o;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_search(int max_depth, int movetime_ms, int use_book) {
  ensure_ready();
  int side = g_board.side;
  char fen[256];
  if (use_book && movetime_ms > 0) {
    static BookProbe hit;
    hit.found = false;
    if (book_manager_probe(&g_book, &g_board, &hit)) {
      int score = hit.score;
      int o = snprintf(g_out, sizeof(g_out), "{\"depth\":0,\"seldepth\":0,\"score\":%d,\"score_white\":%d,\"mate\":null,\"mate_white\":null,\"score_uci\":\"cp %d\",\"nodes\":0,\"nps\":0,\"time\":1,\"hashfull\":%d,",
        score, side == WHITE ? score : -score, score, tt_hashfull(&g_searcher->tt));
      char flat[1024], list[2048];
      pv_list_json(list, sizeof(list), hit.line, hit.line_len, flat, sizeof(flat));
      o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"pv\":\"%s\",\"pv_list\":%s,\"bestmove\":\"%s\",", flat, list, hit.uci);
      if (hit.line_len > 1) {
        char u[16];
        move_to_uci(hit.line[1], u);
        o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"ponder\":\"%s\",", u);
      } else {
        o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"ponder\":null,");
      }
      o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"book\":true,\"opening\":");
      o += opening_json(g_out + o, sizeof(g_out) - (size_t)o, &hit.opening);
      o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, ",\"candidates\":[");
      for (int i = 0; i < hit.ncands; i++) {
        const BookCandidate *c = &hit.cands[i];
        o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "%s{\"uci\":\"%s\",\"lines\":%d,\"quality\":%.6f,\"reach\":%d,\"ends\":%d,\"score\":%d,\"weight\":%.9g,\"prob\":%.9g}",
          i ? "," : "", c->uci, c->lines, c->quality, c->reach, c->ends, c->score, c->weight, c->prob);
      }
      fen_now(fen, sizeof(fen));
      snprintf(g_out + o, sizeof(g_out) - (size_t)o, "],\"fen\":\"%s\",\"side\":\"%c\"}", fen, side == WHITE ? 'w' : 'b');
      return g_out;
    }
  }

  SearchLimits l;
  memset(&l, 0, sizeof(l));
  if (max_depth > 0) { l.depth = max_depth; l.has_depth = true; }
  if (movetime_ms > 0) { l.movetime = movetime_ms; l.has_movetime = true; }
  if (!l.has_depth && !l.has_movetime) { l.depth = 64; l.has_depth = true; }

  double t0 = now_ms();
  g_stop = 0;
  SearchInfo r;
  searcher_search(g_searcher, &g_board, &l, NULL, NULL, &r);
  g_stop = 0;
  double ms = now_ms() - t0;
  long long time_ms = (long long)(ms + 0.5);
  if (time_ms < 1) time_ms = 1;
  uint64_t nps = (uint64_t)(((double)r.nodes * 1000.0) / (double)time_ms);

  bool has_mate;
  int mate;
  mate_fields(r.score, &has_mate, &mate);
  char sc[48], flat[4096], list[8192], best[16], ponder[16];
  score_to_uci(r.score, sc);
  pv_list_json(list, sizeof(list), r.pv, r.pv_len, flat, sizeof(flat));
  if (r.best_move) move_to_uci(r.best_move, best); else snprintf(best, sizeof(best), "0000");
  int o = snprintf(g_out, sizeof(g_out), "{\"depth\":%d,\"seldepth\":%d,\"score\":%d,\"score_white\":%d,", r.depth, r.seldepth, r.score, side == WHITE ? r.score : -r.score);
  if (has_mate) o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"mate\":%d,\"mate_white\":%d,", mate, side == WHITE ? mate : -mate);
  else o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"mate\":null,\"mate_white\":null,");
  o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"score_uci\":\"%s\",\"nodes\":%" PRIu64 ",\"nps\":%" PRIu64 ",\"time\":%lld,\"hashfull\":%d,\"pv\":\"%s\",\"pv_list\":%s,\"bestmove\":\"%s\",",
    sc, r.nodes, nps, time_ms, r.hashfull, flat, list, best);
  if (r.ponder_move) {
    move_to_uci(r.ponder_move, ponder);
    o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"ponder\":\"%s\",", ponder);
  } else {
    o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"ponder\":null,");
  }
  fen_now(fen, sizeof(fen));
  OpeningInfo info;
  memset(&info, 0, sizeof(info));
  book_manager_track(&g_book, &g_board, &info);
  o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "\"fen\":\"%s\",\"side\":\"%c\",\"book\":false,\"opening\":", fen, side == WHITE ? 'w' : 'b');
  o += opening_json(g_out + o, sizeof(g_out) - (size_t)o, &info);
  snprintf(g_out + o, sizeof(g_out) - (size_t)o, "}");
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_perft(int depth) {
  ensure_ready();
  double t0 = now_ms();
  static PerftDivide res;
  perft_divide(&g_board, depth < 1 ? 1 : depth, &res);
  double ms = now_ms() - t0;
  if (ms < 1) ms = 1;
  int o = snprintf(g_out, sizeof(g_out), "{\"depth\":%d,\"nodes\":%" PRIu64 ",\"time\":%.0f,\"nps\":%" PRIu64 ",\"divide\":[", depth, res.total, ms, (uint64_t)((double)res.total * 1000.0 / ms));
  for (int i = 0; i < res.count; i++)
    o += snprintf(g_out + o, sizeof(g_out) - (size_t)o, "%s{\"move\":\"%s\",\"nodes\":%" PRIu64 "}", i ? "," : "", res.moves[i], res.nodes[i]);
  snprintf(g_out + o, sizeof(g_out) - (size_t)o, "]}");
  return g_out;
}

EMSCRIPTEN_KEEPALIVE const char *wasm_eval(void) {
  ensure_ready();
  EvalTrace tr = evaluate_trace(&g_board);
  char fen[256];
  fen_now(fen, sizeof(fen));
  snprintf(g_out, sizeof(g_out), "{\"score\":%d,\"score_white\":%d,\"side\":\"%c\",\"pawn_mg\":%d,\"pawn_eg\":%d,\"fen\":\"%s\"}",
    tr.score, g_board.side == WHITE ? tr.score : -tr.score, g_board.side == WHITE ? 'w' : 'b', tr.pawn_mg, tr.pawn_eg, fen);
  return g_out;
}
