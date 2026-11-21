#ifndef _VX_STDLIB_H
#define _VX_STDLIB_H
#include <stddef.h>
void *malloc(size_t);
void *calloc(size_t, size_t);
void *realloc(void *, size_t);
void free(void *);
void qsort(void *, size_t, size_t, int (*)(const void *, const void *));
long strtol(const char *, char **, int);
long long strtoll(const char *, char **, int);
unsigned long strtoul(const char *, char **, int);
unsigned long long strtoull(const char *, char **, int);
double strtod(const char *, char **);
int atoi(const char *);
long atol(const char *);
int abs(int);
long labs(long);
long long llabs(long long);
char *getenv(const char *);
void abort(void) __attribute__((noreturn));
void exit(int) __attribute__((noreturn));
void _exit(int) __attribute__((noreturn));
int rand(void);
void srand(unsigned);
#define RAND_MAX 0x7fffffff
#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1
#endif
