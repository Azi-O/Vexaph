#ifndef _VX_SYS_STAT_H
#define _VX_SYS_STAT_H
#define S_IFMT 0170000
#define S_IFDIR 0040000
#define S_IFREG 0100000
typedef unsigned mode_t;
struct stat { mode_t st_mode; long long st_size; };
int stat(const char *, struct stat *);
#endif
