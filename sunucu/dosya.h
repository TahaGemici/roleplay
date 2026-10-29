/* Oyun klasörü içinde güvenli dosya işlemleri.
 * Tarayıcıdan gelen her yol burada denetlenir: "..", gizli adlar, kontrol karakterleri ve
 * sembolik bağlar reddedilir; böylece kimse oyun klasörünün dışına çıkamaz. */
#ifndef DOSYA_H
#define DOSYA_H

#include <stddef.h>
#include "ortak.h"

#define YOL_AZAMI 1024

int dosya_adi_gecerli(const char *ad);
int goreli_yol_gecerli(const char *yol); /* "" kök klasördür ve geçerlidir */

/* kök + göreli yolu birleştirir; yoldaki herhangi bir parça sembolik bağsa 0 döner. */
int tam_yol_kur(const char *kok, const char *goreli, char *out, size_t n);

/* Klasör içeriğini JSON dizisi olarak yazar: [{"ad","klasor","boyut","degisim","gorsel"}] */
int klasor_listele(const char *tam, Buf *b);

/* Klasörü içindekilerle birlikte siler. */
int ozyinelemeli_sil(const char *tam);

/* Bir klasörün altındaki dosyaları (en fazla 4 kat derinlikte) göreli yollarıyla bulur. */
typedef struct {
    char **yol;
    int n;
} DosyaListesi;
void dosyalari_bul(const char *kok_tam, int sadece_gorsel, DosyaListesi *l);
void dosya_listesi_sil(DosyaListesi *l);
/* Sıralı listede var mı (dosyalari_bul'un sıralamasıyla ikili arama) */
int dosya_listesinde(const DosyaListesi *l, const char *yol);

/* URL içinde kullanmak için yolu yüzde kodlamasıyla yazar ("/" korunur). */
void url_yol_kodla(Buf *b, const char *yol);

/* Uzantıya göre içerik türü */
const char *icerik_turu(const char *ad);

#endif
