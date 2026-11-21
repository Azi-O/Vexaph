#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>

#define IMPORT(n) __attribute__((import_module("env"), import_name(n)))

IMPORT("host_now") double host_now(void);
IMPORT("host_file_open") int host_file_open(const char *path);
IMPORT("host_file_close") void host_file_close(int h);
IMPORT("host_file_size") unsigned host_file_size(int h);
IMPORT("host_file_read") unsigned host_file_read(int h, void *dst, unsigned off, unsigned len);
IMPORT("host_file_stat") int host_file_stat(const char *path);
IMPORT("host_write") void host_write(int fd, const void *p, unsigned n);
IMPORT("host_abort") void host_abort(void);

int errno;

/* ---------------------------------------------------------------- string */

void *memcpy(void *d, const void *s, size_t n) { return __builtin_memcpy(d, s, n); }
void *memset(void *d, int c, size_t n) { return __builtin_memset(d, c, n); }
void *memmove(void *d, const void *s, size_t n) { return __builtin_memmove(d, s, n); }

int memcmp(const void *a, const void *b, size_t n) {
  const unsigned char *x = a, *y = b;
  for (; n; n--, x++, y++) if (*x != *y) return *x - *y;
  return 0;
}

void *memchr(const void *p, int c, size_t n) {
  const unsigned char *s = p;
  for (; n; n--, s++) if (*s == (unsigned char)c) return (void *)s;
  return NULL;
}

size_t strlen(const char *s) {
  const char *p = s;
  while (*p) p++;
  return (size_t)(p - s);
}

int strcmp(const char *a, const char *b) {
  while (*a && *a == *b) { a++; b++; }
  return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
  for (; n; n--, a++, b++) {
    if (*a != *b) return (unsigned char)*a - (unsigned char)*b;
    if (!*a) return 0;
  }
  return 0;
}

char *strcpy(char *d, const char *s) {
  char *r = d;
  while ((*d++ = *s++)) {}
  return r;
}

char *strncpy(char *d, const char *s, size_t n) {
  size_t i = 0;
  for (; i < n && s[i]; i++) d[i] = s[i];
  for (; i < n; i++) d[i] = 0;
  return d;
}

char *strcat(char *d, const char *s) {
  strcpy(d + strlen(d), s);
  return d;
}

char *strchr(const char *s, int c) {
  for (;; s++) {
    if (*s == (char)c) return (char *)s;
    if (!*s) return NULL;
  }
}

char *strrchr(const char *s, int c) {
  const char *r = NULL;
  for (;; s++) {
    if (*s == (char)c) r = s;
    if (!*s) return (char *)r;
  }
}

char *strstr(const char *h, const char *n) {
  size_t nl = strlen(n);
  if (!nl) return (char *)h;
  for (; *h; h++) if (*h == *n && !strncmp(h, n, nl)) return (char *)h;
  return NULL;
}

char *strdup(const char *s) {
  size_t n = strlen(s) + 1;
  char *p = malloc(n);
  if (p) memcpy(p, s, n);
  return p;
}

char *strtok_r(char *s, const char *delim, char **save) {
  if (!s) s = *save;
  if (!s) return NULL;
  while (*s && strchr(delim, *s)) s++;
  if (!*s) { *save = NULL; return NULL; }
  char *tok = s;
  while (*s && !strchr(delim, *s)) s++;
  if (*s) { *s++ = 0; *save = s; }
  else *save = NULL;
  return tok;
}

char *strerror(int e) {
  switch (e) {
    case ENOENT: return "No such file or directory";
    case ENOMEM: return "Out of memory";
    case EINVAL: return "Invalid argument";
    case ERANGE: return "Result too large";
    default: return "Error";
  }
}

/* ----------------------------------------------------------------- ctype */

