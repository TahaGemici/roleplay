#include "ortak.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ bellek */

static void bellek_bitti(void)
{
    fprintf(stderr, "Bellek yetmedi, sunucu duruyor.\n");
    abort();
}

void *xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p) bellek_bitti();
    return p;
}

void *xcalloc(size_t n, size_t size)
{
    void *p = calloc(n ? n : 1, size ? size : 1);
    if (!p) bellek_bitti();
    return p;
}

void *xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q) bellek_bitti();
    return q;
}

char *xstrdup(const char *s)
{
    size_t n = strlen(s) + 1;
    return memcpy(xmalloc(n), s, n);
}

/* ------------------------------------------------------------------ tampon */

void buf_init(Buf *b)
{
    b->p = NULL;
    b->len = 0;
    b->cap = 0;
}

void buf_free(Buf *b)
{
    free(b->p);
    buf_init(b);
}

static void buf_reserve(Buf *b, size_t extra)
{
    if (b->len + extra + 1 <= b->cap) return;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < b->len + extra + 1) cap *= 2;
    b->p = xrealloc(b->p, cap);
    b->cap = cap;
}

void buf_add(Buf *b, const void *data, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->p + b->len, data, n);
    b->len += n;
    b->p[b->len] = '\0';
}

void buf_puts(Buf *b, const char *s)
{
    buf_add(b, s, strlen(s));
}

void buf_putc(Buf *b, char c)
{
    buf_add(b, &c, 1);
}

static void buf_vprintf(Buf *b, const char *fmt, va_list ap)
{
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return;
    buf_reserve(b, (size_t)n);
    vsnprintf(b->p + b->len, (size_t)n + 1, fmt, ap);
    b->len += (size_t)n;
}

void buf_printf(Buf *b, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    buf_vprintf(b, fmt, ap);
    va_end(ap);
}

void buf_json_str(Buf *b, const char *s)
{
    static const char hex[] = "0123456789abcdef";
    buf_putc(b, '"');
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        unsigned char c = *p;
        switch (c) {
        case '"': buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\n': buf_puts(b, "\\n"); break;
        case '\r': buf_puts(b, "\\r"); break;
        case '\t': buf_puts(b, "\\t"); break;
        default:
            if (c < 0x20 || c == 0x7f) {
                char e[7] = { '\\', 'u', '0', '0', hex[c >> 4], hex[c & 15], 0 };
                buf_puts(b, e);
            } else {
                buf_putc(b, (char)c);
            }
        }
    }
    buf_putc(b, '"');
}

char *buf_take(Buf *b)
{
    if (!b->p) return xstrdup("");
    char *p = b->p;
    buf_init(b);
    return p;
}

/* ------------------------------------------------------------------ rastgelelik */

void rastgele_bayt(void *out, size_t n)
{
    unsigned char *p = out;
    while (n > 0) {
        ssize_t r = getrandom(p, n, 0);
        if (r < 0) {
            if (errno == EINTR) continue;
            int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
            if (fd < 0 || read(fd, p, n) != (ssize_t)n) {
                fprintf(stderr, "Rastgele sayı kaynağı okunamadı.\n");
                abort();
            }
            close(fd);
            return;
        }
        p += r;
        n -= (size_t)r;
    }
}

uint32_t rastgele_aralik(uint32_t n)
{
    if (n <= 1) return 0;
    /* Reddetme örneklemesi: modulo yanlılığı olmadan eşit dağılım */
    uint32_t sinir = UINT32_MAX - (UINT32_MAX % n);
    for (;;) {
        uint32_t x;
        rastgele_bayt(&x, sizeof x);
        if (x < sinir) return x % n;
    }
}

void rastgele_hex(char *out, size_t bayt)
{
    static const char hex[] = "0123456789abcdef";
    unsigned char tmp[64];
    if (bayt > sizeof tmp) bayt = sizeof tmp;
    rastgele_bayt(tmp, bayt);
    for (size_t i = 0; i < bayt; i++) {
        out[2 * i] = hex[tmp[i] >> 4];
        out[2 * i + 1] = hex[tmp[i] & 15];
    }
    out[2 * bayt] = '\0';
}

/* ------------------------------------------------------------------ SHA-1 */

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))

static void sha1_blok(uint32_t h[5], const unsigned char blok[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++) {
        w[i] = ((uint32_t)blok[4 * i] << 24) | ((uint32_t)blok[4 * i + 1] << 16) |
               ((uint32_t)blok[4 * i + 2] << 8) | (uint32_t)blok[4 * i + 3];
    }
    for (int i = 16; i < 80; i++) w[i] = ROL(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20) {
            f = (b & c) | (~b & d);
            k = 0x5A827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ED9EBA1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8F1BBCDC;
        } else {
            f = b ^ c ^ d;
            k = 0xCA62C1D6;
        }
        uint32_t t = ROL(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = ROL(b, 30);
        b = a;
        a = t;
    }
    h[0] += a;
    h[1] += b;
    h[2] += c;
    h[3] += d;
    h[4] += e;
}

