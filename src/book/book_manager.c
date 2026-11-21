#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "book/book.h"
#include "platform/platform.h"

BookManager g_book;

static void reset_loaded(BookManager *m) {
  if (m->has_book) opening_book_free(&m->book);
  if (m->has_classifier) name_map_free(&m->names);
  memset(&m->book, 0, sizeof(m->book));
  memset(&m->names, 0, sizeof(m->names));
  memset(&m->report, 0, sizeof(m->report));
  memset(m->files, 0, sizeof(m->files));
  m->has_book = false;
  m->has_classifier = false;
  m->nfiles = 0;
  m->rejected = 0;
  m->duplicates = 0;
  m->load_ms = 0;
}

void book_manager_init(BookManager *m) {
  memset(m, 0, sizeof(*m));
  m->enabled = true;
  m->options = BOOK_DEFAULTS;
  m->rng_state = 0x9E3779B97F4A7C15ULL ^ (uint64_t)(now_ms() * 1000.0);
  if (!m->rng_state) m->rng_state = 1;
}

void book_manager_free(BookManager *m) {
  reset_loaded(m);
}

bool book_manager_is_ready(const BookManager *m) {
  return m->has_book && m->book.npos > 0;
}

void book_manager_load_texts(BookManager *m, const char *texts[ECO_FILE_COUNT]) {
  if (book_manager_is_ready(m)) return;
  reset_loaded(m);
  double t0 = now_ms();
  EcoCollection col;
  eco_collect(texts, &col);
  memcpy(m->files, col.statuses, sizeof(m->files));
  m->nfiles = ECO_FILE_COUNT;
  m->rejected = col.rejected;
  m->duplicates = col.duplicates;
  if (col.count == 0) {
    m->load_ms = (int)(now_ms() - t0 + 0.5);
    eco_collection_free(&col);
    return;
  }
  build_opening_book(col.records, col.count, &m->book, &m->names, &m->report);
  m->has_book = true;
  m->has_classifier = true;
  eco_collection_free(&col);
  m->load_ms = (int)(now_ms() - t0 + 0.5);
}

BookStats book_manager_stats(const BookManager *m) {
  BookStats s;
  memset(&s, 0, sizeof(s));
  s.ready = book_manager_is_ready(m);
  s.load_ms = m->load_ms;
  memcpy(s.files, m->files, sizeof(s.files));
  s.nfiles = m->nfiles;
  if (m->has_book) {
    s.records = m->report.records;
    s.indexed = m->report.indexed;
    s.illegal = m->report.illegal_count;
    s.fen_mismatch = m->report.fen_mismatch_count;
    s.positions = m->report.positions;
    s.edges = m->report.edges;
    s.classified = m->report.classified;
  }
  s.duplicates = m->duplicates;
  s.rejected = m->rejected;
  return s;
}

static bool is_dir(const char *p) {
  struct stat st;
  return p && *p && stat(p, &st) == 0 && (st.st_mode & S_IFMT) == S_IFDIR;
}

static bool has_eco(const char *dir) {
  char path[1500];
  snprintf(path, sizeof(path), "%s/%s", dir, ECO_FILES[0]);
  struct stat st;
  return stat(path, &st) == 0;
}

const char *book_find_eco_dir(const char *hint, const char *argv0) {
  static char found[1400];
  char cand[1200];
  if (hint && *hint && is_dir(hint) && has_eco(hint)) {
    snprintf(found, sizeof(found), "%s", hint);
    return found;
  }
  const char *env = getenv("VEXAPH_ECO_DIR");
  if (env && *env && is_dir(env) && has_eco(env)) {
    snprintf(found, sizeof(found), "%s", env);
    return found;
  }
  if (argv0 && *argv0) {
    snprintf(cand, sizeof(cand), "%s", argv0);
    char *slash = strrchr(cand, '/');
    char *bslash = strrchr(cand, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
      *slash = 0;
      const char *rel[] = {"../data/eco", "data/eco", "../../data/eco", "eco"};
      for (int i = 0; i < 4; i++) {
        char p[1300];
        snprintf(p, sizeof(p), "%s/%s", cand, rel[i]);
        if (is_dir(p) && has_eco(p)) {
          snprintf(found, sizeof(found), "%s", p);
          return found;
        }
      }
    }
  }
  const char *cwd_rel[] = {"data/eco", "../data/eco", "eco", "src/book/data", "../src/book/data"};
  for (int i = 0; i < 5; i++) {
    if (is_dir(cwd_rel[i]) && has_eco(cwd_rel[i])) {
      snprintf(found, sizeof(found), "%s", cwd_rel[i]);
      return found;
    }
  }
  return NULL;
}

BookStats book_manager_load_dir(BookManager *m, const char *dir) {
  if (book_manager_is_ready(m)) return book_manager_stats(m);
  char *bufs[ECO_FILE_COUNT];
  const char *texts[ECO_FILE_COUNT];
  BookStats pre;
  memset(&pre, 0, sizeof(pre));
  for (int i = 0; i < ECO_FILE_COUNT; i++) {
    bufs[i] = NULL;
    texts[i] = NULL;
    if (!dir) {
      snprintf(pre.missing_names[pre.missing++], 40, "%s", ECO_FILES[i]);
      continue;
    }
    char path[1500];
    snprintf(path, sizeof(path), "%s/%s", dir, ECO_FILES[i]);
    size_t len = 0;
    bufs[i] = eco_read_file(path, &len);
    if (!bufs[i]) {
      snprintf(pre.missing_names[pre.missing++], 40, "%s", ECO_FILES[i]);
      continue;
    }
    texts[i] = bufs[i];
  }
  book_manager_load_texts(m, texts);
  for (int i = 0; i < ECO_FILE_COUNT; i++) free(bufs[i]);
  BookStats s = book_manager_stats(m);
  s.missing = pre.missing;
  memcpy(s.missing_names, pre.missing_names, sizeof(s.missing_names));
  return s;
}

void book_manager_configure(BookManager *m, int enabled, double variety, bool has_variety) {
  if (enabled >= 0) m->enabled = enabled != 0;
  if (has_variety && variety == variety && variety - variety == 0) m->options.variety = variety;
}

bool book_manager_usable(const BookManager *m) {
  return m->enabled && book_manager_is_ready(m);
}

static double rng_next(void *ud) {
  BookManager *m = ud;
  uint64_t x = m->rng_state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  m->rng_state = x;
  return (double)(x >> 11) * (1.0 / 9007199254740992.0);
}

bool book_manager_track(const BookManager *m, const Board *b, OpeningInfo *out) {
  if (!m->has_classifier) return false;
  return classifier_track(&m->names, b, out);
}

bool book_manager_probe(BookManager *m, Board *b, BookProbe *out) {
  if (!book_manager_usable(m)) return false;
  static BookSelection sel;
  sel.found = false;
  if (!select_book_move(b, &m->book, &m->options, rng_next, m, &sel)) return false;
  memset(out, 0, sizeof(*out));
  out->found = true;
  out->move = sel.move;
  snprintf(out->uci, sizeof(out->uci), "%s", sel.uci);
  out->score = sel.score;
  out->ncands = sel.ncands;
  memcpy(out->cands, sel.cands, sizeof(BookCandidate) * (size_t)sel.ncands);
  out->line_len = book_line(b, &m->book, sel.move, 4, &m->options, out->line);
  out->opening.found = false;
  if (m->has_classifier && make_move(b, sel.move)) {
    classifier_track(&m->names, b, &out->opening);
    unmake_move(b);
  }
  return true;
}
