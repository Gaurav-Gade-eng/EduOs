#ifndef EDUOS_COMMON_H
#define EDUOS_COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

/* Kernel-wide status codes: 0 = success, negative = failure. */
#define E_OK      0
#define E_INVAL  (-1)
#define E_NOENT  (-2)
#define E_EXIST  (-3)
#define E_NOSPC  (-4)
#define E_IO     (-5)
#define E_PERM   (-6)
#define E_NODEV  (-7)
#define E_STATE  (-8)
#define E_NOMEM  (-9)
#define E_TIMEOUT (-10)
#define E_NOFS   (-11)

const char *err_name(int code);

#define UNUSED(x) ((void)(x))
#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

/* string.c */
void  *memset(void *dst, int c, size_t n);
void  *memcpy(void *dst, const void *src, size_t n);
void  *memmove(void *dst, const void *src, size_t n);
int    memcmp(const void *a, const void *b, size_t n);
size_t strlen(const char *s);
int    strcmp(const char *a, const char *b);
int    strncmp(const char *a, const char *b, size_t n);
size_t strlcpy(char *dst, const char *src, size_t size);
char  *strchr(const char *s, int c);
int    kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap);
int    ksnprintf(char *buf, size_t size, const char *fmt, ...);
bool   parse_uint(const char *s, uint32_t *out);   /* decimal or 0x hex */

#endif
