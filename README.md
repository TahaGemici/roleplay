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
- **Kurulum otomatik:** `main`'e push'ladığında GitHub Actions testleri çalıştırır, yarım megabaytlık bir systemd-nspawn imajı üretir, temiz bir Ubuntu'da kurup dener ve yeni bir sürüm yayınlar. Sunucu repoyu değil yalnızca son sürümü indirir; kurmak da güncellemek de tek komut.

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

### Sürümler

Projeyi GitHub'a koy. `main` dalına her push'ta GitHub Actions (`.github/workflows/imaj.yml`):

1. birim testlerini çalıştırır,
2. sunucuyu musl ile statik derleyip `zar-masasi.raw` imajını paketler,
3. imajı temiz bir Ubuntu'da `kur.sh` ile kurup systemd-nspawn'da dener: açılıyor mu, GM girebiliyor mu, durdurunca oyunu kaydediyor mu,
4. hepsi geçerse deponun **Releases** sayfasında yeni bir sürüm açar (`v1`, `v2`, ...): `zar-masasi.raw`, `kur.sh` ve `SHA256SUMS`.

İmaj yarım megabaytlık, salt okunur bir dosya sistemidir: statik derlenmiş sunucu, web arayüzü ve konteynerin ayarları. Kabuk, paket yöneticisi, kütüphane yoktur. Oyunun verisi ve `.env` imajda değil, sunucudaki `/var/lib/zar-masasi` klasöründedir; imajı değiştirmek oyuna dokunmaz. Konteyner sunucunun ağını paylaşır, içindeki root ise sunucuda hiçbir yetkisi olmayan ayrı bir kullanıcıdır.

### Sunucuda

Gerekenler: systemd 255 ya da yenisi (Ubuntu 24.04+, Debian 13+). Sunucuya repo inmez; kurmak, başlatmak ve güncellemek hep aynı komut:

```bash
sudo apt install -y systemd-container curl
curl -fsSL https://github.com/KULLANICI/zar-masasi/releases/latest/download/kur.sh | sudo sh
```

(`KULLANICI` yerine GitHub kullanıcı adın.) Komut son sürümü indirir, SHA-256 ile doğrular ve kurar. İlk seferde `/var/lib/zar-masasi/.env` dosyasını oluşturup durur: GM şifresini yaz ve komutu yeniden çalıştır. "Zar Masası çalışıyor" satırını görmelisin.

```bash
sudo nano /var/lib/zar-masasi/.env      # GM_SIFRESI, OYUN_SIFRESI, ALAN_ADI
```

Son olarak HTTPS'i [Caddy](https://caddyserver.com) sağlar: sertifikayı kendisi alır, yeniler ve WebSocket bağlantılarını ek ayarsız taşır. Program dışarıya kapalıdır, herkes yalnızca `https://` ile bağlanır.

```bash
sudo apt install -y caddy
sudo tee /etc/caddy/Caddyfile <<'SON'
oyun.alanadin.com {
	encode zstd gzip
	reverse_proxy 127.0.0.1:3000
	header Strict-Transport-Security "max-age=31536000"
}
SON
sudo systemctl reload caddy
```

Sunucunda zaten nginx varsa Caddy yerine `kurulum/nginx.conf`'u kullan.

GM `https://oyun.alanadin.com/gm` adresini açar ve GM şifresini yazar. Konteyner sunucu her açıldığında kendiliğinden başlar, program çökerse yeniden başlar.

