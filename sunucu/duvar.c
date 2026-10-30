#include "duvar.h"
#include "ortak.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* "Üstünde" sayılan uzaklık (kare): bundan yakın nokta duvarın üstündedir */
#define UST 1e-7
/* İki yön aynı sayılır (birim vektörlerin çapraz çarpımı) */
#define ACI_PAYI 1e-9
/* Köşe noktaları bu kadar yakınsa aynı köşedir */
#define KOSE_ESI 1e-6
/* Görüş çokgeninde yay en fazla bu kadar açıyla bölünür (72 parça) */
#define YAY_PARCA 72
/* Görüş hesabında açı kovaları */
#define KOVA 256
/* Duvar ızgarada bu kadar çevresiyle birlikte kayıtlıdır: yakınlık sorguları bundan kısa olmalı */
#define KAYIT_PAYI (DUVAR_KOSE_PAYI + 0.05)

struct Duvarlar {
    int n, gercek;    /* parça sayısı (harita sınırı dahil) ve gerçek duvar sayısı (ilk gercek parça) */
    double *p;        /* 4n: x1,y1,x2,y2 */
    double g, y;      /* harita boyutu */
    double h;         /* ızgara hücresinin boyu */
    int hx, hy;       /* hücre sayısı */
    int *bas, *dizi;  /* hücre c'nin duvarları: dizi[bas[c]] .. dizi[bas[c+1]-1] */
    unsigned *damga;  /* sorguda aynı duvara iki kez bakılmasın */
    unsigned damga_no;
    double *kes;      /* duvarların birbirini kestiği noktalar (x,y) */
    int kes_n;
    int dn;           /* yol düğümleri: köşelerin dışında, dönülecek noktalar */
    double *dxy;
    int **komsu;      /* düğümün gördüğü düğümler (ilk gerektiğinde hesaplanır) */
    int *komsu_n;     /* -1: hesaplanmadı */
};

/* ------------------------------------------------------------------ temel geometri */

static double uz(double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    return sqrt(dx * dx + dy * dy);
}

/* P'nin AB doğrusuna işaretli uzaklığı (solda artı) */
static double yan(double ax, double ay, double bx, double by, double px, double py)
{
    double dx = bx - ax, dy = by - ay;
    double L = sqrt(dx * dx + dy * dy);
    if (L < 1e-12) return 0;
    return (dx * (py - ay) - dy * (px - ax)) / L;
}

static double nokta_parca(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    double L = dx * dx + dy * dy;
    double t = L > 0 ? ((px - ax) * dx + (py - ay) * dy) / L : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return uz(ax + t * dx, ay + t * dy, px, py);
}

/* AB ile w birbirini tam olarak (uçlar dışında) kesiyor mu */
static int capraz(double ax, double ay, double bx, double by, const double *w)
{
    double d1 = yan(w[0], w[1], w[2], w[3], ax, ay), d2 = yan(w[0], w[1], w[2], w[3], bx, by);
    if (!((d1 > UST && d2 < -UST) || (d1 < -UST && d2 > UST))) return 0;
    double d3 = yan(ax, ay, bx, by, w[0], w[1]), d4 = yan(ax, ay, bx, by, w[2], w[3]);
    return (d3 > UST && d4 < -UST) || (d3 < -UST && d4 > UST);
}

/* P, AB parçasının üstünde ve iki ucundan da uzakta mı */
static int ic_nokta(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, L = dx * dx + dy * dy;
    if (L < 1e-18) return 0;
    double t = ((px - ax) * dx + (py - ay) * dy) / L;
    if (t <= 0 || t >= 1) return 0;
    if (fabs(dx * (py - ay) - dy * (px - ax)) / sqrt(L) > UST) return 0;
    return uz(px, py, ax, ay) > UST && uz(px, py, bx, by) > UST;
}

/* w, A'dan B'ye giden çizgiyi kesiyor mu. Uçlardan biri w'nin üstündeyse kesmez (duvarın üstündeki iki yana da
   geçer); w'nin bir ucu çizginin üstündeyse keser (duvarın ucundan sıyırmak, iki duvarın birleştiği köşeden
   geçmek). */
static int engelliyor(double ax, double ay, double bx, double by, const double *w)
{
    return capraz(ax, ay, bx, by, w) || ic_nokta(w[0], w[1], ax, ay, bx, by) || ic_nokta(w[2], w[3], ax, ay, bx, by);
}

static double parca_mesafesi(double ax, double ay, double bx, double by, const double *w)
{
    if (capraz(ax, ay, bx, by, w)) return 0;
    double m = nokta_parca(ax, ay, w[0], w[1], w[2], w[3]);
    m = fmin(m, nokta_parca(bx, by, w[0], w[1], w[2], w[3]));
    m = fmin(m, nokta_parca(w[0], w[1], ax, ay, bx, by));
    return fmin(m, nokta_parca(w[2], w[3], ax, ay, bx, by));
}

/* ------------------------------------------------------------------ ızgara dizini */

static int hucre_x(const Duvarlar *d, double x)
{
    int i = (int)floor(x / d->h);
    return i < 0 ? 0 : i >= d->hx ? d->hx - 1 : i;
}

static int hucre_y(const Duvarlar *d, double y)
{
    int i = (int)floor(y / d->h);
    return i < 0 ? 0 : i >= d->hy ? d->hy - 1 : i;
}

/* Parçanın geçtiği hücreleri sırayla gezer (harita dışında kalan kısmı haritanın kenar hücrelerine düşer). */
typedef struct {
    int cx, cy, ex, ey, sx, sy, kalan, ilk;
    double tmx, tmy, tdx, tdy;
} Gezgin;

static void gezgin_kur(Gezgin *z, const Duvarlar *d, double ax, double ay, double bx, double by)
{
    double x0 = ax / d->h, y0 = ay / d->h, x1 = bx / d->h, y1 = by / d->h;
    z->cx = hucre_x(d, ax);
    z->cy = hucre_y(d, ay);
    z->ex = hucre_x(d, bx);
    z->ey = hucre_y(d, by);
    z->sx = z->ex > z->cx ? 1 : z->ex < z->cx ? -1 : 0;
    z->sy = z->ey > z->cy ? 1 : z->ey < z->cy ? -1 : 0;
    double ax_ = fabs(x1 - x0), ay_ = fabs(y1 - y0);
    z->tdx = z->sx && ax_ > 0 ? 1 / ax_ : INFINITY;
    z->tdy = z->sy && ay_ > 0 ? 1 / ay_ : INFINITY;
    z->tmx = z->sx > 0 ? (z->cx + 1 - x0) * z->tdx : z->sx < 0 ? (x0 - z->cx) * z->tdx : INFINITY;
    z->tmy = z->sy > 0 ? (z->cy + 1 - y0) * z->tdy : z->sy < 0 ? (y0 - z->cy) * z->tdy : INFINITY;
    z->kalan = abs(z->ex - z->cx) + abs(z->ey - z->cy);
    z->ilk = 1;
}

