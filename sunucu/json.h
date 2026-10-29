/* Küçük, bağımlılıksız JSON ayrıştırıcı ve yazıcı. */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>
#include "ortak.h"

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUM, JSON_STR, JSON_ARR, JSON_OBJ } JsonTur;

typedef struct Json Json;
struct Json {
    JsonTur tur;
    int dogru;      /* JSON_BOOL */
    double sayi;    /* JSON_NUM */
    char *metin;    /* JSON_STR: geçerli UTF-8, içinde \0 yok */
    Json **ogeler;  /* JSON_ARR ve JSON_OBJ değerleri */
    char **anahtarlar; /* JSON_OBJ anahtarları */
    size_t adet;
};

/* Hatalıysa NULL döner. Derinlik ve boyut sınırlıdır. */
Json *json_ayristir(const char *metin, size_t n);
void json_sil(Json *j);

Json *json_al(const Json *nesne, const char *anahtar);
const char *json_metin(const Json *nesne, const char *anahtar);         /* yoksa NULL */
int json_tamsayi(const Json *nesne, const char *anahtar, long long *out); /* tam sayıysa 1 */
double json_sayi(const Json *nesne, const char *anahtar, double varsayilan);
int json_mantik(const Json *nesne, const char *anahtar, int varsayilan);

/* Bir JSON ağacını yeniden metne çevirir. */
void json_yaz(Buf *b, const Json *j);

#endif
