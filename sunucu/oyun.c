#include "oyun.h"
#include "geometri.h"
#include "gorsel.h"
#include "json.h"
#include "kurallar.h"
#include "ws.h"
#include "zar.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

Yapilandirma yap;
pthread_mutex_t oyun_kilit = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dosya_yazma_kilit = PTHREAD_MUTEX_INITIALIZER;

/* kurallar.h: sunucu kuralları çağırmadan hemen önce günceller */
Harita harita;
int tur_sayisi;

#define ID_N 17
#define AD_N 100
#define AZAMI_OYUNCU 64
#define AZAMI_KARAKTER 1024
#define AZAMI_NPC 300 /* harita başına */
#define AZAMI_OTURUM 512
#define AZAMI_KAYIT 200
#define AZAMI_SIRA 128
#define AZAMI_YER_ESYASI 500 /* harita başına yerdeki eşya */
#define AZAMI_KESIF 4000
#define AZAMI_IZIN 512
#define AZAMI_STAT 1000000.0
#define AZAMI_GORUS 1000.0
#define VARSAYILAN_GORUS 6.0f
#define VARSAYILAN_CAN 10.0f
#define ALMA_MESAFESI 0.5    /* yürüyüşü yerdeki eşyaya bu kadar kare yakın biten onu alır */
#define BIRAKMA_MESAFESI 0.55 /* GM bir eşyayı tokenın merkezine bu kadar yakın bırakırsa eşya ona gider */
#define AZAMI_HARITA 1000.0   /* haritanın bir kenarı en fazla bu kadar kare */
#define VARSAYILAN_KARE_PX 70 /* boyutu seçilmemiş haritada bir karenin piksel boyu */
#define KUYRUK_SINIRI (8u * 1024 * 1024)
#define OTURUM_OMRU (365LL * 24 * 3600)
#define DIZI_N(a) ((int)(sizeof(a) / sizeof((a)[0])))

static const char *const OYUNCU_RENK[] = { "#6fa8dc", "#e3798b", "#8cc084", "#b48ee0",
                                           "#5fc3c0", "#e39459", "#d9d2c3", "#f0a3cf" };
static const char *const NPC_RENK[] = { "#a8402f", "#4f5a63", "#7a5b3c", "#2f6b4f", "#6b3f7a", "#8a6a2f" };
static const char *const GM_RENK = "#c9a24a";

/* Oyuncu ya da NPC. Kurallar.c'ye verilen Oyuncu yapısı içindedir; adresi hiç değişmez. */
typedef struct {
    char id[ID_N];
    int npc;
    char ad[AD_N];
    char renk[8];
    int gizli;    /* NPC: oyunculardan gizli */
    int olu;
    int baglanti; /* oyuncu: açık bağlantı sayısı (kaydedilmez) */
    int yerde;    /* oyuncu: açık haritaya yerleştirildi mi (GM yerleştirir; kaydedilmez, konumdan gelir) */
    char *harita; /* NPC: bulunduğu harita (haritalar/ altında göreli yol) */
    char resim[256]; /* tokenın resmi: "tokenlar/..." ya da "" */
    char sapka[256]; /* tokenın şapkası: "sapkalar/..." ya da "" */
    Oyuncu o;
} Karakter;

typedef struct {
    char oyuncu[ID_N];
    float x, y;
} Konum; /* oyuncunun bir haritadaki yeri */

typedef struct {
    char id[ID_N];
    float x, y;
    int olu;
} Bilinen; /* bir oyuncunun başka bir karakteri en son nerede gördüğü */

typedef struct {
    char oyuncu[ID_N];
    Kapsul *kesif; /* keşfedilen alan: görüş dairesinin taradığı kapsüller */
    int kesif_n, kesif_kap;
    long long kesif_surum;
    Bilinen *bilinen;
    int bilinen_n;
} Bilgi; /* bir oyuncunun bir haritadaki hafızası */

typedef struct {
    long long id;
    Esya e;
    float x, y;
} YerEsyasi; /* haritada yerde duran eşya */

typedef struct {
    double genislik, yukseklik; /* GM'in seçtiği boyut, kare cinsinden (0: henüz seçilmedi) */
    double opaklik;             /* ızgara çizgilerinin belirginliği */
    int gorunur, acik;          /* çizgiler görünsün mü, açık renk mi */
    int sis;                    /* görüş sisi açık mı */
    int oturt;                  /* hareket hedefleri kare ortasına otursun */
} Izgara;

typedef struct {
    char *dosya; /* haritalar/ altında göreli yol */
    long long no; /* bu çalıştırmadaki kimliği (kaydedilmez) */
    Izgara iz;
    Konum *konum;
    int konum_n;
    Bilgi *bilgi;
    int bilgi_n;
    YerEsyasi *yer;
    int yer_n;
    long long g_mtime, g_boyut; /* görselin piksel boyutu önbelleği (kaydedilmez) */
    int gen, yuk;
} HaritaKaydi;

typedef struct {
    char ozet[41];
    int gm;
    char oyuncu[ID_N];
    long long son;
} Oturum;

enum { GOR_HERKES = 0, GOR_GM = 1, GOR_GM_SAHIP = 2 };

typedef struct {
    long long id;
    int gorunurluk;
    char *sahipler; /* GOR_GM_SAHIP: GM'den başka görenler, ",id,id," biçiminde */
    char *tam;      /* tam kayıt (JSON nesnesi) */
    char *ozet;     /* diğerlerinin göreceği hali (yoksa NULL: hiç görmezler) */
} Kayit;

typedef struct {
    int aktif;
    char liste[AZAMI_SIRA][ID_N];
    int n;
    int indeks;
    int tur_no;
    float toplam_enerji; /* sıradakinin sıra başındaki enerjisi (ışıklar için) */
} Sira;

enum { ISTEK_HAREKET = 1, ISTEK_SALDIRI = 2, ISTEK_BUYU = 3 };

typedef struct {
    long long id;
    int tur;
    char kim[ID_N];
    float x2, y2;     /* hareket hedefi */
    char hedef[ID_N]; /* saldırı ya da büyü hedefi */
    int esya;         /* saldırıda kullanılacak eşyanın envanterdeki sırası */
    Esya esya_kopya;  /* istek anındaki hali: envanter bu arada değişirse doğru eşyayı bulmak için */
    char buyu[64];    /* büyünün adı (buyuler listesinden) */
} Istek;

typedef struct {
    char oyun_adi[AD_N];
    char sifre[AD_N];
    float tur_enerjisi;
    float gorus; /* herkesin görüş menzili, kare */
    char gm_tuz[33];
    char gm_iz[41]; /* GM şifresinin tuzlu özeti: şifre değişince eski GM oturumları kapanır */
} Ayarlar;

typedef struct {
    char oyuncu[ID_N];
    char resim[256];
    long long bitis;
} ResimIzni; /* saldırı animasyonunda başkasının eşya resmini görebilsin diye kısa süreli izin */

/* Oyuncu'nun konum dışındaki float statları. Durum, kayıt, GM düzenlemesi ve kurallardan sonraki
   onarım bu tek listeyi kullanır. */
typedef struct {
    const char *ad;     /* JSON'daki adı */
    const char *etiket; /* GM'e yazılan uyarılarda */
    size_t yer;
    double lo, hi;
} StatAlani;

#define STAT_ALANI(alan, etiket, lo, hi) { #alan, etiket, offsetof(Oyuncu, alan), lo, hi }
static const StatAlani STATLAR[] = {
    STAT_ALANI(can, "can", -AZAMI_STAT, AZAMI_STAT),
    STAT_ALANI(odak, "odak", -AZAMI_STAT, AZAMI_STAT),
    STAT_ALANI(zirh, "zırh", -AZAMI_STAT, AZAMI_STAT),
    STAT_ALANI(buyu_direnci, "büyü direnci", -AZAMI_STAT, AZAMI_STAT),
    STAT_ALANI(enerji, "enerji", -AZAMI_STAT, AZAMI_STAT),
};

static float *stat_yeri(Oyuncu *o, const StatAlani *a)
{
    return (float *)((char *)o + a->yer);
}

static float stat_oku(const Oyuncu *o, const StatAlani *a)
{
    return *(const float *)((const char *)o + a->yer);
}

static struct {
    Ayarlar ayar;
    Karakter **kar;
    int kar_n;
    HaritaKaydi *harita;
    int harita_n;
    char *aktif; /* açık harita, yoksa NULL */
    int gizli;   /* harita oyunculardan gizli mi */
    Sira sira;
    Istek istek; /* bekleyen tek istek: yalnızca sırası gelen karakter istekte bulunabilir */
    int istek_var;
    long long istek_sayac;
    Oturum oturum[AZAMI_OTURUM];
    int oturum_n;
    Kayit kayit[AZAMI_KAYIT];
    int kayit_n;
    ResimIzni izin[AZAMI_IZIN];
    int izin_n;
    long long sayac;
    long long yer_sayac; /* yerdeki eşyaların kimlikleri (kaydedilmez) */
    int kirli;
} O;

static Istemci *istemciler;
static int kural_icinde;
static Karakter *kural_yapan, *kural_hedef; /* kurallar çalışırken işin tarafları: atılan zarları onlar görür */
static const char *kural_baglam = "";
static long long surum_sayaci;

static void herkese_durum(void);
static void sira_ilerle(void);
static void kayit_metin(const char *tur, int gorunurluk, const char *sahipler, const char *metin);
static void bilgileri_guncelle(void);
static void istekleri_temizle(const char *kim);
static void hedef_istegini_dusur(const char *hedef);
static void siradan_cikar(const char *id);
static void onbellegi_bosalt(void);
static void yumruk_ver(Oyuncu *o);
static void agirlik_hesapla(Oyuncu *o);
static void yerden_al(Karakter *k);

/* ================================================================== yardımcılar */

static int sinirla(long long v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : (int)v;
}

static double sinirla_d(double v, double lo, double hi)
{
    if (!isfinite(v)) return lo;
    return v < lo ? lo : v > hi ? hi : v;
}

static void kirlet(void)
{
    O.kirli = 1;
}

/* JSON sayısı: sonlu, kısa, "-0" olmadan */
static void sayi_yaz(Buf *b, double v)
{
    if (!isfinite(v) || fabs(v) < 5e-7) v = 0;
    buf_printf(b, "%.6g", v);
}

/* ,"ad":sayı */
static void sayi_alani(Buf *b, const char *ad, double v)
{
    buf_printf(b, ",\"%s\":", ad);
    sayi_yaz(b, v);
}

/* Bir kod noktasını çözer; geçerli UTF-8 varsayılır. */
static unsigned kod_noktasi(const unsigned char *p, int *k)
{
    if (p[0] < 0x80) {
        *k = 1;
        return p[0];
    }
    if ((p[0] & 0xE0) == 0xC0) {
        *k = 2;
        return (unsigned)(p[0] & 0x1F) << 6 | (p[1] & 0x3F);
    }
    if ((p[0] & 0xF0) == 0xE0) {
        *k = 3;
        return (unsigned)(p[0] & 0x0F) << 12 | (unsigned)(p[1] & 0x3F) << 6 | (p[2] & 0x3F);
    }
    *k = 4;
    return (unsigned)(p[0] & 0x07) << 18 | (unsigned)(p[1] & 0x3F) << 12 | (unsigned)(p[2] & 0x3F) << 6 |
           (p[3] & 0x3F);
}

static int gorunmez(unsigned cp)
{
    return cp == 0xAD || (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x2028 && cp <= 0x202E) ||
           (cp >= 0x2060 && cp <= 0x206F) || cp == 0xFEFF;
}

/* Boşlukları sadeleştirir, görünmez karakterleri atar, en fazla `azami` karakter bırakır. */
static int metin_temizle(const char *in, char *out, size_t n, size_t azami)
{
    size_t j = 0, kod = 0;
    int bosluk = 0;
    const unsigned char *p = (const unsigned char *)in;
    if (!utf8_gecerli(in, strlen(in))) {
        out[0] = '\0';
        return 0;
    }
    while (*p) {
        int k;
        unsigned cp = kod_noktasi(p, &k);
        if (cp < 0x20 || cp == 0x7f || cp == ' ' || (cp >= 0x80 && cp < 0xA0)) {
            bosluk = 1;
            p += k;
            continue;
        }
        if (gorunmez(cp)) {
            p += k;
            continue;
        }
        if (bosluk && j > 0) {
            if (kod + 1 >= azami || j + 2 >= n) break;
            out[j++] = ' ';
            kod++;
        }
        bosluk = 0;
        if (kod >= azami || j + (size_t)k + 1 >= n) break;
        memcpy(out + j, p, (size_t)k);
        j += (size_t)k;
        kod++;
        p += k;
    }
    while (j > 0 && out[j - 1] == ' ') j--;
    out[j] = '\0';
    return j > 0;
}

/* İsimleri karşılaştırmak için: büyük/küçük harf ve I/ı/İ/i farkı gözetilmez. */
static void ad_anahtari(const char *ad, char *out, size_t n)
{
    size_t j = 0;
    const unsigned char *p = (const unsigned char *)ad;
    while (*p && j + 4 < n) {
        int k;
        unsigned cp = kod_noktasi(p, &k);
        if (cp < 0x80) {
            out[j++] = (char)((cp >= 'A' && cp <= 'Z') ? cp + 32 : cp);
        } else if (cp == 0x130 || cp == 0x131) {
            out[j++] = 'i';
        } else if (cp == 0xC7 || cp == 0xD6 || cp == 0xDC) {
            out[j++] = (char)0xC3; /* Ç Ö Ü → ç ö ü */
            out[j++] = (char)(0x80 | ((cp + 0x20) & 0x3F));
        } else if (cp == 0x11E || cp == 0x15E) {
            unsigned kucuk = cp + 1; /* Ğ → ğ, Ş → ş */
            out[j++] = (char)(0xC0 | (kucuk >> 6));
            out[j++] = (char)(0x80 | (kucuk & 0x3F));
        } else {
            memcpy(out + j, p, (size_t)k);
            j += (size_t)k;
        }
        p += k;
    }
    out[j] = '\0';
}

/* Kurallar dosyasından gelen metinlerdeki bozuk UTF-8 baytlarını '?' yapar. */
static void utf8_onar(char *s)
{
    unsigned char *p = (unsigned char *)s;
    while (*p) {
        size_t k = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : (*p & 0xF8) == 0xF0 ? 4 : 0;
        if (k == 0 || strnlen((const char *)p, k) < k || !utf8_gecerli((const char *)p, k)) {
            *p = '?';
            p++;
            continue;
        }
        if (k == 1 && *p < 0x20) *p = ' ';
        p += k;
    }
}

/* Çok baytlı karakteri bölmeden kopyalar ve bozuk baytları onarır. */
static void kirp_onar(char *dst, size_t n, const char *src)
{
    if (!n) return;
    if (!src) src = "";
    size_t m = strlen(src);
    if (m >= n) {
        m = n - 1;
        while (m > 0 && ((unsigned char)src[m] & 0xC0) == 0x80) m--;
    }
    memcpy(dst, src, m);
    dst[m] = '\0';
    utf8_onar(dst);
}

static int renk_gecerli(const char *r)
{
    if (!r || strlen(r) != 7 || r[0] != '#') return 0;
    for (int i = 1; i < 7; i++)
        if (!strchr("0123456789abcdefABCDEF", r[i])) return 0;
    return 1;
}

static int id_gecerli(const char *s)
{
    if (!s || strlen(s) != 16) return 0;
    for (int i = 0; i < 16; i++)
        if (!strchr("0123456789abcdef", s[i])) return 0;
    return 1;
}

/* ================================================================== karakterler */

static Karakter *karakter_bul(const char *id)
{
    if (!id || !id[0]) return NULL;
    for (int i = 0; i < O.kar_n; i++)
        if (!strcmp(O.kar[i]->id, id)) return O.kar[i];
    return NULL;
}

static Karakter *karakter_oyuncudan(const Oyuncu *o)
{
    for (int i = 0; i < O.kar_n; i++)
        if (&O.kar[i]->o == o) return O.kar[i];
    return NULL;
}

static Karakter *oyuncu_adla(const char *ad)
{
    char a[AD_N * 2], b[AD_N * 2];
    ad_anahtari(ad, a, sizeof a);
    for (int i = 0; i < O.kar_n; i++) {
        if (O.kar[i]->npc) continue;
        ad_anahtari(O.kar[i]->ad, b, sizeof b);
        if (!strcmp(a, b)) return O.kar[i];
    }
    return NULL;
}

static int oyuncu_sayisi(void)
{
    int n = 0;
    for (int i = 0; i < O.kar_n; i++) n += !O.kar[i]->npc;
    return n;
}

/* Açık haritada mı? Oyuncuları GM haritaya yerleştirir (her haritada ayrı bir yerleri olur); NPC'ler tek bir
   haritaya aittir. */
static int haritada(const Karakter *k)
{
    if (!O.aktif) return 0;
    if (!k->npc) return k->yerde;
    return k->harita && !strcmp(k->harita, O.aktif);
}

/* Herkesin görüş menzili (kare): GM Ayarlar'dan belirler. */
static float gorus_menzili(void)
{
    return O.ayar.gorus;
}

/* Şu anki sahnenin parçası mı (oyuncular her zaman) */
static int sahnede(const Karakter *k)
{
    return !k->npc || haritada(k);
}

static int haritadaki_npc_sayisi(void)
{
    int n = 0;
    for (int i = 0; i < O.kar_n; i++) n += O.kar[i]->npc && haritada(O.kar[i]);
    return n;
}

static const char *siradaki_renk(void)
{
    int say[DIZI_N(OYUNCU_RENK)] = { 0 };
    for (int i = 0; i < O.kar_n; i++)
        for (int r = 0; r < DIZI_N(OYUNCU_RENK); r++)
            if (!O.kar[i]->npc && !strcmp(O.kar[i]->renk, OYUNCU_RENK[r])) say[r]++;
    int en = 0;
    for (int r = 1; r < DIZI_N(OYUNCU_RENK); r++)
        if (say[r] < say[en]) en = r;
    return OYUNCU_RENK[en];
}

static Karakter *karakter_olustur(int npc, const char *id, const char *ad, const char *renk)
{
    if (O.kar_n >= AZAMI_KARAKTER) return NULL;
    Karakter *k = xcalloc(1, sizeof *k);
    if (id) kopyala(k->id, sizeof k->id, id);
    else rastgele_hex(k->id, 8);
    k->npc = npc;
    kopyala(k->ad, sizeof k->ad, ad);
    kopyala(k->renk, sizeof k->renk, renk);
    k->o.ad = k->ad;
    k->o.npc = npc;
    k->o.can = VARSAYILAN_CAN;
    k->o.enerji = O.ayar.tur_enerjisi;
    yumruk_ver(&k->o);
    agirlik_hesapla(&k->o);
    O.kar = xrealloc(O.kar, (size_t)(O.kar_n + 1) * sizeof *O.kar);
    O.kar[O.kar_n++] = k;
    kirlet();
    return k;
}

static void bilgileri_sil(const char *oyuncu, const char *hedef);

/* Karakteri her yerden kaldırır ve belleğini bırakır. */
static void karakter_kaldir(Karakter *k)
{
    char id[ID_N];
    kopyala(id, sizeof id, k->id);
    /* Önce listeden çıkar: aşağıda sıra ilerlerse bu karakter artık bulunamaz */
    for (int i = 0; i < O.kar_n; i++) {
        if (O.kar[i] != k) continue;
        memmove(&O.kar[i], &O.kar[i + 1], (size_t)(O.kar_n - i - 1) * sizeof *O.kar);
        O.kar_n--;
        break;
    }
    istekleri_temizle(id);
    hedef_istegini_dusur(id);
    if (!k->npc) {
        for (int i = 0; i < O.harita_n; i++) {
            HaritaKaydi *h = &O.harita[i];
            for (int j = 0; j < h->konum_n; j++) {
                if (strcmp(h->konum[j].oyuncu, id)) continue;
                h->konum[j] = h->konum[--h->konum_n];
                break;
            }
        }
        bilgileri_sil(id, NULL);
    }
    bilgileri_sil(NULL, id);
    free(k->harita);
    free(k);
    kirlet();
    siradan_cikar(id); /* sırası ondaysa sıra ilerler */
}

/* ================================================================== envanter (kurallar.h) */

static void esya_doldur(Esya *e, const char *ad, const char *sinif, const char *resim)
{
    memset(e, 0, sizeof *e);
    kirp_onar(e->ad, sizeof e->ad, ad);
    kirp_onar(e->sinif, sizeof e->sinif, sinif);
    kirp_onar(e->resim, sizeof e->resim, resim);
    e->agirlik = 1;
}

/* kurallar.c'deki yumruk tanımının bir kopyası (dizgileri NUL ile bitmese de güvenle okunur) */
static void yumruk_kopyasi(Esya *e)
{
    char ad[sizeof yumruk.ad + 1], sinif[sizeof yumruk.sinif + 1], resim[sizeof yumruk.resim + 1];
    memcpy(ad, yumruk.ad, sizeof yumruk.ad);
    memcpy(sinif, yumruk.sinif, sizeof yumruk.sinif);
    memcpy(resim, yumruk.resim, sizeof yumruk.resim);
    ad[sizeof yumruk.ad] = sinif[sizeof yumruk.sinif] = resim[sizeof yumruk.resim] = '\0';
    esya_doldur(e, ad[0] ? ad : "Yumruk", sinif, resim);
    e->agirlik = (float)sinirla_d(yumruk.agirlik, 0, AZAMI_STAT);
    e->sabit = 1;
}

static int esya_esit(const Esya *a, const Esya *b)
{
    return !strcmp(a->ad, b->ad) && !strcmp(a->sinif, b->sinif) && !strcmp(a->resim, b->resim);
}

static void envanter_sinirla(Oyuncu *o)
{
    if (o->envanter_adet < 0) o->envanter_adet = 0;
    if (o->envanter_adet > ENVANTER_AZAMI) o->envanter_adet = ENVANTER_AZAMI;
}

/* Envanterin başında tek bir yumruk olsun. Başka bir yerde sabit eşya varsa (kurallar kaydırdıysa) çıkar;
   başta yumruk yoksa araya girer (yer yoksa sondaki eşya düşer). */
static void yumruk_ver(Oyuncu *o)
{
    Esya y;
    yumruk_kopyasi(&y);
    envanter_sinirla(o);
    for (int i = o->envanter_adet - 1; i >= 1; i--) {
        if (!o->envanter[i].sabit) continue;
        memmove(&o->envanter[i], &o->envanter[i + 1], (size_t)(o->envanter_adet - i - 1) * sizeof *o->envanter);
        o->envanter_adet--;
        kirlet();
    }
    if (o->envanter_adet == 0 || !o->envanter[0].sabit) {
        if (o->envanter_adet == ENVANTER_AZAMI) o->envanter_adet--;
        memmove(&o->envanter[1], &o->envanter[0], (size_t)o->envanter_adet * sizeof *o->envanter);
        o->envanter_adet++;
        o->envanter[0] = y;
        kirlet();
    } else if (!esya_esit(&o->envanter[0], &y) || o->envanter[0].agirlik != y.agirlik) {
        o->envanter[0] = y;
        kirlet();
    }
}

/* Taşıdığı yük: eşyaların ağırlıkları toplamı */
static void agirlik_hesapla(Oyuncu *o)
{
    double t = 0;
    for (int i = 0; i < o->envanter_adet && i < ENVANTER_AZAMI; i++) t += o->envanter[i].agirlik;
    o->agirlik = (float)sinirla_d(t, 0, AZAMI_STAT);
}

Esya *envanter_ekle(Oyuncu *o, const char *ad, const char *sinif, const char *resim)
{
    if (!o) return NULL;
    envanter_sinirla(o);
    if (o->envanter_adet >= ENVANTER_AZAMI) return NULL;
    Esya *e = &o->envanter[o->envanter_adet++];
    esya_doldur(e, ad, sinif, resim);
    agirlik_hesapla(o);
    kirlet();
    return e;
}

void envanter_cikar(Oyuncu *o, int sira)
{
    if (!o) return;
    envanter_sinirla(o);
    if (sira < 0 || sira >= o->envanter_adet || o->envanter[sira].sabit) return;
    memmove(&o->envanter[sira], &o->envanter[sira + 1], (size_t)(o->envanter_adet - sira - 1) * sizeof *o->envanter);
    o->envanter_adet--;
    agirlik_hesapla(o);
    kirlet();
}

