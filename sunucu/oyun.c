#include "oyun.h"
#include "duvar.h"
#include "geometri.h"
#include "gorsel.h"
#include "json.h"
#include "kesif.h"
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
float yol_uzunlugu;

#define ID_N 17
#define AD_N 100
#define AZAMI_OYUNCU 64
#define AZAMI_KARAKTER 1024
#define AZAMI_NPC 300 /* harita başına */
#define AZAMI_OTURUM 512
#define AZAMI_KAYIT 200
#define AZAMI_SIRA 128
#define AZAMI_YER_ESYASI 500 /* harita başına yerdeki eşya */
#define AZAMI_PORTAL 100     /* harita başına portal */
#define AZAMI_DUVAR 2000     /* harita başına duvar */
#define AZAMI_YOL 256        /* bir yürüyüşün en fazla köşe sayısı */
#define AZAMI_PARCA 64       /* başkasının yürüyüşünün görülen en fazla parçası */
#define AZAMI_BILINEN_ESYA 1000
#define AZAMI_TAKAS 64
#define TAKAS_YUVA 4         /* takasta bir tarafın koyabileceği eşya çeşidi (para dahil) */
#define AZAMI_PARA 2000000000 /* her sikke türünden */
#define AZAMI_IZIN 512
#define AZAMI_STAT 1000000.0
#define AZAMI_GORUS 1000.0
#define VARSAYILAN_GORUS 6.0f
#define VARSAYILAN_CAN 10.0f
#define VARSAYILAN_DAYANIKLILIK 100.0f
#define ALMA_MESAFESI 0.5    /* yürüyüşü yerdeki eşyanın bu kadar kare yakınından geçen onu alır */
#define PORTAL_MESAFESI 0.5  /* yürüyüşü portala bu kadar kare yakın biten portaldan geçer */
#define YAGMA_MESAFESI 1.6   /* ölünün üstünü arayan ona en fazla bu kadar kare uzakta olmalı */
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
    char *harita; /* bulunduğu harita (haritalar/ altında göreli yol); NULL: hiçbir haritada değil. Oyuncuları GM
                     yerleştirir; her oyuncu ayrı bir haritada olabilir. */
    char resim[256]; /* tokenın resmi: "tokenlar/..." ya da "" */
    char sapka[256]; /* tokenın şapkası: "sapkalar/..." ya da "" */
    Oyuncu o;
} Karakter;

typedef struct {
    char id[ID_N];
    float x, y;
    int olu;
} Bilinen; /* bir oyuncunun başka bir karakteri en son nerede gördüğü */

typedef struct {
    long long id;
    float x, y;
    Esya e;
} BilinenEsya; /* bir oyuncunun yerde görüp hatırladığı eşya (o sırada alınmış olabilir) */

typedef struct {
    char oyuncu[ID_N];
    Kesif kesif; /* keşfedilen alan: görüşün değdiği yerler (duvarların arkası hariç) */
    long long kesif_surum;
    long long *duvar; /* gördüğü duvarların kimlikleri, sıralı: tarayıcısı yalnızca bunları bilir */
    int duvar_n, duvar_kap;
    double son_x, son_y, son_r; /* en son durduğu yerden taranan görüş: değişmediyse yeniden taranmaz */
    long long son_surum;
    Bilinen *bilinen;
    int bilinen_n;
    BilinenEsya *esya;
    int esya_n;
} Bilgi; /* bir oyuncunun bir haritadaki hafızası */

typedef struct {
    long long id;
    Esya e;
    float x, y;
} YerEsyasi; /* haritada yerde duran eşya */

typedef struct {
    long long id;
    char ad[64];
    float x, y;
    char *hedef;  /* hedef harita (haritalar/ altında göreli yol); NULL: bir yere bağlı değil */
    float hx, hy; /* hedef haritada varılan yer */
} Portal; /* GM'in koyduğu geçit: içine yürüyen oyuncu hedef haritaya geçer */

typedef struct {
    long long id;
    int x1, y1, x2, y2; /* binde bir kare cinsinden: tarayıcı da aynı sayılarla hesaplar */
} Duvar; /* GM'in çizdiği duvar: görüşü keser, üstünden geçilmez */

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
    int gizli; /* oyunculardan gizli (GM hazırlıyor) */
    Bilgi *bilgi;
    int bilgi_n;
    YerEsyasi *yer;
    int yer_n;
    Portal *portal;
    int portal_n;
    Duvar *duvar;
    int duvar_n;
    long long duvar_surum; /* duvarlar ya da harita boyutu değişince artar (kaydedilmez) */
    Duvarlar *dizin;       /* duvarların geometri önbelleği (kaydedilmez) */
    long long dizin_surum;
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
    float yol[AZAMI_YOL][2]; /* hareket: duvarların etrafından dolaşan yol (GM görür; onaylanınca yeniden aranır) */
    int yol_n;
    float yol_uzunlugu;
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
} ResimIzni;

typedef struct {
    int tur;     /* 0: boş, 1: eşya, 2: para */
    Esya e;      /* eşyanın türü: bu türden adet tanesi verilir */
    int adet;
    int para[3]; /* altın, gümüş, bakır */
} TakasYuvasi;

typedef struct {
    long long id;
    char kim[2][ID_N]; /* [0] başlatan, [1] karşı taraf */
    TakasYuvasi yuva[2][TAKAS_YUVA];
    int onay[2];
} Takas; /* iki karakter arasında süren takas (kaydedilmez) */ /* saldırı animasyonunda başkasının eşya resmini görebilsin diye kısa süreli izin */

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

/* Para: üç ayrı sikke; birbirine çevrilmez */
static const char *const SIKKE[3] = { "altin", "gumus", "bakir" };
static const char *const SIKKE_ADI[3] = { "altın", "gümüş", "bakır" };

static int *sikke(Oyuncu *o, int i)
{
    return i == 0 ? &o->altin : i == 1 ? &o->gumus : &o->bakir;
}

static int sikke_oku(const Oyuncu *o, int i)
{
    return i == 0 ? o->altin : i == 1 ? o->gumus : o->bakir;
}

/* ,"para":{"altin":1,"gumus":2,"bakir":3} */
static void para_alani(Buf *b, const int p[3])
{
    buf_printf(b, ",\"para\":{\"altin\":%d,\"gumus\":%d,\"bakir\":%d}", p[0], p[1], p[2]);
}

static void oyuncu_parasi(const Oyuncu *o, int p[3])
{
    for (int i = 0; i < 3; i++) p[i] = sikke_oku(o, i);
}

static struct {
    Ayarlar ayar;
    Karakter **kar;
    int kar_n;
    HaritaKaydi *harita;
    int harita_n;
    char *aktif; /* GM'in baktığı harita, yoksa NULL. Oyuncular kendi haritalarındadır; GM'in bakışı onları taşımaz. */
    Sira sira;
    Istek istek; /* bekleyen tek istek: yalnızca sırası gelen karakter istekte bulunabilir */
    int istek_var;
    Takas takas[AZAMI_TAKAS];
    int takas_n;
    long long takas_sayac;
    long long istek_sayac;
    Oturum oturum[AZAMI_OTURUM];
    int oturum_n;
    Kayit kayit[AZAMI_KAYIT];
    int kayit_n;
    ResimIzni izin[AZAMI_IZIN];
    int izin_n;
    long long sayac;
    long long yer_sayac; /* yerdeki eşyaların ve portalların kimlikleri */
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
static void yerden_al(Karakter *k, const Yol *yol);
static Duvarlar *duvar_dizini(HaritaKaydi *h);
static void istek_yolunu_yenile(HaritaKaydi *h);
static void takaslari_bitir(const char *kim, const char *neden);
static void takaslar_yaz(Buf *b, const Karakter *ben);

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

/* Bir haritada mı (oyuncuları GM yerleştirir; NPC'ler hep bir haritadadır) */
static int haritada(const Karakter *k)
{
    return k->harita != NULL;
}

/* Verilen haritada mı */
static int bu_haritada(const Karakter *k, const char *dosya)
{
    return k->harita && dosya && !strcmp(k->harita, dosya);
}

/* İkisi aynı haritada mı */
static int ayni_haritada(const Karakter *a, const Karakter *b)
{
    return a->harita && bu_haritada(b, a->harita);
}

/* GM'in baktığı haritada mı */
static int gm_haritasinda(const Karakter *k)
{
    return bu_haritada(k, O.aktif);
}

/* Herkesin görüş menzili (kare): GM Ayarlar'dan belirler. */
static float gorus_menzili(void)
{
    return O.ayar.gorus;
}

/* GM'in baktığı haritadaki NPC'ler */
static int haritadaki_npc_sayisi(void)
{
    int n = 0;
    for (int i = 0; i < O.kar_n; i++) n += O.kar[i]->npc && gm_haritasinda(O.kar[i]);
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
    takaslari_bitir(id, "Takas bitti: karşı taraf oyundan çıktı.");
    if (!k->npc) bilgileri_sil(id, NULL);
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
    e->dayaniklilik = VARSAYILAN_DAYANIKLILIK;
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
    e->dayaniklilik = (float)sinirla_d(yumruk.dayaniklilik, -AZAMI_STAT, AZAMI_STAT);
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
        y.dayaniklilik = o->envanter[0].dayaniklilik; /* yumruğun dayanıklılığını kurallar yönetir */
        o->envanter[0] = y;
        kirlet();
    }
}

