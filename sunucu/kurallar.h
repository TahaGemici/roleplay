/*
 * Oyun kuralların için arayüz.
 *
 * Sunucu dört fonksiyonunu çağırır; gövdeleri kurallar.c içinde ve onları sen doldurursun:
 *
 *   her_tur(o)                        bir karakterin sırası başladığında
 *   hareket(o, x, y)                  GM sırası gelen oyuncunun yürüyüşünü onayladığında
 *   saldiri(saldiran, hedef, esya)    GM bir saldırıyı onayladığında
 *   buyu(yapan, hedef, buyu_adi)      GM bir büyüyü onayladığında
 *
 * Turlar başlamadan kimse yürüyemez, saldıramaz ve büyü yapamaz; dördü de yalnızca tur sırasında çağrılır.
 *
 * Bu dosyadaki tanımlar o fonksiyonlarda kullanabileceğin her şeydir. kurallar.c'yi
 * değiştirdikten sonra programı yeniden derle (make) ve servisi yeniden başlat.
 */
#ifndef KURALLAR_H
#define KURALLAR_H

/* Bir envanterde yumruğun yanında en fazla bu kadar eşya olabilir (yumruk yer kaplamaz). İstersen değiştir ve
   yeniden derle. */
#define ENVANTER_AZAMI 12

/* Eşyasız bir karakterin ağırlığı: o->agirlik = KARAKTER_AGIRLIGI + eşyalarının ağırlıkları */
#define KARAKTER_AGIRLIGI 70

/* ------------------------------------------------------------------ eşya */

typedef struct {
    char ad[64];     /* "Uzun Kılıç" */
    char sinif[32];  /* "silah". Sınıf adlarını kurallar.c'deki esya_siniflari listesinde sen belirlersin. */
    char resim[256]; /* resmin dosya yolu (resmin kendisi değil), oyun klasörüne göre:
                        "esyalar/silah/Uzun_Kilic.png". Resim dosyası her boyutta olabilir. */
    float agirlik;   /* eşyanın ağırlığı. GM bir eşya verdiğinde 1'dir; GM envanterde değiştirebilir, sen de
                        değiştirebilirsin. */
    float dayaniklilik; /* yeni eşyada 100. Sunucu buna hiç dokunmaz: azaltmak, 0'a inince eşyayı kırmak
                           (envanter_cikar) kurallarının işi. GM de değiştirebilir. */
    int sabit;       /* 1: herkeste olan yumruk (envanter[0]); bırakılamaz, çıkarılamaz. Salt okunur. */
} Esya;

/* ------------------------------------------------------------------ oyuncu */

/* Statların azami değerleri. GM her karakter için karakter düzenleyicisinden belirler; sen de değiştirebilirsin.
   0 ya da altıysa o statın azamisi yoktur. Sunucu bir statı azamisinin üstünde bırakmaz: kurallardan ve GM'in
   düzenlemesinden sonra azamiye indirir (can 40, azami can 35 ise can 35 olur). Ağırlığın azamisi yoktur. */
typedef struct {
    float can, odak, zirh, buyu_direnci, enerji;
} Azami;

