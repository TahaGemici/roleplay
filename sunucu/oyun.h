/* Oyunun durumu ve kuralları: karakterler (oyuncular ve NPC'ler), oturumlar, haritalar, görüş sisi,
 * GM onaylı hareket ve saldırı istekleri, tur sırası, envanter, kayıt.
 * Aksi belirtilmedikçe bu dosyadaki fonksiyonlar oyun_kilit tutulurken çağrılır. */
#ifndef OYUN_H
#define OYUN_H

#include <pthread.h>
#include <stddef.h>
#include "dosya.h"

typedef struct {
    char oyun_klasoru[YOL_AZAMI]; /* oyunun bütün dosyaları burada */
    char web_klasoru[YOL_AZAMI];  /* index.html, stil.css, js/ ... */
    char alan_adi[256];           /* yalnızca açılışta GM adresini yazdırmak için */
    char dinle[300];              /* "http://127.0.0.1:3000" gibi, günlük için */
    long long azami_yukleme;      /* bayt */
    char ilk_sifre[100];          /* OYUN_SIFRESI: şifre hiç belirlenmemişse kullanılır */
    char gm_sifresi[200];         /* GM_SIFRESI: GM ekranına girerken sorulur */
    char surum[80];               /* oyunun sürümü: tarayıcılar farklı bir sürümün belleğini silip sayfayı yeniler */
} Yapilandirma;

extern Yapilandirma yap;
extern pthread_mutex_t oyun_kilit;

typedef struct Paket {
    struct Paket *sonraki;
    int tur;
    size_t n;
    char veri[];
} Paket;

typedef struct Istemci {
    int fd;
    int gm;
    char oyuncu[17];
    char oturum[41];
    pthread_mutex_t m;
    pthread_cond_t c;
    Paket *bas, *son;
    size_t bekleyen;
    int kapat;
    /* Aşağıdakiler oyun_kilit altında kullanılır */
    double kova; /* hız sınırı */
    long long kova_ms;
    long long kesif_harita, kesif_surum, duvar_surum; /* hangi haritanın keşfi ve duvarları gönderildi */
    int gecikme; /* bağlantının gidiş-dönüş süresi (ms, yumuşatılmış); -1: henüz ölçülmedi */
    struct Istemci *sonraki;
} Istemci;

/* Başlangıç ve kayıt (kilit tutulmadan çağrılır) */
void oyun_baslat(void);
void oyun_kaydet(int zorla);
void gm_bilgisini_yazdir(void);

/* Giriş ve oturumlar. HTTP durum kodu döner (200 başarılı). */
int oyuncu_giris(const char *ad, const char *sifre, char token[65], char *hata, size_t hn);
int gm_giris(const char *sifre, char token[65]);
int oturum_dogrula(const char *token, int gm, char oyuncu[17], char ozet[41]);
void oturum_sil(const char *token);
int oyun_sifresi_var(void);
void oyun_bilgisi(Buf *b);

/* WebSocket istemcileri */
void istemci_ekle(Istemci *c);
void istemci_cikar(Istemci *c);
void istemci_mesaj(Istemci *c, const char *metin, size_t n);
/* Kuyruğa ekler; kendi kilidini kullanır, oyun_kilit gerekmez. */
void istemciye_gonder(Istemci *c, int tur, const char *veri, size_t n);
/* Yazıcı iş parçacığı: kuyruğu soketle boşaltır; birkaç saniyede bir, yükü gönderildiği an (ms) olan bir ping atar. */
void *istemci_yazici(void *arg);
/* Tarayıcı pinge yanıt verdi (yük: pingin gönderildiği an); bağlantının gecikmesi güncellenir. */
void istemci_pong(Istemci *c, const char *yuk, size_t n);
/* Saniyede bir çağrılır (kilit tutulmadan): birkaç saniyede bir herkesin bağlantı gecikmesi herkese duyurulur. */
void oyun_tik(void);

/* Eşya dosyasının oyun klasörüne göre yolu ("esyalar/sınıf/ad.png") en fazla bu kadar bayt olabilir:
   oyuncunun envanterinde Esya.resim[256] içinde tutulur. Resmin kendisi her boyutta olabilir. */
#define ESYA_YOLU_AZAMI 255

/* Bir oyuncunun kendi pulu için yükleyebileceği en fazla resim (tokenlar/oyuncular/<kimlik>/ altında) */
#define AZAMI_OYUNCU_RESMI 40

/* Dosya erişimi ve dosya yöneticisi */
int oyuncu_dosya_izni(const char *oyuncu, const char *goreli);
int korunan_yol(const char *goreli);
void dosya_tasindi(const char *eski, const char *yeni); /* yeni NULL ise silindi */
void dosyalar_degisti(void);

#endif