/* Ağırlık: KARAKTER_AGIRLIGI ve eşyalarının ağırlıkları */
static void agirlik_hesapla(Oyuncu *o)
{
    double t = KARAKTER_AGIRLIGI;
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
        e->dayaniklilik = (float)sinirla_d(e->dayaniklilik, -AZAMI_STAT, AZAMI_STAT);
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

/* GM'in baktığı haritanın kaydı (harita açılırken oluşturulur). */
static HaritaKaydi *aktif_harita(void)
{
    return O.aktif ? harita_bul(O.aktif, 0) : NULL;
}

/* Karakterin bulunduğu haritanın kaydı */
static HaritaKaydi *haritasi(const Karakter *k)
{
    return k->harita ? harita_bul(k->harita, 0) : NULL;
}

/* Karakteri bir haritaya koyar (NULL: hiçbir haritada değil). */
static void harita_ata(Karakter *k, const char *dosya)
{
    if (dosya && k->harita && !strcmp(k->harita, dosya)) return;
    char *yeni = dosya ? xstrdup(dosya) : NULL; /* dosya k->harita'nın kendisi olabilir */
    free(k->harita);
    k->harita = yeni;
    kirlet();
}

/* GM'in baktığı haritayı değiştirir (NULL: harita yok). */
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

/* Ortadan başlayıp dışa doğru boş bir kare ortası arar. O haritadaki karakterlere 0.75 kareden fazla yaklaşmaz;
   duvarların dibine (kare ortalarından çapraz duvarlar geçebilir) düşmez. */
static void bos_yer(HaritaKaydi *h, float g, float y, float *ox, float *oy)
{
    int kg = (int)g, ky = (int)y;
    if (kg < 1) kg = 1;
    if (ky < 1) ky = 1;
    int cx = kg / 2, cy = ky / 2;
    int azami = kg > ky ? kg : ky;
    Duvarlar *d = duvar_dizini(h);
    for (int r = 0; r <= azami; r++) {
        for (int dy = -r; dy <= r; dy++) {
            for (int dx = -r; dx <= r; dx++) {
                if (abs(dx) != r && abs(dy) != r) continue;
                int nx = cx + dx, ny = cy + dy;
                if (nx < 0 || ny < 0 || nx >= kg || ny >= ky) continue;
                double px = nx + 0.5, py = ny + 0.5;
                int dolu = d && duvar_yakinlik(d, px, py) < 0.3;
                for (int i = 0; i < O.kar_n && !dolu; i++) {
                    Karakter *k = O.kar[i];
                    if (bu_haritada(k, h->dosya) && mesafe(px, py, k->o.x, k->o.y) < 0.75) dolu = 1;
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

/* Haritadaki herkes harita sınırları içinde kalsın (harita boyutu değişince). */
static void sinirlara_al(HaritaKaydi *h)
{
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) return;
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!bu_haritada(k, h->dosya)) continue;
        float nx = (float)sinirla_d(k->o.x, 0, g), ny = (float)sinirla_d(k->o.y, 0, y);
        if (nx != k->o.x || ny != k->o.y) {
            k->o.x = nx;
            k->o.y = ny;
            kirlet();
        }
    }
}

/* ================================================================== duvarlar */

/* Haritanın duvarlarının geometri önbelleği: duvarlar ya da harita boyutu değişince yeniden kurulur. Duvar yoksa ya da
   haritanın boyutu bilinmiyorsa NULL (o zaman her yer görülür ve düz yürünür). */
static Duvarlar *duvar_dizini(HaritaKaydi *h)
{
    float g, y;
    if (!h || !h->duvar_n || !harita_olcu(h, &g, &y)) return NULL;
    if (h->dizin && h->dizin_surum == h->duvar_surum) return h->dizin;
    duvarlar_sil(h->dizin);
    double *p = xmalloc((size_t)h->duvar_n * 4 * sizeof *p);
    for (int i = 0; i < h->duvar_n; i++) {
        const Duvar *w = &h->duvar[i];
        p[4 * i] = w->x1 / 1000.0;
        p[4 * i + 1] = w->y1 / 1000.0;
        p[4 * i + 2] = w->x2 / 1000.0;
        p[4 * i + 3] = w->y2 / 1000.0;
    }
    h->dizin = duvarlar_kur(p, h->duvar_n, g, y);
    h->dizin_surum = h->duvar_surum;
    free(p);
    return h->dizin;
}

/* Yürümeden bir yere konan karakter duvarın üstünde ya da dibinde durmasın: oradan iki yana da çıkabilirdi */
static void duvardan_ayir(HaritaKaydi *h, double *x, double *y)
{
    Duvarlar *d = duvar_dizini(h);
    if (d) duvar_uzaklastir(d, x, y);
}

/* Duvarlar ya da haritanın boyutu değişti: geometri yeniden kurulsun, bekleyen yürüyüşün yolu yeniden bulunsun. Yeni
   duvar bir karakterin tam üstünden geçiyorsa karakter onun bir yanında kalır. */
static void duvarlar_degisti(HaritaKaydi *h)
{
    h->duvar_surum = ++surum_sayaci;
    kirlet();
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!bu_haritada(k, h->dosya)) continue;
        double x = k->o.x, y = k->o.y;
        duvardan_ayir(h, &x, &y);
        k->o.x = (float)x;
        k->o.y = (float)y;
    }
    istek_yolunu_yenile(h);
}

/* A ile B arasında görüşü kesen duvar yok mu */
static int gorus_acik(HaritaKaydi *h, double ax, double ay, double bx, double by)
{
    Duvarlar *d = duvar_dizini(h);
    return !d || duvar_gorus_acik(d, ax, ay, bx, by);
}

/* (vx,vy)'deki oyuncu (x,y)'yi görebilir mi: sis kapalıysa her yeri; açıksa menzili içinde, arada duvar yoksa */
static int gorulur(HaritaKaydi *h, double vx, double vy, double x, double y)
{
    if (!h->iz.sis) return 1;
    return mesafe(vx, vy, x, y) <= gorus_menzili() + 1e-4 && gorus_acik(h, vx, vy, x, y);
}

/* A'dan B'ye duvarların etrafından dolaşan en kısa yol (duvar yoksa düz). Yol yoksa 0. */
static int yol_bul(HaritaKaydi *h, double ax, double ay, double bx, double by, Yol *y)
{
    Duvarlar *d = duvar_dizini(h);
    if (!d) {
        yol_duz(y, ax, ay, bx, by);
        return 1;
    }
    return duvar_yol_bul(d, ax, ay, bx, by, y);
}

/* Harita boyutu değişti: duvarlar resme göre yerinde kalsın */
static void duvarlari_olcekle(HaritaKaydi *h, double kx, double ky, float g, float y)
{
    for (int i = 0; i < h->duvar_n; i++) {
        Duvar *w = &h->duvar[i];
        w->x1 = (int)lround(sinirla_d(w->x1 * kx, 0, g * 1000.0));
        w->y1 = (int)lround(sinirla_d(w->y1 * ky, 0, y * 1000.0));
        w->x2 = (int)lround(sinirla_d(w->x2 * kx, 0, g * 1000.0));
        w->y2 = (int)lround(sinirla_d(w->y2 * ky, 0, y * 1000.0));
    }
}

static void duvar_yaz(Buf *b, const Duvar *w, int ilk)
{
    buf_printf(b, "%s[%lld,%d,%d,%d,%d]", ilk ? "" : ",", w->id, w->x1, w->y1, w->x2, w->y2);
}

static void bilgi_temizle(Bilgi *b)
{
    kesif_bosalt(&b->kesif);
    free(b->duvar);
    free(b->bilinen);
    free(b->esya);
}

static void portallari_temizle(HaritaKaydi *h)
{
    for (int i = 0; i < h->portal_n; i++) free(h->portal[i].hedef);
    free(h->portal);
    h->portal = NULL;
    h->portal_n = 0;
}

/* Haritanın kaydını ve üstündeki NPC'leri siler; oradaki oyuncular hiçbir haritada kalmaz. */
static void harita_sil(HaritaKaydi *h)
{
    char *dosya = h->dosya;
    for (int i = 0; i < O.kar_n;) {
        Karakter *k = O.kar[i];
        if (bu_haritada(k, dosya)) {
            if (k->npc) {
                karakter_kaldir(k);
                continue;
            }
            harita_ata(k, NULL);
        }
        i++;
    }
    /* Başka haritalardaki portallar buraya çıkıyorsa bağlantısız kalır */
    for (int i = 0; i < O.harita_n; i++)
        for (int j = 0; j < O.harita[i].portal_n; j++) {
            Portal *p = &O.harita[i].portal[j];
            if (p->hedef && !strcmp(p->hedef, dosya)) {
                free(p->hedef);
                p->hedef = NULL;
            }
        }
    /* karakter_kaldir O.harita dizisini büyütmez; h hâlâ geçerli */
    for (int i = 0; i < h->bilgi_n; i++) bilgi_temizle(&h->bilgi[i]);
    free(h->bilgi);
    free(h->yer);
    free(h->duvar);
    duvarlar_sil(h->dizin);
    portallari_temizle(h);
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

static int sis_acik(const HaritaKaydi *h)
{
    return h && h->iz.sis;
}

/* v oyuncusu t karakterini şu an görüyor mu? Aynı haritada olmalılar; gizli haritada kimse kimseyi göremez. Sisli
   haritada görüş menziliyle ve duvarlarla sınırlıdır. */
static int goruyor(const Karakter *v, const Karakter *t)
{
    if (v == t) return 1;
    if (t->gizli || !ayni_haritada(v, t)) return 0;
    HaritaKaydi *h = haritasi(t);
    if (!h || h->gizli) return 0;
    return gorulur(h, v->o.x, v->o.y, t->o.x, t->o.y);
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
    b->son_surum = -1;
    kirlet();
    return b;
}

/* Oyuncu bu duvarı gördü: tarayıcısı artık onu bilir (görüşünü ve yollarını onunla hesaplar) */
static void duvar_ogren(Bilgi *b, long long id)
{
    int lo = 0, hi = b->duvar_n;
    while (lo < hi) {
        int m = (lo + hi) / 2;
        if (b->duvar[m] < id) lo = m + 1;
        else hi = m;
    }
    if (lo < b->duvar_n && b->duvar[lo] == id) return;
    if (b->duvar_n == b->duvar_kap) {
        b->duvar_kap = b->duvar_kap ? b->duvar_kap * 2 : 16;
        b->duvar = xrealloc(b->duvar, (size_t)b->duvar_kap * sizeof *b->duvar);
    }
    memmove(&b->duvar[lo + 1], &b->duvar[lo], (size_t)(b->duvar_n - lo) * sizeof *b->duvar);
    b->duvar[lo] = id;
    b->duvar_n++;
    b->kesif_surum = ++surum_sayaci;
    kirlet();
}

static int duvari_biliyor(const Bilgi *b, long long id)
{
    int lo = 0, hi = b->duvar_n - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        if (b->duvar[m] == id) return 1;
        if (b->duvar[m] < id) lo = m + 1;
        else hi = m - 1;
    }
    return 0;
}

/* (x,y)'den r yarıçapla görülen yeri (duvarların arkası hariç) keşfedilmiş say, görülen duvarları öğren. */
static void kesfet(Bilgi *b, HaritaKaydi *h, double x, double y, double r)
{
    float g, yk;
    if (!(r > 0) || !harita_olcu(h, &g, &yk)) return;
    if (!kesif_uygun(&b->kesif, g, yk)) {
        kesif_kur(&b->kesif, g, yk);
        b->kesif_surum = ++surum_sayaci;
    }
    Duvarlar *d = duvar_dizini(h);
    unsigned char *gorulen = d ? xcalloc((size_t)h->duvar_n, 1) : NULL;
    double *xy;
    int n = duvar_gorus_cokgeni(d, x, y, r, &xy, gorulen);
    if (kesif_cokgen(&b->kesif, xy, n)) {
        b->kesif_surum = ++surum_sayaci;
        kirlet();
    }
    for (int i = 0; d && i < h->duvar_n; i++)
        if (gorulen[i]) duvar_ogren(b, h->duvar[i].id);
    free(xy);
    free(gorulen);
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

static int yer_esyasi_sira(const HaritaKaydi *h, long long id);

static BilinenEsya *bilinen_esya_bul(Bilgi *b, long long id)
{
    for (int i = 0; i < b->esya_n; i++)
        if (b->esya[i].id == id) return &b->esya[i];
    return NULL;
}

static void bilinen_esya_yaz(Bilgi *b, const YerEsyasi *ye)
{
    BilinenEsya *e = bilinen_esya_bul(b, ye->id);
    if (!e) {
        if (b->esya_n >= AZAMI_BILINEN_ESYA) {
            memmove(&b->esya[0], &b->esya[1], (size_t)(b->esya_n - 1) * sizeof *b->esya);
            b->esya_n--;
        }
        b->esya = xrealloc(b->esya, (size_t)(b->esya_n + 1) * sizeof *b->esya);
        e = &b->esya[b->esya_n++];
        memset(e, 0, sizeof *e);
        e->id = ye->id;
        kirlet();
    }
    if (e->x != ye->x || e->y != ye->y || memcmp(&e->e, &ye->e, sizeof e->e)) {
        e->x = ye->x;
        e->y = ye->y;
        e->e = ye->e;
        kirlet();
    }
}

/* v (x,y)'den r yarıçapla baktı: gördüğü yerdeki eşyalar hafızaya yazılır, gördüğü yerde olup artık orada olmayanların
   hatırası silinir. Görmediği yerlerdeki (menzil dışı, duvar arkası) hatıralar eski haliyle kalır: biri eşyayı alsa da
   oyuncu oraya tekrar bakana kadar eşyayı orada sanır. */
static void esya_hafizasi_tara(Bilgi *b, HaritaKaydi *h, double x, double y, double r)
{
    for (int i = 0; i < h->yer_n; i++)
        if (mesafe(h->yer[i].x, h->yer[i].y, x, y) <= r && gorus_acik(h, x, y, h->yer[i].x, h->yer[i].y))
            bilinen_esya_yaz(b, &h->yer[i]);
    for (int i = b->esya_n - 1; i >= 0; i--) {
        BilinenEsya *e = &b->esya[i];
        if (mesafe(e->x, e->y, x, y) > r || !gorus_acik(h, x, y, e->x, e->y)) continue;
        int sira = yer_esyasi_sira(h, e->id);
        if (sira >= 0 && h->yer[sira].x == e->x && h->yer[sira].y == e->y) continue;
        b->esya[i] = b->esya[--b->esya_n];
        kirlet();
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

/* Her oyuncunun bulunduğu haritadaki hafızasını şimdiki görüşüyle güncelle:
   - görüş alanındaki karakterlerin ve yerdeki eşyaların yeri ve durumu hafızaya yazılır,
   - görüş alanında olan ama artık orada olmayanların hatıraları silinir,
   - görüş alanının dışındaki hatıralara dokunulmaz (eski hali kalır; başka haritaya gitmiş olsa da). */
static void bilgileri_guncelle(void)
{
    float r = gorus_menzili();
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *v = O.kar[i];
        if (v->npc) continue;
        HaritaKaydi *h = haritasi(v);
        if (!h || !h->iz.sis || h->gizli) continue; /* gizli haritada oyuncu bir şey görmez, hafızası değişmez */
        Bilgi *b = bilgi_bul(h, v->id, 1);
        if (b->son_x != v->o.x || b->son_y != v->o.y || b->son_r != r || b->son_surum != h->duvar_surum) {
            kesfet(b, h, v->o.x, v->o.y, r);
            b->son_x = v->o.x;
            b->son_y = v->o.y;
            b->son_r = r;
            b->son_surum = h->duvar_surum;
        }
        for (int j = 0; j < O.kar_n; j++) {
            Karakter *t = O.kar[j];
            if (t != v && goruyor(v, t)) bilinen_yaz(b, t);
        }
        for (int j = b->bilinen_n - 1; j >= 0; j--) {
            Bilinen *e = &b->bilinen[j];
            Karakter *t = karakter_bul(e->id);
            if (!t || t == v) {
                b->bilinen[j] = b->bilinen[--b->bilinen_n];
                kirlet();
                continue;
            }
            if (goruyor(v, t)) continue;
            if (gorulur(h, v->o.x, v->o.y, e->x, e->y)) {
                b->bilinen[j] = b->bilinen[--b->bilinen_n];
                kirlet();
            }
        }
        esya_hafizasi_tara(b, h, v->o.x, v->o.y, r);
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

static double parca_boyu(const Yol *y, int i)
{
    double dx = y->xy[2 * i + 2] - y->xy[2 * i], dy = y->xy[2 * i + 3] - y->xy[2 * i + 1];
    return sqrt(dx * dx + dy * dy);
}

static void nokta_yaz(Buf *b, double x, double y, int ilk)
{
    buf_puts(b, ilk ? "[" : ",[");
    sayi_yaz(b, x);
    buf_putc(b, ',');
    sayi_yaz(b, y);
    buf_putc(b, ']');
}

/* Yolun bas..son uzunlukları arasındaki kısmı: [[x,y],...] */
static void yol_parcasi_yaz(Buf *b, const Yol *y, double bas, double son)
{
    double x, yy, s = 0;
    buf_putc(b, '[');
    yol_noktada(y, bas, &x, &yy);
    nokta_yaz(b, x, yy, 1);
    for (int i = 0; i + 1 < y->n; i++) {
        s += parca_boyu(y, i);
        if (s > bas + 1e-9 && s < son - 1e-9) nokta_yaz(b, y->xy[2 * i + 2], y->xy[2 * i + 3], 0);
    }
    yol_noktada(y, son, &x, &yy);
    nokta_yaz(b, x, yy, 0);
    buf_putc(b, ']');
}

/* {"t":"yuru","id":..,"uzunluk":L,"parcalar":[{"bas":..,"son":..,"yol":[[x,y],...]}],...}: t, uzunluğu L olan yolu
   yürüyor; bu istemci yalnızca yolun bas..son (uzunluk cinsinden) parçalarını görür. */
static void yuru_yaz(Buf *b, const Karakter *t, const Yol *y, const double (*parca)[2], int pn)
{
    buf_puts(b, "{\"t\":\"yuru\",\"id\":");
    buf_json_str(b, t->id);
    sayi_alani(b, "uzunluk", y->uzunluk);
    buf_puts(b, ",\"parcalar\":[");
    for (int i = 0; i < pn; i++) {
        buf_puts(b, i ? ",{\"bas\":" : "{\"bas\":");
        sayi_yaz(b, parca[i][0]);
        sayi_alani(b, "son", parca[i][1]);
        buf_puts(b, ",\"yol\":");
        yol_parcasi_yaz(b, y, parca[i][0], parca[i][1]);
        buf_putc(b, '}');
    }
    buf_puts(b, "],\"ad\":");
    buf_json_str(b, t->ad);
    buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s,\"olu\":%s", t->renk, t->npc ? "true" : "false",
               t->olu ? "true" : "false");
    gorunum_alanlari(b, t);
    buf_putc(b, '}');
}

/* (vx,vy)'den r yarıçapla bakan, yolun hangi kısımlarını görür: uzunluk cinsinden [bas, son] aralıkları */
static int yol_gorunen(HaritaKaydi *h, double vx, double vy, double r, const Yol *y, double (*out)[2], int azami)
{
    Duvarlar *d = duvar_dizini(h);
    int n = 0;
    double s0 = 0, t[2 * AZAMI_PARCA];
    for (int i = 0; i + 1 < y->n; i++) {
        double ax = y->xy[2 * i], ay = y->xy[2 * i + 1], bx = y->xy[2 * i + 2], by = y->xy[2 * i + 3];
        double L = parca_boyu(y, i);
        int k = 0;
        if (d) {
            k = duvar_gorunen(d, vx, vy, r, ax, ay, bx, by, t, AZAMI_PARCA);
        } else if (parca_daire_kesisimi(ax, ay, bx, by, vx, vy, r, &t[0], &t[1])) {
            k = 1;
        }
        for (int q = 0; q < k; q++) {
            double a = s0 + t[2 * q] * L, b = s0 + t[2 * q + 1] * L;
            if (n && a - out[n - 1][1] < 1e-6) {
                out[n - 1][1] = b;
            } else if (n < azami) {
                out[n][0] = a;
                out[n][1] = b;
                n++;
            }
        }
        s0 += L;
    }
    return n;
}

static void portaldan_gec(Karakter *k);

/* Yürüyen oyuncu yol boyunca baktı: görüşünün değdiği her yer keşfedildi; gördüğü karakterler ve eşyalar hafızasına
   yazılır, baktığı yerde artık olmayanların hatırası silinir. Görüş yolun üstünde çeyrek karede bir taranır: tarayıcı
   yürürken görüşünü bildiği duvarlarla çizer, bir kapının aralığından bir an görünen duvarı da bilmeli. */
static void yol_boyunca_gor(Bilgi *bl, HaritaKaydi *h, const Karakter *t, const Yol *yol, double r)
{
    const double adim = 0.25;
    int n = 0, kap = 0;
    double *nk = NULL;
    for (int i = 0; i + 1 < yol->n; i++) {
        double ax = yol->xy[2 * i], ay = yol->xy[2 * i + 1], bx = yol->xy[2 * i + 2], by = yol->xy[2 * i + 3];
        int k = (int)ceil(parca_boyu(yol, i) / adim);
        if (k < 1) k = 1;
        for (int q = i ? 1 : 0; q <= k; q++) {
            if (n == kap) {
                kap = kap ? kap * 2 : 64;
                nk = xrealloc(nk, (size_t)kap * 2 * sizeof *nk);
            }
            nk[2 * n] = ax + (bx - ax) * q / k;
            nk[2 * n + 1] = ay + (by - ay) * q / k;
            n++;
        }
    }
    for (int q = 0; q < n; q++) {
        kesfet(bl, h, nk[2 * q], nk[2 * q + 1], r);
        esya_hafizasi_tara(bl, h, nk[2 * q], nk[2 * q + 1], r);
    }
    /* Yürürken görülen karakterler (onlar yerinde duruyor) */
    unsigned char *gordu = xcalloc((size_t)(O.kar_n ? O.kar_n : 1), 1);
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *u = O.kar[i];
        if (u == t || u->gizli || !ayni_haritada(u, t)) continue;
        for (int q = 0; q < n && !gordu[i]; q++)
            if (mesafe(u->o.x, u->o.y, nk[2 * q], nk[2 * q + 1]) <= r && gorus_acik(h, nk[2 * q], nk[2 * q + 1], u->o.x, u->o.y))
                gordu[i] = 1;
        if (gordu[i]) bilinen_yaz(bl, u);
    }
    for (int i = bl->bilinen_n - 1; i >= 0; i--) {
        Bilinen *e = &bl->bilinen[i];
        Karakter *u = karakter_bul(e->id);
        if (!u) continue;
        int sira = 0;
        while (sira < O.kar_n && O.kar[sira] != u) sira++;
        if (sira < O.kar_n && gordu[sira]) continue;
        for (int q = 0; q < n; q++)
            if (mesafe(e->x, e->y, nk[2 * q], nk[2 * q + 1]) <= r && gorus_acik(h, nk[2 * q], nk[2 * q + 1], e->x, e->y)) {
                bilinen_sil(bl, e->id);
                break;
            }
    }
    free(gordu);
    free(nk);
}

/* Karakteri kendi haritasında yol boyunca yürütür (yolun ilk noktası şu anki yeri): aynı haritada olup görenlere
   gördükleri kadarını animasyon olarak gönderir, hafızaları ve keşfedilen alanı günceller; yolunun üstündeki eşyaları
   alır, portalda biterse geçer. sicra: oraya yol yoktu (GM onu duvarın öbür yanına koydu); karakter ışınlanır:
   yürüme animasyonu, yol boyunca keşif ve eşya alma olmaz. Durum yayınını çağıran yapar. */
static void hareket_uygula(Karakter *t, const Yol *yol, int sicra)
{
    HaritaKaydi *h = haritasi(t);
    float g, y;
    if (!h || yol->n < 1 || !harita_olcu(h, &g, &y)) return;
    double ax = t->o.x, ay = t->o.y;
    double hx = sinirla_d(yol->xy[2 * yol->n - 2], 0, g), hy = sinirla_d(yol->xy[2 * yol->n - 1], 0, y);
    if (fabs(hx - ax) < 1e-5 && fabs(hy - ay) < 1e-5) return;
    int sis = h->iz.sis && !h->gizli;
    float r = gorus_menzili();
    double(*parca)[2] = xmalloc(AZAMI_PARCA * sizeof *parca);
    const double tam[1][2] = { { 0, yol->uzunluk } };

    Buf b;
    buf_init(&b);
    for (Istemci *c = istemciler; c && !sicra; c = c->sonraki) {
        b.len = 0;
        if (c->gm) {
            if (!gm_haritasinda(t)) continue;
            yuru_yaz(&b, t, yol, tam, 1);
        } else {
            Karakter *v = karakter_bul(c->oyuncu);
            if (!v || !ayni_haritada(v, t) || h->gizli) continue;
            if (v == t || (!sis && !t->gizli)) {
                yuru_yaz(&b, t, yol, tam, 1);
            } else {
                int pn = t->gizli ? 0 : yol_gorunen(h, v->o.x, v->o.y, r, yol, parca, AZAMI_PARCA);
                if (!pn) continue;
                yuru_yaz(&b, t, yol, (const double(*)[2])parca, pn);
            }
        }
        gonder_buf(c, &b);
    }
    buf_free(&b);

    if (sis && !t->gizli) {
        /* Gidişini görüp varışını görmeyenler artık onun nerede olduğunu bilmez */
        for (int i = 0; i < O.kar_n; i++) {
            Karakter *v = O.kar[i];
            if (v->npc || v == t || !ayni_haritada(v, t)) continue;
            int gordu, sonda;
            if (sicra) {
                gordu = gorulur(h, v->o.x, v->o.y, ax, ay);
                sonda = gorulur(h, v->o.x, v->o.y, hx, hy);
            } else {
                int pn = yol_gorunen(h, v->o.x, v->o.y, r, yol, parca, AZAMI_PARCA);
                gordu = pn > 0;
                sonda = pn > 0 && parca[pn - 1][1] >= yol->uzunluk - 1e-6;
            }
            if (gordu && !sonda) bilinen_sil(bilgi_bul(h, v->id, 1), t->id);
        }
    }
    free(parca);
    t->o.x = (float)hx;
    t->o.y = (float)hy;
    kirlet();
    if (sis && !t->npc && !sicra) yol_boyunca_gor(bilgi_bul(h, t->id, 1), h, t, yol, r);
    bilgileri_guncelle();
    yerden_al(t, sicra ? NULL : yol);
    portaldan_gec(t);
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
        } else if (!gm_haritasinda(a) && !gm_haritasinda(t)) {
            continue;
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
    takaslari_bitir(k->id, "Takas bitti: karşı taraf öldü.");
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
        if ((c->gm && gm_haritasinda(k)) || (v && goruyor(v, k))) gonder_buf(c, &b);
    }
    buf_free(&s);
    buf_free(&b);
    bilgileri_guncelle();
}

/* Haritadaki bir işaret: o haritadaki oyunculara (harita gizli değilse) ve oraya bakan GM'e */
static void isaret_yayinla(const HaritaKaydi *h, double x, double y, const char *ad, const char *renk, int odak)
{
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"t\":\"isaret\",\"x\":%.1f,\"y\":%.1f,\"odak\":%s,\"renk\":\"%s\",\"ad\":", x, y,
               odak ? "true" : "false", renk);
    buf_json_str(&b, ad);
    buf_putc(&b, '}');
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        Karakter *v = c->gm ? NULL : karakter_bul(c->oyuncu);
        if (c->gm ? (O.aktif && !strcmp(O.aktif, h->dosya)) : (v && bu_haritada(v, h->dosya) && !h->gizli))
            gonder_buf(c, &b);
    }
    buf_free(&b);
}