/* Kurallar dizinin sayısını, metinlerini ya da ağırlıkları bozduysa düzelt; yumruk yerinde kalsın. */
static void envanter_onar(Oyuncu *o)
{
    envanter_sinirla(o);
    for (int i = 0; i < o->envanter_adet; i++) {
        Esya *e = &o->envanter[i];
        e->ad[sizeof e->ad - 1] = '\0';
        e->sinif[sizeof e->sinif - 1] = '\0';
        e->resim[sizeof e->resim - 1] = '\0';
        utf8_onar(e->ad);
        utf8_onar(e->sinif);
        utf8_onar(e->resim);
        e->agirlik = (float)sinirla_d(e->agirlik, 0, AZAMI_STAT);
    }
    yumruk_ver(o);
    agirlik_hesapla(o);
}

/* "silah/Uzun_Kılıç.png" → "Uzun Kılıç" (alt çizgi ve tire boşluk olur) */
static void esya_adi(const char *yol, char *out, size_t n)
{
    const char *s = strrchr(yol, '/');
    s = s ? s + 1 : yol;
    kopyala(out, n, s);
    char *nokta = strrchr(out, '.');
    if (nokta && nokta != out) *nokta = '\0';
    for (char *c = out; *c; c++)
        if (*c == '_' || *c == '-') *c = ' ';
}

/* esyalar/ altındaki ilk klasörün adı sınıftır: "silah/kilic.png" → "silah", "kilic.png" → "" */
static void esya_sinifi(const char *yol, char *out, size_t n)
{
    const char *s = strchr(yol, '/');
    if (!s) {
        out[0] = '\0';
        return;
    }
    size_t k = (size_t)(s - yol);
    char tmp[256];
    if (k >= sizeof tmp) k = sizeof tmp - 1;
    memcpy(tmp, yol, k);
    tmp[k] = '\0';
    kirp_onar(out, n, tmp);
}

/* ================================================================== haritalar ve konumlar */

static HaritaKaydi *harita_bul(const char *dosya, int olustur)
{
    for (int i = 0; i < O.harita_n; i++)
        if (!strcmp(O.harita[i].dosya, dosya)) return &O.harita[i];
    if (!olustur) return NULL;
    O.harita = xrealloc(O.harita, (size_t)(O.harita_n + 1) * sizeof *O.harita);
    HaritaKaydi *h = &O.harita[O.harita_n++];
    memset(h, 0, sizeof *h);
    h->dosya = xstrdup(dosya);
    h->no = ++surum_sayaci;
    h->iz = (Izgara){ 0, 0, 0.35, 1, 0, 1, 0 };
    kirlet();
    return h;
}

/* Açık haritanın kaydı her zaman vardır (harita açılırken oluşturulur). */
static HaritaKaydi *aktif_harita(void)
{
    return O.aktif ? harita_bul(O.aktif, 0) : NULL;
}

/* Açık haritayı değiştirir (NULL: harita yok). */
static void aktif_degistir(const char *dosya)
{
    char *yeni = dosya ? xstrdup(dosya) : NULL; /* dosya O.aktif'in kendisi olabilir */
    free(O.aktif);
    O.aktif = yeni;
    kirlet();
}

static void harita_yolu(const HaritaKaydi *h, char *out, size_t n)
{
    snprintf(out, n, "%s/haritalar/%s", yap.oyun_klasoru, h->dosya);
}

/* Görselin piksel boyutunu h->gen, h->yuk'a okur (dosya değişmediyse önbellekten). Okunamazsa 0. */
static int gorsel_olcu(HaritaKaydi *h)
{
    char yol[YOL_AZAMI];
    harita_yolu(h, yol, sizeof yol);
    struct stat st;
    if (stat(yol, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    if (h->gen == 0 || h->g_mtime != (long long)st.st_mtime || h->g_boyut != (long long)st.st_size) {
        GorselBilgi gb;
        if (!gorsel_bilgi(yol, &gb)) {
            h->gen = h->yuk = 0;
            return 0;
        }
        h->gen = gb.genislik;
        h->yuk = gb.yukseklik;
        h->g_mtime = (long long)st.st_mtime;
        h->g_boyut = (long long)st.st_size;
    }
    return 1;
}

/* Haritanın kare cinsinden boyutu: GM'in seçtiği. Seçilmediyse görselden (70 piksel bir kare). */
static int harita_olcu(HaritaKaydi *h, float *g, float *y)
{
    if (!gorsel_olcu(h)) return 0;
    if (h->iz.genislik < 1 || h->iz.yukseklik < 1) {
        h->iz.genislik = fmax(1, round((double)h->gen / VARSAYILAN_KARE_PX));
        h->iz.yukseklik = fmax(1, round((double)h->yuk / VARSAYILAN_KARE_PX));
        kirlet();
    }
    *g = (float)h->iz.genislik;
    *y = (float)h->iz.yukseklik;
    return 1;
}

static Konum *konum_bul(HaritaKaydi *h, const char *oyuncu)
{
    for (int i = 0; i < h->konum_n; i++)
        if (!strcmp(h->konum[i].oyuncu, oyuncu)) return &h->konum[i];
    return NULL;
}

static void konum_yaz(HaritaKaydi *h, const char *oyuncu, float x, float y)
{
    Konum *k = konum_bul(h, oyuncu);
    if (!k) {
        h->konum = xrealloc(h->konum, (size_t)(h->konum_n + 1) * sizeof *h->konum);
        k = &h->konum[h->konum_n++];
        kopyala(k->oyuncu, sizeof k->oyuncu, oyuncu);
    }
    if (k->x != x || k->y != y) kirlet();
    k->x = x;
    k->y = y;
}

/* Hedefi harita içine alır; "kare ortasına otursun" açıksa en yakın karenin ortasına oturtur. */
static void hedefe_oturt(const HaritaKaydi *h, double *x, double *y, float g, float yk)
{
    *x = sinirla_d(*x, 0, g);
    *y = sinirla_d(*y, 0, yk);
    if (!h->iz.oturt) return;
    int kg = g < 1 ? 1 : (int)g, ky = yk < 1 ? 1 : (int)yk;
    int cx = (int)*x, cy = (int)*y;
    *x = (cx >= kg ? kg - 1 : cx) + 0.5;
    *y = (cy >= ky ? ky - 1 : cy) + 0.5;
}

/* Ortadan başlayıp dışa doğru boş bir kare ortası arar. Yeri belli olan (haritadaki NPC'ler ve bu
   haritada konumu kayıtlı oyuncular) karakterlere 0.75 kareden fazla yaklaşmaz. */
static void bos_yer(HaritaKaydi *h, float g, float y, float *ox, float *oy)
{
    int kg = (int)g, ky = (int)y;
    if (kg < 1) kg = 1;
    if (ky < 1) ky = 1;
    int cx = kg / 2, cy = ky / 2;
    int azami = kg > ky ? kg : ky;
    for (int r = 0; r <= azami; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= kg || ny >= ky) continue;
                double px = nx + 0.5, py = ny + 0.5;
                int dolu = 0;
                for (int i = 0; i < O.kar_n && !dolu; i++) {
                    Karakter *k = O.kar[i];
                    float kx, kyy;
                    if (k->npc) {
                        if (!haritada(k)) continue;
                        kx = k->o.x;
                        kyy = k->o.y;
                    } else {
                        Konum *kn = konum_bul(h, k->id);
                        if (!kn) continue;
                        kx = kn->x;
                        kyy = kn->y;
                    }
                    if (mesafe(px, py, kx, kyy) < 0.75) dolu = 1;
                }
                if (!dolu) {
                    *ox = (float)px;
                    *oy = (float)py;
                    return;
                }
            }
        }
    }
    *ox = cx + 0.5f;
    *oy = cy + 0.5f;
}

static void konum_sil(HaritaKaydi *h, const char *oyuncu)
{
    for (int i = 0; i < h->konum_n; i++) {
        if (strcmp(h->konum[i].oyuncu, oyuncu)) continue;
        h->konum[i] = h->konum[--h->konum_n];
        kirlet();
        return;
    }
}

/* Haritaya yerleşmiş oyuncuların açık haritadaki yerlerini o haritanın kaydına yazar. */
static void konumlari_kaydet(void)
{
    HaritaKaydi *h = aktif_harita();
    if (!h) return;
    for (int i = 0; i < O.kar_n; i++)
        if (!O.kar[i]->npc && O.kar[i]->yerde) konum_yaz(h, O.kar[i]->id, O.kar[i]->o.x, O.kar[i]->o.y);
}

/* Açık haritadaki herkes harita sınırları içinde kalsın. */
static void sinirlara_al(void)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) return;
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!haritada(k)) continue;
        float nx = (float)sinirla_d(k->o.x, 0, g), ny = (float)sinirla_d(k->o.y, 0, y);
        if (nx != k->o.x || ny != k->o.y) {
            k->o.x = nx;
            k->o.y = ny;
            kirlet();
        }
    }
}

/* Açık haritanın kaydından oyuncuların yerlerini okur. Bu haritada yeri olmayan oyuncu haritada değildir;
   GM onu sürükleyip bırakarak yerleştirir. */
static void konumlari_yukle(void)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    int olcu = h && harita_olcu(h, &g, &y);
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (k->npc) continue;
        Konum *kn = olcu ? konum_bul(h, k->id) : NULL;
        k->yerde = kn != NULL;
        if (kn) {
            k->o.x = kn->x;
            k->o.y = kn->y;
        }
    }
    sinirlara_al();
}

static void bilgi_temizle(Bilgi *b)
{
    free(b->kesif);
    free(b->bilinen);
}

/* Haritanın kaydını ve üstündeki NPC'leri siler. */
static void harita_sil(HaritaKaydi *h)
{
    char *dosya = h->dosya;
    for (int i = 0; i < O.kar_n;) {
        Karakter *k = O.kar[i];
        if (k->npc && k->harita && !strcmp(k->harita, dosya)) {
            karakter_kaldir(k);
            continue;
        }
        i++;
    }
    /* karakter_kaldir O.harita dizisini büyütmez; h hâlâ geçerli */
    free(h->konum);
    for (int i = 0; i < h->bilgi_n; i++) bilgi_temizle(&h->bilgi[i]);
    free(h->bilgi);
    free(h->yer);
    int i = (int)(h - O.harita);
    memmove(&O.harita[i], &O.harita[i + 1], (size_t)(O.harita_n - i - 1) * sizeof *O.harita);
    O.harita_n--;
    free(dosya);
    kirlet();
}

/* ================================================================== istemcilere gönderme */

void istemciye_gonder(Istemci *c, int tur, const char *veri, size_t n)
{
    pthread_mutex_lock(&c->m);
    if (c->kapat) {
        pthread_mutex_unlock(&c->m);
        return;
    }
    if (c->bekleyen + n > KUYRUK_SINIRI) {
        /* Çok yavaş istemci: bağlantıyı kapat, tarayıcı yeniden bağlanır */
        c->kapat = 1;
        shutdown(c->fd, SHUT_RDWR);
        pthread_cond_signal(&c->c);
        pthread_mutex_unlock(&c->m);
        return;
    }
    Paket *p = xmalloc(sizeof *p + n + 1);
    p->sonraki = NULL;
    p->tur = tur;
    p->n = n;
    if (n) memcpy(p->veri, veri, n);
    p->veri[n] = '\0';
    if (c->son) c->son->sonraki = p;
    else c->bas = p;
    c->son = p;
    c->bekleyen += n;
    pthread_cond_signal(&c->c);
    pthread_mutex_unlock(&c->m);
}

void *istemci_yazici(void *arg)
{
    Istemci *c = arg;
    for (;;) {
        pthread_mutex_lock(&c->m);
        while (!c->bas && !c->kapat) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 25;
            int r = pthread_cond_timedwait(&c->c, &c->m, &ts);
            if (r == ETIMEDOUT && !c->bas && !c->kapat) {
                /* Boşta: bağlantı canlı kalsın diye ping (yalnızca bu iş parçacığı yazar) */
                pthread_mutex_unlock(&c->m);
                if (!ws_cerceve_yaz(c->fd, WS_PING, "", 0)) goto bitti;
                pthread_mutex_lock(&c->m);
            }
        }
        if (c->kapat) {
            pthread_mutex_unlock(&c->m);
            break;
        }
        Paket *p = c->bas;
        c->bas = p->sonraki;
        if (!c->bas) c->son = NULL;
        c->bekleyen -= p->n;
        pthread_mutex_unlock(&c->m);
        int ok = ws_cerceve_yaz(c->fd, p->tur, p->veri, p->n);
        int kapanis = p->tur == WS_KAPAT;
        free(p);
        if (!ok || kapanis) goto bitti;
    }
    return NULL;
bitti:
    pthread_mutex_lock(&c->m);
    c->kapat = 1;
    pthread_mutex_unlock(&c->m);
    shutdown(c->fd, SHUT_RDWR);
    return NULL;
}

static void gonder_buf(Istemci *c, const Buf *b)
{
    istemciye_gonder(c, WS_METIN, b->p, b->len);
}

static void istemciye_bildirim(Istemci *c, const char *tur, const char *metin)
{
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"t\":\"%s\",\"metin\":", tur);
    buf_json_str(&b, metin);
    buf_putc(&b, '}');
    gonder_buf(c, &b);
    buf_free(&b);
}

static void istemciye_hata(Istemci *c, const char *metin)
{
    istemciye_bildirim(c, "hata", metin);
}

/* Bir oyuncunun açık bütün sekmelerine */
static void oyuncuya_bildir(const Karakter *k, const char *tur, const char *metin)
{
    for (Istemci *c = istemciler; c; c = c->sonraki)
        if (!c->gm && !strcmp(c->oyuncu, k->id)) istemciye_bildirim(c, tur, metin);
}

static void istemciyi_kopar(Istemci *c)
{
    pthread_mutex_lock(&c->m);
    c->kapat = 1;
    pthread_cond_signal(&c->c);
    pthread_mutex_unlock(&c->m);
    shutdown(c->fd, SHUT_RDWR);
}

/* ================================================================== kayıt (olay günlüğü) */

static int sahipler_arasinda(const char *sahipler, const char *oyuncu)
{
    if (!sahipler || !oyuncu[0]) return 0;
    char ara[ID_N + 3];
    snprintf(ara, sizeof ara, ",%s,", oyuncu);
    return strstr(sahipler, ara) != NULL;
}

static const char *kayit_gorunumu(const Kayit *k, const Istemci *c)
{
    if (k->gorunurluk == GOR_HERKES || c->gm) return k->tam;
    if (k->gorunurluk == GOR_GM_SAHIP && sahipler_arasinda(k->sahipler, c->oyuncu)) return k->tam;
    return k->ozet;
}

/* ic: "tur":"...",... biçiminde iç alanlar. ozet_ic NULL olabilir. */
static void kayit_ekle(int gorunurluk, const char *sahipler, const char *ic, const char *ozet_ic)
{
    if (O.kayit_n == AZAMI_KAYIT) {
        free(O.kayit[0].tam);
        free(O.kayit[0].ozet);
        free(O.kayit[0].sahipler);
        memmove(&O.kayit[0], &O.kayit[1], (AZAMI_KAYIT - 1) * sizeof O.kayit[0]);
        O.kayit_n--;
    }
    Kayit *k = &O.kayit[O.kayit_n++];
    memset(k, 0, sizeof *k);
    k->id = ++O.sayac;
    k->gorunurluk = gorunurluk;
    k->sahipler = sahipler && sahipler[0] ? xstrdup(sahipler) : NULL;
    long long z = simdi_ms();
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"id\":%lld,\"zaman\":%lld,%s}", k->id, z, ic);
    k->tam = buf_take(&b);
    if (ozet_ic) {
        buf_printf(&b, "{\"id\":%lld,\"zaman\":%lld,%s}", k->id, z, ozet_ic);
        k->ozet = buf_take(&b);
    }
    kirlet();

    for (Istemci *c = istemciler; c; c = c->sonraki) {
        const char *g = kayit_gorunumu(k, c);
        if (!g) continue;
        buf_puts(&b, "{\"t\":\"kayit\",\"g\":");
        buf_puts(&b, g);
        buf_putc(&b, '}');
        gonder_buf(c, &b);
        b.len = 0;
    }
    buf_free(&b);
}

static void kayit_metin(const char *tur, int gorunurluk, const char *sahipler, const char *metin)
{
    Buf b;
    buf_init(&b);
    buf_printf(&b, "\"tur\":\"%s\",\"metin\":", tur);
    buf_json_str(&b, metin);
    kayit_ekle(gorunurluk, sahipler, b.p, NULL);
    buf_free(&b);
}

static void tek_sahip(char out[ID_N + 3], const Karakter *k)
{
    if (k && !k->npc) snprintf(out, ID_N + 3, ",%s,", k->id);
    else out[0] = '\0';
}

/* Bir karakterin sahibine (oyuncuysa) ve GM'e görünen kayıt */
static void kayit_sahibe(const char *tur, const Karakter *k, const char *metin)
{
    char s[ID_N + 3];
    tek_sahip(s, k);
    kayit_metin(tur, s[0] ? GOR_GM_SAHIP : GOR_GM, s, metin);
}

static void kayitlari_gonder(Istemci *c)
{
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"t\":\"kayitlar\",\"g\":[");
    int ilk = 1;
    for (int i = 0; i < O.kayit_n; i++) {
        const char *g = kayit_gorunumu(&O.kayit[i], c);
        if (!g) continue;
        if (!ilk) buf_putc(&b, ',');
        buf_puts(&b, g);
        ilk = 0;
    }
    buf_puts(&b, "]}");
    gonder_buf(c, &b);
    buf_free(&b);
}

/* ================================================================== görüş ve hafıza */

static int sis_acik(void)
{
    HaritaKaydi *h = aktif_harita();
    return h && h->iz.sis;
}

/* v oyuncusu t karakterini şu an görüyor mu? t açık haritada olmalı; sis açıksa v de haritaya yerleşmiş olmalı. */
static int goruyor(const Karakter *v, const Karakter *t)
{
    if (v == t) return 1;
    if (!O.aktif || O.gizli || t->gizli || !haritada(t)) return 0;
    if (!sis_acik()) return 1;
    if (!haritada(v)) return 0;
    return mesafe(v->o.x, v->o.y, t->o.x, t->o.y) <= gorus_menzili() + 1e-4;
}

static Bilgi *bilgi_bul(HaritaKaydi *h, const char *oyuncu, int olustur)
{
    for (int i = 0; i < h->bilgi_n; i++)
        if (!strcmp(h->bilgi[i].oyuncu, oyuncu)) return &h->bilgi[i];
    if (!olustur) return NULL;
    h->bilgi = xrealloc(h->bilgi, (size_t)(h->bilgi_n + 1) * sizeof *h->bilgi);
    Bilgi *b = &h->bilgi[h->bilgi_n++];
    memset(b, 0, sizeof *b);
    kopyala(b->oyuncu, sizeof b->oyuncu, oyuncu);
    b->kesif_surum = ++surum_sayaci;
    kirlet();
    return b;
}

static void kesif_ekle(Bilgi *b, const Kapsul *k)
{
    if (b->kesif_n >= AZAMI_KESIF) {
        /* Çok nadir: en eski parçayı bırak */
        memmove(&b->kesif[0], &b->kesif[1], (size_t)(b->kesif_n - 1) * sizeof *b->kesif);
        b->kesif_n--;
        b->kesif_surum = ++surum_sayaci;
    }
    if (b->kesif_n == b->kesif_kap) {
        b->kesif_kap = b->kesif_kap ? b->kesif_kap * 2 : 16;
        b->kesif = xrealloc(b->kesif, (size_t)b->kesif_kap * sizeof *b->kesif);
    }
    b->kesif[b->kesif_n++] = *k;
    kirlet();
}

/* Görüş dairesinin (x1,y1)'den (x2,y2)'ye giderken taradığı alanı keşfedilmiş say. */
static void kesfet(Bilgi *b, float x1, float y1, float x2, float y2, float r)
{
    if (!(r > 0)) return;
    Kapsul k = { x1, y1, x2, y2, r };
    for (int i = b->kesif_n - 1; i >= 0; i--)
        if (kapsul_icinde(&k, &b->kesif[i])) return;
    kesif_ekle(b, &k);
}

static Bilinen *bilinen_bul(Bilgi *b, const char *id)
{
    for (int i = 0; i < b->bilinen_n; i++)
        if (!strcmp(b->bilinen[i].id, id)) return &b->bilinen[i];
    return NULL;
}

static void bilinen_yaz(Bilgi *b, const Karakter *t)
{
    Bilinen *e = bilinen_bul(b, t->id);
    if (!e) {
        b->bilinen = xrealloc(b->bilinen, (size_t)(b->bilinen_n + 1) * sizeof *b->bilinen);
        e = &b->bilinen[b->bilinen_n++];
        kopyala(e->id, sizeof e->id, t->id);
        e->x = e->y = -1e9f;
        e->olu = -1;
    }
    if (e->x != t->o.x || e->y != t->o.y || e->olu != t->olu) {
        e->x = t->o.x;
        e->y = t->o.y;
        e->olu = t->olu;
        kirlet();
    }
}

static void bilinen_sil(Bilgi *b, const char *id)
{
    for (int i = 0; i < b->bilinen_n; i++) {
        if (strcmp(b->bilinen[i].id, id)) continue;
        b->bilinen[i] = b->bilinen[--b->bilinen_n];
        kirlet();
        return;
    }
}

/* oyuncu verilirse onun bütün hafızasını; hedef verilirse herkesin o karaktere dair hafızasını siler. */
static void bilgileri_sil(const char *oyuncu, const char *hedef)
{
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
        for (int j = 0; j < h->bilgi_n;) {
            Bilgi *b = &h->bilgi[j];
            if (oyuncu && !strcmp(b->oyuncu, oyuncu)) {
                bilgi_temizle(b);
                h->bilgi[j] = h->bilgi[--h->bilgi_n];
                kirlet();
                continue;
            }
            if (hedef) bilinen_sil(b, hedef);
            j++;
        }
    }
}

/* Her oyuncunun hafızasını şimdiki görüşüyle güncelle:
   - görüş alanındaki karakterlerin yeri ve durumu hafızaya yazılır,
   - görüş alanında olan ama karakterin artık orada olmadığı hatıralar silinir,
   - görüş alanının dışındaki hatıralara dokunulmaz (eski hali kalır). */
static void bilgileri_guncelle(void)
{
    HaritaKaydi *h = aktif_harita();
    if (!h || !h->iz.sis || O.gizli) return; /* harita gizliyken oyuncular hiçbir şey görmez, hafızaları da değişmez */
    float r = gorus_menzili();
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *v = O.kar[i];
        if (v->npc || !v->yerde) continue;
        Bilgi *b = bilgi_bul(h, v->id, 1);
        kesfet(b, v->o.x, v->o.y, v->o.x, v->o.y, r);
        for (int j = 0; j < O.kar_n; j++) {
            Karakter *t = O.kar[j];
            if (t != v && haritada(t) && goruyor(v, t)) bilinen_yaz(b, t);
        }
        for (int j = b->bilinen_n - 1; j >= 0; j--) {
            Bilinen *e = &b->bilinen[j];
            Karakter *t = karakter_bul(e->id);
            if (!t || t == v || !haritada(t)) {
                b->bilinen[j] = b->bilinen[--b->bilinen_n];
                kirlet();
                continue;
            }
            if (goruyor(v, t)) continue;
            if (mesafe(e->x, e->y, v->o.x, v->o.y) <= r) {
                b->bilinen[j] = b->bilinen[--b->bilinen_n];
                kirlet();
            }
        }
    }
}

/* ================================================================== animasyon olayları ve hareket */

static void resim_izni_ver(const Karakter *v, const char *resim)
{
    if (!resim[0] || v->npc) return;
    long long simdi = simdi_sn();
    for (int i = 0; i < O.izin_n;) {
        if (O.izin[i].bitis < simdi) {
            O.izin[i] = O.izin[--O.izin_n];
            continue;
        }
        if (!strcmp(O.izin[i].oyuncu, v->id) && !strcmp(O.izin[i].resim, resim)) {
            O.izin[i].bitis = simdi + 120;
            return;
        }
        i++;
    }
    if (O.izin_n >= AZAMI_IZIN) O.izin[0] = O.izin[--O.izin_n];
    ResimIzni *z = &O.izin[O.izin_n++];
    kopyala(z->oyuncu, sizeof z->oyuncu, v->id);
    kopyala(z->resim, sizeof z->resim, resim);
    z->bitis = simdi + 120;
}

