/* Görüş hesapları için küçük düzlem geometrisi. Birimler kare cinsindendir. */
#ifndef GEOMETRI_H
#define GEOMETRI_H

/* Keşfedilen alanın bir parçası: (x1,y1)-(x2,y2) doğru parçasından en fazla r uzaklıktaki noktalar.
   Yürürken görüş dairesinin taradığı alan tam olarak budur. */
typedef struct {
    float x1, y1, x2, y2, r;
} Kapsul;

double mesafe(double ax, double ay, double bx, double by);

/* (px,py) noktasının [A,B] doğru parçasına uzaklığı */
double nokta_parca_mesafesi(double px, double py, double ax, double ay, double bx, double by);

/* [A,B] parçasının (cx,cy) merkezli r yarıçaplı dairenin içinde kalan kısmı.
   Kesişim varsa 1 döner ve parçanın içeride kalan aralığını t0 <= t1 (0..1) olarak verir. */
int parca_daire_kesisimi(double ax, double ay, double bx, double by, double cx, double cy, double r, double *t0,
                         double *t1);

/* ic kapsülü tamamen dis kapsülünün içinde mi? (küçük bir pay ile) */
int kapsul_icinde(const Kapsul *ic, const Kapsul *dis);

#endif
