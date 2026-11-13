/* Freestanding string helpers and a small printf engine (no 64-bit math). */
#include "common.h"

void *memset(void *dst, int c, size_t n) {
    uint8_t *d = dst;
    while (n--) *d++ = (uint8_t)c;
    return dst;
}

void *memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    while (n--) *d++ = *s++;
    return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d < s) {
        while (n--) *d++ = *s++;
    } else {
        d += n; s += n;
        while (n--) *--d = *--s;
    }
    return dst;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = a, *y = b;
    for (size_t i = 0; i < n; i++)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}

size_t strlen(const char *s) {
    size_t n = 0;
    while (s[n]) n++;
    return n;
}

int strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (int)(uint8_t)*a - (int)(uint8_t)*b;
}

int strncmp(const char *a, const char *b, size_t n) {
    while (n && *a && *a == *b) { a++; b++; n--; }
    return n ? (int)(uint8_t)*a - (int)(uint8_t)*b : 0;
}

/* Copies at most size-1 chars, always NUL-terminates (size > 0). Returns strlen(src). */
size_t strlcpy(char *dst, const char *src, size_t size) {
    size_t len = strlen(src);
    if (size) {
        size_t n = len < size - 1 ? len : size - 1;
        memcpy(dst, src, n);
        dst[n] = '\0';
    }
    return len;
}

char *strchr(const char *s, int c) {
    for (; *s; s++)
        if (*s == (char)c) return (char *)s;
    return c == 0 ? (char *)s : NULL;
}

const char *err_name(int code) {
    switch (code) {
    case E_OK: return "OK";
    case E_INVAL: return "ERR_INVAL";
    case E_NOENT: return "ERR_NOENT";
    case E_EXIST: return "ERR_EXIST";
    case E_NOSPC: return "ERR_NOSPC";
    case E_IO: return "ERR_IO";
    case E_PERM: return "ERR_PERM";
    case E_NODEV: return "ERR_NODEV";
    case E_STATE: return "ERR_STATE";
    case E_NOMEM: return "ERR_NOMEM";
    case E_TIMEOUT: return "ERR_TIMEOUT";
    case E_NOFS: return "ERR_NOFS";
    default: return "ERR_UNKNOWN";
    }
}

/* --- printf engine: %s %c %d %u %x %% with optional '0' flag and width --- */
typedef struct { char *buf; size_t size; size_t len; } outbuf_t;

static void put(outbuf_t *o, char c) {
    if (o->len + 1 < o->size) o->buf[o->len] = c;
    o->len++;
}

static void put_num(outbuf_t *o, uint32_t v, uint32_t base, bool neg, int width, bool zero) {
    char tmp[12];
    int n = 0;
    const char *digits = "0123456789abcdef";
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = digits[v % base]; v /= base; }
    int total = n + (neg ? 1 : 0);
    if (neg && zero) put(o, '-');
    for (int i = total; i < width; i++) put(o, zero ? '0' : ' ');
    if (neg && !zero) put(o, '-');
    while (n) put(o, tmp[--n]);
}

int kvsnprintf(char *buf, size_t size, const char *fmt, va_list ap) {
    outbuf_t o = { buf, size, 0 };
    for (; *fmt; fmt++) {
        if (*fmt != '%') { put(&o, *fmt); continue; }
        fmt++;
        bool zero = false, left = false;
        int width = 0;
        if (*fmt == '-') { left = true; fmt++; }
        if (*fmt == '0') { zero = true; fmt++; }
        while (*fmt >= '0' && *fmt <= '9') { width = width * 10 + (*fmt - '0'); fmt++; }
        switch (*fmt) {
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int len = (int)strlen(s);
            if (!left) for (int i = len; i < width; i++) put(&o, ' ');
            while (*s) put(&o, *s++);
            if (left) for (int i = len; i < width; i++) put(&o, ' ');
            break;
        }
        case 'c': put(&o, (char)va_arg(ap, int)); break;
        case 'd': {
            int32_t v = va_arg(ap, int32_t);
            bool neg = v < 0;
            put_num(&o, neg ? (uint32_t)0 - (uint32_t)v : (uint32_t)v, 10, neg, width, zero);
            break;
        }
        case 'u': put_num(&o, va_arg(ap, uint32_t), 10, false, width, zero); break;
        case 'x': put_num(&o, va_arg(ap, uint32_t), 16, false, width, zero); break;
        case '%': put(&o, '%'); break;
        case '\0': fmt--; break;
        default: put(&o, '%'); put(&o, *fmt); break;
        }
    }
    if (size) buf[o.len < size ? o.len : size - 1] = '\0';
    return (int)o.len;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = kvsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

bool parse_uint(const char *s, uint32_t *out) {
    uint32_t base = 10, v = 0;
    if (!s || !*s) return false;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; s += 2; if (!*s) return false; }
    for (; *s; s++) {
        uint32_t d;
        if (*s >= '0' && *s <= '9') d = (uint32_t)(*s - '0');
        else if (base == 16 && *s >= 'a' && *s <= 'f') d = (uint32_t)(*s - 'a' + 10);
        else if (base == 16 && *s >= 'A' && *s <= 'F') d = (uint32_t)(*s - 'A' + 10);
        else return false;
        if (v > (0xFFFFFFFFu - d) / base) return false;   /* overflow */
        v = v * base + d;
    }
    *out = v;
    return true;
}