static int gezgin_adim(Gezgin *z, int *cx, int *cy)
{
    if (z->ilk) {
        z->ilk = 0;
    } else {
        if (z->kalan <= 0) return 0;
        z->kalan--;
        /* Son hücrenin satırına ya da sütununa varıldıysa yalnızca öbür yönde ilerlenir */
        int x_adim = z->cy == z->ey || (z->cx != z->ex && z->tmx < z->tmy);
        if (x_adim) {
            z->cx += z->sx;
            z->tmx += z->tdx;
        } else {
            z->cy += z->sy;
            z->tmy += z->tdy;
        }
    }
    *cx = z->cx;
    *cy = z->cy;
    return 1;
}

static void damga_yenile(Duvarlar *d)
{
    if (++d->damga_no == 0) {
        memset(d->damga, 0, (size_t)d->n * sizeof *d->damga);
        d->damga_no = 1;
    }
}

/* Hücrenin henüz bakılmamış duvarlarını out'a ekler */
static int hucre_duvarlari(Duvarlar *d, int cx, int cy, int *out, int n)
{
    int c = cy * d->hx + cx;
    for (int k = d->bas[c]; k < d->bas[c + 1]; k++) {
        int i = d->dizi[k];
        if (d->damga[i] == d->damga_no) continue;
        d->damga[i] = d->damga_no;
        out[n++] = i;
    }
    return n;
}

/* AB parçasına DUVAR_KOSE_PAYI kadar yakın olabilecek duvarlar (sıra numaraları out'a; d->n kadar yer olmalı).
   Her duvar, çevresindeki KAYIT_PAYI'lık şeritle birlikte kaplanan hücrelere kayıtlıdır; bu yüzden parçanın geçtiği
   hücrelere bakmak yeter. */
static int parca_yakini(Duvarlar *d, double ax, double ay, double bx, double by, int *out)
{
    damga_yenile(d);
    Gezgin z;
    int cx, cy, n = 0;
    gezgin_kur(&z, d, ax, ay, bx, by);
    while (gezgin_adim(&z, &cx, &cy)) n = hucre_duvarlari(d, cx, cy, out, n);
    return n;
}

static int nokta_yakini(Duvarlar *d, double x, double y, int *out)
{
    damga_yenile(d);
    return hucre_duvarlari(d, hucre_x(d, x), hucre_y(d, y), out, 0);
}

/* ------------------------------------------------------------------ kurulum */

static void dizin_kur(Duvarlar *d)
{
    double kenar = fmax(d->g, d->y);
    d->h = fmax(1.0, kenar / 128);
    d->hx = (int)ceil(d->g / d->h);
    d->hy = (int)ceil(d->y / d->h);
    if (d->hx < 1) d->hx = 1;
    if (d->hy < 1) d->hy = 1;
    int hn = d->hx * d->hy;
    int *say = xcalloc((size_t)hn + 1, sizeof *say);
    /* İki geçiş: önce say, sonra yerleştir */
    for (int gecis = 0; gecis < 2; gecis++) {
        if (gecis == 1) {
            d->bas = xcalloc((size_t)hn + 1, sizeof *d->bas);
            for (int c = 0; c < hn; c++) d->bas[c + 1] = d->bas[c] + say[c];
            d->dizi = xmalloc((size_t)(d->bas[hn] ? d->bas[hn] : 1) * sizeof *d->dizi);
            memset(say, 0, (size_t)hn * sizeof *say);
        }
        for (int i = 0; i < d->n; i++) {
            const double *w = d->p + 4 * i;
            int x0 = hucre_x(d, fmin(w[0], w[2]) - KAYIT_PAYI), x1 = hucre_x(d, fmax(w[0], w[2]) + KAYIT_PAYI);
            int y0 = hucre_y(d, fmin(w[1], w[3]) - KAYIT_PAYI), y1 = hucre_y(d, fmax(w[1], w[3]) + KAYIT_PAYI);
            for (int cy = y0; cy <= y1; cy++)
                for (int cx = x0; cx <= x1; cx++) {
                    int c = cy * d->hx + cx;
                    if (gecis == 1) d->dizi[d->bas[c] + say[c]] = i;
                    say[c]++;
                }
        }
    }
    free(say);
    d->damga = xcalloc((size_t)(d->n ? d->n : 1), sizeof *d->damga);
    d->damga_no = 0;
}

/* Duvarların birbirini kestiği noktalar: görüş çokgeninde burada en yakın duvar değişir */
static void kesisimleri_bul(Duvarlar *d)
{
    int *aday = xmalloc((size_t)(d->n ? d->n : 1) * sizeof *aday);
    int kap = 0;
    for (int i = 0; i < d->gercek; i++) {
        const double *a = d->p + 4 * i;
        int m = parca_yakini(d, a[0], a[1], a[2], a[3], aday);
        for (int k = 0; k < m; k++) {
            int j = aday[k];
            if (j <= i || j >= d->gercek) continue;
            const double *b = d->p + 4 * j;
            if (!capraz(a[0], a[1], a[2], a[3], b)) continue;
            double rx = a[2] - a[0], ry = a[3] - a[1], sx = b[2] - b[0], sy = b[3] - b[1];
            double den = rx * sy - ry * sx;
            if (fabs(den) < 1e-18) continue;
            double t = ((b[0] - a[0]) * sy - (b[1] - a[1]) * sx) / den;
            if (d->kes_n == kap) {
                kap = kap ? kap * 2 : 16;
                d->kes = xrealloc(d->kes, (size_t)kap * 2 * sizeof *d->kes);
            }
            d->kes[2 * d->kes_n] = a[0] + t * rx;
            d->kes[2 * d->kes_n + 1] = a[1] + t * ry;
            d->kes_n++;
        }
    }
    free(aday);
}

/* Yönlerin açıya göre sırası (trigonometri yok: iki dil aynı sırayı bulsun) */
static int yari(double x, double y)
{
    return y < 0 || (y == 0 && x < 0);
}

static int once_mi(const double *a, const double *b)
{
    int ya = yari(a[0], a[1]), yb = yari(b[0], b[1]);
    if (ya != yb) return ya < yb;
    return a[0] * b[1] - a[1] * b[0] > 0;
}

static int dugum_gecerli(Duvarlar *d, double px, double py, double nx, double ny, int *aday)
{
    if (nx < DUVAR_ACIKLIK || ny < DUVAR_ACIKLIK || nx > d->g - DUVAR_ACIKLIK || ny > d->y - DUVAR_ACIKLIK) return 0;
    int m = nokta_yakini(d, nx, ny, aday);
    for (int k = 0; k < m; k++) {
        const double *w = d->p + 4 * aday[k];
        if (nokta_parca(nx, ny, w[0], w[1], w[2], w[3]) < DUVAR_ACIKLIK + 1e-6) return 0;
    }
    m = parca_yakini(d, px, py, nx, ny, aday);
    for (int k = 0; k < m; k++)
        if (capraz(px, py, nx, ny, d->p + 4 * aday[k])) return 0;
    return 1;
}

