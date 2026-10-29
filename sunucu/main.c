/*
 * Zar Masası sunucusu.
 *
 *   /           oyuncu ekranı (giriş: isim + oyun şifresi)
 *   /gm         GM ekranı (giriş: .env dosyasındaki GM_SIFRESI)
 *   /ws         WebSocket: oyunun bütün canlı olayları
 *   /oyun/...   oyun klasöründeki dosyalar (yetkiye göre)
 *   /api/...    giriş ve GM dosya yöneticisi
 *
 * Her bağlantı kendi iş parçacığında çalışır; oyunun durumu tek bir kilitle korunur.
 */

#include "dosya.h"
#include "gorsel.h"
#include "http.h"
#include "json.h"
#include "oyun.h"
#include "ws.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netdb.h>
#include <poll.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define AZAMI_BAGLANTI 300
#define JSON_AZAMI (64 * 1024)

static atomic_int durdur; /* sinyal işleyicisi yazar, kaydedici iş parçacığı okur (kilitsiz atomik) */
static atomic_int baglanti_sayisi;

static const char *SAYFA_BASLIKLARI =
    "Content-Security-Policy: default-src 'self'; img-src 'self' data: blob:; style-src 'self'; script-src 'self'; "
    "connect-src 'self'; font-src 'self'; media-src 'self'; object-src 'none'; base-uri 'none'; form-action 'self'; "
    "frame-ancestors 'none'\r\n"
    "Cache-Control: no-cache\r\n";

/* Oyun klasöründeki dosyalar betik çalıştıramaz: sayfa korumalı alanda (sandbox) açılır. */
static const char *DOSYA_CSP =
    "Content-Security-Policy: sandbox; default-src 'none'; img-src 'self' data:; style-src 'unsafe-inline'; "
    "media-src 'self'\r\n";

/* ------------------------------------------------------------------ ayarlar */

static void env_dosyasi_oku(const char *yol)
{
    FILE *f = fopen(yol, "r");
    if (!f) return;
    char satir[1024];
    while (fgets(satir, sizeof satir, f)) {
        char *p = satir;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || !*p) continue;
        if (!strncmp(p, "export ", 7)) p += 7;
        char *esit = strchr(p, '=');
        if (!esit) continue;
        *esit = '\0';
        char *ad = p, *deger = esit + 1;
        size_t an = strlen(ad);
        while (an && (ad[an - 1] == ' ' || ad[an - 1] == '\t')) ad[--an] = '\0';
        while (*deger == ' ' || *deger == '\t') deger++;
        size_t dn = strlen(deger);
        while (dn && (deger[dn - 1] == '\n' || deger[dn - 1] == '\r')) deger[--dn] = '\0';
        if (dn >= 2 && ((deger[0] == '"' && deger[dn - 1] == '"') || (deger[0] == '\'' && deger[dn - 1] == '\''))) {
            deger[dn - 1] = '\0';
            deger++;
        } else {
            char *yorum = strstr(deger, " #");
            if (yorum) *yorum = '\0';
            dn = strlen(deger);
            while (dn && (deger[dn - 1] == ' ' || deger[dn - 1] == '\t')) deger[--dn] = '\0';
        }
        if (an) setenv(ad, deger, 0);
    }
    fclose(f);
}

static const char *ortam(const char *ad, const char *varsayilan)
{
    const char *v = getenv(ad);
    return v && *v ? v : varsayilan;
}

/* ------------------------------------------------------------------ küçük yardımcılar */

static int koken_uygun(const HttpIstek *r)
{
    const char *origin = http_baslik(r, "Origin");
    if (!origin) return 1;
    const char *p = strstr(origin, "://");
    if (!p) return 0;
    p += 3;
    const char *host = http_baslik(r, "Host"), *xfh = http_baslik(r, "X-Forwarded-Host");
    if ((host && !strcasecmp(p, host)) || (xfh && !strcasecmp(p, xfh))) return 1;
    fprintf(stderr, "[uyarı] Başka bir adresten gelen istek reddedildi: %s (Host: %s)\n", origin, host ? host : "-");
    return 0;
}

static void cerez_basligi(const HttpIstek *r, Buf *b, const char *ad, const char *deger, int omur)
{
    const char *proto = http_baslik(r, "X-Forwarded-Proto");
    buf_printf(b, "Set-Cookie: %s=%s; Path=/; Max-Age=%d; HttpOnly; SameSite=Strict%s\r\n", ad, deger, omur,
               proto && !strcasecmp(proto, "https") ? "; Secure" : "");
}

