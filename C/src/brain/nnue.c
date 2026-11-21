#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "brain/nnue.h"

#if defined(__AVX2__)
#include <immintrin.h>
#define NNUE_AVX2 1
#elif defined(__SSSE3__)
#include <tmmintrin.h>
#define NNUE_SSSE3 1
#endif

/* white perspective uses sq ^ 56 (a8=0 -> a1=0), black perspective keeps the engine square
   (vertical flip of the a1=0 square). HalfKAv2 orients by flip, not by 180-degree rotation. */
#ifndef NNUE_BLACK_XOR
#define NNUE_BLACK_XOR 0
#endif

/* nn-190f102a22c3: HalfKAv2 [45056 -> 512] x2, 8 PSQT buckets, 8 layer stacks (1024 -> 16 -> 32 -> 1) */
#define NNUE_VERSION 0x7AF32F20u
#define NNUE_HEADER_HASH 0x3C103E72u
#define NNUE_FT_HASH 0x5F2348B8u
#define NNUE_NET_HASH 0x633376CAu
#define NNUE_L0_IN (2 * NNUE_HALF_DIMS)
#define NNUE_L0_OUT 16
#define NNUE_L1_IN 32
#define NNUE_L1_OUT 32
#define NNUE_PS_END 704
#define NNUE_PS_KING 640
#define NNUE_OUTPUT_SCALE 16

typedef struct {
  _Alignas(64) int32_t b0[NNUE_L0_OUT];
  _Alignas(64) int8_t w0[NNUE_L0_OUT * NNUE_L0_IN];
  _Alignas(64) int32_t b1[NNUE_L1_OUT];
  _Alignas(64) int8_t w1[NNUE_L1_OUT * NNUE_L1_IN];
  int32_t b2;
  _Alignas(32) int8_t w2[NNUE_L1_OUT];
} NnueLayers;

typedef struct {
  int16_t *ft_bias;
  int16_t *ft_weight;
  int32_t *ft_psqt;
  NnueLayers L[NNUE_BUCKETS];
} NnueNet;

static NnueNet g_net;
static void *g_raw = NULL;
static bool g_loaded = false;
static bool g_enabled = true;
static char g_path[1200];

const char *nnue_simd_name(void) {
#if defined(NNUE_AVX2)
  return "AVX2";
#elif defined(NNUE_SSSE3)
  return "SSSE3";
#else
  return "scalar";
#endif
}

bool nnue_loaded(void) { return g_loaded; }
const char *nnue_loaded_path(void) { return g_loaded ? g_path : ""; }
void nnue_set_enabled(bool on) { g_enabled = on; }
bool nnue_enabled(void) { return g_enabled; }
bool nnue_active(void) { return g_enabled && g_loaded; }

void nnue_unload(void) {
  free(g_raw);
  g_raw = NULL;
  g_loaded = false;
  g_path[0] = 0;
  memset(&g_net, 0, sizeof(g_net));
}

static inline uint32_t bswap32_(uint32_t v) {
  return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
}

static inline uint16_t bswap16_(uint16_t v) { return (uint16_t)((v >> 8) | (v << 8)); }

static bool big_endian_host(void) {
  const uint16_t probe = 1;
  return *(const uint8_t *)&probe == 0;
}

static bool read_u32(FILE *f, uint32_t *out) {
  uint32_t v;
  if (fread(&v, 4, 1, f) != 1) return false;
  *out = big_endian_host() ? bswap32_(v) : v;
  return true;
}

static bool read_i16_array(FILE *f, int16_t *dst, size_t n) {
  if (fread(dst, sizeof(int16_t), n, f) != n) return false;
  if (big_endian_host()) for (size_t i = 0; i < n; i++) dst[i] = (int16_t)bswap16_((uint16_t)dst[i]);
  return true;
}

static bool read_i32_array(FILE *f, int32_t *dst, size_t n) {
  if (fread(dst, sizeof(int32_t), n, f) != n) return false;
  if (big_endian_host()) for (size_t i = 0; i < n; i++) dst[i] = (int32_t)bswap32_((uint32_t)dst[i]);
  return true;
}

static bool read_i8_array(FILE *f, int8_t *dst, size_t n) { return fread(dst, 1, n, f) == n; }