static void url_yaz(Buf *b, const char *onek, const char *goreli)
{
    buf_putc(b, '"');
    buf_puts(b, onek);
    url_yol_kodla(b, goreli);
    buf_putc(b, '"');
}

static void gorunum_alanlari(Buf *b, const Karakter *k);

static void yuru_yaz(Buf *b, const Karakter *t, double x1, double y1, double x2, double y2, int gir, int cik)
{
    buf_puts(b, "{\"t\":\"yuru\",\"id\":");
    buf_json_str(b, t->id);
    sayi_alani(b, "x1", x1);
    sayi_alani(b, "y1", y1);
    sayi_alani(b, "x2", x2);
    sayi_alani(b, "y2", y2);
    buf_printf(b, ",\"gir\":%s,\"cik\":%s,\"ad\":", gir ? "true" : "false", cik ? "true" : "false");
    buf_json_str(b, t->ad);
    buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s,\"olu\":%s", t->renk, t->npc ? "true" : "false",
               t->olu ? "true" : "false");
    gorunum_alanlari(b, t);
    buf_putc(b, '}');
}

/* Karakteri (hx,hy)'ye yürütür: herkese gördüğü kadarını animasyon olarak gönderir, hafızaları ve
   keşfedilen alanı günceller. Durum yayınını çağıran yapar. */
static void hareket_uygula(Karakter *t, double hx, double hy)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !haritada(t) || !harita_olcu(h, &g, &y)) return;
    hx = sinirla_d(hx, 0, g);
    hy = sinirla_d(hy, 0, y);
    double ax = t->o.x, ay = t->o.y;
    if (fabs(hx - ax) < 1e-5 && fabs(hy - ay) < 1e-5) return;
    int sis = h->iz.sis && !O.gizli;

    Buf b;
    buf_init(&b);
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        b.len = 0;
        if (c->gm) {
            yuru_yaz(&b, t, ax, ay, hx, hy, 0, 0);
        } else {
            Karakter *v = karakter_bul(c->oyuncu);
            if (!v || O.gizli) continue;
            if (v == t || (!sis && !t->gizli)) {
                yuru_yaz(&b, t, ax, ay, hx, hy, 0, 0);
            } else {
                double t0, t1;
                if (t->gizli || !v->yerde ||
                    !parca_daire_kesisimi(ax, ay, hx, hy, v->o.x, v->o.y, gorus_menzili(), &t0, &t1))
                    continue;
                yuru_yaz(&b, t, ax + (hx - ax) * t0, ay + (hy - ay) * t0, ax + (hx - ax) * t1, ay + (hy - ay) * t1,
                         t0 > 1e-6, t1 < 1 - 1e-6);
            }
        }
        gonder_buf(c, &b);
    }
    buf_free(&b);

    if (sis) {
        /* Görüş alanından çıkışını görenler artık onun nerede olduğunu bilmez */
        for (int i = 0; i < O.kar_n; i++) {
            Karakter *v = O.kar[i];
            double t0, t1;
            if (v->npc || !v->yerde || v == t || t->gizli) continue;
            if (!parca_daire_kesisimi(ax, ay, hx, hy, v->o.x, v->o.y, gorus_menzili(), &t0, &t1)) continue;
            if (t1 < 1 - 1e-6) bilinen_sil(bilgi_bul(h, v->id, 1), t->id);
        }
    }
    t->o.x = (float)hx;
    t->o.y = (float)hy;
    kirlet();
    if (sis && !t->npc) {
        /* Yürürken görüş dairesinin taradığı her yer görüldü */
        Bilgi *bl = bilgi_bul(h, t->id, 1);
        float r = gorus_menzili();
        kesfet(bl, (float)ax, (float)ay, (float)hx, (float)hy, r);
        for (int i = 0; i < O.kar_n; i++) {
            Karakter *u = O.kar[i];
            if (u == t || u->gizli || !haritada(u)) continue;
            if (nokta_parca_mesafesi(u->o.x, u->o.y, ax, ay, hx, hy) <= r) bilinen_yaz(bl, u);
        }
        for (int i = bl->bilinen_n - 1; i >= 0; i--) {
            Bilinen *e = &bl->bilinen[i];
            Karakter *u = karakter_bul(e->id);
            if (!u) continue;
            int u_gorundu = !u->gizli && haritada(u) && nokta_parca_mesafesi(u->o.x, u->o.y, ax, ay, hx, hy) <= r;
            if (!u_gorundu && nokta_parca_mesafesi(e->x, e->y, ax, ay, hx, hy) <= r) bilinen_sil(bl, e->id);
        }
    }
    bilgileri_guncelle();
    yerden_al(t);
}

/* Bir olayı kimlerin gördüğü: a ya da t'yi görenler ve a, t'nin kendileri (oyuncuysa). ",id,id," */
static void gorenler(Buf *b, const Karakter *a, const Karakter *t)
{
    buf_putc(b, ',');
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *v = O.kar[i];
        if (v->npc) continue;
        if (v == a || v == t || (a && goruyor(v, a)) || (t && goruyor(v, t))) {
            buf_puts(b, v->id);
            buf_putc(b, ',');
        }
    }
}

/* Saldırı ya da büyü (buyu_adi NULL değilse) animasyonu: a'yı ya da t'yi görenlere */
static void saldiri_yayinla(const Karakter *a, const Karakter *t, const char *resim, int isabet, double hasar,
                            const char *buyu_adi)
{
    Buf b;
    buf_init(&b);
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        int ga = 1, gt = 1;
        Karakter *v = NULL;
        if (!c->gm) {
            v = karakter_bul(c->oyuncu);
            if (!v) continue;
            ga = goruyor(v, a);
            gt = goruyor(v, t);
            if (!ga && !gt) continue;
        }
        b.len = 0;
        buf_puts(&b, "{\"t\":\"saldiri\",\"saldiran\":");
        buf_json_str(&b, a->id);
        buf_puts(&b, ",\"hedef\":");
        buf_json_str(&b, t->id);
        buf_printf(&b, ",\"isabet\":%s", isabet ? "true" : "false");
        if (buyu_adi) {
            buf_puts(&b, ",\"buyu\":");
            buf_json_str(&b, buyu_adi);
        }
        if (ga) {
            buf_puts(&b, ",\"a\":{\"x\":");
            sayi_yaz(&b, a->o.x);
            sayi_alani(&b, "y", a->o.y);
            buf_putc(&b, '}');
            if (resim[0]) {
                buf_puts(&b, ",\"resim\":");
                url_yaz(&b, "/oyun/", resim);
                if (v) resim_izni_ver(v, resim);
            }
        }
        if (gt) {
            buf_puts(&b, ",\"h\":{\"x\":");
            sayi_yaz(&b, t->o.x);
            sayi_alani(&b, "y", t->o.y);
            buf_putc(&b, '}');
        }
        if (c->gm || v == t) {
            sayi_alani(&b, "hasar", hasar);
        }
        buf_putc(&b, '}');
        gonder_buf(c, &b);
    }
    buf_free(&b);
}

/* Karakter ölür: görenlere animasyon, görenlere ve GM'e kayıt. */
static void oldur(Karakter *k)
{
    if (k->olu) return;
    k->olu = 1;
    kirlet();
    istekleri_temizle(k->id);
    Buf s, b;
    buf_init(&s);
    buf_init(&b);
    gorenler(&s, k, NULL);
    char m[200];
    snprintf(m, sizeof m, "%s öldü.", k->ad);
    kayit_metin("olum", GOR_GM_SAHIP, s.p, m);
    buf_puts(&b, "{\"t\":\"olum\",\"id\":");
    buf_json_str(&b, k->id);
    buf_putc(&b, '}');
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        Karakter *v = c->gm ? NULL : karakter_bul(c->oyuncu);
        if (c->gm || (v && goruyor(v, k))) gonder_buf(c, &b);
    }
    buf_free(&s);
    buf_free(&b);
    bilgileri_guncelle();
}

static void isaret_yayinla(double x, double y, const char *ad, const char *renk, int odak)
{
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"t\":\"isaret\",\"x\":%.1f,\"y\":%.1f,\"odak\":%s,\"renk\":\"%s\",\"ad\":", x, y,
               odak ? "true" : "false", renk);
    buf_json_str(&b, ad);
    buf_putc(&b, '}');
    for (Istemci *c = istemciler; c; c = c->sonraki)
        if (c->gm || !O.gizli) gonder_buf(c, &b);
    buf_free(&b);
}

/* ================================================================== oyun klasörü listesi (önbellekli) */

static struct {
    long long zaman;
    int gecerli;
    DosyaListesi harita, esya, token, sapka;
} onbellek;

static void onbellegi_bosalt(void)
{
    onbellek.zaman = 0;
}

static void onbellegi_tazele(void)
{
    if (onbellek.gecerli && simdi_ms() - onbellek.zaman < 5000) return;
    if (onbellek.gecerli) {
        dosya_listesi_sil(&onbellek.harita);
        dosya_listesi_sil(&onbellek.esya);
        dosya_listesi_sil(&onbellek.token);
        dosya_listesi_sil(&onbellek.sapka);
    }
    char kok[YOL_AZAMI];
    snprintf(kok, sizeof kok, "%s/haritalar", yap.oyun_klasoru);
    dosyalari_bul(kok, 1, &onbellek.harita);
    snprintf(kok, sizeof kok, "%s/esyalar", yap.oyun_klasoru);
    dosyalari_bul(kok, 0, &onbellek.esya);
    snprintf(kok, sizeof kok, "%s/tokenlar", yap.oyun_klasoru);
    dosyalari_bul(kok, 0, &onbellek.token);
    snprintf(kok, sizeof kok, "%s/sapkalar", yap.oyun_klasoru);
    dosyalari_bul(kok, 0, &onbellek.sapka);
    onbellek.gecerli = 1;
    onbellek.zaman = simdi_ms();
}

/* Oyun klasörüne göre yolu verilen dosya var mı */
static int oyun_dosyasi_var(const char *goreli)
{
    if (!goreli[0] || !goreli_yol_gecerli(goreli)) return 0;
    if (!strncmp(goreli, "esyalar/", 8)) {
        onbellegi_tazele();
        return dosya_listesinde(&onbellek.esya, goreli + 8);
    }
    char yol[YOL_AZAMI];
    if (!tam_yol_kur(yap.oyun_klasoru, goreli, yol, sizeof yol)) return 0;
    struct stat st;
    return stat(yol, &st) == 0 && S_ISREG(st.st_mode);
}

/* ================================================================== durum (anlık görüntü) */

static void esya_json(Buf *b, const Esya *e)
{
    buf_puts(b, "{\"ad\":");
    buf_json_str(b, e->ad);
    buf_puts(b, ",\"sinif\":");
    buf_json_str(b, e->sinif);
    buf_puts(b, ",\"resim\":");
    buf_json_str(b, e->resim);
    buf_puts(b, ",\"url\":");
    if (e->resim[0]) url_yaz(b, "/oyun/", e->resim);
    else buf_puts(b, "null");
    buf_printf(b, ",\"var\":%s,\"gorsel\":%s,\"sabit\":%s", oyun_dosyasi_var(e->resim) ? "true" : "false",
               gorsel_uzantili(e->resim) ? "true" : "false", e->sabit ? "true" : "false");
    sayi_alani(b, "agirlik", e->agirlik);
    buf_putc(b, '}');
}

static void envanter_yaz(Buf *b, const Oyuncu *o)
{
    buf_putc(b, '[');
    for (int i = 0; i < o->envanter_adet; i++) {
        if (i) buf_putc(b, ',');
        esya_json(b, &o->envanter[i]);
    }
    buf_putc(b, ']');
}

/* ================================================================== yerdeki eşyalar */

static int yer_esyasi_sira(const HaritaKaydi *h, long long id)
{
    for (int i = 0; h && i < h->yer_n; i++)
        if (h->yer[i].id == id) return i;
    return -1;
}

/* Eşyayı açık haritada (x,y)'ye koyar. Yer yoksa NULL. */
static YerEsyasi *yere_koy(HaritaKaydi *h, const Esya *e, double x, double y)
{
    if (h->yer_n >= AZAMI_YER_ESYASI) return NULL;
    h->yer = xrealloc(h->yer, (size_t)(h->yer_n + 1) * sizeof *h->yer);
    YerEsyasi *ye = &h->yer[h->yer_n++];
    ye->id = ++O.yer_sayac;
    ye->e = *e;
    ye->e.sabit = 0;
    ye->x = (float)x;
    ye->y = (float)y;
    kirlet();
    return ye;
}

static void yerden_kaldir(HaritaKaydi *h, int i)
{
    memmove(&h->yer[i], &h->yer[i + 1], (size_t)(h->yer_n - i - 1) * sizeof *h->yer);
    h->yer_n--;
    kirlet();
}

/* Eşya karakterin envanterine girer (yer varsa). */
static int envantere_koy(Karakter *k, const Esya *e)
{
    envanter_sinirla(&k->o);
    if (k->o.envanter_adet >= ENVANTER_AZAMI) return 0;
    Esya *yeni = &k->o.envanter[k->o.envanter_adet++];
    *yeni = *e;
    yeni->sabit = 0;
    agirlik_hesapla(&k->o);
    kirlet();
    return 1;
}

/* Yürüyüşü (ya da GM'in taşıması, yerleştirmesi) yerdeki eşyaların üstünde biten karakter onları alır. */
static void yerden_al(Karakter *k)
{
    HaritaKaydi *h = aktif_harita();
    if (!h || !haritada(k) || k->olu) return;
    int dolu = 0;
    for (int i = 0; i < h->yer_n;) {
        YerEsyasi *ye = &h->yer[i];
        if (mesafe(ye->x, ye->y, k->o.x, k->o.y) > ALMA_MESAFESI) {
            i++;
            continue;
        }
        if (!envantere_koy(k, &ye->e)) {
            dolu = 1;
            i++;
            continue;
        }
        char m[300];
        snprintf(m, sizeof m, "%s yerden aldı: %s", k->ad, ye->e.ad);
        kayit_sahibe("esya", k, m);
        yerden_kaldir(h, i);
    }
    if (dolu && !k->npc) oyuncuya_bildir(k, "hata", "Envanterin dolu; yerdeki eşyayı alamadın.");
}

/* v oyuncusu yerdeki eşyayı görüyor mu: sis kapalıysa herkes, açıksa görüş menzilindekiler */
static int yer_goruyor(const Karakter *v, const YerEsyasi *ye)
{
    if (!O.aktif || O.gizli) return 0;
    if (!sis_acik()) return 1;
    return haritada(v) && mesafe(v->o.x, v->o.y, ye->x, ye->y) <= gorus_menzili() + 1e-4;
}

/* ,"yerdekiler":[{"id":1,"x":..,"y":..,"esya":{...}}]  (v NULL ise GM: hepsi) */
static void yerdekiler_yaz(Buf *b, HaritaKaydi *h, const Karakter *v)
{
    buf_puts(b, ",\"yerdekiler\":[");
    int ilk = 1;
    for (int i = 0; h && i < h->yer_n; i++) {
        const YerEsyasi *ye = &h->yer[i];
        if (v && !yer_goruyor(v, ye)) continue;
        buf_printf(b, "%s{\"id\":%lld,\"x\":", ilk ? "" : ",", ye->id);
        ilk = 0;
        sayi_yaz(b, ye->x);
        sayi_alani(b, "y", ye->y);
        buf_puts(b, ",\"esya\":");
        esya_json(b, &ye->e);
        buf_putc(b, '}');
    }
    buf_putc(b, ']');
}

/* {"can":10,"odak":0,...,"agirlik":3} (ağırlık sunucunun hesabıdır, GM düzenleyemez) */
static void statlar_yaz(Buf *b, const Oyuncu *o)
{
    for (int i = 0; i < DIZI_N(STATLAR); i++) {
        buf_printf(b, "%c\"%s\":", i ? ',' : '{', STATLAR[i].ad);
        sayi_yaz(b, stat_oku(o, &STATLAR[i]));
    }
    sayi_alani(b, "agirlik", o->agirlik);
    buf_putc(b, '}');
}

/* Ağırlık arttıkça pul büyür: yüksüz 1, 20 ve üstü yükte 1.8 kat */
static double token_olcegi(const Karakter *k)
{
    return 1 + fmin(fmax(k->o.agirlik, 0), 20) * 0.04;
}

/* Tokenın görünümü: ,"resim":url|null,"sapka":url|null,"olcek":1.2 */
static void gorunum_alanlari(Buf *b, const Karakter *k)
{
    buf_puts(b, ",\"resim\":");
    if (k->resim[0]) url_yaz(b, "/oyun/", k->resim);
    else buf_puts(b, "null");
    buf_puts(b, ",\"sapka\":");
    if (k->sapka[0]) url_yaz(b, "/oyun/", k->sapka);
    else buf_puts(b, "null");
    sayi_alani(b, "olcek", token_olcegi(k));
}

/* Token resmi ve şapka olabilecek dosyalar */
static int gorunum_uzantili(const char *ad)
{
    const char *n = strrchr(ad, '.');
    return gorsel_uzantili(ad) || (n && !strcasecmp(n, ".svg"));
}

/* Seçilebilecek token resimleri ve şapkalar: ,"gorunum":{"resimler":[...],"sapkalar":[...]} */
static void gorunum_listesi_yaz(Buf *b)
{
    onbellegi_tazele();
    const DosyaListesi *l[2] = { &onbellek.token, &onbellek.sapka };
    const char *ad[2] = { "resimler", "sapkalar" }, *klasor[2] = { "tokenlar/", "sapkalar/" };
    buf_puts(b, ",\"gorunum\":{");
    for (int k = 0; k < 2; k++) {
        buf_printf(b, "%s\"%s\":[", k ? "," : "", ad[k]);
        int ilk = 1;
        for (int i = 0; i < l[k]->n; i++) {
            const char *yol = l[k]->yol[i];
            if (!gorunum_uzantili(yol) || strlen(yol) + strlen(klasor[k]) > ESYA_YOLU_AZAMI) continue;
            char tam[YOL_AZAMI];
            snprintf(tam, sizeof tam, "%s%s", klasor[k], yol);
            buf_puts(b, ilk ? "{\"dosya\":" : ",{\"dosya\":");
            ilk = 0;
            buf_json_str(b, tam);
            buf_puts(b, ",\"url\":");
            url_yaz(b, "/oyun/", tam);
            buf_putc(b, '}');
        }
        buf_putc(b, ']');
    }
    buf_putc(b, '}');
}

/* Kuralların büyüleri: ,"buyuler":[...] */
static void buyuler_yaz(Buf *b)
{
    buf_puts(b, ",\"buyuler\":[");
    int n = 0;
    for (int i = 0; buyuler[i] && i < 200; i++) {
        const char *s = buyuler[i];
        if (!s[0] || strlen(s) >= 64 || !utf8_gecerli(s, strlen(s))) continue;
        int var = 0;
        for (int j = 0; j < i; j++) var |= !strcmp(buyuler[j], s);
        if (var) continue;
        if (n++) buf_putc(b, ',');
        buf_json_str(b, s);
    }
    buf_putc(b, ']');
}

static Karakter *siradaki(void)
{
    if (!O.sira.aktif || O.sira.indeks < 0 || O.sira.indeks >= O.sira.n) return NULL;
    return karakter_bul(O.sira.liste[O.sira.indeks]);
}

static int sirada_mi(const char *id)
{
    for (int i = 0; i < O.sira.n; i++)
        if (!strcmp(O.sira.liste[i], id)) return 1;
    return 0;
}

/* Izgara ayarları: durumda ve durum.json'da aynı biçim */
static void izgara_json(Buf *b, const Izgara *iz)
{
    buf_puts(b, "{\"genislik\":");
    sayi_yaz(b, iz->genislik);
    sayi_alani(b, "yukseklik", iz->yukseklik);
    buf_printf(b, ",\"opaklik\":%.2f,\"gorunur\":%s,\"acik\":%s,\"sis\":%s,\"oturt\":%s}", iz->opaklik,
               iz->gorunur ? "true" : "false", iz->acik ? "true" : "false", iz->sis ? "true" : "false",
               iz->oturt ? "true" : "false");
}

static void harita_json(Buf *b, HaritaKaydi *h, float g, float y)
{
    buf_puts(b, "{\"dosya\":");
    buf_json_str(b, h->dosya);
    buf_puts(b, ",\"url\":\"/oyun/haritalar/");
    url_yol_kodla(b, h->dosya);
    buf_printf(b, "?v=%lld\",\"gen\":%d,\"yuk\":%d,\"genislik\":", h->g_mtime, h->gen, h->yuk);
    sayi_yaz(b, g);
    sayi_alani(b, "yukseklik", y);
    buf_printf(b, ",\"gizli\":%s,\"izgara\":", O.gizli ? "true" : "false");
    izgara_json(b, &h->iz);
    buf_putc(b, '}');
}

static void oyuncular_yaz(Buf *b)
{
    buf_puts(b, ",\"oyuncular\":[");
    int ilk = 1;
    for (int i = 0; i < O.kar_n; i++) {
        const Karakter *k = O.kar[i];
        if (k->npc) continue;
        if (!ilk) buf_putc(b, ',');
        ilk = 0;
        buf_puts(b, "{\"id\":");
        buf_json_str(b, k->id);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, k->ad);
        buf_printf(b, ",\"renk\":\"%s\",\"cevrimici\":%s}", k->renk, k->baglanti > 0 ? "true" : "false");
    }
    buf_putc(b, ']');
}

/* Bekleyen istek (yoksa null) */
static void istek_json(Buf *b, const Istek *I)
{
    if (!I || !karakter_bul(I->kim)) {
        buf_puts(b, "null");
        return;
    }
    const char *tur = I->tur == ISTEK_HAREKET ? "hareket" : I->tur == ISTEK_BUYU ? "buyu" : "saldiri";
    buf_printf(b, "{\"id\":%lld,\"tur\":\"%s\",\"kim\":", I->id, tur);
    buf_json_str(b, I->kim);
    if (I->tur == ISTEK_HAREKET) {
        sayi_alani(b, "x2", I->x2);
        sayi_alani(b, "y2", I->y2);
    } else {
        Karakter *t = karakter_bul(I->hedef);
        buf_puts(b, ",\"hedef\":");
        buf_json_str(b, I->hedef);
        buf_puts(b, ",\"hedef_ad\":");
        buf_json_str(b, t ? t->ad : "?");
        if (I->tur == ISTEK_BUYU) {
            buf_puts(b, ",\"buyu\":");
            buf_json_str(b, I->buyu);
        } else {
            buf_puts(b, ",\"esya\":");
            esya_json(b, &I->esya_kopya);
        }
    }
    buf_putc(b, '}');
}

static Istek *istek_kimin(const char *kim)
{
    return O.istek_var && !strcmp(O.istek.kim, kim) ? &O.istek : NULL;
}

static void siniflari_yaz(Buf *b)
{
    buf_puts(b, ",\"siniflar\":[");
    int n = 0;
    char gorulen[200][32];
    for (int i = 0; esya_siniflari[i] && n < 200; i++) {
        const char *s = esya_siniflari[i];
        if (!dosya_adi_gecerli(s) || strlen(s) >= 32) continue;
        int var = 0;
        for (int j = 0; j < n; j++) var |= !strcmp(gorulen[j], s);
        if (var) continue;
        kopyala(gorulen[n++], 32, s);
    }
    onbellegi_tazele();
    for (int i = 0; i < onbellek.esya.n && n < 200; i++) {
        char s[32];
        esya_sinifi(onbellek.esya.yol[i], s, sizeof s);
        if (!s[0]) continue;
        int var = 0;
        for (int j = 0; j < n; j++) var |= !strcmp(gorulen[j], s);
        if (!var) kopyala(gorulen[n++], 32, s);
    }
    for (int i = 0; i < n; i++) {
        if (i) buf_putc(b, ',');
        buf_json_str(b, gorulen[i]);
    }
    buf_putc(b, ']');
}