static int gm_mi(const HttpIstek *r)
{
    char token[100];
    if (!http_cerez(r, "zm_gm", token, sizeof token)) return 0;
    pthread_mutex_lock(&oyun_kilit);
    int ok = oturum_dogrula(token, 1, NULL, NULL);
    pthread_mutex_unlock(&oyun_kilit);
    return ok;
}

static Json *json_govde(HttpIstek *r)
{
    char *govde;
    long long n = http_govde_oku(r, &govde, JSON_AZAMI);
    if (n < 0) return NULL;
    Json *j = json_ayristir(govde, (size_t)n);
    free(govde);
    if (j && j->tur != JSON_OBJ) {
        json_sil(j);
        return NULL;
    }
    return j;
}

static void tamam(int fd)
{
    http_json(fd, 200, "{\"ok\":true}", NULL);
}

/* ------------------------------------------------------------------ giriş denemesi sınırı */

static pthread_mutex_t deneme_kilit = PTHREAD_MUTEX_INITIALIZER;
static struct {
    char ip[64];
    int sayi;
    long long bitis;
} denemeler[256];

static int deneme_engelli(const char *ip)
{
    pthread_mutex_lock(&deneme_kilit);
    int engel = 0;
    for (int i = 0; i < 256; i++) {
        if (strcmp(denemeler[i].ip, ip)) continue;
        if (simdi_sn() > denemeler[i].bitis) denemeler[i].ip[0] = '\0';
        else engel = denemeler[i].sayi >= 10;
        break;
    }
    pthread_mutex_unlock(&deneme_kilit);
    return engel;
}

static void deneme_basarisiz(const char *ip)
{
    pthread_mutex_lock(&deneme_kilit);
    int bos = -1;
    for (int i = 0; i < 256; i++) {
        if (!strcmp(denemeler[i].ip, ip)) {
            denemeler[i].sayi++;
            pthread_mutex_unlock(&deneme_kilit);
            return;
        }
        if (bos < 0 && (!denemeler[i].ip[0] || simdi_sn() > denemeler[i].bitis)) bos = i;
    }
    if (bos < 0) bos = (int)rastgele_aralik(256);
    kopyala(denemeler[bos].ip, sizeof denemeler[bos].ip, ip);
    denemeler[bos].sayi = 1;
    denemeler[bos].bitis = simdi_sn() + 15 * 60;
    pthread_mutex_unlock(&deneme_kilit);
}

/* ------------------------------------------------------------------ dosya gönderme */

static void tarih_yaz(time_t t, char *out, size_t n)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, n, "%a, %d %b %Y %H:%M:%S GMT", &tm);
}

/* Last-Modified / If-Modified-Since destekli dosya yanıtı */
static void dosya_yanitla(const HttpIstek *r, const char *tam, const char *tur, const char *ek)
{
    struct stat st;
    if (stat(tam, &st) != 0 || !S_ISREG(st.st_mode)) {
        http_hata(r->fd, 404, "Dosya bulunamadı.");
        return;
    }
    char tarih[64];
    tarih_yaz(st.st_mtime, tarih, sizeof tarih);
    const char *ims = http_baslik(r, "If-Modified-Since");
    Buf b;
    buf_init(&b);
    buf_printf(&b, "Last-Modified: %s\r\n", tarih);
    if (ek) buf_puts(&b, ek);
    if (ims && !strcmp(ims, tarih)) {
        http_yanit(r->fd, 304, NULL, b.p, NULL, 0);
    } else if (!http_dosya_gonder(r->fd, tam, tur, b.p)) {
        http_hata(r->fd, 404, "Dosya bulunamadı.");
    }
    buf_free(&b);
}

static void web_dosyasi(HttpIstek *r, const char *goreli)
{
    char tam[YOL_AZAMI];
    if (!tam_yol_kur(yap.web_klasoru, goreli, tam, sizeof tam)) {
        http_hata(r->fd, 404, "Bulunamadı.");
        return;
    }
    dosya_yanitla(r, tam, icerik_turu(goreli), "Cache-Control: no-cache\r\n");
}

