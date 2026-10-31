/* Uçtan uca testlerin kullandığı kurallar (make KURALLAR=test/kurallar_test.c) */
#include "kurallar.h"

#include <math.h>
#include <string.h>

const char *const esya_siniflari[] = { "silah", "iksir", "../kotu", NULL };
const Esya yumruk = { .ad = "Yumruk", .sinif = "silah", .dayaniklilik = 100 };
const char *const buyuler[] = { "Şifa", "Yıldırım", NULL };

void her_tur(Oyuncu *o)
{
    int r = zar_at(1, 6);
    duyur("KANCA: %s konum (%.1f,%.1f) tur %d, zar %d, harita %.0fx%.0f, can %.0f, envanter %d", o->ad, o->x, o->y,
          tur_sayisi, r, harita.genislik, harita.yukseklik, o->can, o->envanter_adet);
    gm_bildir("GM-KANCA: %s", o->ad);
    if (!o->npc) bildir(o, "OZEL: %s", o->ad);
    zar("bozuk ifade");
    if (tur_sayisi == 2 && !o->npc) envanter_ekle(o, "Hediye", "iksir", "");
    o->enerji = 5;
}

/* 7 hasar, 2 enerji; enerjisi 2'den azsa saldırı olmaz */
int saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    duyur("SALDIRI: %s -> %s [%s/%s] can %.0f", saldiran->ad, hedef->ad, esya->ad, esya->sinif, hedef->can);
    if (saldiran->enerji < 2) {
        bildir(saldiran, "YORGUN: %.1f", saldiran->enerji);
        return 0;
    }
    hedef->can -= 7;
    saldiran->enerji -= 2;
    if (strcmp(esya->ad, "Itme") == 0) hedef->x += 1;
    if (strcmp(esya->ad, "Bozuk") == 0) hedef->zirh = NAN;
    if (strcmp(esya->sinif, "iksir") == 0) envanter_cikar(saldiran, (int)(esya - saldiran->envanter));
    return 1;
}

/* Kare başına 1 enerji, yetmezse reddeder (yürümeden geçmek bedava).
   x >= 20'ye gitmek isteyeni kural yalnızca 1 kare sağa yürütür (hedefi kuralın değiştirmesi). */
int hareket(Oyuncu *o, float x, float y)
{
    duyur("HAREKET: %s (%.1f,%.1f) -> (%.1f,%.1f) tur %d enerji %.1f", o->ad, o->x, o->y, x, y, tur_sayisi, o->enerji);
    if (x >= 20) {
        o->x += 1;
        o->enerji -= 1;
        return 1;
    }
    float d = yol_uzunlugu;
    if (d > o->enerji) {
        bildir(o, "YETMEZ: %.1f > %.1f", d, o->enerji);
        return 0;
    }
    o->enerji -= d;
    return 1;
}

/* Şifa 3 can verir, Yıldırım 4 hasar; ikisi de 1 enerji */
int buyu(Oyuncu *yapan, Oyuncu *hedef, const char *buyu_adi)
{
    duyur("BUYU: %s -> %s [%s] can %.0f", yapan->ad, hedef->ad, buyu_adi, hedef->can);
    yapan->enerji -= 1;
    if (strcmp(buyu_adi, "Şifa") == 0) hedef->can += 3;
    else hedef->can -= 4;
    return 1;
}

/* Eylem yapmayan dinlenir: 1 enerji */
void eylem_yok(Oyuncu *o)
{
    duyur("EYLEM YOK: %s enerji %.1f", o->ad, o->enerji);
    o->enerji += 1;
}