static void durum_yaz_gm(Buf *b)
{
    buf_printf(b, "{\"t\":\"durum\",\"ben\":{\"gm\":true,\"ad\":\"GM\",\"renk\":\"%s\"},\"oyun_adi\":", GM_RENK);
    buf_json_str(b, O.ayar.oyun_adi);
    oyuncular_yaz(b);

    HaritaKaydi *h = aktif_harita();
    float g = 0, y = 0;
    buf_puts(b, ",\"harita\":");
    if (h && harita_olcu(h, &g, &y)) harita_json(b, h, g, y);
    else buf_puts(b, "null");

    buf_puts(b, ",\"karakterler\":[");
    int ilk = 1;
    for (int i = 0; i < O.kar_n; i++) {
        const Karakter *k = O.kar[i];
        if (k->npc && !haritada(k)) continue;
        if (!ilk) buf_putc(b, ',');
        ilk = 0;
        buf_puts(b, "{\"id\":");
        buf_json_str(b, k->id);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, k->ad);
        buf_printf(b,
                   ",\"renk\":\"%s\",\"npc\":%s,\"gizli\":%s,\"olu\":%s,\"cevrimici\":%s,\"sirada\":%s,\"yerde\":%s,\"x\":",
                   k->renk, k->npc ? "true" : "false", k->gizli ? "true" : "false", k->olu ? "true" : "false",
                   k->baglanti > 0 ? "true" : "false", sirada_mi(k->id) ? "true" : "false", haritada(k) ? "true" : "false");
        sayi_yaz(b, k->o.x);
        sayi_alani(b, "y", k->o.y);
        gorunum_alanlari(b, k);
        buf_puts(b, ",\"resim_yolu\":");
        buf_json_str(b, k->resim);
        buf_puts(b, ",\"sapka_yolu\":");
        buf_json_str(b, k->sapka);
        buf_puts(b, ",\"statlar\":");
        statlar_yaz(b, &k->o);
        buf_puts(b, ",\"envanter\":");
        envanter_yaz(b, &k->o);
        buf_putc(b, '}');
    }
    buf_putc(b, ']');

    Karakter *s = siradaki();
    buf_printf(b, ",\"sira\":{\"aktif\":%s,\"tur\":%d,\"indeks\":%d,\"toplam_enerji\":", O.sira.aktif ? "true" : "false",
               O.sira.tur_no, O.sira.indeks);
    sayi_yaz(b, O.sira.toplam_enerji);
    buf_puts(b, ",\"sirada\":");
    if (s) buf_json_str(b, s->id);
    else buf_puts(b, "null");
    buf_puts(b, ",\"liste\":[");
    for (int i = 0; i < O.sira.n; i++) {
        if (i) buf_putc(b, ',');
        buf_json_str(b, O.sira.liste[i]);
    }
    buf_puts(b, "]}");

    buf_puts(b, ",\"istek\":");
    istek_json(b, O.istek_var ? &O.istek : NULL);
    yerdekiler_yaz(b, h, NULL);
    buyuler_yaz(b);
    gorunum_listesi_yaz(b);
    sayi_alani(b, "gorus", gorus_menzili());
    buf_printf(b, ",\"envanter_azami\":%d", ENVANTER_AZAMI);

    onbellegi_tazele();
    buf_puts(b, ",\"haritalar\":[");
    for (int i = 0; i < onbellek.harita.n; i++) {
        const char *yol = onbellek.harita.yol[i];
        HaritaKaydi *hk = harita_bul(yol, 0);
        if (i) buf_putc(b, ',');
        buf_puts(b, "{\"dosya\":");
        buf_json_str(b, yol);
        buf_puts(b, ",\"url\":");
        url_yaz(b, "/oyun/haritalar/", yol);
        if (hk && hk->iz.genislik >= 1) { /* boyutu seçilmiş: GM açarken yeniden sorulmaz */
            sayi_alani(b, "genislik", hk->iz.genislik);
            sayi_alani(b, "yukseklik", hk->iz.yukseklik);
        }
        buf_putc(b, '}');
    }
    buf_puts(b, "],\"esyalar\":[");
    for (int i = 0; i < onbellek.esya.n; i++) {
        const char *yol = onbellek.esya.yol[i];
        char ad[64], sinif[32];
        esya_adi(yol, ad, sizeof ad);
        utf8_onar(ad);
        esya_sinifi(yol, sinif, sizeof sinif);
        if (i) buf_putc(b, ',');
        buf_puts(b, "{\"dosya\":");
        buf_json_str(b, yol);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, ad);
        buf_puts(b, ",\"sinif\":");
        buf_json_str(b, sinif);
        buf_printf(b, ",\"gorsel\":%s,\"url\":", gorsel_uzantili(yol) ? "true" : "false");
        url_yaz(b, "/oyun/esyalar/", yol);
        buf_putc(b, '}');
    }
    buf_putc(b, ']');
    siniflari_yaz(b);

    buf_puts(b, ",\"ayarlar\":{\"oyun_adi\":");
    buf_json_str(b, O.ayar.oyun_adi);
    buf_puts(b, ",\"sifre\":");
    buf_json_str(b, O.ayar.sifre);
    sayi_alani(b, "tur_enerjisi", O.ayar.tur_enerjisi);
    sayi_alani(b, "gorus", O.ayar.gorus);
    buf_puts(b, "}}");
}

static void token_yaz(Buf *b, const Karakter *t, double x, double y, int olu, int canli, int ben)
{
    buf_puts(b, "{\"id\":");
    buf_json_str(b, t->id);
    buf_puts(b, ",\"ad\":");
    buf_json_str(b, t->ad);
    buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s,\"olu\":%s,\"canli\":%s,\"ben\":%s,\"cevrimici\":%s,\"x\":", t->renk,
               t->npc ? "true" : "false", olu ? "true" : "false", canli ? "true" : "false", ben ? "true" : "false",
               !t->npc && t->baglanti > 0 ? "true" : "false");
    sayi_yaz(b, x);
    sayi_alani(b, "y", y);
    gorunum_alanlari(b, t);
    buf_putc(b, '}');
}

/* v oyuncusu t'yi tanıyor mu (adını görebilir mi)? Oyuncular herkesi tanır; NPC'ler görülünce tanınır. */
static int taniyor(const Karakter *v, const Karakter *t, Bilgi *bl)
{
    if (t == v || !t->npc) return 1;
    if (!haritada(t) || t->gizli) return 0;
    if (goruyor(v, t)) return 1;
    if (!sis_acik()) return 0;
    return bl && bilinen_bul(bl, t->id);
}

/* v'nin bildiği ölüm durumu */
static int bilinen_olum(const Karakter *v, const Karakter *t, Bilgi *bl)
{
    if (t == v || goruyor(v, t)) return t->olu;
    Bilinen *e = bl ? bilinen_bul(bl, t->id) : NULL;
    return e ? e->olu > 0 : 0;
}

/* [x1,y1,x2,y2,r]; virgul: önüne virgül konsun mu */
static void kapsul_yaz(Buf *b, const Kapsul *k, int virgul)
{
    const float v[5] = { k->x1, k->y1, k->x2, k->y2, k->r };
    buf_puts(b, virgul ? ",[" : "[");
    for (int i = 0; i < 5; i++) {
        if (i) buf_putc(b, ',');
        sayi_yaz(b, v[i]);
    }
    buf_putc(b, ']');
}

static void kesif_yaz(Buf *b, Istemci *c, HaritaKaydi *h, Bilgi *bl)
{
    int bas = 0, sifirla = 0;
    if (!bl) {
        buf_puts(b, ",\"kesif\":{\"sifirla\":true,\"liste\":[]}");
        c->kesif_harita = h->no;
        c->kesif_surum = 0;
        c->kesif_n = 0;
        return;
    }
    if (c->kesif_harita != h->no || c->kesif_surum != bl->kesif_surum || c->kesif_n > bl->kesif_n) sifirla = 1;
    else bas = c->kesif_n;
    buf_printf(b, ",\"kesif\":{\"sifirla\":%s,\"liste\":[", sifirla ? "true" : "false");
    for (int i = bas; i < bl->kesif_n; i++) kapsul_yaz(b, &bl->kesif[i], i > bas);
    buf_puts(b, "]}");
    c->kesif_harita = h->no;
    c->kesif_surum = bl->kesif_surum;
    c->kesif_n = bl->kesif_n;
}

static void durum_yaz_oyuncu(Buf *b, Istemci *c, Karakter *ben)
{
    buf_puts(b, "{\"t\":\"durum\",\"ben\":{\"gm\":false,\"id\":");
    buf_json_str(b, ben->id);
    buf_puts(b, ",\"ad\":");
    buf_json_str(b, ben->ad);
    buf_printf(b, ",\"renk\":\"%s\",\"yerde\":%s", ben->renk, haritada(ben) ? "true" : "false");
    gorunum_alanlari(b, ben);
    buf_puts(b, ",\"resim_yolu\":");
    buf_json_str(b, ben->resim);
    buf_puts(b, ",\"sapka_yolu\":");
    buf_json_str(b, ben->sapka);
    buf_puts(b, "},\"oyun_adi\":");
    buf_json_str(b, O.ayar.oyun_adi);
    oyuncular_yaz(b);

    HaritaKaydi *h = aktif_harita();
    float g = 0, y = 0;
    Bilgi *bl = NULL;
    buf_puts(b, ",\"harita\":");
    if (!h || !harita_olcu(h, &g, &y)) {
        buf_puts(b, "null,\"tokenler\":[],\"yerdekiler\":[]");
    } else if (O.gizli) {
        buf_puts(b, "{\"gizli\":true},\"tokenler\":[],\"yerdekiler\":[]");
    } else {
        harita_json(b, h, g, y);
        if (h->iz.sis) bl = bilgi_bul(h, ben->id, 0);
        buf_puts(b, ",\"tokenler\":[");
        int ilk = 1;
        for (int i = 0; i < O.kar_n; i++) {
            const Karakter *t = O.kar[i];
            if (!haritada(t)) continue;
            if (t == ben || goruyor(ben, t)) {
                if (!ilk) buf_putc(b, ',');
                ilk = 0;
                token_yaz(b, t, t->o.x, t->o.y, t->olu, 1, t == ben);
            } else if (bl) {
                Bilinen *e = bilinen_bul(bl, t->id);
                if (!e) continue;
                if (!ilk) buf_putc(b, ',');
                ilk = 0;
                token_yaz(b, t, e->x, e->y, e->olu > 0, 0, 0);
            }
        }
        buf_putc(b, ']');
        yerdekiler_yaz(b, h, ben);
        if (h->iz.sis) kesif_yaz(b, c, h, bl);
    }
    sayi_alani(b, "gorus", gorus_menzili());
    buf_printf(b, ",\"envanter_azami\":%d", ENVANTER_AZAMI);
    buyuler_yaz(b);
    gorunum_listesi_yaz(b);

    buf_puts(b, ",\"statlar\":");
    statlar_yaz(b, &ben->o);
    buf_printf(b, ",\"olu\":%s,\"envanter\":", ben->olu ? "true" : "false");
    envanter_yaz(b, &ben->o);

    Karakter *s = siradaki();
    buf_printf(b, ",\"sira\":{\"aktif\":%s,\"tur\":%d,\"sirada_ben\":%s,\"toplam_enerji\":", O.sira.aktif ? "true" : "false",
               O.sira.tur_no, s == ben ? "true" : "false");
    sayi_yaz(b, s == ben ? O.sira.toplam_enerji : 0);
    buf_puts(b, ",\"sirada\":");
    if (s && taniyor(ben, s, bl)) {
        buf_puts(b, "{\"id\":");
        buf_json_str(b, s->id);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, s->ad);
        buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s}", s->renk, s->npc ? "true" : "false");
    } else if (s) {
        buf_puts(b, "{\"ad\":null,\"renk\":null,\"npc\":true}");
    } else {
        buf_puts(b, "null");
    }
    buf_puts(b, ",\"liste\":[");
    int ilk = 1;
    for (int i = 0; i < O.sira.n; i++) {
        Karakter *t = karakter_bul(O.sira.liste[i]);
        if (!t || !sahnede(t)) continue;
        if (!ilk) buf_putc(b, ',');
        ilk = 0;
        int tanir = taniyor(ben, t, bl);
        buf_puts(b, "{\"ad\":");
        if (tanir) buf_json_str(b, t->ad);
        else buf_puts(b, "null");
        buf_puts(b, ",\"renk\":");
        if (tanir) buf_printf(b, "\"%s\"", t->renk);
        else buf_puts(b, "null");
        buf_printf(b, ",\"npc\":%s,\"olu\":%s,\"cevrimici\":%s,\"ben\":%s,\"simdiki\":%s}", t->npc ? "true" : "false",
                   bilinen_olum(ben, t, bl) ? "true" : "false", t->npc || t->baglanti > 0 ? "true" : "false",
                   t == ben ? "true" : "false", O.sira.aktif && i == O.sira.indeks ? "true" : "false");
    }
    buf_puts(b, "]}");

    buf_puts(b, ",\"istek\":");
    istek_json(b, istek_kimin(ben->id));
    buf_putc(b, '}');
}

static int durum_yaz(Buf *b, Istemci *c)
{
    if (c->gm) {
        durum_yaz_gm(b);
        return 1;
    }
    Karakter *ben = karakter_bul(c->oyuncu);
    if (!ben || ben->npc) return 0;
    durum_yaz_oyuncu(b, c, ben);
    return 1;
}

static void herkese_durum(void)
{
    Buf gm, b;
    buf_init(&gm);
    buf_init(&b);
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        if (c->gm) {
            if (!gm.len) durum_yaz(&gm, c);
            gonder_buf(c, &gm);
        } else {
            b.len = 0;
            if (durum_yaz(&b, c)) gonder_buf(c, &b);
        }
    }
    buf_free(&gm);
    buf_free(&b);
}

static void durum_gonder(Istemci *c)
{
    Buf b;
    buf_init(&b);
    if (durum_yaz(&b, c)) gonder_buf(c, &b);
    buf_free(&b);
}

/* ================================================================== kurallar.c için arayüz */

/* Kurallarda atılan zar kayda yazılır: GM ve işin tarafları (yapan ve hedef, oyuncuysalar) görür. */
static void kural_zari_yaz(const ZarSonuc *s)
{
    Karakter *a = kural_yapan, *t = kural_hedef;
    char sahip[2 * ID_N + 4] = "";
    if ((a && !a->npc) || (t && !t->npc)) {
        strcat(sahip, ",");
        if (a && !a->npc) {
            strcat(sahip, a->id);
            strcat(sahip, ",");
        }
        if (t && t != a && !t->npc) {
            strcat(sahip, t->id);
            strcat(sahip, ",");
        }
    }
    Buf ic;
    buf_init(&ic);
    buf_puts(&ic, "\"tur\":\"zar\",\"kim\":");
    buf_json_str(&ic, a ? a->ad : "Kurallar");
    buf_printf(&ic, ",\"renk\":\"%s\",\"gm\":false,\"kural\":true,\"baglam\":", a ? a->renk : GM_RENK);
    buf_json_str(&ic, kural_baglam);
    buf_puts(&ic, ",\"sonuc\":");
    zar_json_yaz(&ic, s);
    kayit_ekle(sahip[0] ? GOR_GM_SAHIP : GOR_GM, sahip, ic.p, NULL);
    buf_free(&ic);
}

int zar_at(int adet, int yuz)
{
    if (adet < 1 || adet > ZAR_AZAMI || yuz < 2 || yuz > ZAR_AZAMI_YUZ) return 0;
    if (kural_icinde) {
        char ifade[32], hata[200];
        ZarSonuc s;
        snprintf(ifade, sizeof ifade, "%dd%d", adet, yuz);
        if (zar_ifadesi_at(ifade, &s, hata, sizeof hata)) {
            kural_zari_yaz(&s);
            return (int)s.toplam;
        }
    }
    int t = 0;
    for (int i = 0; i < adet; i++) t += (int)rastgele_aralik((uint32_t)yuz) + 1;
    return t;
}

int zar(const char *ifade)
{
    ZarSonuc s;
    char hata[200];
    if (!zar_ifadesi_at(ifade, &s, hata, sizeof hata)) {
        gm_bildir("kurallar.c: \"%s\" zar ifadesi hatalı. %s", ifade ? ifade : "(boş)", hata);
        return 0;
    }
    if (kural_icinde) kural_zari_yaz(&s);
    return (int)s.toplam;
}

static void kural_mesaji(int gorunurluk, const char *sahipler, const char *tur, const char *bicim, va_list ap)
{
    char m[600];
    vsnprintf(m, sizeof m, bicim, ap);
    utf8_onar(m);
    if (!kural_icinde) {
        fprintf(stderr, "[kurallar] duyur, gm_bildir ve bildir yalnızca her_tur(), hareket(), saldiri() ve buyu() içinde çalışır: %s\n", m);
        return;
    }
    kayit_metin(tur, gorunurluk, sahipler, m);
}

void duyur(const char *bicim, ...)
{
    va_list ap;
    va_start(ap, bicim);
    kural_mesaji(GOR_HERKES, NULL, "kural", bicim, ap);
    va_end(ap);
}

void gm_bildir(const char *bicim, ...)
{
    va_list ap;
    va_start(ap, bicim);
    kural_mesaji(GOR_GM, NULL, "gm", bicim, ap);
    va_end(ap);
}

void bildir(const Oyuncu *o, const char *bicim, ...)
{
    Karakter *k = o ? karakter_oyuncudan(o) : NULL;
    char s[ID_N + 3];
    tek_sahip(s, k);
    va_list ap;
    va_start(ap, bicim);
    kural_mesaji(s[0] ? GOR_GM_SAHIP : GOR_GM, s, s[0] ? "ozel" : "gm", bicim, ap);
    va_end(ap);
}

/* Kurallar çağrılmadan hemen önce: genel bilgiler güncellenir, duyur/bildir çalışır hale gelir, atılan zarlar
   yapan ve hedefe gösterilir. Çağrıdan sonra kurallardan_cik(). */
static void kurallara_gir(Karakter *yapan, Karakter *hedef, const char *baglam)
{
    kural_yapan = yapan;
    kural_hedef = hedef;
    kural_baglam = baglam;
    HaritaKaydi *h = aktif_harita();
    float g = 0, y = 0;
    if (h && harita_olcu(h, &g, &y)) {
        harita.genislik = g;
        harita.yukseklik = y;
    } else {
        harita.genislik = harita.yukseklik = 0;
    }
    tur_sayisi = O.sira.tur_no;
    kural_icinde = 1;
}

static void kurallardan_cik(void)
{
    kural_icinde = 0;
    kural_yapan = kural_hedef = NULL;
    kural_baglam = "";
}

static void alan_onar(float *v, float once, double lo, double hi, const Karakter *k, const char *alan,
                      const char *baglam)
{
    if (!isfinite(*v)) {
        char m[300];
        snprintf(m, sizeof m, "kurallar.c (%s): %s için %s geçersiz bir sayı oldu (NaN ya da sonsuz); eski değerine döndü.",
                 baglam, k->ad, alan);
        kayit_metin("gm", GOR_GM, NULL, m);
        *v = once;
    }
    if (*v < lo) *v = (float)lo;
    if (*v > hi) *v = (float)hi;
}

/* Kurallar çağrısından sonra: değerler mantıklı kalsın (once: çağrıdan önceki hali) */
static void karakter_onar(Karakter *k, const Oyuncu *once, const char *baglam)
{
    k->o.ad = k->ad;
    k->o.npc = k->npc;
    alan_onar(&k->o.x, once->x, -AZAMI_STAT, AZAMI_STAT, k, "x", baglam);
    alan_onar(&k->o.y, once->y, -AZAMI_STAT, AZAMI_STAT, k, "y", baglam);
    for (int i = 0; i < DIZI_N(STATLAR); i++) {
        const StatAlani *a = &STATLAR[i];
        alan_onar(stat_yeri(&k->o, a), stat_oku(once, a), a->lo, a->hi, k, a->etiket, baglam);
    }
    envanter_onar(&k->o);
    kirlet();
}

/* Kurallar karakterin yerini değiştirdiyse onu oraya yürüt (animasyonla). */
static void konum_degisimi(Karakter *k, const Oyuncu *s)
{
    float nx = k->o.x, ny = k->o.y;
    if (nx == s->x && ny == s->y) return;
    k->o.x = s->x;
    k->o.y = s->y;
    if (haritada(k)) hareket_uygula(k, nx, ny);
    else {
        k->o.x = nx;
        k->o.y = ny;
    }
}

static void kural_her_tur(Karakter *k)
{
    Oyuncu once = k->o;
    kurallara_gir(k, NULL, "sıra başı");
    her_tur(&k->o);
    kurallardan_cik();
    karakter_onar(k, &once, "her_tur");
    konum_degisimi(k, &once);
    bilgileri_guncelle();
}

/* GM'in onayladığı yürüyüş: kurallar.c'deki hareket() enerjiyi ayarlar ve izin verir ya da vermez.
   İzin verirse karakter (x,y)'ye, kurallar konumunu değiştirdiyse oraya yürür. Yürüdüyse 1 döner. */
static int kural_hareket(Karakter *k, double x, double y)
{
    Oyuncu once = k->o;
    kurallara_gir(k, NULL, "hareket");
    int izin = hareket(&k->o, (float)x, (float)y);
    kurallardan_cik();
    karakter_onar(k, &once, "hareket");
    if (k->o.x != once.x || k->o.y != once.y) {
        x = k->o.x; /* kurallar hedefi değiştirdi (ör. yolu kısalttı) */
        y = k->o.y;
    }
    k->o.x = once.x;
    k->o.y = once.y;
    if (izin) hareket_uygula(k, x, y);
    bilgileri_guncelle();
    return izin != 0;
}

static void olumleri_denetle(void);

/* Kurallar saldırı ya da büyüde iki karakteri değiştirdikten sonra: önce eylem eski yerlerde oynasın, sonra
   kuralların yaptığı itme vb. yürüsün; canı 0 ya da altına inen hemen ölür. */
static void eylemi_oynat(Karakter *a, Karakter *t, const Oyuncu *oa, const Oyuncu *ot, const char *resim, const char *buyu_adi)
{
    float ax = a->o.x, ay = a->o.y, tx = t->o.x, ty = t->o.y;
    a->o.x = oa->x;
    a->o.y = oa->y;
    t->o.x = ot->x;
    t->o.y = ot->y;
    double hasar = (double)ot->can - t->o.can;
    saldiri_yayinla(a, t, resim, buyu_adi ? fabs(hasar) > 1e-6 : hasar > 1e-6, hasar, buyu_adi);
    a->o.x = ax;
    a->o.y = ay;
    konum_degisimi(a, oa);
    if (t != a) {
        t->o.x = tx;
        t->o.y = ty;
        konum_degisimi(t, ot);
    }
    bilgileri_guncelle();
    olumleri_denetle();
}

/* Saldırıyı uygular: kayıt, kurallar.c'deki saldiri(), animasyon, ölüm. */
static void saldiri_uygula(Karakter *a, Karakter *t, int idx)
{
    Esya kopya = a->o.envanter[idx];
    Buf s;
    buf_init(&s);
    gorenler(&s, a, t);
    char m[400];
    snprintf(m, sizeof m, "Saldırı: %s → %s (%s)", a->ad, t->ad, kopya.ad[0] ? kopya.ad : "eşya");
    kayit_metin("saldiri", GOR_GM_SAHIP, s.p, m);
    buf_free(&s);

    Oyuncu oa = a->o, ot = t->o;
    kurallara_gir(a, t, "saldırı");
    saldiri(&a->o, &t->o, &a->o.envanter[idx]);
    kurallardan_cik();
    karakter_onar(a, &oa, "saldiri");
    karakter_onar(t, &ot, "saldiri");
    eylemi_oynat(a, t, &oa, &ot, kopya.resim, NULL);
}

/* buyuler listesinde mi */
static int buyu_gecerli(const char *ad)
{
    if (!ad || !ad[0]) return 0;
    for (int i = 0; buyuler[i] && i < 200; i++)
        if (!strcmp(buyuler[i], ad)) return 1;
    return 0;
}

/* Büyüyü uygular: kayıt, kurallar.c'deki buyu(), animasyon, ölüm. Hedef yapanın kendisi olabilir. */
static void buyu_uygula(Karakter *a, Karakter *t, const char *buyu_adi)
{
    char ad[64];
    kirp_onar(ad, sizeof ad, buyu_adi);
    Buf s;
    buf_init(&s);
    gorenler(&s, a, t);
    char m[400];
    if (a == t) snprintf(m, sizeof m, "Büyü: %s → kendine (%s)", a->ad, ad);
    else snprintf(m, sizeof m, "Büyü: %s → %s (%s)", a->ad, t->ad, ad);
    kayit_metin("buyu", GOR_GM_SAHIP, s.p, m);
    buf_free(&s);

    Oyuncu oa = a->o, ot = t->o;
    char arg[64];
    kopyala(arg, sizeof arg, ad); /* kurallar değiştirse de bizim kopyamız kalsın */
    kurallara_gir(a, t, "büyü");
    buyu(&a->o, &t->o, arg);
    kurallardan_cik();
    karakter_onar(a, &oa, "buyu");
    if (t != a) karakter_onar(t, &ot, "buyu");
    eylemi_oynat(a, t, &oa, &ot, "", ad);
}