static void oyun_dosyasi(HttpIstek *r)
{
    const char *goreli = r->yol + 6; /* "/oyun/" sonrası */
    if (!goreli[0] || !goreli_yol_gecerli(goreli)) {
        http_hata(r->fd, 404, "Bulunamadı.");
        return;
    }
    int izin = gm_mi(r);
    if (!izin) {
        char token[100], oyuncu[17];
        if (http_cerez(r, "zm_oyuncu", token, sizeof token)) {
            pthread_mutex_lock(&oyun_kilit);
            izin = oturum_dogrula(token, 0, oyuncu, NULL) && oyuncu_dosya_izni(oyuncu, goreli);
            pthread_mutex_unlock(&oyun_kilit);
        }
    }
    char tam[YOL_AZAMI];
    if (!izin || !tam_yol_kur(yap.oyun_klasoru, goreli, tam, sizeof tam)) {
        http_hata(r->fd, 404, "Bulunamadı.");
        return;
    }
    const char *tur = icerik_turu(goreli);
    int betik = !strncmp(tur, "text/html", 9) || !strncmp(tur, "text/javascript", 15);
    int indir = betik || http_sorgu(r, "indir", NULL, 0) || !strcmp(tur, "application/octet-stream");
    if (betik) tur = "application/octet-stream";
    Buf ek;
    buf_init(&ek);
    buf_puts(&ek, DOSYA_CSP);
    buf_puts(&ek, "Cache-Control: private, no-cache\r\n");
    if (indir) {
        const char *ad = strrchr(goreli, '/');
        ad = ad ? ad + 1 : goreli;
        buf_puts(&ek, "Content-Disposition: attachment; filename*=UTF-8''");
        url_yol_kodla(&ek, ad);
        buf_puts(&ek, "\r\n");
    }
    dosya_yanitla(r, tam, tur, ek.p);
    buf_free(&ek);
}

/* ------------------------------------------------------------------ giriş / çıkış */

/* Oyuncu (ad + oyun şifresi) ya da GM (GM şifresi) girişi; başarılıysa oturum çerezi verilir. */
static void giris(HttpIstek *r, int gm)
{
    if (deneme_engelli(r->ip)) {
        http_hata(r->fd, 429, "Çok fazla hatalı deneme yapıldı. 15 dakika sonra tekrar dene.");
        return;
    }
    Json *j = json_govde(r);
    if (!j) {
        http_hata(r->fd, 400, "İstek okunamadı.");
        return;
    }
    char token[65], hata[200] = "GM şifresi yanlış.";
    pthread_mutex_lock(&oyun_kilit);
    int durum = gm ? gm_giris(json_metin(j, "sifre"), token)
                   : oyuncu_giris(json_metin(j, "ad"), json_metin(j, "sifre"), token, hata, sizeof hata);
    pthread_mutex_unlock(&oyun_kilit);
    json_sil(j);
    if (durum != 200) {
        if (durum == 401) deneme_basarisiz(r->ip);
        http_hata(r->fd, durum, hata);
        return;
    }
    Buf ek;
    buf_init(&ek);
    cerez_basligi(r, &ek, gm ? "zm_gm" : "zm_oyuncu", token, 31536000);
    http_json(r->fd, 200, "{\"ok\":true}", ek.p);
    buf_free(&ek);
}

static void api_giris(HttpIstek *r)
{
    giris(r, 0);
}

static void api_gm_giris(HttpIstek *r)
{
    giris(r, 1);
}

static void api_cikis(HttpIstek *r)
{
    char rol[16] = "";
    http_sorgu(r, "rol", rol, sizeof rol);
    const char *cerez = !strcmp(rol, "gm") ? "zm_gm" : "zm_oyuncu";
    char token[100];
    if (http_cerez(r, cerez, token, sizeof token)) {
        pthread_mutex_lock(&oyun_kilit);
        oturum_sil(token);
        pthread_mutex_unlock(&oyun_kilit);
    }
    Buf ek;
    buf_init(&ek);
    cerez_basligi(r, &ek, cerez, "", 0);
    http_json(r->fd, 200, "{\"ok\":true}", ek.p);
    buf_free(&ek);
}

/* ------------------------------------------------------------------ GM dosya yöneticisi */

static int klasor_mu(const char *tam)
{
    struct stat st;
    return lstat(tam, &st) == 0 && S_ISDIR(st.st_mode);
}

static int var_mi(const char *tam)
{
    struct stat st;
    return lstat(tam, &st) == 0;
}

static void api_dosyalar(HttpIstek *r)
{
    char yol[YOL_AZAMI] = "", tam[YOL_AZAMI];
    http_sorgu(r, "yol", yol, sizeof yol);
    if (!tam_yol_kur(yap.oyun_klasoru, yol, tam, sizeof tam) || !klasor_mu(tam)) {
        http_hata(r->fd, 404, "Klasör bulunamadı.");
        return;
    }
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"yol\":");
    buf_json_str(&b, yol);
    buf_puts(&b, ",\"ogeler\":");
    if (!klasor_listele(tam, &b)) {
        buf_free(&b);
        http_hata(r->fd, 500, "Klasör okunamadı.");
        return;
    }
    buf_putc(&b, '}');
    http_json(r->fd, 200, b.p, NULL);
    buf_free(&b);
}