int isspace(int c) { return c == ' ' || (c >= 9 && c <= 13); }
int isdigit(int c) { return c >= '0' && c <= '9'; }
int isupper(int c) { return c >= 'A' && c <= 'Z'; }
int islower(int c) { return c >= 'a' && c <= 'z'; }
int isalpha(int c) { return isupper(c) || islower(c); }
int isalnum(int c) { return isalpha(c) || isdigit(c); }
int isxdigit(int c) { return isdigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
int isprint(int c) { return c >= 32 && c < 127; }
int ispunct(int c) { return isprint(c) && c != ' ' && !isalnum(c); }
int tolower(int c) { return isupper(c) ? c + 32 : c; }
int toupper(int c) { return islower(c) ? c - 32 : c; }

/* ---------------------------------------------------------------- malloc */

typedef struct Chunk {
  unsigned size;
  unsigned prev;
  struct Chunk *fd, *bk;
} Chunk;

#define F_INUSE 1u
#define F_PINUSE 2u
#define CSIZE(c) ((c)->size & ~15u)
#define MINCHUNK 32u
#define NBINS 96
#define PAGE 65536u

extern unsigned char __heap_base[];

static Chunk *bins[NBINS];
static Chunk *top;
static int heap_ready;

static unsigned bin_index(unsigned sz) {
  if (sz < 1024) return sz >> 4;
  unsigned lg = 31 - (unsigned)__builtin_clz(sz);
  unsigned idx = 64 + (lg - 10) * 2 + ((sz >> (lg - 1)) & 1);
  return idx < NBINS ? idx : NBINS - 1;
}

static void bin_insert(Chunk *c) {
  unsigned i = bin_index(CSIZE(c));
  c->fd = bins[i];
  c->bk = NULL;
  if (bins[i]) bins[i]->bk = c;
  bins[i] = c;
}

static void bin_remove(Chunk *c) {
  if (c->bk) c->bk->fd = c->fd;
  else bins[bin_index(CSIZE(c))] = c->fd;
  if (c->fd) c->fd->bk = c->bk;
}

static void heap_init(void) {
  uintptr_t base = ((uintptr_t)__heap_base + 15u) & ~(uintptr_t)15u;
  base += 8;
  uintptr_t end = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
  end = (end - 8) & ~(uintptr_t)15u;
  end += 8;
  top = (Chunk *)base;
  top->size = (unsigned)(end - base) | F_PINUSE;
  heap_ready = 1;
}

static int heap_grow(unsigned need) {
  unsigned pages = (need + PAGE - 1) / PAGE;
  if (pages < 16) pages = 16;
  uintptr_t old_end = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
  if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) {
    pages = (need + PAGE - 1) / PAGE;
    if (__builtin_wasm_memory_grow(0, pages) == (size_t)-1) return 0;
  }
  uintptr_t top_end = (uintptr_t)top + CSIZE(top);
  uintptr_t new_end = (uintptr_t)__builtin_wasm_memory_size(0) * PAGE;
  new_end = ((new_end - 8) & ~(uintptr_t)15u) + 8;
  (void)old_end;
  top->size = (unsigned)(new_end - (uintptr_t)top) | (top->size & 15u);
  (void)top_end;
  return 1;
}

static Chunk *next_chunk(Chunk *c) { return (Chunk *)((char *)c + CSIZE(c)); }

static void place(Chunk *c, unsigned sz) {
  unsigned have = CSIZE(c);
  unsigned rem = have - sz;
  unsigned pin = c->size & F_PINUSE;
  if (rem >= MINCHUNK) {
    c->size = sz | F_INUSE | pin;
    Chunk *r = (Chunk *)((char *)c + sz);
    r->size = rem | F_PINUSE;
    Chunk *n = next_chunk(r);
    n->prev = rem;
    bin_insert(r);
  } else {
    c->size = have | F_INUSE | pin;
    Chunk *n = next_chunk(c);
    n->size |= F_PINUSE;
  }
}

void *malloc(size_t n) {
  if (!heap_ready) heap_init();
  if (n == 0) n = 1;
  if (n > 0x7ffffff0u) return NULL;
  unsigned sz = (((unsigned)n + 8u) + 15u) & ~15u;
  if (sz < MINCHUNK) sz = MINCHUNK;
  for (unsigned b = bin_index(sz); b < NBINS; b++) {
    Chunk *best = NULL;
    for (Chunk *c = bins[b]; c; c = c->fd) {
      if (CSIZE(c) >= sz && (!best || CSIZE(c) < CSIZE(best))) {
        best = c;
        if (CSIZE(c) == sz || b < 64) break;
      }
    }
    if (best) {
      bin_remove(best);
      place(best, sz);
      return (char *)best + 8;
    }
  }
  if (CSIZE(top) < sz + MINCHUNK) {
    if (!heap_grow(sz + MINCHUNK - CSIZE(top))) return NULL;
    if (CSIZE(top) < sz + MINCHUNK) return NULL;
  }
  Chunk *c = top;
  unsigned rem = CSIZE(top) - sz;
  c->size = sz | F_INUSE | (c->size & F_PINUSE);
  top = (Chunk *)((char *)c + sz);
  top->size = rem | F_PINUSE;
  return (char *)c + 8;
}

void free(void *p) {
  if (!p) return;
  Chunk *c = (Chunk *)((char *)p - 8);
  unsigned sz = CSIZE(c);
  if (!(c->size & F_PINUSE)) {
    Chunk *pv = (Chunk *)((char *)c - c->prev);
    bin_remove(pv);
    sz += CSIZE(pv);
    c = pv;
  }
  Chunk *nx = (Chunk *)((char *)c + sz);
  if (nx == top) {
    unsigned tsz = CSIZE(top);
    top = c;
    top->size = (sz + tsz) | F_PINUSE;
    return;
  }
  if (!(nx->size & F_INUSE)) {
    bin_remove(nx);
    sz += CSIZE(nx);
    nx = (Chunk *)((char *)c + sz);
  }
  c->size = sz | F_PINUSE;
  nx->prev = sz;
  nx->size &= ~F_PINUSE;
  bin_insert(c);
}

