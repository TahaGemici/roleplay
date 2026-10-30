/* Görüş hesapları için küçük düzlem geometrisi. Birimler kare cinsindendir. */
#ifndef GEOMETRI_H
#define GEOMETRI_H

double mesafe(double ax, double ay, double bx, double by);

/* (px,py) noktasının [A,B] doğru parçasına uzaklığı */
double nokta_parca_mesafesi(double px, double py, double ax, double ay, double bx, double by);

/* [A,B] parçasının (cx,cy) merkezli r yarıçaplı dairenin içinde kalan kısmı.
   Kesişim varsa 1 döner ve parçanın içeride kalan aralığını t0 <= t1 (0..1) olarak verir. */
int parca_daire_kesisimi(double ax, double ay, double bx, double by, double cx, double cy, double r, double *t0,
                         double *t1);

#endif