static void set_err(char *err, size_t cap, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
#include <stdarg.h>
static void set_err(char *err, size_t cap, const char *fmt, ...) {
  if (!err || cap == 0) return;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(err, cap, fmt, ap);
  va_end(ap);
}

bool nnue_load(const char *path, char *err, size_t err_cap) {
  if (err && err_cap) err[0] = 0;
  if (!path || !*path) {
    set_err(err, err_cap, "empty network file name");
    return false;
  }
  FILE *f = fopen(path, "rb");
  if (!f) {
    set_err(err, err_cap, "cannot open '%s': %s", path, strerror(errno));
    return false;
  }

  uint32_t version = 0, hash = 0, dsize = 0;
  if (!read_u32(f, &version) || !read_u32(f, &hash) || !read_u32(f, &dsize)) {
    set_err(err, err_cap, "'%s' is too short to be an NNUE file", path);
    fclose(f);
    return false;
  }
  if (version != NNUE_VERSION) {
    set_err(err, err_cap, "'%s' has unsupported NNUE version 0x%08X (expected 0x%08X)", path, version, NNUE_VERSION);
    fclose(f);
    return false;
  }
  if (dsize == 0 || dsize > 4096) {
    set_err(err, err_cap, "'%s' has an invalid architecture description size %u", path, dsize);
    fclose(f);
    return false;
  }
  char desc[4100];
  if (fread(desc, 1, dsize, f) != dsize) {
    set_err(err, err_cap, "'%s' is truncated inside the header", path);
    fclose(f);
    return false;
  }
  desc[dsize] = 0;
  if (hash != NNUE_HEADER_HASH) {
    set_err(err, err_cap, "'%s' has an unsupported architecture (hash 0x%08X, expected 0x%08X for HalfKAv2 512x2, 8 stacks): %.120s", path, hash, NNUE_HEADER_HASH, desc);
    fclose(f);
    return false;
  }

  uint32_t fth = 0;
  if (!read_u32(f, &fth) || fth != NNUE_FT_HASH) {
    set_err(err, err_cap, "'%s' feature transformer header mismatch", path);
    fclose(f);
    return false;
  }

  size_t ft_bias_n = NNUE_HALF_DIMS;
  size_t ft_weight_n = (size_t)NNUE_FEATURES * NNUE_HALF_DIMS;
  size_t ft_psqt_n = (size_t)NNUE_FEATURES * NNUE_BUCKETS;
  size_t raw_bytes = (ft_bias_n + ft_weight_n) * sizeof(int16_t) + ft_psqt_n * sizeof(int32_t) + 64;
  void *raw = malloc(raw_bytes);
  if (!raw) {
    set_err(err, err_cap, "out of memory loading '%s'", path);
    fclose(f);
    return false;
  }
  uintptr_t aligned = ((uintptr_t)raw + 63u) & ~(uintptr_t)63u;
  NnueNet *tmp = malloc(sizeof(NnueNet) + 64);
  if (!tmp) {
    set_err(err, err_cap, "out of memory loading '%s'", path);
    free(raw);
    fclose(f);
    return false;
  }
  NnueNet *net = (NnueNet *)(((uintptr_t)tmp + 63u) & ~(uintptr_t)63u);
  memset(net, 0, sizeof(*net));
  net->ft_bias = (int16_t *)aligned;
  net->ft_weight = net->ft_bias + ft_bias_n;
  net->ft_psqt = (int32_t *)(net->ft_weight + ft_weight_n);

  bool ok = read_i16_array(f, net->ft_bias, ft_bias_n) && read_i16_array(f, net->ft_weight, ft_weight_n) &&
            read_i32_array(f, net->ft_psqt, ft_psqt_n);
  if (!ok) {
    set_err(err, err_cap, "'%s' is truncated inside the feature transformer", path);
    free(raw);
    free(tmp);
    fclose(f);
    return false;
  }

  for (int k = 0; k < NNUE_BUCKETS; k++) {
    NnueLayers *L = &net->L[k];
    uint32_t nh = 0;
    if (!read_u32(f, &nh) || nh != NNUE_NET_HASH) {
      set_err(err, err_cap, "'%s' network layer header mismatch in stack %d", path, k);
      free(raw);
      free(tmp);
      fclose(f);
      return false;
    }
    ok = read_i32_array(f, L->b0, NNUE_L0_OUT) && read_i8_array(f, L->w0, sizeof(L->w0)) &&
         read_i32_array(f, L->b1, NNUE_L1_OUT) && read_i8_array(f, L->w1, sizeof(L->w1)) &&
         read_i32_array(f, &L->b2, 1) && read_i8_array(f, L->w2, sizeof(L->w2));
    if (!ok) {
      set_err(err, err_cap, "'%s' is truncated inside layer stack %d", path, k);
      free(raw);
      free(tmp);
      fclose(f);
      return false;
    }
  }
  if (fgetc(f) != EOF) {
    set_err(err, err_cap, "'%s' has unexpected trailing data", path);
    free(raw);
    free(tmp);
    fclose(f);
    return false;
  }
  fclose(f);

  nnue_unload();
  void *keep = raw;
  g_raw = keep;
  memcpy(&g_net, net, sizeof(g_net));
  g_net.ft_bias = (int16_t *)aligned;
  g_net.ft_weight = g_net.ft_bias + ft_bias_n;
  g_net.ft_psqt = (int32_t *)(g_net.ft_weight + ft_weight_n);
  free(tmp);
  g_loaded = true;
  snprintf(g_path, sizeof(g_path), "%s", path);
  return true;
}

static bool file_exists(const char *p) {
  FILE *f = fopen(p, "rb");
  if (!f) return false;
  fclose(f);
  return true;
}

static const char *base_name(const char *p) {
  const char *s = strrchr(p, '/');
  const char *b = strrchr(p, '\\');
  if (b && (!s || b > s)) s = b;
  return s ? s + 1 : p;
}

const char *nnue_find_file(const char *name, const char *argv0, char *out, size_t cap) {
  if (!name || !*name) return NULL;
  if (file_exists(name)) {
    snprintf(out, cap, "%s", name);
    return out;
  }
  const char *base = base_name(name);
  char cand[1500];
  const char *env = getenv("VEXAPH_NNUE_DIR");
  if (env && *env) {
    snprintf(cand, sizeof(cand), "%s/%s", env, base);
    if (file_exists(cand)) {
      snprintf(out, cap, "%s", cand);
      return out;
    }
  }
  if (argv0 && *argv0) {
    char dir[1300];
    snprintf(dir, sizeof(dir), "%s", argv0);
    char *slash = strrchr(dir, '/');
    char *bslash = strrchr(dir, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) {
      *slash = 0;
      const char *rel[] = {"%s/%s", "%s/../%s", "%s/net/%s", "%s/../net/%s", "%s/../../%s"};
      for (int i = 0; i < 5; i++) {
        snprintf(cand, sizeof(cand), rel[i], dir, base);
        if (file_exists(cand)) {
          snprintf(out, cap, "%s", cand);
          return out;
        }
      }
    }
  }
  const char *cwd_rel[] = {"net/%s", "../%s", "../net/%s", "build/%s"};
  for (int i = 0; i < 4; i++) {
    snprintf(cand, sizeof(cand), cwd_rel[i], base);
    if (file_exists(cand)) {
      snprintf(out, cap, "%s", cand);
      return out;
    }
  }
  return NULL;
}

bool nnue_autoload(const char *argv0, char *msg, size_t cap) {
  char found[1500];
  char err[1600];
  if (!nnue_enabled()) return false;
  const char *path = nnue_find_file(NNUE_DEFAULT_FILE, argv0, found, sizeof(found));
  if (!path) {
    snprintf(msg, cap, "NNUE file '%s' not found, using classical evaluation", NNUE_DEFAULT_FILE);
    return false;
  }
  if (!nnue_load(path, err, sizeof(err))) {
    snprintf(msg, cap, "%s, using classical evaluation", err);
    return false;
  }
  snprintf(msg, cap, "NNUE evaluation using %s (%s)", path, nnue_simd_name());
  return true;
}

static inline int orient_sq(int persp, int sq) { return sq ^ (persp == WHITE ? 56 : NNUE_BLACK_XOR); }

static inline int feat_index(int persp, int ksq_o, int pc, int sq) {
  int type = pc >= 6 ? pc - 6 : pc;
  int enemy = (pc >= 6 ? 1 : 0) != persp;
  int base = type == KING ? NNUE_PS_KING : 64 * (2 * type + enemy);
  return orient_sq(persp, sq) + base + NNUE_PS_END * ksq_o;
}

static inline int bucket_of(const Board *b) { return (count_bits(b->occupancies[BOTH]) - 1) / 4; }

static void acc_apply_scalar(int16_t *dst, const int16_t *src, const int *add, int na, const int *sub, int ns) {
  int16_t tmp[NNUE_HALF_DIMS];
  memcpy(tmp, src, sizeof(tmp));
  for (int k = 0; k < na; k++) {
    const int16_t *w = g_net.ft_weight + (size_t)add[k] * NNUE_HALF_DIMS;
    for (int i = 0; i < NNUE_HALF_DIMS; i++) tmp[i] = (int16_t)(tmp[i] + w[i]);
  }
  for (int k = 0; k < ns; k++) {
    const int16_t *w = g_net.ft_weight + (size_t)sub[k] * NNUE_HALF_DIMS;
    for (int i = 0; i < NNUE_HALF_DIMS; i++) tmp[i] = (int16_t)(tmp[i] - w[i]);
  }
  memcpy(dst, tmp, sizeof(tmp));
}

#if defined(NNUE_AVX2)

static void acc_apply(int16_t *dst, const int16_t *src, const int *add, int na, const int *sub, int ns) {
  for (int t = 0; t < NNUE_HALF_DIMS; t += 128) {
    __m256i r[8];
    for (int i = 0; i < 8; i++) r[i] = _mm256_loadu_si256((const __m256i *)(src + t + i * 16));
    for (int k = 0; k < na; k++) {
      const int16_t *w = g_net.ft_weight + (size_t)add[k] * NNUE_HALF_DIMS + t;
      for (int i = 0; i < 8; i++) r[i] = _mm256_add_epi16(r[i], _mm256_loadu_si256((const __m256i *)(w + i * 16)));
    }
    for (int k = 0; k < ns; k++) {
      const int16_t *w = g_net.ft_weight + (size_t)sub[k] * NNUE_HALF_DIMS + t;
      for (int i = 0; i < 8; i++) r[i] = _mm256_sub_epi16(r[i], _mm256_loadu_si256((const __m256i *)(w + i * 16)));
    }
    for (int i = 0; i < 8; i++) _mm256_storeu_si256((__m256i *)(dst + t + i * 16), r[i]);
  }
}

static void transform(const int16_t *us, const int16_t *them, uint8_t *out) {
  const __m256i zero = _mm256_setzero_si256();
  for (int half = 0; half < 2; half++) {
    const int16_t *src = half == 0 ? us : them;
    for (int i = 0; i < NNUE_HALF_DIMS; i += 32) {
      __m256i a = _mm256_max_epi16(_mm256_loadu_si256((const __m256i *)(src + i)), zero);
      __m256i b = _mm256_max_epi16(_mm256_loadu_si256((const __m256i *)(src + i + 16)), zero);
      __m256i p = _mm256_permute4x64_epi64(_mm256_packs_epi16(a, b), 0xD8);
      _mm256_storeu_si256((__m256i *)(out + half * NNUE_HALF_DIMS + i), p);
    }
  }
}

static inline __m128i hadd4(__m256i a, __m256i b, __m256i c, __m256i d, __m128i bias) {
  a = _mm256_hadd_epi32(a, b);
  c = _mm256_hadd_epi32(c, d);
  a = _mm256_hadd_epi32(a, c);
  __m128i s = _mm_add_epi32(_mm256_extracti128_si256(a, 1), _mm256_castsi256_si128(a));
  return _mm_add_epi32(s, bias);
}

static inline __m256i crelu32(const int32_t *p) {
  __m256i w0 = _mm256_srai_epi16(_mm256_packs_epi32(_mm256_loadu_si256((const __m256i *)(p)), _mm256_loadu_si256((const __m256i *)(p + 8))), 6);
  __m256i w1 = _mm256_srai_epi16(_mm256_packs_epi32(_mm256_loadu_si256((const __m256i *)(p + 16)), _mm256_loadu_si256((const __m256i *)(p + 24))), 6);
  __m256i b = _mm256_max_epi8(_mm256_packs_epi16(w0, w1), _mm256_setzero_si256());
  return _mm256_permutevar8x32_epi32(b, _mm256_setr_epi32(0, 4, 1, 5, 2, 6, 3, 7));
}

static int forward(const uint8_t *in, const NnueLayers *L) {
  const __m256i ones = _mm256_set1_epi16(1);
  _Alignas(32) int32_t o0[NNUE_L1_IN];
  _Alignas(32) int32_t o1[NNUE_L1_OUT];
  memset(o0, 0, sizeof(o0));

  for (int o = 0; o < NNUE_L0_OUT; o += 4) {
    __m256i s0 = _mm256_setzero_si256(), s1 = s0, s2 = s0, s3 = s0;
    const int8_t *w = L->w0 + (size_t)o * NNUE_L0_IN;
    for (int c = 0; c < NNUE_L0_IN; c += 32) {
      __m256i x = _mm256_loadu_si256((const __m256i *)(in + c));
      s0 = _mm256_add_epi32(s0, _mm256_madd_epi16(_mm256_maddubs_epi16(x, _mm256_loadu_si256((const __m256i *)(w + c))), ones));
      s1 = _mm256_add_epi32(s1, _mm256_madd_epi16(_mm256_maddubs_epi16(x, _mm256_loadu_si256((const __m256i *)(w + NNUE_L0_IN + c))), ones));
      s2 = _mm256_add_epi32(s2, _mm256_madd_epi16(_mm256_maddubs_epi16(x, _mm256_loadu_si256((const __m256i *)(w + 2 * NNUE_L0_IN + c))), ones));
      s3 = _mm256_add_epi32(s3, _mm256_madd_epi16(_mm256_maddubs_epi16(x, _mm256_loadu_si256((const __m256i *)(w + 3 * NNUE_L0_IN + c))), ones));
    }
    _mm_storeu_si128((__m128i *)(o0 + o), hadd4(s0, s1, s2, s3, _mm_loadu_si128((const __m128i *)(L->b0 + o))));
  }
  __m256i h1 = crelu32(o0);

  for (int o = 0; o < NNUE_L1_OUT; o += 4) {
    const int8_t *w = L->w1 + (size_t)o * NNUE_L1_IN;
    __m256i s0 = _mm256_madd_epi16(_mm256_maddubs_epi16(h1, _mm256_loadu_si256((const __m256i *)(w))), ones);
    __m256i s1 = _mm256_madd_epi16(_mm256_maddubs_epi16(h1, _mm256_loadu_si256((const __m256i *)(w + 32))), ones);
    __m256i s2 = _mm256_madd_epi16(_mm256_maddubs_epi16(h1, _mm256_loadu_si256((const __m256i *)(w + 64))), ones);
    __m256i s3 = _mm256_madd_epi16(_mm256_maddubs_epi16(h1, _mm256_loadu_si256((const __m256i *)(w + 96))), ones);
    _mm_storeu_si128((__m128i *)(o1 + o), hadd4(s0, s1, s2, s3, _mm_loadu_si128((const __m128i *)(L->b1 + o))));
  }
  __m256i h2 = crelu32(o1);

  __m256i v = _mm256_madd_epi16(_mm256_maddubs_epi16(h2, _mm256_loadu_si256((const __m256i *)L->w2)), ones);
  __m128i s = _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
  s = _mm_hadd_epi32(s, s);
  s = _mm_hadd_epi32(s, s);
  return _mm_cvtsi128_si32(s) + L->b2;
}

#elif defined(NNUE_SSSE3)

static void acc_apply(int16_t *dst, const int16_t *src, const int *add, int na, const int *sub, int ns) {
  for (int t = 0; t < NNUE_HALF_DIMS; t += 64) {
    __m128i r[8];
    for (int i = 0; i < 8; i++) r[i] = _mm_loadu_si128((const __m128i *)(src + t + i * 8));
    for (int k = 0; k < na; k++) {
      const int16_t *w = g_net.ft_weight + (size_t)add[k] * NNUE_HALF_DIMS + t;
      for (int i = 0; i < 8; i++) r[i] = _mm_add_epi16(r[i], _mm_loadu_si128((const __m128i *)(w + i * 8)));
    }
    for (int k = 0; k < ns; k++) {
      const int16_t *w = g_net.ft_weight + (size_t)sub[k] * NNUE_HALF_DIMS + t;
      for (int i = 0; i < 8; i++) r[i] = _mm_sub_epi16(r[i], _mm_loadu_si128((const __m128i *)(w + i * 8)));
    }
    for (int i = 0; i < 8; i++) _mm_storeu_si128((__m128i *)(dst + t + i * 8), r[i]);
  }
}

static void transform(const int16_t *us, const int16_t *them, uint8_t *out) {
  const __m128i zero = _mm_setzero_si128();
  for (int half = 0; half < 2; half++) {
    const int16_t *src = half == 0 ? us : them;
    for (int i = 0; i < NNUE_HALF_DIMS; i += 16) {
      __m128i a = _mm_max_epi16(_mm_loadu_si128((const __m128i *)(src + i)), zero);
      __m128i b = _mm_max_epi16(_mm_loadu_si128((const __m128i *)(src + i + 8)), zero);
      _mm_storeu_si128((__m128i *)(out + half * NNUE_HALF_DIMS + i), _mm_packs_epi16(a, b));
    }
  }
}

static inline __m128i hadd4(__m128i a, __m128i b, __m128i c, __m128i d, __m128i bias) {
  __m128i t0 = _mm_hadd_epi32(a, b);
  __m128i t1 = _mm_hadd_epi32(c, d);
  return _mm_add_epi32(_mm_hadd_epi32(t0, t1), bias);
}

static inline void crelu32(const int32_t *p, uint8_t *out) {
  const __m128i zero = _mm_setzero_si128();
  __m128i w[4];
  for (int i = 0; i < 4; i++) {
    __m128i a = _mm_loadu_si128((const __m128i *)(p + i * 8));
    __m128i b = _mm_loadu_si128((const __m128i *)(p + i * 8 + 4));
    w[i] = _mm_max_epi16(_mm_srai_epi16(_mm_packs_epi32(a, b), 6), zero);
  }
  _mm_storeu_si128((__m128i *)out, _mm_packs_epi16(w[0], w[1]));
  _mm_storeu_si128((__m128i *)(out + 16), _mm_packs_epi16(w[2], w[3]));
}

static int forward(const uint8_t *in, const NnueLayers *L) {
  const __m128i ones = _mm_set1_epi16(1);
  _Alignas(16) int32_t o0[NNUE_L1_IN];
  _Alignas(16) int32_t o1[NNUE_L1_OUT];
  _Alignas(16) uint8_t h1[NNUE_L1_IN];
  memset(o0, 0, sizeof(o0));
  _Alignas(16) uint8_t h2[NNUE_L1_OUT];

  for (int o = 0; o < NNUE_L0_OUT; o += 4) {
    __m128i s0 = _mm_setzero_si128(), s1 = s0, s2 = s0, s3 = s0;
    const int8_t *w = L->w0 + (size_t)o * NNUE_L0_IN;
    for (int c = 0; c < NNUE_L0_IN; c += 16) {
      __m128i x = _mm_loadu_si128((const __m128i *)(in + c));
      s0 = _mm_add_epi32(s0, _mm_madd_epi16(_mm_maddubs_epi16(x, _mm_loadu_si128((const __m128i *)(w + c))), ones));
      s1 = _mm_add_epi32(s1, _mm_madd_epi16(_mm_maddubs_epi16(x, _mm_loadu_si128((const __m128i *)(w + NNUE_L0_IN + c))), ones));
      s2 = _mm_add_epi32(s2, _mm_madd_epi16(_mm_maddubs_epi16(x, _mm_loadu_si128((const __m128i *)(w + 2 * NNUE_L0_IN + c))), ones));
      s3 = _mm_add_epi32(s3, _mm_madd_epi16(_mm_maddubs_epi16(x, _mm_loadu_si128((const __m128i *)(w + 3 * NNUE_L0_IN + c))), ones));
    }
    _mm_storeu_si128((__m128i *)(o0 + o), hadd4(s0, s1, s2, s3, _mm_loadu_si128((const __m128i *)(L->b0 + o))));
  }
  crelu32(o0, h1);

  __m128i x0 = _mm_loadu_si128((const __m128i *)h1);
  __m128i x1 = _mm_loadu_si128((const __m128i *)(h1 + 16));
  for (int o = 0; o < NNUE_L1_OUT; o += 4) {
    const int8_t *w = L->w1 + (size_t)o * NNUE_L1_IN;
    __m128i s[4];
    for (int k = 0; k < 4; k++) {
      __m128i a = _mm_madd_epi16(_mm_maddubs_epi16(x0, _mm_loadu_si128((const __m128i *)(w + k * 32))), ones);
      __m128i b = _mm_madd_epi16(_mm_maddubs_epi16(x1, _mm_loadu_si128((const __m128i *)(w + k * 32 + 16))), ones);
      s[k] = _mm_add_epi32(a, b);
    }
    _mm_storeu_si128((__m128i *)(o1 + o), hadd4(s[0], s[1], s[2], s[3], _mm_loadu_si128((const __m128i *)(L->b1 + o))));
  }
  crelu32(o1, h2);

  __m128i y0 = _mm_loadu_si128((const __m128i *)h2);
  __m128i y1 = _mm_loadu_si128((const __m128i *)(h2 + 16));
  __m128i v = _mm_add_epi32(
      _mm_madd_epi16(_mm_maddubs_epi16(y0, _mm_loadu_si128((const __m128i *)L->w2)), ones),
      _mm_madd_epi16(_mm_maddubs_epi16(y1, _mm_loadu_si128((const __m128i *)(L->w2 + 16))), ones));
  v = _mm_hadd_epi32(v, v);
  v = _mm_hadd_epi32(v, v);
  return _mm_cvtsi128_si32(v) + L->b2;
}

#else

static void acc_apply(int16_t *dst, const int16_t *src, const int *add, int na, const int *sub, int ns) {
  acc_apply_scalar(dst, src, add, na, sub, ns);
}

static void transform(const int16_t *us, const int16_t *them, uint8_t *out) {
  for (int i = 0; i < NNUE_HALF_DIMS; i++) {
    int a = us[i], b = them[i];
    out[i] = (uint8_t)(a < 0 ? 0 : (a > 127 ? 127 : a));
    out[NNUE_HALF_DIMS + i] = (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
  }
}

static int forward_scalar(const uint8_t *in, const NnueLayers *L);

static int forward(const uint8_t *in, const NnueLayers *L) { return forward_scalar(in, L); }

#endif

static void transform_scalar(const int16_t *us, const int16_t *them, uint8_t *out) {
  for (int i = 0; i < NNUE_HALF_DIMS; i++) {
    int a = us[i], b = them[i];
    out[i] = (uint8_t)(a < 0 ? 0 : (a > 127 ? 127 : a));
    out[NNUE_HALF_DIMS + i] = (uint8_t)(b < 0 ? 0 : (b > 127 ? 127 : b));
  }
}

static inline uint8_t crelu_scalar(int32_t v) {
  int x = v >> 6;
  return (uint8_t)(x < 0 ? 0 : (x > 127 ? 127 : x));
}

static int forward_scalar(const uint8_t *in, const NnueLayers *L) {
  uint8_t h1[NNUE_L1_IN] = {0}, h2[NNUE_L1_OUT];
  for (int o = 0; o < NNUE_L0_OUT; o++) {
    int32_t sum = L->b0[o];
    const int8_t *w = L->w0 + (size_t)o * NNUE_L0_IN;
    for (int i = 0; i < NNUE_L0_IN; i++) sum += (int32_t)in[i] * w[i];
    h1[o] = crelu_scalar(sum);
  }
  for (int o = 0; o < NNUE_L1_OUT; o++) {
    int32_t sum = L->b1[o];
    const int8_t *w = L->w1 + (size_t)o * NNUE_L1_IN;
    for (int i = 0; i < NNUE_L1_IN; i++) sum += (int32_t)h1[i] * w[i];
    h2[o] = crelu_scalar(sum);
  }
  int32_t sum = L->b2;
  for (int i = 0; i < NNUE_L1_OUT; i++) sum += (int32_t)h2[i] * L->w2[i];
  return sum;
}

static int collect_features(const Board *b, int persp, int *list) {
  int ksq = orient_sq(persp, b->king_sq[persp]);
  int n = 0;
  for (int pc = 0; pc < 12; pc++) {
    U64 bb = b->bitboards[pc];
    while (bb) {
      int sq = __builtin_ctzll(bb);
      bb &= bb - 1;
      list[n++] = feat_index(persp, ksq, pc, sq);
    }
  }
  return n;
}

static void psqt_apply(int32_t *dst, const int32_t *src, const int *add, int na, const int *sub, int ns) {
  int32_t tmp[NNUE_BUCKETS];
  memcpy(tmp, src, sizeof(tmp));
  for (int k = 0; k < na; k++) {
    const int32_t *w = g_net.ft_psqt + (size_t)add[k] * NNUE_BUCKETS;
    for (int i = 0; i < NNUE_BUCKETS; i++) tmp[i] += w[i];
  }
  for (int k = 0; k < ns; k++) {
    const int32_t *w = g_net.ft_psqt + (size_t)sub[k] * NNUE_BUCKETS;
    for (int i = 0; i < NNUE_BUCKETS; i++) tmp[i] -= w[i];
  }
  memcpy(dst, tmp, sizeof(tmp));
}

static void refresh(const Board *b, int persp, int16_t *acc, int32_t *psqt) {
  int list[40];
  static const int32_t zero[NNUE_BUCKETS] = {0};
  int n = collect_features(b, persp, list);
  acc_apply(acc, g_net.ft_bias, list, n, NULL, 0);
  psqt_apply(psqt, zero, list, n, NULL, 0);
}

static inline int finish(int raw_out) { return raw_out / NNUE_OUTPUT_SCALE; }

static inline int combine(const Board *b, int32_t psqt[2][NNUE_BUCKETS], const uint8_t *in, int bucket, bool scalar) {
  int32_t p = (psqt[b->side][bucket] - psqt[b->side ^ 1][bucket]) / 2;
  int32_t pos = scalar ? forward_scalar(in, &g_net.L[bucket]) : forward(in, &g_net.L[bucket]);
  return finish(p + pos);
}

int nnue_raw_board(const Board *b) {
  _Alignas(32) int16_t acc[2][NNUE_HALF_DIMS];
  _Alignas(32) uint8_t in[NNUE_L0_IN];
  int32_t psqt[2][NNUE_BUCKETS];
  refresh(b, WHITE, acc[WHITE], psqt[WHITE]);
  refresh(b, BLACK, acc[BLACK], psqt[BLACK]);
  transform(acc[b->side], acc[b->side ^ 1], in);
  return combine(b, psqt, in, bucket_of(b), false);
}

int nnue_raw_reference(const Board *b) {
  int16_t acc[2][NNUE_HALF_DIMS];
  uint8_t in[NNUE_L0_IN];
  int32_t psqt[2][NNUE_BUCKETS];
  static const int32_t zero[NNUE_BUCKETS] = {0};
  for (int p = 0; p < 2; p++) {
    int list[40];
    int n = collect_features(b, p, list);
    acc_apply_scalar(acc[p], g_net.ft_bias, list, n, NULL, 0);
    psqt_apply(psqt[p], zero, list, n, NULL, 0);
  }
  transform_scalar(acc[b->side], acc[b->side ^ 1], in);
  return combine(b, psqt, in, bucket_of(b), true);
}

NnueStack *nnue_stack_create(void) {
  NnueStack *st = calloc(1, sizeof(NnueStack));
  if (!st) abort();
  return st;
}

void nnue_stack_free(NnueStack *st) { free(st); }

void nnue_stack_reset_root(NnueStack *st) {
  NnueEntry *e = &st->e[0];
  e->computed[0] = e->computed[1] = 0;
  e->nd = 0;
  e->king_color = -1;
}

void nnue_push_null(NnueStack *st, int ply) {
  NnueEntry *e = &st->e[ply];
  e->computed[0] = e->computed[1] = 0;
  e->nd = 0;
  e->king_color = -1;
}

void nnue_push_move(NnueStack *st, int ply, const Board *b, int move) {
  NnueEntry *e = &st->e[ply];
  e->computed[0] = e->computed[1] = 0;
  e->king_color = -1;
  int side = b->side;
  int src = move & 63;
  int dst = (move >> 6) & 63;
  int pc = (move >> 12) & 15;
  int promo = (move >> 16) & 15;
  int n = 0;
  if (pc % 6 == KING) e->king_color = (int8_t)side;
  if (move & MOVE_ENPASSANT) {
    e->d[n].pc = (int8_t)(side == WHITE ? B_PAWN : W_PAWN);
    e->d[n].from = (int8_t)(side == WHITE ? dst + 8 : dst - 8);
    e->d[n].to = NO_SQ;
    n++;
  } else if (move & MOVE_CAPTURE) {
    e->d[n].pc = b->mailbox[dst];
    e->d[n].from = (int8_t)dst;
    e->d[n].to = NO_SQ;
    n++;
  }
  if (promo) {
    e->d[n].pc = (int8_t)pc;
    e->d[n].from = (int8_t)src;
    e->d[n].to = NO_SQ;
    n++;
    e->d[n].pc = (int8_t)promo;
    e->d[n].from = NO_SQ;
    e->d[n].to = (int8_t)dst;
    n++;
  } else {
    e->d[n].pc = (int8_t)pc;
    e->d[n].from = (int8_t)src;
    e->d[n].to = (int8_t)dst;
    n++;
  }
  if (move & MOVE_CASTLING) {
    int rs, rd, rk;
    if (dst == g1) { rs = h1; rd = f1; rk = W_ROOK; }
    else if (dst == c1) { rs = a1; rd = d1; rk = W_ROOK; }
    else if (dst == g8) { rs = h8; rd = f8; rk = B_ROOK; }
    else { rs = a8; rd = d8; rk = B_ROOK; }
    e->d[n].pc = (int8_t)rk;
    e->d[n].from = (int8_t)rs;
    e->d[n].to = (int8_t)rd;
    n++;
  }
  e->nd = (int8_t)n;
}

static void ensure_acc(NnueStack *st, const Board *b, int ply, int p) {
  NnueEntry *cur = &st->e[ply];
  if (cur->computed[p]) return;
  int j = ply;
  int steps = 0;
  bool ok = false;
  while (j > 0 && steps < NNUE_MAX_CHAIN) {
    if (st->e[j].king_color == p) break;
    j--;
    steps++;
    if (st->e[j].computed[p]) {
      ok = true;
      break;
    }
  }
  if (ok) {
    int ksq = orient_sq(p, b->king_sq[p]);
    int add[3 * NNUE_MAX_CHAIN], sub[3 * NNUE_MAX_CHAIN];
    int na = 0, ns = 0;
    for (int i = j + 1; i <= ply; i++) {
      const NnueEntry *e = &st->e[i];
      for (int k = 0; k < e->nd; k++) {
        int pc = e->d[k].pc;
        if (e->d[k].from != NO_SQ) sub[ns++] = feat_index(p, ksq, pc, e->d[k].from);
        if (e->d[k].to != NO_SQ) add[na++] = feat_index(p, ksq, pc, e->d[k].to);
      }
    }
    acc_apply(cur->acc[p], st->e[j].acc[p], add, na, sub, ns);
    psqt_apply(cur->psqt[p], st->e[j].psqt[p], add, na, sub, ns);
  } else {
    refresh(b, p, cur->acc[p], cur->psqt[p]);
  }
  cur->computed[p] = 1;
}

int nnue_raw_stack(NnueStack *st, const Board *b, int ply) {
  ensure_acc(st, b, ply, WHITE);
  ensure_acc(st, b, ply, BLACK);
  _Alignas(32) uint8_t in[NNUE_L0_IN];
  NnueEntry *e = &st->e[ply];
  transform(e->acc[b->side], e->acc[b->side ^ 1], in);
  return combine(b, e->psqt, in, bucket_of(b), false);
}