void *calloc(size_t a, size_t b) {
  if (b && a > 0x7ffffff0u / b) return NULL;
  size_t n = a * b;
  void *p = malloc(n);
  if (p) memset(p, 0, n);
  return p;
}

void *realloc(void *p, size_t n) {
  if (!p) return malloc(n);
  if (n == 0) { free(p); return NULL; }
  if (n > 0x7ffffff0u) return NULL;
  Chunk *c = (Chunk *)((char *)p - 8);
  unsigned sz = (((unsigned)n + 8u) + 15u) & ~15u;
  if (sz < MINCHUNK) sz = MINCHUNK;
  unsigned cur = CSIZE(c);
  if (cur >= sz) return p;
  Chunk *nx = next_chunk(c);
  if (nx == top) {
    unsigned want = sz - cur;
    if (CSIZE(top) < want + MINCHUNK) {
      if (!heap_grow(want + MINCHUNK - CSIZE(top))) goto slow;
      nx = next_chunk(c);
    }
    unsigned rem = CSIZE(top) - want;
    c->size = sz | F_INUSE | (c->size & F_PINUSE);
    top = (Chunk *)((char *)c + sz);
    top->size = rem | F_PINUSE;
    return p;
  }
  if (!(nx->size & F_INUSE) && cur + CSIZE(nx) >= sz) {
    bin_remove(nx);
    unsigned total = cur + CSIZE(nx);
    unsigned rem = total - sz;
    unsigned pin = c->size & F_PINUSE;
    if (rem >= MINCHUNK) {
      c->size = sz | F_INUSE | pin;
      Chunk *r = (Chunk *)((char *)c + sz);
      r->size = rem | F_PINUSE;
      Chunk *after = next_chunk(r);
      after->prev = rem;
      bin_insert(r);
    } else {
      c->size = total | F_INUSE | pin;
      next_chunk(c)->size |= F_PINUSE;
    }
    return p;
  }
slow:;
  void *q = malloc(n);
  if (!q) return NULL;
  memcpy(q, p, cur - 8);
  free(p);
  return q;
}

/* ------------------------------------------------------------------ misc */

void abort(void) {
  host_abort();
  __builtin_trap();
}

void exit(int c) {
  (void)c;
  abort();
}

void _exit(int c) { exit(c); }

char *getenv(const char *n) {
  (void)n;
  return NULL;
}

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }
long long llabs(long long x) { return x < 0 ? -x : x; }

static uint64_t rand_state = 88172645463325252ull;
int rand(void) {
  rand_state ^= rand_state << 13;
  rand_state ^= rand_state >> 7;
  rand_state ^= rand_state << 17;
  return (int)((rand_state >> 33) & 0x7fffffff);
}
void srand(unsigned s) { rand_state = 88172645463325252ull ^ ((uint64_t)s << 7) ^ s; }

int clock_gettime(clockid_t id, struct timespec *ts) {
  (void)id;
  double ms = host_now();
  ts->tv_sec = (time_t)(ms / 1000.0);
  ts->tv_nsec = (long)((ms - (double)ts->tv_sec * 1000.0) * 1e6);
  return 0;
}

time_t time(time_t *t) {
  time_t v = (time_t)(host_now() / 1000.0);
  if (t) *t = v;
  return v;
}

static void qsort_swap(char *a, char *b, size_t w) {
  while (w--) { char t = *a; *a++ = *b; *b++ = t; }
}

static void qsort_rec(char *base, size_t n, size_t w, int (*cmp)(const void *, const void *)) {
  while (n > 12) {
    char *mid = base + (n / 2) * w, *last = base + (n - 1) * w;
    if (cmp(base, mid) > 0) qsort_swap(base, mid, w);
    if (cmp(mid, last) > 0) qsort_swap(mid, last, w);
    if (cmp(base, mid) > 0) qsort_swap(base, mid, w);
    qsort_swap(mid, last - w, w);
    char *pivot = last - w;
    char *i = base, *j = last - w;
    for (;;) {
      while (cmp(i += w, pivot) < 0) {}
      while (cmp(j -= w, pivot) > 0) {}
      if (i >= j) break;
      qsort_swap(i, j, w);
    }
    qsort_swap(i, last - w, w);
    size_t left = (size_t)(i - base) / w;
    size_t right = n - left - 1;
    if (left < right) { qsort_rec(base, left, w, cmp); base = i + w; n = right; }
    else { qsort_rec(i + w, right, w, cmp); n = left; }
  }
  for (size_t a = 1; a < n; a++)
    for (size_t b = a; b > 0 && cmp(base + (b - 1) * w, base + b * w) > 0; b--)
      qsort_swap(base + (b - 1) * w, base + b * w, w);
}