/* ================================================================== tur sırası ve ölüm */

static void siraya_ekle(const char *id)
{
    if (sirada_mi(id) || O.sira.n >= AZAMI_SIRA) return;
    kopyala(O.sira.liste[O.sira.n++], ID_N, id);
    kirlet();
}

/* Oyuncular her zaman sıradadır (NPC'leri GM ekler). */
static void oyunculari_siraya_ekle(void)
{
    for (int i = 0; i < O.kar_n; i++)
        if (!O.kar[i]->npc) siraya_ekle(O.kar[i]->id);
}

static void siradan_cikar(const char *id)
{
    for (int i = 0; i < O.sira.n; i++) {
        if (strcmp(O.sira.liste[i], id)) continue;
        int sirasiydi = O.sira.aktif && i == O.sira.indeks;
        memmove(O.sira.liste[i], O.sira.liste[i + 1], (size_t)(O.sira.n - i - 1) * ID_N);
        O.sira.n--;
        if (i < O.sira.indeks || (sirasiydi && O.sira.indeks >= 0)) O.sira.indeks--;
        kirlet();
        if (sirasiydi) sira_ilerle();
        return;
    }
}

/* Sıra başı: enerji dolar, kurallar.c'deki her_tur() çağrılır. */
static void tur_basi(Karakter *k)
{
    k->o.enerji = O.ayar.tur_enerjisi;
    char m[200];
    snprintf(m, sizeof m, "Sıra: %s (tur %d)", k->ad, O.sira.tur_no);
    if (k->npc) kayit_metin("sira", GOR_GM, NULL, m);
    else kayit_metin("sira", GOR_HERKES, NULL, m);
    kural_her_tur(k);
    O.sira.toplam_enerji = k->o.enerji > 0 ? k->o.enerji : 0;
    kirlet();
}

/* Canı 0 ya da altına inmiş olanlar ölür (bir sıranın sonunda ve saldırı ya da büyüden hemen sonra çağrılır). */
static void olumleri_denetle(void)
{
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!k->olu && sahnede(k) && k->o.can <= 0) oldur(k);
    }
}

/* Sırayı bir sonraki uygun karaktere geçirir; liste başa sarınca tur sayısı artar.
   Sıra başında enerjisi olmayan karakterin sırası hemen geçer. */
static void sira_ilerle(void)
{
    if (!O.sira.aktif) return;
    int uygun = 0, sinir = 2 * O.sira.n + 2;
    for (int deneme = 0; deneme < sinir && O.sira.n > 0; deneme++) {
        O.sira.indeks++;
        if (O.sira.indeks >= O.sira.n) {
            O.sira.indeks = 0;
            O.sira.tur_no++;
        }
        Karakter *k = karakter_bul(O.sira.liste[O.sira.indeks]);
        if (!k || k->olu || !sahnede(k)) continue;
        if (!k->npc && (k->baglanti <= 0 || !haritada(k))) {
            if (deneme < O.sira.n) {
                char m[200];
                snprintf(m, sizeof m, k->baglanti <= 0 ? "%s bağlı değil, sırası geçildi." : "%s haritada değil, sırası geçildi.",
                         k->ad);
                kayit_metin("sira", GOR_HERKES, NULL, m);
            }
            continue;
        }
        uygun = 1;
        tur_basi(k);
        if (k->o.enerji > 0) return;
        char m[200];
        snprintf(m, sizeof m, "%s: enerji yok, sıra geçti.", k->ad);
        if (k->npc) kayit_metin("sira", GOR_GM, NULL, m);
        else kayit_metin("sira", GOR_HERKES, NULL, m);
        istekleri_temizle(k->id);
        olumleri_denetle();
    }
    O.sira.aktif = 0;
    O.sira.indeks = -1;
    istekleri_temizle(NULL);
    kayit_metin("sira", GOR_HERKES, NULL,
                uygun ? "Kimsenin enerjisi kalmadı; tur sırası durdu." : "Sırası gelebilecek kimse kalmadı; tur sırası durdu.");
    kirlet();
}

/* Sıradakinin sırası biter: bekleyen istekleri düşer, ölümler denetlenir, sıra ilerler. */
static void tur_bitir_ve_ilerle(void)
{
    Karakter *k = siradaki();
    if (k) istekleri_temizle(k->id);
    olumleri_denetle();
    sira_ilerle();
    kirlet();
}

/* Durumu yayınlar; k sıradaysa ve öldüyse ya da enerjisi bittiyse sıra geçer, yeni durum da yayınlanır. */
static void yayinla_ve_enerji_denetle(Karakter *k)
{
    herkese_durum();
    if (O.sira.aktif && siradaki() == k && (k->olu || k->o.enerji <= 0)) {
        tur_bitir_ve_ilerle();
        herkese_durum();
    }
}

static int uygun_var(void)
{
    for (int i = 0; i < O.sira.n; i++) {
        Karakter *k = karakter_bul(O.sira.liste[i]);
        if (k && !k->olu && sahnede(k) && (k->npc || (k->baglanti > 0 && haritada(k)))) return 1;
    }
    return 0;
}

static void sira_baslat(Istemci *c)
{
    oyunculari_siraya_ekle();
    if (!uygun_var()) {
        istemciye_hata(c, "Tur sırasını başlatmak için sırada haritaya yerleşmiş, bağlı ve hayatta en az bir oyuncu (ya da haritada bir NPC) olmalı.");
        return;
    }
    istekleri_temizle(NULL);
    O.sira.aktif = 1;
    O.sira.tur_no = 1;
    O.sira.indeks = -1; /* ilerleyince ilk uygun karaktere gelir */
    kayit_metin("sira", GOR_HERKES, NULL, "Tur sırası başladı.");
    sira_ilerle();
    kirlet();
}

static void sira_durdur(void)
{
    if (!O.sira.aktif) return;
    O.sira.aktif = 0;
    O.sira.indeks = -1;
    istekleri_temizle(NULL);
    kayit_metin("sira", GOR_HERKES, NULL, "Turlar durdu; GM yeniden başlatana kadar kimse yürüyemez, saldıramaz, büyü yapamaz.");
    kirlet();
}

/* ================================================================== hareket, saldırı ve büyü istekleri */

static Istek *istek_bul(long long id)
{
    return O.istek_var && O.istek.id == id ? &O.istek : NULL;
}

/* kim NULL ise kiminse */
static void istekleri_temizle(const char *kim)
{
    if (O.istek_var && (!kim || !strcmp(O.istek.kim, kim))) O.istek_var = 0;
}

/* Bu karaktere saldırı ya da büyü isteği bekliyorsa düşer (karakter gidince ya da gizlenince). */
static void hedef_istegini_dusur(const char *hedef)
{
    if (O.istek_var && O.istek.tur != ISTEK_HAREKET && !strcmp(O.istek.hedef, hedef)) O.istek_var = 0;
}

/* Yeni istek eskisinin yerini alır. */
static Istek *istek_ekle(int tur, const char *kim)
{
    Istek *I = &O.istek;
    memset(I, 0, sizeof *I);
    I->id = ++O.istek_sayac;
    I->tur = tur;
    kopyala(I->kim, sizeof I->kim, kim);
    O.istek_var = 1;
    return I;
}

/* İstekteki eşyayı envanterde bul: önce aynı sırada, yoksa aynı eşyanın ilk kopyası. */
static int esya_bul(const Karakter *k, const Esya *e, int sira)
{
    if (sira >= 0 && sira < k->o.envanter_adet && esya_esit(&k->o.envanter[sira], e)) return sira;
    for (int i = 0; i < k->o.envanter_adet; i++)
        if (esya_esit(&k->o.envanter[i], e)) return i;
    return -1;
}

static int tamsayi_al(const Json *j, const char *k, int *out)
{
    long long v;
    if (!json_tamsayi(j, k, &v) || v < -1000000 || v > 1000000) return 0;
    *out = (int)v;
    return 1;
}

/* Oyuncu yürüyebilir, saldırabilir ya da büyü yapabilir mi? Değilse nedenini yazar ve 0 döner. */
static int eyleme_hazir(Istemci *c, Karakter *ben, const char *eylem)
{
    char m[200];
    if (!O.aktif || O.gizli) {
        istemciye_hata(c, "Şu an açık bir harita yok.");
        return 0;
    }
    if (!haritada(ben)) {
        istemciye_hata(c, "Henüz haritada değilsin; GM seni haritaya yerleştirecek.");
        return 0;
    }
    if (ben->olu) {
        istemciye_hata(c, "Öldün; GM seni diriltebilir.");
        return 0;
    }
    if (!O.sira.aktif) {
        snprintf(m, sizeof m, "Turlar başlamadı; sıran gelince %s.", eylem);
        istemciye_hata(c, m);
        return 0;
    }
    if (siradaki() != ben) {
        istemciye_hata(c, "Sıra sende değil.");
        durum_gonder(c);
        return 0;
    }
    if (ben->o.enerji <= 0) {
        istemciye_hata(c, "Enerjin kalmadı.");
        return 0;
    }
    return 1;
}

/* Oyuncu haritada bir yere tıkladı: GM'e hareket isteği gider. */
static void m_hareket(Istemci *c, Karakter *ben, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!eyleme_hazir(c, ben, "yürüyebilirsin")) return;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "Şu an açık bir harita yok.");
        return;
    }
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    if (!isfinite(x) || !isfinite(yy)) return;
    hedefe_oturt(h, &x, &yy, g, y);
    if (mesafe(ben->o.x, ben->o.y, x, yy) < 0.02) return;
    Istek *I = istek_ekle(ISTEK_HAREKET, ben->id);
    I->x2 = (float)x;
    I->y2 = (float)yy;
    herkese_durum();
}

/* Oyuncu bir karakteri seçip bir eşyayla saldırmak istedi: GM'e saldırı isteği gider. */
static void m_saldir(Istemci *c, Karakter *ben, const Json *j)
{
    if (!eyleme_hazir(c, ben, "saldırabilirsin")) return;
    Karakter *t = karakter_bul(json_metin(j, "hedef"));
    if (!t || t == ben || !haritada(t) || !goruyor(ben, t)) {
        istemciye_hata(c, "Bu karakteri göremiyorsun.");
        return;
    }
    if (t->olu) {
        istemciye_hata(c, "Bu karakter zaten ölü.");
        return;
    }
    int i;
    if (!tamsayi_al(j, "esya", &i) || i < 0 || i >= ben->o.envanter_adet) {
        istemciye_hata(c, "Saldırmak için envanterinden bir eşya seç.");
        return;
    }
    const char *ad = json_metin(j, "esya_ad");
    if (ad && strcmp(ad, ben->o.envanter[i].ad)) {
        istemciye_hata(c, "Envanterin değişti; eşyayı yeniden seç.");
        durum_gonder(c);
        return;
    }
    Istek *I = istek_ekle(ISTEK_SALDIRI, ben->id);
    kopyala(I->hedef, sizeof I->hedef, t->id);
    I->esya = i;
    I->esya_kopya = ben->o.envanter[i];
    herkese_durum();
}

/* Oyuncu bir karaktere (ya da kendine) büyü yapmak istedi: GM'e büyü isteği gider. */
static void m_buyu(Istemci *c, Karakter *ben, const Json *j)
{
    if (!eyleme_hazir(c, ben, "büyü yapabilirsin")) return;
    Karakter *t = karakter_bul(json_metin(j, "hedef"));
    if (!t || (t != ben && (!haritada(t) || !goruyor(ben, t)))) {
        istemciye_hata(c, "Bu karakteri göremiyorsun.");
        return;
    }
    if (t->olu) {
        istemciye_hata(c, "Bu karakter ölü.");
        return;
    }
    const char *ad = json_metin(j, "buyu");
    if (!buyu_gecerli(ad)) {
        istemciye_hata(c, "Böyle bir büyü yok; listeden seç.");
        return;
    }
    Istek *I = istek_ekle(ISTEK_BUYU, ben->id);
    kopyala(I->hedef, sizeof I->hedef, t->id);
    kirp_onar(I->buyu, sizeof I->buyu, ad);
    herkese_durum();
}

static void m_istek_iptal(Karakter *ben)
{
    if (!istek_kimin(ben->id)) return;
    istekleri_temizle(ben->id);
    herkese_durum();
}

static void m_istek_reddet(Istemci *c, const Json *j)
{
    long long id = 0;
    json_tamsayi(j, "id", &id);
    Istek *I = istek_bul(id);
    if (!I) {
        istemciye_hata(c, "Bu istek artık geçerli değil.");
        durum_gonder(c);
        return;
    }
    Karakter *k = karakter_bul(I->kim);
    int tur = I->tur;
    O.istek_var = 0;
    if (k)
        oyuncuya_bildir(k, "bilgi", tur == ISTEK_HAREKET ? "GM bu hareketi onaylamadı."
                                    : tur == ISTEK_BUYU  ? "GM bu büyüyü onaylamadı."
                                                         : "GM bu saldırıyı onaylamadı.");
    herkese_durum();
}

static void m_istek_onayla(Istemci *c, const Json *j)
{
    long long id = 0;
    json_tamsayi(j, "id", &id);
    Istek *I = istek_bul(id);
    if (!I) {
        istemciye_hata(c, "Bu istek artık geçerli değil.");
        durum_gonder(c);
        return;
    }
    Istek is = *I;
    O.istek_var = 0;
    Karakter *k = karakter_bul(is.kim);
    if (!k || !haritada(k) || k->olu) {
        istemciye_hata(c, "İsteyen karakter artık hareket edemiyor.");
        herkese_durum();
        return;
    }
    if (!O.sira.aktif || siradaki() != k) {
        istemciye_hata(c, "Sıra artık onda değil.");
        herkese_durum();
        return;
    }
    if (is.tur == ISTEK_HAREKET) {
        HaritaKaydi *h = aktif_harita();
        float g, y;
        if (!h || !harita_olcu(h, &g, &y)) {
            herkese_durum();
            return;
        }
        double x = is.x2, yy = is.y2;
        double jx = json_sayi(j, "x", NAN), jy = json_sayi(j, "y", NAN);
        if (isfinite(jx) && isfinite(jy)) {
            /* GM hedefi kaydırdı (ör. engel yüzünden) */
            x = jx;
            yy = jy;
            hedefe_oturt(h, &x, &yy, g, y);
        }
        if (!kural_hareket(k, x, yy)) {
            char m[200];
            snprintf(m, sizeof m, "Kurallar (hareket()) %s için bu yürüyüşe izin vermedi.", k->ad);
            istemciye_hata(c, m);
            oyuncuya_bildir(k, "hata", "Kurallara göre bu yürüyüş yapılamadı.");
        }
        yayinla_ve_enerji_denetle(k);
        return;
    }
    Karakter *t = karakter_bul(is.hedef);
    if (is.tur == ISTEK_BUYU) {
        if (!t || !haritada(t) || t->olu || !buyu_gecerli(is.buyu)) {
            istemciye_hata(c, "Bu büyü artık yapılamaz (hedef ya da büyü değişti).");
            herkese_durum();
            return;
        }
        buyu_uygula(k, t, is.buyu);
        yayinla_ve_enerji_denetle(k);
        return;
    }
    if (!t || t == k || !haritada(t) || t->olu) {
        istemciye_hata(c, "Hedef artık saldırılabilir değil.");
        herkese_durum();
        return;
    }
    int idx = esya_bul(k, &is.esya_kopya, is.esya);
    if (idx < 0) {
        istemciye_hata(c, "Saldırıda seçilen eşya artık envanterde değil.");
        herkese_durum();
        return;
    }
    saldiri_uygula(k, t, idx);
    yayinla_ve_enerji_denetle(k);
}

/* ================================================================== oturumlar */

static void oturum_olustur(int gm, const char *oyuncu, char token[65])
{
    rastgele_hex(token, 32);
    if (O.oturum_n == AZAMI_OTURUM) {
        int en_eski = 0;
        for (int i = 1; i < O.oturum_n; i++)
            if (O.oturum[i].son < O.oturum[en_eski].son) en_eski = i;
        O.oturum[en_eski] = O.oturum[--O.oturum_n];
    }
    Oturum *o = &O.oturum[O.oturum_n++];
    sha1_hex(token, o->ozet);
    o->gm = gm;
    kopyala(o->oyuncu, sizeof o->oyuncu, oyuncu ? oyuncu : "");
    o->son = simdi_sn();
    kirlet();
}

static void oturumlari_sil(int gm, const char *oyuncu, const char *haric_ozet)
{
    for (int i = 0; i < O.oturum_n;) {
        Oturum *o = &O.oturum[i];
        int eslesir = o->gm == gm && (!oyuncu || !strcmp(o->oyuncu, oyuncu)) &&
                      (!haric_ozet || strcmp(o->ozet, haric_ozet));
        if (eslesir) {
            O.oturum[i] = O.oturum[--O.oturum_n];
            kirlet();
        } else {
            i++;
        }
    }
}

int oyun_sifresi_var(void)
{
    return O.ayar.sifre[0] != '\0';
}

void oyun_bilgisi(Buf *b)
{
    buf_puts(b, "{\"oyun_adi\":");
    buf_json_str(b, O.ayar.oyun_adi);
    buf_printf(b, ",\"sifre_var\":%s,\"azami_yukleme\":%lld}", oyun_sifresi_var() ? "true" : "false",
               yap.azami_yukleme);
}

int oyuncu_giris(const char *ad_ham, const char *sifre, char token[65], char *hata, size_t hn)
{
    if (!O.ayar.sifre[0]) {
        kopyala(hata, hn, "GM henüz oyun şifresini belirlemedi. GM'den şifreyi ayarlamasını iste.");
        return 403;
    }
    if (!sifre || !sabit_esit(sifre, O.ayar.sifre)) {
        kopyala(hata, hn, "Şifre yanlış.");
        return 401;
    }
    char ad[AD_N];
    if (!ad_ham || !metin_temizle(ad_ham, ad, sizeof ad, 24)) {
        kopyala(hata, hn, "Oyunda görünecek bir isim yaz.");
        return 400;
    }
    char anahtar[AD_N * 2];
    ad_anahtari(ad, anahtar, sizeof anahtar);
    if (!strcmp(anahtar, "gm")) {
        kopyala(hata, hn, "Bu isim GM'e ayrılmış. Başka bir isim seç.");
        return 409;
    }
    Karakter *p = oyuncu_adla(ad);
    if (p && p->baglanti > 0) {
        kopyala(hata, hn, "Bu isimle biri şu an oyunda. Başka bir isim seç.");
        return 409;
    }
    if (!p) {
        konumlari_kaydet(); /* diğerlerinin şimdiki yerleri kayda geçsin; yenisi boş bir kareye konur */
        if (oyuncu_sayisi() >= AZAMI_OYUNCU || !(p = karakter_olustur(0, NULL, ad, siradaki_renk()))) {
            kopyala(hata, hn, "Oyun dolu.");
            return 409;
        }
        siraya_ekle(p->id);
        konumlari_yukle();
        bilgileri_guncelle();
        char m[200];
        snprintf(m, sizeof m, "%s oyuna katıldı.", p->ad);
        kayit_metin("bilgi", GOR_HERKES, NULL, m);
    }
    oturum_olustur(0, p->id, token);
    herkese_durum();
    return 200;
}

int gm_giris(const char *sifre, char token[65])
{
    if (!sifre || !yap.gm_sifresi[0] || !sabit_esit(sifre, yap.gm_sifresi)) return 401;
    oturum_olustur(1, NULL, token);
    return 200;
}

int oturum_dogrula(const char *token, int gm, char oyuncu[17], char ozet[41])
{
    if (!token || strlen(token) != 64) return 0;
    char h[41];
    sha1_hex(token, h);
    for (int i = 0; i < O.oturum_n; i++) {
        Oturum *o = &O.oturum[i];
        if (strcmp(o->ozet, h) || o->gm != gm) continue;
        if (simdi_sn() - o->son > OTURUM_OMRU) return 0;
        if (!gm) {
            Karakter *k = karakter_bul(o->oyuncu);
            if (!k || k->npc) return 0;
        }
        if (simdi_sn() - o->son > 3600) {
            o->son = simdi_sn();
            kirlet();
        }
        if (oyuncu) kopyala(oyuncu, 17, o->oyuncu);
        if (ozet) kopyala(ozet, 41, o->ozet);
        return 1;
    }
    return 0;
}

void oturum_sil(const char *token)
{
    if (!token || strlen(token) != 64) return;
    char h[41];
    sha1_hex(token, h);
    for (int i = 0; i < O.oturum_n; i++) {
        if (strcmp(O.oturum[i].ozet, h)) continue;
        O.oturum[i] = O.oturum[--O.oturum_n];
        kirlet();
        return;
    }
}

/* ================================================================== istemci olayları */

void istemci_ekle(Istemci *c)
{
    c->sonraki = istemciler;
    istemciler = c;
    Karakter *p = c->gm ? NULL : karakter_bul(c->oyuncu);
    if (p) p->baglanti++;
    kayitlari_gonder(c);
    herkese_durum();
}

void istemci_cikar(Istemci *c)
{
    for (Istemci **pp = &istemciler; *pp; pp = &(*pp)->sonraki) {
        if (*pp == c) {
            *pp = c->sonraki;
            break;
        }
    }
    Karakter *p = c->gm ? NULL : karakter_bul(c->oyuncu);
    if (p && p->baglanti > 0) p->baglanti--;
    herkese_durum();
}

/* Oyuncu başına basit hız sınırı: saniyede 3, art arda en fazla 10 olay */
static int izin_ver(Istemci *c)
{
    long long simdi = simdi_ms();
    if (c->kova_ms == 0) {
        c->kova = 10;
        c->kova_ms = simdi;
    }
    c->kova += (double)(simdi - c->kova_ms) / 1000.0 * 3.0;
    if (c->kova > 10) c->kova = 10;
    c->kova_ms = simdi;
    if (c->kova < 1) return 0;
    c->kova -= 1;
    return 1;
}

static void m_zar(Istemci *c, Karakter *ben, const Json *j)
{
    if (!c->gm && !izin_ver(c)) {
        istemciye_hata(c, "Biraz yavaşla: çok hızlı zar atıyorsun.");
        return;
    }
    const char *ifade = json_metin(j, "ifade");
    char buf[256];
    utf8_kirp(buf, sizeof buf, ifade && ifade[0] ? ifade : "1d20");
    ZarSonuc s;
    char hata[200];
    if (!zar_ifadesi_at(buf, &s, hata, sizeof hata)) {
        istemciye_hata(c, hata);
        return;
    }
    const char *ad = c->gm ? "GM" : ben->ad;
    const char *renk = c->gm ? GM_RENK : ben->renk;
    Buf ic;
    buf_init(&ic);
    buf_puts(&ic, "\"tur\":\"zar\",\"kim\":");
    buf_json_str(&ic, ad);
    buf_printf(&ic, ",\"renk\":\"%s\",\"gm\":%s,\"sonuc\":", renk, c->gm ? "true" : "false");
    zar_json_yaz(&ic, &s);
    kayit_ekle(GOR_HERKES, NULL, ic.p, NULL);
    buf_free(&ic);
}

static void m_isaret(Istemci *c, Karakter *ben, const Json *j)
{
    if (!c->gm && !izin_ver(c)) return;
    HaritaKaydi *h = aktif_harita();
    if (!h || (!c->gm && O.gizli)) return;
    double x = json_sayi(j, "x", -1e9), y = json_sayi(j, "y", -1e9);
    if (x < -1e5 || y < -1e5 || x > 1e5 || y > 1e5) return;
    isaret_yayinla(x, y, c->gm ? "GM" : ben->ad, c->gm ? GM_RENK : ben->renk, c->gm && json_mantik(j, "odak", 0));
}