/* Oyuncuların ve GM'in NPC'lerinin hepsi bu yapıyı kullanır. */
typedef struct {
    const char *ad;     /* adı (değiştirme) */

    float x, y;         /* konumu, kare biriminde: (0,0) haritanın sol üst köşesi, (0.5, 0.5) ilk karenin
                           ortası. GM Ayarlar'da "Konumlar tam sayı" seçeneğini açtıysa konum karenin numarasıdır:
                           (0,0) sol üstteki kare, (3,5) soldan dördüncü, yukarıdan altıncı kare; x ve y hep tam sayıdır
                           (karakter karenin ortasında durur). Değiştirirsen karakter oraya düz çizgide itilir: harita
                           sınırları içinde kalır, arada duvar varsa duvarın önünde durur (hareket() için oradaki
                           açıklamaya bak). */
    float can;          /* 0 ya da altına inerse ölür: saldırı ve büyüde hemen, başka zaman sırası bitince */
    float odak;
    float zirh;
    float buyu_direnci;
    float enerji;       /* aksiyon puanı. Sunucu ona hiç dokunmaz: sıra başında ne olacağını her_tur(), ne kadar
                           harcanacağını hareket(), saldiri() ve buyu() içinde sen belirlersin; sıra başında ya da
                           bir işten sonra 0 ya da altındaysa sıra geçer. */
    Azami azami;        /* statların azami değerleri: o->azami.can, o->azami.enerji... (0: azamisi yok) */
    float agirlik;      /* ağırlığı: KARAKTER_AGIRLIGI (70) + eşyalarının ağırlıkları. Sunucu hesaplar (salt okunur);
                           arttıkça haritadaki pulu büyür (70'te 1 kat, 140'ta 2 kat). */
    int altin;          /* parası: altın, gümüş ve bakır ayrı ayrı sayılır, birbirine çevrilmez. 0'dan aşağı inmez. */
    int gumus;
    int bakir;

    Esya envanter[ENVANTER_AZAMI + 1]; /* envanter[0] her zaman yumruktur; eşyalar envanter[1]'den
                                          envanter[envanter_adet - 1]'e kadar */
    int envanter_adet;  /* dizideki eşya sayısı (yumruk dahil; en fazla ENVANTER_AZAMI + 1) */

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

/* hareket() çağrılırken: karakterin x, y'ye yürüyeceği yolun uzunluğu (kare). Duvar varsa yol duvarların etrafından
   dolaşır, bu yüzden düz çizgiden uzun olabilir. Başka zaman 0. */
extern float yol_uzunlugu;

/* hareket() içinde: yolun başından uzunluk kadar ilerideki noktayı *x, *y'ye yazar (yolun sonunu geçerse hedefi).
   Konumlar tam sayıysa o noktanın bulunduğu karenin numarasını yazar. Yolun bir kısmını yürütmek için:
   yol_noktasi(o->enerji, &o->x, &o->y); return 1; */
void yol_noktasi(float uzunluk, float *x, float *y);

/* ------------------------------------------------------------------ kurallar.c'de senin yazdıkların */

/* Eşya sınıfları; liste NULL ile biter. Sunucu her sınıf için oyun/esyalar/<sınıf>/ klasörünü açar ve
   GM'in o klasöre koyduğu eşyalar o sınıftan olur. */
extern const char *const esya_siniflari[];

/* Herkesin envanterinde baştan olan, bırakılamayan eşya: her karakterin envanter[0]'ı. */
extern const Esya yumruk;

/* Büyüler; liste NULL ile biter. Oyuncu bir karaktere tıklayıp "Büyü yap"ı seçince bu listeden seçer. */
extern const char *const buyuler[];

/* Sıra o karaktere geldiğinde (oyuncular da NPC'ler de). Sunucu enerjiye dokunmaz: sıra başında enerjinin ne
   olacağını burada sen belirlersin (ör. o->enerji = o->azami.enerji; ya da o->enerji += 3;). Dönüşte enerjisi 0 ya
   da altındaysa sırası hemen geçer. */
void her_tur(Oyuncu *o);

/* Sırası gelen oyuncu haritada bir yere tıkladı ve GM bu yürüyüşü onayladı (GM hedefi kaydırdıysa x, y
   kaydırılmış haldir); ya da sırası gelen NPC'yi GM haritada sürükledi. Çağrıldığında o->x, o->y karakterin şu anki
   yeri, x, y gitmek istediği yerdir (konumlar tam sayıysa ikisi de karenin numarasıdır). Arada duvar
   varsa karakter duvarın etrafından dolaşır: yürüyeceği yolun uzunluğu yol_uzunlugu'dur (düz çizgiden uzun olabilir).
   Enerjiyi sen düşürürsün; sunucu yürüyüş için kendiliğinden enerji harcamaz.
     1 döndürürsen oyuncu x, y'ye yürür.
     0 döndürürsen yerinde kalır (ör. enerjisi yetmiyorsa). Nedenini bildir(o, ...) ile yazarsan
       oyuncunun ekranında uyarı olarak da çıkar.
     o->x, o->y'yi değiştirip 1 döndürürsen yeni yazdığın yere yürür (ör. yolu kısaltmak için:
       yol_noktasi(o->enerji, &o->x, &o->y) yolun enerjinin yettiği yerine kadar yürütür). Oraya da duvarların
       etrafından gider; yol yoksa yerinde kalır ve hareket() hiç çağrılmamış gibi olur.
       Yazdığın yer şu anki yerle aynıysa x, y'ye yürür; yerinde kalsın istiyorsan 0 döndür.
   Dönüşten sonra enerjisi 0 ya da altındaysa sırası biter. GM'in tokenları sürüklemesi bu fonksiyonu
   çağırmaz (sırası gelen NPC'yi sürüklemesi dışında). Yürüyen, yolunun üstündeki eşyaları alır (envanterinde yer
   varsa); yürüyüşü bir portalda biterse portalın haritasına geçer. */
int hareket(Oyuncu *o, float x, float y);

/* GM sırası gelen karakterin saldırısını onayladığında (ya da GM onu saldırttığında). esya, saldıranın
   envanterindeki seçilen eşyadır (esya == &saldiran->envanter[i]; yumruk da olabilir). Çağrıdan sonra canı
   0 ya da altına inen hemen ölür; saldıranın enerjisi 0 ya da altındaysa sırası biter. */
void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya);

