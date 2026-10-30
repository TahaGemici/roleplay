/* Birim testleri:  make test */
#include "dosya.h"
#include "duvar.h"
#include "geometri.h"
#include "kesif.h"
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

}

/* Yolun hiçbir parçası duvarları kesmiyor mu */
static int yol_temiz(Duvarlar *d, const Yol *y)
{
    for (int i = 0; i + 1 < y->n; i++)
        if (!duvar_gorus_acik(d, y->xy[2 * i], y->xy[2 * i + 1], y->xy[2 * i + 2], y->xy[2 * i + 3])) return 0;
    return 1;
}

static void test_duvar(void)
{
    /* x=5'te dikey bir duvar ve (1,1) köşesinde birleşen iki duvar */
    const double p1[] = { 5, 0, 5, 10, 1, 1, 1, 5, 1, 1, 5, 1 };
    Duvarlar *d = duvarlar_kur(p1, 3, 20, 20);
    KONTROL(!duvar_gorus_acik(d, 2, 5, 8, 5), "duvar görüşü keser");
    KONTROL(duvar_gorus_acik(d, 2, 5, 4, 5), "duvarın önü görülür");
    KONTROL(duvar_gorus_acik(d, 2, 12, 8, 12), "duvarın ötesinden görülür");
    KONTROL(!duvar_gorus_acik(d, 2, 10, 8, 10), "duvarın ucunu sıyıran çizgi kesilir");
    KONTROL(duvar_gorus_acik(d, 5, 5, 8, 5) && duvar_gorus_acik(d, 5, 5, 2, 5), "duvarın üstündeki iki yanı görür");
    KONTROL(!duvar_gorus_acik(d, 0.5, 0.5, 2.5, 2.5), "iki duvarın birleştiği köşeden görülmez");
    duvarlar_sil(d);

    /* Görüş çokgeni: (12,5)-(12,15) duvarı daireyi tam ortadan keser */
    const double p2[] = { 12, 5, 12, 15 };
    d = duvarlar_kur(p2, 1, 30, 30);
    double *xy;
    unsigned char gorulen[1] = { 0 };
    int n = duvar_gorus_cokgeni(d, 10, 10, 5, &xy, gorulen);
    int icinde = n >= 3, en_sag = 0;
    for (int i = 0; i < n; i++) {
        if (xy[2 * i] > 12 + 1e-6) icinde = 0;
        if (xy[2 * i] > 11.99) en_sag = 1;
        if (hypot(xy[2 * i] - 10, xy[2 * i + 1] - 10) > 5 + 1e-6) icinde = 0;
    }
    KONTROL(icinde && en_sag && gorulen[0], "görüş çokgeni duvarda biter");
    Kesif k = { 0 };
    kesif_kur(&k, 30, 30);
    KONTROL(kesif_cokgen(&k, xy, n), "çokgen ızgaraya boyanır");
    KONTROL(kesif_var(&k, 11, 10) && kesif_var(&k, 10, 14.5) && !kesif_var(&k, 13, 10) && !kesif_var(&k, 10, 15.6),
            "duvarın arkası ve menzil dışı keşfedilmez");
    Buf b;
    buf_init(&b);
    kesif_kodla(&b, &k);
    Kesif k2 = { 0 };
    kesif_kur(&k2, 30, 30);
    KONTROL(kesif_coz(&k2, b.p) && !memcmp(k.bit, k2.bit, (size_t)(k.g * k.y + 7) / 8), "keşif sıkıştırılıp açılır");
    kesif_olcekle(&k2, 30, 30, 60, 60);
    KONTROL(kesif_var(&k2, 22, 20) && !kesif_var(&k2, 26, 20), "harita büyüyünce keşif resimle birlikte büyür");
    buf_free(&b);
    kesif_bosalt(&k);
    kesif_bosalt(&k2);
    free(xy);
    n = duvar_gorus_cokgeni(d, 10, 10, 0, &xy, NULL);
    KONTROL(n == 0, "sıfır menzil boş çokgen");
    free(xy);
    duvarlar_sil(d);

    /* Yol: yukarıdan inen bir duvarın altından dolaşır; harita kenarından sızmaz */
    const double p3[] = { 10, 0, 10, 15 };
    d = duvarlar_kur(p3, 1, 20, 20);
    Yol y;
    KONTROL(duvar_yol_bul(d, 5, 5, 15, 5, &y) && y.n >= 3 && y.uzunluk > 22 && y.uzunluk < 24.5 && yol_temiz(d, &y),
            "yol duvarın ucundan dolaşır");
    int alttan = 1;
    for (int i = 0; i < y.n; i++)
        if (fabs(y.xy[2 * i] - 10) < 0.5 && y.xy[2 * i + 1] < 15) alttan = 0;
    KONTROL(alttan, "yol duvarın ucunu açıktan döner");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 5, 5, 8, 9, &y) && y.n == 2, "engel yoksa yol düz");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 9.95, 5, 5, 5, &y) && y.n == 2, "duvarın dibinden uzaklaşılır");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 10, 5, 15, 5, &y) && y.n == 2, "duvarın üstündeki iki yana da çıkar");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 9.95, 5, 15, 5, &y) && y.n >= 3, "duvarın dibindeki öbür yana geçemez");
    yol_bosalt(&y);
    double cx, cy;
    duvar_carpma(d, 5, 5, 15, 5, &cx, &cy);
    KONTROL(fabs(cx - 9.9) < 1e-6 && fabs(cy - 5) < 1e-9, "itilen duvarda durur");
    duvar_carpma(d, 5, 5, 10, 5, &cx, &cy);
    KONTROL(fabs(cx - 9.99) < 1e-9 && fabs(cy - 5) < 1e-9, "duvarın üstüne itilen önünde kalır");

    /* Tam duvarın üstüne yürüyen, geldiği yanda duvardan biraz uzakta durur; oradan öbür yana düz geçemez (konum
       float'a yuvarlansa da) */
    KONTROL(duvar_yol_bul(d, 5, 5, 10, 5, &y) && y.n == 2 && fabs(y.xy[2] - 9.99) < 1e-9 && fabs(y.xy[3] - 5) < 1e-9 &&
                fabs(y.uzunluk - 4.99) < 1e-9,
            "duvarın üstündeki hedefe varılmaz, önünde durulur");
    Yol y2;
    KONTROL(duvar_yol_bul(d, (float)y.xy[2], (float)y.xy[3], 15, 5, &y2) && y2.n >= 3 && yol_temiz(d, &y2),
            "duvarın üstüne yürüyen öbür yana geçemez");
    yol_bosalt(&y2);
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 9.99, 3, 10, 8, &y) && y.n == 2 && fabs(y.xy[2] - 9.99) < 1e-9 && fabs(y.xy[3] - 8) < 1e-9,
            "duvar boyunca kayan yol duvara yapışmaz");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 15, 3, 10.001, 9, &y) && y.n == 2 && y.xy[2] > 10.005 - 1e-9, "duvarın dibindeki hedef öbür yandan da itilir");
    yol_bosalt(&y);
    duvarlar_sil(d);

    /* İki duvarın birleştiği köşe: köşeye yürüyen içeride kalır, kapalı bölgeden çıkamaz */
    const double p3b[] = { 5, 0, 5, 5, 5, 5, 20, 5 };
    d = duvarlar_kur(p3b, 2, 20, 20);
    KONTROL(duvar_yol_bul(d, 7, 3, 5, 5, &y) && y.xy[2 * y.n - 2] > 5.004 && y.xy[2 * y.n - 1] < 4.996,
            "köşeye yürüyen içeride durur");
    KONTROL(!duvar_yol_bul(d, (float)y.xy[2 * y.n - 2], (float)y.xy[2 * y.n - 1], 3, 7, &y2), "köşeden dışarı sızılmaz");
    yol_bosalt(&y);
    duvarlar_sil(d);

    /* Kare ortalarından geçen çapraz duvar: ortaya oturan hedef geldiği yanda kalır */
    const double p3c[] = { 2, 2, 6, 6 };
    d = duvarlar_kur(p3c, 1, 10, 10);
    KONTROL(duvar_yol_bul(d, 5, 3, 4.5, 4.5, &y) && y.n == 2 && y.xy[2] - y.xy[3] > 0.01, "çapraz duvarın üstündeki hedef");
    KONTROL(duvar_yol_bul(d, (float)y.xy[2], (float)y.xy[3], 3, 5, &y2) && y2.n >= 3 && yol_temiz(d, &y2),
            "çapraz duvarın üstünden öbür yana geçilmez");
    yol_bosalt(&y2);
    yol_bosalt(&y);
    KONTROL(duvar_yakinlik(d, 4, 4.2) < 0.15 && duvar_yakinlik(d, 9, 1) >= 0.4 - 1e-12, "duvara yakınlık");
    duvarlar_sil(d);

    /* Yürümeden konan nokta: duvarın üstündeyse soluna, dibindeyse kendi yanına alınır; uzaktaysa kımıldamaz */
    const double p3d[] = { 5, 0, 5, 10 };
    d = duvarlar_kur(p3d, 1, 10, 10);
    double ux = 5, uy = 5;
    duvar_uzaklastir(d, &ux, &uy);
    KONTROL(fabs(ux - 4.99) < 1e-9 && fabs(uy - 5) < 1e-9, "duvarın üstüne konan bir yanına alınır");
    ux = 5.002;
    duvar_uzaklastir(d, &ux, &uy);
    KONTROL(fabs(ux - 5.01) < 1e-9, "duvarın dibine konan kendi yanında kalır");
    ux = 7;
    duvar_uzaklastir(d, &ux, &uy);
    KONTROL(ux == 7 && uy == 5, "duvardan uzaktaki yerinde kalır");
    duvarlar_sil(d);

    /* Kapalı oda: içine yol yok */
    const double p4[] = { 12, 2, 18, 2, 18, 2, 18, 8, 18, 8, 12, 8, 12, 8, 12, 2 };
    d = duvarlar_kur(p4, 4, 20, 20);
    KONTROL(!duvar_yol_bul(d, 5, 5, 15, 5, &y), "kapalı odaya yol yok");
    KONTROL(duvar_yol_bul(d, 13, 3, 17, 7, &y) && y.n == 2, "oda içinde yürünür");
    yol_bosalt(&y);
    KONTROL(duvar_yol_bul(d, 5, 5, 19, 9, &y) && yol_temiz(d, &y), "odanın etrafından dolaşılır");
    yol_bosalt(&y);
    duvarlar_sil(d);

    /* Kapı: 1 kare açıklıktan geçilir; 0.15'lik aralıktan geçilmez */
    const double p5[] = { 10, 0, 10, 9, 10, 10, 10, 20 };
    d = duvarlar_kur(p5, 2, 20, 20);
    KONTROL(duvar_yol_bul(d, 5, 2, 15, 18, &y) && yol_temiz(d, &y), "kapıdan geçilir");
    int kapidan = 0;
    for (int i = 0; i + 1 < y.n; i++) {
        double ax = y.xy[2 * i], ay = y.xy[2 * i + 1], bx = y.xy[2 * i + 2], by = y.xy[2 * i + 3];
        if ((ax - 10) * (bx - 10) <= 0 && bx != ax) {
            double t = (10 - ax) / (bx - ax), gy = ay + t * (by - ay);
            if (gy > 9 && gy < 10) kapidan = 1;
        }
    }
    KONTROL(kapidan, "yol kapının içinden geçer");
    yol_bosalt(&y);
    duvarlar_sil(d);
    const double p6[] = { 10, 0, 10, 9.9, 10, 10.05, 10, 20 };
    d = duvarlar_kur(p6, 2, 20, 20);
    KONTROL(!duvar_yol_bul(d, 5, 2, 15, 18, &y), "dar aralıktan geçilmez");
    duvarlar_sil(d);

    /* Yürüyüşün görünen parçaları: (8,4)-(8,6) duvarının gölgesi x=10'daki yolu 3.33..6.67 arasında örter */
    const double p7[] = { 8, 4, 8, 6 };
    d = duvarlar_kur(p7, 1, 20, 20);
    double t[8];
    int tn = duvar_gorunen(d, 5, 5, 10, 10, 0, 10, 10, t, 4);
    KONTROL(tn == 2 && fabs(t[0]) < 1e-9 && fabs(t[1] - 1.0 / 3) < 1e-6 && fabs(t[2] - 2.0 / 3) < 1e-6 && fabs(t[3] - 1) < 1e-9,
            "duvarın gölgesindeki yürüyüş görünmez");
    tn = duvar_gorunen(d, 5, 5, 6, 10, 0, 10, 10, t, 4);
    double kenar = (5 - sqrt(11.0)) / 10;
    KONTROL(tn == 2 && fabs(t[0] - kenar) < 1e-6 && fabs(t[1] - 1.0 / 3) < 1e-6 && fabs(t[2] - 2.0 / 3) < 1e-6 &&
                fabs(t[3] - (1 - kenar)) < 1e-6,
            "menzil ve gölge birlikte");
    tn = duvar_gorunen(d, 5, 5, 3, 10, 0, 10, 10, t, 4);
    KONTROL(tn == 0, "menzil dışındaki yürüyüş görünmez");
    tn = duvar_gorunen(d, 5, 5, 10, 6, 0, 6, 10, t, 4);
    KONTROL(tn == 1 && fabs(t[0]) < 1e-9 && fabs(t[1] - 1) < 1e-9, "duvarın önündeki yürüyüş görünür");
    duvarlar_sil(d);
}