static void m_tur_bitir(Istemci *c, Karakter *ben)
{
    if (!O.sira.aktif) return;
    Karakter *s = siradaki();
    if (!c->gm && s != ben) {
        istemciye_hata(c, "Sıra sende değil.");
        return;
    }
    if (c->gm && s) {
        char m[200];
        snprintf(m, sizeof m, "GM sırayı geçti (%s).", s->ad);
        kayit_metin("sira", s->npc ? GOR_GM : GOR_HERKES, NULL, m);
    }
    tur_bitir_ve_ilerle();
    herkese_durum();
}

/* --- GM olayları --- */

/* GM haritanın kare cinsinden boyutunu seçti (iletide genislik ve yukseklik varsa). */
static void boyut_al(HaritaKaydi *h, const Json *j)
{
    if (!json_al(j, "genislik") || !json_al(j, "yukseklik")) return;
    h->iz.genislik = sinirla_d(json_sayi(j, "genislik", 0), 1, AZAMI_HARITA);
    h->iz.yukseklik = sinirla_d(json_sayi(j, "yukseklik", 0), 1, AZAMI_HARITA);
    kirlet();
}

static void m_harita_ac(Istemci *c, const Json *j)
{
    const char *dosya = json_metin(j, "dosya");
    int kapat = !dosya || !dosya[0];
    char yol[YOL_AZAMI], goreli[YOL_AZAMI];
    GorselBilgi g;
    if (!kapat) {
        snprintf(goreli, sizeof goreli, "haritalar/%s", dosya);
        if (!goreli_yol_gecerli(dosya) || !tam_yol_kur(yap.oyun_klasoru, goreli, yol, sizeof yol) ||
            !gorsel_bilgi(yol, &g)) {
            istemciye_hata(c, "Bu dosya harita olarak açılamadı. PNG, JPG, WEBP ya da GIF olmalı.");
            return;
        }
    }
    konumlari_kaydet();
    istekleri_temizle(NULL);
    aktif_degistir(kapat ? NULL : dosya);
    if (kapat) {
        kayit_metin("bilgi", GOR_HERKES, NULL, "Harita kapatıldı.");
        herkese_durum();
        return;
    }
    boyut_al(harita_bul(dosya, 1), j);
    konumlari_yukle();
    bilgileri_guncelle();
    char ad[AD_N], m[300];
    esya_adi(dosya, ad, sizeof ad);
    snprintf(m, sizeof m, "Yeni harita: %s", ad);
    kayit_metin("bilgi", O.gizli ? GOR_GM : GOR_HERKES, NULL, m);
    /* Sıra başka haritadaki bir NPC'deyse ilerlesin */
    Karakter *s = siradaki();
    if (s && !sahnede(s)) sira_ilerle();
    herkese_durum();
}

static void m_izgara(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    if (!h) {
        istemciye_hata(c, "Önce bir harita aç.");
        return;
    }
    Izgara *iz = &h->iz;
    boyut_al(h, j);
    iz->opaklik = sinirla_d(json_sayi(j, "opaklik", iz->opaklik), 0.05, 1);
    iz->gorunur = json_mantik(j, "gorunur", iz->gorunur);
    iz->acik = json_mantik(j, "acik", iz->acik);
    iz->sis = json_mantik(j, "sis", iz->sis);
    iz->oturt = json_mantik(j, "oturt", iz->oturt);
    sinirlara_al();
    bilgileri_guncelle();
    kirlet();
    herkese_durum();
}

static void m_kesif_sifirla(void)
{
    HaritaKaydi *h = aktif_harita();
    if (!h) return;
    for (int i = 0; i < h->bilgi_n; i++) bilgi_temizle(&h->bilgi[i]);
    free(h->bilgi);
    h->bilgi = NULL;
    h->bilgi_n = 0;
    bilgileri_guncelle();
    kayit_metin("bilgi", GOR_GM, NULL, "Bu haritada oyuncuların keşfettikleri ve hatırladıkları sıfırlandı.");
    kirlet();
    herkese_durum();
}

static void m_tasi(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) return;
    Karakter *k = karakter_bul(json_metin(j, "id"));
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    if (!k || !haritada(k) || !isfinite(x) || !isfinite(yy)) {
        istemciye_hata(c, "Taşınamadı.");
        return;
    }
    hedefe_oturt(h, &x, &yy, g, y);
    istekleri_temizle(k->id);
    hareket_uygula(k, x, yy);
    herkese_durum();
}

static void m_npc_ekle(Istemci *c)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "NPC eklemek için önce bir harita aç.");
        return;
    }
    if (haritadaki_npc_sayisi() >= AZAMI_NPC) {
        istemciye_hata(c, "Bu haritada çok fazla NPC var.");
        return;
    }
    char ad[AD_N];
    int n = haritadaki_npc_sayisi();
    snprintf(ad, sizeof ad, "NPC %d", n + 1);
    Karakter *k = karakter_olustur(1, NULL, ad, NPC_RENK[n % DIZI_N(NPC_RENK)]);
    if (!k) {
        istemciye_hata(c, "Oyunda çok fazla karakter var.");
        return;
    }
    k->harita = xstrdup(O.aktif);
    k->o.x = k->o.y = -1000; /* boş yer ararken kendine çarpmasın */
    konumlari_kaydet();       /* oyuncuların şimdiki yerleri dolu sayılsın */
    bos_yer(h, g, y, &k->o.x, &k->o.y);
    bilgileri_guncelle();
    herkese_durum();
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"t\":\"npc_eklendi\",\"id\":");
    buf_json_str(&b, k->id);
    buf_putc(&b, '}');
    gonder_buf(c, &b);
    buf_free(&b);
}

static void m_karakter_guncelle(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !sahnede(k)) return;
    if (k->npc) {
        const char *ad = json_metin(j, "ad"), *renk = json_metin(j, "renk");
        char temiz[AD_N];
        if (ad && metin_temizle(ad, temiz, sizeof temiz, 24)) kopyala(k->ad, sizeof k->ad, temiz);
        if (renk_gecerli(renk)) {
            kopyala(k->renk, sizeof k->renk, renk);
            for (char *p = k->renk; *p; p++)
                if (*p >= 'A' && *p <= 'F') *p = (char)(*p + 32);
        }
        k->gizli = json_mantik(j, "gizli", k->gizli);
        if (k->gizli) hedef_istegini_dusur(k->id);
    }
    for (int i = 0; i < DIZI_N(STATLAR); i++) {
        Json *v = json_al(j, STATLAR[i].ad);
        if (v && v->tur == JSON_NUM && isfinite(v->sayi))
            *stat_yeri(&k->o, &STATLAR[i]) = (float)sinirla_d(v->sayi, STATLAR[i].lo, STATLAR[i].hi);
    }
    kirlet();
    bilgileri_guncelle();
    yayinla_ve_enerji_denetle(k);
}

static void m_npc_sil(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !k->npc) return;
    karakter_kaldir(k);
    bilgileri_guncelle();
    herkese_durum();
}

static void m_oldur(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !sahnede(k) || k->olu) return;
    int sirasiydi = siradaki() == k;
    oldur(k);
    if (sirasiydi) tur_bitir_ve_ilerle();
    herkese_durum();
}

static void m_dirilt(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !sahnede(k) || !k->olu) return;
    k->olu = 0;
    if (k->o.can <= 0) k->o.can = 1;
    Buf s;
    buf_init(&s);
    gorenler(&s, k, NULL);
    char m[200];
    snprintf(m, sizeof m, "%s dirildi.", k->ad);
    kayit_metin("bilgi", GOR_GM_SAHIP, s.p, m);
    buf_free(&s);
    bilgileri_guncelle();
    kirlet();
    herkese_durum();
}

static void m_sira_duzenle(const Json *j)
{
    Json *liste = json_al(j, "liste");
    if (!liste || liste->tur != JSON_ARR) return;
    Karakter *s = siradaki();
    char simdiki[ID_N] = "";
    if (s) kopyala(simdiki, sizeof simdiki, s->id);
    char yeni[AZAMI_SIRA][ID_N];
    int n = 0;
    for (size_t i = 0; i < liste->adet && n < AZAMI_SIRA; i++) {
        Json *v = liste->ogeler[i];
        if (v->tur != JSON_STR || !karakter_bul(v->metin)) continue;
        int var = 0;
        for (int k = 0; k < n; k++) var |= !strcmp(yeni[k], v->metin);
        if (!var) kopyala(yeni[n++], ID_N, v->metin);
    }
    O.sira.n = n;
    memcpy(O.sira.liste, yeni, sizeof yeni);
    oyunculari_siraya_ekle();
    O.sira.indeks = -1;
    if (simdiki[0]) {
        for (int i = 0; i < O.sira.n; i++)
            if (!strcmp(O.sira.liste[i], simdiki)) O.sira.indeks = i;
        if (O.sira.indeks < 0) {
            O.sira.indeks = -1;
            sira_ilerle();
        }
    }
    kirlet();
    herkese_durum();
}

static void m_siraya(const Json *j, int ekle)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !k->npc) return;
    if (ekle) siraya_ekle(k->id);
    else siradan_cikar(k->id);
    herkese_durum();
}

typedef struct {
    char id[ID_N];
    int deger, kura;
} SiraZari;

/* Büyük zar önce; eşitlikte kura */
static int sira_zari_karsilastir(const void *a, const void *b)
{
    const SiraZari *x = a, *y = b;
    if (x->deger != y->deger) return y->deger - x->deger;
    return (y->kura > x->kura) - (y->kura < x->kura);
}

static void m_sira_zarla(void)
{
    oyunculari_siraya_ekle();
    SiraZari z[AZAMI_SIRA];
    for (int i = 0; i < O.sira.n; i++) {
        kopyala(z[i].id, ID_N, O.sira.liste[i]);
        z[i].deger = zar_at(1, 20);
        z[i].kura = (int)rastgele_aralik(1000000);
    }
    qsort(z, (size_t)O.sira.n, sizeof z[0], sira_zari_karsilastir);
    for (int i = 0; i < O.sira.n; i++) kopyala(O.sira.liste[i], ID_N, z[i].id);
    Buf b, oz;
    buf_init(&b);
    buf_init(&oz);
    buf_puts(&b, "Sıra zarları (1d20): ");
    buf_puts(&oz, "Sıra zarları (1d20): ");
    int ilk_oz = 1;
    for (int i = 0; i < O.sira.n; i++) {
        Karakter *k = karakter_bul(O.sira.liste[i]);
        buf_printf(&b, "%s%s %d", i ? ", " : "", k ? k->ad : "?", z[i].deger);
        if (k && !k->npc) {
            buf_printf(&oz, "%s%s %d", ilk_oz ? "" : ", ", k->ad, z[i].deger);
            ilk_oz = 0;
        }
    }
    /* NPC'lerin adları ve zarları yalnızca GM'e */
    Buf ic, ozic;
    buf_init(&ic);
    buf_init(&ozic);
    buf_puts(&ic, "\"tur\":\"sira\",\"metin\":");
    buf_json_str(&ic, b.p);
    buf_puts(&ozic, "\"tur\":\"sira\",\"metin\":");
    buf_json_str(&ozic, oz.p);
    kayit_ekle(GOR_GM, NULL, ic.p, ozic.p);
    buf_free(&ic);
    buf_free(&ozic);
    buf_free(&b);
    buf_free(&oz);
    if (O.sira.aktif) {
        /* Yeni sırayla yeni bir tur başlar */
        Karakter *s = siradaki();
        if (s) istekleri_temizle(s->id);
        O.sira.tur_no++;
        O.sira.indeks = -1;
        sira_ilerle();
    }
    kirlet();
    herkese_durum();
}

_Static_assert(sizeof(((Esya *)0)->resim) == ESYA_YOLU_AZAMI + 1, "Esya.resim boyu ESYA_YOLU_AZAMI ile uyuşmuyor");

/* esyalar/ altındaki bir dosyadan yeni eşya (ağırlığı 1). Olmazsa nedenini GM'e yazar ve 0 döner. */
static int katalog_esyasi(Istemci *c, const char *esya, Esya *e)
{
    if (!esya || !esya[0] || !goreli_yol_gecerli(esya)) {
        istemciye_hata(c, "Eşya bulunamadı.");
        return 0;
    }
    char resim[YOL_AZAMI];
    snprintf(resim, sizeof resim, "esyalar/%s", esya);
    if (strlen(resim) > ESYA_YOLU_AZAMI) {
        istemciye_hata(c, "Eşyanın dosya yolu çok uzun (en fazla 255 bayt). Dosya yöneticisinden adını kısalt.");
        return 0;
    }
    onbellegi_bosalt();
    if (!oyun_dosyasi_var(resim)) {
        istemciye_hata(c, "Bu eşyanın dosyası esyalar klasöründe yok.");
        return 0;
    }
    char ad[64], sinif[32];
    esya_adi(esya, ad, sizeof ad);
    esya_sinifi(esya, sinif, sizeof sinif);
    esya_doldur(e, ad, sinif, resim);
    return 1;
}

static void envantere_eklendi(Karakter *k, const char *ad)
{
    char m[300];
    snprintf(m, sizeof m, "Envantere eklendi (%s): %s", k->ad, ad);
    kayit_sahibe("esya", k, m);
}

static void m_esya_ver(Istemci *c, const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "karakter"));
    Esya e;
    if (!k) {
        istemciye_hata(c, "Karakter bulunamadı.");
        return;
    }
    if (!katalog_esyasi(c, json_metin(j, "esya"), &e)) return;
    if (!envantere_koy(k, &e)) {
        istemciye_hata(c, "Envanter dolu.");
        return;
    }
    envantere_eklendi(k, e.ad);
    herkese_durum();
}

static void m_esya_al(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "karakter"));
    int i;
    if (!k || !tamsayi_al(j, "sira", &i) || i < 0 || i >= k->o.envanter_adet || k->o.envanter[i].sabit) return;
    char m[300];
    snprintf(m, sizeof m, "Envanterden çıkarıldı (%s): %s", k->ad, k->o.envanter[i].ad);
    envanter_cikar(&k->o, i);
    /* Bu eşyayla bekleyen saldırı isteği varsa sırası kayabilir; esya_bul onaylarken bulur */
    kayit_sahibe("esya", k, m);
    herkese_durum();
}

static void m_esya_duzenle(Istemci *c, const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "karakter"));
    int i;
    if (!k || !tamsayi_al(j, "sira", &i) || i < 0 || i >= k->o.envanter_adet) return;
    if (k->o.envanter[i].sabit) {
        istemciye_hata(c, "Yumruk kurallar.c'de tanımlıdır; orada değiştirilir.");
        return;
    }
    const char *ad = json_metin(j, "ad"), *sinif = json_metin(j, "sinif");
    Esya *e = &k->o.envanter[i];
    char temiz[200], eski[64];
    kopyala(eski, sizeof eski, e->ad);
    if (ad) {
        if (!metin_temizle(ad, temiz, sizeof temiz, 60)) {
            istemciye_hata(c, "Eşyanın adı boş olamaz.");
            return;
        }
        kirp_onar(e->ad, sizeof e->ad, temiz);
    }
    if (sinif) {
        if (sinif[0] && !metin_temizle(sinif, temiz, sizeof temiz, 31)) temiz[0] = '\0';
        if (!sinif[0]) temiz[0] = '\0';
        kirp_onar(e->sinif, sizeof e->sinif, temiz);
    }
    Json *ag = json_al(j, "agirlik");
    if (ag && ag->tur == JSON_NUM && isfinite(ag->sayi)) {
        e->agirlik = (float)sinirla_d(ag->sayi, 0, AZAMI_STAT);
        agirlik_hesapla(&k->o);
    }
    kirlet();
    char m[300];
    if (strcmp(eski, e->ad)) snprintf(m, sizeof m, "Eşya değişti (%s): %s → %s", k->ad, eski, e->ad);
    else snprintf(m, sizeof m, "Eşya değişti (%s): %s", k->ad, e->ad);
    kayit_sahibe("esya", k, m);
    herkese_durum();
}

static void m_gm_saldiri(Istemci *c, const Json *j)
{
    Karakter *a = karakter_bul(json_metin(j, "saldiran")), *t = karakter_bul(json_metin(j, "hedef"));
    int i;
    if (!a || !O.sira.aktif || siradaki() != a) {
        istemciye_hata(c, "Yalnızca sırası gelen karakter saldırabilir.");
        return;
    }
    if (!t || a == t || !haritada(a) || !haritada(t)) {
        istemciye_hata(c, "Saldıran ve hedef açık haritada olmalı.");
        return;
    }
    if (a->olu || t->olu) {
        istemciye_hata(c, "Ölüler saldıramaz ve ölülere saldırılamaz.");
        return;
    }
    if (!tamsayi_al(j, "esya", &i) || i < 0 || i >= a->o.envanter_adet) {
        istemciye_hata(c, "Saldıranın envanterinden bir eşya seç.");
        return;
    }
    istekleri_temizle(a->id);
    saldiri_uygula(a, t, i);
    yayinla_ve_enerji_denetle(a);
}

/* GM sırası gelen karaktere büyü yaptırır (onay beklemez). */
static void m_gm_buyu(Istemci *c, const Json *j)
{
    Karakter *a = karakter_bul(json_metin(j, "yapan")), *t = karakter_bul(json_metin(j, "hedef"));
    if (!a || !O.sira.aktif || siradaki() != a) {
        istemciye_hata(c, "Yalnızca sırası gelen karakter büyü yapabilir.");
        return;
    }
    if (!t || !haritada(a) || !haritada(t)) {
        istemciye_hata(c, "Büyüyü yapan ve hedef açık haritada olmalı.");
        return;
    }
    if (a->olu || t->olu) {
        istemciye_hata(c, "Ölüler büyü yapamaz ve ölülere büyü yapılamaz.");
        return;
    }
    const char *ad = json_metin(j, "buyu");
    if (!buyu_gecerli(ad)) {
        istemciye_hata(c, "Böyle bir büyü yok.");
        return;
    }
    istekleri_temizle(a->id);
    buyu_uygula(a, t, ad);
    yayinla_ve_enerji_denetle(a);
}

/* GM bir karakteri (listeden sürükleyerek) haritaya yerleştirdi. Zaten haritadaysa oraya yürür. */
static void m_yerlestir(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "Önce bir harita aç.");
        return;
    }
    Karakter *k = karakter_bul(json_metin(j, "id"));
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    if (!k || !isfinite(x) || !isfinite(yy)) {
        istemciye_hata(c, "Yerleştirilemedi.");
        return;
    }
    if (k->npc && !haritada(k)) {
        istemciye_hata(c, "Bu NPC başka bir haritada.");
        return;
    }
    hedefe_oturt(h, &x, &yy, g, y);
    istekleri_temizle(k->id);
    if (haritada(k)) {
        hareket_uygula(k, x, yy);
    } else {
        k->o.x = (float)x;
        k->o.y = (float)yy;
        k->yerde = 1;
        konum_yaz(h, k->id, k->o.x, k->o.y);
        char m[200];
        snprintf(m, sizeof m, "%s haritaya yerleştirildi.", k->ad);
        kayit_sahibe("bilgi", k, m);
        bilgileri_guncelle();
        yerden_al(k);
    }
    herkese_durum();
}

/* GM bir oyuncuyu açık haritadan kaldırdı: yeniden yerleştirilene kadar haritada değildir. */
static void m_haritadan_kaldir(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    HaritaKaydi *h = aktif_harita();
    if (!k || k->npc || !h || !k->yerde) return;
    k->yerde = 0;
    konum_sil(h, k->id);
    istekleri_temizle(k->id);
    hedef_istegini_dusur(k->id);
    char m[200];
    snprintf(m, sizeof m, "%s haritadan kaldırıldı.", k->ad);
    kayit_sahibe("bilgi", k, m);
    bilgileri_guncelle();
    if (O.sira.aktif && siradaki() == k) tur_bitir_ve_ilerle();
    herkese_durum();
}

/* GM bir eşyayı haritaya sürükleyip bıraktı. Kaynak: katalogdaki bir dosya ("katalog"), bir karakterin
   envanterindeki eşya ("envanter") ya da yerdeki eşya ("yer"). Bırakılan yerde yaşayan bir karakter varsa eşya
   onun envanterine gider; yoksa (ya da envanteri doluysa) yere konur. */
static void m_esya_birak(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "Önce bir harita aç.");
        return;
    }
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    const char *kaynak = json_metin(j, "kaynak");
    if (!isfinite(x) || !isfinite(yy) || !kaynak) return;
    x = sinirla_d(x, 0, g);
    yy = sinirla_d(yy, 0, y);

    Esya e;
    Karakter *veren = NULL;
    int sira = -1, yer_sira = -1;
    if (!strcmp(kaynak, "katalog")) {
        if (!katalog_esyasi(c, json_metin(j, "esya"), &e)) return;
    } else if (!strcmp(kaynak, "envanter")) {
        veren = karakter_bul(json_metin(j, "karakter"));
        if (!veren || !tamsayi_al(j, "sira", &sira) || sira < 0 || sira >= veren->o.envanter_adet) return;
        const char *ad = json_metin(j, "ad");
        if (ad && strcmp(ad, veren->o.envanter[sira].ad)) {
            istemciye_hata(c, "Envanter değişti; yeniden dene.");
            return;
        }
        if (veren->o.envanter[sira].sabit) {
            istemciye_hata(c, "Yumruk bırakılamaz.");
            return;
        }
        e = veren->o.envanter[sira];
    } else if (!strcmp(kaynak, "yer")) {
        long long id = 0;
        json_tamsayi(j, "id", &id);
        yer_sira = yer_esyasi_sira(h, id);
        if (yer_sira < 0) {
            istemciye_hata(c, "Bu eşya artık yerde değil.");
            return;
        }
        e = h->yer[yer_sira].e;
    } else {
        return;
    }

    /* Bırakılan yerdeki (en yakın) yaşayan karakter; pul büyüdükçe hedefi de büyür */
    Karakter *alan = NULL;
    double en = 1e9;
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!haritada(k) || k->olu) continue;
        double d = mesafe(k->o.x, k->o.y, x, yy);
        if (d <= BIRAKMA_MESAFESI * token_olcegi(k) && d < en) {
            en = d;
            alan = k;
        }
    }
    if (alan && alan == veren) return;
    char m[300];
    if (alan && envantere_koy(alan, &e)) {
        if (veren) envanter_cikar(&veren->o, sira);
        if (yer_sira >= 0) yerden_kaldir(h, yer_sira);
        envantere_eklendi(alan, e.ad);
        herkese_durum();
        return;
    }
    if (alan) {
        snprintf(m, sizeof m, "%s karakterinin envanteri dolu; eşya yere kondu.", alan->ad);
        istemciye_hata(c, m);
    }
    hedefe_oturt(h, &x, &yy, g, y);
    if (yer_sira >= 0) {
        h->yer[yer_sira].x = (float)x;
        h->yer[yer_sira].y = (float)yy;
        kirlet();
    } else {
        if (!yere_koy(h, &e, x, yy)) {
            istemciye_hata(c, "Bu haritada yerde çok fazla eşya var.");
            return;
        }
        if (veren) {
            snprintf(m, sizeof m, "Envanterden yere kondu (%s): %s", veren->ad, e.ad);
            kayit_sahibe("esya", veren, m);
            envanter_cikar(&veren->o, sira);
        }
    }
    herkese_durum();
}

static void m_yer_esya_sil(const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    long long id = 0;
    json_tamsayi(j, "id", &id);
    int i = yer_esyasi_sira(h, id);
    if (i < 0) return;
    yerden_kaldir(h, i);
    herkese_durum();
}

/* Oyuncu bir eşyasını bulunduğu yere bıraktı (yumruk bırakılamaz). */
static void m_esya_yere(Istemci *c, Karakter *ben, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    int i;
    if (!h || O.gizli || !haritada(ben)) {
        istemciye_hata(c, "Eşya bırakmak için haritada olmalısın.");
        return;
    }
    if (ben->olu) {
        istemciye_hata(c, "Ölüler eşya bırakamaz.");
        return;
    }
    if (!tamsayi_al(j, "sira", &i) || i < 0 || i >= ben->o.envanter_adet) return;
    const char *ad = json_metin(j, "ad");
    if (ad && strcmp(ad, ben->o.envanter[i].ad)) {
        istemciye_hata(c, "Envanterin değişti; yeniden dene.");
        durum_gonder(c);
        return;
    }
    if (ben->o.envanter[i].sabit) {
        istemciye_hata(c, "Yumruk bırakılamaz.");
        return;
    }
    Esya e = ben->o.envanter[i];
    if (!yere_koy(h, &e, ben->o.x, ben->o.y)) {
        istemciye_hata(c, "Bu haritada yerde çok fazla eşya var.");
        return;
    }
    envanter_cikar(&ben->o, i);
    Buf s;
    buf_init(&s);
    gorenler(&s, ben, NULL);
    char m[300];
    snprintf(m, sizeof m, "%s yere bıraktı: %s", ben->ad, e.ad);
    kayit_metin("esya", GOR_GM_SAHIP, s.p, m);
    buf_free(&s);
    herkese_durum();
}