/* esyalar/ altına gidecek dosyanın yolu (klasörse içindeki en uzun yol da eklenerek) envantere sığmalı */
static int esya_yolu_cok_uzun(const char *goreli, const char *tasinan_tam)
{
    if (strncmp(goreli, "esyalar/", 8)) return 0;
    size_t ic = 0;
    if (tasinan_tam && klasor_mu(tasinan_tam)) {
        DosyaListesi l;
        dosyalari_bul(tasinan_tam, 0, &l);
        for (int i = 0; i < l.n; i++) {
            size_t n = strlen(l.yol[i]) + 1;
            if (n > ic) ic = n;
        }
        dosya_listesi_sil(&l);
    }
    return strlen(goreli) + ic > ESYA_YOLU_AZAMI;
}

#define ESYA_YOLU_HATASI \
    "Eşya dosyasının yolu (esyalar/sınıf/ad) en fazla 255 bayt olabilir; Türkçe harfler 2 bayt sayılır. Adı kısalt."

static void api_yukle(HttpIstek *r)
{
    char klasor[YOL_AZAMI] = "", ad[300] = "", goreli[YOL_AZAMI], tam_klasor[YOL_AZAMI], tam[YOL_AZAMI];
    http_sorgu(r, "yol", klasor, sizeof klasor);
    if (!http_sorgu(r, "ad", ad, sizeof ad) || !dosya_adi_gecerli(ad)) {
        http_hata(r->fd, 400, "Dosya adı geçersiz. Nokta ile başlayamaz; / ve kontrol karakterleri içeremez.");
        return;
    }
    int w = klasor[0] ? snprintf(goreli, sizeof goreli, "%s/%s", klasor, ad) : snprintf(goreli, sizeof goreli, "%s", ad);
    if (w < 0 || (size_t)w >= sizeof goreli || !tam_yol_kur(yap.oyun_klasoru, klasor, tam_klasor, sizeof tam_klasor) ||
        !tam_yol_kur(yap.oyun_klasoru, goreli, tam, sizeof tam)) {
        http_hata(r->fd, 400, "Yol geçersiz.");
        return;
    }
    if (korunan_yol(goreli)) {
        http_hata(r->fd, 403, "Bu ad oyunun kendi dosyasına ayrılmış.");
        return;
    }
    if (esya_yolu_cok_uzun(goreli, NULL)) {
        http_hata(r->fd, 400, ESYA_YOLU_HATASI);
        return;
    }
    if (!klasor_mu(tam_klasor)) {
        http_hata(r->fd, 404, "Klasör bulunamadı.");
        return;
    }
    if (klasor_mu(tam)) {
        http_hata(r->fd, 409, "Bu adda bir klasör var.");
        return;
    }
    if (var_mi(tam) && !http_sorgu(r, "uzerine", NULL, 0)) {
        http_json(r->fd, 409, "{\"hata\":\"Bu adda bir dosya zaten var.\",\"var\":true}", NULL);
        return;
    }
    if (r->icerik_uzunlugu < 0) {
        http_hata(r->fd, 411, "Dosya boyutu bildirilmedi.");
        return;
    }
    if (r->icerik_uzunlugu > yap.azami_yukleme) {
        char m[160];
        snprintf(m, sizeof m, "Dosya çok büyük. En fazla %lld MB yükleyebilirsin.", yap.azami_yukleme / (1024 * 1024));
        http_hata(r->fd, 413, m);
        return;
    }
    char gecici[YOL_AZAMI + 64], rnd[17];
    rastgele_hex(rnd, 8);
    snprintf(gecici, sizeof gecici, "%s/.yukleniyor-%s", tam_klasor, rnd);
    int fd = open(gecici, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0664);
    if (fd < 0) {
        http_hata(r->fd, 500, "Dosya yazılamadı.");
        return;
    }
    int ok = http_govde_dosyaya(r, fd);
    if (close(fd) != 0) ok = 0;
    if (!ok || rename(gecici, tam) != 0) {
        unlink(gecici);
        http_hata(r->fd, 400, "Yükleme tamamlanamadı. Tekrar dene.");
        return;
    }
    pthread_mutex_lock(&oyun_kilit);
    dosyalar_degisti();
    pthread_mutex_unlock(&oyun_kilit);
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"ok\":true,\"yol\":");
    buf_json_str(&b, goreli);
    buf_putc(&b, '}');
    http_json(r->fd, 200, b.p, NULL);
    buf_free(&b);
}