/* Portal efekti: bir haritada (x,y)'de; o haritada orayı görenlere ve oraya bakan GM'e. kim: geçen karakter. */
static void portal_yayinla(HaritaKaydi *h, const Karakter *kim, double x, double y, int varis)
{
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"t\":\"portal\",\"id\":");
    buf_json_str(&b, kim->id);
    sayi_alani(&b, "x", x);
    sayi_alani(&b, "y", y);
    buf_printf(&b, ",\"varis\":%s}", varis ? "true" : "false");
    for (Istemci *c = istemciler; c; c = c->sonraki) {
        Karakter *v = c->gm ? NULL : karakter_bul(c->oyuncu);
        int gonder;
        if (c->gm) gonder = O.aktif && !strcmp(O.aktif, h->dosya);
        else gonder = v && v != kim && bu_haritada(v, h->dosya) && !h->gizli && gorulur(h, v->o.x, v->o.y, x, y);
        if (gonder) gonder_buf(c, &b);
    }
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
    sayi_alani(b, "dayaniklilik", e->dayaniklilik);
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

/* Karakter yol boyunca yürüdü (yol NULL: yalnızca bulunduğu yere kondu): yolunun üstündeki (yola ALMA_MESAFESI kadar
   yakın ve arasında duvar olmayan) yerdeki eşyaları, yolda karşılaştığı sırayla envanterine alır (yer kaldıkça). */
static void yerden_al(Karakter *k, const Yol *yol)
{
    HaritaKaydi *h = haritasi(k);
    if (!h || k->olu || !h->yer_n) return;
    int dolu = 0, aldi = 0, an = 0;
    struct {
        long long id;
        double s;
    } *aday = xmalloc((size_t)h->yer_n * sizeof *aday);
    for (int i = 0; i < h->yer_n; i++) {
        const YerEsyasi *ye = &h->yer[i];
        double s = 0, px = k->o.x, py = k->o.y, m;
        if (yol) {
            m = yol_en_yakin(yol, ye->x, ye->y, &s);
            yol_noktada(yol, s, &px, &py);
        } else {
            m = mesafe(px, py, ye->x, ye->y);
        }
        if (m > ALMA_MESAFESI || !gorus_acik(h, px, py, ye->x, ye->y)) continue;
        int q = an++;
        while (q > 0 && aday[q - 1].s > s) {
            aday[q] = aday[q - 1];
            q--;
        }
        aday[q].id = ye->id;
        aday[q].s = s;
    }
    for (int q = 0; q < an; q++) {
        int i = yer_esyasi_sira(h, aday[q].id);
        if (i < 0) continue;
        YerEsyasi *ye = &h->yer[i];
        if (!envantere_koy(k, &ye->e)) {
            dolu = 1;
            continue;
        }
        char m[300];
        snprintf(m, sizeof m, "%s yerden aldı: %s", k->ad, ye->e.ad);
        kayit_sahibe("esya", k, m);
        yerden_kaldir(h, i);
        aldi = 1;
    }
    free(aday);
    if (aldi) bilgileri_guncelle(); /* alanın (ve o an görenlerin) hafızasından da düşsün */
    if (dolu && !k->npc) oyuncuya_bildir(k, "hata", "Envanterin dolu; yerdeki eşyayı alamadın.");
}

/* v oyuncusu yerdeki eşyayı şu an görüyor mu: kendi haritasında; sis kapalıysa hepsini, açıksa görüş menzilindekileri
   (duvarların arkası hariç) */
static int yer_goruyor(const Karakter *v, HaritaKaydi *h, const YerEsyasi *ye)
{
    if (!h || h->gizli || !bu_haritada(v, h->dosya)) return 0;
    return gorulur(h, v->o.x, v->o.y, ye->x, ye->y);
}

static void yer_esyasi_yaz(Buf *b, long long id, double x, double y, const Esya *e, int hatira, int ilk)
{
    buf_printf(b, "%s{\"id\":%lld,\"x\":", ilk ? "" : ",", id);
    sayi_yaz(b, x);
    sayi_alani(b, "y", y);
    buf_printf(b, ",\"hatira\":%s,\"esya\":", hatira ? "true" : "false");
    esya_json(b, e);
    buf_putc(b, '}');
}

/* ,"yerdekiler":[{"id":1,"x":..,"y":..,"hatira":false,"esya":{...}}]
   v NULL ise GM: haritadakilerin hepsi. Oyuncu şu an gördüklerini ve hatırladıklarını (hatira: o sırada orada
   olmayabilir) alır. */
static void yerdekiler_yaz(Buf *b, HaritaKaydi *h, const Karakter *v, Bilgi *bl)
{
    buf_puts(b, ",\"yerdekiler\":[");
    int ilk = 1;
    for (int i = 0; h && i < h->yer_n; i++) {
        const YerEsyasi *ye = &h->yer[i];
        if (v && !yer_goruyor(v, h, ye)) continue;
        yer_esyasi_yaz(b, ye->id, ye->x, ye->y, &ye->e, 0, ilk);
        ilk = 0;
    }
    for (int i = 0; v && bl && h && h->iz.sis && i < bl->esya_n; i++) {
        const BilinenEsya *e = &bl->esya[i];
        int sira = yer_esyasi_sira(h, e->id);
        if (sira >= 0 && yer_goruyor(v, h, &h->yer[sira])) continue; /* zaten canlı olarak yazıldı */
        yer_esyasi_yaz(b, e->id, e->x, e->y, &e->e, 1, ilk);
        ilk = 0;
    }
    buf_putc(b, ']');
}

/* ================================================================== portallar */

static int portal_sira(const HaritaKaydi *h, long long id)
{
    for (int i = 0; h && i < h->portal_n; i++)
        if (h->portal[i].id == id) return i;
    return -1;
}

/* Oyuncu bu noktayı daha önce görmüş mü (keşfettiği alanda mı) */
static int kesfedilmis(const Bilgi *bl, double x, double y)
{
    return bl && kesif_var(&bl->kesif, x, y);
}

/* ,"portallar":[{"id":..,"ad":..,"x":..,"y":..,"hedef":..,"hx":..,"hy":..}]
   Oyuncular portalı haritanın bir parçası gibi görür: görüş menzilinde ya da daha önce gördüğü yerdeyse. Nereye çıktığını
   yalnızca GM bilir. */
static void portallar_yaz(Buf *b, HaritaKaydi *h, const Karakter *v, const Bilgi *bl)
{
    buf_puts(b, ",\"portallar\":[");
    int ilk = 1;
    for (int i = 0; h && i < h->portal_n; i++) {
        const Portal *p = &h->portal[i];
        if (v && (h->gizli || (!gorulur(h, v->o.x, v->o.y, p->x, p->y) && !kesfedilmis(bl, p->x, p->y)))) continue;
        buf_printf(b, "%s{\"id\":%lld,\"ad\":", ilk ? "" : ",", p->id);
        ilk = 0;
        buf_json_str(b, p->ad);
        sayi_alani(b, "x", p->x);
        sayi_alani(b, "y", p->y);
        if (!v) {
            buf_puts(b, ",\"hedef\":");
            if (p->hedef) buf_json_str(b, p->hedef);
            else buf_puts(b, "null");
            sayi_alani(b, "hx", p->hx);
            sayi_alani(b, "hy", p->hy);
        }
        buf_putc(b, '}');
    }
    buf_putc(b, ']');
}