/* Görünüm dosyası: klasor/ altında, var olan bir görsel (ya da "": yok) */
static int gorunum_dosyasi_gecerli(const char *yol, const char *klasor)
{
    size_t n = strlen(klasor);
    if (!yol[0]) return 1;
    return !strncmp(yol, klasor, n) && yol[n] == '/' && yol[n + 1] && strlen(yol) <= ESYA_YOLU_AZAMI &&
           goreli_yol_gecerli(yol) && gorunum_uzantili(yol) && oyun_dosyasi_var(yol);
}

/* Token resmi ve şapka. Oyuncu kendisininkini, GM herkesinkini değiştirir; GM onayı gerekmez. */
static void m_gorunum(Istemci *c, Karakter *ben, const Json *j)
{
    Karakter *k = c->gm ? karakter_bul(json_metin(j, "id")) : ben;
    if (!k) return;
    const char *resim = json_metin(j, "resim"), *sapka = json_metin(j, "sapka");
    if (resim && !gorunum_dosyasi_gecerli(resim, "tokenlar")) {
        istemciye_hata(c, "Bu resim tokenlar klasöründe yok.");
        return;
    }
    if (sapka && !gorunum_dosyasi_gecerli(sapka, "sapkalar")) {
        istemciye_hata(c, "Bu şapka sapkalar klasöründe yok.");
        return;
    }
    if (resim) kopyala(k->resim, sizeof k->resim, resim);
    if (sapka) kopyala(k->sapka, sizeof k->sapka, sapka);
    kirlet();
    herkese_durum();
}

static void oyunculari_kopar(void)
{
    for (Istemci *c = istemciler; c; c = c->sonraki)
        if (!c->gm) istemciyi_kopar(c);
}

static void m_ayarlar(Istemci *c, const Json *j)
{
    const char *ad = json_metin(j, "oyun_adi"), *sifre = json_metin(j, "sifre");
    char temiz[AD_N];
    if (ad && metin_temizle(ad, temiz, sizeof temiz, 40)) kopyala(O.ayar.oyun_adi, sizeof O.ayar.oyun_adi, temiz);
    if (sifre) {
        char s[AD_N];
        if (!metin_temizle(sifre, s, sizeof s, 64)) {
            istemciye_hata(c, "Şifre boş olamaz.");
        } else if (!strcmp(s, yap.gm_sifresi)) {
            istemciye_hata(c, "Oyun şifresi GM şifresiyle aynı olamaz.");
        } else if (strcmp(s, O.ayar.sifre)) {
            kopyala(O.ayar.sifre, sizeof O.ayar.sifre, s);
            /* Eski şifreyle girenler çıkarılır; yeni şifreyle tekrar girerler */
            oturumlari_sil(0, NULL, NULL);
            oyunculari_kopar();
            kayit_metin("bilgi", GOR_GM, NULL, "Oyun şifresi değişti; oyuncuların yeni şifreyle girmesi gerekiyor.");
        }
    }
    Json *e = json_al(j, "tur_enerjisi");
    if (e && e->tur == JSON_NUM && isfinite(e->sayi)) O.ayar.tur_enerjisi = (float)sinirla_d(e->sayi, 0, 100000);
    Json *g = json_al(j, "gorus");
    if (g && g->tur == JSON_NUM && isfinite(g->sayi)) {
        O.ayar.gorus = (float)sinirla_d(g->sayi, 0, AZAMI_GORUS);
        bilgileri_guncelle();
    }
    kirlet();
    herkese_durum();
}

static void durum_json_yaz(Buf *b);

/* Şu anki durumu oyun klasörüne ayrı bir dosya olarak yazar (sıfırlamadan önce yedek). */
static void durum_yedekle(const char *ad)
{
    Buf b;
    buf_init(&b);
    durum_json_yaz(&b);
    char yol[YOL_AZAMI + 64];
    snprintf(yol, sizeof yol, "%s/%s", yap.oyun_klasoru, ad);
    FILE *f = fopen(yol, "wb");
    if (f) {
        if (fwrite(b.p, 1, b.len, f) != b.len) fprintf(stderr, "%s yazılamadı.\n", yol);
        fclose(f);
    }
    buf_free(&b);
}

/* Maçı sıfırlar: NPC'ler, oyuncuların statları, envanterleri (yumruk kalır), konumları, ölümleri, keşifler,
   yerdeki eşyalar, tur sırası ve kayıt silinir. Ayarlar, haritaların boyutları ve oyuncuların kendileri
   (adları, oturumları, görünümleri) kalır. Önceki hali durum-sifirlamadan-once.json'a yazılır. */
static void m_mac_sifirla(void)
{
    durum_yedekle("durum-sifirlamadan-once.json");
    O.sira.aktif = 0; /* NPC'ler silinirken sıra ilerlemesin */
    O.sira.indeks = -1;
    O.istek_var = 0;
    for (int i = 0; i < O.kar_n;) {
        if (O.kar[i]->npc) {
            karakter_kaldir(O.kar[i]);
            continue;
        }
        i++;
    }
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        Oyuncu *o = &k->o;
        k->olu = 0;
        k->yerde = 0;
        o->x = o->y = 0;
        o->can = VARSAYILAN_CAN;
        o->odak = o->zirh = o->buyu_direnci = 0;
        o->enerji = O.ayar.tur_enerjisi;
        o->envanter_adet = 0;
        yumruk_ver(o);
        agirlik_hesapla(o);
    }
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
        free(h->konum);
        h->konum = NULL;
        h->konum_n = 0;
        for (int k = 0; k < h->bilgi_n; k++) bilgi_temizle(&h->bilgi[k]);
        free(h->bilgi);
        h->bilgi = NULL;
        h->bilgi_n = 0;
        free(h->yer);
        h->yer = NULL;
        h->yer_n = 0;
    }
    O.sira.n = 0;
    O.sira.tur_no = 1;
    O.sira.toplam_enerji = 0;
    oyunculari_siraya_ekle();
    for (int i = 0; i < O.kayit_n; i++) {
        free(O.kayit[i].tam);
        free(O.kayit[i].ozet);
        free(O.kayit[i].sahipler);
    }
    O.kayit_n = 0;
    O.izin_n = 0;
    kirlet();
    kayit_metin("bilgi", GOR_HERKES, NULL, "GM maçı sıfırladı. Yeni maç başlıyor; GM herkesi haritaya yerleştirecek.");
    for (Istemci *c = istemciler; c; c = c->sonraki) kayitlari_gonder(c);
    herkese_durum();
}

static void m_oyuncu_sil(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || k->npc) return;
    char id[ID_N], ad[AD_N];
    kopyala(id, sizeof id, k->id);
    kopyala(ad, sizeof ad, k->ad);
    for (Istemci *c = istemciler; c; c = c->sonraki)
        if (!c->gm && !strcmp(c->oyuncu, id)) istemciyi_kopar(c);
    oturumlari_sil(0, id, NULL);
    karakter_kaldir(k);
    char m[200];
    snprintf(m, sizeof m, "%s oyundan çıkarıldı.", ad);
    kayit_metin("bilgi", GOR_HERKES, NULL, m);
    bilgileri_guncelle();
    herkese_durum();
}

void istemci_mesaj(Istemci *c, const char *metin, size_t n)
{
    Json *j = json_ayristir(metin, n);
    if (!j || j->tur != JSON_OBJ) {
        json_sil(j);
        return;
    }
    const char *t = json_metin(j, "t");
    Karakter *ben = c->gm ? NULL : karakter_bul(c->oyuncu);
    if (!t || (!c->gm && (!ben || ben->npc))) goto son;

    if (!strcmp(t, "zar")) m_zar(c, ben, j);
    else if (!strcmp(t, "isaret")) m_isaret(c, ben, j);
    else if (!strcmp(t, "tur_bitir")) m_tur_bitir(c, ben);
    else if (!c->gm) {
        if (!strcmp(t, "istek_iptal")) m_istek_iptal(ben);
        else if (!izin_ver(c)) istemciye_hata(c, "Biraz yavaşla.");
        else if (!strcmp(t, "hareket")) m_hareket(c, ben, j);
        else if (!strcmp(t, "saldir")) m_saldir(c, ben, j);
        else if (!strcmp(t, "buyu")) m_buyu(c, ben, j);
        else if (!strcmp(t, "esya_yere")) m_esya_yere(c, ben, j);
        else if (!strcmp(t, "gorunum")) m_gorunum(c, ben, j);
        else istemciye_hata(c, "Bunu yalnızca GM yapabilir.");
    } else if (!strcmp(t, "istek_onayla")) m_istek_onayla(c, j);
    else if (!strcmp(t, "istek_reddet")) m_istek_reddet(c, j);
    else if (!strcmp(t, "harita_ac")) m_harita_ac(c, j);
    else if (!strcmp(t, "harita_gizle")) {
        O.gizli = json_mantik(j, "gizli", O.gizli);
        if (O.gizli) istekleri_temizle(NULL);
        kirlet();
        herkese_durum();
    } else if (!strcmp(t, "izgara")) m_izgara(c, j);
    else if (!strcmp(t, "kesif_sifirla")) m_kesif_sifirla();
    else if (!strcmp(t, "tasi")) m_tasi(c, j);
    else if (!strcmp(t, "npc_ekle")) m_npc_ekle(c);
    else if (!strcmp(t, "karakter_guncelle")) m_karakter_guncelle(j);
    else if (!strcmp(t, "npc_sil")) m_npc_sil(j);
    else if (!strcmp(t, "oldur")) m_oldur(j);
    else if (!strcmp(t, "dirilt")) m_dirilt(j);
    else if (!strcmp(t, "sira_baslat")) {
        sira_baslat(c);
        herkese_durum();
    } else if (!strcmp(t, "sira_durdur")) {
        sira_durdur();
        herkese_durum();
    } else if (!strcmp(t, "sira_duzenle")) m_sira_duzenle(j);
    else if (!strcmp(t, "siraya_ekle")) m_siraya(j, 1);
    else if (!strcmp(t, "siradan_cikar")) m_siraya(j, 0);
    else if (!strcmp(t, "sira_zarla")) m_sira_zarla();
    else if (!strcmp(t, "esya_ver")) m_esya_ver(c, j);
    else if (!strcmp(t, "esya_al")) m_esya_al(j);
    else if (!strcmp(t, "esya_duzenle")) m_esya_duzenle(c, j);
    else if (!strcmp(t, "gm_saldiri")) m_gm_saldiri(c, j);
    else if (!strcmp(t, "gm_buyu")) m_gm_buyu(c, j);
    else if (!strcmp(t, "yerlestir")) m_yerlestir(c, j);
    else if (!strcmp(t, "haritadan_kaldir")) m_haritadan_kaldir(j);
    else if (!strcmp(t, "esya_birak")) m_esya_birak(c, j);
    else if (!strcmp(t, "yer_esya_sil")) m_yer_esya_sil(j);
    else if (!strcmp(t, "gorunum")) m_gorunum(c, ben, j);
    else if (!strcmp(t, "mac_sifirla")) m_mac_sifirla();
    else if (!strcmp(t, "ayarlar")) m_ayarlar(c, j);
    else if (!strcmp(t, "oyuncu_sil")) m_oyuncu_sil(j);
son:
    json_sil(j);
}

/* ================================================================== dosya yöneticisiyle uyum */

int oyuncu_dosya_izni(const char *oyuncu, const char *goreli)
{
    if (O.aktif && !O.gizli && !strncmp(goreli, "haritalar/", 10) && !strcmp(goreli + 10, O.aktif)) return 1;
    /* Token resimleri ve şapkalar herkesin seçebileceği görseller */
    if (!strncmp(goreli, "tokenlar/", 9) || !strncmp(goreli, "sapkalar/", 9)) return 1;
    Karakter *k = karakter_bul(oyuncu);
    if (!k || k->npc) return 0;
    for (int i = 0; i < k->o.envanter_adet; i++)
        if (!strcmp(k->o.envanter[i].resim, goreli)) return 1;
    HaritaKaydi *h = aktif_harita();
    for (int i = 0; h && i < h->yer_n; i++)
        if (!strcmp(h->yer[i].e.resim, goreli) && yer_goruyor(k, &h->yer[i])) return 1;
    long long simdi = simdi_sn();
    for (int i = 0; i < O.izin_n; i++)
        if (O.izin[i].bitis >= simdi && !strcmp(O.izin[i].oyuncu, oyuncu) && !strcmp(O.izin[i].resim, goreli)) return 1;
    return 0;
}

int korunan_yol(const char *goreli)
{
    return !goreli[0] || !strcmp(goreli, "durum.json") || !strcmp(goreli, "haritalar") || !strcmp(goreli, "esyalar") ||
           !strcmp(goreli, "tokenlar") || !strcmp(goreli, "sapkalar");
}

/* ref, eski yolun kendisi ya da altındaysa yeni karşılığını döner (yeni NULL ise "" döner: silindi). */
static char *yol_degisti(const char *ref, const char *eski, const char *yeni)
{
    size_t n = strlen(eski);
    if (strncmp(ref, eski, n) || (ref[n] != '\0' && ref[n] != '/')) return NULL;
    if (!yeni) return xstrdup("");
    Buf b;
    buf_init(&b);
    buf_puts(&b, yeni);
    buf_puts(&b, ref + n);
    return buf_take(&b);
}

/* Eşyanın resmi taşındıysa yolu güncellenir (eşya kalır). */
static void esya_yolu_degisti(Esya *e, const char *eski, const char *yeni)
{
    char *y = yol_degisti(e->resim, eski, yeni);
    if (!y) return;
    if (y[0] && strlen(y) < sizeof e->resim) kopyala(e->resim, sizeof e->resim, y);
    free(y);
    kirlet();
}

/* Görünüm dosyası (token resmi, şapka) taşındıysa yolu güncellenir, silindiyse görünüm kalkar. */
static void gorunum_yolu_degisti(char *alan, size_t n, const char *klasor, const char *eski, const char *yeni)
{
    char *y = yol_degisti(alan, eski, yeni);
    if (!y) return;
    size_t k = strlen(klasor);
    int gecerli = y[0] && strlen(y) < n && !strncmp(y, klasor, k) && y[k] == '/';
    kopyala(alan, n, gecerli ? y : "");
    free(y);
    kirlet();
}

void dosya_tasindi(const char *eski, const char *yeni)
{
    onbellegi_bosalt();
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *kr = O.kar[i];
        for (int k = 0; k < kr->o.envanter_adet; k++) esya_yolu_degisti(&kr->o.envanter[k], eski, yeni);
        gorunum_yolu_degisti(kr->resim, sizeof kr->resim, "tokenlar", eski, yeni);
        gorunum_yolu_degisti(kr->sapka, sizeof kr->sapka, "sapkalar", eski, yeni);
    }
    for (int i = 0; i < O.harita_n; i++)
        for (int k = 0; k < O.harita[i].yer_n; k++) esya_yolu_degisti(&O.harita[i].yer[k].e, eski, yeni);
    /* Haritalar */
    char tam[YOL_AZAMI * 2];
    for (int i = 0; i < O.harita_n;) {
        HaritaKaydi *h = &O.harita[i];
        snprintf(tam, sizeof tam, "haritalar/%s", h->dosya);
        char *y = yol_degisti(tam, eski, yeni);
        if (!y) {
            i++;
            continue;
        }
        int aktifti = O.aktif && !strcmp(O.aktif, h->dosya);
        if (y[0] && !strncmp(y, "haritalar/", 10) && y[10]) {
            const char *yd = y + 10;
            /* Başka bir haritanın üzerine taşındıysa o haritanın eski durumu gider */
            HaritaKaydi *eski_h = harita_bul(yd, 0);
            if (eski_h && eski_h != h) {
                int j = (int)(eski_h - O.harita);
                if (O.aktif && !strcmp(O.aktif, eski_h->dosya) && !aktifti) aktif_degistir(NULL);
                harita_sil(eski_h);
                if (j < i) i--;
                h = &O.harita[i];
            }
            for (int k = 0; k < O.kar_n; k++) {
                Karakter *kr = O.kar[k];
                if (kr->npc && kr->harita && !strcmp(kr->harita, h->dosya)) {
                    free(kr->harita);
                    kr->harita = xstrdup(yd);
                }
            }
            free(h->dosya);
            h->dosya = xstrdup(yd);
            h->gen = 0;
            if (aktifti) aktif_degistir(yd);
            i++;
        } else {
            if (aktifti) {
                aktif_degistir(NULL);
                istekleri_temizle(NULL);
            }
            harita_sil(h);
        }
        free(y);
        kirlet();
    }
    if (O.aktif && !harita_bul(O.aktif, 0)) aktif_degistir(NULL);
    Karakter *s = siradaki();
    if (s && !sahnede(s)) sira_ilerle();
    herkese_durum();
}

void dosyalar_degisti(void)
{
    onbellegi_bosalt();
    herkese_durum();
}

/* ================================================================== kalıcılık (durum.json) */

/* durum.json biçimi: 2 = yerleşik yumruk, eşya ağırlıkları, yerdeki eşyalar, genel görüş */
#define DURUM_SURUMU 2

/* {"ad":..,"sinif":..,"resim":..,"agirlik":..} */
static void esya_kaydi_yaz(Buf *b, const Esya *e)
{
    buf_puts(b, "{\"ad\":");
    buf_json_str(b, e->ad);
    buf_puts(b, ",\"sinif\":");
    buf_json_str(b, e->sinif);
    buf_puts(b, ",\"resim\":");
    buf_json_str(b, e->resim);
    sayi_alani(b, "agirlik", e->agirlik);
    buf_putc(b, '}');
}

static void durum_json_yaz(Buf *b)
{
    konumlari_kaydet();
    buf_printf(b, "{\"surum\":%d,\"sayac\":%lld,\"ayarlar\":{\"oyun_adi\":", DURUM_SURUMU, O.sayac);
    buf_json_str(b, O.ayar.oyun_adi);
    buf_puts(b, ",\"sifre\":");
    buf_json_str(b, O.ayar.sifre);
    sayi_alani(b, "tur_enerjisi", O.ayar.tur_enerjisi);
    sayi_alani(b, "gorus", O.ayar.gorus);
    buf_printf(b, ",\"gm_tuz\":\"%s\",\"gm_iz\":\"%s\"},\n\"karakterler\":[", O.ayar.gm_tuz, O.ayar.gm_iz);
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        buf_puts(b, i ? ",\n{\"id\":" : "\n{\"id\":");
        buf_json_str(b, k->id);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, k->ad);
        buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s,\"gizli\":%s,\"olu\":%s", k->renk, k->npc ? "true" : "false",
                   k->gizli ? "true" : "false", k->olu ? "true" : "false");
        if (k->npc) {
            buf_puts(b, ",\"harita\":");
            buf_json_str(b, k->harita ? k->harita : "");
        }
        sayi_alani(b, "x", k->o.x);
        sayi_alani(b, "y", k->o.y);
        buf_puts(b, ",\"resim\":");
        buf_json_str(b, k->resim);
        buf_puts(b, ",\"sapka\":");
        buf_json_str(b, k->sapka);
        for (int s = 0; s < DIZI_N(STATLAR); s++) sayi_alani(b, STATLAR[s].ad, stat_oku(&k->o, &STATLAR[s]));
        buf_puts(b, ",\"envanter\":[");
        int ilk = 1;
        for (int e = 0; e < k->o.envanter_adet; e++) {
            if (k->o.envanter[e].sabit) continue; /* yumruk kurallar.c'den gelir */
            if (!ilk) buf_putc(b, ',');
            ilk = 0;
            esya_kaydi_yaz(b, &k->o.envanter[e]);
        }
        buf_puts(b, "]}");
    }
    buf_puts(b, "],\n\"oturumlar\":[");
    for (int i = 0; i < O.oturum_n; i++) {
        Oturum *o = &O.oturum[i];
        buf_printf(b, "%s{\"ozet\":\"%s\",\"gm\":%s,\"oyuncu\":\"%s\",\"son\":%lld}", i ? "," : "", o->ozet,
                   o->gm ? "true" : "false", o->oyuncu, o->son);
    }
    buf_puts(b, "],\n\"aktif_harita\":");
    if (O.aktif) buf_json_str(b, O.aktif);
    else buf_puts(b, "null");
    buf_printf(b, ",\"harita_gizli\":%s,\n\"haritalar\":[", O.gizli ? "true" : "false");
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
        buf_puts(b, i ? ",\n{\"dosya\":" : "\n{\"dosya\":");
        buf_json_str(b, h->dosya);
        buf_puts(b, ",\"izgara\":");
        izgara_json(b, &h->iz);
        buf_puts(b, ",\"konum\":[");
        for (int k = 0; k < h->konum_n; k++) {
            buf_printf(b, "%s{\"oyuncu\":\"%s\",\"x\":", k ? "," : "", h->konum[k].oyuncu);
            sayi_yaz(b, h->konum[k].x);
            sayi_alani(b, "y", h->konum[k].y);
            buf_putc(b, '}');
        }
        buf_puts(b, "],\"bilgi\":[");
        for (int k = 0; k < h->bilgi_n; k++) {
            Bilgi *bl = &h->bilgi[k];
            buf_printf(b, "%s\n{\"oyuncu\":\"%s\",\"kesif\":[", k ? "," : "", bl->oyuncu);
            for (int m = 0; m < bl->kesif_n; m++) kapsul_yaz(b, &bl->kesif[m], m > 0);
            buf_puts(b, "],\"bilinen\":[");
            for (int m = 0; m < bl->bilinen_n; m++) {
                const Bilinen *e = &bl->bilinen[m];
                buf_printf(b, "%s{\"id\":\"%s\",\"x\":", m ? "," : "", e->id);
                sayi_yaz(b, e->x);
                sayi_alani(b, "y", e->y);
                buf_printf(b, ",\"olu\":%s}", e->olu > 0 ? "true" : "false");
            }
            buf_puts(b, "]}");
        }
        buf_puts(b, "],\"yer\":[");
        for (int k = 0; k < h->yer_n; k++) {
            const YerEsyasi *ye = &h->yer[k];
            buf_printf(b, "%s{\"x\":", k ? "," : "");
            sayi_yaz(b, ye->x);
            sayi_alani(b, "y", ye->y);
            buf_puts(b, ",\"esya\":");
            esya_kaydi_yaz(b, &ye->e);
            buf_putc(b, '}');
        }
        buf_puts(b, "]}");
    }
    buf_printf(b, "],\n\"sira\":{\"aktif\":%s,\"indeks\":%d,\"tur\":%d,\"toplam_enerji\":", O.sira.aktif ? "true" : "false",
               O.sira.indeks, O.sira.tur_no);
    sayi_yaz(b, O.sira.toplam_enerji);
    buf_puts(b, ",\"liste\":[");
    for (int i = 0; i < O.sira.n; i++) buf_printf(b, "%s\"%s\"", i ? "," : "", O.sira.liste[i]);
    buf_puts(b, "]},\n\"kayit\":[");
    for (int i = 0; i < O.kayit_n; i++) {
        Kayit *k = &O.kayit[i];
        buf_printf(b, "%s\n{\"id\":%lld,\"gorunurluk\":%d,\"sahipler\":", i ? "," : "", k->id, k->gorunurluk);
        buf_json_str(b, k->sahipler ? k->sahipler : "");
        buf_printf(b, ",\"tam\":%s,\"ozet\":%s}", k->tam, k->ozet ? k->ozet : "null");
    }
    buf_puts(b, "]}\n");
}

static float json_float(const Json *o, const char *ad, float varsayilan, double lo, double hi)
{
    return (float)sinirla_d(json_sayi(o, ad, varsayilan), lo, hi);
}

/* Kayıttaki bir eşya; ağırlığı yoksa 1 */
static int esya_yukle(const Json *o, Esya *e)
{
    const char *ad = json_metin(o, "ad");
    if (!ad) return 0;
    esya_doldur(e, ad, json_metin(o, "sinif"), json_metin(o, "resim"));
    e->agirlik = json_float(o, "agirlik", 1, 0, AZAMI_STAT);
    return 1;
}

