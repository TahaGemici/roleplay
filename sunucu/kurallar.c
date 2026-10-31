/*
 * ===================== OYUN KURALLARIN BURAYA =====================
 *
 * Her turda sırası gelen karakter önce yürür (ya da yürümeden geçer), sonra bir eylem yapar (saldırı ya da
 * büyü) ya da eylem yapmadan bitirir; sonra tur biter. Sunucu bu dosyadaki fonksiyonları çağırır:
 *
 *   her_tur(o)                      bir karakterin sırası başladığında
 *   hareket(o, x, y)                yürüme: GM yürüyüşü onaylayınca; yürümeden geçerse yol_uzunlugu 0 ile
 *   saldiri(saldiran, hedef, esya)  eylem: GM bir saldırıyı onaylayınca (1 döndürürse saldırı olur)
 *   buyu(yapan, hedef, buyu_adi)    eylem: GM bir büyüyü onaylayınca (1 döndürürse büyü olur)
 *   eylem_yok(o)                    eylem: karakter saldırmadan, büyü yapmadan turunu bitirince
 *
 * Turlar başlamadan kimse yürüyemez, saldıramaz, büyü yapamaz; hepsi yalnızca tur sırasında çağrılır.
 *
 * Bir Oyuncu'da (oyuncular da NPC'ler de) şunlar var (ayrıntılar kurallar.h içinde):
 *
 *   o->ad                       adı
 *   o->x, o->y                  konumu, kare biriminde (değiştirirsen taşınır)
 *   o->can, o->odak, o->zirh, o->buyu_direnci
 *   o->enerji                   sunucu dokunmaz; ne kadar kazanılıp harcanacağını bu dosyada sen yazarsın
 *   o->azami.can, o->azami.enerji, ...  statların azami değerleri (GM belirler; 0: azamisi yok).
 *                               Sunucu bir statı azamisinin üstünde bırakmaz.
 *   o->agirlik                  ağırlığı: KARAKTER_AGIRLIGI (70) + eşyalarının ağırlıkları (salt okunur)
 *   o->altin, o->gumus, o->bakir parası: üç ayrı sikke, birbirine çevrilmez
 *   o->envanter[i]              eşyalar: .ad, .sinif, .resim, .agirlik, .dayaniklilik  (i < o->envanter_adet;
 *                               envanter[0] her zaman yumruk, yanında en fazla ENVANTER_AZAMI eşya)
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

/* ------------------------------------------------------------------ enerji
 *
 * Yürüme: 4 birim ya da daha az yürüyen (hiç yürümeyen de) 1 enerji kazanır. Daha fazla yürüyen kazanmaz, 4'ü aşan
 * her birim için 1 enerji harcar. Bir turda en fazla 8 birim yürünür (enerjisi yetmiyorsa yettiği kadar).
 * Eylem: saldıran ya da büyü yapan 1 enerji harcar; eylem yapmayan 1 enerji kazanır. Sonra tur biter.
 */
#define BEDAVA_YOL 4.0f /* bu kadar birime kadar yürüyen 1 enerji kazanır */
#define AZAMI_YOL 8.0f  /* bir turda en fazla bu kadar birim yürünür */

void her_tur(Oyuncu *o)
{
}

int hareket(Oyuncu *o, float x, float y)
{
    float yol = yol_uzunlugu;
    float azami = fminf(AZAMI_YOL, BEDAVA_YOL + fmaxf(o->enerji, 0)); /* enerjinin yettiği kadar */
    if (yol > azami) {
        yol_noktasi(azami, &o->x, &o->y); /* yolun ilk "azami" birimini yürür */
        bildir(o, "Bu tur en fazla %g birim yürüyebilirsin; o kadarını yürüdün.", roundf(azami * 10) / 10);
        yol = azami;
    }
    if (yol <= BEDAVA_YOL) o->enerji += 1;
    else o->enerji -= yol - BEDAVA_YOL;
    return 1;
}

int saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    if (saldiran->enerji < 1) {
        bildir(saldiran, "Enerjin yetmiyor: saldırı 1 enerji ister.");
        return 0;
    }
    saldiran->enerji -= 1;
    return 1;
}

int buyu(Oyuncu *yapan, Oyuncu *hedef, const char *buyu_adi)
{
    if (yapan->enerji < 1) {
        bildir(yapan, "Enerjin yetmiyor: büyü 1 enerji ister.");
        return 0;
    }
    yapan->enerji -= 1;
    return 1;
}

void eylem_yok(Oyuncu *o)
{
    o->enerji += 1;
}