void qsort(void *base, size_t n, size_t w, int (*cmp)(const void *, const void *)) {
  if (n > 1 && w) qsort_rec(base, n, w, cmp);
}

static int digit_val(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'z') return c - 'a' + 10;
  if (c >= 'A' && c <= 'Z') return c - 'A' + 10;
  return 99;
}

unsigned long long strtoull(const char *s, char **end, int base) {
  const char *p = s;
  while (isspace((unsigned char)*p)) p++;
  int neg = 0;
  if (*p == '+' || *p == '-') neg = *p++ == '-';
  if ((base == 0 || base == 16) && p[0] == '0' && (p[1] == 'x' || p[1] == 'X') && digit_val((unsigned char)p[2]) < 16) { p += 2; base = 16; }
  else if (base == 0) base = p[0] == '0' ? 8 : 10;
  unsigned long long v = 0;
  int any = 0, ovf = 0;
  for (; digit_val((unsigned char)*p) < base; p++) {
    unsigned d = (unsigned)digit_val((unsigned char)*p);
    if (v > (~0ull - d) / (unsigned)base) ovf = 1;
    else v = v * (unsigned)base + d;
    any = 1;
  }
  if (end) *end = (char *)(any ? p : s);
  if (ovf) { errno = ERANGE; return ~0ull; }
  return neg ? (unsigned long long)(-(long long)v) : v;
}

long long strtoll(const char *s, char **end, int base) {
  const char *p = s;
  while (isspace((unsigned char)*p)) p++;
  int neg = *p == '-';
  unsigned long long v = strtoull(s, end, base);
  if (neg) {
    if ((long long)v > 0 && v > 0x8000000000000000ull) { errno = ERANGE; return (-0x7fffffffffffffffll - 1); }
    return (long long)v;
  }
  if (v > 0x7fffffffffffffffull) { errno = ERANGE; return 0x7fffffffffffffffll; }
  return (long long)v;
}

long strtol(const char *s, char **end, int base) {
  long long v = strtoll(s, end, base);
  if (v > 0x7fffffffll) { errno = ERANGE; return 0x7fffffffl; }
  if (v < -0x7fffffffll - 1) { errno = ERANGE; return -0x7fffffffl - 1; }
  return (long)v;
}

unsigned long strtoul(const char *s, char **end, int base) {
  unsigned long long v = strtoull(s, end, base);
  if (v > 0xffffffffull) { errno = ERANGE; return 0xfffffffful; }
  return (unsigned long)v;
}

double strtod(const char *s, char **end) {
  const char *p = s;
  while (isspace((unsigned char)*p)) p++;
  int neg = 0;
  if (*p == '+' || *p == '-') neg = *p++ == '-';
  double v = 0;
  int any = 0;
  for (; isdigit((unsigned char)*p); p++) { v = v * 10 + (*p - '0'); any = 1; }
  if (*p == '.') {
    p++;
    double sc = 0.1;
    for (; isdigit((unsigned char)*p); p++) { v += (*p - '0') * sc; sc /= 10; any = 1; }
  }
  if (!any) { if (end) *end = (char *)s; return 0; }
  if (*p == 'e' || *p == 'E') {
    const char *q = p + 1;
    int en = 0;
    if (*q == '+' || *q == '-') en = *q++ == '-';
    if (isdigit((unsigned char)*q)) {
      int e = 0;
      for (; isdigit((unsigned char)*q); q++) e = e * 10 + (*q - '0');
      v *= pow(10.0, en ? -e : e);
      p = q;
    }
  }
  if (end) *end = (char *)p;
  return neg ? -v : v;
}

int atoi(const char *s) { return (int)strtol(s, NULL, 10); }
long atol(const char *s) { return strtol(s, NULL, 10); }

/* ------------------------------------------------------------------ math */

double sqrt(double x) { return __builtin_sqrt(x); }
double floor(double x) { return __builtin_floor(x); }
double ceil(double x) { return __builtin_ceil(x); }
double trunc(double x) { return __builtin_trunc(x); }
double fabs(double x) { return __builtin_fabs(x); }
double fmin(double a, double b) { return __builtin_fmin(a, b); }
double fmax(double a, double b) { return __builtin_fmax(a, b); }
double round(double x) { return x < 0 ? -__builtin_floor(-x + 0.5) : __builtin_floor(x + 0.5); }

double fmod(double x, double y) {
  if (y == 0 || __builtin_isinf(x) || __builtin_isnan(x) || __builtin_isnan(y)) return NAN;
  if (__builtin_isinf(y)) return x;
  double ax = fabs(x), ay = fabs(y);
  while (ax >= ay) {
    double t = ay;
    while (t * 2 <= ax) t *= 2;
    ax -= t;
  }
  return x < 0 ? -ax : ax;
}

static const double LN2_HI = 6.93147180369123816490e-01;
static const double LN2_LO = 1.90821492927058770002e-10;