static double parca_parca(double ax, double ay, double bx, double by, const double *w)
{
    double m = nokta_parca_mesafesi(ax, ay, w[0], w[1], w[2], w[3]);
    m = fmin(m, nokta_parca_mesafesi(bx, by, w[0], w[1], w[2], w[3]));
    m = fmin(m, nokta_parca_mesafesi(w[0], w[1], ax, ay, bx, by));
    m = fmin(m, nokta_parca_mesafesi(w[2], w[3], ax, ay, bx, by));
    double d1 = (w[2] - w[0]) * (ay - w[1]) - (w[3] - w[1]) * (ax - w[0]);
    double d2 = (w[2] - w[0]) * (by - w[1]) - (w[3] - w[1]) * (bx - w[0]);
    double d3 = (bx - ax) * (w[1] - ay) - (by - ay) * (w[0] - ax);
    double d4 = (bx - ax) * (w[3] - ay) - (by - ay) * (w[2] - ax);
    if (d1 * d2 < 0 && d3 * d4 < 0) return 0;
    return m;
}

/* Rastgele duvarlarla: ince bir ızgarada açıklığı korunarak varılabilen yere yol bulunmalı; bulunan yol duvar
   kesmemeli ve duvarlara (uçlardaki kenarlar dışında) DUVAR_ACIKLIK'tan çok yaklaşmamalı. */
