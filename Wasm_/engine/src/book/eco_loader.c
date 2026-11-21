#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "book/book.h"

const char *const ECO_FILES[ECO_FILE_COUNT] = {
  "ecoA.json", "ecoB.json", "ecoC.json", "ecoD.json", "ecoE.json", "eco_interpolated.json"
};

char *eco_read_file(const char *path, size_t *len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long n = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t r = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[r] = 0;
  if (len) *len = r;
  return buf;
}

typedef struct {
  const char *p;
  const char *end;
  char err[160];
} Json;

static void jerr(Json *j, const char *msg) {
  if (!j->err[0]) snprintf(j->err, sizeof(j->err), "%s", msg);
}

static void skip_ws(Json *j) {
  while (j->p < j->end && (*j->p == ' ' || *j->p == '\t' || *j->p == '\n' || *j->p == '\r')) j->p++;
}

static int hexv(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

static int put_utf8(char *o, unsigned cp) {
  if (cp < 0x80) { o[0] = (char)cp; return 1; }
  if (cp < 0x800) { o[0] = (char)(0xC0 | (cp >> 6)); o[1] = (char)(0x80 | (cp & 63)); return 2; }
  if (cp < 0x10000) { o[0] = (char)(0xE0 | (cp >> 12)); o[1] = (char)(0x80 | ((cp >> 6) & 63)); o[2] = (char)(0x80 | (cp & 63)); return 3; }
  o[0] = (char)(0xF0 | (cp >> 18)); o[1] = (char)(0x80 | ((cp >> 12) & 63));
  o[2] = (char)(0x80 | ((cp >> 6) & 63)); o[3] = (char)(0x80 | (cp & 63));
  return 4;
}

static unsigned parse_hex4(Json *j) {
  unsigned v = 0;
  for (int i = 0; i < 4; i++) {
    if (j->p >= j->end) { jerr(j, "bad unicode escape"); return 0; }
    int h = hexv(*j->p++);
    if (h < 0) { jerr(j, "bad unicode escape"); return 0; }
    v = v * 16 + (unsigned)h;
  }
  return v;
}

static char *parse_string(Json *j) {
  if (j->p >= j->end || *j->p != '"') { jerr(j, "expected string"); return NULL; }
  j->p++;
  size_t cap = 64, n = 0;
  char *out = malloc(cap);
  while (j->p < j->end && *j->p != '"') {
    if (n + 8 >= cap) { cap *= 2; out = realloc(out, cap); }
    char c = *j->p++;
    if (c == '\\') {
      if (j->p >= j->end) { jerr(j, "bad escape"); break; }
      char e = *j->p++;
      switch (e) {
        case 'n': out[n++] = '\n'; break;
        case 't': out[n++] = '\t'; break;
        case 'r': out[n++] = '\r'; break;
        case 'b': out[n++] = '\b'; break;
        case 'f': out[n++] = '\f'; break;
        case '/': out[n++] = '/'; break;
        case '\\': out[n++] = '\\'; break;
        case '"': out[n++] = '"'; break;
        case 'u': {
          unsigned cp = parse_hex4(j);
          if (cp >= 0xD800 && cp < 0xDC00 && j->p + 1 < j->end && j->p[0] == '\\' && j->p[1] == 'u') {
            j->p += 2;
            unsigned lo = parse_hex4(j);
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          n += (size_t)put_utf8(out + n, cp);
          break;
        }
        default: jerr(j, "bad escape"); break;
      }
    } else {
      out[n++] = c;
    }
    if (j->err[0]) break;
  }
  if (j->p >= j->end || *j->p != '"') { jerr(j, "unterminated string"); free(out); return NULL; }
  j->p++;
  out[n] = 0;
  return out;
}

static void skip_value(Json *j);

static void skip_container(Json *j, char open, char close) {
  j->p++;
  skip_ws(j);
  if (j->p < j->end && *j->p == close) { j->p++; return; }
  for (;;) {
    skip_ws(j);
    if (open == '{') {
      char *k = parse_string(j);
      if (!k) return;
      free(k);
      skip_ws(j);
      if (j->p >= j->end || *j->p != ':') { jerr(j, "expected ':'"); return; }
      j->p++;
    }
    skip_value(j);
    if (j->err[0]) return;
    skip_ws(j);
    if (j->p < j->end && *j->p == ',') { j->p++; continue; }
    if (j->p < j->end && *j->p == close) { j->p++; return; }
    jerr(j, "expected ',' or closing bracket");
    return;
  }
}

static void skip_value(Json *j) {
  skip_ws(j);
  if (j->p >= j->end) { jerr(j, "unexpected end"); return; }
  char c = *j->p;
  if (c == '"') { char *s = parse_string(j); free(s); return; }
  if (c == '{') { skip_container(j, '{', '}'); return; }
  if (c == '[') { skip_container(j, '[', ']'); return; }
  if (!strncmp(j->p, "true", 4)) { j->p += 4; return; }
  if (!strncmp(j->p, "false", 5)) { j->p += 5; return; }
  if (!strncmp(j->p, "null", 4)) { j->p += 4; return; }
  if (c == '-' || isdigit((unsigned char)c)) {
    while (j->p < j->end && (isdigit((unsigned char)*j->p) || strchr("+-.eE", *j->p))) j->p++;
    return;
  }
  jerr(j, "unexpected token");
}

typedef struct {
  bool is_object;
  char *src, *eco, *name, *moves, *scid;
  bool src_str, eco_str, name_str, moves_str, scid_str;
  bool root_true;
} RawRecord;

static void raw_free(RawRecord *r) {
  free(r->src); free(r->eco); free(r->name); free(r->moves); free(r->scid);
  memset(r, 0, sizeof(*r));
}

static void set_field(char **dst, bool *is_str, Json *j) {
  skip_ws(j);
  free(*dst);
  *dst = NULL;
  *is_str = false;
  if (j->p < j->end && *j->p == '"') {
    *dst = parse_string(j);
    *is_str = *dst != NULL;
  } else {
    skip_value(j);
  }
}

static void parse_record(Json *j, RawRecord *r) {
  memset(r, 0, sizeof(*r));
  skip_ws(j);
  if (j->p >= j->end || *j->p != '{') {
    skip_value(j);
    return;
  }
  r->is_object = true;
  j->p++;
  skip_ws(j);
  if (j->p < j->end && *j->p == '}') { j->p++; return; }
  for (;;) {
    skip_ws(j);
    char *k = parse_string(j);
    if (!k) return;
    skip_ws(j);
    if (j->p >= j->end || *j->p != ':') { jerr(j, "expected ':'"); free(k); return; }
    j->p++;
    if (!strcmp(k, "src")) set_field(&r->src, &r->src_str, j);
    else if (!strcmp(k, "eco")) set_field(&r->eco, &r->eco_str, j);
    else if (!strcmp(k, "name")) set_field(&r->name, &r->name_str, j);
    else if (!strcmp(k, "moves")) set_field(&r->moves, &r->moves_str, j);
    else if (!strcmp(k, "scid")) set_field(&r->scid, &r->scid_str, j);
    else if (!strcmp(k, "isEcoRoot")) {
      skip_ws(j);
      r->root_true = (j->p + 4 <= j->end && !strncmp(j->p, "true", 4));
      skip_value(j);
    } else skip_value(j);
    free(k);
    if (j->err[0]) return;
    skip_ws(j);
    if (j->p < j->end && *j->p == ',') { j->p++; continue; }
    if (j->p < j->end && *j->p == '}') { j->p++; return; }
    jerr(j, "expected ',' or '}'");
    return;
  }
}

static char *trim_dup(const char *s) {
  while (*s && isspace((unsigned char)*s)) s++;
  size_t n = strlen(s);
  while (n > 0 && isspace((unsigned char)s[n - 1])) n--;
  char *o = malloc(n + 1);
  memcpy(o, s, n);
  o[n] = 0;
  return o;
}

static int count_fields(const char *s) {
  int n = 0;
  bool in = false;
  for (; *s; s++) {
    if (isspace((unsigned char)*s)) in = false;
    else if (!in) { in = true; n++; }
  }
  return n;
}

static bool valid_record(const char *fen, const RawRecord *r) {
  if (count_fields(fen) < 4) return false;
  if (!r->is_object) return false;
  if (!r->eco_str) return false;
  char *eco = trim_dup(r->eco);
  bool ok = strlen(eco) == 3 && eco[0] >= 'A' && eco[0] <= 'E' && isdigit((unsigned char)eco[1]) && isdigit((unsigned char)eco[2]);
  free(eco);
  if (!ok) return false;
  if (!r->name_str) return false;
  char *nm = trim_dup(r->name);
  ok = nm[0] != 0;
  free(nm);
  if (!ok) return false;
  if (!r->moves_str) return false;
  char *mv = trim_dup(r->moves);
  ok = mv[0] != 0;
  free(mv);
  return ok;
}

static char *fen_first4(const char *fen) {
  size_t cap = strlen(fen) + 1;
  char *o = malloc(cap);
  size_t n = 0;
  int fields = 0;
  const char *p = fen;
  while (*p && fields < 4) {
    while (*p && isspace((unsigned char)*p)) p++;
    if (!*p) break;
    if (fields > 0) o[n++] = ' ';
    while (*p && !isspace((unsigned char)*p)) o[n++] = *p++;
    fields++;
  }
  o[n] = 0;
  return o;
}

typedef struct {
  char **keys;
  size_t cap;
  size_t n;
} StrSet;

static uint64_t fnv(const char *s) {
  uint64_t h = 1469598103934665603ULL;
  for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
  return h;
}

static void set_init(StrSet *s) {
  s->cap = 1 << 16;
  s->n = 0;
  s->keys = calloc(s->cap, sizeof(char *));
}

static bool set_insert(StrSet *s, char *key);

static void set_grow(StrSet *s) {
  StrSet n;
  n.cap = s->cap * 2;
  n.n = 0;
  n.keys = calloc(n.cap, sizeof(char *));
  for (size_t i = 0; i < s->cap; i++) if (s->keys[i]) set_insert(&n, s->keys[i]);
  free(s->keys);
  *s = n;
}

static bool set_insert(StrSet *s, char *key) {
  if ((s->n + 1) * 2 > s->cap) set_grow(s);
  size_t i = (size_t)(fnv(key) & (s->cap - 1));
  while (s->keys[i]) {
    if (!strcmp(s->keys[i], key)) return false;
    i = (i + 1) & (s->cap - 1);
  }
  s->keys[i] = key;
  s->n++;
  return true;
}

static void set_free(StrSet *s) {
  for (size_t i = 0; i < s->cap; i++) free(s->keys[i]);
  free(s->keys);
}

static void add_record(EcoCollection *c, EcoRecord *r) {
  if (c->count == c->cap) {
    c->cap = c->cap ? c->cap * 2 : 4096;
    c->records = realloc(c->records, sizeof(EcoRecord) * (size_t)c->cap);
  }
  c->records[c->count++] = *r;
}

static void copy_cap(char *dst, size_t cap, const char *src) {
  snprintf(dst, cap, "%s", src);
}

void eco_collect(const char *texts[ECO_FILE_COUNT], EcoCollection *out) {
  memset(out, 0, sizeof(*out));
  StrSet seen;
  set_init(&seen);
  for (int fi = 0; fi < ECO_FILE_COUNT; fi++) {
    EcoFileStatus *st = &out->statuses[fi];
    memset(st, 0, sizeof(*st));
    copy_cap(st->file, sizeof(st->file), ECO_FILES[fi]);
    if (!texts[fi]) {
      copy_cap(st->error, sizeof(st->error), "file missing");
      continue;
    }
    Json j;
    memset(&j, 0, sizeof(j));
    j.p = texts[fi];
    j.end = texts[fi] + strlen(texts[fi]);
    skip_ws(&j);
    if (j.p >= j.end || *j.p != '{') {
      copy_cap(st->error, sizeof(st->error), j.p >= j.end ? "invalid json: unexpected end" : "unexpected json shape");
      continue;
    }

    size_t first_new = (size_t)out->count;
    int dups_before = out->duplicates;
    (void)dups_before;
    int total = 0, accepted = 0, rejected = 0;
    j.p++;
    skip_ws(&j);
    bool done = false;
    if (j.p < j.end && *j.p == '}') { j.p++; done = true; }
    while (!done) {
      skip_ws(&j);
      char *fen = parse_string(&j);
      if (!fen) break;
      skip_ws(&j);
      if (j.p >= j.end || *j.p != ':') { jerr(&j, "expected ':'"); free(fen); break; }
      j.p++;
      RawRecord rr;
      parse_record(&j, &rr);
      if (j.err[0]) { raw_free(&rr); free(fen); break; }
      total++;
      if (!valid_record(fen, &rr)) {
        rejected++;
        out->rejected++;
      } else {
        accepted++;
        EcoRecord rec;
        memset(&rec, 0, sizeof(rec));
        rec.fen = fen;
        fen = NULL;
        char *eco = trim_dup(rr.eco);
        copy_cap(rec.eco, sizeof(rec.eco), eco);
        free(eco);
        rec.name = trim_dup(rr.name);
        rec.moves = trim_dup(rr.moves);
        copy_cap(rec.src, sizeof(rec.src), rr.src_str ? rr.src : "unknown");
        rec.interpolated = rr.src_str && !strcmp(rr.src, "interpolated");
        rec.root = rr.root_true;
        copy_cap(rec.scid, sizeof(rec.scid), rr.scid_str ? rr.scid : "");
        rec.file = fi;

        char *f4 = fen_first4(rec.fen);
        size_t kl = strlen(f4) + 1 + strlen(rec.moves) + 1;
        char *key = malloc(kl);
        snprintf(key, kl, "%s|%s", f4, rec.moves);
        free(f4);
        if (!set_insert(&seen, key)) {
          free(key);
          out->duplicates++;
          free(rec.fen); free(rec.name); free(rec.moves);
        } else {
          add_record(out, &rec);
        }
      }
      free(fen);
      raw_free(&rr);
      skip_ws(&j);
      if (j.p < j.end && *j.p == ',') { j.p++; continue; }
      if (j.p < j.end && *j.p == '}') { j.p++; break; }
      jerr(&j, "expected ',' or '}'");
      break;
    }
    if (j.err[0]) {
      snprintf(st->error, sizeof(st->error), "invalid json: %s", j.err);
      for (size_t k = first_new; k < (size_t)out->count; k++) {
        free(out->records[k].fen); free(out->records[k].name); free(out->records[k].moves);
      }
      out->count = (int)first_new;
      continue;
    }
    st->ok = true;
    st->total = total;
    st->accepted = accepted;
    st->rejected = rejected;
  }
  set_free(&seen);
}

void eco_collection_free(EcoCollection *c) {
  for (int i = 0; i < c->count; i++) {
    free(c->records[i].fen);
    free(c->records[i].name);
    free(c->records[i].moves);
  }
  free(c->records);
  memset(c, 0, sizeof(*c));
}