/* Eski kayıtlarda (surum < 2) GM'in elle verdiği yumruklar yerleşik yumruğa dönüşür. */
static void envanter_yukle(Karakter *k, const Json *env, int surum)
{
    Esya y;
    yumruk_kopyasi(&y);
    for (size_t i = 0; env && env->tur == JSON_ARR && i < env->adet; i++) {
        Esya e;
        if (!esya_yukle(env->ogeler[i], &e)) continue;
        if (surum < 2 && !strcmp(e.ad, y.ad) && !strcmp(e.sinif, y.sinif)) continue;
        envantere_koy(k, &e);
    }
}

/* Token resmi ya da şapka yolu: klasor/ altında geçerli bir yol, yoksa "" */
static void gorunum_yukle(char *alan, size_t n, const char *deger, const char *klasor)
{
    size_t k = strlen(klasor);
    if (deger && !strncmp(deger, klasor, k) && deger[k] == '/' && strlen(deger) < n && goreli_yol_gecerli(deger))
        kopyala(alan, n, deger);
}

static void izgara_yukle(HaritaKaydi *h, const Json *iz)
{
    h->iz.genislik = sinirla_d(json_sayi(iz, "genislik", 0), 0, AZAMI_HARITA);
    h->iz.yukseklik = sinirla_d(json_sayi(iz, "yukseklik", 0), 0, AZAMI_HARITA);
    h->iz.opaklik = sinirla_d(json_sayi(iz, "opaklik", 0.35), 0.05, 1);
    h->iz.gorunur = json_mantik(iz, "gorunur", 1);
    h->iz.acik = json_mantik(iz, "acik", 0);
    h->iz.sis = json_mantik(iz, "sis", 1);
    h->iz.oturt = json_mantik(iz, "oturt", 0);
}

static void durum_yukle(const Json *d)
{
    const Json *a = json_al(d, "ayarlar");
    const char *s;
    long long v;
    char temiz[AD_N];
    if ((s = json_metin(a, "oyun_adi")) && metin_temizle(s, temiz, sizeof temiz, 40))
        kopyala(O.ayar.oyun_adi, sizeof O.ayar.oyun_adi, temiz);
    if ((s = json_metin(a, "sifre"))) kopyala(O.ayar.sifre, sizeof O.ayar.sifre, s);
    O.ayar.tur_enerjisi = json_float(a, "tur_enerjisi", 10, 0, 100000);
    O.ayar.gorus = json_float(a, "gorus", VARSAYILAN_GORUS, 0, AZAMI_GORUS);
    int surum = json_tamsayi(d, "surum", &v) ? (int)v : 1;
    if ((s = json_metin(a, "gm_tuz")) && strlen(s) == 32) kopyala(O.ayar.gm_tuz, sizeof O.ayar.gm_tuz, s);
    if ((s = json_metin(a, "gm_iz")) && strlen(s) == 40) kopyala(O.ayar.gm_iz, sizeof O.ayar.gm_iz, s);
    if (json_tamsayi(d, "sayac", &v)) O.sayac = v;

    const Json *kl = json_al(d, "karakterler");
    for (size_t i = 0; kl && kl->tur == JSON_ARR && i < kl->adet; i++) {
        const Json *o = kl->ogeler[i];
        const char *id = json_metin(o, "id"), *ad = json_metin(o, "ad"), *renk = json_metin(o, "renk");
        int npc = json_mantik(o, "npc", 0);
        if (!id_gecerli(id) || !ad || !metin_temizle(ad, temiz, sizeof temiz, 24) || karakter_bul(id)) continue;
        if (!npc && oyuncu_sayisi() >= AZAMI_OYUNCU) continue;
        const char *hd = json_metin(o, "harita");
        if (npc && (!hd || !hd[0] || !goreli_yol_gecerli(hd))) continue;
        Karakter *k = karakter_olustur(npc, id, temiz, renk_gecerli(renk) ? renk : npc ? NPC_RENK[0] : OYUNCU_RENK[0]);
        if (!k) break;
        if (npc) k->harita = xstrdup(hd);
        k->gizli = npc && json_mantik(o, "gizli", 0);
        k->olu = json_mantik(o, "olu", 0);
        k->o.x = json_float(o, "x", 0.5f, 0, AZAMI_STAT);
        k->o.y = json_float(o, "y", 0.5f, 0, AZAMI_STAT);
        gorunum_yukle(k->resim, sizeof k->resim, json_metin(o, "resim"), "tokenlar");
        gorunum_yukle(k->sapka, sizeof k->sapka, json_metin(o, "sapka"), "sapkalar");
        for (int s = 0; s < DIZI_N(STATLAR); s++) { /* yoksa karakter_olustur'un verdiği değer kalır */
            float *v = stat_yeri(&k->o, &STATLAR[s]);
            *v = json_float(o, STATLAR[s].ad, *v, STATLAR[s].lo, STATLAR[s].hi);
        }
        envanter_yukle(k, json_al(o, "envanter"), surum);
    }

    const Json *ot = json_al(d, "oturumlar");
    for (size_t i = 0; ot && ot->tur == JSON_ARR && i < ot->adet && O.oturum_n < AZAMI_OTURUM; i++) {
        const Json *o = ot->ogeler[i];
        const char *oz = json_metin(o, "ozet"), *oyuncu = json_metin(o, "oyuncu");
        int gm = json_mantik(o, "gm", 0);
        Karakter *k = gm ? NULL : karakter_bul(oyuncu);
        if (!oz || strlen(oz) != 40 || (!gm && (!k || k->npc))) continue;
        Oturum *t = &O.oturum[O.oturum_n++];
        kopyala(t->ozet, sizeof t->ozet, oz);
        t->gm = gm;
        kopyala(t->oyuncu, sizeof t->oyuncu, gm ? "" : oyuncu);
        t->son = json_tamsayi(o, "son", &v) ? v : simdi_sn();
    }

    const Json *hl = json_al(d, "haritalar");
    for (size_t i = 0; hl && hl->tur == JSON_ARR && i < hl->adet; i++) {
        const Json *o = hl->ogeler[i];
        const char *dosya = json_metin(o, "dosya");
        if (!dosya || !dosya[0] || !goreli_yol_gecerli(dosya) || harita_bul(dosya, 0)) continue;
        HaritaKaydi *h = harita_bul(dosya, 1);
        izgara_yukle(h, json_al(o, "izgara"));
        const Json *konl = json_al(o, "konum");
        for (size_t k = 0; konl && konl->tur == JSON_ARR && k < konl->adet; k++) {
            const char *oyuncu = json_metin(konl->ogeler[k], "oyuncu");
            Karakter *kr = karakter_bul(oyuncu);
            if (!kr || kr->npc || konum_bul(h, oyuncu)) continue;
            konum_yaz(h, oyuncu, json_float(konl->ogeler[k], "x", 0, 0, AZAMI_STAT),
                      json_float(konl->ogeler[k], "y", 0, 0, AZAMI_STAT));
        }
        const Json *bil = json_al(o, "bilgi");
        for (size_t k = 0; bil && bil->tur == JSON_ARR && k < bil->adet; k++) {
            const Json *bo = bil->ogeler[k];
            const char *oyuncu = json_metin(bo, "oyuncu");
            Karakter *kr = karakter_bul(oyuncu);
            if (!kr || kr->npc || bilgi_bul(h, oyuncu, 0)) continue;
            Bilgi *bl = bilgi_bul(h, oyuncu, 1);
            const Json *ke = json_al(bo, "kesif");
            for (size_t m = 0; ke && ke->tur == JSON_ARR && m < ke->adet && bl->kesif_n < AZAMI_KESIF; m++) {
                const Json *p = ke->ogeler[m];
                if (p->tur != JSON_ARR || p->adet != 5) continue;
                double s5[5];
                int ok = 1;
                for (int q = 0; q < 5; q++) {
                    if (p->ogeler[q]->tur != JSON_NUM || !isfinite(p->ogeler[q]->sayi)) ok = 0;
                    else s5[q] = p->ogeler[q]->sayi;
                }
                if (!ok) continue;
                Kapsul kp = { (float)s5[0], (float)s5[1], (float)s5[2], (float)s5[3], (float)sinirla_d(s5[4], 0, AZAMI_GORUS) };
                kesif_ekle(bl, &kp);
            }
            const Json *bi = json_al(bo, "bilinen");
            for (size_t m = 0; bi && bi->tur == JSON_ARR && m < bi->adet; m++) {
                const Json *e = bi->ogeler[m];
                const char *hid = json_metin(e, "id");
                if (!id_gecerli(hid) || bilinen_bul(bl, hid)) continue;
                bl->bilinen = xrealloc(bl->bilinen, (size_t)(bl->bilinen_n + 1) * sizeof *bl->bilinen);
                Bilinen *be = &bl->bilinen[bl->bilinen_n++];
                kopyala(be->id, sizeof be->id, hid);
                be->x = json_float(e, "x", 0, -AZAMI_STAT, AZAMI_STAT);
                be->y = json_float(e, "y", 0, -AZAMI_STAT, AZAMI_STAT);
                be->olu = json_mantik(e, "olu", 0);
            }
        }
        const Json *yer = json_al(o, "yer");
        for (size_t k = 0; yer && yer->tur == JSON_ARR && k < yer->adet; k++) {
            const Json *yo = yer->ogeler[k];
            Esya e;
            if (!esya_yukle(json_al(yo, "esya"), &e)) continue;
            yere_koy(h, &e, json_float(yo, "x", 0, 0, AZAMI_HARITA), json_float(yo, "y", 0, 0, AZAMI_HARITA));
        }
    }
    /* Haritası olmayan NPC'ler düşer */
    for (int i = 0; i < O.kar_n;) {
        Karakter *k = O.kar[i];
        if (k->npc && !harita_bul(k->harita, 0)) {
            karakter_kaldir(k);
            continue;
        }
        i++;
    }
    if ((s = json_metin(d, "aktif_harita")) && harita_bul(s, 0)) O.aktif = xstrdup(s);
    O.gizli = json_mantik(d, "harita_gizli", 0);

    const Json *sr = json_al(d, "sira");
    const Json *liste = json_al(sr, "liste");
    for (size_t i = 0; liste && liste->tur == JSON_ARR && i < liste->adet; i++)
        if (liste->ogeler[i]->tur == JSON_STR && karakter_bul(liste->ogeler[i]->metin)) siraya_ekle(liste->ogeler[i]->metin);
    oyunculari_siraya_ekle();
    O.sira.aktif = json_mantik(sr, "aktif", 0);
    O.sira.indeks = json_tamsayi(sr, "indeks", &v) ? sinirla(v, -1, O.sira.n - 1) : -1;
    O.sira.tur_no = json_tamsayi(sr, "tur", &v) ? sinirla(v, 1, 1000000) : 1;
    O.sira.toplam_enerji = json_float(sr, "toplam_enerji", 0, 0, AZAMI_STAT);
    if (O.sira.aktif && O.sira.indeks < 0) O.sira.aktif = 0;

    const Json *kayl = json_al(d, "kayit");
    for (size_t i = 0; kayl && kayl->tur == JSON_ARR && i < kayl->adet && O.kayit_n < AZAMI_KAYIT; i++) {
        const Json *k = kayl->ogeler[i];
        const Json *tam = json_al(k, "tam"), *oz = json_al(k, "ozet");
        if (!tam || tam->tur != JSON_OBJ) continue;
        Kayit *r = &O.kayit[O.kayit_n++];
        memset(r, 0, sizeof *r);
        r->id = json_tamsayi(k, "id", &v) ? v : 0;
        r->gorunurluk = json_tamsayi(k, "gorunurluk", &v) ? sinirla(v, 0, 2) : GOR_GM;
        s = json_metin(k, "sahipler");
        if (s && s[0] == ',' && strlen(s) < 4096) r->sahipler = xstrdup(s);
        Buf b;
        buf_init(&b);
        json_yaz(&b, tam);
        r->tam = buf_take(&b);
        if (oz && oz->tur == JSON_OBJ) {
            json_yaz(&b, oz);
            r->ozet = buf_take(&b);
        }
        buf_free(&b);
    }
}

/* GM şifresinin tuzlu, yinelenmiş özeti. Şifrenin kendisi diske yazılmaz. */
static void gm_sifre_izi(const char *tuz, const char *sifre, char out[41])
{
    static const char hex[] = "0123456789abcdef";
    unsigned char d[20];
    Buf b;
    buf_init(&b);
    buf_printf(&b, "roleplay-gm:%s:%s", tuz, sifre);
    sha1(b.p, b.len, d);
    buf_free(&b);
    unsigned char t[20 + 32];
    size_t tn = strlen(tuz);
    if (tn > 32) tn = 32;
    for (int i = 0; i < 20000; i++) {
        memcpy(t, d, 20);
        memcpy(t + 20, tuz, tn);
        sha1(t, 20 + tn, d);
    }
    for (int i = 0; i < 20; i++) {
        out[2 * i] = hex[d[i] >> 4];
        out[2 * i + 1] = hex[d[i] & 15];
    }
    out[40] = '\0';
}

static void sinif_klasorlerini_ac(void)
{
    for (int i = 0; esya_siniflari[i] && i < 200; i++) {
        const char *s = esya_siniflari[i];
        if (!dosya_adi_gecerli(s) || strlen(s) >= sizeof(((Esya *)0)->sinif)) {
            fprintf(stderr, "kurallar.c: \"%s\" eşya sınıfı klasör adı olamaz (/ içeremez, nokta ile başlayamaz, en fazla 31 bayt); atlandı.\n", s);
            continue;
        }
        char yol[YOL_AZAMI];
        snprintf(yol, sizeof yol, "%s/esyalar/%s", yap.oyun_klasoru, s);
        mkdir(yol, 0775);
    }
}

/* sapkalar/ klasörü ilk kez açılırken içine konan şapkalar (GM silebilir, değiştirebilir, yenilerini ekleyebilir) */
static const struct {
    const char *ad, *svg;
} VARSAYILAN_SAPKALAR[] = {
    { "Sihirbaz.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 80\"><path d=\"M50 3 77 63H23Z\" fill=\"#5b3a9e\" stroke=\"#21123f\" stroke-width=\"3\" stroke-linejoin=\"round\"/><ellipse cx=\"50\" cy=\"66\" rx=\"46\" ry=\"10\" fill=\"#4a2d85\" stroke=\"#21123f\" stroke-width=\"3\"/><path d=\"M48 28l2.4 5 5.4.8-3.9 3.8.9 5.4-4.8-2.6-4.8 2.6.9-5.4-3.9-3.8 5.4-.8z\" fill=\"#f4d35e\"/><circle cx=\"59\" cy=\"50\" r=\"3\" fill=\"#f4d35e\"/><circle cx=\"39\" cy=\"53\" r=\"2\" fill=\"#f4d35e\"/></svg>" },
    { "Taç.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 62\"><path d=\"M10 52 6 14l24 20L50 6l20 28 24-20-4 38z\" fill=\"#e8b923\" stroke=\"#6b4a00\" stroke-width=\"3\" stroke-linejoin=\"round\"/><rect x=\"9\" y=\"46\" width=\"82\" height=\"12\" rx=\"3\" fill=\"#d39e12\" stroke=\"#6b4a00\" stroke-width=\"3\"/><circle cx=\"50\" cy=\"30\" r=\"5\" fill=\"#d7263d\"/><circle cx=\"28\" cy=\"40\" r=\"4\" fill=\"#1b998b\"/><circle cx=\"72\" cy=\"40\" r=\"4\" fill=\"#1b998b\"/></svg>" },
    { "Silindir.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 78\"><rect x=\"26\" y=\"3\" width=\"48\" height=\"60\" rx=\"4\" fill=\"#262626\" stroke=\"#000\" stroke-width=\"3\"/><rect x=\"27.5\" y=\"46\" width=\"45\" height=\"10\" fill=\"#b22234\"/><ellipse cx=\"50\" cy=\"66\" rx=\"46\" ry=\"9\" fill=\"#2e2e2e\" stroke=\"#000\" stroke-width=\"3\"/></svg>" },
    { "Kovboy.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 62\"><path d=\"M4 40q46 26 92 0-6 16-46 17Q10 56 4 40z\" fill=\"#8b5a2b\" stroke=\"#3e2410\" stroke-width=\"3\" stroke-linejoin=\"round\"/><path d=\"M26 45q-2-35 14-37 10 10 20 0 16 2 14 37-24 9-48 0z\" fill=\"#a0692f\" stroke=\"#3e2410\" stroke-width=\"3\" stroke-linejoin=\"round\"/><path d=\"M27 38q23 9 46 0\" fill=\"none\" stroke=\"#3e2410\" stroke-width=\"5\"/></svg>" },
    { "Miğfer.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 72\"><path d=\"M42 16q8-20 16 0\" fill=\"#c0392b\" stroke=\"#5c1a12\" stroke-width=\"3\"/><path d=\"M14 62q0-48 36-50 36 2 36 50z\" fill=\"#9aa5b1\" stroke=\"#2f3a45\" stroke-width=\"3\" stroke-linejoin=\"round\"/><path d=\"M50 13v49\" stroke=\"#2f3a45\" stroke-width=\"3\"/><rect x=\"10\" y=\"58\" width=\"80\" height=\"9\" rx=\"3\" fill=\"#7d8894\" stroke=\"#2f3a45\" stroke-width=\"3\"/></svg>" },
    { "Korsan.svg",
      "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 62\"><path d=\"M4 46q16-38 46-36 30-2 46 36-46-12-92 0z\" fill=\"#1d1d1d\" stroke=\"#000\" stroke-width=\"3\" stroke-linejoin=\"round\"/><path d=\"M4 46q46 14 92 0\" fill=\"none\" stroke=\"#c9a24a\" stroke-width=\"4\"/><circle cx=\"50\" cy=\"26\" r=\"7\" fill=\"#eee\"/><rect x=\"46\" y=\"30\" width=\"8\" height=\"5\" fill=\"#eee\"/><circle cx=\"47.5\" cy=\"25\" r=\"1.8\" fill=\"#111\"/><circle cx=\"52.5\" cy=\"25\" r=\"1.8\" fill=\"#111\"/></svg>" },
};

static void varsayilan_sapkalari_yaz(const char *klasor)
{
    for (int i = 0; i < DIZI_N(VARSAYILAN_SAPKALAR); i++) {
        char yol[YOL_AZAMI + 64];
        snprintf(yol, sizeof yol, "%s/%s", klasor, VARSAYILAN_SAPKALAR[i].ad);
        FILE *f = fopen(yol, "wb");
        if (!f) continue;
        fputs(VARSAYILAN_SAPKALAR[i].svg, f);
        fclose(f);
    }
}

void oyun_baslat(void)
{
    char yol[YOL_AZAMI];
    mkdir(yap.oyun_klasoru, 0775);
    snprintf(yol, sizeof yol, "%s/haritalar", yap.oyun_klasoru);
    mkdir(yol, 0775);
    snprintf(yol, sizeof yol, "%s/esyalar", yap.oyun_klasoru);
    mkdir(yol, 0775);
    snprintf(yol, sizeof yol, "%s/tokenlar", yap.oyun_klasoru);
    mkdir(yol, 0775);
    snprintf(yol, sizeof yol, "%s/sapkalar", yap.oyun_klasoru);
    if (mkdir(yol, 0775) == 0) varsayilan_sapkalari_yaz(yol);
    sinif_klasorlerini_ac();

    memset(&O, 0, sizeof O);
    kopyala(O.ayar.oyun_adi, sizeof O.ayar.oyun_adi, "Roleplay");
    O.ayar.tur_enerjisi = 10;
    O.ayar.gorus = VARSAYILAN_GORUS;
    O.sira.indeks = -1;
    O.sira.tur_no = 1;

    snprintf(yol, sizeof yol, "%s/durum.json", yap.oyun_klasoru);
    FILE *f = fopen(yol, "rb");
    if (f) {
        Buf b;
        buf_init(&b);
        char tmp[65536];
        size_t k;
        while ((k = fread(tmp, 1, sizeof tmp, f)) > 0) buf_add(&b, tmp, k);
        fclose(f);
        Json *d = b.len ? json_ayristir(b.p, b.len) : NULL;
        if (d && d->tur == JSON_OBJ) {
            durum_yukle(d);
        } else { /* üzerine yazılmasın: elle düzeltilebilir */
            char yedek[YOL_AZAMI + 64];
            snprintf(yedek, sizeof yedek, "%s/durum-bozuk-%lld.json", yap.oyun_klasoru, simdi_sn());
            rename(yol, yedek);
            fprintf(stderr, "durum.json okunamadı; %s olarak saklandı, yeni bir oyunla başlanıyor.\n", yedek);
        }
        json_sil(d);
        buf_free(&b);
    }
    /* GM şifresi .env'de değiştiyse eski GM oturumları geçersiz */
    char iz[41];
    if (strlen(O.ayar.gm_tuz) != 32) rastgele_hex(O.ayar.gm_tuz, 16);
    gm_sifre_izi(O.ayar.gm_tuz, yap.gm_sifresi, iz);
    if (strcmp(iz, O.ayar.gm_iz)) {
        if (O.ayar.gm_iz[0]) fprintf(stderr, "GM şifresi değişmiş: açık GM oturumları kapatıldı.\n");
        oturumlari_sil(1, NULL, NULL);
        kopyala(O.ayar.gm_iz, sizeof O.ayar.gm_iz, iz);
    }
    if (!O.ayar.sifre[0] && yap.ilk_sifre[0]) {
        char s[AD_N];
        if (metin_temizle(yap.ilk_sifre, s, sizeof s, 64)) kopyala(O.ayar.sifre, sizeof O.ayar.sifre, s);
    }
    if (O.ayar.sifre[0] && !strcmp(O.ayar.sifre, yap.gm_sifresi))
        fprintf(stderr, "UYARI: oyun şifresi GM şifresiyle aynı. GM ekranındaki Ayarlar'dan oyun şifresini değiştir.\n");
    if (O.sira.aktif) kayit_metin("sira", GOR_GM, NULL, "Sunucu yeniden başladı; tur sırası kaldığı yerden sürüyor.");
    konumlari_yukle();
    bilgileri_guncelle();
    O.kirli = 1;
}

void oyun_kaydet(int zorla)
{
    pthread_mutex_lock(&oyun_kilit);
    if (!O.kirli && !zorla) {
        pthread_mutex_unlock(&oyun_kilit);
        return;
    }
    Buf b;
    buf_init(&b);
    durum_json_yaz(&b);
    O.kirli = 0;
    pthread_mutex_unlock(&oyun_kilit);

    pthread_mutex_lock(&dosya_yazma_kilit);
    char yol[YOL_AZAMI], gecici[YOL_AZAMI + 16];
    snprintf(yol, sizeof yol, "%s/durum.json", yap.oyun_klasoru);
    snprintf(gecici, sizeof gecici, "%s/.durum.json.yeni", yap.oyun_klasoru);
    int fd = open(gecici, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0664);
    int ok = fd >= 0;
    if (ok) {
        size_t yazildi = 0;
        while (ok && yazildi < b.len) {
            ssize_t w = write(fd, b.p + yazildi, b.len - yazildi);
            if (w < 0 && errno == EINTR) continue;
            if (w <= 0) ok = 0;
            else yazildi += (size_t)w;
        }
        if (fsync(fd) != 0) ok = 0;
        close(fd);
    }
    if (ok && rename(gecici, yol) != 0) ok = 0;
    if (!ok) {
        fprintf(stderr, "durum.json yazılamadı: %s\n", strerror(errno));
        unlink(gecici);
        pthread_mutex_lock(&oyun_kilit);
        O.kirli = 1;
        pthread_mutex_unlock(&oyun_kilit);
    }
    pthread_mutex_unlock(&dosya_yazma_kilit);
    buf_free(&b);
}

void gm_bilgisini_yazdir(void)
{
    pthread_mutex_lock(&oyun_kilit);
    if (yap.alan_adi[0]) printf("GM ekranı:      https://%s/gm   (şifre: .env dosyasındaki GM_SIFRESI)\n", yap.alan_adi);
    else printf("GM ekranı:      <sitenin adresi>/gm   (ör. %s/gm, şifre: .env dosyasındaki GM_SIFRESI)\n", yap.dinle);
    if (!O.ayar.sifre[0]) printf("Oyun şifresi henüz yok: GM ekranında Ayarlar bölümünden belirle.\n");
    pthread_mutex_unlock(&oyun_kilit);
    fflush(stdout);
}