static void dugum_ekle(Duvarlar *d, double px, double py, double vx, double vy, int *kap, int *aday)
{
    double L = sqrt(vx * vx + vy * vy);
    if (L < 1e-12) return;
    vx /= L;
    vy /= L;
    double nx = px + DUVAR_KOSE_PAYI * vx, ny = py + DUVAR_KOSE_PAYI * vy;
    if (!dugum_gecerli(d, px, py, nx, ny, aday)) {
        nx = px + DUVAR_KOSE_PAYI_DAR * vx;
        ny = py + DUVAR_KOSE_PAYI_DAR * vy;
        if (!dugum_gecerli(d, px, py, nx, ny, aday)) return;
    }
    if (d->dn == *kap) {
        *kap = *kap ? *kap * 2 : 64;
        d->dxy = xrealloc(d->dxy, (size_t)*kap * 2 * sizeof *d->dxy);
    }
    d->dxy[2 * d->dn] = nx;
    d->dxy[2 * d->dn + 1] = ny;
    d->dn++;
}

/* Köşeler: duvar uçları. Bir köşede birleşen duvarların arasında kalan, 180 dereceden geniş her boşlukta yol köşeyi
   dönebilir; oraya düğümler konur (boşluğun ortasına, çok genişse iki yanına da). */
static void dugumleri_kur(Duvarlar *d)
{
    int *aday = xmalloc((size_t)(d->n ? d->n : 1) * sizeof *aday);
    int kap = 0;
    for (int i = 0; i < d->gercek; i++) {
        for (int e = 0; e < 2; e++) {
            double px = d->p[4 * i + 2 * e], py = d->p[4 * i + 2 * e + 1];
            int m = nokta_yakini(d, px, py, aday);
            /* Bu köşeye daha önce (daha küçük bir uçtan) bakıldıysa atla */
            int bakildi = 0;
            for (int k = 0; k < m && !bakildi; k++) {
                int j = aday[k];
                if (j >= d->gercek) continue;
                for (int f = 0; f < 2; f++)
                    if (2 * j + f < 2 * i + e && uz(px, py, d->p[4 * j + 2 * f], d->p[4 * j + 2 * f + 1]) < KOSE_ESI) bakildi = 1;
            }
            if (bakildi) continue;
            /* Köşeden çıkan yönler */
            double yon[64][2];
            int yn = 0;
            for (int k = 0; k < m && yn < 62; k++) {
                const double *w = d->p + 4 * aday[k];
                double v[2][2];
                int vn = 0;
                if (uz(px, py, w[0], w[1]) < KOSE_ESI) {
                    v[vn][0] = w[2] - px;
                    v[vn++][1] = w[3] - py;
                } else if (uz(px, py, w[2], w[3]) < KOSE_ESI) {
                    v[vn][0] = w[0] - px;
                    v[vn++][1] = w[1] - py;
                } else if (nokta_parca(px, py, w[0], w[1], w[2], w[3]) < KOSE_ESI) {
                    v[vn][0] = w[2] - w[0];
                    v[vn++][1] = w[3] - w[1];
                    v[vn][0] = w[0] - w[2];
                    v[vn++][1] = w[1] - w[3];
                }
                for (int q = 0; q < vn; q++) {
                    double L = sqrt(v[q][0] * v[q][0] + v[q][1] * v[q][1]);
                    if (L < KOSE_ESI) continue;
                    double ux = v[q][0] / L, uy = v[q][1] / L;
                    int ayni = 0;
                    for (int r = 0; r < yn; r++)
                        if (fabs(ux * yon[r][1] - uy * yon[r][0]) < ACI_PAYI && ux * yon[r][0] + uy * yon[r][1] > 0) ayni = 1;
                    if (ayni) continue;
                    /* Açıya göre sıralı yerine koy */
                    int s = yn;
                    double u[2] = { ux, uy };
                    while (s > 0 && once_mi(u, yon[s - 1])) {
                        yon[s][0] = yon[s - 1][0];
                        yon[s][1] = yon[s - 1][1];
                        s--;
                    }
                    yon[s][0] = ux;
                    yon[s][1] = uy;
                    yn++;
                }
            }
            if (yn == 0) continue;
            if (yn == 1) {
                /* Serbest uç: iki yanında, ucun biraz ötesinde (duvarın tersine 45 derece) */
                double ax = yon[0][0], ay = yon[0][1];
                dugum_ekle(d, px, py, -ax - ay, -ay + ax, &kap, aday);
                dugum_ekle(d, px, py, -ax + ay, -ay - ax, &kap, aday);
                continue;
            }
            for (int s = 0; s < yn; s++) {
                const double *a = yon[s], *b = yon[(s + 1) % yn];
                double x = a[0] * b[0] + a[1] * b[1], y = a[0] * b[1] - a[1] * b[0];
                if (y >= -ACI_PAYI) continue; /* a'dan b'ye boşluk 180 dereceden dar */
                double bx = -(a[0] + b[0]), by = -(a[1] + b[1]);
                double L = sqrt(bx * bx + by * by);
                if (L < 1e-12) continue;
                bx /= L;
                by /= L;
                if (x > 0) {
                    /* 270 dereceden geniş: ortayla iki yanın arasına iki düğüm */
                    dugum_ekle(d, px, py, bx - a[1], by + a[0], &kap, aday);
                    dugum_ekle(d, px, py, bx + b[1], by - b[0], &kap, aday);
                } else {
                    dugum_ekle(d, px, py, bx, by, &kap, aday);
                }
            }
        }
    }
    free(aday);
    d->komsu = xcalloc((size_t)(d->dn ? d->dn : 1), sizeof *d->komsu);
    d->komsu_n = xmalloc((size_t)(d->dn ? d->dn : 1) * sizeof *d->komsu_n);
    for (int i = 0; i < d->dn; i++) d->komsu_n[i] = -1;
}

Duvarlar *duvarlar_kur(const double *parca, int n, double g, double y)
{
    Duvarlar *d = xcalloc(1, sizeof *d);
    d->g = g > 0 ? g : 1;
    d->y = y > 0 ? y : 1;
    d->gercek = n;
    d->n = n + 4;
    d->p = xmalloc((size_t)d->n * 4 * sizeof *d->p);
    if (n) memcpy(d->p, parca, (size_t)n * 4 * sizeof *d->p);
    const double sinir[16] = { 0, 0, d->g, 0, d->g, 0, d->g, d->y, d->g, d->y, 0, d->y, 0, d->y, 0, 0 };
    memcpy(d->p + 4 * n, sinir, sizeof sinir);
    dizin_kur(d);
    kesisimleri_bul(d);
    dugumleri_kur(d);
    return d;
}

void duvarlar_sil(Duvarlar *d)
{
    if (!d) return;
    for (int i = 0; i < d->dn; i++) free(d->komsu[i]);
    free(d->komsu);
    free(d->komsu_n);
    free(d->dxy);
    free(d->kes);
    free(d->damga);
    free(d->bas);
    free(d->dizi);
    free(d->p);
    free(d);
}

/* ------------------------------------------------------------------ görüş */