static void api_klasor(HttpIstek *r)
{
    Json *j = json_govde(r);
    const char *yol = json_metin(j, "yol");
    char tam[YOL_AZAMI];
    if (!yol || !yol[0] || !tam_yol_kur(yap.oyun_klasoru, yol, tam, sizeof tam) || korunan_yol(yol)) {
        json_sil(j);
        http_hata(r->fd, 400, "Klasör adı geçersiz.");
        return;
    }
    json_sil(j);
    if (var_mi(tam)) {
        http_hata(r->fd, 409, "Bu adda bir dosya ya da klasör zaten var.");
        return;
    }
    if (mkdir(tam, 0775) != 0) {
        http_hata(r->fd, errno == ENOENT ? 404 : 500, "Klasör oluşturulamadı.");
        return;
    }
    tamam(r->fd);
}

static void api_sil(HttpIstek *r)
{
    Json *j = json_govde(r);
    const char *yol = json_metin(j, "yol");
    char tam[YOL_AZAMI], goreli[YOL_AZAMI];
    if (!yol || !yol[0] || !tam_yol_kur(yap.oyun_klasoru, yol, tam, sizeof tam)) {
        json_sil(j);
        http_hata(r->fd, 400, "Yol geçersiz.");
        return;
    }
    kopyala(goreli, sizeof goreli, yol);
    json_sil(j);
    if (korunan_yol(goreli)) {
        http_hata(r->fd, 403, "Bu klasör ya da dosya oyunun çalışması için gerekli; silinemez.");
        return;
    }
    if (!var_mi(tam)) {
        http_hata(r->fd, 404, "Bulunamadı.");
        return;
    }
    int ok = ozyinelemeli_sil(tam);
    pthread_mutex_lock(&oyun_kilit);
    dosya_tasindi(goreli, NULL);
    pthread_mutex_unlock(&oyun_kilit);
    if (!ok) {
        http_hata(r->fd, 500, "Bazı dosyalar silinemedi.");
        return;
    }
    tamam(r->fd);
}

static void api_tasi(HttpIstek *r)
{
    Json *j = json_govde(r);
    const char *k = json_metin(j, "kaynak"), *h = json_metin(j, "hedef");
    char kaynak[YOL_AZAMI], hedef[YOL_AZAMI], tk[YOL_AZAMI], th[YOL_AZAMI], ust[YOL_AZAMI], tu[YOL_AZAMI];
    int gecerli = k && h && k[0] && h[0] && strlen(k) < sizeof kaynak && strlen(h) < sizeof hedef;
    if (gecerli) {
        kopyala(kaynak, sizeof kaynak, k);
        kopyala(hedef, sizeof hedef, h);
    }
    json_sil(j);
    if (!gecerli || !tam_yol_kur(yap.oyun_klasoru, kaynak, tk, sizeof tk) ||
        !tam_yol_kur(yap.oyun_klasoru, hedef, th, sizeof th)) {
        http_hata(r->fd, 400, "Yeni ad ya da yol geçersiz.");
        return;
    }
    if (korunan_yol(kaynak) || korunan_yol(hedef)) {
        http_hata(r->fd, 403, "Bu klasör ya da dosya oyunun çalışması için gerekli; taşınamaz.");
        return;
    }
    size_t kn = strlen(kaynak);
    if (!strncmp(hedef, kaynak, kn) && (hedef[kn] == '/' || hedef[kn] == '\0')) {
        http_hata(r->fd, 400, "Bir klasör kendi içine taşınamaz.");
        return;
    }
    kopyala(ust, sizeof ust, hedef);
    char *bolu = strrchr(ust, '/');
    if (bolu) *bolu = '\0';
    else ust[0] = '\0';
    if (!var_mi(tk)) {
        http_hata(r->fd, 404, "Taşınacak dosya bulunamadı.");
        return;
    }
    if (!tam_yol_kur(yap.oyun_klasoru, ust, tu, sizeof tu) || !klasor_mu(tu)) {
        http_hata(r->fd, 404, "Hedef klasör bulunamadı.");
        return;
    }
    if (var_mi(th)) {
        http_hata(r->fd, 409, "Hedefte aynı adda bir dosya ya da klasör var.");
        return;
    }
    if (esya_yolu_cok_uzun(hedef, tk)) {
        http_hata(r->fd, 400, ESYA_YOLU_HATASI);
        return;
    }
    if (rename(tk, th) != 0) {
        http_hata(r->fd, 500, "Taşınamadı.");
        return;
    }
    pthread_mutex_lock(&oyun_kilit);
    dosya_tasindi(kaynak, hedef);
    pthread_mutex_unlock(&oyun_kilit);
    tamam(r->fd);
}

static void api_bilgi(HttpIstek *r)
{
    Buf b;
    buf_init(&b);
    pthread_mutex_lock(&oyun_kilit);
    oyun_bilgisi(&b);
    pthread_mutex_unlock(&oyun_kilit);
    http_json(r->fd, 200, b.p, NULL);
    buf_free(&b);
}

