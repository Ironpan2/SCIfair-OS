/*
 * lib.c - a tiny C library.
 *
 * An OS can't use the normal C library (printf, strlen...) because that
 * library itself needs an OS underneath it! So we write our own versions.
 */
#include "kernel.h"

void *memset(void *dst, int c, size_t n)
{
    void *ret = dst;
    __asm__ volatile ("rep stosb" : "+D"(dst), "+c"(n) : "a"(c) : "memory");
    return ret;
}

void *memcpy(void *dst, const void *src, size_t n)
{
    void *ret = dst;
    __asm__ volatile ("rep movsb" : "+D"(dst), "+S"(src), "+c"(n) : : "memory");
    return ret;
}

void *memmove(void *dst, const void *src, size_t n)
{
    uint8_t *d = dst;
    const uint8_t *s = src;
    if (d == s || n == 0)
        return dst;
    if (d < s || d >= s + n)
        return memcpy(dst, src, n);      /* forward copy is safe */
    while (n--)                          /* overlapping: copy backwards */
        d[n] = s[n];
    return dst;
}

size_t strlen(const char *s)
{
    size_t n = 0;
    while (s[n])
        n++;
    return n;
}

int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)*a - (unsigned char)*b;
}

int strncmp(const char *a, const char *b, size_t n)
{
    while (n && *a && *a == *b) {
        a++;
        b++;
        n--;
    }
    if (n == 0)
        return 0;
    return (unsigned char)*a - (unsigned char)*b;
}

bool parse_uint(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    if (!s || !*s)
        return false;
    for (; *s; s++) {
        if (*s < '0' || *s > '9')
            return false;
        if (v > 400000000u)          /* stop silly huge numbers overflowing */
            return false;
        v = v * 10 + (uint32_t)(*s - '0');
    }
    *out = v;
    return true;
}

/* ---- 64-bit division ----------------------------------------------
 * A 32-bit CPU can't divide 64-bit numbers in one instruction. GCC
 * normally calls helper functions from "libgcc" for that; we provide
 * our own (simple long division, one bit at a time). */
uint64_t __udivmoddi4(uint64_t num, uint64_t den, uint64_t *rem)
{
    uint64_t q = 0, r = 0;
    if (den == 0) {
        if (rem) *rem = 0;
        return 0;
    }
    if ((num >> 32) == 0 && (den >> 32) == 0) {      /* fast path */
        uint32_t n32 = (uint32_t)num, d32 = (uint32_t)den;
        if (rem) *rem = n32 % d32;
        return n32 / d32;
    }
    for (int i = 63; i >= 0; i--) {
        r = (r << 1) | ((num >> i) & 1);
        if (r >= den) {
            r -= den;
            q |= (uint64_t)1 << i;
        }
    }
    if (rem) *rem = r;
    return q;
}
uint64_t __udivdi3(uint64_t a, uint64_t b) { return __udivmoddi4(a, b, 0); }
uint64_t __umoddi3(uint64_t a, uint64_t b)
{
    uint64_t r;
    __udivmoddi4(a, b, &r);
    return r;
}
int64_t __divdi3(int64_t a, int64_t b)
{
    bool neg = (a < 0) != (b < 0);
    uint64_t q = __udivdi3(a < 0 ? -(uint64_t)a : (uint64_t)a,
                           b < 0 ? -(uint64_t)b : (uint64_t)b);
    return neg ? -(int64_t)q : (int64_t)q;
}
int64_t __moddi3(int64_t a, int64_t b)
{
    uint64_t r = __umoddi3(a < 0 ? -(uint64_t)a : (uint64_t)a,
                           b < 0 ? -(uint64_t)b : (uint64_t)b);
    return a < 0 ? -(int64_t)r : (int64_t)r;
}

/* ---- printf ----------------------------------------------------------
 * Supports: %d %i %u %x %X %s %c %p %%, widths (%5d), zero padding
 * (%05d), left alignment (%-10s), and l / ll for long / 64-bit. */
typedef struct {
    void (*sink)(char, void *);
    void *ctx;
    int count;
} fmt_out_t;