/* GM sırası gelen karakterin büyüsünü onayladığında (ya da GM ona büyü yaptırdığında). buyu_adi, buyuler
   listesinden seçilendir. hedef, yapanın kendisi de olabilir (kendine büyü, ör. iyileşme). Çağrıdan sonra
   canı 0 ya da altına inen hemen ölür; yapanın enerjisi 0 ya da altındaysa sırası biter. */
void buyu(Oyuncu *yapan, Oyuncu *hedef, const char *buyu_adi);

/* ------------------------------------------------------------------ yardımcılar */

/* adet tane yuz yüzlü zar atar ve toplamı döner. zar_at(2, 6) = 2d6.
   Zarlar sunucunun güvenli rastgele kaynağıyla atılır. Geçersiz değerlerde 0 döner.
   Kurallarda atılan her zar kayda yazılır: GM ve o işteki oyuncular (yapan ve hedef) görür. */
int zar_at(int adet, int yuz);

/* Zar ifadesini atar ve toplamı döner: zar("1d20+5"), zar("4d6kh3"), zar("2d20kl1").
   İfade hatalıysa 0 döner ve hatayı GM'in kaydına yazar. zar_at gibi kayda yazılır. */
int zar(const char *ifade);

/* Herkesin ekranındaki kayda bir satır yazar. printf gibi kullanılır:
   duyur("%s tuzağa bastı, %d hasar aldı.", o->ad, hasar); */
void duyur(const char *bicim, ...) __attribute__((format(printf, 1, 2)));

/* Yalnızca GM'in kaydına yazar (oyunculardan saklamak istediğin bilgiler için). */
void gm_bildir(const char *bicim, ...) __attribute__((format(printf, 1, 2)));

/* Yalnızca o oyuncunun (ve GM'in) kaydına yazar: bildir(o, "Uzaktan bir ses duydun."); */
void bildir(const Oyuncu *o, const char *bicim, ...) __attribute__((format(printf, 2, 3)));

/* Envantere yeni bir eşya ekler (ağırlığı 1) ve onu döner; envanter doluysa (yumruğun yanında ENVANTER_AZAMI eşya
   varsa) NULL döner. resim NULL ya da "" olabilir: resmi olmayan eşya sınıfına göre varsayılan bir resimle görünür. */
Esya *envanter_ekle(Oyuncu *o, const char *ad, const char *sinif, const char *resim);

/* envanter[sira]'yı çıkarır; sonrakiler bir öne kayar. Yumruk (envanter[0]) çıkarılamaz. */
void envanter_cikar(Oyuncu *o, int sira);

#endif
