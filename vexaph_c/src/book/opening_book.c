#include <stdlib.h>
#include <string.h>
#include "book/book.h"

static inline uint32_t hash_slot(U64 h, int cap) {
  h ^= h >> 33;
  h *= 0xff51afd7ed558ccdULL;
  h ^= h >> 33;
  return (uint32_t)(h & (U64)(cap - 1));
}

void opening_book_init(OpeningBook *b) {
  memset(b, 0, sizeof(*b));
  b->index_cap = 1 << 15;
  b->index = malloc(sizeof(int) * (size_t)b->index_cap);
  memset(b->index, 0xff, sizeof(int) * (size_t)b->index_cap);
}

void opening_book_free(OpeningBook *b) {
  for (int i = 0; i < b->npos; i++) free(b->pos[i].edges);
  free(b->pos);
  free(b->index);
  memset(b, 0, sizeof(*b));
}

static int book_find(const OpeningBook *b, U64 hash) {
  if (!b->index) return -1;
  uint32_t i = hash_slot(hash, b->index_cap);
  while (b->index[i] >= 0) {
    if (b->pos[b->index[i]].hash == hash) return b->index[i];
    i = (i + 1) & (uint32_t)(b->index_cap - 1);
  }
  return -1;
}

const BookPosition *opening_book_edges_at(const OpeningBook *b, U64 hash) {
  int i = book_find(b, hash);
  return i < 0 ? NULL : &b->pos[i];
}

static void book_reindex(OpeningBook *b) {
  b->index_cap *= 2;
  free(b->index);
  b->index = malloc(sizeof(int) * (size_t)b->index_cap);
  memset(b->index, 0xff, sizeof(int) * (size_t)b->index_cap);
  for (int p = 0; p < b->npos; p++) {
    uint32_t i = hash_slot(b->pos[p].hash, b->index_cap);
    while (b->index[i] >= 0) i = (i + 1) & (uint32_t)(b->index_cap - 1);
    b->index[i] = p;
  }
}

static void add_edge(OpeningBook *b, U64 hash, const char *uci, double quality, int reach, bool terminal) {
  int pi = book_find(b, hash);
  if (pi < 0) {
    if (b->npos == b->cap) {
      b->cap = b->cap ? b->cap * 2 : 1024;
      b->pos = realloc(b->pos, sizeof(BookPosition) * (size_t)b->cap);
    }
    if ((b->npos + 1) * 2 > b->index_cap) book_reindex(b);
    pi = b->npos++;
    memset(&b->pos[pi], 0, sizeof(BookPosition));
    b->pos[pi].hash = hash;
    uint32_t i = hash_slot(hash, b->index_cap);
    while (b->index[i] >= 0) i = (i + 1) & (uint32_t)(b->index_cap - 1);
    b->index[i] = pi;
  }
  BookPosition *p = &b->pos[pi];
  BookEdge *edge = NULL;
  for (int i = 0; i < p->n; i++) {
    if (!strcmp(p->edges[i].uci, uci)) { edge = &p->edges[i]; break; }
  }
  if (!edge) {
    if (p->n == p->cap) {
      p->cap = p->cap ? p->cap * 2 : 4;
      p->edges = realloc(p->edges, sizeof(BookEdge) * (size_t)p->cap);
    }
    edge = &p->edges[p->n++];
    memset(edge, 0, sizeof(*edge));
    snprintf(edge->uci, sizeof(edge->uci), "%s", uci);
    b->edge_count++;
  }
  edge->lines++;
  edge->quality += quality;
  if (reach > edge->reach) edge->reach = reach;
  if (terminal) edge->ends++;
}

void name_map_init(NameMap *m) {
  memset(m, 0, sizeof(*m));
  m->index_cap = 1 << 15;
  m->index = malloc(sizeof(int) * (size_t)m->index_cap);
  memset(m->index, 0xff, sizeof(int) * (size_t)m->index_cap);
}

void name_map_free(NameMap *m) {
  for (int i = 0; i < m->n; i++) { free(m->items[i].name); free(m->items[i].moves); }
  free(m->items);
  free(m->index);
  memset(m, 0, sizeof(*m));
}

static int name_find(const NameMap *m, U64 hash) {
  if (!m->index) return -1;
  uint32_t i = hash_slot(hash, m->index_cap);
  while (m->index[i] >= 0) {
    if (m->items[m->index[i]].hash == hash) return m->index[i];
    i = (i + 1) & (uint32_t)(m->index_cap - 1);
  }
  return -1;
}

