# Zar Masası

Kendi sunucunda, kendi alan adınla çalışan, Fallout 1 gibi sıra tabanlı bir sanal oyun masası. Sohbet yok; oyuncular sesli konuşur, ekran haritayı, sırayı, zarları, statları ve envanteri taşır. Herkes aynı oyuna girer.

- **Sunucu C ile yazıldı.** Bağımlılığı yok; yalnızca C derleyicisi ve standart kütüphane.
- **Web tarafı yalnızca HTML, CSS ve JavaScript.** Çerçeve ya da derleme adımı yok.
- **GM ekranı** (`https://alanadin/gm`): `.env` dosyasındaki GM şifresiyle girilir.
- **Oyuncu ekranı** (`https://alanadin/`): isim ve oyun şifresiyle girilir.
- **Sıra tabanlı:** Turlar başlamadan kimse yürüyemez ve saldıramaz. Sırası gelen oyuncu haritada bir yere tıklayıp yürür, bir karaktere tıklayıp eşyasıyla saldırır. İkisi de önce GM'in onayına gider; ne kadar enerji harcanacağını senin kuralların belirler. Enerji bitince ya da "Turu bitir"e basınca sıra geçer.
- **Harita boyutu GM'in:** GM bir haritayı ilk açarken kaç kare olduğunu (genişlik × yükseklik) seçer.
- **Kurallar senin:** Her sıranın başında `her_tur()`, her onaylanan yürüyüşte `hareket()`, her onaylanan saldırıda `saldiri()` çağrılır. Üçünü de C ile sen yazarsın.
- **Görüş sisi:** Her oyuncu yalnızca görüş menzilindekini görür. Daha önce gördüğü yerler eski haliyle kalır, oradaki değişikliği görmez. GM her şeyi görür.
- **Statlar:** Can, odak, zırh, büyü direnci, enerji, görüş. GM herkesinkini görür, oyuncu yalnızca kendisininkini.
- **Zarlar sunucuda atılır**, kimse tarayıcıdan değiştiremez.
- **Tek klasör:** oyunun bütün dosyaları `oyun/` klasöründe durur ve GM bu klasöre web'deki **Dosyalar** ekranından tam erişir.

---

## 1. Kendi bilgisayarında dene

Linux ya da macOS'ta (Windows'ta WSL ile):

```bash
make
GM_SIFRESI=gizli-gm-sifresi OYUN_SIFRESI=deneme ./zar-masasi
```

- GM `http://127.0.0.1:3000/gm` adresini açar ve `gizli-gm-sifresi` yazar. Tarayıcı onu hatırlar.
- Oyuncular `http://127.0.0.1:3000` adresini açar, isim ve `deneme` şifresiyle girer.
- Aynı tarayıcıda bir sekme GM, diğeri oyuncu olabilir.

`make test` birim testlerini çalıştırır.

---

## 2. Sunucuya kur (HTTPS ile)

İki hazırlık:

1. **DNS:** Alan adının panelinde bir A kaydı aç: `oyun.alanadin.com → sunucunun IP adresi`.
2. **Güvenlik duvarı:** 80 ve 443 portları açık olsun (Let's Encrypt sertifikayı alırken 80'i kullanır).

HTTPS'i [Caddy](https://caddyserver.com) sağlar: sertifikayı kendisi alır, yeniler ve WebSocket bağlantılarını ek ayarsız taşır. C sunucusu dışarıya kapalıdır; herkes yalnızca `https://` ile bağlanır.

### Yol A: Docker (en kolayı)

```bash
cd zar-masasi
cp .env.example .env
nano .env                       # ALAN_ADI, GM_SIFRESI, OYUN_SIFRESI
docker compose up -d --build
docker compose logs masa        # "Zar Masası çalışıyor" satırını görmelisin
```

GM `https://oyun.alanadin.com/gm` adresini açar ve GM şifresini yazar. İşte bu kadar.