| İş | Komut |
| --- | --- |
| Güncelle (push'tan sonra, Actions bitince) | kurulum komutunun aynısı |
| Belirli bir sürüme geç ya da geri dön | kurulum komutunun sonuna `-s v41` ekle |
| Günlüğü izle | `journalctl -u systemd-nspawn@zar-masasi -f` |
| `.env`'i değiştirdikten sonra | `sudo systemctl restart systemd-nspawn@zar-masasi` |
| Durdur / başlat | `sudo machinectl stop zar-masasi` / `sudo machinectl start zar-masasi` |
| Terminalde çalıştır (önce durdur; Ctrl+C oyunu kaydedip kapatır) | `sudo systemd-nspawn -M zar-masasi` |
| Yedek al | `sudo tar -czf oyun-yedek.tgz -C /var/lib/zar-masasi oyun` |

Güncellerken yeni imaj doğrulanıp çalışanın yerine konur ve konteyner yeniden başlar; eski sunucu kapanırken oyunu kaydeder. `.env`'e ve oyunun dosyalarına dokunulmaz. Konteynerin ayarları imajın içinde gelir, yani her sürüm kendi ayarlarıyla kurulur.

**Depo özelse** sürümler herkese açık değildir. Sunucuda [GitHub CLI](https://cli.github.com) ile bir kere giriş yap (`gh auth login`), sonra kurmak ve güncellemek için:

```bash
gh release download -R KULLANICI/zar-masasi -p zar-masasi.raw -p kur.sh --clobber
sudo sh kur.sh zar-masasi.raw
```

**Kendi imajın:** `sudo apt install make musl-tools squashfs-tools`, sonra `make imaj` sürüm dosyalarını `cikti/` klasörüne yazar; `sudo sh cikti/kur.sh cikti/zar-masasi.raw` ile kurarsın.

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

- Değiştirdikten sonra commit'le ve `main`'e push'la. Actions yeni sürümü birkaç dakikada üretip dener; sonra sunucuda kurulum komutunu çalıştır. Push'lamadan önce kendi bilgisayarında `make test` ve `make && ./zar-masasi` ile deneyebilirsin.
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
- Sunucuda program salt okunur bir imajda, kendi kullanıcı ad alanında çalışır: içerideki root sunucuda ayrıcalıksız bir kullanıcıdır (UID 1514995712), hiçbir yetkisi (capability) yoktur ve yenisini alamaz. Sunucunun dosyalarından yalnızca `/var/lib/zar-masasi`'yı görür; imajda kabuk ya da başka bir program yoktur. Program statik ve adresleri her açılışta rastgele (static-pie) derlenir.

## 7. Yedekleme

Bütün oyun sunucudaki `/var/lib/zar-masasi/oyun` klasöründedir (kendi bilgisayarında denerken `oyun/`):

- `durum.json`: karakterler, statlar, envanterler, konumlar, keşifler, sıra, kayıt ve ayarlar
- `haritalar/`
- `esyalar/` (sınıf klasörleriyle)
- Senin eklediğin her şey

Bu klasörü kopyalamak yeterli. GM, Dosyalar ekranından `durum.json`'u indirerek de yedek alabilir. `durum.json` okunamazsa (ör. elle bozulduysa) üzerine yazılmaz: `durum-bozuk-….json` adıyla saklanır ve yeni oyun başlar.

## 8. Dosyalar

```
sunucu/kurallar.c          SENİN KURALLARIN: her_tur(), hareket(), saldiri(), eşya sınıfları
sunucu/kurallar.h          kurallarında kullanabileceklerin (Oyuncu, Esya, Harita, yardımcılar)
sunucu/main.c              HTTP sunucusu, yönlendirme, giriş, GM dosya yöneticisi
sunucu/oyun.c              oyun durumu: karakterler, görüş ve hafıza, istekler, tur, ölüm, envanter, kayıt
sunucu/geometri.c          görüş ve yürüyüş hesapları
sunucu/zar.c               zar ifadeleri
sunucu/ws.c                WebSocket
sunucu/http.c              HTTP istek/yanıt yardımcıları
sunucu/json.c              küçük JSON ayrıştırıcı
sunucu/dosya.c             güvenli yol denetimi, klasör listeleme
sunucu/gorsel.c            görsel türü ve boyutu (EXIF yönü dahil)
web/                       index.html, stil.css, js/ (ana, harita, sis, efekt, tur, kayit, envanter, gm, dosyalar)
kurulum/imaj-olustur.sh    sürüm dosyalarını üretir (make imaj): imaj, kur.sh, SHA256SUMS
kurulum/kur.sh             sunucuda kurar, başlatır ve günceller; her sürümle yayınlanır
kurulum/zar-masasi.nspawn  konteynerin ayarları (imajın içinde gelir)
kurulum/servis.conf        çökünce yeniden başlatma (imajın içinde gelir)
kurulum/Caddyfile          HTTPS için Caddy ayarı (nginx isteyene: kurulum/nginx.conf)
.github/workflows/imaj.yml main'e her push'ta test, imaj, Ubuntu'da deneme ve yeni sürüm
test/                      birim testleri (make test) ve testlerin kullandığı kurallar
oyun/                      ilk çalıştırmada oluşur: oyunun bütün dosyaları
cikti/                     make imaj'ın çıktısı: zar-masasi.raw, kur.sh, SHA256SUMS
```

## 9. Bilinen sınırlar

- Bir sunucu tek bir oyun çalıştırır; herkes aynı oyuna girer.
- Harita bir resimdir: duvarları bilmez. Görüş duvarlardan geçer, yürüyüş düz çizgidir. Engelleri GM yönetir.
- Konumlar, görüş ve keşif kare birimindedir. Oyun ilerledikten sonra harita boyutunu değiştirirsen her şey yeni karelere göre kayar; boyutu oyunun başında seç.
- `her_tur()`, `hareket()` ve `saldiri()` sunucunun içinde çalışır; sonsuz döngüye girerse sunucu donar.
