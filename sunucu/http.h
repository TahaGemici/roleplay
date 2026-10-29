/* Küçük HTTP/1.1 sunucu yardımcıları: istek okuma, yanıt yazma, dosya gönderme.
 * Her bağlantı tek istek taşır (Connection: close); önündeki Caddy/nginx bağlantıları yönetir. */
#ifndef HTTP_H
#define HTTP_H

#include <stddef.h>

#define HTTP_BASLIK_AZAMI 16384
#define HTTP_BASLIK_SAYISI 64

typedef struct {
    int fd;
    size_t ham_n;       /* tamponda okunmuş bayt */
    size_t govde_basi;  /* gövdenin tampondaki başlangıcı */
    size_t govde_okunan; /* tampondan tüketilen gövde baytı */
    char *yontem;
    char *hedef;
    char yol[2048];     /* yüzde kodlaması çözülmüş yol */
    char *sorgu;        /* '?' sonrası ham sorgu ("" olabilir) */
    char *adlar[HTTP_BASLIK_SAYISI];
    char *degerler[HTTP_BASLIK_SAYISI];
    int baslik_n;
    long long icerik_uzunlugu; /* yoksa -1 */
    char ip[64];
    char ham[HTTP_BASLIK_AZAMI + 1]; /* en sonda durmalı: sıfırlama bu alana kadar yapılır */
} HttpIstek;

/* 1: istek okundu, 0: bağlantı boş kapandı, -1: bozuk istek, -2: başlık çok büyük */
int http_istek_oku(int fd, HttpIstek *r);

const char *http_baslik(const HttpIstek *r, const char *ad);
int http_sorgu(const HttpIstek *r, const char *ad, char *out, size_t n);
int http_cerez(const HttpIstek *r, const char *ad, char *out, size_t n);

/* Gövdeyi belleğe okur (JSON için). Uzunluğu ya da hata için -1 döner. */
long long http_govde_oku(HttpIstek *r, char **out, size_t azami);
/* Gövdeyi dosyaya akıtır. Başarılıysa 1. */
int http_govde_dosyaya(HttpIstek *r, int hedef_fd);

void http_yanit(int fd, int durum, const char *tur, const char *ek, const void *govde, size_t n);
void http_json(int fd, int durum, const char *json, const char *ek);
void http_hata(int fd, int durum, const char *mesaj);
/* Dosyayı gönderir; dosya yoksa 0 döner (hiçbir şey yazmaz). */
int http_dosya_gonder(int fd, const char *yol, const char *tur, const char *ek);

int yaz_hepsi(int fd, const void *p, size_t n);
int yuzde_coz(const char *in, size_t n, char *out, size_t outn, int arti_bosluk);

#endif
