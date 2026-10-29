#include "geometri.h"

#include <math.h>

#define PAY 1e-4

double mesafe(double ax, double ay, double bx, double by)
{
    return hypot(bx - ax, by - ay);
}

double nokta_parca_mesafesi(double px, double py, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay;
    double L = dx * dx + dy * dy;
    double t = L > 0 ? ((px - ax) * dx + (py - ay) * dy) / L : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    return hypot(ax + t * dx - px, ay + t * dy - py);
}

int parca_daire_kesisimi(double ax, double ay, double bx, double by, double cx, double cy, double r, double *t0,
                         double *t1)
{
    double dx = bx - ax, dy = by - ay;
    double fx = ax - cx, fy = ay - cy;
    double a = dx * dx + dy * dy;
    double c = fx * fx + fy * fy - r * r;
    if (a < 1e-12) {
        /* Parça bir nokta */
        if (c > PAY) return 0;
        *t0 = 0;
        *t1 = 1;
        return 1;
    }
    double b = 2 * (fx * dx + fy * dy);
    double disk = b * b - 4 * a * c;
    if (disk < 0) return 0;
    double kok = sqrt(disk);
    double s0 = (-b - kok) / (2 * a), s1 = (-b + kok) / (2 * a);
    if (s1 < 0 || s0 > 1) return 0;
    *t0 = s0 < 0 ? 0 : s0;
    *t1 = s1 > 1 ? 1 : s1;
    return *t1 - *t0 > 1e-9 || c <= PAY;
}

int kapsul_icinde(const Kapsul *ic, const Kapsul *dis)
{
    /* Bir noktanın dis parçasına uzaklığı, ic parçası boyunca dışbükey bir fonksiyondur:
       en büyük değeri uçlardadır. Uçlar r payıyla içerideyse bütün kapsül içeridedir. */
    double d1 = nokta_parca_mesafesi(ic->x1, ic->y1, dis->x1, dis->y1, dis->x2, dis->y2);
    double d2 = nokta_parca_mesafesi(ic->x2, ic->y2, dis->x1, dis->y1, dis->x2, dis->y2);
    return d1 + ic->r <= dis->r + 1e-3 && d2 + ic->r <= dis->r + 1e-3;
}
