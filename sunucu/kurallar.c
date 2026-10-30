/*
 * ===================== OYUN KURALLARIN BURAYA =====================
 *
 * Sunucu bu dosyadaki dört fonksiyonu çağırır:
 *
 *   her_tur(o)                      bir karakterin sırası başladığında
 *   hareket(o, x, y)                GM sırası gelen oyuncunun yürüyüşünü onayladığında
 *   saldiri(saldiran, hedef, esya)  GM bir saldırıyı onayladığında
 *   buyu(yapan, hedef, buyu_adi)    GM bir büyüyü onayladığında
 *
 * Turlar başlamadan kimse yürüyemez, saldıramaz, büyü yapamaz; dördü de yalnızca tur sırasında çağrılır.
 *
 * Bir Oyuncu'da (oyuncular da NPC'ler de) şunlar var (ayrıntılar kurallar.h içinde):
 *
 *   o->ad                       adı
 *   o->x, o->y                  konumu, kare biriminde (değiştirirsen taşınır)
 *   o->can, o->odak, o->zirh, o->buyu_direnci
 *   o->enerji                   aksiyon puanı; 0 olunca sıra geçer (ne kadar harcanacağını sen yazarsın)
 *   o->agirlik                  ağırlığı: KARAKTER_AGIRLIGI (70) + eşyalarının ağırlıkları (salt okunur)
 *   o->altin, o->gumus, o->bakir parası: üç ayrı sikke, birbirine çevrilmez
 *   o->envanter[i]              eşyalar: .ad, .sinif, .resim, .agirlik, .dayaniklilik  (i < o->envanter_adet;
 *                               envanter[0] her zaman yumruk, en fazla ENVANTER_AZAMI eşya)
 *   o->npc                      1 ise GM'in karakteri
 *
 *   harita.genislik, harita.yukseklik   açık haritanın boyutu (kare; GM haritayı açarken seçer)
 *   tur_sayisi                          kaçıncı tur
 *   yol_uzunlugu                        hareket() içinde: yürünecek yolun uzunluğu (duvarların etrafından)
 *   yol_noktasi(u, &x, &y)              hareket() içinde: yolun u kadar ilerisindeki nokta
 *
 *   zar_at(2, 6)          2d6 atar, toplamı döner (sonucu GM ve o işteki oyuncular kayıtta görür)
 *   zar("1d20+5")         ifadeyle zar atar (aynı şekilde kayda yazılır)
 *   duyur("...", ...)     herkesin ekranındaki kayda yazar (printf gibi)
 *   gm_bildir("...", ...) yalnızca GM'in kaydına yazar
 *   bildir(o, "...", ...) yalnızca o oyuncunun ve GM'in kaydına yazar
 *   envanter_ekle(o, ad, sinif, resim)   envanter_cikar(o, sira)
 *
 * Fonksiyonlar hızlı bitmeli: onlar çalışırken sunucu diğer istekleri bekletir.
 * Değiştirdikten sonra:  make  ve servisi yeniden başlat.
 */

#include "kurallar.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ eşya sınıfları
 *
 * Kendi sınıflarını buraya yaz: tırnak içinde, virgülle ayırarak. Listenin sonu NULL kalmalı.
 * Sunucu her sınıf için oyun/esyalar/<sınıf>/ klasörünü açar. GM bir eşya resmini hangi
 * klasöre yüklerse eşya o sınıftan olur (esyalar/ klasörünün kendisine konan eşyanın sınıfı "").
 * Sınıf adı bir klasör adıdır: / içeremez, nokta ile başlayamaz, en fazla 31 bayt olabilir.
 */
const char *const esya_siniflari[] = {
    "silah",
    "zirh",
    "iksir",
    NULL
};

/* ------------------------------------------------------------------ yumruk
 *
 * Herkesin envanterinde baştan olan, bırakılamayan eşya (envanter[0]). saldiri() onu da alır.
 */
const Esya yumruk = { .ad = "Yumruk", .sinif = "silah", .agirlik = 0, .dayaniklilik = 100 };