/* Tarayıcı çerezi göremez (HttpOnly); oturumun geçerli olup olmadığını buradan öğrenir. */
static void api_oturum(HttpIstek *r)
{
    char rol[16] = "", token[100];
    http_sorgu(r, "rol", rol, sizeof rol);
    int gm = !strcmp(rol, "gm"), ok = 0;
    if (http_cerez(r, gm ? "zm_gm" : "zm_oyuncu", token, sizeof token)) {
        pthread_mutex_lock(&oyun_kilit);
        ok = oturum_dogrula(token, gm, NULL, NULL);
        pthread_mutex_unlock(&oyun_kilit);
    }
    if (ok) tamam(r->fd);
    else http_hata(r->fd, 401, "Oturum yok.");
}

static const struct {
    const char *yol;
    int post; /* 1: POST, 0: GET */
    int gm;   /* yalnızca GM */
    void (*isle)(HttpIstek *);
} ROTALAR[] = {
    { "/api/bilgi", 0, 0, api_bilgi },
    { "/api/oturum", 0, 0, api_oturum },
    { "/api/giris", 1, 0, api_giris },
    { "/api/gm-giris", 1, 0, api_gm_giris },
    { "/api/cikis", 1, 0, api_cikis },
    { "/api/dosyalar", 0, 1, api_dosyalar }, /* GM dosya yöneticisi */
    { "/api/yukle", 1, 1, api_yukle },
    { "/api/klasor", 1, 1, api_klasor },
    { "/api/sil", 1, 1, api_sil },
    { "/api/tasi", 1, 1, api_tasi },
};

static void api(HttpIstek *r)
{
    if (!strcmp(r->yontem, "POST") && !koken_uygun(r)) {
        http_hata(r->fd, 403, "Bu istek başka bir siteden geldi.");
        return;
    }
    for (size_t i = 0; i < sizeof ROTALAR / sizeof ROTALAR[0]; i++) {
        if (strcmp(r->yol, ROTALAR[i].yol)) continue;
        if (ROTALAR[i].gm && !gm_mi(r)) http_hata(r->fd, 403, "Bunu yalnızca GM yapabilir.");
        else if (strcmp(r->yontem, ROTALAR[i].post ? "POST" : "GET")) http_hata(r->fd, 405, "Bu yöntem desteklenmiyor.");
        else ROTALAR[i].isle(r);
        return;
    }
    http_hata(r->fd, 404, "Bulunamadı.");
}

/* ------------------------------------------------------------------ WebSocket oturumu */

static void ws_oturumu(HttpIstek *r)
{
    char rol[16] = "";
    http_sorgu(r, "rol", rol, sizeof rol);
    int gm = !strcmp(rol, "gm");
    if (!koken_uygun(r)) {
        http_hata(r->fd, 403, "Bu bağlantı başka bir siteden geldi.");
        return;
    }
    char token[100], oyuncu[17] = "", ozet[41] = "";
    int ok = 0;
    if (http_cerez(r, gm ? "zm_gm" : "zm_oyuncu", token, sizeof token)) {
        pthread_mutex_lock(&oyun_kilit);
        ok = oturum_dogrula(token, gm, oyuncu, ozet);
        pthread_mutex_unlock(&oyun_kilit);
    }
    if (!ok) {
        http_hata(r->fd, 401, "Önce giriş yapmalısın.");
        return;
    }
    if (!ws_el_sikis(r->fd, r)) {
        http_hata(r->fd, 400, "WebSocket bağlantısı kurulamadı.");
        return;
    }

    Istemci *c = xcalloc(1, sizeof *c);
    c->fd = r->fd;
    c->gm = gm;
    kopyala(c->oyuncu, sizeof c->oyuncu, oyuncu);
    kopyala(c->oturum, sizeof c->oturum, ozet);
    pthread_mutex_init(&c->m, NULL);
    pthread_cond_init(&c->c, NULL);

    struct timeval tv = { 75, 0 }; /* sunucu 25 saniyede bir ping atar; 75 saniye sessizlik kopukluk demektir */
    setsockopt(r->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);

    pthread_t yazici;
    if (pthread_create(&yazici, NULL, istemci_yazici, c) != 0) {
        pthread_mutex_destroy(&c->m);
        pthread_cond_destroy(&c->c);
        free(c);
        return;
    }
    pthread_mutex_lock(&oyun_kilit);
    istemci_ekle(c);
    pthread_mutex_unlock(&oyun_kilit);

    for (;;) {
        char *veri;
        size_t n;
        int tur = ws_mesaj_oku(r->fd, &veri, &n, JSON_AZAMI);
        int devam = 1;
        if (tur == WS_METIN) {
            pthread_mutex_lock(&oyun_kilit);
            istemci_mesaj(c, veri, n);
            pthread_mutex_unlock(&oyun_kilit);
        } else if (tur == WS_PING) {
            istemciye_gonder(c, WS_PONG, veri, n);
        } else if (tur == WS_PONG) {
            /* canlı */
        } else if (tur == WS_KAPAT) {
            istemciye_gonder(c, WS_KAPAT, veri, n >= 2 ? 2 : 0);
            devam = 0;
        } else {
            devam = 0;
        }
        free(veri);
        pthread_mutex_lock(&c->m);
        if (c->kapat) devam = 0;
        pthread_mutex_unlock(&c->m);
        if (!devam) break;
    }

    pthread_mutex_lock(&oyun_kilit);
    istemci_cikar(c);
    pthread_mutex_unlock(&oyun_kilit);

    /* Kapanış çerçevesi gönderilebilsin diye yazıcıya kısa bir süre tanı */
    struct timespec bekle = { 0, 200 * 1000000 };
    nanosleep(&bekle, NULL);
    pthread_mutex_lock(&c->m);
    c->kapat = 1;
    pthread_cond_signal(&c->c);
    pthread_mutex_unlock(&c->m);
    shutdown(r->fd, SHUT_RDWR);
    pthread_join(yazici, NULL);
    while (c->bas) {
        Paket *p = c->bas;
        c->bas = p->sonraki;
        free(p);
    }
    pthread_mutex_destroy(&c->m);
    pthread_cond_destroy(&c->c);
    free(c);
}

