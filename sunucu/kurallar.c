/*
 * ===================== OYUN KURALLARIN BURAYA =====================
 *
 * Sunucu bu dosyadaki üç fonksiyonu çağırır:
 *
 *   her_tur(o)                      bir karakterin sırası başladığında
 *   hareket(o, x, y)                GM sırası gelen oyuncunun yürüyüşünü onayladığında
 *   saldiri(saldiran, hedef, esya)  GM bir saldırıyı onayladığında
 *
 * Turlar başlamadan kimse yürüyemez ve saldıramaz; üçü de yalnızca tur sırasında çağrılır.
 *
 * Bir Oyuncu'da (oyuncular da NPC'ler de) şunlar var (ayrıntılar kurallar.h içinde):
 *
 *   o->ad                       adı
 *   o->x, o->y                  konumu, kare biriminde (değiştirirsen taşınır)
 *   o->can, o->odak, o->zirh, o->buyu_direnci
 *   o->enerji                   aksiyon puanı; 0 olunca sıra geçer (ne kadar harcanacağını sen yazarsın)
 *   o->gorus                    görüş menzili (kare)
 *   o->envanter[i]              eşyalar: .ad, .sinif, .resim  (i < o->envanter_adet)
 *   o->npc                      1 ise GM'in karakteri
 *
 *   harita.genislik, harita.yukseklik   açık haritanın boyutu (kare; GM haritayı açarken seçer)
 *   tur_sayisi                          kaçıncı tur
 *
 *   zar_at(2, 6)          2d6 atar, toplamı döner
 *   zar("1d20+5")         ifadeyle zar atar
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
    /* "silah", */
    /* "zirh",  */
    /* "iksir", */
    NULL
};

/* ------------------------------------------------------------------ her sıranın başında */

void her_tur(Oyuncu *o)
{
    (void)o; /* kullanmaya başlayınca bu satırı silebilirsin */

    /* --- Örnekler: denemek için yorum işaretlerini kaldır ---

    // Enerjiyi odağa göre ver: 5 + odak/2
    o->enerji = 5 + o->odak / 2;

    // Haritanın sol üst köşesindeki 3x3'lük alan radyasyonlu olsun
    if (o->x < 3 && o->y < 3) {
        int hasar = zar("1d4");
        o->can -= hasar;
        duyur("%s radyasyona maruz kaldı: %d hasar.", o->ad, hasar);
    }

    // Gizli bir algı kontrolü: sonucu yalnızca o oyuncu ve GM görür
    if (!o->npc && zar("1d20") >= 15) bildir(o, "Duvarda bir çatlak fark ettin.");

    */
}

/* ------------------------------------------------------------------ GM bir yürüyüşü onaylayınca
 *
 * o->x, o->y oyuncunun şu anki yeri; x, y gitmek istediği yer (kare cinsinden).
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
    /* ÖRNEK KURAL: istediğin gibi değiştir */

    float mesafe = hypotf(x - o->x, y - o->y);  /* düz çizgi uzunluğu, kare cinsinden */
    float bedel = mesafe;                       /* kare başına 1 enerji */

    /* --- Başka örnekler: denemek için yorum işaretlerini kaldır ---

    // Zırh ağırsa her kare daha pahalı
    bedel = mesafe * (1 + o->zirh / 10);

    // Enerji yetmiyorsa yetebildiği kadar yürüsün (yolu kısalt)
    if (bedel > o->enerji) {
        if (o->enerji <= 0) return 0;
        float oran = o->enerji / bedel;
        o->x += (x - o->x) * oran;
        o->y += (y - o->y) * oran;
        o->enerji = 0;
        return 1;
    }

    */

    if (bedel > o->enerji) {
        bildir(o, "Enerjin yetmiyor: bu yol %.1f enerji ister, %.1f enerjin var.", bedel, o->enerji);
        return 0;                               /* yerinde kalır */
    }
    o->enerji -= bedel;
    return 1;
}

/* ------------------------------------------------------------------ GM bir saldırıyı onaylayınca */

void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    (void)saldiran; /* kullanmaya başlayınca bu üç satırı silebilirsin */
    (void)hedef;
    (void)esya;

    /* --- Örnek: denemek için yorum işaretlerini kaldır ---

    // Saldırı 4 enerji harcar (enerji 0'a inerse sıra geçer)
    saldiran->enerji -= 4;

    // Hasar zarı eşyanın sınıfına göre
    int hasar = strcmp(esya->sinif, "silah") == 0 ? zar("1d8") : zar("1d4");

    // İsabet: 1d20 + odak, 10 + hedefin zırhını geçmeli
    int atis = zar_at(1, 20) + (int)saldiran->odak;
    if (atis > 10 + hedef->zirh) {
        hedef->can -= hasar;
        duyur("%s, %s ile vurdu; %s %d hasar aldı.", saldiran->ad, esya->ad, hedef->ad, hasar);
    } else {
        duyur("%s ıskaladı (%d).", saldiran->ad, atis);
    }

    // Tek kullanımlık eşya: kullanınca envanterden çıkar. Bunu en sonda yap,
    // çünkü çıkardıktan sonra esya göstericisi başka bir eşyayı gösterir.
    if (strcmp(esya->sinif, "iksir") == 0) envanter_cikar(saldiran, (int)(esya - saldiran->envanter));

    */
}