const NameInfo *name_map_get(const NameMap *m, U64 hash) {
  int i = name_find(m, hash);
  return i < 0 ? NULL : &m->items[i];
}

static void name_reindex(NameMap *m) {
  m->index_cap *= 2;
  free(m->index);
  m->index = malloc(sizeof(int) * (size_t)m->index_cap);
  memset(m->index, 0xff, sizeof(int) * (size_t)m->index_cap);
  for (int p = 0; p < m->n; p++) {
    uint32_t i = hash_slot(m->items[p].hash, m->index_cap);
    while (m->index[i] >= 0) i = (i + 1) & (uint32_t)(m->index_cap - 1);
    m->index[i] = p;
  }
}

static NameInfo *name_slot(NameMap *m, U64 hash, bool *created) {
  int i = name_find(m, hash);
  if (i >= 0) { *created = false; return &m->items[i]; }
  if (m->n == m->cap) {
    m->cap = m->cap ? m->cap * 2 : 1024;
    m->items = realloc(m->items, sizeof(NameInfo) * (size_t)m->cap);
  }
  if ((m->n + 1) * 2 > m->index_cap) name_reindex(m);
  int pi = m->n++;
  memset(&m->items[pi], 0, sizeof(NameInfo));
  m->items[pi].hash = hash;
  uint32_t s = hash_slot(hash, m->index_cap);
  while (m->index[s] >= 0) s = (s + 1) & (uint32_t)(m->index_cap - 1);
  m->index[s] = pi;
  *created = true;
  return &m->items[pi];
}

double record_quality(const EcoRecord *r) {
  double base;
  if (!strcmp(r->src, "eco_tsv")) base = 3;
  else if (!strcmp(r->src, "eco_json")) base = 3;
  else if (!strcmp(r->src, "scid")) base = 2.5;
  else if (!strcmp(r->src, "eco_js")) base = 2;
  else if (!strcmp(r->src, "interpolated")) base = 0.5;
  else base = 1.5;
  return base + (r->root ? 2 : 0);
}

static int name_priority(const EcoRecord *r) {
  return (r->interpolated ? 0 : 4) + (r->root ? 2 : 0) + (!strcmp(r->src, "eco_tsv") ? 1 : 0);
}

typedef struct {
  const EcoRecord *rec;
  char **t;
  int n;
  char *arena;
} Entry;

static int compare_entries(const Entry *a, const Entry *b) {
  int n = a->n < b->n ? a->n : b->n;
  for (int i = 0; i < n; i++) {
    int c = strcmp(a->t[i], b->t[i]);
    if (c < 0) return -1;
    if (c > 0) return 1;
  }
  return a->n - b->n;
}

static void merge_sort(Entry *a, Entry *tmp, int n) {
  if (n < 2) return;
  int m = n / 2;
  merge_sort(a, tmp, m);
  merge_sort(a + m, tmp, n - m);
  int i = 0, j = m, k = 0;
  while (i < m && j < n) {
    if (compare_entries(&a[j], &a[i]) < 0) tmp[k++] = a[j++];
    else tmp[k++] = a[i++];
  }
  while (i < m) tmp[k++] = a[i++];
  while (j < n) tmp[k++] = a[j++];
  memcpy(a, tmp, sizeof(Entry) * (size_t)n);
}

static void fen_core(const char *fen, char *out, size_t cap) {
  size_t o = 0;
  int fields = 0;
  const char *p = fen;
  while (*p && fields < 3) {
    while (*p == ' ' || *p == '\t') p++;
    if (!*p) break;
    if (fields > 0 && o + 1 < cap) out[o++] = ' ';
    while (*p && *p != ' ' && *p != '\t' && o + 1 < cap) out[o++] = *p++;
    fields++;
  }
  out[o] = 0;
}