/* ------------------------------------------------------------------ yönlendirme */

static void yonlendir(HttpIstek *r)
{
    const char *y = r->yol;
    if (!strcmp(y, "/ws")) {
        ws_oturumu(r);
        return;
    }
    if (!strncmp(y, "/api/", 5)) {
        api(r);
        return;
    }
    if (strcmp(r->yontem, "GET")) {
        http_hata(r->fd, 405, "Bu yöntem desteklenmiyor.");
        return;
    }
    if (!strncmp(y, "/oyun/", 6)) {
        oyun_dosyasi(r);
        return;
    }
    if (!strcmp(y, "/saglik")) {
        http_yanit(r->fd, 200, "text/plain; charset=utf-8", "Cache-Control: no-store\r\n", "ok", 2);
        return;
    }
    if (!strcmp(y, "/") || !strcmp(y, "/gm") || !strcmp(y, "/gm/")) {
        char tam[YOL_AZAMI];
        snprintf(tam, sizeof tam, "%s/index.html", yap.web_klasoru);
        if (!http_dosya_gonder(r->fd, tam, "text/html; charset=utf-8", SAYFA_BASLIKLARI))
            http_hata(r->fd, 500, "index.html bulunamadı; WEB_KLASORU ayarını kontrol et.");
        return;
    }
    web_dosyasi(r, y + 1);
}