| İş | Komut |
| --- | --- |
| Günlüğü izle | `docker compose logs -f masa` |
| `kurallar.c`'yi ya da `.env`'i değiştirdikten sonra | `docker compose up -d --build` |
| Yedek al | `docker compose cp masa:/veri/oyun ./oyun-yedek` |

### Yol B: Docker'sız (Ubuntu/Debian, systemd + Caddy)

```bash
# 1) Derle
sudo apt install -y build-essential
make

# 2) Yerleştir: ayrı bir sistem kullanıcısıyla /opt altına
sudo useradd --system --home /opt/zar-masasi --shell /usr/sbin/nologin zarmasasi
sudo mkdir -p /opt/zar-masasi/oyun
sudo cp -r zar-masasi web .env.example /opt/zar-masasi/
cd /opt/zar-masasi
sudo cp .env.example .env && sudo nano .env      # ALAN_ADI, GM_SIFRESI, OYUN_SIFRESI; HOST=127.0.0.1 kalsın
sudo chown -R zarmasasi:zarmasasi oyun
sudo chown root:zarmasasi .env && sudo chmod 640 .env

# 3) Servis: açılışta başlasın, çökerse yeniden başlasın
sudo cp ~/zar-masasi/kurulum/manuel/zar-masasi.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable --now zar-masasi
journalctl -u zar-masasi -n 20

# 4) Caddy: https://caddyserver.com/docs/install adresindeki Debian/Ubuntu adımlarıyla kur, sonra
sudo cp ~/zar-masasi/kurulum/manuel/Caddyfile /etc/caddy/Caddyfile
sudo nano /etc/caddy/Caddyfile                    # oyun.ornek.com yerine kendi alan adın
sudo systemctl reload caddy
```

`~/zar-masasi` yerine proje klasörünü nereye açtıysan onu yaz. Caddy yerine nginx kullanacaksan `kurulum/manuel/nginx.conf` içindeki adımları izle.

### Kurulumdan sonra

Kimsenin terminal açması gerekmez. Oyun şifresi, haritalar, eşyalar, NPC'ler, statlar ve bütün dosyalar GM ekranından yönetilir. Terminal yalnızca kurulum işleri için gerekir: `kurallar.c`'yi değiştirmek ve GM şifresini değiştirmek.

---

## 3. Oyun kuralların: `sunucu/kurallar.c`

Sunucu bu dosyadaki üç fonksiyonu çağırır. İçlerini sen doldurursun:

```c
#include "kurallar.h"
#include <math.h>
#include <string.h>

const char *const esya_siniflari[] = { "silah", "zirh", "iksir", NULL };   /* sınıflarını sen belirlersin */

void her_tur(Oyuncu *o)
{
    o->enerji = 5 + o->odak / 2;                     /* bu sıranın enerjisi */
    if (o->x < 3 && o->y < 3) {                      /* sol üst 3x3 kare radyasyonlu */
        int hasar = zar("1d4");
        o->can -= hasar;
        duyur("%s radyasyona maruz kaldı: %d hasar.", o->ad, hasar);
    }
}

int hareket(Oyuncu *o, float x, float y)            /* o->x, o->y şu anki yeri; x, y gitmek istediği yer */
{
    float bedel = hypotf(x - o->x, y - o->y);        /* kare başına 1 enerji */
    if (bedel > o->enerji) {
        bildir(o, "Enerjin yetmiyor: bu yol %.1f enerji ister.", bedel);
        return 0;                                    /* yerinde kalır */
    }
    o->enerji -= bedel;
    return 1;                                        /* x, y'ye yürür */
}

void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    saldiran->enerji -= 4;                           /* saldırı 4 enerji */
    int hasar = strcmp(esya->sinif, "silah") == 0 ? zar("1d8") : zar("1d4");
    if (zar_at(1, 20) + saldiran->odak > 10 + hedef->zirh) {
        hedef->can -= hasar;
        duyur("%s, %s ile vurdu; %s %d hasar aldı.", saldiran->ad, esya->ad, hedef->ad, hasar);
    } else {
        duyur("%s ıskaladı.", saldiran->ad);
    }
}
```