void build_opening_book(const EcoRecord *records, int n, OpeningBook *book, NameMap *names, BookReport *report) {
  opening_book_init(book);
  name_map_init(names);
  memset(report, 0, sizeof(*report));
  report->records = n;

  Board board;
  board_create(&board, START_FEN);
  Entry *entries = malloc(sizeof(Entry) * (size_t)(n ? n : 1));
  Entry *tmp = malloc(sizeof(Entry) * (size_t)(n ? n : 1));
  for (int i = 0; i < n; i++) {
    entries[i].rec = &records[i];
    entries[i].n = tokenize_moves(records[i].moves, &entries[i].t, &entries[i].arena);
  }
  merge_sort(entries, tmp, n);

  int path_cap = 256;
  const char **path_tokens = malloc(sizeof(char *) * (size_t)path_cap);
  U64 *path_hash = malloc(sizeof(U64) * (size_t)path_cap);
  char (*path_uci)[8] = malloc(8 * (size_t)path_cap);
  int path_len = 0;

  for (int ei = 0; ei < n; ei++) {
    Entry *e = &entries[ei];
    int cnt = e->n;
    int common = 0;
    while (common < path_len && common < cnt && !strcmp(path_tokens[common], e->t[common])) common++;
    while (path_len > common) {
      unmake_move(&board);
      path_len--;
    }

    bool ok = cnt > 0;
    for (int i = common; i < cnt; i++) {
      int mv = parse_san(&board, e->t[i]);
      if (mv == 0) {
        ok = false;
        report->illegal_count++;
        break;
      }
      if (path_len == path_cap) {
        path_cap *= 2;
        path_tokens = realloc(path_tokens, sizeof(char *) * (size_t)path_cap);
        path_hash = realloc(path_hash, sizeof(U64) * (size_t)path_cap);
        path_uci = realloc(path_uci, 8 * (size_t)path_cap);
      }
      path_hash[path_len] = board.hash_key;
      move_to_uci(mv, path_uci[path_len]);
      path_tokens[path_len] = e->t[i];
      path_len++;
      make_move(&board, mv);
    }
    if (!ok || path_len != cnt) continue;

    char fa[160], fb[160], got[160];
    board_export_fen(&board, got, sizeof(got));
    fen_core(got, fa, sizeof(fa));
    fen_core(e->rec->fen, fb, sizeof(fb));
    if (strcmp(fa, fb) != 0) report->fen_mismatch_count++;

    double q = record_quality(e->rec);
    for (int i = 0; i < cnt; i++) add_edge(book, path_hash[i], path_uci[i], q, cnt - i, i == cnt - 1);
    report->indexed++;

    U64 key = board.hash_key;
    bool created;
    NameInfo *ni = name_slot(names, key, &created);
    int pr = name_priority(e->rec);
    if (created || pr > ni->priority) {
      free(ni->name);
      free(ni->moves);
      snprintf(ni->eco, sizeof(ni->eco), "%s", e->rec->eco);
      ni->name = strdup(e->rec->name);
      ni->moves = strdup(e->rec->moves);
      snprintf(ni->src, sizeof(ni->src), "%s", e->rec->src);
      ni->ply = cnt;
      ni->priority = pr;
    }
  }

  while (path_len > 0) {
    unmake_move(&board);
    path_len--;
  }

  report->positions = book->npos;
  report->edges = book->edge_count;
  report->classified = names->n;

  for (int i = 0; i < n; i++) { free(entries[i].t); free(entries[i].arena); }
  free(entries); free(tmp); free(path_tokens); free(path_hash); free(path_uci);
  board_free(&board);
}

bool classifier_classify(const NameMap *names, const Board *b, OpeningInfo *out) {
  const NameInfo *ni = name_map_get(names, b->hash_key);
  if (!ni) { out->found = false; return false; }
  snprintf(out->eco, sizeof(out->eco), "%s", ni->eco);
  out->name = ni->name;
  out->moves = ni->moves;
  out->ply = ni->ply;
  out->matched_ply = b->hist_ply;
  out->exact = true;
  out->found = true;
  return true;
}

bool classifier_track(const NameMap *names, const Board *b, OpeningInfo *out) {
  int total = b->hist_ply;
  for (int ply = total; ply >= 0; ply--) {
    U64 hash = ply == total ? b->hash_key : b->undo_stack[ply].hash_key;
    const NameInfo *ni = name_map_get(names, hash);
    if (ni) {
      snprintf(out->eco, sizeof(out->eco), "%s", ni->eco);
      out->name = ni->name;
      out->moves = ni->moves;
      out->ply = ni->ply;
      out->matched_ply = ply;
      out->exact = ply == total;
      out->found = true;
      return true;
    }
  }
  out->found = false;
  return false;
}