static void test_duvar_rastgele(void)
{
    enum { N = 200 }; /* 20 karelik haritada 0.1'lik hücreler */
    static unsigned char bos[N * N], gorulen[N * N];
    static int kuyruk[N * N];
    srand(7);
    int bulunmali = 0, eksik = 0, kesen = 0, yakin = 0, kisa = 0;
    for (int deneme = 0; deneme < 120; deneme++) {
        double p[4 * 16];
        int wn = 4 + rand() % 12;
        for (int i = 0; i < wn; i++) {
            double x = 1 + rand() % 18, y = 1 + rand() % 18;
            int yatay = rand() % 2, boy = 2 + rand() % 7;
            p[4 * i] = x;
            p[4 * i + 1] = y;
            p[4 * i + 2] = yatay ? fmin(20, x + boy) : x;
            p[4 * i + 3] = yatay ? y : fmin(20, y + boy);
            if (rand() % 4 == 0) { /* çapraz duvar */
                p[4 * i + 2] = fmin(20, x + boy * 0.7);
                p[4 * i + 3] = fmin(20, y + boy * 0.6);
            }
        }
        Duvarlar *d = duvarlar_kur(p, wn, 20, 20);
        for (int c = 0; c < N * N; c++) {
            double cx = (c % N + 0.5) / 10, cy = (c / N + 0.5) / 10;
            bos[c] = cx > 0.2 && cy > 0.2 && cx < 19.8 && cy < 19.8;
            for (int i = 0; i < wn && bos[c]; i++)
                if (nokta_parca_mesafesi(cx, cy, p[4 * i], p[4 * i + 1], p[4 * i + 2], p[4 * i + 3]) < 0.2) bos[c] = 0;
        }
        for (int q = 0; q < 6; q++) {
            int s, g;
            do s = rand() % (N * N);
            while (!bos[s]);
            do g = rand() % (N * N);
            while (!bos[g]);
            double sx = (s % N + 0.5) / 10, sy = (s / N + 0.5) / 10, gx = (g % N + 0.5) / 10, gy = (g / N + 0.5) / 10;
            memset(gorulen, 0, sizeof gorulen);
            int bas = 0, son = 0;
            kuyruk[son++] = s;
            gorulen[s] = 1;
            while (bas < son && !gorulen[g]) {
                int c = kuyruk[bas++], cx = c % N, cy = c / N;
                const int kom[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
                for (int k = 0; k < 4; k++) {
                    int nx = cx + kom[k][0], ny = cy + kom[k][1];
                    if (nx < 0 || ny < 0 || nx >= N || ny >= N) continue;
                    int nc = ny * N + nx;
                    if (!bos[nc] || gorulen[nc]) continue;
                    gorulen[nc] = 1;
                    kuyruk[son++] = nc;
                }
            }
            Yol y;
            int var = duvar_yol_bul(d, sx, sy, gx, gy, &y);
            if (gorulen[g]) {
                bulunmali++;
                if (!var) eksik++;
            }
            if (!var) continue;
            if (!yol_temiz(d, &y)) kesen++;
            if (y.uzunluk < hypot(gx - sx, gy - sy) - 1e-9) kisa++;
            for (int i = 0; i + 1 < y.n; i++) {
                if (i == 0 || i + 2 == y.n) continue;
                for (int w = 0; w < wn; w++)
                    if (parca_parca(y.xy[2 * i], y.xy[2 * i + 1], y.xy[2 * i + 2], y.xy[2 * i + 3], p + 4 * w) < DUVAR_ACIKLIK - 1e-6) yakin++;
            }
            yol_bosalt(&y);
        }
        duvarlar_sil(d);
    }
    KONTROL(bulunmali > 300, "rastgele denemelerin çoğunda yol olmalı");
    KONTROL(eksik == 0, "varılabilen yere yol bulunur");
    KONTROL(kesen == 0, "bulunan yol duvar kesmez");
    KONTROL(yakin == 0, "yol duvarlara açıklıktan çok yaklaşmaz");
    KONTROL(kisa == 0, "yol düz çizgiden kısa olmaz");
    if (eksik || kesen || yakin) printf("  eksik %d, kesen %d, yakın %d (%d denemede)\n", eksik, kesen, yakin, bulunmali);
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
    test_duvar();
    test_duvar_rastgele();
    printf("%d kontrol geçti, %d kaldı\n", gecen, kalan);
    return kalan ? 1 : 0;
}