void sha1(const void *data, size_t len, unsigned char out[20])
{
    uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
    const unsigned char *p = data;
    size_t kalan = len;
    while (kalan >= 64) {
        sha1_blok(h, p);
        p += 64;
        kalan -= 64;
    }
    unsigned char son[128];
    memset(son, 0, sizeof son);
    memcpy(son, p, kalan);
    son[kalan] = 0x80;
    size_t toplam = (kalan + 9 <= 64) ? 64 : 128;
    uint64_t bit = (uint64_t)len * 8;
    for (int i = 0; i < 8; i++) son[toplam - 1 - i] = (unsigned char)(bit >> (8 * i));
    sha1_blok(h, son);
    if (toplam == 128) sha1_blok(h, son + 64);
    for (int i = 0; i < 5; i++) {
        out[4 * i] = (unsigned char)(h[i] >> 24);
        out[4 * i + 1] = (unsigned char)(h[i] >> 16);
        out[4 * i + 2] = (unsigned char)(h[i] >> 8);
        out[4 * i + 3] = (unsigned char)h[i];
    }
}

void sha1_hex(const char *s, char out[41])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char d[20];
    sha1(s, strlen(s), d);
    for (int i = 0; i < 20; i++) {
        out[2 * i] = hex[d[i] >> 4];
        out[2 * i + 1] = hex[d[i] & 15];
    }
    out[40] = '\0';
}

void base64_kodla(const unsigned char *in, size_t n, char *out)
{
    static const char t[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t j = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)in[i] << 16;
        if (i + 1 < n) v |= (uint32_t)in[i + 1] << 8;
        if (i + 2 < n) v |= in[i + 2];
        out[j++] = t[(v >> 18) & 63];
        out[j++] = t[(v >> 12) & 63];
        out[j++] = (i + 1 < n) ? t[(v >> 6) & 63] : '=';
        out[j++] = (i + 2 < n) ? t[v & 63] : '=';
    }
    out[j] = '\0';
}

long long base64_coz(const char *in, unsigned char **out)
{
    size_t n = strlen(in);
    unsigned char *b = xmalloc(n / 4 * 3 + 3);
    size_t j = 0;
    uint32_t v = 0;
    int bit = 0;
    for (size_t i = 0; i < n; i++) {
        char c = in[i];
        int d = c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
              : c == '+' ? 62 : c == '/' ? 63 : c == '=' ? -2 : -1;
        if (d == -2) break;
        if (d < 0) {
            free(b);
            *out = NULL;
            return -1;
        }
        v = (v << 6) | (uint32_t)d;
        bit += 6;
        if (bit >= 8) {
            bit -= 8;
            b[j++] = (unsigned char)(v >> bit);
        }
    }
    *out = b;
    return (long long)j;
}

/* ------------------------------------------------------------------ UTF-8 */

int utf8_gecerli(const char *s, size_t n)
{
    const unsigned char *p = (const unsigned char *)s;
    size_t i = 0;
    while (i < n) {
        unsigned char c = p[i];
        if (c < 0x80) {
            i++;
            continue;
        }
        size_t k;
        uint32_t cp;
        if ((c & 0xE0) == 0xC0) {
            k = 1;
            cp = c & 0x1F;
        } else if ((c & 0xF0) == 0xE0) {
            k = 2;
            cp = c & 0x0F;
        } else if ((c & 0xF8) == 0xF0) {
            k = 3;
            cp = c & 0x07;
        } else {
            return 0;
        }
        for (size_t j = 1; j <= k; j++) {
            if (i + j >= n || (p[i + j] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (p[i + j] & 0x3F);
        }
        /* Fazladan uzun kodlamalar, vekil çiftler ve sınır dışı değerler geçersiz */
        if ((k == 1 && cp < 0x80) || (k == 2 && cp < 0x800) || (k == 3 && cp < 0x10000)) return 0;
        if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return 0;
        i += k + 1;
    }
    return 1;
}

int sabit_esit(const char *a, const char *b)
{
    /* Özetleri karşılaştırarak uzunluktan bağımsız, sabit süreli eşitlik */
    unsigned char ha[20], hb[20];
    sha1(a, strlen(a), ha);
    sha1(b, strlen(b), hb);
    unsigned char fark = 0;
    for (int i = 0; i < 20; i++) fark |= (unsigned char)(ha[i] ^ hb[i]);
    return fark == 0 && strlen(a) == strlen(b);
}

long long simdi_sn(void)
{
    return (long long)time(NULL);
}

long long simdi_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void kopyala(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    size_t m = strlen(src);
    if (m >= n) m = n - 1;
    memcpy(dst, src, m);
    dst[m] = '\0';
}
