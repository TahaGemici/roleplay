#include "dosya.h"
#include "gorsel.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

int dosya_adi_gecerli(const char *ad)
{
    size_t n = strlen(ad);
    if (n == 0 || n > 255) return 0;
    if (ad[0] == '.') return 0; /* ".", "..", gizli dosyalar ve yarım yüklemeler */
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)ad[i];
        if (c < 0x20 || c == 0x7f || c == '/' || c == '\\') return 0;
    }
    if (ad[n - 1] == ' ') return 0;
    return utf8_gecerli(ad, n);
}

int goreli_yol_gecerli(const char *yol)
{
    size_t n = strlen(yol);
    if (n == 0) return 1;
    if (n > 600) return 0; /* kök (en fazla 400) + göreli yol her zaman YOL_AZAMI içine sığsın */
    if (yol[0] == '/' || yol[n - 1] == '/') return 0;
    char parca[256];
    const char *p = yol;
    while (*p) {
        const char *s = strchr(p, '/');
        size_t k = s ? (size_t)(s - p) : strlen(p);
        if (k == 0 || k >= sizeof parca) return 0;
        memcpy(parca, p, k);
        parca[k] = '\0';
        if (!dosya_adi_gecerli(parca)) return 0;
        p += k;
        if (*p == '/') p++;
    }
    return 1;
}

int tam_yol_kur(const char *kok, const char *goreli, char *out, size_t n)
{
    if (!goreli_yol_gecerli(goreli)) return 0;
    int w = goreli[0] ? snprintf(out, n, "%s/%s", kok, goreli) : snprintf(out, n, "%s", kok);
    if (w < 0 || (size_t)w >= n) return 0;
    /* Yol boyunca var olan her parçayı denetle: sembolik bağ olmamalı */
    size_t kok_n = strlen(kok);
    char ara[YOL_AZAMI];
    for (size_t i = kok_n; i <= (size_t)w; i++) {
        if (out[i] == '/' || out[i] == '\0') {
            if (i == kok_n) continue;
            memcpy(ara, out, i);
            ara[i] = '\0';
            struct stat st;
            if (lstat(ara, &st) != 0) break; /* henüz yok: oluşturulacak */
            if (S_ISLNK(st.st_mode)) return 0;
        }
    }
    return 1;
}

typedef struct {
    char *ad;
    int klasor;
    long long boyut;
    long long degisim;
} Girdi;

static int girdi_karsilastir(const void *a, const void *b)
{
    const Girdi *x = a, *y = b;
    if (x->klasor != y->klasor) return y->klasor - x->klasor;
    int c = strcasecmp(x->ad, y->ad);
    return c ? c : strcmp(x->ad, y->ad);
}

int klasor_listele(const char *tam, Buf *b)
{
    DIR *d = opendir(tam);
    if (!d) return 0;
    Girdi *g = NULL;
    int n = 0, cap = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!dosya_adi_gecerli(e->d_name)) continue;
        char yol[YOL_AZAMI];
        if (snprintf(yol, sizeof yol, "%s/%s", tam, e->d_name) >= (int)sizeof yol) continue;
        struct stat st;
        if (lstat(yol, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (!S_ISDIR(st.st_mode) && !S_ISREG(st.st_mode)) continue;
        if (n == cap) {
            cap = cap ? cap * 2 : 32;
            g = xrealloc(g, (size_t)cap * sizeof *g);
        }
        g[n].ad = xstrdup(e->d_name);
        g[n].klasor = S_ISDIR(st.st_mode);
        g[n].boyut = (long long)st.st_size;
        g[n].degisim = (long long)st.st_mtime;
        n++;
        if (n >= 5000) break;
    }
    closedir(d);
    if (n > 1) qsort(g, (size_t)n, sizeof *g, girdi_karsilastir);
    buf_putc(b, '[');
    for (int i = 0; i < n; i++) {
        if (i) buf_putc(b, ',');
        buf_puts(b, "{\"ad\":");
        buf_json_str(b, g[i].ad);
        buf_printf(b, ",\"klasor\":%s,\"boyut\":%lld,\"degisim\":%lld,\"gorsel\":%s}", g[i].klasor ? "true" : "false",
                   g[i].boyut, g[i].degisim, (!g[i].klasor && gorsel_uzantili(g[i].ad)) ? "true" : "false");
        free(g[i].ad);
    }
    buf_putc(b, ']');
    free(g);
    return 1;
}

