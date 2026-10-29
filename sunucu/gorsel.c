#include "gorsel.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

static unsigned be16(const unsigned char *p) { return (unsigned)p[0] << 8 | p[1]; }
static unsigned le16(const unsigned char *p) { return (unsigned)p[1] << 8 | p[0]; }
static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}
static uint32_t le32(const unsigned char *p)
{
    return (uint32_t)p[3] << 24 | (uint32_t)p[2] << 16 | (uint32_t)p[1] << 8 | p[0];
}

/* EXIF (APP1) içinden yön etiketini (0x0112) okur; bulunamazsa 1. */
static int exif_yon(const unsigned char *d, size_t n)
{
    if (n < 14 || memcmp(d, "Exif\0\0", 6) != 0) return 1;
    const unsigned char *t = d + 6;
    size_t tn = n - 6;
    int le;
    if (memcmp(t, "II", 2) == 0) le = 1;
    else if (memcmp(t, "MM", 2) == 0) le = 0;
    else return 1;
    uint32_t ifd = le ? le32(t + 4) : be32(t + 4);
    if (ifd + 2 > tn) return 1;
    unsigned sayi = le ? le16(t + ifd) : be16(t + ifd);
    for (unsigned i = 0; i < sayi; i++) {
        size_t o = ifd + 2 + 12u * i;
        if (o + 12 > tn) break;
        unsigned etiket = le ? le16(t + o) : be16(t + o);
        if (etiket == 0x0112) {
            unsigned v = le ? le16(t + o + 8) : be16(t + o + 8);
            return (v >= 1 && v <= 8) ? (int)v : 1;
        }
    }
    return 1;
}

static int jpeg(FILE *f, GorselBilgi *g)
{
    int yon = 1;
    unsigned char h[4];
    if (fseek(f, 2, SEEK_SET) != 0) return 0;
    for (int bolum = 0; bolum < 512; bolum++) {
        int c = fgetc(f);
        if (c != 0xFF) return 0;
        int isaret;
        do {
            isaret = fgetc(f);
        } while (isaret == 0xFF);
        if (isaret == EOF) return 0;
        if (isaret == 0xD8 || isaret == 0x01 || (isaret >= 0xD0 && isaret <= 0xD7)) continue;
        if (isaret == 0xD9 || isaret == 0xDA) return 0;
        if (fread(h, 1, 2, f) != 2) return 0;
        unsigned uzunluk = be16(h);
        if (uzunluk < 2) return 0;
        int sof = isaret >= 0xC0 && isaret <= 0xCF && isaret != 0xC4 && isaret != 0xC8 && isaret != 0xCC;
        if (sof) {
            unsigned char s[5];
            if (fread(s, 1, 5, f) != 5) return 0;
            int yuk = (int)be16(s + 1), gen = (int)be16(s + 3);
            if (yon >= 5 && yon <= 8) { /* 90° döndürülmüş: kenarlar yer değiştirir */
                int tmp = gen;
                gen = yuk;
                yuk = tmp;
            }
            g->genislik = gen;
            g->yukseklik = yuk;
            g->tur = "jpg";
            return gen > 0 && yuk > 0;
        }
        if (isaret == 0xE1 && uzunluk > 8) {
            unsigned char exif[4096];
            size_t okunacak = uzunluk - 2 < sizeof exif ? uzunluk - 2 : sizeof exif;
            long bas = ftell(f);
            size_t okunan = fread(exif, 1, okunacak, f);
            int y = exif_yon(exif, okunan);
            if (y != 1) yon = y;
            if (fseek(f, bas + (long)uzunluk - 2, SEEK_SET) != 0) return 0;
            continue;
        }
        if (fseek(f, (long)uzunluk - 2, SEEK_CUR) != 0) return 0;
    }
    return 0;
}

int gorsel_bilgi(const char *yol, GorselBilgi *g)
{
    memset(g, 0, sizeof *g);
    FILE *f = fopen(yol, "rb");
    if (!f) return 0;
    unsigned char b[32];
    size_t n = fread(b, 1, sizeof b, f);
    int ok = 0;
    if (n >= 24 && memcmp(b, "\x89PNG\r\n\x1a\n", 8) == 0 && memcmp(b + 12, "IHDR", 4) == 0) {
        g->genislik = (int)be32(b + 16);
        g->yukseklik = (int)be32(b + 20);
        g->tur = "png";
        ok = 1;
    } else if (n >= 10 && (memcmp(b, "GIF87a", 6) == 0 || memcmp(b, "GIF89a", 6) == 0)) {
        g->genislik = (int)le16(b + 6);
        g->yukseklik = (int)le16(b + 8);
        g->tur = "gif";
        ok = 1;
    } else if (n >= 30 && memcmp(b, "RIFF", 4) == 0 && memcmp(b + 8, "WEBP", 4) == 0) {
        g->tur = "webp";
        if (memcmp(b + 12, "VP8 ", 4) == 0 && b[23] == 0x9d && b[24] == 0x01 && b[25] == 0x2a) {
            g->genislik = (int)(le16(b + 26) & 0x3fff);
            g->yukseklik = (int)(le16(b + 28) & 0x3fff);
            ok = 1;
        } else if (memcmp(b + 12, "VP8L", 4) == 0 && b[20] == 0x2f) {
            uint32_t bits = le32(b + 21);
            g->genislik = (int)(bits & 0x3fff) + 1;
            g->yukseklik = (int)((bits >> 14) & 0x3fff) + 1;
            ok = 1;
        } else if (memcmp(b + 12, "VP8X", 4) == 0) {
            g->genislik = (int)(b[24] | b[25] << 8 | b[26] << 16) + 1;
            g->yukseklik = (int)(b[27] | b[28] << 8 | b[29] << 16) + 1;
            ok = 1;
        }
    } else if (n >= 4 && b[0] == 0xFF && b[1] == 0xD8 && b[2] == 0xFF) {
        ok = jpeg(f, g);
    }
    fclose(f);
    if (ok && (g->genislik <= 0 || g->yukseklik <= 0 || g->genislik > 30000 || g->yukseklik > 30000)) ok = 0;
    return ok;
}

int gorsel_uzantili(const char *ad)
{
    const char *n = strrchr(ad, '.');
    if (!n) return 0;
    return !strcasecmp(n, ".png") || !strcasecmp(n, ".jpg") || !strcasecmp(n, ".jpeg") || !strcasecmp(n, ".gif") ||
           !strcasecmp(n, ".webp");
}
