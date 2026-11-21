#ifndef _VX_STDIO_H
#define _VX_STDIO_H
#include <stddef.h>
#include <stdarg.h>
typedef struct VX_FILE FILE;
extern FILE *stdin, *stdout, *stderr;
#define EOF (-1)
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
FILE *fopen(const char *, const char *);
int fclose(FILE *);
size_t fread(void *, size_t, size_t, FILE *);
size_t fwrite(const void *, size_t, size_t, FILE *);
int fseek(FILE *, long, int);
long ftell(FILE *);
int fgetc(FILE *);
int fflush(FILE *);
int fputs(const char *, FILE *);
int puts(const char *);
int putchar(int);
int printf(const char *, ...) __attribute__((format(printf, 1, 2)));
int fprintf(FILE *, const char *, ...) __attribute__((format(printf, 2, 3)));
int sprintf(char *, const char *, ...) __attribute__((format(printf, 2, 3)));
int snprintf(char *, size_t, const char *, ...) __attribute__((format(printf, 3, 4)));
int vsnprintf(char *, size_t, const char *, va_list);
int vsprintf(char *, const char *, va_list);
int vfprintf(FILE *, const char *, va_list);
typedef long ssize_t;
#endif