double log(double x) {
  if (__builtin_isnan(x)) return x;
  if (x < 0) return NAN;
  if (x == 0) return -HUGE_VAL;
  if (__builtin_isinf(x)) return x;
  union { double d; uint64_t u; } v = {x};
  int k = 0;
  if (((v.u >> 52) & 0x7ff) == 0) { v.d *= 18014398509481984.0; k -= 54; }
  k += (int)((v.u >> 52) & 0x7ff) - 1023;
  v.u = (v.u & 0x000fffffffffffffull) | 0x3ff0000000000000ull;
  double m = v.d;
  if (m > 1.4142135623730951) { m *= 0.5; k++; }
  double s = (m - 1.0) / (m + 1.0);
  double s2 = s * s;
  double t = 0.0;
  for (int n = 41; n >= 3; n -= 2) t = t * s2 + 1.0 / (double)n;
  double r = 2.0 * s + 2.0 * s * s2 * t;
  return (double)k * LN2_HI + (r + (double)k * LN2_LO);
}

double log2(double x) { return log(x) / M_LN2; }

static double scale2(double x, int k) {
  while (k > 1000) { x *= 0x1p1000; k -= 1000; }
  while (k < -1000) { x *= 0x1p-1000; k += 1000; }
  union { double d; uint64_t u; } f;
  f.u = (uint64_t)(k + 1023) << 52;
  return x * f.d;
}

double exp(double x) {
  if (__builtin_isnan(x)) return x;
  if (x > 709.782712893384) return HUGE_VAL;
  if (x < -745.2) return 0.0;
  double kf = __builtin_floor(x / M_LN2 + 0.5);
  int k = (int)kf;
  double r = (x - kf * LN2_HI) - kf * LN2_LO;
  double t = 0.0;
  for (int n = 18; n >= 1; n--) t = (t + 1.0) * r / (double)n;
  return scale2(1.0 + t, k);
}

double pow(double x, double y) {
  if (y == 0.0) return 1.0;
  if (x == 1.0) return 1.0;
  if (__builtin_isnan(x) || __builtin_isnan(y)) return NAN;
  double yi = __builtin_floor(y);
  int y_int = yi == y;
  if (y_int && fabs(y) <= 64.0) {
    long long n = (long long)fabs(y);
    double b = x, r = 1.0;
    while (n) { if (n & 1) r *= b; b *= b; n >>= 1; }
    return y < 0 ? 1.0 / r : r;
  }
  if (x == 0.0) return y < 0 ? HUGE_VAL : 0.0;
  if (x < 0) {
    if (!y_int) return NAN;
    double r = exp(y * log(-x));
    return fmod(fabs(y), 2.0) == 1.0 ? -r : r;
  }
  return exp(y * log(x));
}

/* ----------------------------------------------------------------- stdio */

struct VX_FILE {
  int fd;
  int handle;
  unsigned size;
  unsigned pos;
};

static FILE std_in = {0, 0, 0, 0}, std_out = {1, 0, 0, 0}, std_err = {2, 0, 0, 0};
FILE *stdin = &std_in, *stdout = &std_out, *stderr = &std_err;

int stat(const char *path, struct stat *st) {
  int k = host_file_stat(path);
  if (!k) { errno = ENOENT; return -1; }
  st->st_mode = k == 2 ? S_IFDIR : S_IFREG;
  st->st_size = 0;
  if (k == 1) {
    int h = host_file_open(path);
    if (h) { st->st_size = host_file_size(h); host_file_close(h); }
  }
  return 0;
}

FILE *fopen(const char *path, const char *mode) {
  if (!mode || mode[0] != 'r') { errno = EINVAL; return NULL; }
  int h = host_file_open(path);
  if (!h) { errno = ENOENT; return NULL; }
  FILE *f = malloc(sizeof(FILE));
  if (!f) { host_file_close(h); errno = ENOMEM; return NULL; }
  f->fd = -1;
  f->handle = h;
  f->size = host_file_size(h);
  f->pos = 0;
  return f;
}

int fclose(FILE *f) {
  if (!f || f->fd >= 0) return 0;
  host_file_close(f->handle);
  free(f);
  return 0;
}

size_t fread(void *dst, size_t sz, size_t n, FILE *f) {
  if (!f || f->fd >= 0 || !sz || !n) return 0;
  size_t want = sz * n;
  if (f->pos >= f->size) return 0;
  if (want > f->size - f->pos) want = f->size - f->pos;
  unsigned got = host_file_read(f->handle, dst, f->pos, (unsigned)want);
  f->pos += got;
  return got / sz;
}

int fgetc(FILE *f) {
  unsigned char c;
  return fread(&c, 1, 1, f) == 1 ? c : EOF;
}

int fseek(FILE *f, long off, int whence) {
  if (!f || f->fd >= 0) return -1;
  long long base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? (long long)f->pos : (long long)f->size;
  long long np = base + off;
  if (np < 0) return -1;
  f->pos = (unsigned)np;
  return 0;
}

