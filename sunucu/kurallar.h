/*
 * Oyun kuralların için arayüz.
 *
 * Sunucu üç fonksiyonunu çağırır; gövdeleri kurallar.c içinde ve onları sen doldurursun:
 *
 *   her_tur(o)                     bir karakterin sırası başladığında
 *   hareket(o, x, y)               GM sırası gelen oyuncunun yürüyüşünü onayladığında
 *   saldiri(saldiran, hedef, esya) GM bir saldırıyı onayladığında
 *
 * Turlar başlamadan kimse yürüyemez ve saldıramaz; üçü de yalnızca tur sırasında çağrılır.
 *
 * Bu dosyadaki tanımlar o fonksiyonlarda kullanabileceğin her şeydir. kurallar.c'yi
 * değiştirdikten sonra programı yeniden derle (make) ve servisi yeniden başlat.
 */
#ifndef KURALLAR_H
#define KURALLAR_H

/* ------------------------------------------------------------------ eşya */

typedef struct {
    char ad[64];     /* "Uzun Kılıç" */
    char sinif[32];  /* "silah". Sınıf adlarını kurallar.c'deki esya_siniflari listesinde sen belirlersin. */
    char resim[256]; /* resmin dosya yolu (resmin kendisi değil), oyun klasörüne göre:
                        "esyalar/silah/Uzun_Kilic.png". Resim dosyası her boyutta olabilir. */
} Esya;

/* ------------------------------------------------------------------ oyuncu */

/* Oyuncuların ve GM'in yerleştirdiği NPC'lerin hepsi bu yapıyı kullanır. */
typedef struct {
    const char *ad;     /* adı (değiştirme) */

    float x, y;         /* konumu, kare biriminde: (0,0) haritanın sol üst köşesi, (0.5, 0.5) ilk karenin
                           ortası. Değiştirirsen karakter oraya taşınır (harita sınırları içinde kalır). */
    float can;          /* 0 ya da altına inerse sırası bittiğinde ölür */
    float odak;
    float zirh;
    float buyu_direnci;
    float enerji;       /* aksiyon puanı. Ne kadar harcanacağını hareket() ve saldiri() içinde sen
                           belirlersin; 0 ya da altına inince sıra geçer. */
    float gorus;        /* görüş menzili, kare. Oyuncu bu uzaklıktan ötesini göremez. */

    Esya *envanter;     /* dinamik dizi: envanter[0] ... envanter[envanter_adet - 1] */
    int envanter_adet;  /* dizideki eşya sayısı */

    int npc;            /* 1: GM'in yönettiği karakter (NPC), 0: oyuncu. Salt okunur. */
} Oyuncu;

/* ------------------------------------------------------------------ harita */

typedef struct {
    float genislik;   /* açık haritanın genişliği, kare biriminde: GM haritayı açarken seçer (harita yoksa 0) */
    float yukseklik;  /* ... ve yüksekliği */
} Harita;

/* Her turda aynı olan bilgiler. Sunucu, fonksiyonlarını çağırmadan hemen önce günceller. */
extern Harita harita;
extern int tur_sayisi; /* kaçıncı tur (1, 2, 3...) */

/* ------------------------------------------------------------------ kurallar.c'de senin yazdıkların */

/* Eşya sınıfları; liste NULL ile biter. Sunucu her sınıf için oyun/esyalar/<sınıf>/ klasörünü açar ve
   GM'in o klasöre koyduğu eşyalar o sınıftan olur. */
extern const char *const esya_siniflari[];

/* Sıra o karaktere geldiğinde. Çağrılmadan hemen önce o->enerji, GM'in Ayarlar'da verdiği
   "sıra başı enerji" değerine doldurulur; istersen değiştir. 0 yaparsan sırası hemen geçer. */
void her_tur(Oyuncu *o);

/* Sırası gelen oyuncu haritada bir yere tıkladı ve GM bu yürüyüşü onayladı (GM hedefi kaydırdıysa x, y
   kaydırılmış haldir). Çağrıldığında o->x, o->y oyuncunun şu anki yeri, x, y gitmek istediği yerdir.
   Enerjiyi sen düşürürsün; sunucu yürüyüş için kendiliğinden enerji harcamaz.
     1 döndürürsen oyuncu x, y'ye yürür.
     0 döndürürsen yerinde kalır (ör. enerjisi yetmiyorsa). Nedenini bildir(o, ...) ile yazarsan
       oyuncunun ekranında uyarı olarak da çıkar.
     o->x, o->y'yi değiştirip 1 döndürürsen yeni yazdığın yere yürür (ör. yolu kısaltmak için).
       Yazdığın yer şu anki yerle aynıysa x, y'ye yürür; yerinde kalsın istiyorsan 0 döndür.
   Dönüşten sonra enerjisi 0 ya da altındaysa sırası biter. GM'in tokenları sürüklemesi bu fonksiyonu
   çağırmaz. */
int hareket(Oyuncu *o, float x, float y);

/* GM sırası gelen karakterin saldırısını onayladığında (ya da GM onu saldırttığında). esya, saldıranın
   envanterindeki seçilen eşyadır
   (esya == &saldiran->envanter[i]). Çağrıdan sonra saldıranın enerjisi 0 ya da altındaysa sırası
   biter. Hedefin canı 0'a inerse o sıranın sonunda ölür. */
void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya);

/* ------------------------------------------------------------------ yardımcılar */

/* adet tane yuz yüzlü zar atar ve toplamı döner. zar_at(2, 6) = 2d6.
   Zarlar sunucunun güvenli rastgele kaynağıyla atılır. Geçersiz değerlerde 0 döner. */
int zar_at(int adet, int yuz);

/* Zar ifadesini atar ve toplamı döner: zar("1d20+5"), zar("4d6kh3"), zar("2d20kl1").
   İfade hatalıysa 0 döner ve hatayı GM'in kaydına yazar. */
int zar(const char *ifade);

/* Herkesin ekranındaki kayda bir satır yazar. printf gibi kullanılır:
   duyur("%s tuzağa bastı, %d hasar aldı.", o->ad, hasar); */
void duyur(const char *bicim, ...) __attribute__((format(printf, 1, 2)));

/* Yalnızca GM'in kaydına yazar (oyunculardan saklamak istediğin bilgiler için). */
void gm_bildir(const char *bicim, ...) __attribute__((format(printf, 1, 2)));

/* Yalnızca o oyuncunun (ve GM'in) kaydına yazar: bildir(o, "Uzaktan bir ses duydun."); */
void bildir(const Oyuncu *o, const char *bicim, ...) __attribute__((format(printf, 2, 3)));

/* Envantere yeni bir eşya ekler ve onu döner (envanter doluysa NULL).
   resim NULL ya da "" olabilir. Dizinin yeri değişebilir: elindeki eski Esya göstericilerini
   bundan sonra kullanma. */
Esya *envanter_ekle(Oyuncu *o, const char *ad, const char *sinif, const char *resim);

/* envanter[sira]'yı çıkarır; sonrakiler bir öne kayar. */
void envanter_cikar(Oyuncu *o, int sira);

#endif
