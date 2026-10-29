#include "ws.h"
#include "ortak.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

static int iceriyor(const char *liste, const char *kelime)
{
    /* "keep-alive, Upgrade" gibi virgüllü listelerde büyük/küçük harf duyarsız arama */
    size_t n = strlen(kelime);
    const char *p = liste;
    while (p && *p) {
        while (*p == ' ' || *p == ',') p++;
        const char *s = strchr(p, ',');
        size_t len = s ? (size_t)(s - p) : strlen(p);
        while (len && p[len - 1] == ' ') len--;
        if (len == n && !strncasecmp(p, kelime, n)) return 1;
        p = s;
    }
    return 0;
}

int ws_el_sikis(int fd, const HttpIstek *r)
{
    const char *up = http_baslik(r, "Upgrade");
    const char *con = http_baslik(r, "Connection");
    const char *key = http_baslik(r, "Sec-WebSocket-Key");
    const char *ver = http_baslik(r, "Sec-WebSocket-Version");
    if (!up || strcasecmp(up, "websocket") || !con || !iceriyor(con, "upgrade") || !key || !ver || strcmp(ver, "13"))
        return 0;
    if (strlen(key) > 64) return 0;
    char birlesik[128];
    snprintf(birlesik, sizeof birlesik, "%s258EAFA5-E914-47DA-95CA-C5AB0DC85B11", key);
    unsigned char ozet[20];
    sha1(birlesik, strlen(birlesik), ozet);
    char kabul[32];
    base64_kodla(ozet, 20, kabul);
    char yanit[256];
    int n = snprintf(yanit, sizeof yanit,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n\r\n",
                     kabul);
    return yaz_hepsi(fd, yanit, (size_t)n);
}

static int tam_oku(int fd, void *p, size_t n)
{
    char *c = p;
    while (n > 0) {
        ssize_t k = recv(fd, c, n, 0);
        if (k < 0 && errno == EINTR) continue;
        if (k <= 0) return 0;
        c += k;
        n -= (size_t)k;
    }
    return 1;
}

int ws_mesaj_oku(int fd, char **veri, size_t *n, size_t azami)
{
    *veri = NULL;
    *n = 0;
    Buf b;
    buf_init(&b);
    int ilk_tur = -1;
    for (;;) {
        unsigned char h[2];
        if (!tam_oku(fd, h, 2)) goto hata;
        int fin = h[0] & 0x80;
        int tur = h[0] & 0x0F;
        if (h[0] & 0x70) goto hata;           /* uzantı bitleri kullanılmıyor */
        if (!(h[1] & 0x80)) goto hata;        /* istemci çerçeveleri maskeli olmalı */
        unsigned long long len = h[1] & 0x7F;
        if (len == 126) {
            unsigned char e[2];
            if (!tam_oku(fd, e, 2)) goto hata;
            len = (unsigned long long)e[0] << 8 | e[1];
        } else if (len == 127) {
            unsigned char e[8];
            if (!tam_oku(fd, e, 8)) goto hata;
            len = 0;
            for (int i = 0; i < 8; i++) len = len << 8 | e[i];
        }
        unsigned char maske[4];
        if (!tam_oku(fd, maske, 4)) goto hata;
        int kontrol = tur >= 8;
        if (kontrol && (len > 125 || !fin)) goto hata;
        if (len > azami || b.len + len > azami) goto hata;
        char *yuk = xmalloc((size_t)len + 1);
        if (len && !tam_oku(fd, yuk, (size_t)len)) {
            free(yuk);
            goto hata;
        }
        for (unsigned long long i = 0; i < len; i++) yuk[i] ^= (char)maske[i & 3];
        yuk[len] = '\0';
        if (kontrol) {
            /* Kontrol çerçeveleri parçalı mesajların arasına girebilir; hemen döndür */
            buf_free(&b);
            *veri = yuk;
            *n = (size_t)len;
            return tur;
        }
        if (tur == 0) {
            if (ilk_tur < 0) {
                free(yuk);
                goto hata;
            }
        } else {
            if (ilk_tur >= 0) {
                free(yuk);
                goto hata;
            }
            ilk_tur = tur;
        }
        buf_add(&b, yuk, (size_t)len);
        free(yuk);
        if (fin) break;
    }
    *n = b.len;
    *veri = buf_take(&b);
    return ilk_tur;
hata:
    buf_free(&b);
    return -1;
}

int ws_cerceve_yaz(int fd, int tur, const void *veri, size_t n)
{
    unsigned char h[10];
    size_t hn;
    h[0] = (unsigned char)(0x80 | (tur & 0x0F));
    if (n < 126) {
        h[1] = (unsigned char)n;
        hn = 2;
    } else if (n < 65536) {
        h[1] = 126;
        h[2] = (unsigned char)(n >> 8);
        h[3] = (unsigned char)n;
        hn = 4;
    } else {
        h[1] = 127;
        for (int i = 0; i < 8; i++) h[2 + i] = (unsigned char)((unsigned long long)n >> (56 - 8 * i));
        hn = 10;
    }
    return yaz_hepsi(fd, h, hn) && (n == 0 || yaz_hepsi(fd, veri, n));
}