long ftell(FILE *f) { return f && f->fd < 0 ? (long)f->pos : -1; }
int fflush(FILE *f) { (void)f; return 0; }

size_t fwrite(const void *p, size_t sz, size_t n, FILE *f) {
  if (!f || f->fd < 0) return 0;
  host_write(f->fd, p, (unsigned)(sz * n));
  return n;
}

int fputs(const char *s, FILE *f) {
  if (!f || f->fd < 0) return EOF;
  host_write(f->fd, s, (unsigned)strlen(s));
  return 1;
}

int puts(const char *s) {
  host_write(1, s, (unsigned)strlen(s));
  host_write(1, "\n", 1);
  return 1;
}

int putchar(int c) {
  char ch = (char)c;
  host_write(1, &ch, 1);
  return c;
}

typedef struct {
  char *buf;
  size_t cap;
  size_t n;
} Out;

static void out_ch(Out *o, char c) {
  if (o->n + 1 < o->cap) o->buf[o->n] = c;
  o->n++;
}

static void out_pad(Out *o, char c, int n) {
  while (n-- > 0) out_ch(o, c);
}

static void out_str(Out *o, const char *s, size_t n) {
  while (n--) out_ch(o, *s++);
}

#define DIGBUF 1400
#define LIMB_BASE 1000000000u

typedef struct {
  char d[DIGBUF];
  int len;
  int frac;
} Dec;

static void dec_exact(double v, Dec *r) {
  union { double d; uint64_t u; } x = {v};
  int e = (int)((x.u >> 52) & 0x7ff);
  uint64_t m = x.u & 0x000fffffffffffffull;
  if (e == 0) e = 1;
  else m |= 0x0010000000000000ull;
  int e2 = e - 1075;
  static uint32_t limb[200];
  int nl = 0;
  uint64_t t = m;
  while (t) { limb[nl++] = (uint32_t)(t % LIMB_BASE); t /= LIMB_BASE; }
  if (!nl) limb[nl++] = 0;
  int frac = 0;
  int steps = e2 >= 0 ? e2 : -e2;
  uint32_t mul = e2 >= 0 ? 2u : 5u;
  while (steps > 0) {
    int chunk = e2 >= 0 ? (steps >= 29 ? 29 : steps) : (steps >= 12 ? 12 : steps);
    uint64_t f = 1;
    for (int i = 0; i < chunk; i++) f *= mul;
    uint64_t carry = 0;
    for (int i = 0; i < nl; i++) {
      uint64_t cur = (uint64_t)limb[i] * f + carry;
      limb[i] = (uint32_t)(cur % LIMB_BASE);
      carry = cur / LIMB_BASE;
    }
    while (carry) { limb[nl++] = (uint32_t)(carry % LIMB_BASE); carry /= LIMB_BASE; }
    steps -= chunk;
    if (e2 < 0) frac += chunk;
  }
  char tmp[DIGBUF];
  int n = 0;
  for (int i = nl - 1; i >= 0; i--) {
    char b[10];
    uint32_t w = limb[i];
    for (int k = 8; k >= 0; k--) { b[k] = (char)('0' + w % 10); w /= 10; }
    for (int k = 0; k < 9; k++) {
      if (n == 0 && i == nl - 1 && b[k] == '0') continue;
      tmp[n++] = b[k];
    }
  }
  if (n == 0) tmp[n++] = '0';
  if (n < frac + 1) {
    int pad = frac + 1 - n;
    memmove(tmp + pad, tmp, (size_t)n);
    memset(tmp, '0', (size_t)pad);
    n += pad;
  }
  memcpy(r->d, tmp, (size_t)n);
  r->len = n;
  r->frac = frac;
}

/* round decimal string to keep `keep` leading digits (keep may be <= 0 or >= len); returns carry-out (digit string grew by one leading '1') */
static int dec_round(char *d, int *len, int keep) {
  if (keep >= *len) return 0;
  if (keep < 0) { *len = 0; return 0; }
  int up = 0;
  if (d[keep] > '5') up = 1;
  else if (d[keep] == '5') {
    int rest = 0;
    for (int i = keep + 1; i < *len; i++) if (d[i] != '0') { rest = 1; break; }
    if (rest) up = 1;
    else up = keep > 0 ? ((d[keep - 1] - '0') & 1) : 0;
  }
  *len = keep;
  if (!up) return 0;
  int i = keep - 1;
  while (i >= 0) {
    if (d[i] == '9') { d[i] = '0'; i--; }
    else { d[i]++; return 0; }
  }
  memmove(d + 1, d, (size_t)*len);
  d[0] = '1';
  (*len)++;
  return 1;
}