int ozyinelemeli_sil(const char *tam)
{
    struct stat st;
    if (lstat(tam, &st) != 0) return errno == ENOENT;
    if (!S_ISDIR(st.st_mode)) return unlink(tam) == 0;
    DIR *d = opendir(tam);
    if (!d) return 0;
    struct dirent *e;
    int ok = 1;
    while ((e = readdir(d)) != NULL) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        char yol[YOL_AZAMI];
        if (snprintf(yol, sizeof yol, "%s/%s", tam, e->d_name) >= (int)sizeof yol) {
            ok = 0;
            continue;
        }
        if (!ozyinelemeli_sil(yol)) ok = 0;
    }
    closedir(d);
    if (rmdir(tam) != 0) ok = 0;
    return ok;
}

static void bul(const char *tam, const char *goreli, int derinlik, int sadece_gorsel, DosyaListesi *l, int *cap)
{
    if (derinlik > 4 || l->n >= 3000) return;
    DIR *d = opendir(tam);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!dosya_adi_gecerli(e->d_name)) continue;
        char yol[YOL_AZAMI], gyol[YOL_AZAMI];
        if (snprintf(yol, sizeof yol, "%s/%s", tam, e->d_name) >= (int)sizeof yol) continue;
        if (goreli[0]) {
            if (snprintf(gyol, sizeof gyol, "%s/%s", goreli, e->d_name) >= (int)sizeof gyol) continue;
        } else {
            kopyala(gyol, sizeof gyol, e->d_name);
        }
        struct stat st;
        if (lstat(yol, &st) != 0 || S_ISLNK(st.st_mode)) continue;
        if (S_ISDIR(st.st_mode)) {
            bul(yol, gyol, derinlik + 1, sadece_gorsel, l, cap);
        } else if (S_ISREG(st.st_mode)) {
            if (sadece_gorsel && !gorsel_uzantili(e->d_name)) continue;
            if (l->n == *cap) {
                *cap = *cap ? *cap * 2 : 64;
                l->yol = xrealloc(l->yol, (size_t)*cap * sizeof *l->yol);
            }
            l->yol[l->n++] = xstrdup(gyol);
        }
    }
    closedir(d);
}

static int yol_karsilastir(const void *a, const void *b)
{
    const char *x = *(const char *const *)a, *y = *(const char *const *)b;
    int c = strcasecmp(x, y);
    return c ? c : strcmp(x, y);
}

void dosyalari_bul(const char *kok_tam, int sadece_gorsel, DosyaListesi *l)
{
    l->yol = NULL;
    l->n = 0;
    int cap = 0;
    bul(kok_tam, "", 0, sadece_gorsel, l, &cap);
    if (l->n > 1) qsort(l->yol, (size_t)l->n, sizeof *l->yol, yol_karsilastir);
}

int dosya_listesinde(const DosyaListesi *l, const char *yol)
{
    if (!l->n) return 0;
    return bsearch(&yol, l->yol, (size_t)l->n, sizeof *l->yol, yol_karsilastir) != NULL;
}

void dosya_listesi_sil(DosyaListesi *l)
{
    for (int i = 0; i < l->n; i++) free(l->yol[i]);
    free(l->yol);
    l->yol = NULL;
    l->n = 0;
}

void url_yol_kodla(Buf *b, const char *yol)
{
    static const char hex[] = "0123456789ABCDEF";
    for (const unsigned char *p = (const unsigned char *)yol; *p; p++) {
        unsigned char c = *p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~' || c == '/') {
            buf_putc(b, (char)c);
        } else {
            char e[4] = { '%', hex[c >> 4], hex[c & 15], 0 };
            buf_puts(b, e);
        }
    }
}

const char *icerik_turu(const char *ad)
{
    const char *n = strrchr(ad, '.');
    if (!n) return "application/octet-stream";
    static const struct {
        const char *uzanti, *tur;
    } t[] = {
        { ".html", "text/html; charset=utf-8" },
        { ".css", "text/css; charset=utf-8" },
        { ".js", "text/javascript; charset=utf-8" },
        { ".json", "application/json; charset=utf-8" },
        { ".svg", "image/svg+xml" },
        { ".png", "image/png" },
        { ".jpg", "image/jpeg" },
        { ".jpeg", "image/jpeg" },
        { ".gif", "image/gif" },
        { ".webp", "image/webp" },
        { ".woff2", "font/woff2" },
        { ".txt", "text/plain; charset=utf-8" },
        { ".md", "text/plain; charset=utf-8" },
        { ".csv", "text/plain; charset=utf-8" },
        { ".c", "text/plain; charset=utf-8" },
        { ".h", "text/plain; charset=utf-8" },
        { ".pdf", "application/pdf" },
        { ".mp3", "audio/mpeg" },
        { ".ogg", "audio/ogg" },
        { ".wav", "audio/wav" },
    };
    for (size_t i = 0; i < sizeof t / sizeof t[0]; i++)
        if (!strcasecmp(n, t[i].uzanti)) return t[i].tur;
    return "application/octet-stream";
}
