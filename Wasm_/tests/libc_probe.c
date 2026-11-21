#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <inttypes.h>

#ifdef __wasm__
#define EXPORT __attribute__((visibility("default")))
#else
#define EXPORT
#endif

static char buf[1 << 20];

EXPORT const char *run(void) {
  char *p = buf;
  char t[512];
  double vals[] = {0.0, -0.0, 1.0, -1.5, 0.5, 2.5, 3.5, 0.1, 0.05, 123456.789, 1e-5, 1.5e-7, 1e15, 1e21, 9.9999995, 0.000123456, 5e-324, 1.7976931348623157e308, 0.999999999, 99999.5, 1234567.0, 0.486001234, 1e-4, 0.0001234, 100.0, 1e5, 123456789.0, 0.30000000000000004, 2.675, 1.005, 1e100};
  const char *fmts[] = {"%f", "%.0f", "%.1f", "%.3f", "%.6f", "%10.2f", "%-10.2f|", "%+.2f", "%e", "%.3e", "%g", "%.9g", "%.3g", "%.15g", "%.17g", "%G", "%#g", "%08.3f", "%.0e", "%#.0f"};
  for (size_t f = 0; f < sizeof(fmts) / sizeof(*fmts); f++)
    for (size_t v = 0; v < sizeof(vals) / sizeof(*vals); v++) {
      snprintf(t, sizeof t, fmts[f], vals[v]);
      p += sprintf(p, "%s [%s]\n", fmts[f], t);
    }
  long long ints[] = {0, 1, -1, 42, -42, 2147483647LL, -2147483648LL, 9223372036854775807LL, -9223372036854775807LL - 1};
  const char *ifm[] = {"%d", "%5d", "%-5d|", "%05d", "%+d", "% d", "%x", "%X", "%#x", "%o", "%u", "%lld", "%llu", "%016llx", "%08X", "%.5d", "%c"};
  for (size_t f = 0; f < sizeof(ifm) / sizeof(*ifm); f++)
    for (size_t v = 0; v < sizeof(ints) / sizeof(*ints); v++) {
      if (!strcmp(ifm[f], "%lld") || !strcmp(ifm[f], "%llu") || !strcmp(ifm[f], "%016llx")) snprintf(t, sizeof t, ifm[f], ints[v]);
      else snprintf(t, sizeof t, ifm[f], (int)ints[v]);
      p += sprintf(p, "%s [%s]\n", ifm[f], t);
    }
  snprintf(t, sizeof t, "%s|%10s|%-10s|%.3s|%.120s|%5c|%%|%zu|%" PRIu64, "abc", "abc", "abc", "abcdef", "xyz", 'q', (size_t)77, (uint64_t)18446744073709551615ull);
  p += sprintf(p, "%s\n", t);
  snprintf(t, sizeof t, "%*d|%-*d|%.*f", 6, 42, 6, 42, 2, 3.14159);
  p += sprintf(p, "%s\n", t);
  int n = snprintf(t, 5, "hello world");
  p += sprintf(p, "trunc %d [%s]\n", n, t);
  for (int d = 1; d < 64; d++) for (int m = 1; m < 64; m++) p += sprintf(p, "%d", (int)(int8_t)floor(0.75 + (log((double)d) * log((double)m)) / 2.25));
  *p++ = '\n';
  double maxrel = 0;
  for (double x = -20; x < 20; x += 0.37) { double e = exp(x); double ref = e; (void)ref; }
  p += sprintf(p, "strtol %ld %lld %ld\n", strtol("  -123abc", NULL, 10), strtoll("9223372036854775807", NULL, 10), strtol("0x1f", NULL, 16));
  (void)maxrel;
  *p = 0;
  return buf;
}

EXPORT const char *math_probe(void) {
  char *p = buf;
  for (int i = 1; i < 200; i++) {
    double x = i * 0.173;
    p += sprintf(p, "%.17g %.17g %.17g %.17g\n", log(x), exp(x / 7), pow(x, 0.37), pow(x, 3.0));
  }
  *p = 0;
  return buf;
}

#ifndef __wasm__
int main(void) {
  fputs(run(), stdout);
  fputs("---MATH\n", stdout);
  fputs(math_probe(), stdout);
  return 0;
}
#endif