static void fmt_float(Out *o, double v, char conv, int prec, int left, int plus, int space, int alt, int zero, int width) {
  char lower = (char)(conv | 0x20);
  int upper = conv != lower;
  char sign = 0;
  union { double d; uint64_t u; } x = {v};
  if (x.u >> 63) { sign = '-'; v = -v; }
  else if (plus) sign = '+';
  else if (space) sign = ' ';
  static char body[DIGBUF + 64];
  int bl = 0;
  if (__builtin_isnan(v) || __builtin_isinf(v)) {
    const char *t = __builtin_isnan(v) ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf");
    memcpy(body, t, 3);
    bl = 3;
    zero = 0;
  } else {
    if (prec < 0) prec = 6;
    static Dec dec;
    dec_exact(v, &dec);
    char *d = dec.d;
    int len = dec.len, frac = dec.frac;
    char *p = body;
    if (lower == 'f') {
      int keep = len - frac + prec;
      if (keep < len) dec_round(d, &len, keep);
      if (len <= 0) { d[0] = '0'; len = 1; }
      else if (frac < prec) { memset(d + len, '0', (size_t)(prec - frac)); len += prec - frac; }
      if (len < prec + 1) {
        int pad = prec + 1 - len;
        memmove(d + pad, d, (size_t)len);
        memset(d, '0', (size_t)pad);
        len += pad;
      }
      int ip = len - prec;
      int first = 0;
      while (first < ip - 1 && d[first] == '0') first++;
      for (int i = first; i < ip; i++) *p++ = d[i];
      if (prec > 0 || alt) *p++ = '.';
      for (int i = ip; i < len; i++) *p++ = d[i];
    } else {
      int first = 0;
      while (first < len - 1 && d[first] == '0') first++;
      int sig = len - first;
      memmove(d, d + first, (size_t)sig);
      len = sig;
      int is_zero = len == 1 && d[0] == '0';
      int X = is_zero ? 0 : (len - 1 - frac);
      int P = lower == 'g' ? (prec == 0 ? 1 : prec) : prec + 1;
      if (!is_zero && len > P) { if (dec_round(d, &len, P)) X++; if (len > P) len = P; }
      while (len < P) d[len++] = '0';
      int use_e = lower == 'e' || X < -4 || X >= P;
      if (use_e) {
        int fd = P - 1;
        if (lower == 'g' && !alt) while (fd > 0 && d[fd] == '0') fd--;
        *p++ = d[0];
        if (fd > 0 || alt) *p++ = '.';
        for (int i = 1; i <= fd; i++) *p++ = d[i];
        *p++ = upper ? 'E' : 'e';
        int ex = X;
        *p++ = ex < 0 ? '-' : '+';
        if (ex < 0) ex = -ex;
        if (ex >= 100) *p++ = (char)('0' + ex / 100);
        *p++ = (char)('0' + (ex / 10) % 10);
        *p++ = (char)('0' + ex % 10);
      } else {
        int fd = P - 1 - X;
        if (X >= 0) { for (int i = 0; i <= X; i++) *p++ = d[i]; }
        else *p++ = '0';
        int fstart = X >= 0 ? X + 1 : 0;
        char fr[DIGBUF];
        int fl = 0;
        if (X < 0) for (int i = 0; i < -X - 1; i++) fr[fl++] = '0';
        for (int i = fstart; i < P; i++) fr[fl++] = d[i];
        if (!alt) while (fl > 0 && fr[fl - 1] == '0') fl--;
        (void)fd;
        if (fl > 0 || alt) *p++ = '.';
        for (int i = 0; i < fl; i++) *p++ = fr[i];
      }
    }
    bl = (int)(p - body);
  }
  int total = bl + (sign ? 1 : 0);
  if (!left && !zero) out_pad(o, ' ', width - total);
  if (sign) out_ch(o, sign);
  if (!left && zero) out_pad(o, '0', width - total);
  out_str(o, body, (size_t)bl);
  if (left) out_pad(o, ' ', width - total);
}

