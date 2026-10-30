#include "kesif.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define AZAMI_K 8.0       /* bir karede en fazla bu kadar hücre */
#define AZAMI_HUCRE 2048  /* bir kenarda en fazla bu kadar hücre */

static void boyutlar(double genislik, double yukseklik, int *g, int *y, double *k)
{
    double kenar = fmax(fmax(genislik, yukseklik), 1);
    *k = fmin(AZAMI_K, AZAMI_HUCRE / kenar);
    *g = (int)ceil(fmax(genislik, 1) * *k - 1e-9);
    *y = (int)ceil(fmax(yukseklik, 1) * *k - 1e-9);
    if (*g < 1) *g = 1;
    if (*y < 1) *y = 1;
}

static size_t bayt(const Kesif *k)
{
    return ((size_t)k->g * (size_t)k->y + 7) / 8;
}

void kesif_bosalt(Kesif *k)
{
    free(k->bit);
    memset(k, 0, sizeof *k);
}

void kesif_kur(Kesif *k, double genislik, double yukseklik)
{
    kesif_bosalt(k);
    boyutlar(genislik, yukseklik, &k->g, &k->y, &k->k);
    k->bit = xcalloc(bayt(k), 1);
}

int kesif_uygun(const Kesif *k, double genislik, double yukseklik)
{
    int g, y;
    double kk;
    boyutlar(genislik, yukseklik, &g, &y, &kk);
    return k->bit && k->g == g && k->y == y && k->k == kk;
}

static int al(const Kesif *k, int i, int j)
{
    size_t n = (size_t)j * (size_t)k->g + (size_t)i;
    return (k->bit[n >> 3] >> (n & 7)) & 1;
}

void kesif_olcekle(Kesif *k, double eski_g, double eski_y, double yeni_g, double yeni_y)
{
    if (!k->bit || eski_g <= 0 || eski_y <= 0) {
        kesif_kur(k, yeni_g, yeni_y);
        return;
    }
    Kesif e = *k;
    memset(k, 0, sizeof *k);
    kesif_kur(k, yeni_g, yeni_y);
    for (int j = 0; j < k->y; j++) {
        /* yeni hücrenin ortası resmin hangi kesrinde, eski ızgarada hangi hücrede */
        int ej = (int)floor((j + 0.5) / k->k / yeni_y * eski_y * e.k);
        if (ej < 0 || ej >= e.y) continue;
        for (int i = 0; i < k->g; i++) {
            int ei = (int)floor((i + 0.5) / k->k / yeni_g * eski_g * e.k);
            if (ei < 0 || ei >= e.g || !al(&e, ei, ej)) continue;
            size_t n = (size_t)j * (size_t)k->g + (size_t)i;
            k->bit[n >> 3] |= (unsigned char)(1u << (n & 7));
        }
    }
    free(e.bit);
}

int kesif_cokgen(Kesif *k, const double *xy, int n)
{
    if (!k->bit || n < 3) return 0;
    double ymin = xy[1], ymax = xy[1];
    for (int i = 1; i < n; i++) {
        ymin = fmin(ymin, xy[2 * i + 1]);
        ymax = fmax(ymax, xy[2 * i + 1]);
    }
    int j0 = (int)ceil(ymin * k->k - 0.5), j1 = (int)floor(ymax * k->k - 0.5);
    if (j0 < 0) j0 = 0;
    if (j1 > k->y - 1) j1 = k->y - 1;
    double *xs = xmalloc((size_t)n * sizeof *xs);
    int yeni = 0;
    for (int j = j0; j <= j1; j++) {
        double yc = (j + 0.5) / k->k;
        int m = 0;
        for (int i = 0; i < n; i++) {
            double x1 = xy[2 * i], y1 = xy[2 * i + 1], x2 = xy[2 * ((i + 1) % n)], y2 = xy[2 * ((i + 1) % n) + 1];
            if ((y1 <= yc && y2 > yc) || (y2 <= yc && y1 > yc)) {
                double x = x1 + (yc - y1) * (x2 - x1) / (y2 - y1);
                int s = m++;
                while (s > 0 && xs[s - 1] > x) {
                    xs[s] = xs[s - 1];
                    s--;
                }
                xs[s] = x;
            }
        }
        for (int p = 0; p + 1 < m; p += 2) {
            int i0 = (int)ceil(xs[p] * k->k - 0.5), i1 = (int)ceil(xs[p + 1] * k->k - 0.5) - 1;
            if (i0 < 0) i0 = 0;
            if (i1 > k->g - 1) i1 = k->g - 1;
            for (int i = i0; i <= i1; i++) {
                size_t b = (size_t)j * (size_t)k->g + (size_t)i;
                unsigned char m8 = (unsigned char)(1u << (b & 7));
                if (k->bit[b >> 3] & m8) continue;
                k->bit[b >> 3] |= m8;
                yeni = 1;
            }
        }
    }
    free(xs);
    return yeni;
}

int kesif_var(const Kesif *k, double x, double y)
{
    if (!k->bit || !isfinite(x) || !isfinite(y)) return 0;
    int i = (int)floor(x * k->k), j = (int)floor(y * k->k);
    if (i < 0 || j < 0 || i >= k->g || j >= k->y) return 0;
    return al(k, i, j);
}

static void sayi_ekle(Buf *b, size_t v)
{
    do {
        unsigned char c = v & 0x7f;
        v >>= 7;
        if (v) c |= 0x80;
        buf_putc(b, (char)c);
    } while (v);
}

void kesif_kodla(Buf *b, const Kesif *k)
{
    Buf ham;
    buf_init(&ham);
    size_t toplam = k->bit ? (size_t)k->g * (size_t)k->y : 0, kosu = 0;
    int deger = 0;
    for (size_t n = 0; n < toplam; n++) {
        int v = (k->bit[n >> 3] >> (n & 7)) & 1;
        if (v != deger) {
            sayi_ekle(&ham, kosu);
            kosu = 0;
            deger = v;
        }
        kosu++;
    }
    if (kosu) sayi_ekle(&ham, kosu);
    char *s = xmalloc(4 * ((ham.len + 2) / 3) + 1);
    base64_kodla((const unsigned char *)(ham.p ? ham.p : ""), ham.len, s);
    buf_puts(b, s);
    free(s);
    buf_free(&ham);
}

int kesif_coz(Kesif *k, const char *s)
{
    if (!k->bit || !s) return 0;
    unsigned char *ham;
    long long n = base64_coz(s, &ham);
    if (n < 0) return 0;
    size_t toplam = (size_t)k->g * (size_t)k->y, yer = 0;
    int deger = 0, ok = 1;
    for (long long i = 0; i < n && ok;) {
        size_t v = 0;
        int kaydir = 0;
        for (;;) {
            if (i >= n || kaydir > 56) {
                ok = 0;
                break;
            }
            unsigned char c = ham[i++];
            v |= (size_t)(c & 0x7f) << kaydir;
            kaydir += 7;
            if (!(c & 0x80)) break;
        }
        if (!ok) break;
        if (v > toplam - yer) {
            ok = 0;
            break;
        }
        if (deger)
            for (size_t q = yer; q < yer + v; q++) k->bit[q >> 3] |= (unsigned char)(1u << (q & 7));
        yer += v;
        deger = !deger;
    }
    free(ham);
    if (!ok) memset(k->bit, 0, bayt(k));
    return ok;
}