### Ne zaman çağrılırlar

- **`her_tur(o)`**: bir karakterin (oyuncu ya da sıraya eklenmiş NPC) sırası başladığında. Çağrılmadan hemen önce `o->enerji`, GM'in Ayarlar'da verdiği "sıra başı enerji" değerine doldurulur. İstersen değiştir. 0 yaparsan sırası hemen geçer.
- **`hareket(o, x, y)`**: oyuncu haritada bir yere tıklayınca istek GM'e gider; GM onaylarsa çağrılır. `o->x, o->y` oyuncunun şu anki yeri, `x, y` gitmek istediği yer (GM hedefi kaydırdıysa kaydırılmış hali).
  - Enerjiyi sen düşürürsün; sunucu yürüyüş için kendiliğinden enerji harcamaz.
  - `1` döndürürsen oyuncu `x, y`'ye yürür. `0` döndürürsen yerinde kalır; nedenini `bildir(o, ...)` ile yazarsan oyuncunun ekranında uyarı olarak da çıkar.
  - `o->x, o->y`'yi değiştirip `1` döndürürsen yeni yazdığın yere yürür (ör. yolu enerjinin yettiği kadar kısaltmak için).
  - Dönüşten sonra enerjisi 0 ya da altındaysa sırası biter.
  - GM'in tokenları sürüklemesi `hareket()`'i çağırmaz.
- **`saldiri(saldiran, hedef, esya)`**: oyuncu bir karaktere tıklayıp eşyasını seçtiğinde istek GM'e gider; GM onaylarsa çağrılır. GM sırası gelen karakteri (ör. NPC'yi) "Saldırt…" ile saldırttığında da çağrılır. Çağrıdan sonra saldıranın enerjisi 0 ya da altındaysa sırası biter.
- Turlar başlamadan (ya da GM durdurunca) kimse yürüyemez ve saldıramaz; bu üç fonksiyon yalnızca tur sırasında çağrılır.
- **Ölüm:** Bir karakterin sırası bittiğinde canı 0 ya da altında olan herkes ölür. Ölüler sırada atlanır, yürüyemez, saldırılamaz. GM diriltebilir.

### Elindekiler (`kurallar.h`)

**`Oyuncu`**: oyuncular da NPC'ler de bu yapıyı kullanır.

| Alan | Ne |
| --- | --- |
| `o->ad` | Adı (değiştirme) |
| `o->x`, `o->y` | Konumu, kare biriminde (float). (0,0) haritanın sol üst köşesi, (0.5, 0.5) ilk karenin ortası. Değiştirirsen karakter oraya yürür. |
| `o->can`, `o->odak`, `o->zirh`, `o->buyu_direnci` | Statlar (float) |
| `o->enerji` | Aksiyon puanı (float). Ne kadar harcanacağını `hareket()` ve `saldiri()` içinde sen belirlersin; 0 ya da altına inince sıra geçer. |
| `o->gorus` | Görüş menzili, kare (float) |
| `o->envanter[i]` | Eşyalar, dinamik dizi: `i < o->envanter_adet` |
| `o->npc` | 1 ise GM'in karakteri (salt okunur) |

**`Esya`**: `e->ad`, `e->sinif`, `e->resim` (üçü de C dizgisi). `resim`, resmin kendisi değil, dosyasının oyun klasörüne göre yoludur (ör. `esyalar/silah/kilic.png`) ve en fazla 255 bayt olabilir. Resim dosyası her boyutta olabilir.

