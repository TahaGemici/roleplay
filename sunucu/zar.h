/* Zar ifadeleri: d20, 1d20+5, 2d6 + 1d4 - 1, d% (d100), 4d6kh3, 4d6dl1, 2d20kh1, 2d20kl1.
 * İfadeden sonra gelen yazı etiket olur: "1d20+5 kılıç saldırısı".
 * Zarlar sunucuda, işletim sisteminin güvenli rastgele kaynağıyla atılır. */
#ifndef ZAR_H
#define ZAR_H

#include "ortak.h"

#define ZAR_AZAMI 100
#define ZAR_AZAMI_YUZ 1000
#define ZAR_AZAMI_PARCA 20
#define ZAR_AZAMI_SABIT 100000

typedef struct {
    int zar_mi;     /* 1: zar, 0: sabit sayı */
    int isaret;     /* +1 ya da -1 */
    int adet, yuz;  /* zar için */
    int tut;        /* tutulacak zar sayısı (hepsi için adet) */
    int yuksek;     /* 1: en yüksekleri tut, 0: en düşükleri */
    int ilk;        /* sonuç dizisindeki ilk zarın sırası */
    int deger;      /* sabit sayı ya da tutulan zarların toplamı */
    char yazim[32]; /* "4d6kh3" ya da "5" */
} ZarParca;

typedef struct {
    ZarParca parca[ZAR_AZAMI_PARCA];
    int parca_n;
    int deger[ZAR_AZAMI];
    unsigned char tutuldu[ZAR_AZAMI];
    int zar_n;
    long long toplam;
    char etiket[128];
    char gosterim[512];
} ZarSonuc;

/* İfadeyi çözer ve zarları atar. Başarılıysa 1; değilse 0 ve hata metni. */
int zar_ifadesi_at(const char *ifade, ZarSonuc *s, char *hata, size_t hata_n);

/* Sonucu JSON nesnesi olarak yazar. */
void zar_json_yaz(Buf *b, const ZarSonuc *s);

/* UTF-8 karakterini bölmeden en fazla n-1 bayt kopyalar. */
void utf8_kirp(char *dst, size_t n, const char *src);

#endif
