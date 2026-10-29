/* Birim testleri:  make test */
#include "dosya.h"
#include "geometri.h"
#include "gorsel.h"
#include "json.h"
#include "ortak.h"
#include "zar.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gecen, kalan;

#define KONTROL(kosul, ad)                                                                                             \
    do {                                                                                                               \
        if (kosul) gecen++;                                                                                            \
        else {                                                                                                         \
            kalan++;                                                                                                   \
            printf("KALDI: %s  (%s:%d)\n", ad, __FILE__, __LINE__);                                                    \
        }                                                                                                              \
    } while (0)

static void test_sha1_base64(void)
{
    char h[41];
    sha1_hex("abc", h);
    KONTROL(!strcmp(h, "a9993e364706816aba3e25717850c26c9cd0d89d"), "SHA-1 test vektörü");
    sha1_hex("", h);
    KONTROL(!strcmp(h, "da39a3ee5e6b4b0d3255bfef95601890afd80709"), "SHA-1 boş girdi");
    /* RFC 6455 örneği */
    const char *k = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    unsigned char d[20];
    char b64[40];
    sha1(k, strlen(k), d);
    base64_kodla(d, 20, b64);
    KONTROL(!strcmp(b64, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo="), "WebSocket kabul anahtarı (RFC 6455)");
    /* 56 baytlık sınır durumu (iki bloklu dolgu) */
    sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq", h);
    KONTROL(!strcmp(h, "84983e441c3bd26ebaae4aa1f95129e5e54670f1"), "SHA-1 iki blok");
}

static void test_utf8(void)
{
    KONTROL(utf8_gecerli("Ayşe Çağrı İğneada", strlen("Ayşe Çağrı İğneada")), "Türkçe UTF-8 geçerli");
    KONTROL(!utf8_gecerli("\xC3", 1), "yarım karakter geçersiz");
    KONTROL(!utf8_gecerli("\xC0\xAF", 2), "fazla uzun kodlama geçersiz");
    KONTROL(!utf8_gecerli("\xED\xA0\x80", 3), "vekil çift geçersiz");
    KONTROL(sabit_esit("ejderha", "ejderha") && !sabit_esit("ejderha", "ejderhA") && !sabit_esit("a", "ab"),
            "sabit zamanlı karşılaştırma");
}

static void test_json(void)
{
    const char *m = "{\"t\":\"zar\",\"ifade\":\"1d20+5 \\u015fans\",\"gizli\":true,\"x\":3,\"liste\":[1,2.5,null,\"\\ud83c\\udfb2\"]}";
    Json *j = json_ayristir(m, strlen(m));
    KONTROL(j && j->tur == JSON_OBJ, "nesne ayrıştırılır");
    KONTROL(j && !strcmp(json_metin(j, "ifade"), "1d20+5 şans"), "\\u kaçışı UTF-8'e çevrilir");
    long long x = 0;
    KONTROL(j && json_tamsayi(j, "x", &x) && x == 3, "tam sayı okunur");
    KONTROL(j && json_mantik(j, "gizli", 0) == 1, "mantık değeri okunur");
    Json *l = json_al(j, "liste");
    KONTROL(l && l->adet == 4 && !strcmp(l->ogeler[3]->metin, "\xF0\x9F\x8E\xB2"), "vekil çift (emoji) çözülür");
    Buf b;
    buf_init(&b);
    json_yaz(&b, j);
    Json *k = json_ayristir(b.p, b.len);
    KONTROL(k && !strcmp(json_metin(k, "ifade"), "1d20+5 şans"), "yaz-oku döngüsü");
    json_sil(k);
    buf_free(&b);
    json_sil(j);

    const char *bozuk[] = { "", "{", "{\"a\":}", "[1,]", "{\"a\" 1}", "\"\\u0000\"", "\"\x01\"", "01x", "[1] 2",
                            "\"\xff\"", "{\"a\":tru}", "nul" };
    for (size_t i = 0; i < sizeof bozuk / sizeof bozuk[0]; i++) {
        Json *z = json_ayristir(bozuk[i], strlen(bozuk[i]));
        KONTROL(z == NULL, "bozuk JSON reddedilir");
        json_sil(z);
    }
    /* Çok derin iç içe yapı reddedilir (yığın taşmasın) */
    char derin[200];
    memset(derin, '[', 100);
    memset(derin + 100, ']', 100);
    Json *z = json_ayristir(derin, 200);
    KONTROL(z == NULL, "aşırı derinlik reddedilir");
    json_sil(z);

    buf_init(&b);
    buf_json_str(&b, "tırnak \" ters \\ satır\n kontrol\x01");
    KONTROL(!strcmp(b.p, "\"tırnak \\\" ters \\\\ satır\\n kontrol\\u0001\""), "JSON kaçışları yazılır");
    buf_free(&b);
}

static void test_zar(void)
{
    ZarSonuc s;
    char h[200];
    int gorulen[21] = { 0 };
    for (int i = 0; i < 4000; i++) {
        zar_ifadesi_at("d20", &s, h, sizeof h);
        if (s.toplam >= 1 && s.toplam <= 20) gorulen[s.toplam]++;
    }
    int hepsi = 1;
    for (int i = 1; i <= 20; i++)
        if (!gorulen[i]) hepsi = 0;
    KONTROL(hepsi, "d20 1-20 arası her değeri üretir");

    int dogru = 1;
    for (int i = 0; i < 500; i++) {
        zar_ifadesi_at("4d6kh3", &s, h, sizeof h);
        int v[4], tutulan = 0, toplam = 0;
        for (int k = 0; k < 4; k++) {
            v[k] = s.deger[k];
            if (s.tutuldu[k]) tutulan++;
        }
        int en_kucuk = 0;
        for (int k = 1; k < 4; k++)
            if (v[k] < v[en_kucuk]) en_kucuk = k;
        for (int k = 0; k < 4; k++)
            if (k != en_kucuk) toplam += v[k];
        if (tutulan != 3 || s.toplam != toplam) dogru = 0;
    }
    KONTROL(dogru, "4d6kh3 en yüksek üçü tutar");

    dogru = 1;
    for (int i = 0; i < 500; i++) {
        zar_ifadesi_at("2d20kl1", &s, h, sizeof h);
        int m = s.deger[0] < s.deger[1] ? s.deger[0] : s.deger[1];
        if (s.toplam != m) dogru = 0;
        zar_ifadesi_at("1d6 + 4 - 2", &s, h, sizeof h);
        if (s.toplam != s.deger[0] + 2) dogru = 0;
        zar_ifadesi_at("4d6dl1", &s, h, sizeof h);
        int en = s.deger[0], top = 0;
        for (int k = 0; k < 4; k++) {
            top += s.deger[k];
            if (s.deger[k] < en) en = s.deger[k];
        }
        if (s.toplam != top - en) dogru = 0;
    }
    KONTROL(dogru, "kl, sabit sayı ve dl doğru hesaplanır");

    KONTROL(zar_ifadesi_at("1d20+5 kılıç saldırısı", &s, h, sizeof h) && !strcmp(s.etiket, "kılıç saldırısı"),
            "ifadeden sonrası etiket");
    KONTROL(zar_ifadesi_at("2d6kılıç", &s, h, sizeof h) && !strcmp(s.etiket, "kılıç") && s.parca[0].tut == 2,
            "Türkçe harfle başlayan etiket eke karışmaz");
    KONTROL(zar_ifadesi_at("2d20k", &s, h, sizeof h) && !strcmp(s.gosterim, "2d20kh1"), "k = kh1");
    KONTROL(zar_ifadesi_at("d%", &s, h, sizeof h) && s.parca[0].yuz == 100, "d% yüzlük zar");
    KONTROL(zar_ifadesi_at("1d20 + 1d4 - 1", &s, h, sizeof h) && !strcmp(s.gosterim, "1d20 + 1d4 \xE2\x88\x92 1"),
            "gösterim");
    KONTROL(zar_ifadesi_at("10 \xE2\x88\x92 4", &s, h, sizeof h) && s.toplam == 6, "tipografik eksi");
    KONTROL(zar_ifadesi_at("-3", &s, h, sizeof h) && s.toplam == -3, "baştaki eksi");
    KONTROL(zar_ifadesi_at("d20 #algı", &s, h, sizeof h) && !strcmp(s.etiket, "algı"), "# ile etiket");

    const char *hatali[] = { "", "merhaba", "1d20+", "0d6", "1d1", "1d5000", "101d6", "2d6kh3", "3d6dl3", "999999",
                             "1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1", "60d6+50d6" };
    for (size_t i = 0; i < sizeof hatali / sizeof hatali[0]; i++) {
        int ok = zar_ifadesi_at(hatali[i], &s, h, sizeof h);
        KONTROL(!ok && h[0], hatali[i]);
    }

    Buf b;
    buf_init(&b);
    zar_ifadesi_at("2d6+1 ateş", &s, h, sizeof h);
    zar_json_yaz(&b, &s);
    Json *j = json_ayristir(b.p, b.len);
    KONTROL(j && json_al(j, "parcalar") && json_al(j, "parcalar")->adet == 2, "zar sonucu geçerli JSON");
    json_sil(j);
    buf_free(&b);
}

static void test_gorsel(void)
{
    GorselBilgi g;
    KONTROL(gorsel_bilgi("test/ornekler/kucuk.png", &g) && g.genislik == 3 && g.yukseklik == 2, "PNG boyutu");
    KONTROL(gorsel_bilgi("test/ornekler/kucuk.jpg", &g) && g.genislik == 5 && g.yukseklik == 4, "JPEG boyutu");
    KONTROL(gorsel_bilgi("test/ornekler/kucuk.gif", &g) && g.genislik == 8 && g.yukseklik == 2, "GIF boyutu");
    KONTROL(gorsel_bilgi("test/ornekler/kayipli.webp", &g) && g.genislik == 7 && g.yukseklik == 3, "WEBP (VP8)");
    KONTROL(gorsel_bilgi("test/ornekler/kayipsiz.webp", &g) && g.genislik == 9 && g.yukseklik == 5, "WEBP (VP8L)");
    KONTROL(gorsel_bilgi("test/ornekler/saydam.webp", &g) && g.genislik == 6 && g.yukseklik == 6, "WEBP (VP8X)");
    KONTROL(gorsel_bilgi("test/ornekler/donmus.jpg", &g) && g.genislik == 2 && g.yukseklik == 6,
            "EXIF ile döndürülmüş JPEG");
    KONTROL(!gorsel_bilgi("test/ornekler/sahte.png", &g), "görsel olmayan dosya reddedilir");
    KONTROL(!gorsel_bilgi("test/ornekler/yok.png", &g), "olmayan dosya");
}

static void test_yollar(void)
{
    KONTROL(goreli_yol_gecerli(""), "kök geçerli");
    KONTROL(goreli_yol_gecerli("esyalar/silahlar/Uzun Kılıç.png"), "Türkçe alt yol geçerli");
    const char *kotu[] = { "../durum.json", "esyalar/../../etc/passwd", "/etc/passwd", "esyalar//x", "esyalar/",
                           ".gizli", "esyalar/.yukleniyor-1", "a\\b", "a\nb", "esyalar/x ", "\xff\xfe" };
    for (size_t i = 0; i < sizeof kotu / sizeof kotu[0]; i++) KONTROL(!goreli_yol_gecerli(kotu[i]), kotu[i]);
    char t[YOL_AZAMI];
    KONTROL(tam_yol_kur("/kok", "esyalar/a.png", t, sizeof t) && !strcmp(t, "/kok/esyalar/a.png"), "tam yol");
    KONTROL(!tam_yol_kur("/kok", "../x", t, sizeof t), "kök dışına çıkılamaz");
    Buf b;
    buf_init(&b);
    url_yol_kodla(&b, "esyalar/Kılıç #1?.png");
    KONTROL(!strcmp(b.p, "esyalar/K%C4%B1l%C4%B1%C3%A7%20%231%3F.png"), "URL kodlama");
    buf_free(&b);
}

static int yakin(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

static void test_geometri(void)
{
    KONTROL(yakin(mesafe(0, 0, 3, 4), 5), "mesafe 3-4-5");
    KONTROL(yakin(nokta_parca_mesafesi(1, 1, 0, 0, 2, 0), 1), "noktanın parçaya uzaklığı (orta)");
    KONTROL(yakin(nokta_parca_mesafesi(-3, 4, 0, 0, 2, 0), 5), "noktanın parçaya uzaklığı (uç dışı)");
    KONTROL(yakin(nokta_parca_mesafesi(2, 2, 1, 1, 1, 1), sqrt(2)), "nokta parçası");

    double t0, t1;
    /* (0,0)-(10,0) yolu, (5,0) merkezli 2 yarıçaplı daireyi 3..7 arasında keser */
    KONTROL(parca_daire_kesisimi(0, 0, 10, 0, 5, 0, 2, &t0, &t1) && yakin(t0, 0.3) && yakin(t1, 0.7), "yol daireyi keser");
    KONTROL(parca_daire_kesisimi(4, 0, 10, 0, 5, 0, 2, &t0, &t1) && yakin(t0, 0) && yakin(t1, 0.5), "içeriden çıkan yol");
    KONTROL(parca_daire_kesisimi(0, 0, 5, 0, 5, 0, 2, &t0, &t1) && yakin(t0, 0.6) && yakin(t1, 1), "dışarıdan giren yol");
    KONTROL(parca_daire_kesisimi(4, 0, 6, 0, 5, 0, 2, &t0, &t1) && yakin(t0, 0) && yakin(t1, 1), "tamamen içeride");
    KONTROL(!parca_daire_kesisimi(0, 5, 10, 5, 5, 0, 2, &t0, &t1), "uzaktan geçen yol kesmez");
    KONTROL(!parca_daire_kesisimi(0, 0, 2, 0, 5, 0, 2, &t0, &t1), "daireye varmayan yol");
    KONTROL(!parca_daire_kesisimi(0, 2, 10, 2, 5, 0, 2, &t0, &t1), "teğet geçen yol görülmez");

    Kapsul dis = { 0, 0, 10, 0, 3 };
    Kapsul ic1 = { 2, 0, 8, 1, 1.5f };
    Kapsul ic2 = { 2, 0, 13, 0, 1 };
    Kapsul nokta = { 10, 0, 10, 0, 3 };
    KONTROL(kapsul_icinde(&ic1, &dis), "içteki kapsül");
    KONTROL(!kapsul_icinde(&ic2, &dis), "taşan kapsül");
    KONTROL(kapsul_icinde(&nokta, &dis), "uçtaki görüş dairesi zaten keşfedilmiş");
}

int main(void)
{
    test_sha1_base64();
    test_utf8();
    test_json();
    test_zar();
    test_gorsel();
    test_yollar();
    test_geometri();
    printf("%d kontrol geçti, %d kaldı\n", gecen, kalan);
    return kalan ? 1 : 0;
}
