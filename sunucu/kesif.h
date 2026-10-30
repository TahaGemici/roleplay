/* Bir oyuncunun bir haritada keşfettiği alan: haritayı kaplayan ince bir ızgara (bir karede en fazla 8×8 hücre).
 * Görüş çokgenleri (duvarların arkası hariç görülen alan) ızgaraya boyanır; böylece sonradan kaldırılan bir duvar
 * geçmişte görülmemiş yerleri keşfedilmiş yapmaz. Diske ve tarayıcıya satır satır sıkıştırılmış olarak gider. */
#ifndef KESIF_H
#define KESIF_H

#include "ortak.h"

typedef struct {
    int g, y;           /* hücre sayısı: genişlik, yükseklik */
    double k;           /* bir karede kaç hücre */
    unsigned char *bit; /* g*y bit, satır satır */
} Kesif;

/* Harita boyutuna (kare) göre boş bir ızgara kurar; öncekini bırakır. */
void kesif_kur(Kesif *k, double genislik, double yukseklik);
void kesif_bosalt(Kesif *k);
/* Ölçüler bu harita boyutununkiyle aynı mı */
int kesif_uygun(const Kesif *k, double genislik, double yukseklik);
/* Harita boyutu değişti: keşfedilen yerler resme göre aynı yerde kalacak şekilde yeni ızgaraya taşınır. */
void kesif_olcekle(Kesif *k, double eski_g, double eski_y, double yeni_g, double yeni_y);

/* Çokgenin (x0,y0,x1,y1,... kare cinsinden) içini keşfedilmiş yapar; yeni keşfedilen hücre varsa 1 döner. */
int kesif_cokgen(Kesif *k, const double *xy, int n);
/* Bu nokta keşfedilmiş mi */
int kesif_var(const Kesif *k, double x, double y);

/* Sıkıştırılmış biçim: sırayla 0 ve 1 dizilerinin uzunlukları (her biri 7 bitlik parçalarla), base64. */
void kesif_kodla(Buf *b, const Kesif *k);
/* Okur (k->g ve k->y önceden kurulmuş olmalı); bozuksa 0 döner ve ızgara boş kalır. */
int kesif_coz(Kesif *k, const char *s);

#endif
