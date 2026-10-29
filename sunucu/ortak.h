/* Ortak yardımcılar: bellek, büyüyen metin tamponu, rastgelelik, SHA-1, UTF-8. */
#ifndef ORTAK_H
#define ORTAK_H

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

/* Bellek: yer kalmazsa program anlamlı bir şekilde devam edemez; durur. */
void *xmalloc(size_t n);
void *xcalloc(size_t n, size_t size);
void *xrealloc(void *p, size_t n);
char *xstrdup(const char *s);

/* Büyüyen metin tamponu */
typedef struct {
    char *p;
    size_t len;
    size_t cap;
} Buf;

void buf_init(Buf *b);
void buf_free(Buf *b);
void buf_add(Buf *b, const void *data, size_t n);
void buf_puts(Buf *b, const char *s);
void buf_putc(Buf *b, char c);
void buf_printf(Buf *b, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
/* JSON dizgisi olarak yazar: tırnaklar ve kaçış karakterleriyle birlikte. */
void buf_json_str(Buf *b, const char *s);
/* Tamponun içeriğini NUL ile biten bir dizgi olarak verir; tampon boşalır. */
char *buf_take(Buf *b);

/* Güvenli rastgelelik (işletim sisteminin kaynağından) */
void rastgele_bayt(void *out, size_t n);
uint32_t rastgele_aralik(uint32_t n); /* 0..n-1, eşit dağılımlı */
void rastgele_hex(char *out, size_t bayt); /* out en az 2*bayt+1 uzunlukta */

/* SHA-1 (WebSocket el sıkışması ve oturum anahtarı özetleri için) */
void sha1(const void *data, size_t len, unsigned char out[20]);
void sha1_hex(const char *s, char out[41]);
void base64_kodla(const unsigned char *in, size_t n, char *out); /* out en az 4*((n+2)/3)+1 */

/* UTF-8 geçerli mi? */
int utf8_gecerli(const char *s, size_t n);

/* Sabit zamanlı karşılaştırma (şifreler için) */
int sabit_esit(const char *a, const char *b);

/* Saniye ve milisaniye cinsinden şimdiki zaman */
long long simdi_sn(void);
long long simdi_ms(void);

/* Dizgiyi güvenle kopyala (her zaman NUL ile biter) */
void kopyala(char *dst, size_t n, const char *src);

#endif
