#include "zar.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void hata_yaz(char *hata, size_t n, const char *m)
{
    if (hata && n) kopyala(hata, n, m);
}

void utf8_kirp(char *dst, size_t n, const char *src)
{
    if (n == 0) return;
    size_t m = strlen(src);
    if (m >= n) {
        m = n - 1;
        /* Çok baytlı bir karakterin ortasında kesme */
        while (m > 0 && ((unsigned char)src[m] & 0xC0) == 0x80) m--;
    }
    memcpy(dst, src, m);
    dst[m] = '\0';
    if (!utf8_gecerli(dst, m)) dst[0] = '\0';
}

/* Eksi işareti olarak '-' ve tipografik '−' (U+2212) kabul edilir. Okunan bayt sayısını döner. */
static int isaret_oku(const char *p, int *isaret)
{
    if (*p == '+') {
        *isaret = 1;
        return 1;
    }
    if (*p == '-') {
        *isaret = -1;
        return 1;
    }
    if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x88 && (unsigned char)p[2] == 0x92) {
        *isaret = -1;
        return 3;
    }
    return 0;
}

static const char *bosluk_gec(const char *p)
{
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    return p;
}

/* Sınırlı sayı okur. Taşma olursa -2, rakam yoksa -1 döner. */
static long sayi_oku(const char **pp, long sinir)
{
    const char *p = *pp;
    if (!isdigit((unsigned char)*p)) return -1;
    long v = 0;
    while (isdigit((unsigned char)*p)) {
        v = v * 10 + (*p - '0');
        if (v > sinir) {
            while (isdigit((unsigned char)*p)) p++;
            *pp = p;
            return -2;
        }
        p++;
    }
    *pp = p;
    return v;
}

static int harf_mi(unsigned char c)
{
    return isalpha(c) || c >= 0x80;
}