int duvar_gorus_acik(Duvarlar *d, double ax, double ay, double bx, double by)
{
    if (!d || !d->gercek) return 1;
    int *aday = xmalloc((size_t)d->n * sizeof *aday);
    int m = parca_yakini(d, ax, ay, bx, by, aday), acik = 1;
    for (int k = 0; k < m && acik; k++)
        if (aday[k] < d->gercek && engelliyor(ax, ay, bx, by, d->p + 4 * aday[k])) acik = 0;
    free(aday);
    return acik;
}

static int dbl_karsilastir(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* O'dan u yönündeki ışının w'nin doğrusunu kestiği uzaklık (kesmiyorsa -1) */
static double isin_dogru(double ox, double oy, double ux, double uy, const double *w)
{
    double sx = w[2] - w[0], sy = w[3] - w[1];
    double den = ux * sy - uy * sx;
    if (fabs(den) < 1e-15) return -1;
    return ((w[0] - ox) * sy - (w[1] - oy) * sx) / den;
}

/* Işın parçayı kesiyorsa uzaklığı, kesmiyorsa -1 */
static double isin_parca(double ox, double oy, double ux, double uy, const double *w)
{
    double sx = w[2] - w[0], sy = w[3] - w[1];
    double den = ux * sy - uy * sx;
    if (fabs(den) < 1e-15) return -1;
    double t = ((w[0] - ox) * sy - (w[1] - oy) * sx) / den;
    double s = ((w[0] - ox) * uy - (w[1] - oy) * ux) / den;
    if (t < 0 || s < 0 || s > 1) return -1;
    return t;
}

static int kova_no(double a)
{
    int k = (int)floor((a + M_PI) / (2 * M_PI) * KOVA);
    return ((k % KOVA) + KOVA) % KOVA;
}

/* Bir noktadan bakınca çevrenin açısal aralıkları. aci[0..n-1] sıralı kritik açılar (-π..π); aralık i, aci[i]'den
   bir sonrakine (sonuncusu başa sarar) uzanır ve içindeki ışınların r içinde ilk çarptığı duvar duvar[i]'dir (-1:
   yok). Kritik açılar duvar uçları, duvarların daireyi ve birbirini kestiği yerler ve (r sonluysa) yayın bölümleridir:
   bir aralığın içinde en yakın duvar değişmez. Gözlemcinin tam üstünde durduğu duvar görüşü kesmez. */
typedef struct {
    double *aci;
    int *duvar;
    int n;
} Araliklar;

static void araliklari_kur(Duvarlar *d, double ox, double oy, double r, Araliklar *a)
{
    int yay = isfinite(r);
    int *aday = NULL, an = 0;
    if (d && d->gercek) {
        aday = xmalloc((size_t)d->gercek * sizeof *aday);
        damga_yenile(d);
        int x0 = 0, x1 = d->hx - 1, y0 = 0, y1 = d->hy - 1;
        if (yay) {
            x0 = hucre_x(d, ox - r) - 1;
            x1 = hucre_x(d, ox + r) + 1;
            y0 = hucre_y(d, oy - r) - 1;
            y1 = hucre_y(d, oy + r) + 1;
        }
        for (int cy = y0 < 0 ? 0 : y0; cy <= y1 && cy < d->hy; cy++)
            for (int cx = x0 < 0 ? 0 : x0; cx <= x1 && cx < d->hx; cx++) {
                int c = cy * d->hx + cx;
                for (int k = d->bas[c]; k < d->bas[c + 1]; k++) {
                    int i = d->dizi[k];
                    if (i >= d->gercek || d->damga[i] == d->damga_no) continue;
                    d->damga[i] = d->damga_no;
                    const double *w = d->p + 4 * i;
                    double m = nokta_parca(ox, oy, w[0], w[1], w[2], w[3]);
                    if (m <= r && m > UST) aday[an++] = i;
                }
            }
    }
    int kap = (yay ? YAY_PARCA : 0) + 4 * an + (d ? d->kes_n : 0) + 4;
    double *aci = xmalloc((size_t)kap * sizeof *aci);
    int n = 0;
    for (int i = 0; yay && i < YAY_PARCA; i++) aci[n++] = -M_PI + 2 * M_PI * i / YAY_PARCA;
    double *uc = xmalloc((size_t)(an ? an : 1) * 2 * sizeof *uc); /* adayların uçlarının açıları */
    for (int k = 0; k < an; k++) {
        const double *w = d->p + 4 * aday[k];
        for (int e = 0; e < 2; e++) {
            uc[2 * k + e] = atan2(w[2 * e + 1] - oy, w[2 * e] - ox);
            if (!yay || uz(ox, oy, w[2 * e], w[2 * e + 1]) <= r) aci[n++] = uc[2 * k + e];
        }
        if (!yay) continue;
        double dx = w[2] - w[0], dy = w[3] - w[1], fx = w[0] - ox, fy = w[1] - oy;
        double qa = dx * dx + dy * dy, qb = 2 * (fx * dx + fy * dy), qc = fx * fx + fy * fy - r * r;
        double disk = qb * qb - 4 * qa * qc;
        if (qa > 0 && disk > 0) {
            double kok = sqrt(disk);
            for (int s = -1; s <= 1; s += 2) {
                double t = (-qb + s * kok) / (2 * qa);
                if (t > 0 && t < 1) aci[n++] = atan2(w[1] + t * dy - oy, w[0] + t * dx - ox);
            }
        }
    }
    for (int k = 0; an && k < d->kes_n; k++)
        if (uz(ox, oy, d->kes[2 * k], d->kes[2 * k + 1]) <= r) aci[n++] = atan2(d->kes[2 * k + 1] - oy, d->kes[2 * k] - ox);
    qsort(aci, (size_t)n, sizeof *aci, dbl_karsilastir);
    int u = 0;
    for (int i = 0; i < n; i++)
        if (!u || aci[i] - aci[u - 1] > 1e-9) aci[u++] = aci[i];
    n = u;
    if (n > 1 && aci[n - 1] - aci[0] > 2 * M_PI - 1e-9) n--;

    /* Açı kovaları: bir ışın yalnızca açısal aralığı kendi kovasına düşen duvarlara bakar */
    int *kova_bas = NULL, *kova = NULL;
    if (an) {
        int *say = xcalloc(KOVA + 1, sizeof *say);
        int(*ara)[2] = xmalloc((size_t)an * sizeof *ara);
        for (int k = 0; k < an; k++) {
            double a1 = uc[2 * k], a2 = uc[2 * k + 1];
            double fark = a2 - a1;
            while (fark > M_PI) fark -= 2 * M_PI;
            while (fark < -M_PI) fark += 2 * M_PI;
            int k0 = kova_no(fark >= 0 ? a1 : a2), adet = (int)floor(fabs(fark) / (2 * M_PI) * KOVA) + 2;
            if (adet > KOVA) adet = KOVA;
            ara[k][0] = k0;
            ara[k][1] = adet;
            for (int q = 0; q < adet; q++) say[(k0 + q) % KOVA]++;
        }
        kova_bas = xcalloc(KOVA + 1, sizeof *kova_bas);
        for (int q = 0; q < KOVA; q++) kova_bas[q + 1] = kova_bas[q] + say[q];
        kova = xmalloc((size_t)(kova_bas[KOVA] ? kova_bas[KOVA] : 1) * sizeof *kova);
        memset(say, 0, KOVA * sizeof *say);
        for (int k = 0; k < an; k++)
            for (int q = 0; q < ara[k][1]; q++) {
                int kk = (ara[k][0] + q) % KOVA;
                kova[kova_bas[kk] + say[kk]++] = k;
            }
        free(say);
        free(ara);
    }
    int *duvar = xmalloc((size_t)(n ? n : 1) * sizeof *duvar);
    for (int i = 0; i < n; i++) {
        double a0 = aci[i], a1 = i + 1 < n ? aci[i + 1] : aci[0] + 2 * M_PI, orta = (a0 + a1) / 2;
        double ux = cos(orta), uy = sin(orta), en = r;
        duvar[i] = -1;
        if (!an) continue;
        int kk = kova_no(orta);
        for (int q = kova_bas[kk]; q < kova_bas[kk + 1]; q++) {
            double t = isin_parca(ox, oy, ux, uy, d->p + 4 * aday[kova[q]]);
            if (t >= 0 && t < en) {
                en = t;
                duvar[i] = aday[kova[q]];
            }
        }
    }
    free(uc);
    free(aday);
    free(kova_bas);
    free(kova);
    a->aci = aci;
    a->duvar = duvar;
    a->n = n;
}

static void araliklar_bosalt(Araliklar *a)
{
    free(a->aci);
    free(a->duvar);
}

int duvar_gorus_cokgeni(Duvarlar *d, double ox, double oy, double r, double **xy, unsigned char *gorulen)
{
    *xy = NULL;
    if (!(r > 0) || !isfinite(r)) return 0;
    Araliklar a;
    araliklari_kur(d, ox, oy, r, &a);
    double *p = xmalloc((size_t)(2 * a.n + 2) * 2 * sizeof *p);
    int pn = 0;
    for (int i = 0; i < a.n; i++) {
        double a0 = a.aci[i], a1 = i + 1 < a.n ? a.aci[i + 1] : a.aci[0] + 2 * M_PI;
        double u0x = cos(a0), u0y = sin(a0), u1x = cos(a1), u1y = sin(a1), t0 = r, t1 = r;
        if (a.duvar[i] >= 0) {
            const double *w = d->p + 4 * a.duvar[i];
            t0 = isin_dogru(ox, oy, u0x, u0y, w);
            t1 = isin_dogru(ox, oy, u1x, u1y, w);
            t0 = t0 < 0 ? 0 : t0 > r ? r : t0;
            t1 = t1 < 0 ? 0 : t1 > r ? r : t1;
            if (gorulen) gorulen[a.duvar[i]] = 1;
        }
        double nk[2][2] = { { ox + t0 * u0x, oy + t0 * u0y }, { ox + t1 * u1x, oy + t1 * u1y } };
        for (int q = 0; q < 2; q++) {
            if (pn && fabs(p[2 * pn - 2] - nk[q][0]) < 1e-9 && fabs(p[2 * pn - 1] - nk[q][1]) < 1e-9) continue;
            p[2 * pn] = nk[q][0];
            p[2 * pn + 1] = nk[q][1];
            pn++;
        }
    }
    if (pn > 1 && fabs(p[0] - p[2 * pn - 2]) < 1e-9 && fabs(p[1] - p[2 * pn - 1]) < 1e-9) pn--;
    araliklar_bosalt(&a);
    *xy = p;
    return pn;
}

/* V, aralık i'deki en yakın duvardan önce mi (ya da o aralıkta duvar yok mu) */
static int aralikta_gorunur(const Duvarlar *d, const Araliklar *a, int i, double ox, double oy, double vx, double vy)
{
    if (a->duvar[i] < 0) return 1;
    double L = uz(ox, oy, vx, vy);
    if (L < 1e-12) return 1;
    double t = isin_dogru(ox, oy, (vx - ox) / L, (vy - oy) / L, d->p + 4 * a->duvar[i]);
    return t < 0 || L <= t + 1e-6;
}

/* O'dan görülen düğümler (out'a). Kenarın yürünebilirliğine ayrıca bakılır; bu yalnızca bir ön eleme. */
static int gorunen_dugumler(Duvarlar *d, double ox, double oy, int haric, int *out)
{
    Araliklar a;
    araliklari_kur(d, ox, oy, INFINITY, &a);
    int m = 0;
    for (int v = 0; v < d->dn; v++) {
        if (v == haric) continue;
        double vx = d->dxy[2 * v], vy = d->dxy[2 * v + 1];
        if (a.n == 0) {
            out[m++] = v;
            continue;
        }
        double t = atan2(vy - oy, vx - ox);
        int lo = 0, hi = a.n - 1, i = a.n - 1;
        while (lo <= hi) {
            int o = (lo + hi) / 2;
            if (a.aci[o] <= t) {
                i = o;
                lo = o + 1;
            } else {
                hi = o - 1;
            }
        }
        if (t < a.aci[0]) i = a.n - 1;
        int gor = aralikta_gorunur(d, &a, i, ox, oy, vx, vy);
        /* Kritik açının tam üstündeyse komşu aralığa da bakılır */
        if (!gor && fabs(t - a.aci[i]) < 1e-7) gor = aralikta_gorunur(d, &a, (i + a.n - 1) % a.n, ox, oy, vx, vy);
        if (!gor && fabs((i + 1 < a.n ? a.aci[i + 1] : a.aci[0] + 2 * M_PI) - t) < 1e-7)
            gor = aralikta_gorunur(d, &a, (i + 1) % a.n, ox, oy, vx, vy);
        if (gor) out[m++] = v;
    }
    araliklar_bosalt(&a);
    return m;
}

/* f(P) = f0 + t (f1 - f0) >= 0 olan t aralığını [lo, hi]'ye kesiştirir */
static void yari_duzlem(double f0, double f1, double *lo, double *hi)
{
    if (f0 >= 0 && f1 >= 0) return;
    if (f0 < 0 && f1 < 0) {
        *lo = 1;
        *hi = 0;
        return;
    }
    double t = f0 / (f0 - f1);
    if (f0 < 0) *lo = fmax(*lo, t);
    else *hi = fmin(*hi, t);
}

static double yon3(double ax, double ay, double bx, double by, double px, double py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

static int aralik_karsilastir(const void *a, const void *b)
{
    const double *x = a, *y = b;
    return (x[0] > y[0]) - (x[0] < y[0]);
}

int duvar_gorunen(Duvarlar *d, double vx, double vy, double r, double ax, double ay, double bx, double by, double *t,
                  int azami)
{
    double t0, t1;
    {
        /* Görüş dairesinin içinde kalan kısım */
        double dx = bx - ax, dy = by - ay, fx = ax - vx, fy = ay - vy;
        double a = dx * dx + dy * dy, c = fx * fx + fy * fy - r * r;
        if (a < 1e-18) {
            if (c > 1e-9) return 0;
            t0 = 0;
            t1 = 1;
        } else {
            double b = 2 * (fx * dx + fy * dy), disk = b * b - 4 * a * c;
            if (disk < 0) return 0;
            double kok = sqrt(disk);
            t0 = fmax(0, (-b - kok) / (2 * a));
            t1 = fmin(1, (-b + kok) / (2 * a));
            if (t1 - t0 < 1e-9) return 0;
        }
    }
    /* Duvarların gölgeleri: V'den bakınca duvarın ardında, iki ucunun arasındaki koni */
    int an = 0, gn = 0;
    int *aday = d && d->gercek ? xmalloc((size_t)d->n * sizeof *aday) : NULL;
    double(*golge)[2] = NULL;
    if (aday) {
        damga_yenile(d);
        int x0 = hucre_x(d, vx - r) - 1, x1 = hucre_x(d, vx + r) + 1, y0 = hucre_y(d, vy - r) - 1, y1 = hucre_y(d, vy + r) + 1;
        for (int cy = y0 < 0 ? 0 : y0; cy <= y1 && cy < d->hy; cy++)
            for (int cx = x0 < 0 ? 0 : x0; cx <= x1 && cx < d->hx; cx++) an = hucre_duvarlari(d, cx, cy, aday, an);
        golge = xmalloc((size_t)(an ? an : 1) * sizeof *golge);
    }
    for (int k = 0; k < an; k++) {
        if (aday[k] >= d->gercek) continue;
        const double *w = d->p + 4 * aday[k];
        double sv = yon3(w[0], w[1], w[2], w[3], vx, vy);
        if (fabs(sv) < 1e-12 || nokta_parca(vx, vy, w[0], w[1], w[2], w[3]) <= UST) continue;
        double lo = t0, hi = t1;
        /* duvarın öbür yanı */
        yari_duzlem(-sv * yon3(w[0], w[1], w[2], w[3], ax, ay), -sv * yon3(w[0], w[1], w[2], w[3], bx, by), &lo, &hi);
        /* koninin iki kenarı */
        double s1 = yon3(vx, vy, w[0], w[1], w[2], w[3]) > 0 ? 1 : -1, s2 = -s1;
        yari_duzlem(s1 * yon3(vx, vy, w[0], w[1], ax, ay), s1 * yon3(vx, vy, w[0], w[1], bx, by), &lo, &hi);
        yari_duzlem(s2 * yon3(vx, vy, w[2], w[3], ax, ay), s2 * yon3(vx, vy, w[2], w[3], bx, by), &lo, &hi);
        if (hi - lo > 1e-9) {
            golge[gn][0] = lo;
            golge[gn][1] = hi;
            gn++;
        }
    }
    qsort(golge, (size_t)gn, sizeof *golge, aralik_karsilastir);
    int n = 0;
    double bas = t0;
    for (int k = 0; k <= gn && n < azami; k++) {
        double son = k < gn ? golge[k][0] : t1;
        if (son - bas > 1e-6) {
            t[2 * n] = bas;
            t[2 * n + 1] = fmin(son, t1);
            n++;
        }
        if (k < gn && golge[k][1] > bas) bas = golge[k][1];
        if (bas >= t1) break;
    }
    free(aday);
    free(golge);
    return n;
}

/* ------------------------------------------------------------------ yol */

/* AB kenarı yürünebilir mi: hiçbir duvarı kesmez ve duvarlara DUVAR_ACIKLIK'tan fazla yaklaşmaz. a_uc / b_uc: o
   nokta yolun başı ya da sonuysa ve bir duvara zaten yakınsa, kenar o duvara ondan daha çok yaklaşmasın yeter. */
static int kenar_gecerli(Duvarlar *d, double ax, double ay, int a_uc, double bx, double by, int b_uc, int *aday)
{
    int m = parca_yakini(d, ax, ay, bx, by, aday);
    for (int k = 0; k < m; k++) {
        const double *w = d->p + 4 * aday[k];
        if (engelliyor(ax, ay, bx, by, w)) return 0;
        double gerek = DUVAR_ACIKLIK;
        if (a_uc) gerek = fmin(gerek, nokta_parca(ax, ay, w[0], w[1], w[2], w[3]) - 1e-9);
        if (b_uc) gerek = fmin(gerek, nokta_parca(bx, by, w[0], w[1], w[2], w[3]) - 1e-9);
        if (gerek > 0 && parca_mesafesi(ax, ay, bx, by, w) < gerek) return 0;
    }
    return 1;
}

/* (x,y)'ye en yakın gerçek duvar (harita sınırı sayılmaz) ve uzaklığı. KAYIT_PAYI'ndan uzaktakiler yok sayılır: yakında
   duvar yoksa -1 döner, uzaklık KAYIT_PAYI olur. */
static int en_yakin_duvar(Duvarlar *d, double x, double y, double *u, int *aday)
{
    int m = nokta_yakini(d, x, y, aday), en = -1;
    *u = KAYIT_PAYI;
    for (int k = 0; k < m; k++) {
        int i = aday[k];
        if (i >= d->gercek) continue;
        const double *w = d->p + 4 * i;
        double v = nokta_parca(x, y, w[0], w[1], w[2], w[3]);
        if (v < *u || (v == *u && en >= 0 && i < en)) {
            *u = v;
            en = i;
        }
    }
    return en;
}

/* B, w duvarından DUVAR_UC_PAYI uzağa itilir: tam duvarın üstündeyse A'dan geldiği yana, değilse kendi yanına.
   Yön belli değilse (A da duvarın doğrusunda) 0 döner. */
static int duvardan_it(const double *w, double ax, double ay, double *bx, double *by)
{
    double dx = w[2] - w[0], dy = w[3] - w[1], L2 = dx * dx + dy * dy;
    double t = L2 > 0 ? ((*bx - w[0]) * dx + (*by - w[1]) * dy) / L2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double nx = w[0] + t * dx, ny = w[1] + t * dy; /* duvarın B'ye en yakın noktası */
    double ux = *bx - nx, uy = *by - ny, u = sqrt(ux * ux + uy * uy);
    if (u > UST) {
        ux /= u;
        uy /= u;
    } else if (t > 0 && t < 1) {
        double s = yan(w[0], w[1], w[2], w[3], ax, ay), L = sqrt(L2);
        if (fabs(s) <= UST) return 0;
        ux = (s > 0 ? -dy : dy) / L;
        uy = (s > 0 ? dx : -dx) / L;
    } else {
        ux = ax - *bx;
        uy = ay - *by;
        u = sqrt(ux * ux + uy * uy);
        if (u <= UST) return 0;
        ux /= u;
        uy /= u;
    }
    *bx = nx + ux * DUVAR_UC_PAYI;
    *by = ny + uy * DUVAR_UC_PAYI;
    return 1;
}

/* Yolun son kenarı A'dan B'ye. Karakter duvarın üstünde ya da dibinde durmasın: duvarın üstündeki nokta iki yana da
   çıkabilir, konumun yuvarlanması da onu öbür yana geçirebilir. B duvarlara DUVAR_UC_PAYI / 2'den yakınsa duvardan
   uzağa itilir; itilen yere A'dan gidilemiyorsa B, AB üstünde duvarlardan o kadar uzak ilk noktaya geri çekilir (A
   duvara daha yakınsa A kadar uzak). a_uc: A yolun başı mı. */
static void uca_cek(Duvarlar *d, double ax, double ay, int a_uc, double *bx, double *by, int *aday)
{
    double u, au;
    int w = en_yakin_duvar(d, *bx, *by, &u, aday);
    if (w < 0 || u >= DUVAR_UC_PAYI / 2) return;
    en_yakin_duvar(d, ax, ay, &au, aday);
    double gerek = fmin(DUVAR_UC_PAYI / 2, au);
    if (gerek <= UST) return; /* A da duvarın üstünde: hangi yanda olduğu zaten belli değil */
    double x = *bx, y = *by;
    for (int k = 0; k < 4 && w >= 0 && u < gerek; k++) {
        if (!duvardan_it(d->p + 4 * w, ax, ay, &x, &y)) break;
        w = en_yakin_duvar(d, x, y, &u, aday);
    }
    if (u >= gerek && x >= 0 && y >= 0 && x <= d->g && y <= d->y && kenar_gecerli(d, ax, ay, a_uc, x, y, 1, aday)) {
        *bx = x;
        *by = y;
        return;
    }
    double lo = 0, hi = 1; /* B'den A'ya: hi'deki nokta duvarlardan yeterince uzak */
    for (int k = 0; k < 48; k++) {
        double t = (lo + hi) / 2;
        en_yakin_duvar(d, *bx + (ax - *bx) * t, *by + (ay - *by) * t, &u, aday);
        if (u >= gerek) hi = t;
        else lo = t;
    }
    x = *bx + (ax - *bx) * hi;
    y = *by + (ay - *by) * hi;
    *bx = x;
    *by = y;
}

static const int *komsular(Duvarlar *d, int u, int *n, int *aday)
{
    if (d->komsu_n[u] < 0) {
        double ux = d->dxy[2 * u], uy = d->dxy[2 * u + 1];
        int *l = xmalloc((size_t)(d->dn ? d->dn : 1) * sizeof *l);
        int g = gorunen_dugumler(d, ux, uy, u, l), m = 0;
        for (int k = 0; k < g; k++) {
            int v = l[k];
            if (kenar_gecerli(d, ux, uy, 0, d->dxy[2 * v], d->dxy[2 * v + 1], 0, aday)) l[m++] = v;
        }
        d->komsu[u] = m ? xrealloc(l, (size_t)m * sizeof *l) : (free(l), NULL);
        d->komsu_n[u] = m;
    }
    *n = d->komsu_n[u];
    return d->komsu[u];
}

typedef struct {
    double f;
    int i;
} Oge;

typedef struct {
    Oge *o;
    int n, kap;
} Yigin;

static int oge_once(const Oge *a, const Oge *b)
{
    return a->f < b->f || (a->f == b->f && a->i < b->i);
}

static void yigin_it(Yigin *y, double f, int i)
{
    if (y->n == y->kap) {
        y->kap = y->kap ? y->kap * 2 : 64;
        y->o = xrealloc(y->o, (size_t)y->kap * sizeof *y->o);
    }
    int k = y->n++;
    y->o[k] = (Oge){ f, i };
    while (k > 0) {
        int ust = (k - 1) / 2;
        if (!oge_once(&y->o[k], &y->o[ust])) break;
        Oge t = y->o[k];
        y->o[k] = y->o[ust];
        y->o[ust] = t;
        k = ust;
    }
}

static int yigin_al(Yigin *y, Oge *out)
{
    if (!y->n) return 0;
    *out = y->o[0];
    y->o[0] = y->o[--y->n];
    int k = 0;
    for (;;) {
        int a = 2 * k + 1, b = a + 1, en = k;
        if (a < y->n && oge_once(&y->o[a], &y->o[en])) en = a;
        if (b < y->n && oge_once(&y->o[b], &y->o[en])) en = b;
        if (en == k) break;
        Oge t = y->o[k];
        y->o[k] = y->o[en];
        y->o[en] = t;
        k = en;
    }
    return 1;
}

void yol_bosalt(Yol *y)
{
    free(y->xy);
    y->xy = NULL;
    y->n = 0;
    y->uzunluk = 0;
}

void yol_duz(Yol *y, double ax, double ay, double bx, double by)
{
    y->xy = xmalloc(4 * sizeof *y->xy);
    y->xy[0] = ax;
    y->xy[1] = ay;
    y->xy[2] = bx;
    y->xy[3] = by;
    y->n = 2;
    y->uzunluk = uz(ax, ay, bx, by);
}

void yol_kopyala(Yol *hedef, const Yol *kaynak)
{
    hedef->n = kaynak->n;
    hedef->uzunluk = kaynak->uzunluk;
    hedef->xy = xmalloc((size_t)(kaynak->n ? kaynak->n : 1) * 2 * sizeof *hedef->xy);
    if (kaynak->n) memcpy(hedef->xy, kaynak->xy, (size_t)kaynak->n * 2 * sizeof *hedef->xy);
}

int duvar_yol_bul(Duvarlar *d, double sx, double sy, double gx, double gy, Yol *yol)
{
    memset(yol, 0, sizeof *yol);
    int *aday = xmalloc((size_t)d->n * sizeof *aday);
    if (uz(sx, sy, gx, gy) < 1e-9 || kenar_gecerli(d, sx, sy, 1, gx, gy, 1, aday)) {
        if (uz(sx, sy, gx, gy) >= 1e-9) uca_cek(d, sx, sy, 1, &gx, &gy, aday);
        free(aday);
        yol_duz(yol, sx, sy, gx, gy);
        return 1;
    }
    int V = d->dn, S = V, G = V + 1;
    double *g = xmalloc((size_t)(V + 2) * sizeof *g);
    int *ata = xmalloc((size_t)(V + 2) * sizeof *ata);
    unsigned char *kapali = xcalloc((size_t)V + 2, 1);
    for (int i = 0; i < V + 2; i++) {
        g[i] = INFINITY;
        ata[i] = -1;
    }
    g[S] = 0;
    Yigin y = { 0 };
#define DX(i) ((i) == S ? sx : (i) == G ? gx : d->dxy[2 * (i)])
#define DY(i) ((i) == S ? sy : (i) == G ? gy : d->dxy[2 * (i) + 1])
    int *gorunen = xmalloc((size_t)(V ? V : 1) * sizeof *gorunen);
    int gn = gorunen_dugumler(d, sx, sy, -1, gorunen);
    for (int k = 0; k < gn; k++) {
        int v = gorunen[k];
        if (!kenar_gecerli(d, sx, sy, 1, DX(v), DY(v), 0, aday)) continue;
        g[v] = uz(sx, sy, DX(v), DY(v));
        ata[v] = S;
        yigin_it(&y, g[v] + uz(DX(v), DY(v), gx, gy), v);
    }
    free(gorunen);
    Oge o;
    while (yigin_al(&y, &o)) {
        int u = o.i;
        if (kapali[u]) continue;
        if (u == G) break;
        kapali[u] = 1;
        if (kenar_gecerli(d, DX(u), DY(u), 0, gx, gy, 1, aday)) {
            double c = g[u] + uz(DX(u), DY(u), gx, gy);
            if (c < g[G]) {
                g[G] = c;
                ata[G] = u;
                yigin_it(&y, c, G);
            }
        }
        int kn;
        const int *k = komsular(d, u, &kn, aday);
        for (int q = 0; q < kn; q++) {
            int v = k[q];
            if (kapali[v]) continue;
            double c = g[u] + uz(DX(u), DY(u), DX(v), DY(v));
            if (c < g[v]) {
                g[v] = c;
                ata[v] = u;
                yigin_it(&y, c + uz(DX(v), DY(v), gx, gy), v);
            }
        }
    }
    int bulundu = ata[G] >= 0;
    if (bulundu) {
        int adet = 1;
        for (int i = G; i != S; i = ata[i]) adet++;
        yol->xy = xmalloc((size_t)adet * 2 * sizeof *yol->xy);
        yol->n = adet;
        int k = adet - 1;
        for (int i = G;; i = ata[i]) {
            yol->xy[2 * k] = DX(i);
            yol->xy[2 * k + 1] = DY(i);
            k--;
            if (i == S) break;
        }
        int p = ata[G];
        double *son = yol->xy + 2 * adet - 2;
        uca_cek(d, DX(p), DY(p), p == S, &son[0], &son[1], aday);
        yol->uzunluk = g[p] + uz(DX(p), DY(p), son[0], son[1]);
    }
#undef DX
#undef DY
    free(y.o);
    free(g);
    free(ata);
    free(kapali);
    free(aday);
    return bulundu;
}

void duvar_carpma(Duvarlar *d, double ax, double ay, double bx, double by, double *cx, double *cy)
{
    *cx = bx;
    *cy = by;
    double L = uz(ax, ay, bx, by);
    if (!d || L < 1e-9) return;
    int *aday = xmalloc((size_t)d->n * sizeof *aday);
    int m = parca_yakini(d, ax, ay, bx, by, aday);
    double en = 2;
    for (int k = 0; k < m; k++) {
        const double *w = d->p + 4 * aday[k];
        if (nokta_parca(ax, ay, w[0], w[1], w[2], w[3]) < UST || !engelliyor(ax, ay, bx, by, w)) continue;
        /* Çizginin duvara değdiği ilk yer: kesişim ya da duvar ucunun çizgi üstündeki yeri */
        double rx = bx - ax, ry = by - ay, sx = w[2] - w[0], sy = w[3] - w[1];
        double den = rx * sy - ry * sx;
        if (capraz(ax, ay, bx, by, w) && fabs(den) > 1e-15) {
            double t = ((w[0] - ax) * sy - (w[1] - ay) * sx) / den;
            if (t >= 0 && t < en) en = t;
        }
        for (int e = 0; e < 2; e++)
            if (ic_nokta(w[2 * e], w[2 * e + 1], ax, ay, bx, by)) {
                double t = ((w[2 * e] - ax) * rx + (w[2 * e + 1] - ay) * ry) / (L * L);
                if (t >= 0 && t < en) en = t;
            }
    }
    if (en <= 1) {
        double t = en - DUVAR_ACIKLIK / L;
        if (t < 0) t = 0;
        *cx = ax + t * (bx - ax);
        *cy = ay + t * (by - ay);
    }
    if (uz(ax, ay, *cx, *cy) >= 1e-9) uca_cek(d, ax, ay, 1, cx, cy, aday);
    free(aday);
}

void duvar_uzaklastir(Duvarlar *d, double *x, double *y)
{
    if (!d || !d->gercek) return;
    int *aday = xmalloc((size_t)d->n * sizeof *aday);
    double u, x0 = *x, y0 = *y, nx = *x, ny = *y;
    int w = en_yakin_duvar(d, nx, ny, &u, aday);
    for (int k = 0; k < 4 && w >= 0 && u < DUVAR_UC_PAYI / 2; k++) {
        const double *p = d->p + 4 * w;
        /* Tam duvarın üstündeki noktanın yanı belli değil: duvarın soluna */
        if (!duvardan_it(p, nx - (p[3] - p[1]), ny + (p[2] - p[0]), &nx, &ny)) break;
        w = en_yakin_duvar(d, nx, ny, &u, aday);
    }
    free(aday);
    nx = nx < 0 ? 0 : nx > d->g ? d->g : nx;
    ny = ny < 0 ? 0 : ny > d->y ? d->y : ny;
    if (duvar_gorus_acik(d, x0, y0, nx, ny)) { /* itilirken başka bir duvarın öbür yanına geçmesin */
        *x = nx;
        *y = ny;
    }
}

double duvar_yakinlik(Duvarlar *d, double x, double y)
{
    if (!d) return KAYIT_PAYI;
    int *aday = xmalloc((size_t)d->n * sizeof *aday);
    double u;
    en_yakin_duvar(d, x, y, &u, aday);
    free(aday);
    return u;
}

void yol_noktada(const Yol *y, double s, double *x, double *yy)
{
    if (!y->n) return;
    *x = y->xy[0];
    *yy = y->xy[1];
    if (s <= 0) return;
    for (int i = 0; i + 1 < y->n; i++) {
        double ax = y->xy[2 * i], ay = y->xy[2 * i + 1], bx = y->xy[2 * i + 2], by = y->xy[2 * i + 3];
        double L = uz(ax, ay, bx, by);
        if (s <= L && L > 0) {
            *x = ax + (bx - ax) * s / L;
            *yy = ay + (by - ay) * s / L;
            return;
        }
        s -= L;
    }
    *x = y->xy[2 * y->n - 2];
    *yy = y->xy[2 * y->n - 1];
}

double yol_en_yakin(const Yol *y, double px, double py, double *s)
{
    double en = INFINITY, bas = 0;
    *s = 0;
    if (y->n == 1) {
        *s = 0;
        return uz(px, py, y->xy[0], y->xy[1]);
    }
    for (int i = 0; i + 1 < y->n; i++) {
        double ax = y->xy[2 * i], ay = y->xy[2 * i + 1], bx = y->xy[2 * i + 2], by = y->xy[2 * i + 3];
        double dx = bx - ax, dy = by - ay, L2 = dx * dx + dy * dy;
        double t = L2 > 0 ? ((px - ax) * dx + (py - ay) * dy) / L2 : 0;
        t = t < 0 ? 0 : t > 1 ? 1 : t;
        double m = uz(ax + t * dx, ay + t * dy, px, py);
        if (m < en) {
            en = m;
            *s = bas + t * sqrt(L2);
        }
        bas += sqrt(L2);
    }
    return en;
}