/* Oyuncunun yürüyüşü bir portalda bittiyse hedef haritaya geçer. Varışta portal tetiklenmez (yürüyüş değil). */
static void portaldan_gec(Karakter *k)
{
    HaritaKaydi *h = haritasi(k);
    if (k->npc || k->olu || !h) return;
    for (int i = 0; i < h->portal_n; i++) {
        Portal *p = &h->portal[i];
        if (!p->hedef || mesafe(p->x, p->y, k->o.x, k->o.y) > PORTAL_MESAFESI || !gorus_acik(h, k->o.x, k->o.y, p->x, p->y))
            continue;
        HaritaKaydi *hh = harita_bul(p->hedef, 0);
        float g, y;
        if (!hh || !harita_olcu(hh, &g, &y)) continue;
        double hx = sinirla_d(p->hx, 0, g), hy = sinirla_d(p->hy, 0, y);
        duvardan_ayir(hh, &hx, &hy);
        char m[400], ad[AD_N];
        kopyala(ad, sizeof ad, p->ad);
        portal_yayinla(h, k, p->x, p->y, 0);
        istekleri_temizle(k->id);
        hedef_istegini_dusur(k->id);
        takaslari_bitir(k->id, "Takas bitti: karşı taraf portaldan geçti.");
        harita_ata(k, hh->dosya); /* bundan sonra h ve p kullanılmaz */
        k->o.x = (float)hx;
        k->o.y = (float)hy;
        snprintf(m, sizeof m, "%s portaldan geçti%s%s.", k->ad, ad[0] ? ": " : "", ad);
        kayit_sahibe("bilgi", k, m);
        bilgileri_guncelle();
        portal_yayinla(hh, k, hx, hy, 1);
        return;
    }
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

/* Pul ağırlıkla büyür: 70 ağırlıkta 1 kat; ağırlık iki katına çıkınca iki kat (0.6 ile 2.2 kat arasında) */
static double token_olcegi(const Karakter *k)
{
    return fmin(fmax(k->o.agirlik / KARAKTER_AGIRLIGI, 0.6), 2.2);
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
    buf_printf(b, ",\"gizli\":%s,\"izgara\":", h->gizli ? "true" : "false");
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

/* Bekleyen istek (yoksa null). GM hareketin duvarların etrafından dolaşan yolunu da görür (oyuncu kendi yolunu
   bildiği duvarlarla kendisi hesaplar). */
static void istek_json(Buf *b, const Istek *I, int gm)
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
        if (gm) {
            sayi_alani(b, "uzunluk", I->yol_uzunlugu);
            buf_puts(b, ",\"yol\":[");
            for (int i = 0; i < I->yol_n; i++) nokta_yaz(b, I->yol[i][0], I->yol[i][1], i == 0);
            buf_putc(b, ']');
        }
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

    /* Bütün karakterler: hangi haritada oldukları (harita), GM'in baktığı haritada mı (burada) */
    buf_puts(b, ",\"karakterler\":[");
    int ilk = 1;
    for (int i = 0; i < O.kar_n; i++) {
        const Karakter *k = O.kar[i];
        if (!ilk) buf_putc(b, ',');
        ilk = 0;
        buf_puts(b, "{\"id\":");
        buf_json_str(b, k->id);
        buf_puts(b, ",\"ad\":");
        buf_json_str(b, k->ad);
        buf_printf(b,
                   ",\"renk\":\"%s\",\"npc\":%s,\"gizli\":%s,\"olu\":%s,\"cevrimici\":%s,\"sirada\":%s,"
                   "\"burada\":%s",
                   k->renk, k->npc ? "true" : "false", k->gizli ? "true" : "false", k->olu ? "true" : "false",
                   k->baglanti > 0 ? "true" : "false", sirada_mi(k->id) ? "true" : "false",
                   gm_haritasinda(k) ? "true" : "false");
        int para[3];
        oyuncu_parasi(&k->o, para);
        para_alani(b, para);
        buf_puts(b, ",\"harita\":");
        if (k->harita) buf_json_str(b, k->harita);
        else buf_puts(b, "null");
        buf_puts(b, ",\"x\":");
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
    istek_json(b, O.istek_var ? &O.istek : NULL, 1);
    yerdekiler_yaz(b, h, NULL, NULL);
    portallar_yaz(b, h, NULL, NULL);
    buf_puts(b, ",\"duvarlar\":[");
    for (int i = 0; h && i < h->duvar_n; i++) duvar_yaz(b, &h->duvar[i], i == 0);
    buf_putc(b, ']');
    /* Portal hedefi seçerken: bütün haritaların portalları */
    buf_puts(b, ",\"portal_listesi\":[");
    int pilk = 1;
    for (int i = 0; i < O.harita_n; i++)
        for (int k = 0; k < O.harita[i].portal_n; k++) {
            const Portal *p = &O.harita[i].portal[k];
            buf_printf(b, "%s{\"harita\":", pilk ? "" : ",");
            pilk = 0;
            buf_json_str(b, O.harita[i].dosya);
            buf_printf(b, ",\"id\":%lld,\"ad\":", p->id);
            buf_json_str(b, p->ad);
            buf_putc(b, '}');
        }
    buf_putc(b, ']');
    takaslar_yaz(b, NULL);
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
    if (t->gizli) return 0;
    if (goruyor(v, t)) return 1;
    if (!sis_acik(haritasi(v))) return 0;
    return bl && bilinen_bul(bl, t->id);
}

/* v'nin bildiği ölüm durumu */
static int bilinen_olum(const Karakter *v, const Karakter *t, Bilgi *bl)
{
    if (t == v || goruyor(v, t)) return t->olu;
    Bilinen *e = bl ? bilinen_bul(bl, t->id) : NULL;
    return e ? e->olu > 0 : 0;
}

/* Oyuncunun keşfettiği alan ve bildiği duvarlar, değiştiyse (bu istemciye en son gönderilenden beri):
   ,"kesif":{"k":8,"g":..,"y":..,"veri":"..."},"duvarlar":[[id,x1,y1,x2,y2],...]
   Değişmediyse ikisi de yazılmaz; tarayıcı eldekini kullanır. Sis kapalıysa her duvar bilinir, keşif gönderilmez. */
static void kesif_yaz(Buf *b, Istemci *c, HaritaKaydi *h, Bilgi *bl)
{
    long long surum = h->iz.sis ? (bl ? bl->kesif_surum : 0) : -1;
    if (c->kesif_harita == h->no && c->kesif_surum == surum && c->duvar_surum == h->duvar_surum) return;
    if (h->iz.sis) {
        float g = 0, y = 0;
        harita_olcu(h, &g, &y);
        Kesif bos = { 0 };
        const Kesif *k = bl && kesif_uygun(&bl->kesif, g, y) ? &bl->kesif : &bos;
        if (k == &bos) kesif_kur(&bos, g, y);
        buf_puts(b, ",\"kesif\":{\"k\":");
        sayi_yaz(b, k->k);
        buf_printf(b, ",\"g\":%d,\"y\":%d,\"veri\":\"", k->g, k->y);
        kesif_kodla(b, k);
        buf_puts(b, "\"}");
        kesif_bosalt(&bos);
    }
    buf_puts(b, ",\"duvarlar\":[");
    int ilk = 1;
    for (int i = 0; i < h->duvar_n; i++) {
        if (h->iz.sis && !(bl && duvari_biliyor(bl, h->duvar[i].id))) continue;
        duvar_yaz(b, &h->duvar[i], ilk);
        ilk = 0;
    }
    buf_putc(b, ']');
    c->kesif_harita = h->no;
    c->kesif_surum = surum;
    c->duvar_surum = h->duvar_surum;
}

static void durum_yaz_oyuncu(Buf *b, Istemci *c, Karakter *ben)
{
    buf_puts(b, "{\"t\":\"durum\",\"ben\":{\"gm\":false,\"id\":");
    buf_json_str(b, ben->id);
    buf_puts(b, ",\"ad\":");
    buf_json_str(b, ben->ad);
    buf_printf(b, ",\"renk\":\"%s\",\"yerde\":%s", ben->renk, haritada(ben) ? "true" : "false");
    int para[3];
    oyuncu_parasi(&ben->o, para);
    para_alani(b, para);
    gorunum_alanlari(b, ben);
    buf_puts(b, ",\"resim_yolu\":");
    buf_json_str(b, ben->resim);
    buf_puts(b, ",\"sapka_yolu\":");
    buf_json_str(b, ben->sapka);
    buf_puts(b, "},\"oyun_adi\":");
    buf_json_str(b, O.ayar.oyun_adi);
    oyuncular_yaz(b);

    /* Oyuncu yalnızca kendi haritasını görür */
    HaritaKaydi *h = haritasi(ben);
    float g = 0, y = 0;
    Bilgi *bl = NULL;
    buf_puts(b, ",\"harita\":");
    if (!h || !harita_olcu(h, &g, &y)) {
        buf_puts(b, "null,\"tokenler\":[],\"yerdekiler\":[],\"portallar\":[]");
        c->kesif_harita = 0; /* harita yeniden görünce keşif ve duvarlar baştan gönderilsin */
    } else if (h->gizli) {
        buf_puts(b, "{\"gizli\":true},\"tokenler\":[],\"yerdekiler\":[],\"portallar\":[]");
        c->kesif_harita = 0;
    } else {
        harita_json(b, h, g, y);
        if (h->iz.sis) bl = bilgi_bul(h, ben->id, 0);
        kesif_yaz(b, c, h, bl);
        buf_puts(b, ",\"tokenler\":[");
        int ilk = 1;
        for (int i = 0; i < O.kar_n; i++) {
            const Karakter *t = O.kar[i];
            if (goruyor(ben, t) && haritada(t)) {
                if (!ilk) buf_putc(b, ',');
                ilk = 0;
                token_yaz(b, t, t->o.x, t->o.y, t->olu, 1, t == ben);
            } else if (bl) {
                Bilinen *e = bilinen_bul(bl, t->id); /* başka haritaya gitmiş olsa da hatırası burada kalır */
                if (!e) continue;
                if (!ilk) buf_putc(b, ',');
                ilk = 0;
                token_yaz(b, t, e->x, e->y, e->olu > 0, 0, 0);
            }
        }
        buf_putc(b, ']');
        yerdekiler_yaz(b, h, ben, bl);
        portallar_yaz(b, h, ben, bl);
    }
    takaslar_yaz(b, ben);
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
        if (!t) continue;
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
    istek_json(b, istek_kimin(ben->id), 0);
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
    HaritaKaydi *h = haritasi(yapan); /* harita: işi yapanın bulunduğu harita */
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
    for (int i = 0; i < 3; i++) { /* para eksiye düşmez */
        int *p = sikke(&k->o, i);
        if (*p < 0) *p = 0;
        if (*p > AZAMI_PARA) *p = AZAMI_PARA;
    }
    alan_onar(&k->o.x, once->x, -AZAMI_STAT, AZAMI_STAT, k, "x", baglam);
    alan_onar(&k->o.y, once->y, -AZAMI_STAT, AZAMI_STAT, k, "y", baglam);
    for (int i = 0; i < DIZI_N(STATLAR); i++) {
        const StatAlani *a = &STATLAR[i];
        alan_onar(stat_yeri(&k->o, a), stat_oku(once, a), a->lo, a->hi, k, a->etiket, baglam);
    }
    envanter_onar(&k->o);
    kirlet();
}

/* Kurallar karakterin yerini değiştirdiyse onu oraya düz çizgide it (animasyonla); arada duvar varsa duvarın önünde
   durur. */
static void konum_degisimi(Karakter *k, const Oyuncu *s)
{
    float nx = k->o.x, ny = k->o.y;
    if (nx == s->x && ny == s->y) return;
    HaritaKaydi *h = haritasi(k);
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) return; /* haritada değil: kuralların yazdığı kalır */
    k->o.x = s->x;
    k->o.y = s->y;
    double cx = sinirla_d(nx, 0, g), cy = sinirla_d(ny, 0, y);
    Duvarlar *d = duvar_dizini(h);
    if (d) duvar_carpma(d, s->x, s->y, cx, cy, &cx, &cy);
    Yol yol;
    yol_duz(&yol, s->x, s->y, cx, cy);
    hareket_uygula(k, &yol, 0);
    yol_bosalt(&yol);
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

/* hareket() çalışırken yürünecek yol (yol_noktasi için) */
static const Yol *kural_yolu;

void yol_noktasi(float uzunluk, float *x, float *y)
{
    if (!kural_yolu || !x || !y || !isfinite(uzunluk)) return;
    double px, py;
    yol_noktada(kural_yolu, uzunluk, &px, &py);
    *x = (float)px;
    *y = (float)py;
}

/* GM'in onayladığı yürüyüş: kurallar.c'deki hareket() yolun uzunluğunu bilir, enerjiyi ayarlar ve izin verir ya da
   vermez. İzin verirse karakter yol boyunca yürür; kurallar hedefi değiştirdiyse oraya (yine duvarların etrafından)
   yürür. Yürüdüyse 1, kurallar izin vermediyse 0, kuralların seçtiği yere yol yoksa -1 döner (o zaman hareket() hiç
   çağrılmamış gibi karakter eski haline döner). */
static int kural_hareket(Karakter *k, const Yol *yol)
{
    Oyuncu once = k->o;
    double x = yol->xy[2 * yol->n - 2], y = yol->xy[2 * yol->n - 1];
    yol_uzunlugu = (float)yol->uzunluk;
    kural_yolu = yol;
    kurallara_gir(k, NULL, "hareket");
    int izin = hareket(&k->o, (float)x, (float)y);
    kurallardan_cik();
    kural_yolu = NULL;
    yol_uzunlugu = 0;
    karakter_onar(k, &once, "hareket");
    Yol yeni = { 0 };
    const Yol *yurunen = yol;
    if (izin && (k->o.x != once.x || k->o.y != once.y)) {
        /* kurallar hedefi değiştirdi (ör. yolu kısalttı) */
        HaritaKaydi *h = haritasi(k);
        float g, hy;
        double tx = k->o.x, ty = k->o.y;
        if (h && harita_olcu(h, &g, &hy)) {
            tx = sinirla_d(tx, 0, g);
            ty = sinirla_d(ty, 0, hy);
        }
        if (!yol_bul(h, once.x, once.y, tx, ty, &yeni)) {
            k->o = once;
            return -1;
        }
        yurunen = &yeni;
    }
    k->o.x = once.x;
    k->o.y = once.y;
    if (izin) hareket_uygula(k, yurunen, 0);
    yol_bosalt(&yeni);
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
        if (!k->olu && k->o.can <= 0) oldur(k);
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
        if (!k || k->olu || (k->npc && !haritada(k))) continue;
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
        if (k && !k->olu && haritada(k) && (k->npc || k->baglanti > 0)) return 1;
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

/* Yürüme isteğinin yolu (çok köşeliyse 0) */
static int istek_yolu_yaz(Istek *I, const Yol *y)
{
    if (y->n > AZAMI_YOL) return 0;
    for (int i = 0; i < y->n; i++) {
        I->yol[i][0] = (float)y->xy[2 * i];
        I->yol[i][1] = (float)y->xy[2 * i + 1];
    }
    I->yol_n = y->n;
    I->yol_uzunlugu = (float)y->uzunluk;
    return 1;
}

/* Haritanın duvarları değişti: bekleyen yürüme isteğinin yolu yeniden bulunur; yol kapandıysa istek düşer. */
static void istek_yolunu_yenile(HaritaKaydi *h)
{
    if (!O.istek_var || O.istek.tur != ISTEK_HAREKET) return;
    Karakter *k = karakter_bul(O.istek.kim);
    if (!k || !bu_haritada(k, h->dosya)) return;
    Yol y;
    int var = yol_bul(h, k->o.x, k->o.y, O.istek.x2, O.istek.y2, &y);
    if (!var || !istek_yolu_yaz(&O.istek, &y)) {
        O.istek_var = 0;
        oyuncuya_bildir(k, "hata", "Gideceğin yerin yolu kapandı; başka bir yer seç.");
    }
    if (var) yol_bosalt(&y);
}

/* GM'in taşıdığı karakter: oraya yol varsa yürür; yoksa (duvarın öbür yanı, kapalı bir oda) ışınlanır. */
static void gm_tasi(Karakter *k, double x, double y)
{
    HaritaKaydi *h = haritasi(k);
    Yol yol;
    int var = yol_bul(h, k->o.x, k->o.y, x, y, &yol);
    if (!var) {
        duvardan_ayir(h, &x, &y);
        yol_duz(&yol, k->o.x, k->o.y, x, y);
    }
    hareket_uygula(k, &yol, !var);
    yol_bosalt(&yol);
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
    HaritaKaydi *h = haritasi(ben);
    if (!h) {
        istemciye_hata(c, "Henüz haritada değilsin; GM seni haritaya yerleştirecek.");
        return 0;
    }
    if (h->gizli) {
        istemciye_hata(c, "GM haritayı hazırlıyor; biraz bekle.");
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
    HaritaKaydi *h = haritasi(ben);
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
    Yol yol;
    if (!yol_bul(h, ben->o.x, ben->o.y, x, yy, &yol)) {
        istemciye_hata(c, "Oraya gidecek bir yol yok.");
        return;
    }
    Istek I;
    memset(&I, 0, sizeof I);
    int sigdi = istek_yolu_yaz(&I, &yol);
    yol_bosalt(&yol);
    if (!sigdi) {
        istemciye_hata(c, "Oraya giden yol çok dolambaçlı; daha yakın bir yer seç.");
        return;
    }
    Istek *yeni = istek_ekle(ISTEK_HAREKET, ben->id);
    memcpy(yeni->yol, I.yol, sizeof I.yol);
    yeni->yol_n = I.yol_n;
    yeni->yol_uzunlugu = I.yol_uzunlugu;
    yeni->x2 = (float)x;
    yeni->y2 = (float)yy;
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
        HaritaKaydi *h = haritasi(k);
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
        Yol yol;
        if (!yol_bul(h, k->o.x, k->o.y, x, yy, &yol)) {
            istemciye_hata(c, "Bu hedefe gidecek yol yok (arada duvar var).");
            oyuncuya_bildir(k, "hata", "Gideceğin yerin yolu kapalı; yürüyüş olmadı.");
            herkese_durum();
            return;
        }
        int sonuc = kural_hareket(k, &yol);
        yol_bosalt(&yol);
        if (sonuc <= 0) {
            char m[200];
            if (sonuc < 0) snprintf(m, sizeof m, "Kuralların (hareket()) %s için seçtiği yere yol yok; yürüyüş olmadı.", k->ad);
            else snprintf(m, sizeof m, "Kurallar (hareket()) %s için bu yürüyüşe izin vermedi.", k->ad);
            istemciye_hata(c, m);
            oyuncuya_bildir(k, "hata", "Kurallara göre bu yürüyüş yapılamadı.");
        }
        yayinla_ve_enerji_denetle(k);
        return;
    }
    Karakter *t = karakter_bul(is.hedef);
    if (is.tur == ISTEK_BUYU) {
        if (!t || !ayni_haritada(k, t) || t->olu || !buyu_gecerli(is.buyu)) {
            istemciye_hata(c, "Bu büyü artık yapılamaz (hedef ya da büyü değişti).");
            herkese_durum();
            return;
        }
        buyu_uygula(k, t, is.buyu);
        yayinla_ve_enerji_denetle(k);
        return;
    }
    if (!t || t == k || !ayni_haritada(k, t) || t->olu) {
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
        /* Yeni oyuncu hiçbir haritada değildir; GM onu bir haritaya yerleştirir */
        if (oyuncu_sayisi() >= AZAMI_OYUNCU || !(p = karakter_olustur(0, NULL, ad, siradaki_renk()))) {
            kopyala(hata, hn, "Oyun dolu.");
            return 409;
        }
        siraya_ekle(p->id);
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
    HaritaKaydi *h = c->gm ? aktif_harita() : haritasi(ben);
    if (!h || (!c->gm && h->gizli)) return;
    double x = json_sayi(j, "x", -1e9), y = json_sayi(j, "y", -1e9);
    if (x < -1e5 || y < -1e5 || x > 1e5 || y > 1e5) return;
    isaret_yayinla(h, x, y, c->gm ? "GM" : ben->ad, c->gm ? GM_RENK : ben->renk, c->gm && json_mantik(j, "odak", 0));
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

/* Haritanın kare cinsinden boyutu değişti (resim aynı): resmin üstündeki her şey yerinde kalsın diye bu haritadaki
   karakterler, yerdeki eşyalar, portallar ve bu haritaya çıkan portalların varış yerleri, oyuncuların hatırladıkları
   ve bekleyen yürüyüşün hedefi duvarlarla aynı oranda ölçeklenir. Yoksa karakter, yerinde kalan duvarın öbür yanına
   düşebilirdi. */
static void haritayi_olcekle(HaritaKaydi *h, double kx, double ky)
{
    for (int i = 0; i < O.kar_n; i++) {
        Karakter *k = O.kar[i];
        if (!bu_haritada(k, h->dosya)) continue;
        k->o.x = (float)(k->o.x * kx);
        k->o.y = (float)(k->o.y * ky);
    }
    for (int i = 0; i < h->yer_n; i++) {
        h->yer[i].x = (float)(h->yer[i].x * kx);
        h->yer[i].y = (float)(h->yer[i].y * ky);
    }
    for (int i = 0; i < h->portal_n; i++) {
        h->portal[i].x = (float)(h->portal[i].x * kx);
        h->portal[i].y = (float)(h->portal[i].y * ky);
    }
    for (int m = 0; m < O.harita_n; m++)
        for (int i = 0; i < O.harita[m].portal_n; i++) {
            Portal *p = &O.harita[m].portal[i];
            if (!p->hedef || strcmp(p->hedef, h->dosya)) continue;
            p->hx = (float)(p->hx * kx);
            p->hy = (float)(p->hy * ky);
        }
    for (int i = 0; i < h->bilgi_n; i++) {
        Bilgi *b = &h->bilgi[i];
        for (int k = 0; k < b->bilinen_n; k++) {
            b->bilinen[k].x = (float)(b->bilinen[k].x * kx);
            b->bilinen[k].y = (float)(b->bilinen[k].y * ky);
        }
        for (int k = 0; k < b->esya_n; k++) {
            b->esya[k].x = (float)(b->esya[k].x * kx);
            b->esya[k].y = (float)(b->esya[k].y * ky);
        }
    }
    Karakter *ik = O.istek_var && O.istek.tur == ISTEK_HAREKET ? karakter_bul(O.istek.kim) : NULL;
    if (ik && bu_haritada(ik, h->dosya)) {
        O.istek.x2 = (float)(O.istek.x2 * kx);
        O.istek.y2 = (float)(O.istek.y2 * ky);
    }
}

/* GM haritanın kare cinsinden boyutunu seçti (iletide genislik ve yukseklik varsa). Duvarlar, keşfedilen yerler ve
   haritadaki her şey resme göre yerinde kalır: yeni boyuta ölçeklenir. */
static void boyut_al(HaritaKaydi *h, const Json *j)
{
    if (!json_al(j, "genislik") || !json_al(j, "yukseklik")) return;
    double g1 = h->iz.genislik, y1 = h->iz.yukseklik;
    double g2 = sinirla_d(json_sayi(j, "genislik", 0), 1, AZAMI_HARITA), y2 = sinirla_d(json_sayi(j, "yukseklik", 0), 1, AZAMI_HARITA);
    if (g1 == g2 && y1 == y2) return;
    h->iz.genislik = g2;
    h->iz.yukseklik = y2;
    if (g1 >= 1 && y1 >= 1) {
        duvarlari_olcekle(h, g2 / g1, y2 / y1, (float)g2, (float)y2);
        haritayi_olcekle(h, g2 / g1, y2 / y1);
        for (int i = 0; i < h->bilgi_n; i++) {
            Bilgi *b = &h->bilgi[i];
            if (!b->kesif.bit) continue;
            kesif_olcekle(&b->kesif, g1, y1, g2, y2);
            b->kesif_surum = ++surum_sayaci;
        }
    }
    duvarlar_degisti(h);
    kirlet();
}

/* ------------------------------------------------------------------ duvarlar (GM) */

static Duvar *duvar_bul(HaritaKaydi *h, long long id, int *sira)
{
    for (int i = 0; h && i < h->duvar_n; i++)
        if (h->duvar[i].id == id) {
            if (sira) *sira = i;
            return &h->duvar[i];
        }
    return NULL;
}

/* GM baktığı haritaya bir duvar çizdi (x1,y1)-(x2,y2), kare cinsinden. Kimliği ona döner (geri almak için). */
static void m_duvar_ekle(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "Duvar çizmek için önce bir harita aç.");
        return;
    }
    if (h->duvar_n >= AZAMI_DUVAR) {
        istemciye_hata(c, "Bu haritada çok fazla duvar var.");
        return;
    }
    static const char *const ad[4] = { "x1", "y1", "x2", "y2" };
    int v[4];
    for (int i = 0; i < 4; i++) {
        double d = json_sayi(j, ad[i], NAN);
        if (!isfinite(d)) return;
        v[i] = (int)lround(sinirla_d(d, 0, i % 2 ? y : g) * 1000);
    }
    if (llabs((long long)v[0] - v[2]) + llabs((long long)v[1] - v[3]) < 10) return; /* 0.01 kareden kısa */
    for (int i = 0; i < h->duvar_n; i++) {
        const Duvar *w = &h->duvar[i];
        if ((w->x1 == v[0] && w->y1 == v[1] && w->x2 == v[2] && w->y2 == v[3]) ||
            (w->x1 == v[2] && w->y1 == v[3] && w->x2 == v[0] && w->y2 == v[1]))
            return; /* zaten var */
    }
    h->duvar = xrealloc(h->duvar, (size_t)(h->duvar_n + 1) * sizeof *h->duvar);
    Duvar *w = &h->duvar[h->duvar_n++];
    w->id = ++O.yer_sayac;
    w->x1 = v[0];
    w->y1 = v[1];
    w->x2 = v[2];
    w->y2 = v[3];
    long long id = w->id;
    duvarlar_degisti(h);
    bilgileri_guncelle();
    herkese_durum();
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"t\":\"duvar_eklendi\",\"id\":%lld}", id);
    gonder_buf(c, &b);
    buf_free(&b);
}

static void m_duvar_sil(const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    long long id = 0;
    int i;
    json_tamsayi(j, "id", &id);
    if (!duvar_bul(h, id, &i)) return;
    memmove(&h->duvar[i], &h->duvar[i + 1], (size_t)(h->duvar_n - i - 1) * sizeof *h->duvar);
    h->duvar_n--;
    duvarlar_degisti(h);
    bilgileri_guncelle();
    herkese_durum();
}

static void m_duvarlari_sil(void)
{
    HaritaKaydi *h = aktif_harita();
    if (!h || !h->duvar_n) return;
    free(h->duvar);
    h->duvar = NULL;
    h->duvar_n = 0;
    duvarlar_degisti(h);
    kayit_metin("bilgi", GOR_GM, NULL, "Bu haritadaki bütün duvarlar silindi.");
    bilgileri_guncelle();
    herkese_durum();
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
    /* Yalnızca GM'in baktığı harita değişir; oyuncular kendi haritalarında kalır */
    aktif_degistir(kapat ? NULL : dosya);
    if (!kapat) boyut_al(harita_bul(dosya, 1), j);
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
    sinirlara_al(h);
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
    if (!k || !gm_haritasinda(k) || !isfinite(x) || !isfinite(yy)) {
        istemciye_hata(c, "Taşınamadı.");
        return;
    }
    hedefe_oturt(h, &x, &yy, g, y);
    istekleri_temizle(k->id);
    gm_tasi(k, x, yy);
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

/* Yeni karakter için a'nın yanında boş bir yer: aynı haritada, arada duvar olmayan, duvarların dibinde olmayan,
   başkalarına 0.75 kareden yaklaşmayan en yakın yer (yoksa a'nın yeri). */
static void yanina_yer(HaritaKaydi *h, const Karakter *a, float *ox, float *oy)
{
    float g, y;
    *ox = a->o.x;
    *oy = a->o.y;
    if (!harita_olcu(h, &g, &y)) return;
    Duvarlar *d = duvar_dizini(h);
    static const int yon[8][2] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 }, { 1, 1 }, { -1, 1 }, { -1, -1 }, { 1, -1 } };
    for (int r = 1; r <= 4; r++)
        for (int k = 0; k < 8; k++) {
            double x = a->o.x + yon[k][0] * r, yy = a->o.y + yon[k][1] * r;
            if (x < 0 || yy < 0 || x > g || yy > y || !gorus_acik(h, a->o.x, a->o.y, x, yy)) continue;
            if (d && duvar_yakinlik(d, x, yy) < 0.3) continue;
            int dolu = 0;
            for (int i = 0; i < O.kar_n && !dolu; i++) {
                const Karakter *u = O.kar[i];
                if (bu_haritada(u, h->dosya) && mesafe(x, yy, u->o.x, u->o.y) < 0.75) dolu = 1;
            }
            if (dolu) continue;
            Yol yol; /* yalnızca görünmesi yetmez: oraya yürünebilmeli (dar aralıkların ötesi olmasın) */
            if (!yol_bul(h, a->o.x, a->o.y, x, yy, &yol)) continue;
            int duz = yol.n == 2;
            yol_bosalt(&yol);
            if (!duz) continue;
            *ox = (float)x;
            *oy = (float)yy;
            return;
        }
}

/* Sıraya id'yi once'nin hemen arkasına koyar (once sırada değilse sona) */
static void siraya_arkasina(const char *id, const char *once)
{
    if (sirada_mi(id) || O.sira.n >= AZAMI_SIRA) return;
    int i = 0;
    while (i < O.sira.n && strcmp(O.sira.liste[i], once)) i++;
    if (i == O.sira.n) {
        siraya_ekle(id);
        return;
    }
    memmove(O.sira.liste[i + 2], O.sira.liste[i + 1], (size_t)(O.sira.n - i - 1) * ID_N);
    kopyala(O.sira.liste[i + 1], ID_N, id);
    O.sira.n++;
    if (O.sira.indeks > i) O.sira.indeks++;
    kirlet();
}

/* GM bir NPC'yi kopyaladı: adı, rengi, görünümü, statları, parası ve envanteri aynı olan yeni bir NPC, aynı haritada
   yanında belirir. Asıl NPC sıradaysa kopyası da hemen arkasına girer. Kopya ölü değildir. */
static void m_npc_kopyala(Istemci *c, const Json *j)
{
    Karakter *a = karakter_bul(json_metin(j, "id"));
    if (!a || !a->npc) return;
    HaritaKaydi *h = haritasi(a);
    if (!h) {
        istemciye_hata(c, "Bu NPC'nin haritası yok.");
        return;
    }
    int n = 0;
    for (int i = 0; i < O.kar_n; i++) n += O.kar[i]->npc && bu_haritada(O.kar[i], h->dosya);
    if (n >= AZAMI_NPC) {
        istemciye_hata(c, "Bu haritada çok fazla NPC var.");
        return;
    }
    Karakter *k = karakter_olustur(1, NULL, a->ad, a->renk);
    if (!k) {
        istemciye_hata(c, "Oyunda çok fazla karakter var.");
        return;
    }
    k->o = a->o;
    k->o.ad = k->ad;
    k->o.npc = 1;
    if (k->o.can <= 0) k->o.can = 1;
    k->gizli = a->gizli;
    kopyala(k->resim, sizeof k->resim, a->resim);
    kopyala(k->sapka, sizeof k->sapka, a->sapka);
    k->harita = xstrdup(a->harita);
    yanina_yer(h, a, &k->o.x, &k->o.y);
    agirlik_hesapla(&k->o);
    if (sirada_mi(a->id)) siraya_arkasina(k->id, a->id);
    char m[200];
    snprintf(m, sizeof m, "%s kopyalandı.", k->ad);
    kayit_metin("bilgi", GOR_GM, NULL, m);
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
    if (!k) return;
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
    for (int i = 0; i < 3; i++) {
        Json *v = json_al(j, SIKKE[i]);
        if (v && v->tur == JSON_NUM && isfinite(v->sayi)) *sikke(&k->o, i) = (int)sinirla_d(round(v->sayi), 0, AZAMI_PARA);
    }
    agirlik_hesapla(&k->o);
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
    if (!k || k->olu) return;
    int sirasiydi = siradaki() == k;
    oldur(k);
    if (sirasiydi) tur_bitir_ve_ilerle();
    herkese_durum();
}

static void m_dirilt(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || !k->olu) return;
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
    Json *dy = json_al(j, "dayaniklilik");
    if (dy && dy->tur == JSON_NUM && isfinite(dy->sayi)) e->dayaniklilik = (float)sinirla_d(dy->sayi, -AZAMI_STAT, AZAMI_STAT);
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
    if (!t || a == t || !ayni_haritada(a, t)) {
        istemciye_hata(c, "Saldıran ve hedef aynı haritada olmalı.");
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
    if (!t || !ayni_haritada(a, t)) {
        istemciye_hata(c, "Büyüyü yapan ve hedef aynı haritada olmalı.");
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

/* GM bir karakteri (listeden sürükleyerek) baktığı haritaya koydu. Zaten bu haritadaysa oraya yürür; başka bir
   haritadaysa (ya da hiçbir haritada değilse) buraya geçer. */
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
    if (k->npc && !gm_haritasinda(k) && haritadaki_npc_sayisi() >= AZAMI_NPC) {
        istemciye_hata(c, "Bu haritada çok fazla NPC var.");
        return;
    }
    hedefe_oturt(h, &x, &yy, g, y);
    istekleri_temizle(k->id);
    if (gm_haritasinda(k)) {
        gm_tasi(k, x, yy);
    } else {
        hedef_istegini_dusur(k->id);
        takaslari_bitir(k->id, "Takas bitti: karşı taraf başka bir haritaya geçti.");
        harita_ata(k, h->dosya);
        duvardan_ayir(h, &x, &yy);
        k->o.x = (float)x;
        k->o.y = (float)yy;
        char m[300], ad[AD_N];
        esya_adi(h->dosya, ad, sizeof ad);
        snprintf(m, sizeof m, "%s şu haritaya kondu: %s", k->ad, ad);
        kayit_sahibe("bilgi", k, m);
        bilgileri_guncelle();
        yerden_al(k, NULL);
    }
    herkese_durum();
}

/* GM bir oyuncuyu haritadan kaldırdı: yeniden yerleştirilene kadar hiçbir haritada değildir. */
static void m_haritadan_kaldir(const Json *j)
{
    Karakter *k = karakter_bul(json_metin(j, "id"));
    if (!k || k->npc || !haritada(k)) return;
    harita_ata(k, NULL);
    istekleri_temizle(k->id);
    hedef_istegini_dusur(k->id);
    takaslari_bitir(k->id, "Takas bitti: karşı taraf haritadan çıktı.");
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
        if (!bu_haritada(k, h->dosya) || k->olu) continue;
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
        bilgileri_guncelle();
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
    bilgileri_guncelle();
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
    bilgileri_guncelle();
    herkese_durum();
}

/* Oyuncunun envanterindeki bir eşya: sıra ve (değişmediğini doğrulamak için) ad. Yumruksa ya da bulunamazsa
   nedenini yazar, -1 döner. */
static int oyuncu_esyasi(Istemci *c, Karakter *ben, const Json *j, const char *yumruk_hatasi)
{
    int i;
    if (!tamsayi_al(j, "sira", &i) || i < 0 || i >= ben->o.envanter_adet) return -1;
    const char *ad = json_metin(j, "ad");
    if (ad && strcmp(ad, ben->o.envanter[i].ad)) {
        istemciye_hata(c, "Envanterin değişti; yeniden dene.");
        durum_gonder(c);
        return -1;
    }
    if (ben->o.envanter[i].sabit) {
        istemciye_hata(c, yumruk_hatasi);
        return -1;
    }
    return i;
}

/* Oyuncu bir eşyasını yok etti (yumruk yok edilemez). */
static void m_esya_imha(Istemci *c, Karakter *ben, const Json *j)
{
    int i = oyuncu_esyasi(c, ben, j, "Yumruk yok edilemez.");
    if (i < 0) return;
    char m[300];
    snprintf(m, sizeof m, "%s bir eşyayı yok etti: %s", ben->ad, ben->o.envanter[i].ad);
    envanter_cikar(&ben->o, i);
    kayit_sahibe("esya", ben, m);
    herkese_durum();
}

/* Oyuncu bir eşyasını bulunduğu yere bıraktı (yumruk bırakılamaz). */
static void m_esya_yere(Istemci *c, Karakter *ben, const Json *j)
{
    HaritaKaydi *h = haritasi(ben);
    int i;
    if (!h || h->gizli) {
        istemciye_hata(c, "Eşya bırakmak için haritada olmalısın.");
        return;
    }
    if (ben->olu) {
        istemciye_hata(c, "Ölüler eşya bırakamaz.");
        return;
    }
    if ((i = oyuncu_esyasi(c, ben, j, "Yumruk bırakılamaz.")) < 0) return;
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
    bilgileri_guncelle();
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

static int esya_yukle(const Json *o, Esya *e);

/* ------------------------------------------------------------------ ölünün üstünü aramak */

/* Arayan yaşıyor, ölü aynı haritada, görüşte ve yakında olmalı. Olmazsa nedenini yazar, NULL döner. */
static Karakter *yagma_hedefi(Istemci *c, Karakter *ben, const Json *j)
{
    Karakter *t = karakter_bul(json_metin(j, "hedef"));
    if (!t || t == ben || !goruyor(ben, t)) {
        istemciye_hata(c, "Bu karakteri göremiyorsun.");
        return NULL;
    }
    if (!t->olu) {
        istemciye_hata(c, "Yalnızca ölülerin üstü aranabilir.");
        return NULL;
    }
    if (ben->olu) {
        istemciye_hata(c, "Öldün; GM seni diriltebilir.");
        return NULL;
    }
    if (mesafe(ben->o.x, ben->o.y, t->o.x, t->o.y) > YAGMA_MESAFESI) {
        istemciye_hata(c, "Üstünü aramak için ona yaklaş.");
        return NULL;
    }
    if (!gorus_acik(haritasi(ben), ben->o.x, ben->o.y, t->o.x, t->o.y)) {
        istemciye_hata(c, "Aranızda duvar var; yanına git.");
        return NULL;
    }
    return t;
}

/* {"t":"ganimet","hedef":..,"ad":..,"para":{..},"envanter":[{"sira":..,"esya":{...}}]} (yumruk alınamaz, gösterilmez) */
static void ganimet_gonder(Istemci *c, const Karakter *ben, const Karakter *t)
{
    Buf b;
    buf_init(&b);
    buf_puts(&b, "{\"t\":\"ganimet\",\"hedef\":");
    buf_json_str(&b, t->id);
    buf_puts(&b, ",\"ad\":");
    buf_json_str(&b, t->ad);
    int para[3];
    oyuncu_parasi(&t->o, para);
    para_alani(&b, para);
    buf_puts(&b, ",\"envanter\":[");
    int ilk = 1;
    for (int i = 0; i < t->o.envanter_adet; i++) {
        if (t->o.envanter[i].sabit) continue;
        buf_printf(&b, "%s{\"sira\":%d,\"esya\":", ilk ? "" : ",", i);
        ilk = 0;
        esya_json(&b, &t->o.envanter[i]);
        buf_putc(&b, '}');
        resim_izni_ver(ben, t->o.envanter[i].resim);
    }
    buf_puts(&b, "]}");
    gonder_buf(c, &b);
    buf_free(&b);
}

/* Para metni: {4,12,3} → "4 altın 12 gümüş 3 bakır", hepsi 0 → "0 bakır" (BUF'a ekler) */
static void para_yaz(Buf *b, const int p[3])
{
    const char *ara = "";
    for (int i = 0; i < 3; i++) {
        if (!p[i]) continue;
        buf_printf(b, "%s%d %s", ara, p[i], SIKKE_ADI[i]);
        ara = " ";
    }
    if (!*ara) buf_puts(b, "0 bakır");
}

static void m_ganimet_bak(Istemci *c, Karakter *ben, const Json *j)
{
    Karakter *t = yagma_hedefi(c, ben, j);
    if (t) ganimet_gonder(c, ben, t);
}

/* Ölünün bir eşyasını (sira, ad), parasını (para: true) ya da hepsini (hepsi: true) alır. */
static void m_ganimet_al(Istemci *c, Karakter *ben, const Json *j)
{
    Karakter *t = yagma_hedefi(c, ben, j);
    if (!t) return;
    Buf alinan;
    buf_init(&alinan);
    int dolu = 0;
    int hepsi = json_mantik(j, "hepsi", 0);
    int para[3];
    oyuncu_parasi(&t->o, para);
    if ((hepsi || json_mantik(j, "para", 0)) && (para[0] || para[1] || para[2])) {
        for (int i = 0; i < 3; i++) {
            long long yeni = (long long)sikke_oku(&ben->o, i) + para[i];
            *sikke(&ben->o, i) = (int)(yeni > AZAMI_PARA ? AZAMI_PARA : yeni);
            *sikke(&t->o, i) = 0;
        }
        if (alinan.len) buf_puts(&alinan, ", ");
        para_yaz(&alinan, para);
        kirlet();
    }
    int i;
    if (!hepsi && tamsayi_al(j, "sira", &i)) {
        const char *ad = json_metin(j, "ad");
        if (i < 0 || i >= t->o.envanter_adet || t->o.envanter[i].sabit || (ad && strcmp(ad, t->o.envanter[i].ad))) {
            istemciye_hata(c, "Bu eşya artık orada değil.");
        } else if (envantere_koy(ben, &t->o.envanter[i])) {
            buf_printf(&alinan, "%s%s", alinan.len ? ", " : "", t->o.envanter[i].ad);
            envanter_cikar(&t->o, i);
        } else {
            dolu = 1;
        }
    }
    for (i = t->o.envanter_adet - 1; hepsi && i >= 0; i--) {
        if (t->o.envanter[i].sabit) continue;
        if (!envantere_koy(ben, &t->o.envanter[i])) {
            dolu = 1;
            break;
        }
        buf_printf(&alinan, "%s%s", alinan.len ? ", " : "", t->o.envanter[i].ad);
        envanter_cikar(&t->o, i);
    }
    if (dolu) istemciye_hata(c, "Envanterin dolu; hepsini alamadın.");
    if (alinan.len) {
        char sahip[2 * ID_N + 4], m[700];
        snprintf(sahip, sizeof sahip, t->npc ? ",%s," : ",%s,%s,", ben->id, t->id);
        snprintf(m, sizeof m, "%s, %s üstünden aldı: %s", ben->ad, t->ad, alinan.p);
        kayit_metin("esya", GOR_GM_SAHIP, sahip, m);
    }
    buf_free(&alinan);
    ganimet_gonder(c, ben, t);
    herkese_durum();
}

/* ------------------------------------------------------------------ takas */

static Takas *takas_bul(long long id)
{
    for (int i = 0; i < O.takas_n; i++)
        if (O.takas[i].id == id) return &O.takas[i];
    return NULL;
}

/* kim'in sürmekte olan takası; taraf: 0 başlatan, 1 karşı taraf */
static Takas *takas_kimin(const char *kim, int *taraf)
{
    for (int i = 0; i < O.takas_n; i++)
        for (int s = 0; s < 2; s++)
            if (!strcmp(O.takas[i].kim[s], kim)) {
                if (taraf) *taraf = s;
                return &O.takas[i];
            }
    return NULL;
}

static void takas_kaldir(Takas *t)
{
    int i = (int)(t - O.takas);
    O.takas[i] = O.takas[--O.takas_n];
}

/* kim'in takaslarını bitirir; karşı taraf oyuncuysa nedenini görür. */
static void takaslari_bitir(const char *kim, const char *neden)
{
    int s;
    Takas *t;
    while ((t = takas_kimin(kim, &s))) {
        Karakter *karsi = karakter_bul(t->kim[1 - s]);
        if (karsi && !karsi->npc && neden) oyuncuya_bildir(karsi, "bilgi", neden);
        takas_kaldir(t);
    }
}

/* Takastaki iki eşya aynı türden mi (adet tanesi verilecek tür) */
static int esya_ayni(const Esya *a, const Esya *b)
{
    return esya_esit(a, b) && a->agirlik == b->agirlik && a->dayaniklilik == b->dayaniklilik && !a->sabit && !b->sabit;
}

static int esya_sayisi(const Oyuncu *o, const Esya *e)
{
    int n = 0;
    for (int i = 0; i < o->envanter_adet; i++) n += esya_ayni(&o->envanter[i], e);
    return n;
}

static void takas_yuvasi_yaz(Buf *b, const TakasYuvasi *y, int ilk)
{
    if (!ilk) buf_putc(b, ',');
    if (y->tur == 1) {
        buf_printf(b, "{\"tur\":\"esya\",\"adet\":%d,\"esya\":", y->adet);
        esya_json(b, &y->e);
        buf_putc(b, '}');
    } else if (y->tur == 2) {
        buf_puts(b, "{\"tur\":\"para\"");
        para_alani(b, y->para);
        buf_putc(b, '}');
    } else {
        buf_puts(b, "null");
    }
}

static void takas_tarafi_yaz(Buf *b, const char *ad, const Karakter *k)
{
    buf_printf(b, ",\"%s\":{\"id\":", ad);
    buf_json_str(b, k ? k->id : "");
    buf_puts(b, ",\"ad\":");
    buf_json_str(b, k ? k->ad : "?");
    buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s}", k ? k->renk : "#999999", k && k->npc ? "true" : "false");
}

/* Oyuncuya kendi takası: ,"takas":{"id":..,"karsi":{..},"benim":[4],"onun":[4],"onayim":..,"onayi":..} ya da null.
   GM'e (ben NULL) hepsi: ,"takaslar":[{"id":..,"taraflar":[{..},{..}],"yuva":[[4],[4]],"onay":[..]}] */
static void takaslar_yaz(Buf *b, const Karakter *ben)
{
    if (ben) {
        int s;
        Takas *t = takas_kimin(ben->id, &s);
        buf_puts(b, ",\"takas\":");
        if (!t) {
            buf_puts(b, "null");
            return;
        }
        buf_printf(b, "{\"id\":%lld", t->id);
        takas_tarafi_yaz(b, "karsi", karakter_bul(t->kim[1 - s]));
        for (int k = 0; k < 2; k++) {
            int taraf = k ? 1 - s : s;
            buf_printf(b, ",\"%s\":[", k ? "onun" : "benim");
            for (int i = 0; i < TAKAS_YUVA; i++) {
                takas_yuvasi_yaz(b, &t->yuva[taraf][i], i == 0);
                if (k && t->yuva[taraf][i].tur == 1) resim_izni_ver(ben, t->yuva[taraf][i].e.resim); /* karşının resmi */
            }
            buf_putc(b, ']');
        }
        buf_printf(b, ",\"onayim\":%s,\"onayi\":%s}", t->onay[s] ? "true" : "false", t->onay[1 - s] ? "true" : "false");
        return;
    }
    buf_puts(b, ",\"takaslar\":[");
    for (int n = 0; n < O.takas_n; n++) {
        Takas *t = &O.takas[n];
        buf_printf(b, "%s{\"id\":%lld,\"taraflar\":[", n ? "," : "", t->id);
        for (int s = 0; s < 2; s++) {
            Karakter *k = karakter_bul(t->kim[s]);
            if (s) buf_putc(b, ',');
            buf_puts(b, "{\"id\":");
            buf_json_str(b, t->kim[s]);
            buf_puts(b, ",\"ad\":");
            buf_json_str(b, k ? k->ad : "?");
            buf_printf(b, ",\"npc\":%s,\"renk\":\"%s\"}", k && k->npc ? "true" : "false", k ? k->renk : "#999999");
        }
        buf_puts(b, "],\"yuva\":[");
        for (int s = 0; s < 2; s++) {
            buf_puts(b, s ? ",[" : "[");
            for (int i = 0; i < TAKAS_YUVA; i++) takas_yuvasi_yaz(b, &t->yuva[s][i], i == 0);
            buf_putc(b, ']');
        }
        buf_printf(b, "],\"onay\":[%s,%s]}", t->onay[0] ? "true" : "false", t->onay[1] ? "true" : "false");
    }
    buf_putc(b, ']');
}

/* İstemcinin bu takastaki tarafı: oyuncu kendi tarafı; GM, takastaki NPC'nin tarafı. Yoksa -1. */
static int takas_tarafi(const Istemci *c, const Karakter *ben, const Takas *t)
{
    for (int s = 0; s < 2; s++) {
        Karakter *k = karakter_bul(t->kim[s]);
        if (!k) continue;
        if (c->gm ? k->npc : (ben && k == ben)) return s;
    }
    return -1;
}

static void takas_bildir(const Takas *t, const char *metin)
{
    for (int s = 0; s < 2; s++) {
        Karakter *k = karakter_bul(t->kim[s]);
        if (k && !k->npc) oyuncuya_bildir(k, "bilgi", metin);
    }
}

/* Takas başlar: oyuncu kendisi adına, GM bir NPC adına (yapan). Karşı taraf görüşte, aynı haritada olmalı. */
static void m_takas_baslat(Istemci *c, Karakter *ben, const Json *j)
{
    Karakter *a = c->gm ? karakter_bul(json_metin(j, "yapan")) : ben;
    Karakter *b = karakter_bul(json_metin(j, "hedef"));
    if (!a || !b || a == b || (c->gm && !a->npc)) {
        istemciye_hata(c, "Takas başlatılamadı.");
        return;
    }
    if (a->npc && b->npc) {
        istemciye_hata(c, "İki NPC arasında takas olmaz; birinin envanterinden ötekine sürükle.");
        return;
    }
    if (!ayni_haritada(a, b) || (!a->npc && !goruyor(a, b)) || (!b->npc && !goruyor(b, a))) {
        istemciye_hata(c, "Takas için ikiniz aynı haritada olmalı ve birbirinizi görmelisiniz.");
        return;
    }
    if (a->olu || b->olu) {
        istemciye_hata(c, "Ölülerle takas olmaz; ölünün üstü aranır.");
        return;
    }
    if (takas_kimin(a->id, NULL) || takas_kimin(b->id, NULL)) {
        istemciye_hata(c, "Biriniz zaten bir takasta.");
        return;
    }
    if (!b->npc && b->baglanti <= 0) {
        istemciye_hata(c, "Karşı taraf şu an oyunda değil.");
        return;
    }
    if (O.takas_n >= AZAMI_TAKAS) {
        istemciye_hata(c, "Şu an çok fazla takas var; biraz bekle.");
        return;
    }
    Takas *t = &O.takas[O.takas_n++];
    memset(t, 0, sizeof *t);
    t->id = ++O.takas_sayac;
    kopyala(t->kim[0], ID_N, a->id);
    kopyala(t->kim[1], ID_N, b->id);
    char m[200];
    snprintf(m, sizeof m, "%s seninle takas yapmak istiyor.", a->ad);
    if (!b->npc) oyuncuya_bildir(b, "bilgi", m);
    herkese_durum();
}

/* Bir tarafın teklifi: en fazla 4 yuva; her yuva bir eşya türü (adet) ya da para. Teklif değişince onaylar düşer. */
static void m_takas_teklif(Istemci *c, Karakter *ben, const Json *j)
{
    long long id = 0;
    json_tamsayi(j, "id", &id);
    Takas *t = takas_bul(id);
    int s = t ? takas_tarafi(c, ben, t) : -1;
    if (s < 0) {
        istemciye_hata(c, "Bu takas artık yok.");
        return;
    }
    Karakter *k = karakter_bul(t->kim[s]);
    Json *liste = json_al(j, "yuvalar");
    if (!k || !liste || liste->tur != JSON_ARR || liste->adet > TAKAS_YUVA) return;
    TakasYuvasi yeni[TAKAS_YUVA];
    memset(yeni, 0, sizeof yeni);
    int para_var = 0;
    for (size_t i = 0; i < liste->adet; i++) {
        const Json *y = liste->ogeler[i];
        const char *tur = json_metin(y, "tur");
        if (!tur) continue;
        if (!strcmp(tur, "para")) {
            const Json *pj = json_al(y, "para");
            int p[3], toplam = 0;
            for (int s = 0; s < 3; s++) {
                double v = json_sayi(pj, SIKKE[s], 0);
                p[s] = isfinite(v) && v >= 1 ? (int)fmin(v, AZAMI_PARA) : 0;
                toplam |= p[s];
            }
            if (para_var || !toplam) continue;
            for (int s = 0; s < 3; s++)
                if (p[s] > sikke_oku(&k->o, s)) {
                    char m[100];
                    snprintf(m, sizeof m, "O kadar %s paran yok.", SIKKE_ADI[s]);
                    istemciye_hata(c, m);
                    return;
                }
            yeni[i].tur = 2;
            memcpy(yeni[i].para, p, sizeof p);
            para_var = 1;
        } else if (!strcmp(tur, "esya")) {
            Esya e;
            if (!esya_yukle(json_al(y, "esya"), &e)) continue;
            int adet;
            if (!tamsayi_al(y, "adet", &adet) || adet < 1) adet = 1;
            int var = esya_sayisi(&k->o, &e);
            for (size_t m = 0; m < i; m++)
                if (yeni[m].tur == 1 && esya_ayni(&yeni[m].e, &e)) var = -1;
            if (var < 0) {
                istemciye_hata(c, "Aynı eşyayı iki yuvaya koyma; adedini artır.");
                return;
            }
            if (var < adet) {
                istemciye_hata(c, var ? "O eşyadan o kadar yok." : "Bu eşya envanterinde yok.");
                return;
            }
            yeni[i].tur = 1;
            yeni[i].e = e;
            yeni[i].adet = adet;
        }
    }
    memcpy(t->yuva[s], yeni, sizeof yeni);
    t->onay[0] = t->onay[1] = 0;
    herkese_durum();
}

/* İki taraf da onaylayınca: teklifler hâlâ geçerliyse ve iki envanterde de yer varsa eşyalar ve para el değiştirir. */
static void takas_yap(Takas *t)
{
    Karakter *k[2] = { karakter_bul(t->kim[0]), karakter_bul(t->kim[1]) };
    const char *sorun = NULL;
    if (!k[0] || !k[1] || k[0]->olu || k[1]->olu || !ayni_haritada(k[0], k[1])) sorun = "taraflardan biri artık takas yapamıyor";
    int giden[2] = { 0, 0 };
    for (int s = 0; s < 2 && !sorun; s++) {
        long long para[3] = { 0, 0, 0 };
        for (int i = 0; i < TAKAS_YUVA; i++) {
            const TakasYuvasi *y = &t->yuva[s][i];
            if (y->tur == 1) {
                if (esya_sayisi(&k[s]->o, &y->e) < y->adet) sorun = "teklif edilen bir eşya artık envanterde yok";
                giden[s] += y->adet;
            } else if (y->tur == 2) {
                for (int q = 0; q < 3; q++) para[q] += y->para[q];
            }
        }
        for (int q = 0; q < 3; q++)
            if (para[q] > sikke_oku(&k[s]->o, q)) sorun = "teklif edilen para artık yok";
    }
    for (int s = 0; s < 2 && !sorun; s++)
        if (k[s]->o.envanter_adet - giden[s] + giden[1 - s] > ENVANTER_AZAMI) sorun = "bir tarafın envanterinde yer yok";
    if (sorun) {
        char m[200];
        snprintf(m, sizeof m, "Takas yapılamadı: %s. Teklifleri düzeltip yeniden onaylayın.", sorun);
        takas_bildir(t, m);
        t->onay[0] = t->onay[1] = 0;
        return;
    }
    /* Önce iki taraftan da çıkar, sonra karşıya ekle: yer sayımı yukarıda yapıldı */
    Esya cikan[2][ENVANTER_AZAMI];
    int cikan_n[2] = { 0, 0 };
    Buf ozet[2];
    for (int s = 0; s < 2; s++) {
        buf_init(&ozet[s]);
        for (int i = 0; i < TAKAS_YUVA; i++) {
            const TakasYuvasi *y = &t->yuva[s][i];
            if (y->tur == 1) {
                int kalan = y->adet;
                for (int m = k[s]->o.envanter_adet - 1; m >= 0 && kalan > 0; m--) {
                    if (!esya_ayni(&k[s]->o.envanter[m], &y->e)) continue;
                    cikan[s][cikan_n[s]++] = k[s]->o.envanter[m];
                    envanter_cikar(&k[s]->o, m);
                    kalan--;
                }
                buf_printf(&ozet[s], "%s%d × %s", ozet[s].len ? ", " : "", y->adet, y->e.ad);
            } else if (y->tur == 2) {
                for (int q = 0; q < 3; q++) {
                    *sikke(&k[s]->o, q) -= y->para[q];
                    long long yeni = (long long)sikke_oku(&k[1 - s]->o, q) + y->para[q];
                    *sikke(&k[1 - s]->o, q) = (int)(yeni > AZAMI_PARA ? AZAMI_PARA : yeni);
                }
                if (ozet[s].len) buf_puts(&ozet[s], ", ");
                para_yaz(&ozet[s], y->para);
            }
        }
    }
    for (int s = 0; s < 2; s++)
        for (int i = 0; i < cikan_n[s]; i++) envantere_koy(k[1 - s], &cikan[s][i]);
    char sahip[2 * ID_N + 4], m[1200];
    snprintf(sahip, sizeof sahip, ",%s,%s,", k[0]->npc ? "-" : k[0]->id, k[1]->npc ? "-" : k[1]->id);
    snprintf(m, sizeof m, "Takas: %s verdi: %s; %s verdi: %s", k[0]->ad, ozet[0].len ? ozet[0].p : "hiçbir şey", k[1]->ad,
             ozet[1].len ? ozet[1].p : "hiçbir şey");
    kayit_metin("esya", GOR_GM_SAHIP, sahip, m);
    buf_free(&ozet[0]);
    buf_free(&ozet[1]);
    takas_bildir(t, "Takas tamamlandı.");
    takas_kaldir(t);
    kirlet();
}

static void m_takas_onay(Istemci *c, Karakter *ben, const Json *j)
{
    long long id = 0;
    json_tamsayi(j, "id", &id);
    Takas *t = takas_bul(id);
    int s = t ? takas_tarafi(c, ben, t) : -1;
    if (s < 0) {
        istemciye_hata(c, "Bu takas artık yok.");
        return;
    }
    t->onay[s] = json_mantik(j, "onay", 1);
    if (t->onay[0] && t->onay[1]) takas_yap(t);
    herkese_durum();
}

static void m_takas_iptal(Istemci *c, Karakter *ben, const Json *j)
{
    long long id = 0;
    json_tamsayi(j, "id", &id);
    Takas *t = takas_bul(id);
    int s = t ? takas_tarafi(c, ben, t) : -1;
    if (s < 0) return;
    Karakter *karsi = karakter_bul(t->kim[1 - s]), *k = karakter_bul(t->kim[s]);
    char m[200];
    snprintf(m, sizeof m, "%s takastan vazgeçti.", k ? k->ad : "Karşı taraf");
    if (karsi && !karsi->npc) oyuncuya_bildir(karsi, "bilgi", m);
    takas_kaldir(t);
    herkese_durum();
}

/* ------------------------------------------------------------------ portallar (GM) */

/* GM baktığı haritada bir yere portal koydu; düzenleyicisini açabilsin diye kimliği ona döner. */
static void m_portal_koy(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    float g, y;
    if (!h || !harita_olcu(h, &g, &y)) {
        istemciye_hata(c, "Önce bir harita aç.");
        return;
    }
    if (h->portal_n >= AZAMI_PORTAL) {
        istemciye_hata(c, "Bu haritada çok fazla portal var.");
        return;
    }
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    if (!isfinite(x) || !isfinite(yy)) return;
    hedefe_oturt(h, &x, &yy, g, y);
    h->portal = xrealloc(h->portal, (size_t)(h->portal_n + 1) * sizeof *h->portal);
    Portal *p = &h->portal[h->portal_n++];
    memset(p, 0, sizeof *p);
    p->id = ++O.yer_sayac;
    snprintf(p->ad, sizeof p->ad, "Portal %d", h->portal_n);
    p->x = (float)x;
    p->y = (float)yy;
    kirlet();
    herkese_durum();
    Buf b;
    buf_init(&b);
    buf_printf(&b, "{\"t\":\"portal_eklendi\",\"id\":%lld}", p->id);
    gonder_buf(c, &b);
    buf_free(&b);
}

/* Portalın adı, yeri ya da hedefi. Hedef: harita (""/null: bağlı değil) ve oradaki yer (hx, hy) ya da oradaki bir
   portal (hedef_portal: o portalın yerine çıkar). */
static void m_portal_guncelle(Istemci *c, const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    long long id = 0;
    json_tamsayi(j, "id", &id);
    int i = portal_sira(h, id);
    if (i < 0) {
        istemciye_hata(c, "Bu portal artık yok.");
        return;
    }
    Portal *p = &h->portal[i];
    const char *ad = json_metin(j, "ad");
    char temiz[AD_N];
    if (ad && metin_temizle(ad, temiz, sizeof temiz, 40)) kirp_onar(p->ad, sizeof p->ad, temiz);
    double x = json_sayi(j, "x", NAN), yy = json_sayi(j, "y", NAN);
    float g, y;
    if (isfinite(x) && isfinite(yy) && harita_olcu(h, &g, &y)) {
        hedefe_oturt(h, &x, &yy, g, y);
        p->x = (float)x;
        p->y = (float)yy;
    }
    Json *hd = json_al(j, "hedef");
    if (hd && hd->tur == JSON_NULL) {
        free(p->hedef);
        p->hedef = NULL;
    } else if (hd && hd->tur == JSON_STR) {
        const char *dosya = hd->metin;
        char yol[YOL_AZAMI], goreli[YOL_AZAMI];
        GorselBilgi gb;
        snprintf(goreli, sizeof goreli, "haritalar/%s", dosya);
        if (!dosya[0]) {
            free(p->hedef);
            p->hedef = NULL;
        } else if (!goreli_yol_gecerli(dosya) || !tam_yol_kur(yap.oyun_klasoru, goreli, yol, sizeof yol) || !gorsel_bilgi(yol, &gb)) {
            istemciye_hata(c, "Hedef harita bulunamadı.");
            return;
        } else {
            HaritaKaydi *hh = harita_bul(dosya, 1); /* h kayabilir: p'yi yeniden bul */
            h = aktif_harita();
            p = &h->portal[portal_sira(h, id)];
            float hg = 0, hy2 = 0;
            harita_olcu(hh, &hg, &hy2);
            free(p->hedef);
            p->hedef = xstrdup(hh->dosya);
            long long hp = 0;
            int k = json_tamsayi(j, "hedef_portal", &hp) ? portal_sira(hh, hp) : -1;
            if (k >= 0) {
                p->hx = hh->portal[k].x;
                p->hy = hh->portal[k].y;
            } else {
                p->hx = (float)sinirla_d(json_sayi(j, "hx", hg / 2), 0, hg);
                p->hy = (float)sinirla_d(json_sayi(j, "hy", hy2 / 2), 0, hy2);
            }
        }
    }
    kirlet();
    herkese_durum();
}

static void m_portal_sil(const Json *j)
{
    HaritaKaydi *h = aktif_harita();
    long long id = 0;
    json_tamsayi(j, "id", &id);
    int i = portal_sira(h, id);
    if (i < 0) return;
    free(h->portal[i].hedef);
    memmove(&h->portal[i], &h->portal[i + 1], (size_t)(h->portal_n - i - 1) * sizeof *h->portal);
    h->portal_n--;
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

/* Maçı sıfırlar: NPC'ler, oyuncuların statları, envanterleri (yumruk kalır), paraları, haritaları, ölümleri, keşifler,
   yerdeki eşyalar, takaslar, tur sırası ve kayıt silinir. Ayarlar, haritaların boyutları, portallar ve oyuncuların
   kendileri (adları, oturumları, görünümleri) kalır. Önceki hali durum-sifirlamadan-once.json'a yazılır. */
static void m_mac_sifirla(void)
{
    durum_yedekle("durum-sifirlamadan-once.json");
    O.sira.aktif = 0; /* NPC'ler silinirken sıra ilerlemesin */
    O.sira.indeks = -1;
    O.istek_var = 0;
    O.takas_n = 0;
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
        harita_ata(k, NULL);
        o->x = o->y = 0;
        o->can = VARSAYILAN_CAN;
        o->odak = o->zirh = o->buyu_direnci = 0;
        o->enerji = O.ayar.tur_enerjisi;
        o->altin = o->gumus = o->bakir = 0;
        o->envanter_adet = 0;
        yumruk_ver(o);
        agirlik_hesapla(o);
    }
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
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
        else if (!strcmp(t, "esya_imha")) m_esya_imha(c, ben, j);
        else if (!strcmp(t, "ganimet_bak")) m_ganimet_bak(c, ben, j);
        else if (!strcmp(t, "ganimet_al")) m_ganimet_al(c, ben, j);
        else if (!strcmp(t, "takas_baslat")) m_takas_baslat(c, ben, j);
        else if (!strcmp(t, "takas_teklif")) m_takas_teklif(c, ben, j);
        else if (!strcmp(t, "takas_onay")) m_takas_onay(c, ben, j);
        else if (!strcmp(t, "takas_iptal")) m_takas_iptal(c, ben, j);
        else if (!strcmp(t, "gorunum")) m_gorunum(c, ben, j);
        else istemciye_hata(c, "Bunu yalnızca GM yapabilir.");
    } else if (!strcmp(t, "istek_onayla")) m_istek_onayla(c, j);
    else if (!strcmp(t, "istek_reddet")) m_istek_reddet(c, j);
    else if (!strcmp(t, "harita_ac")) m_harita_ac(c, j);
    else if (!strcmp(t, "harita_gizle")) {
        HaritaKaydi *h = aktif_harita();
        if (h) {
            h->gizli = json_mantik(j, "gizli", h->gizli);
            Karakter *ik = O.istek_var ? karakter_bul(O.istek.kim) : NULL;
            if (h->gizli && ik && bu_haritada(ik, h->dosya)) istekleri_temizle(NULL);
            kirlet();
            herkese_durum();
        }
    } else if (!strcmp(t, "izgara")) m_izgara(c, j);
    else if (!strcmp(t, "kesif_sifirla")) m_kesif_sifirla();
    else if (!strcmp(t, "tasi")) m_tasi(c, j);
    else if (!strcmp(t, "npc_ekle")) m_npc_ekle(c);
    else if (!strcmp(t, "npc_kopyala")) m_npc_kopyala(c, j);
    else if (!strcmp(t, "duvar_ekle")) m_duvar_ekle(c, j);
    else if (!strcmp(t, "duvar_sil")) m_duvar_sil(j);
    else if (!strcmp(t, "duvarlari_sil")) m_duvarlari_sil();
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
    else if (!strcmp(t, "takas_baslat")) m_takas_baslat(c, ben, j);
    else if (!strcmp(t, "takas_teklif")) m_takas_teklif(c, ben, j);
    else if (!strcmp(t, "takas_onay")) m_takas_onay(c, ben, j);
    else if (!strcmp(t, "takas_iptal")) m_takas_iptal(c, ben, j);
    else if (!strcmp(t, "portal_koy")) m_portal_koy(c, j);
    else if (!strcmp(t, "portal_guncelle")) m_portal_guncelle(c, j);
    else if (!strcmp(t, "portal_sil")) m_portal_sil(j);
    else if (!strcmp(t, "mac_sifirla")) m_mac_sifirla();
    else if (!strcmp(t, "ayarlar")) m_ayarlar(c, j);
    else if (!strcmp(t, "oyuncu_sil")) m_oyuncu_sil(j);
son:
    json_sil(j);
}

/* ================================================================== dosya yöneticisiyle uyum */

int oyuncu_dosya_izni(const char *oyuncu, const char *goreli)
{
    /* Token resimleri ve şapkalar herkesin seçebileceği görseller */
    if (!strncmp(goreli, "tokenlar/", 9) || !strncmp(goreli, "sapkalar/", 9)) return 1;
    Karakter *k = karakter_bul(oyuncu);
    if (!k || k->npc) return 0;
    HaritaKaydi *h = haritasi(k);
    /* Kendi haritasının resmi (gizli değilse) */
    if (h && !h->gizli && !strncmp(goreli, "haritalar/", 10) && !strcmp(goreli + 10, h->dosya)) return 1;
    for (int i = 0; i < k->o.envanter_adet; i++)
        if (!strcmp(k->o.envanter[i].resim, goreli)) return 1;
    /* Yerde gördüğü ya da gördüğünü hatırladığı eşyalar */
    for (int i = 0; h && i < h->yer_n; i++)
        if (!strcmp(h->yer[i].e.resim, goreli) && yer_goruyor(k, h, &h->yer[i])) return 1;
    Bilgi *bl = h ? bilgi_bul(h, k->id, 0) : NULL;
    for (int i = 0; bl && i < bl->esya_n; i++)
        if (!strcmp(bl->esya[i].e.resim, goreli)) return 1;
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
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
        for (int k = 0; k < h->yer_n; k++) esya_yolu_degisti(&h->yer[k].e, eski, yeni);
        for (int b = 0; b < h->bilgi_n; b++)
            for (int k = 0; k < h->bilgi[b].esya_n; k++) esya_yolu_degisti(&h->bilgi[b].esya[k].e, eski, yeni);
    }
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
            for (int k = 0; k < O.kar_n; k++)
                if (bu_haritada(O.kar[k], h->dosya)) harita_ata(O.kar[k], yd);
            /* Buraya çıkan portallar yeni adı göstersin */
            for (int m = 0; m < O.harita_n; m++)
                for (int k = 0; k < O.harita[m].portal_n; k++) {
                    Portal *p = &O.harita[m].portal[k];
                    if (p->hedef && !strcmp(p->hedef, h->dosya)) {
                        free(p->hedef);
                        p->hedef = xstrdup(yd);
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
    if (s && !haritada(s)) sira_ilerle();
    bilgileri_guncelle();
    herkese_durum();
}

void dosyalar_degisti(void)
{
    onbellegi_bosalt();
    herkese_durum();
}

/* ================================================================== kalıcılık (durum.json) */

/* durum.json biçiminin sürümü. Başka bir sürümün kaydı okunmaz (kenara alınır, yeni oyun başlar); kurulum zaten
   her seferinde oyun klasörünü siler. Biçim değişince artır. */
#define DURUM_SURUMU 5

/* {"ad":..,"sinif":..,"resim":..,"agirlik":..,"dayaniklilik":..} */
static void esya_kaydi_yaz(Buf *b, const Esya *e)
{
    buf_puts(b, "{\"ad\":");
    buf_json_str(b, e->ad);
    buf_puts(b, ",\"sinif\":");
    buf_json_str(b, e->sinif);
    buf_puts(b, ",\"resim\":");
    buf_json_str(b, e->resim);
    sayi_alani(b, "agirlik", e->agirlik);
    sayi_alani(b, "dayaniklilik", e->dayaniklilik);
    buf_putc(b, '}');
}

static void durum_json_yaz(Buf *b)
{
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
        buf_printf(b, ",\"renk\":\"%s\",\"npc\":%s,\"gizli\":%s,\"olu\":%s,\"altin\":%d,\"gumus\":%d,\"bakir\":%d,\"harita\":",
                   k->renk, k->npc ? "true" : "false", k->gizli ? "true" : "false", k->olu ? "true" : "false", k->o.altin,
                   k->o.gumus, k->o.bakir);
        if (k->harita) buf_json_str(b, k->harita);
        else buf_puts(b, "null");
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
    buf_puts(b, ",\n\"haritalar\":[");
    for (int i = 0; i < O.harita_n; i++) {
        HaritaKaydi *h = &O.harita[i];
        buf_puts(b, i ? ",\n{\"dosya\":" : "\n{\"dosya\":");
        buf_json_str(b, h->dosya);
        buf_printf(b, ",\"gizli\":%s,\"izgara\":", h->gizli ? "true" : "false");
        izgara_json(b, &h->iz);
        buf_puts(b, ",\"bilgi\":[");
        for (int k = 0; k < h->bilgi_n; k++) {
            Bilgi *bl = &h->bilgi[k];
            buf_printf(b, "%s\n{\"oyuncu\":\"%s\",\"kesif\":", k ? "," : "", bl->oyuncu);
            if (bl->kesif.bit) {
                buf_printf(b, "{\"g\":%d,\"y\":%d,\"veri\":\"", bl->kesif.g, bl->kesif.y);
                kesif_kodla(b, &bl->kesif);
                buf_puts(b, "\"}");
            } else {
                buf_puts(b, "null");
            }
            buf_puts(b, ",\"duvarlar\":[");
            for (int m = 0; m < bl->duvar_n; m++) buf_printf(b, "%s%lld", m ? "," : "", bl->duvar[m]);
            buf_puts(b, "],\"bilinen\":[");
            for (int m = 0; m < bl->bilinen_n; m++) {
                const Bilinen *e = &bl->bilinen[m];
                buf_printf(b, "%s{\"id\":\"%s\",\"x\":", m ? "," : "", e->id);
                sayi_yaz(b, e->x);
                sayi_alani(b, "y", e->y);
                buf_printf(b, ",\"olu\":%s}", e->olu > 0 ? "true" : "false");
            }
            buf_puts(b, "],\"esyalar\":[");
            for (int m = 0; m < bl->esya_n; m++) {
                const BilinenEsya *e = &bl->esya[m];
                buf_printf(b, "%s{\"id\":%lld,\"x\":", m ? "," : "", e->id);
                sayi_yaz(b, e->x);
                sayi_alani(b, "y", e->y);
                buf_puts(b, ",\"esya\":");
                esya_kaydi_yaz(b, &e->e);
                buf_putc(b, '}');
            }
            buf_puts(b, "]}");
        }
        buf_puts(b, "],\"yer\":[");
        for (int k = 0; k < h->yer_n; k++) {
            const YerEsyasi *ye = &h->yer[k];
            buf_printf(b, "%s{\"id\":%lld,\"x\":", k ? "," : "", ye->id);
            sayi_yaz(b, ye->x);
            sayi_alani(b, "y", ye->y);
            buf_puts(b, ",\"esya\":");
            esya_kaydi_yaz(b, &ye->e);
            buf_putc(b, '}');
        }
        buf_puts(b, "],\"duvarlar\":[");
        for (int k = 0; k < h->duvar_n; k++) duvar_yaz(b, &h->duvar[k], k == 0);
        buf_puts(b, "],\"portal\":[");
        for (int k = 0; k < h->portal_n; k++) {
            const Portal *p = &h->portal[k];
            buf_printf(b, "%s{\"id\":%lld,\"ad\":", k ? "," : "", p->id);
            buf_json_str(b, p->ad);
            sayi_alani(b, "x", p->x);
            sayi_alani(b, "y", p->y);
            buf_puts(b, ",\"hedef\":");
            if (p->hedef) buf_json_str(b, p->hedef);
            else buf_puts(b, "null");
            sayi_alani(b, "hx", p->hx);
            sayi_alani(b, "hy", p->hy);
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

/* Kayıttaki (ya da takas teklifindeki) bir eşya; ağırlığı yoksa 1, dayanıklılığı yoksa 100 */
static int esya_yukle(const Json *o, Esya *e)
{
    const char *ad = json_metin(o, "ad");
    if (!ad) return 0;
    esya_doldur(e, ad, json_metin(o, "sinif"), json_metin(o, "resim"));
    e->agirlik = json_float(o, "agirlik", 1, 0, AZAMI_STAT);
    e->dayaniklilik = json_float(o, "dayaniklilik", VARSAYILAN_DAYANIKLILIK, -AZAMI_STAT, AZAMI_STAT);
    return 1;
}

/* Kayıttaki envanter (yumruk kaydedilmez, karakter_olustur verir) */
static void envanter_yukle(Karakter *k, const Json *env)
{
    for (size_t i = 0; env && env->tur == JSON_ARR && i < env->adet; i++) {
        Esya e;
        if (esya_yukle(env->ogeler[i], &e)) envantere_koy(k, &e);
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
        int hd_gecerli = hd && hd[0] && goreli_yol_gecerli(hd);
        if (npc && !hd_gecerli) continue;
        Karakter *k = karakter_olustur(npc, id, temiz, renk_gecerli(renk) ? renk : npc ? NPC_RENK[0] : OYUNCU_RENK[0]);
        if (!k) break;
        if (hd_gecerli) k->harita = xstrdup(hd);
        for (int s = 0; s < 3; s++) *sikke(&k->o, s) = (int)sinirla_d(round(json_sayi(o, SIKKE[s], 0)), 0, AZAMI_PARA);
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
        envanter_yukle(k, json_al(o, "envanter"));
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
        h->gizli = json_mantik(o, "gizli", 0);
        const Json *dl = json_al(o, "duvarlar");
        double dg = h->iz.genislik * 1000, dy = h->iz.yukseklik * 1000;
        for (size_t k = 0; dl && dl->tur == JSON_ARR && k < dl->adet && h->duvar_n < AZAMI_DUVAR && dg >= 1000; k++) {
            const Json *w = dl->ogeler[k];
            if (w->tur != JSON_ARR || w->adet != 5) continue;
            double v[5];
            int ok = 1;
            for (int q = 0; q < 5; q++) {
                if (w->ogeler[q]->tur != JSON_NUM || !isfinite(w->ogeler[q]->sayi)) ok = 0;
                else v[q] = w->ogeler[q]->sayi;
            }
            if (!ok || v[0] < 1 || duvar_bul(h, (long long)v[0], NULL)) continue;
            h->duvar = xrealloc(h->duvar, (size_t)(h->duvar_n + 1) * sizeof *h->duvar);
            Duvar *d = &h->duvar[h->duvar_n++];
            d->id = (long long)v[0];
            d->x1 = (int)sinirla_d(round(v[1]), 0, dg);
            d->y1 = (int)sinirla_d(round(v[2]), 0, dy);
            d->x2 = (int)sinirla_d(round(v[3]), 0, dg);
            d->y2 = (int)sinirla_d(round(v[4]), 0, dy);
            if (d->id > O.yer_sayac) O.yer_sayac = d->id;
        }
        const Json *bil = json_al(o, "bilgi");
        for (size_t k = 0; bil && bil->tur == JSON_ARR && k < bil->adet; k++) {
            const Json *bo = bil->ogeler[k];
            const char *oyuncu = json_metin(bo, "oyuncu");
            Karakter *kr = karakter_bul(oyuncu);
            if (!kr || kr->npc || bilgi_bul(h, oyuncu, 0)) continue;
            Bilgi *bl = bilgi_bul(h, oyuncu, 1);
            const Json *ke = json_al(bo, "kesif");
            long long kg, ky;
            if (ke && h->iz.genislik >= 1 && h->iz.yukseklik >= 1) {
                kesif_kur(&bl->kesif, h->iz.genislik, h->iz.yukseklik);
                /* Izgaranın ölçüleri tutmuyorsa (bozuk kayıt) keşif boş başlar */
                if (!json_tamsayi(ke, "g", &kg) || !json_tamsayi(ke, "y", &ky) || kg != bl->kesif.g || ky != bl->kesif.y ||
                    !kesif_coz(&bl->kesif, json_metin(ke, "veri")))
                    memset(bl->kesif.bit, 0, ((size_t)bl->kesif.g * (size_t)bl->kesif.y + 7) / 8);
            }
            const Json *bd = json_al(bo, "duvarlar");
            for (size_t m = 0; bd && bd->tur == JSON_ARR && m < bd->adet; m++) {
                const Json *v = bd->ogeler[m];
                if (v->tur == JSON_NUM && duvar_bul(h, (long long)v->sayi, NULL)) duvar_ogren(bl, (long long)v->sayi);
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
            const Json *es = json_al(bo, "esyalar");
            for (size_t m = 0; es && es->tur == JSON_ARR && m < es->adet && bl->esya_n < AZAMI_BILINEN_ESYA; m++) {
                const Json *e = es->ogeler[m];
                YerEsyasi ye;
                if (!json_tamsayi(e, "id", &ye.id) || ye.id <= 0 || !esya_yukle(json_al(e, "esya"), &ye.e)) continue;
                ye.x = json_float(e, "x", 0, 0, AZAMI_HARITA);
                ye.y = json_float(e, "y", 0, 0, AZAMI_HARITA);
                bilinen_esya_yaz(bl, &ye);
                if (ye.id > O.yer_sayac) O.yer_sayac = ye.id;
            }
        }
        const Json *yer = json_al(o, "yer");
        for (size_t k = 0; yer && yer->tur == JSON_ARR && k < yer->adet; k++) {
            const Json *yo = yer->ogeler[k];
            Esya e;
            long long id;
            if (!esya_yukle(json_al(yo, "esya"), &e)) continue;
            YerEsyasi *ye = yere_koy(h, &e, json_float(yo, "x", 0, 0, AZAMI_HARITA), json_float(yo, "y", 0, 0, AZAMI_HARITA));
            if (ye && json_tamsayi(yo, "id", &id) && id > 0 && yer_esyasi_sira(h, id) < 0) {
                ye->id = id; /* oyuncuların hafızası bu kimliği tutar */
                if (id > O.yer_sayac) O.yer_sayac = id;
            }
        }
        const Json *pl = json_al(o, "portal");
        for (size_t k = 0; pl && pl->tur == JSON_ARR && k < pl->adet && h->portal_n < AZAMI_PORTAL; k++) {
            const Json *po = pl->ogeler[k];
            long long id;
            h->portal = xrealloc(h->portal, (size_t)(h->portal_n + 1) * sizeof *h->portal);
            Portal *p = &h->portal[h->portal_n++];
            memset(p, 0, sizeof *p);
            p->id = json_tamsayi(po, "id", &id) && id > 0 && portal_sira(h, id) < 0 ? id : 0;
            if (p->id > O.yer_sayac) O.yer_sayac = p->id;
            kirp_onar(p->ad, sizeof p->ad, json_metin(po, "ad") ? json_metin(po, "ad") : "Portal");
            p->x = json_float(po, "x", 0, 0, AZAMI_HARITA);
            p->y = json_float(po, "y", 0, 0, AZAMI_HARITA);
            const char *hedef = json_metin(po, "hedef");
            if (hedef && hedef[0] && goreli_yol_gecerli(hedef)) p->hedef = xstrdup(hedef);
            p->hx = json_float(po, "hx", 0, 0, AZAMI_HARITA);
            p->hy = json_float(po, "hy", 0, 0, AZAMI_HARITA);
        }
    }
    /* Kimliği olmayan portallar (bozuk kayıt) yeni kimlik alır; hedef haritası kalmayanlar bağlantısız kalır */
    for (int i = 0; i < O.harita_n; i++)
        for (int k = 0; k < O.harita[i].portal_n; k++) {
            Portal *p = &O.harita[i].portal[k];
            if (!p->id) p->id = ++O.yer_sayac;
            if (p->hedef && !harita_bul(p->hedef, 0)) {
                free(p->hedef);
                p->hedef = NULL;
            }
        }
    /* Haritası olmayan NPC'ler düşer; oyuncular hiçbir haritada kalmaz */
    for (int i = 0; i < O.kar_n;) {
        Karakter *k = O.kar[i];
        if (k->harita && !harita_bul(k->harita, 0)) {
            if (k->npc) {
                karakter_kaldir(k);
                continue;
            }
            harita_ata(k, NULL);
        }
        i++;
    }
    const char *aktif = json_metin(d, "aktif_harita"); /* GM'in baktığı harita */
    if (aktif && harita_bul(aktif, 0)) O.aktif = xstrdup(aktif);

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
        long long surum = 0;
        if (d && d->tur == JSON_OBJ && json_tamsayi(d, "surum", &surum) && surum == DURUM_SURUMU) {
            durum_yukle(d);
        } else { /* bozuk ya da başka bir sürümün kaydı: üzerine yazılmasın, kenara alınsın */
            char yedek[YOL_AZAMI + 64];
            snprintf(yedek, sizeof yedek, "%s/durum-okunmayan-%lld.json", yap.oyun_klasoru, simdi_sn());
            rename(yol, yedek);
            fprintf(stderr, "durum.json %s; %s olarak saklandı, yeni bir oyunla başlanıyor.\n",
                    d && d->tur == JSON_OBJ ? "başka bir sürümün kaydı" : "okunamadı", yedek);
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
    for (int i = 0; i < O.harita_n; i++) sinirlara_al(&O.harita[i]);
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