int zar_ifadesi_at(const char *ifade, ZarSonuc *s, char *hata, size_t hata_n)
{
    memset(s, 0, sizeof *s);
    const char *p = bosluk_gec(ifade ? ifade : "");
    int isaret = 1;
    int k = isaret_oku(p, &isaret);
    if (k) p = bosluk_gec(p + k);

    for (;;) {
        if (s->parca_n >= ZAR_AZAMI_PARCA) {
            hata_yaz(hata, hata_n, "Bir ifade en fazla 20 parçadan oluşabilir.");
            return 0;
        }
        ZarParca *pr = &s->parca[s->parca_n];
        const char *q = p;
        long adet = 1;
        if (isdigit((unsigned char)*q)) {
            adet = sayi_oku(&q, 100000);
        }
        if ((*q == 'd' || *q == 'D') && adet != -2) {
            /* zar parçası */
            q++;
            long yuz;
            int yuzde = 0;
            if (*q == '%') {
                yuz = 100;
                yuzde = 1;
                q++;
            } else {
                yuz = sayi_oku(&q, 100000);
                if (yuz == -1) goto anlasilamadi;
            }
            if (adet < 1) {
                hata_yaz(hata, hata_n, "Zar adedi en az 1 olmalı.");
                return 0;
            }
            if (yuz < 2 || yuz > ZAR_AZAMI_YUZ) {
                hata_yaz(hata, hata_n, "Zarın yüz sayısı 2 ile 1000 arasında olmalı.");
                return 0;
            }
            if (adet > ZAR_AZAMI || s->zar_n + adet > ZAR_AZAMI) {
                hata_yaz(hata, hata_n, "Tek seferde en fazla 100 zar atılabilir.");
                return 0;
            }
            pr->zar_mi = 1;
            pr->adet = (int)adet;
            pr->yuz = (int)yuz;
            pr->tut = (int)adet;
            pr->yuksek = 1;

            /* İsteğe bağlı tutma/atma eki: kh, kl, dh, dl, k (ardından harf gelmemeli) */
            char ek[3] = { 0 };
            const char *r = q;
            if ((r[0] == 'k' || r[0] == 'd') && (r[1] == 'h' || r[1] == 'l')) {
                ek[0] = r[0];
                ek[1] = r[1];
                r += 2;
            } else if (r[0] == 'k') {
                ek[0] = 'k';
                r += 1;
            }
            long n = 1;
            if (ek[0]) {
                if (isdigit((unsigned char)*r)) n = sayi_oku(&r, 100000);
                if (harf_mi((unsigned char)*r)) ek[0] = '\0'; /* "2d6kılıç": ek değil, etiket */
            }
            char ek_yazim[16] = "";
            if (ek[0]) {
                if (n < 0 || n > adet) {
                    snprintf(hata, hata_n, "%ld zardan %ld tanesi seçilemez.", adet, n);
                    return 0;
                }
                if (ek[0] == 'k' && ek[1] != 'l') {
                    pr->tut = (int)n;
                    pr->yuksek = 1;
                } else if (ek[0] == 'k') {
                    pr->tut = (int)n;
                    pr->yuksek = 0;
                } else if (ek[1] == 'l') {
                    pr->tut = (int)(adet - n);
                    pr->yuksek = 1;
                } else {
                    pr->tut = (int)(adet - n);
                    pr->yuksek = 0;
                }
                if (pr->tut < 1) {
                    hata_yaz(hata, hata_n, "En az bir zar kalmalı.");
                    return 0;
                }
                snprintf(ek_yazim, sizeof ek_yazim, "%s%ld", ek[0] == 'k' && !ek[1] ? "kh" : ek, n);
                q = r;
            }
            if (yuzde) snprintf(pr->yazim, sizeof pr->yazim, "%ldd%%%s", adet, ek_yazim);
            else snprintf(pr->yazim, sizeof pr->yazim, "%ldd%ld%s", adet, yuz, ek_yazim);
            pr->ilk = s->zar_n;
            s->zar_n += (int)adet;
        } else {
            /* sabit sayı */
            q = p;
            long v = sayi_oku(&q, ZAR_AZAMI_SABIT);
            if (v == -2) {
                hata_yaz(hata, hata_n, "Sayılar en fazla 100000 olabilir.");
                return 0;
            }
            if (v < 0) goto anlasilamadi;
            pr->zar_mi = 0;
            pr->deger = (int)v;
            snprintf(pr->yazim, sizeof pr->yazim, "%ld", v);
        }
        pr->isaret = isaret;
        s->parca_n++;
        p = q;

        const char *r = bosluk_gec(p);
        k = isaret_oku(r, &isaret);
        if (k) {
            p = bosluk_gec(r + k);
            continue;
        }
        /* Kalan yazı etiket */
        if (*r == '#') r = bosluk_gec(r + 1);
        char etiket[128];
        utf8_kirp(etiket, sizeof etiket > 81 ? 81 : sizeof etiket, r);
        for (char *c = etiket; *c; c++)
            if ((unsigned char)*c < 0x20) *c = ' ';
        size_t L = strlen(etiket);
        while (L && etiket[L - 1] == ' ') etiket[--L] = '\0';
        kopyala(s->etiket, sizeof s->etiket, etiket);
        break;
    }

    /* Zarları at */
    s->toplam = 0;
    for (int i = 0; i < s->parca_n; i++) {
        ZarParca *pr = &s->parca[i];
        if (pr->zar_mi) {
            int *v = &s->deger[pr->ilk];
            unsigned char *t = &s->tutuldu[pr->ilk];
            for (int j = 0; j < pr->adet; j++) {
                v[j] = (int)rastgele_aralik((uint32_t)pr->yuz) + 1;
                t[j] = 1;
            }
            if (pr->tut < pr->adet) {
                /* Sıralı dizin: küçükten büyüğe, eşitlikte önce gelen */
                int sira[ZAR_AZAMI];
                for (int j = 0; j < pr->adet; j++) sira[j] = j;
                for (int a = 1; a < pr->adet; a++) {
                    int x = sira[a], b = a - 1;
                    while (b >= 0 && v[sira[b]] > v[x]) {
                        sira[b + 1] = sira[b];
                        b--;
                    }
                    sira[b + 1] = x;
                }
                for (int j = 0; j < pr->adet; j++) t[j] = 0;
                for (int j = 0; j < pr->tut; j++) {
                    int idx = pr->yuksek ? sira[pr->adet - 1 - j] : sira[j];
                    t[idx] = 1;
                }
            }
            pr->deger = 0;
            for (int j = 0; j < pr->adet; j++)
                if (t[j]) pr->deger += v[j];
        }
        s->toplam += (long long)pr->isaret * pr->deger;
    }

    /* Gösterim: "1d20 + 5" */
    Buf b;
    buf_init(&b);
    for (int i = 0; i < s->parca_n; i++) {
        const ZarParca *pr = &s->parca[i];
        if (i == 0) {
            if (pr->isaret < 0) buf_puts(&b, "\xE2\x88\x92");
        } else {
            buf_puts(&b, pr->isaret < 0 ? " \xE2\x88\x92 " : " + ");
        }
        buf_puts(&b, pr->yazim);
    }
    kopyala(s->gosterim, sizeof s->gosterim, b.p ? b.p : "");
    buf_free(&b);
    return 1;

anlasilamadi:
    hata_yaz(hata, hata_n,
             s->parca_n ? "İşaretten sonra bir sayı ya da zar bekleniyordu. Örnek: 1d20+5"
                        : "Zar ifadesi anlaşılamadı. Örnekler: d20, 1d20+5, 2d6+3, 4d6kh3");
    return 0;
}

void zar_json_yaz(Buf *b, const ZarSonuc *s)
{
    buf_puts(b, "{\"gosterim\":");
    buf_json_str(b, s->gosterim);
    buf_puts(b, ",\"etiket\":");
    buf_json_str(b, s->etiket);
    buf_printf(b, ",\"toplam\":%lld,\"parcalar\":[", s->toplam);
    for (int i = 0; i < s->parca_n; i++) {
        const ZarParca *pr = &s->parca[i];
        if (i) buf_putc(b, ',');
        if (pr->zar_mi) {
            buf_printf(b, "{\"zar\":true,\"isaret\":%d,\"yuz\":%d,\"deger\":%d,\"zarlar\":[", pr->isaret, pr->yuz,
                       pr->deger);
            for (int j = 0; j < pr->adet; j++) {
                if (j) buf_putc(b, ',');
                buf_printf(b, "{\"v\":%d,\"t\":%s}", s->deger[pr->ilk + j],
                           s->tutuldu[pr->ilk + j] ? "true" : "false");
            }
            buf_puts(b, "]}");
        } else {
            buf_printf(b, "{\"zar\":false,\"isaret\":%d,\"deger\":%d}", pr->isaret, pr->deger);
        }
    }
    buf_puts(b, "]}");
}