static void out_c(fmt_out_t *o, char c)
{
    o->sink(c, o->ctx);
    o->count++;
}

static void out_num(fmt_out_t *o, uint64_t v, unsigned base, bool neg,
                    int width, char pad, bool left, bool upper)
{
    const char *digits = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char tmp[24];
    int n = 0;

    if (v == 0)
        tmp[n++] = '0';
    while (v) {
        if ((v >> 32) == 0) {           /* finish with fast 32-bit math */
            uint32_t w = (uint32_t)v;
            while (w) {
                tmp[n++] = digits[w % base];
                w /= base;
            }
            break;
        }
        tmp[n++] = digits[v % base];
        v /= base;
    }

    int len = n + (neg ? 1 : 0);
    if (!left && pad == ' ')
        for (; len < width; width--) out_c(o, ' ');
    if (neg)
        out_c(o, '-');
    if (!left && pad == '0')
        for (; len < width; width--) out_c(o, '0');
    while (n)
        out_c(o, tmp[--n]);
    if (left)
        for (; len < width; width--) out_c(o, ' ');
}

int kvformat(void (*sink)(char, void *), void *ctx, const char *fmt, va_list ap)
{
    fmt_out_t o = { sink, ctx, 0 };

    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            out_c(&o, *fmt);
            continue;
        }
        fmt++;
        bool left = false;
        char pad = ' ';
        while (*fmt == '-' || *fmt == '0') {
            if (*fmt == '-') left = true;
            else pad = '0';
            fmt++;
        }
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');
        int lng = 0;
        while (*fmt == 'l') {
            lng++;
            fmt++;
        }

        switch (*fmt) {
        case 'd':
        case 'i': {
            int64_t v;
            if (lng >= 2) v = va_arg(ap, long long);
            else if (lng == 1) v = va_arg(ap, long);
            else v = va_arg(ap, int);
            bool neg = v < 0;
            uint64_t u = neg ? (uint64_t)0 - (uint64_t)v : (uint64_t)v;
            out_num(&o, u, 10, neg, width, pad, left, false);
            break;
        }
        case 'u':
        case 'x':
        case 'X': {
            uint64_t v;
            if (lng >= 2) v = va_arg(ap, unsigned long long);
            else if (lng == 1) v = va_arg(ap, unsigned long);
            else v = va_arg(ap, unsigned int);
            out_num(&o, v, *fmt == 'u' ? 10 : 16, false, width, pad, left,
                    *fmt == 'X');
            break;
        }
        case 'p':
            out_c(&o, '0');
            out_c(&o, 'x');
            out_num(&o, (uintptr_t)va_arg(ap, void *), 16, false, 8, '0',
                    false, false);
            break;
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s)
                s = "(null)";
            int len = (int)strlen(s);
            if (!left)
                for (int i = len; i < width; i++) out_c(&o, ' ');
            while (*s)
                out_c(&o, *s++);
            if (left)
                for (int i = len; i < width; i++) out_c(&o, ' ');
            break;
        }
        case 'c':
            out_c(&o, (char)va_arg(ap, int));
            break;
        case '%':
            out_c(&o, '%');
            break;
        case '\0':
            return o.count;
        default:
            out_c(&o, '%');
            out_c(&o, *fmt);
            break;
        }
    }
    return o.count;
}

typedef struct {
    char *buf;
    size_t size;
    size_t len;
} snbuf_t;

static void sn_sink(char c, void *ctx)
{
    snbuf_t *b = ctx;
    if (b->len + 1 < b->size)
        b->buf[b->len] = c;
    b->len++;
}

int ksnprintf(char *buf, size_t size, const char *fmt, ...)
{
    snbuf_t b = { buf, size, 0 };
    va_list ap;
    va_start(ap, fmt);
    int n = kvformat(sn_sink, &b, fmt, ap);
    va_end(ap);
    if (size)
        buf[b.len < size ? b.len : size - 1] = '\0';
    return n;
}

void fmt_ms(char *buf, size_t size, uint64_t us)
{
    ksnprintf(buf, size, "%llu.%03u ms", us / 1000, (unsigned)(us % 1000));
}