static void *baglanti(void *arg)
{
    int fd = (int)(intptr_t)arg;
    struct timeval tv = { 30, 0 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    HttpIstek *r = xmalloc(sizeof *r);
    int k = http_istek_oku(fd, r);
    if (k == 1) yonlendir(r);
    else if (k == -1) http_hata(fd, 400, "İstek okunamadı.");
    else if (k == -2) http_hata(fd, 431, "İstek başlıkları çok büyük.");
    free(r);
    close(fd);
    atomic_fetch_sub(&baglanti_sayisi, 1);
    return NULL;
}

/* ------------------------------------------------------------------ başlangıç */

static void *kaydedici(void *arg)
{
    (void)arg;
    while (!durdur) {
        sleep(1);
        oyun_kaydet(0);
    }
    return NULL;
}

static void sinyal(int s)
{
    (void)s;
    durdur = 1;
}

static void klasor_ayarla(const char *ad, const char *deger, char *out, int olustur)
{
    if (olustur) mkdir(deger, 0775);
    if (!realpath(deger, out)) {
        fprintf(stderr, "%s klasörü bulunamadı: %s\n", ad, deger);
        exit(1);
    }
    if (strlen(out) > 400) {
        fprintf(stderr, "%s klasörünün yolu çok uzun (en fazla 400 karakter): %s\n", ad, out);
        exit(1);
    }
}

int main(void)
{
    signal(SIGPIPE, SIG_IGN);
    env_dosyasi_oku(".env");

    klasor_ayarla("OYUN_KLASORU", ortam("OYUN_KLASORU", "oyun"), yap.oyun_klasoru, 1);
    klasor_ayarla("WEB_KLASORU", ortam("WEB_KLASORU", "web"), yap.web_klasoru, 0);
    kopyala(yap.alan_adi, sizeof yap.alan_adi, ortam("ALAN_ADI", ortam("DOMAIN", "")));
    kopyala(yap.ilk_sifre, sizeof yap.ilk_sifre, ortam("OYUN_SIFRESI", ""));
    if (yap.ilk_sifre[0] && !strcmp(yap.ilk_sifre, ortam("GM_SIFRESI", ""))) {
        fprintf(stderr, "GM_SIFRESI ile OYUN_SIFRESI aynı olamaz: oyuncular GM ekranına da girebilirdi.\n");
        return 1;
    }
    const char *gm_sifresi = ortam("GM_SIFRESI", "");
    if (strlen(gm_sifresi) >= sizeof yap.gm_sifresi) {
        fprintf(stderr, "GM_SIFRESI en fazla %zu karakter olabilir.\n", sizeof yap.gm_sifresi - 1);
        return 1;
    }
    kopyala(yap.gm_sifresi, sizeof yap.gm_sifresi, gm_sifresi);
    if (strlen(yap.gm_sifresi) < 8) {
        fprintf(stderr, "GM_SIFRESI tanımlı değil ya da çok kısa. .env dosyasına en az 8 karakterlik bir GM şifresi yaz:\n"
                        "    GM_SIFRESI=uzun-ve-tahmin-edilemez-bir-sifre\n"
                        "GM, oyun klasörüne tam erişir; bu şifre oyun şifresinden farklı olmalı.\n");
        return 1;
    }
    long long mb = atoll(ortam("AZAMI_YUKLEME_MB", "64"));
    if (mb < 1) mb = 64;
    if (mb > 4096) mb = 4096;
    yap.azami_yukleme = mb * 1024 * 1024;
    const char *host = ortam("HOST", "127.0.0.1"), *port = ortam("PORT", "3000");
    snprintf(yap.dinle, sizeof yap.dinle, "http://%s:%s", strchr(host, ':') ? "[::1]" : host, port);

    oyun_baslat();
    oyun_kaydet(1);

    struct addrinfo ipucu = { 0 }, *adres = NULL;
    ipucu.ai_family = AF_UNSPEC;
    ipucu.ai_socktype = SOCK_STREAM;
    ipucu.ai_flags = AI_PASSIVE;
    int e = getaddrinfo(host, port, &ipucu, &adres);
    if (e != 0) {
        fprintf(stderr, "Adres çözülemedi (%s:%s): %s\n", host, port, gai_strerror(e));
        return 1;
    }
    int dinleyici = socket(adres->ai_family, adres->ai_socktype | SOCK_CLOEXEC, adres->ai_protocol);
    int bir = 1;
    setsockopt(dinleyici, SOL_SOCKET, SO_REUSEADDR, &bir, sizeof bir);
    int dinliyor = dinleyici >= 0 && bind(dinleyici, adres->ai_addr, adres->ai_addrlen) == 0 && listen(dinleyici, 128) == 0;
    int hata = errno;
    freeaddrinfo(adres);
    if (!dinliyor) {
        fprintf(stderr, "%s:%s dinlenemiyor: %s\n", host, port, strerror(hata));
        return 1;
    }

    struct sigaction sa = { 0 };
    sa.sa_handler = sinyal;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    pthread_t kt;
    pthread_create(&kt, NULL, kaydedici, NULL);
    pthread_detach(kt);

    printf("Zar Masası çalışıyor: %s   (oyun klasörü: %s)\n", yap.dinle, yap.oyun_klasoru);
    gm_bilgisini_yazdir();

    pthread_attr_t oz;
    pthread_attr_init(&oz);
    pthread_attr_setdetachstate(&oz, PTHREAD_CREATE_DETACHED);
    pthread_attr_setstacksize(&oz, 2 * 1024 * 1024); /* kurallar.c için de yeterli yer */

    while (!durdur) {
        struct pollfd p = { dinleyici, POLLIN, 0 };
        int hazir = poll(&p, 1, 1000);
        if (hazir <= 0) continue;
        int fd = accept4(dinleyici, NULL, NULL, SOCK_CLOEXEC);
        if (fd < 0) continue;
        if (atomic_fetch_add(&baglanti_sayisi, 1) >= AZAMI_BAGLANTI) {
            atomic_fetch_sub(&baglanti_sayisi, 1);
            http_hata(fd, 503, "Sunucu şu an çok meşgul.");
            close(fd);
            continue;
        }
        pthread_t t;
        if (pthread_create(&t, &oz, baglanti, (void *)(intptr_t)fd) != 0) {
            atomic_fetch_sub(&baglanti_sayisi, 1);
            close(fd);
        }
    }
    printf("Kapanıyor, oyun kaydediliyor...\n");
    oyun_kaydet(1);
    return 0;
}