**Genel:** `harita.genislik`, `harita.yukseklik` (açık haritanın GM'in seçtiği kare cinsinden boyutu), `tur_sayisi` (kaçıncı tur).

| Yardımcı | Ne yapar |
| --- | --- |
| `zar_at(2, 6)` | 2d6 atar, toplamı döner |
| `zar("1d20+5")` | İfadeyle atar: `4d6kh3`, `2d20kl1` de olur |
| `duyur("...", ...)` | Herkesin ekranındaki kayda yazar (printf gibi) |
| `gm_bildir("...", ...)` | Yalnızca GM'in kaydına yazar |
| `bildir(o, "...", ...)` | Yalnızca o oyuncunun ve GM'in kaydına yazar |
| `envanter_ekle(o, ad, sinif, resim)` | Envantere eşya ekler, onu döner (doluysa NULL) |
| `envanter_cikar(o, sira)` | `o->envanter[sira]`'yı çıkarır |

### Eşya sınıfları

`esya_siniflari` listesine yazdığın her sınıf için sunucu `oyun/esyalar/<sınıf>/` klasörünü açar. GM bir eşya resmini hangi sınıf klasörüne yüklerse eşya o sınıftan olur (ad, dosya adından gelir: `Uzun_Kilic.png` → "Uzun Kilic"). GM bir eşyanın adını ve sınıfını envanterde sonradan da değiştirebilir. Kuralında sınıfı `strcmp(esya->sinif, "silah") == 0` ile sorarsın.

### Dikkat

- Değiştirdikten sonra programı yeniden derleyip başlat:
  - **Docker:** `docker compose up -d --build`
  - **Docker'sız:** `make && sudo cp zar-masasi /opt/zar-masasi/ && sudo systemctl restart zar-masasi`
- Fonksiyonlar hızlı bitmeli; onlar çalışırken sunucu diğer istekleri bekletir.
- `envanter` dizisini yalnızca `envanter_ekle` ve `envanter_cikar` ile büyütüp küçült; elemanlarını doğrudan değiştirebilirsin. `envanter_ekle` dizinin yerini değiştirebilir: elindeki eski `Esya *` göstericisini sonra kullanma. `saldiri()` içinde eşyayı çıkaracaksan bunu en sonda yap: `envanter_cikar(saldiran, (int)(esya - saldiran->envanter));`
- Bir stat hatayla NaN ya da sonsuz olursa sunucu onu eski değerine döndürür ve GM'e yazar.
- Kodda bir hata programı çökertirse servis kendiliğinden yeniden başlar; oyun saniyede bir kaydedildiği için kaldığı yerden sürer.

---

## 4. Oyun nasıl oynanır

### GM

- **Harita:** "Harita yükle" ile ya da görseli haritanın üstüne sürükleyerek `haritalar` klasörüne ekle. Listeden birine tıklayınca herkese açılır.
  - **Harita boyutu:** Bir haritayı ilk açarken kaç kare olduğunu (genişlik × yükseklik) seçersin; resmin boyutundan bir öneri gelir (70 piksel bir kare). Resimde çizili kareler varsa onların sayısını yaz. Resim bu alana gerilir. Konumlar, görüş ve yürüyüş bu karelerle ölçülür. Boyutu sonra "Harita boyutu" bölümünden değiştirebilirsin.
  - "Haritayı oyunculardan gizle" ile hazırlarken saklayabilirsin.
  - **Görüş sisi** harita başına açılıp kapanır. "Görüş halkaları" oyuncuların ne kadarını gördüğünü yalnızca sana gösterir. "Keşfi sıfırla" o haritada oyuncuların gördüklerini unutturur.
  - **Izgara çizgileri** gösterilip gizlenebilir. "Hareket hedefleri kare ortasına otursun" açıksa herkes kare kare yürür; kapalıysa serbestçe.
- **İstek:** Sırası gelen oyuncunun yürüme ya da saldırı isteği sağ paneldeki "Bekleyen istek"e düşer, haritada da yolu çizilir; yürüme isteğinde yolun uzunluğu (kare) yazar. Enerji yazmaz: ne kadar harcanacağını onayladığında kuralların hesaplar. Sekme başlığı "● İstek bekliyor" olur.
  - **Onayla**: yürüyüşte `hareket()`, saldırıda `saldiri()` çağrılır. Kuralların yürüyüşe izin vermezse sana bildirim çıkar.
  - **Reddet**: oyuncuya "GM onaylamadı" bildirimi gider.
  - Haritada bir engel varsa yürüme hedefini **sürükleyerek kaydır**, sonra onayla: `hareket()` senin seçtiğin hedefle çağrılır.
- **Tur sırası:** Oyuncular sıraya kendiliğinden girer. NPC'leri Karakterler bölümünden "Sıraya ekle" ile eklersin. Oklarla sırala ya da "Zarla sırala" (her birine 1d20). Ardından "Turları başlat".
  - Sırası gelen oyuncu bağlı değilse atlanır.
  - NPC'nin sırasında onu sürükleyerek yürütür, "Saldırt…" ile saldırtırsın, sonra "Sıradakine geç". "Saldırt…" yalnızca sırası gelen karakterde görünür.
  - Sağdaki tur kartından sıradakinin enerjisini artırıp azaltabilir, sırayı geçebilir, turları durdurabilirsin.
  - Turlar başlamadan ya da durunca kimse yürüyemez ve saldıramaz ("Sıra kimsede değil"). Bu arada haritayı hazırlar, tokenları sürükleyerek yerleştirirsin.
- **Karakterler:** Bir karaktere (listede ya da haritada) tıklayınca düzenleyicisi açılır:
  - Can, odak, zırh, büyü direnci, enerji ve görüşü değiştir.
  - Öldür / Dirilt, Saldırt… (sırası gelene), NPC için ad, renk, "oyunculardan gizle", sıraya ekle/çıkar, sil.
  - Envanteri: eşya düzenle (✎) ve bir tane al (−).
  - Her tokenı sürükleyebilirsin; senin hamlelerin onay beklemez, enerji harcamaz ve `hareket()`'i çağırmaz.
- **Eşyalar:** "Eşya yükle" sınıfı sorar ve resmi o sınıfın klasörüne koyar. Bir karakter seçiliyken bir eşyaya tıklarsan ona verilir. Eşyayı sahibinden başka oyuncu göremez.
  - Resim her boyutta olabilir; ekranda kutusuna sığdırılarak gösterilir.
  - Eşya dosyasının yolu (`esyalar/sınıf/ad.png`) en fazla 255 bayt olabilir (Türkçe harfler 2 bayt sayılır). Daha uzun adlı dosya yüklenmez ve o yolu aşacak bir taşıma ya da yeniden adlandırma yapılmaz; sunucu nedenini söyler.
- **Dosyalar:** Üstteki "Dosyalar" düğmesi oyun klasörünü açar. Gezinebilir, yükleyebilir (sürükle-bırak da olur), indirebilir, metin dosyalarını düzenleyebilir, yeniden adlandırabilir, taşıyabilir ve silebilirsin.
  - Bir eşyanın resmini taşırsan envanterlerdeki yolu da güncellenir.
  - `durum.json` (oyunun kayıt dosyası) ile `haritalar` ve `esyalar` klasörleri silinemez.
- **Ayarlar:** Oyunun adı, oyun şifresi, sıra başında verilen enerji ve oyuncuları oyundan çıkarma. Şifreyi değiştirince oyuncular çıkar ve yeni şifreyle tekrar girer.
- **Gizli at:** Zar sonucunu yalnızca GM görür; oyuncular "GM gizli bir zar attı" notunu görür.

### Oyuncu

- Ana adresi aç, adını ve oyun şifresini yaz. Aynı adla tekrar girersen statların, envanterin ve yerin seni bekler.
- Sağ üstte kendi statların durur. Başkalarının statlarını göremezsin, yalnızca adlarını.
- Turlar başlamadan yürüyemez ve saldıramazsın; tur kartında "Sıra kimsede değil" yazar.
- **Sıra sendeyken yürümek için haritada gitmek istediğin yere tıkla.** İsteğin GM'e gider; onaylarsa yürürsün.
  - İmleç gezerken gideceğin yere uzaklık (kare) yanında görünür. Ne kadar enerji harcayacağını oyunun kuralları belirler.
  - Kurallar yürüyüşe izin vermezse (ör. enerjin yetmiyorsa) yerinde kalırsın ve nedeni ekranında çıkar.
- **Saldırmak için bir karaktere tıkla**, sonra hangi eşyanla saldıracağını seç. İstek GM'e gider.
- Bekleyen isteğini tur kartında görürsün; "Vazgeç" ile geri alabilirsin.
- Enerjin bitince sıra kendiliğinden geçer. Erken bitirmek için **Turu bitir**. Sıra sana gelince bildirim çıkar ve sekme başlığı "● Sıra sende" olur.
- **Görüş:** Haritada yalnızca görüş menzilini görürsün. Daha önce geçtiğin yerler karartılmış olarak kalır; oradaki karakterleri en son gördüğün haliyle (soluk) hatırlarsın, oradaki değişikliği oraya tekrar bakana kadar görmezsin.
- Zar tepsisi: tek tıkla d4–d100, kutuya `2d6+3 ateş topu` gibi ifadeler, Avantaj / Dezavantaj.
- Envanter sekmesinde eşyaların; tıklayınca büyür.

### Harita kontrolleri

| Hareket | Ne yapar |
| --- | --- |
| Boş bir yere tıkla | Oyuncu: yürüme isteği |
| Bir karaktere tıkla | Oyuncu: saldırı (eşya seçimi). GM: karakteri seçer. |
| Sürükle (boş yerde) | Haritayı kaydırır |
| Tokenı sürükle | GM: taşır |
| İstek hedefini sürükle | GM: yürüme hedefini kaydırır |
| Tekerlek / iki parmak | Yakınlaştırır, uzaklaştırır |
| Sağ tık (telefonda uzun bas) | Herkese işaret gösterir. GM Shift + sağ tıkla herkesin ekranını oraya kaydırır. |
| `+` `-` `0`, ok tuşları | Klavyeyle yakınlaştırma, sığdırma, kaydırma |

Zar ifadeleri: `d20`, `2d6+3`, `1d8+1d6-1`, `d%`, `4d6kh3` (en yüksek 3), `4d6dl1` (en düşüğü at), `2d20kh1` (avantaj), `2d20kl1` (dezavantaj). İfadeden sonra yazılan yazı etiket olur.

---

## 5. Görüş sisi nasıl çalışır

- Her oyuncunun görüşü, bulunduğu yerin çevresinde `gorus` kare yarıçaplı bir dairedir. Yürürken bu dairenin taradığı her yer keşfedilmiş sayılır.
- Oyuncu görüşündeki karakterleri canlı görür. Görüşünden çıkan karakteri en son gördüğü yerde, soluk haliyle hatırlar. O yere tekrar baktığında karakter orada değilse hatıra silinir.
- Görüşün dışında olup biteni (yürüyüş, saldırı, ölüm) sunucu o oyuncuya hiç göndermez; tarayıcıyı kurcalayarak da görülemez. Görüşünü kesen bir yürüyüşün yalnızca gördüğü kısmı oynar.
- Görüş duvarlardan geçer (harita bir resimdir, duvarları bilmez). Engelleri GM yürüme isteklerini onaylarken yönetir.
- Keşfedilenler ve hatıralar harita başına kaydedilir; aynı haritaya dönülünce kaldığı gibidir.

---

## 6. Güvenlik

- Bütün kurallar ve yetkiler sunucuda denetlenir. Oyuncu tarayıcıyı kurcalasa bile:
  - sırası gelmeden, enerjisi bitmişken ya da GM onaylamadan yürüyemez ve saldıramaz; yürüyüşün ve saldırının bedelini sunucudaki kuralların belirler,
  - görmediği bir karaktere saldıramaz,
  - başkasının statlarını, envanterini, gizli zarını, görüşü dışındakileri, gizli haritayı ve gizli NPC'leri göremez,
  - GM işlemi yapamaz.
- GM şifresi yalnızca `.env` dosyasındadır; sunucu onu diske yazmaz (yalnızca tuzlu bir özetini, şifre değişince eski GM oturumlarını kapatmak için tutar). GM şifresiyle oyun şifresi aynı olamaz.
- Oyun klasöründeki her yol denetlenir: `..`, gizli adlar ve sembolik bağlar reddedilir. Kimse klasörün dışına çıkamaz.
- Yüklenen dosyalar tarayıcıda betik çalıştıramaz: korumalı alanda (sandbox) ve gerekirse indirme olarak açılır.
- Oturumlar `HttpOnly` ve `SameSite=Strict` çerezlerle tutulur; başka sitelerden gelen istekler reddedilir. Hatalı şifre denemeleri IP başına sınırlıdır (15 dakikada 10).
- Zarlar işletim sisteminin güvenli rastgele kaynağıyla (`getrandom`) ve modulo yanlılığı olmadan atılır.

## 7. Yedekleme

Bütün oyun `oyun/` klasöründedir:

- `durum.json`: karakterler, statlar, envanterler, konumlar, keşifler, sıra, kayıt ve ayarlar
- `haritalar/`
- `esyalar/` (sınıf klasörleriyle)
- Senin eklediğin her şey

Bu klasörü kopyalamak yeterli. GM, Dosyalar ekranından `durum.json`'u indirerek de yedek alabilir. v3 ve v4'ün `durum.json`'u açılışta yeni biçime çevrilir (haritaların ızgarası kare sayısına dönüşür). Daha eski bir kayıt açılmaz; `durum-eski-….json` adıyla saklanır ve yeni oyun başlar.

## 8. Dosyalar

```
sunucu/kurallar.c    SENİN KURALLARIN: her_tur(), hareket(), saldiri(), eşya sınıfları
sunucu/kurallar.h    kurallarında kullanabileceklerin (Oyuncu, Esya, Harita, yardımcılar)
sunucu/main.c        HTTP sunucusu, yönlendirme, giriş, GM dosya yöneticisi
sunucu/oyun.c        oyun durumu: karakterler, görüş ve hafıza, istekler, tur, ölüm, envanter, kayıt
sunucu/geometri.c    görüş ve yürüyüş hesapları
sunucu/zar.c         zar ifadeleri
sunucu/ws.c          WebSocket
sunucu/http.c        HTTP istek/yanıt yardımcıları
sunucu/json.c        küçük JSON ayrıştırıcı
sunucu/dosya.c       güvenli yol denetimi, klasör listeleme
sunucu/gorsel.c      görsel türü ve boyutu (EXIF yönü dahil)
web/                 index.html, stil.css, js/ (ana, harita, sis, efekt, tur, kayit, envanter, gm, dosyalar)
kurulum/             Caddy, systemd ve nginx örnekleri
test/                birim testleri (make test) ve testlerin kullandığı kurallar
oyun/                ilk çalıştırmada oluşur: oyunun bütün dosyaları
```

## 9. Bilinen sınırlar

- Bir sunucu tek bir oyun çalıştırır; herkes aynı oyuna girer.
- Harita bir resimdir: duvarları bilmez. Görüş duvarlardan geçer, yürüyüş düz çizgidir. Engelleri GM yönetir.
- Konumlar, görüş ve keşif kare birimindedir. Oyun ilerledikten sonra harita boyutunu değiştirirsen her şey yeni karelere göre kayar; boyutu oyunun başında seç.
- `her_tur()`, `hareket()` ve `saldiri()` sunucunun içinde çalışır; sonsuz döngüye girerse sunucu donar.