/* ------------------------------------------------------------------ büyüler
 *
 * Oyuncu bir karaktere (ya da kendine) tıklayıp "Büyü yap"ı seçince bu listeden seçer. Listenin sonu NULL kalmalı.
 */
const char *const buyuler[] = {
    "Ateş topu",
    "İyileştir",
    NULL
};

/* ------------------------------------------------------------------ her sıranın başında */

void her_tur(Oyuncu *o)
{
}

/* ------------------------------------------------------------------ GM bir yürüyüşü onaylayınca
 *
 * o->x, o->y oyuncunun şu anki yeri; x, y gitmek istediği yer (kare cinsinden). Arada duvar varsa karakter
 * duvarın etrafından dolaşır; yürüyeceği yolun uzunluğu yol_uzunlugu'dur.
 * Enerjiyi burada sen düşürürsün; sunucu yürüyüş için kendiliğinden enerji harcamaz.
 *
 *   return 1;                           oyuncu x, y'ye yürür
 *   return 0;                           yerinde kalır (nedenini bildir(o, ...) ile yazarsan
 *                                       oyuncunun ekranında uyarı olarak da çıkar)
 *   o->x, o->y'yi değiştir, return 1;   yeni yazdığın yere yürür
 *
 * Dönüşten sonra enerjisi 0 ya da altındaysa sırası biter.
 */
int hareket(Oyuncu *o, float x, float y)
{
    float mesafe = yol_uzunlugu;                 /* yürünecek yolun uzunluğu (duvarların etrafından), kare cinsinden */
    float bedel = (mesafe > 4.0) ? (mesafe - 4.0) : 0.0;    /* kare başına 1 enerji */

    if (bedel > o->enerji) {
        bildir(o, "Enerjin yetmiyor: bu yol %.1f enerji ister, %.1f enerjin var.", bedel, o->enerji);
        return 0;                               /* yerinde kalır */
    }
    
    gm_bildir("HAREKET: %s (%.1f,%.1f) -> (%.1f,%.1f) tur %d enerji %.1f", o->ad, o->x, o->y, x, y, tur_sayisi, o->enerji);
    o->enerji -= bedel;
    return 1;
}

/* ------------------------------------------------------------------ GM bir saldırıyı onaylayınca */

void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    if(strcmp(esya->sinif, "silah") != 0){
        bildir(saldiran, "Bu bir silah değil!");
        return;
    }

    int miktar, bedel;
    if(strstr(esya->ad,"Uzun") != NULL){
        miktar = 20;
        bedel = 2;
    } else if(strstr(esya->ad,"Kısa") != NULL){
        miktar = 12;
        bedel = 1;
    } else if(strstr(esya->ad,"Yumruk") != NULL){
        miktar = 4;
        bedel = 1;
    } else{
        bildir(saldiran, "Bilinmeyen eşya!");
        return;
    }

    if(saldiran->enerji>bedel){
        saldiran->enerji -= bedel;
        hedef->can -= zar_at(1,miktar);
        bildir(saldiran, "SALDIRI: %s -> %s [%s/%s] can %.0f", saldiran->ad, hedef->ad, esya->ad, esya->sinif, hedef->can);
    } else{
        bildir(saldiran, "Enerjin yetmiyor!");
    }
}

/* ------------------------------------------------------------------ GM bir büyüyü onaylayınca
 *
 * buyu_adi yukarıdaki buyuler listesinden biridir. hedef, yapanın kendisi de olabilir.
 * Canı 0 ya da altına inen hemen ölür; yapanın enerjisi biterse sırası geçer.
 */
void buyu(Oyuncu *yapan, Oyuncu *hedef, const char *buyu_adi)
{
    int bedel = 2;
    if (yapan->enerji < bedel) {
        bildir(yapan, "Enerjin yetmiyor!");
        return;
    }
    yapan->enerji -= bedel;

    if (strcmp(buyu_adi, "İyileştir") == 0) {
        hedef->can += zar("1d6");
    } else if (strcmp(buyu_adi, "Ateş topu") == 0) {
        int hasar = zar("1d8") - (int)hedef->buyu_direnci;
        if (hasar > 0) hedef->can -= hasar;
    }
}