static int vformat(Out *o, const char *f, va_list ap) {
  for (; *f; f++) {
    if (*f != '%') { out_ch(o, *f); continue; }
    f++;
    int left = 0, plus = 0, space = 0, alt = 0, zero = 0;
    for (;; f++) {
      if (*f == '-') left = 1;
      else if (*f == '+') plus = 1;
      else if (*f == ' ') space = 1;
      else if (*f == '#') alt = 1;
      else if (*f == '0') zero = 1;
      else break;
    }
    int width = 0, prec = -1;
    if (*f == '*') { width = va_arg(ap, int); if (width < 0) { left = 1; width = -width; } f++; }
    else while (isdigit((unsigned char)*f)) width = width * 10 + (*f++ - '0');
    if (*f == '.') {
      f++;
      prec = 0;
      if (*f == '*') { prec = va_arg(ap, int); f++; }
      else while (isdigit((unsigned char)*f)) prec = prec * 10 + (*f++ - '0');
    }
    int lng = 0;
    for (;; f++) {
      if (*f == 'l') lng = lng == 1 ? 2 : 1;
      else if (*f == 'z' || *f == 'j' || *f == 't') lng = *f == 'z' || *f == 't' ? 1 : 2;
      else if (*f == 'h' || *f == 'L') {}
      else break;
    }
    char c = *f;
    if (!c) break;
    if (c == '%') { out_ch(o, '%'); continue; }
    if (c == 'c') {
      char ch = (char)va_arg(ap, int);
      if (!left) out_pad(o, ' ', width - 1);
      out_ch(o, ch);
      if (left) out_pad(o, ' ', width - 1);
      continue;
    }
    if (c == 's') {
      const char *s = va_arg(ap, const char *);
      if (!s) s = "(null)";
      size_t n = 0;
      while (s[n] && (prec < 0 || (int)n < prec)) n++;
      if (!left) out_pad(o, ' ', width - (int)n);
      out_str(o, s, n);
      if (left) out_pad(o, ' ', width - (int)n);
      continue;
    }
    if (c == 'f' || c == 'F' || c == 'e' || c == 'E' || c == 'g' || c == 'G') {
      double v = va_arg(ap, double);
      fmt_float(o, v, c, prec, left, plus, space, alt, zero && !left, width);
      continue;
    }
    if (c == 'd' || c == 'i' || c == 'u' || c == 'x' || c == 'X' || c == 'o' || c == 'p') {
      unsigned long long u;
      int neg = 0;
      int base = 10;
      const char *digs = "0123456789abcdef";
      if (c == 'p') { u = (uintptr_t)va_arg(ap, void *); base = 16; alt = 1; }
      else if (c == 'd' || c == 'i') {
        long long s = lng == 2 ? va_arg(ap, long long) : lng == 1 ? (long long)va_arg(ap, long) : (long long)va_arg(ap, int);
        if (s < 0) { neg = 1; u = (unsigned long long)(-(s + 1)) + 1; }
        else u = (unsigned long long)s;
      } else {
        u = lng == 2 ? va_arg(ap, unsigned long long) : lng == 1 ? (unsigned long long)va_arg(ap, unsigned long) : (unsigned long long)va_arg(ap, unsigned);
        if (c == 'x' || c == 'p') base = 16;
        else if (c == 'X') { base = 16; digs = "0123456789ABCDEF"; }
        else if (c == 'o') base = 8;
      }
      char tmp[72];
      int n = 0;
      int uzero = u == 0;
      if (u == 0) { if (prec != 0) tmp[n++] = '0'; }
      while (u) { tmp[n++] = digs[u % (unsigned)base]; u /= (unsigned)base; }
      int zeros = prec > n ? prec - n : 0;
      char sign = neg ? '-' : (c == 'd' || c == 'i') ? (plus ? '+' : space ? ' ' : 0) : 0;
      const char *pre = "";
      if (alt && base == 16 && !uzero) pre = c == 'X' ? "0X" : "0x";
      else if (alt && base == 8 && (n == 0 || tmp[n - 1] != '0') && zeros == 0) zeros = 1;
      int total = n + zeros + (sign ? 1 : 0) + (int)strlen(pre);
      int lz = 0;
      if (zero && !left && prec < 0) lz = width - total > 0 ? width - total : 0;
      if (!left) out_pad(o, ' ', width - total - lz);
      if (sign) out_ch(o, sign);
      out_str(o, pre, strlen(pre));
      out_pad(o, '0', zeros + lz);
      while (n) out_ch(o, tmp[--n]);
      if (left) out_pad(o, ' ', width - total);
      continue;
    }
    out_ch(o, '%');
    out_ch(o, c);
  }
  if (o->cap) o->buf[o->n < o->cap ? o->n : o->cap - 1] = 0;
  return (int)o->n;
}

int vsnprintf(char *buf, size_t cap, const char *fmt, va_list ap) {
  Out o = {buf, cap, 0};
  return vformat(&o, fmt, ap);
}

int snprintf(char *buf, size_t cap, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, cap, fmt, ap);
  va_end(ap);
  return r;
}

int vsprintf(char *buf, const char *fmt, va_list ap) { return vsnprintf(buf, (size_t)0x7fffffff, fmt, ap); }

int sprintf(char *buf, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vsprintf(buf, fmt, ap);
  va_end(ap);
  return r;
}

int vfprintf(FILE *f, const char *fmt, va_list ap) {
  static char line[16384];
  int r = vsnprintf(line, sizeof(line), fmt, ap);
  size_t n = (size_t)r < sizeof(line) ? (size_t)r : sizeof(line) - 1;
  if (f && f->fd >= 0) host_write(f->fd, line, (unsigned)n);
  return r;
}

int fprintf(FILE *f, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(f, fmt, ap);
  va_end(ap);
  return r;
}

int printf(const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  int r = vfprintf(stdout, fmt, ap);
  va_end(ap);
  return r;
}
