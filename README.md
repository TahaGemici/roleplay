# Roleplay

Kendi sunucunda çalışan, Fallout 1 gibi sıra tabanlı bir sanal oyun masası. Oyuncular sesli konuşur; ekranda harita, tur sırası, zarlar, statlar ve envanter durur.

- **GM ekranı:** `https://alanadin/gm`, GM şifresiyle girilir. GM her şeyi görür ve yönetir.
- **Oyuncu ekranı:** `https://alanadin/`, isim ve oyun şifresiyle girilir. Oyuncu yalnızca kendi statlarını ve görüş menzilindekini görür; duvarların arkasını görmez, haritanın nerede bittiğini bilmez.
- **Sıra tabanlı:** Her sıra iki aşamalıdır: sırası gelen önce haritaya tıklayıp yürür (ya da yürümeden geçer), sonra bir karaktere tıklayıp saldırır ya da büyü yapar; sonra sıra geçer. Hepsi GM'in onayına gider. NPC'ler de sıraya girer.
- **Tepkiler:** Alkış, kahkaha, ağlama ve bağırma. Bağırmayı duvarların arkasındakiler de duyar.
- **Bağlantı:** Sağ üstte oyundakilerin (GM dahil) adının yanında bağlantılarının kalitesi ve gecikmesi görünür.
- **Duvarlar:** GM haritanın üstüne duvar çizer. Duvarın üstünden geçilmez, görüş sisi açıkken arkası görülmez. Yürüyüş duvarların etrafından dolaşan en kısa yoldan gider; yol haritada çizilir.
- **Birden çok harita:** Oyuncular farklı haritalarda olabilir; aralarında GM'in koyduğu portallarla geçerler.
- **Ekonomi:** Altın, gümüş ve bakır ayrı ayrı sayılır, birbirine çevrilmez; eşya takası, ölünün üstünü arama, her eşyada dayanıklılık.
- **Sesli:** Her görsel efektin (yürüme, saldırı, büyü, ölüm, portal, zar...) bir sesi vardır; üst çubuktaki 🔊 ile kapanır.
- **Kurallar senin:** Enerji, hareket, saldırı ve büyü kurallarını `sunucu/kurallar.c` içinde C ile yazarsın.
- Sunucu C ile, arayüz düz HTML/CSS/JS ile yazıldı; bağımlılık yok.

## Kendi bilgisayarında dene

```bash
make
GM_SIFRESI=gizli-gm-sifresi OYUN_SIFRESI=deneme ./roleplay
```

GM `http://127.0.0.1:3000/gm` adresini, oyuncular `http://127.0.0.1:3000` adresini açar. `make test` birim testlerini çalıştırır.

## Sunucuya kur

`main`'e her push'ta GitHub Actions testleri çalıştırır, imajı üretir, dener ve **Releases**'a yeni bir sürüm koyar. Sunucuda (Ubuntu 24.04+ ya da Debian 13+) kurmak da güncellemek de aynı komut:

```bash
sudo apt install -y systemd-container curl
```

```bash
curl -fsSL https://github.com/TahaGemici/roleplay/releases/latest/download/kur.sh | sudo sh
```

Komut her çalıştığında oyun klasörü (`/var/lib/roleplay/oyun`) silinir ve yeni sürüm boş bir oyunla başlar; `.env` kalır. İlk seferde `/var/lib/roleplay/.env` oluşur ve kurulum durur. `GM_SIFRESI`, `OYUN_SIFRESI` ve `ALAN_ADI` değerlerini yazıp komutu tekrar çalıştır:

```bash
sudo nano /var/lib/roleplay/.env
```

HTTPS için Caddy kur (alan adının A kaydı sunucuyu göstermeli, 80 ve 443 açık olmalı):

```bash
sudo apt install -y caddy
sudo tee /etc/caddy/Caddyfile <<'SON'
oyun.alanadin.com {
	reverse_proxy 127.0.0.1:3000
}
SON
sudo systemctl reload caddy
```

nginx kullanıyorsan `kurulum/nginx.conf`'a bak.

| İş | Komut |
| --- | --- |
| Güncelle (oyun silinir) | kurulum komutunun aynısı |
| Eski bir sürüme dön (oyun silinir) | kurulum komutunun sonuna `-s v41` ekle |
| Günlük | `journalctl -u systemd-nspawn@roleplay -f` |
| `.env` değişince (oyun kalır) | `sudo systemctl restart systemd-nspawn@roleplay` |

Güncellemeden sonra açık GM ve oyuncu sayfaları kendini yeniler. Tarayıcı oyunun sürümünü kendi belleğinde tutar; sürüm değişmişse eski bellek (yerel depolama ve önbellek) kendiliğinden silinir, kimsenin elle temizlemesi gerekmez.

Depo özelse sürümler herkese açık olmaz. Bu durumda sunucuda `gh auth login` ile giriş yapıp şunu çalıştır:

```bash
gh release download -R TahaGemici/roleplay -p roleplay.raw -p kur.sh --clobber && sudo sh kur.sh roleplay.raw
```

## Oyun kuralları: `sunucu/kurallar.c`

Her sıra iki aşamalıdır: sırası gelen karakter önce yürür (ya da yürümeden geçer), sonra bir eylem yapar (saldırı ya da büyü) ya da eylem yapmadan turunu bitirir; sonra sıra geçer. Sunucu beş fonksiyonunu çağırır:

- `her_tur(o)`: bir karakterin (oyuncu ya da NPC) sırası başlarken, yürümeden önce.
- `hareket(o, x, y)`: yürüme aşaması. GM bir yürüyüşü onaylayınca ya da sırası gelen NPC'yi haritada sürükleyince. Karakter yürümeden geçerse ("Yürümeden geç", "Turu bitir" ya da yürümeden saldırıp büyü yaparsa) `yol_uzunlugu` 0 ile çağrılır. `1` döndürürsen karakter yürür ve eylem aşaması başlar (bir turda bir kez yürünür); `0` döndürürsen yerinde kalır ve başka bir yer seçebilir. Arada duvar varsa karakter duvarların etrafından dolaşır; yürüyeceği yolun uzunluğu `yol_uzunlugu`'dur.
- `saldiri(saldiran, hedef, esya)`: eylem aşaması; GM bir saldırıyı onaylayınca. `1` döndürürsen saldırı olur ve sıra geçer; `0` döndürürsen olmaz (ör. enerjisi yetmiyorsa), karakter başka bir şey deneyebilir ya da turu bitirebilir.
- `buyu(yapan, hedef, buyu_adi)`: eylem aşaması; GM bir büyüyü onaylayınca. Hedef, yapanın kendisi de olabilir. Dönüş değeri `saldiri()`'deki gibidir.
- `eylem_yok(o)`: karakter saldırmadan, büyü yapmadan turunu bitirince ("Turu bitir"e bastı ya da GM sırasını geçti). Dönüşte sıra geçer.

Ayrıca üç liste tanımlarsın: eşya sınıfları (`esya_siniflari`), büyüler (`buyuler`) ve herkeste olan bırakılamaz eşya (`yumruk`).

Sunucu enerjiye hiç dokunmaz ve sırayı enerjiye göre geçirmez: enerjinin ne kadar kazanılıp harcanacağını bu fonksiyonlarda sen yazarsın. Saldırı, büyü ya da `her_tur()` ile canı 0 ya da altına inen hemen ölür; başka yoldan inen sırası bitince ölür.

Şu anki kurallar:

- 4 birim ya da daha az yürüyen (hiç yürümeyen de) 1 enerji kazanır. Daha fazla yürüyen enerji kazanmaz, 4'ü aşan her birim için 1 enerji harcar. Bir turda en fazla 8 birim yürünür; enerjisi yetmiyorsa yettiği kadar (4 + enerjisi) yürür.
- Saldıran ya da büyü yapan 1 enerji harcar (enerjisi 1'den azsa saldıramaz, büyü yapamaz). Eylem yapmayan 1 enerji kazanır. Sonra tur biter.

```c
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
```

Hasar, iyileşme, ıskalama gibi şeyleri de bu fonksiyonlara sen eklersin; ör. saldırıda `if (zar("1d20") > 10 + hedef->zirh) hedef->can -= zar("1d8");`.

**Kullanabileceklerin** (`sunucu/kurallar.h`):

- Karakter: `ad`, `x`, `y`, `can`, `odak`, `zirh`, `buyu_direnci`, `enerji`, `azami` (statların azamileri), `agirlik` (70 + eşyalarının ağırlığı, salt okunur; 70 = `KARAKTER_AGIRLIGI`), `altin`, `gumus`, `bakir`, `envanter[]` / `envanter_adet`, `npc`
- Azamiler: `o->azami.can`, `o->azami.odak`, `o->azami.zirh`, `o->azami.buyu_direnci`, `o->azami.enerji`. GM her karakter için belirler (0: azamisi yok); sunucu statı azamisinin üstünde bırakmaz (can 40, azami can 35 ise can 35 olur). Ağırlığın azamisi yoktur. Ekranda `22/35` diye görünür.
- Konum: normalde kare cinsinden kesirli bir sayıdır, `(0,0)` haritanın sol üst köşesi. GM Ayarlar'da **Konumlar tam sayı olsun**'u açarsa herkes kare ortasında durur ve kurallar karenin numarasını görür: `o->x`, `o->y` ve `hareket()`'e gelen `x`, `y` hep tam sayıdır (`(0,0)` sol üstteki kare). Seçenek herkes için aynıdır.
- Eşya: `ad`, `sinif`, `resim` (resmi yoksa `""`), `agirlik`, `dayaniklilik` (yeni eşyada 100), `sabit` (yumrukta 1)
- Para üç ayrı sikkedir, birbirine çevrilmez: `o->altin += 2; o->gumus += 5;` (hiçbiri 0'ın altına inmez)
- Dayanıklılığa sunucu hiç dokunmaz: azaltmak, 0'a inince eşyayı kırmak (`envanter_cikar`) senin işin.
- `zar("2d6+3")`, `zar_at(2, 6)`: zar atar. Sonucu GM ve o işteki oyuncular (yapan ve hedef) kayıtta görür.
- `duyur(...)`: herkese, `bildir(o, ...)`: o oyuncuya ve GM'e, `gm_bildir(...)`: yalnızca GM'e yazar. Kayıtta her satırın yanında kimin gördüğü yazar ("herkes", "sen ve GM", "yalnızca GM"...).
- `envanter_ekle(o, ad, sinif, resim)`, `envanter_cikar(o, sira)`: envanterde yumruğun yanında en fazla `ENVANTER_AZAMI` (12) eşya olur; yumruk yer kaplamaz. `envanter[0]` her zaman yumruktur ve çıkarılamaz.
- `harita.genislik`, `harita.yukseklik`, `tur_sayisi`
- `hareket()` içinde: `yol_uzunlugu` (duvarların etrafından dolaşan yolun uzunluğu, kare) ve `yol_noktasi(u, &x, &y)` (yolun `u` kadar ilerisindeki nokta; konumlar tam sayıysa o noktanın karesi). Enerjinin yettiği yere kadar yürütmek için: `yol_noktasi(o->enerji, &o->x, &o->y); return 1;`
- `o->x`, `o->y`'yi başka bir fonksiyonda değiştirirsen karakter oraya düz çizgide itilir; arada duvar varsa duvarın önünde durur.

Fonksiyonlar hızlı bitmeli; çalışırken sunucu bekler. Değişiklikten sonra `make test` ile dene, `main`'e push'la ve sunucuda kurulum komutunu çalıştır.

## Nasıl oynanır

**GM:**

- Harita yükler ve açar; ilk açışta kaç kare olduğunu seçer. Başka bir haritaya bakmak kimseyi taşımaz: her oyuncu kendi haritasında kalır. Boyutu sonradan değiştirirse duvarlar, pullar, eşyalar ve portallar resmin üstünde yerinde kalır.
- Haritayı açınca "Duvarlar" bölümünden duvarları çizer: "Duvar çiz"e basıp köşeden köşeye tıklar (uçlara ve ızgara köşelerine yapışır, Shift basılıyken yapışmaz). Son noktaya bir daha tıklamak, çift tıklamak, sağ tıklamak ya da Esc çizgiyi bitirir; Ctrl+Z son duvarı geri alır, "Duvar sil" tıklanan duvarı siler. Kapı yoktur: kapıyı açmak için o duvarı siler, kapatmak için yeniden çizer. Duvarları yalnızca GM görür; oyuncular haritanın resmindeki duvarları görür.
- Bir pulu sürükleyince pul duvarların etrafından yürür; oraya yol yoksa (kapalı oda, duvarın öbür yanı) ışınlanır.
- Oyuncular girince kendiliğinden haritaya konmaz. GM onları soldaki listeden baktığı haritaya sürükleyip bırakarak yerleştirir (başka haritadakini de buraya getirir); haritada olmayanın sırası geçilir. Liste, karakterleri gruplar: haritada olmayanlar, sonra her harita ayrı (baktığın harita işaretli). Gruplar başlıklarına tıklayınca açılıp kapanır; ↗ o haritayı açar, "Haritasına git" o karakterin haritasını.
- Haritada bir karaktere tıklayınca (sürüklemeden) düzenleyicisi bir pencerede açılır: statlar ve azamileri, para, notlar, eşyalar, işlemler. Panelin sağ kenarı sürüklenerek genişletilir (çift tıklama varsayılana döndürür).
- Portalı "Portallar" bölümünden haritaya sürükler, sonra hedef haritayı ve nereye çıkılacağını seçer. Yürüyüşü portalda biten oyuncu o haritaya geçer. Oyuncular portalı görür ama nereye çıktığını bilmez.
- Eşyaları "Eşya ekle" ile oluşturur: adı ve sınıfı yeter, resim isteğe bağlıdır. Resimsiz eşya, sınıfına göre varsayılan bir resimle (kılıç, kalkan, şişe ya da kese) görünür; `esyalar/<sınıf>/<ad>.esya` adlı boş bir dosyadır.
- Eşyaları katalogdan haritaya sürükler. Bir karakterin üstüne bırakılan eşya onun envanterine gider; boş yere bırakılan yerde durur.
- Yerdeki eşyalara tıklayınca seçer; Ctrl ya da Shift ile tıklayarak birden çoğunu seçer, boş yerde Shift'e basılı tutup sürükleyerek kutu içindekileri seçer. Seçilenler birlikte sürüklenir. Haritanın altındaki çubuktan ya da kısayollarla kopyalar (Ctrl+D yanına, Ctrl+C ve Ctrl+V imlecin olduğu yere) ve kaldırır (Delete); Esc seçimi bırakır. Seçili tek eşyaya bir daha tıklamak ya da çift tıklamak ayrıntısını açar.
- NPC'leri yönetir: "NPC ekle"ye tıklarsa hiçbir haritada olmayan bir NPC oluşur (sonra listeden haritaya sürüklenir); düğmeyi haritaya sürüklerse NPC bırakılan yerde oluşur. Statlarını, azamilerini, parasını, envanterini ve notunu (yalnızca GM görür; NPC'nin ne yaptığını, ne bildiğini yazmak için) düzenler; adına tepki verir; bir NPC adına oyuncuyla takas yapar ("Takas…"). "Kopyala" seçili NPC'nin aynısını (adı, statları, parası, envanteri, notu, görünümü aynı) yanına koyar; haritada olmayan NPC'nin kopyası da haritada olmaz. Asıl NPC sıradaysa kopyası hemen arkasına girer. "Haritadan kaldır" NPC'yi de haritadan alır.
- Tur sırasını kurar, "Turları başlat"a basar; yürüme, saldırı ve büyü isteklerini onaylar ya da reddeder. Tur kartında sıradakinin hangi aşamada olduğu (1 Yürü, 2 Eylem) görünür; "Sıradakine geç" sırayı eylemsiz bitirir. Yeni NPC'ler de sıraya girer: sırası gelen NPC'yi yürüme aşamasında haritada sürüklerse NPC `hareket()`'e göre yürür (ya da GM tur kartındaki "Yürümeden geç"e basar); sonra NPC'ye Karakterler bölümünden saldırtır ya da büyü yaptırır (olunca sırası biter) ya da "Sıradakine geç"e basar. Shift'e basılı tutarak bırakırsa NPC kurallara bakmadan taşınır.
- Karakterlerin statlarını ve azamilerini (ağırlık hariç; ekranda `22/35`), paralarını, eşyalarının ağırlığını ve dayanıklılığını, görünümünü (resim ve şapka) ve oyun klasöründeki dosyaları GM ekranından yönetir.
- Ayarlar'dan görüş menzilini herkes için tek bir değer olarak belirler; **Konumlar tam sayı olsun**'u açarsa herkes kare ortasında durur ve kurallar konumu kare numarası olarak görür. Yeni bir maç için "Maçı sıfırla"ya basar.

**Oyuncu:**

- Sırası gelince önce yürür: haritaya tıklar ya da "Yürümeden geç"e basar. Sonra eylem: bir karaktere tıklayıp saldırır ya da büyü yapar; olunca sırası biter. Eylem yapmayacaksa "Turu bitir"e basar. Hangi aşamada olduğu tur kartında görünür (1 Yürü, 2 Eylem).
- Arada duvar varsa yol duvarların etrafından dolaşır; tıklamadan önce yol ve uzunluğu haritada çizilir. Yolu olmayan yere ("Yol yok") yürünmez. Yolu üstündeki ya da yürüyüşünün bittiği yerdeki eşyayı, envanteri dolu değilse alır.
- Görüş sisi açıkken duvarların arkasını görmez. Gördüğü yerleri karartılmış ve hafif bulanık olarak hatırlar; başkalarının yürüyüşünün de yalnızca gördüğü kısmını görür.
- Sağ tıkla (dokunmatikte uzun basarak) işaret bırakır. İşareti yalnızca orayı gören oyuncular görür ve duyar; GM hepsini görür.
- Haritanın sol altındaki düğmelerle tepki verir: alkış, kahkaha, ağlama, bağırma. Tepkiyi onu görenler görür; bağırmayı aynı haritada görüş menzili kadar yakındaki (çapı menzilin iki katı olan çemberdeki) herkes duvarlardan bağımsız duyar, görmeyen sesin geldiği yeri görür.
- Bir karaktere tıklayınca "Saldır", "Büyü yap" ya da "Takas"ı seçer; kendine tıklayınca kendine büyü yapar. Yürümeden saldırır ya da büyü yaparsa o tur yürümeden geçmiş sayılır. Takas her zaman yapılabilir (sıra beklemez).
- Takasta iki taraf da en fazla 4 çeşit eşya ya da para koyar; kimse karşısının envanterini görmez, yalnızca ortaya konanları görür. İkisi de onaylayınca takas olur; teklif değişince onaylar düşer.
- Yanındaki bir ölüye tıklayınca üstünü arar: eşyalarını ve parasını tek tek ya da hepsini birden alır.
- Envanterdeki bir eşyayı önüne bırakabilir (en son yürüdüğü yönde, hiç yürümediyse aşağıda) ya da yok edebilir; yumruk bırakılamaz, yok edilemez ve envanterde yer kaplamaz.
- Gördüğü bir eşyayı görüşünden çıkınca da hatırlar: biri onu alsa bile, gidip yeniden bakana kadar eşya orada görünür.
- Pulunun resmini ve şapkasını "Görünüm" düğmesinden kendisi seçer; pulu için resim yükleyebilir (PNG, JPG, WEBP ya da GIF, en fazla 8 MB, 40 resim) ve yüklediklerini silebilir. Orada yalnızca kendi yüklediği resimleri görür; GM'in hazırladığı resimler oyunculara görünmez.
- Durum kartında statları azamileriyle (`22/35`) ve parasını altın, gümüş, bakır ayrı ayrı görür.
- Ağırlığı (70 + eşyalarının ağırlığı) arttıkça haritadaki pulu büyür: 70'te 1 kat, 140'ta 2 kat.
- Zar tepsisi `d20`, `2d6+3`, `4d6kh3` gibi ifadeleri anlar. Kayıtta her zarın simgesinde türü (d4 için üçgende 4), yanında çıkan sayı yazar.

## Oyunun verisi

Oyunun bütün verisi `oyun/` klasöründedir (sunucuda `/var/lib/roleplay/oyun`):

- `durum.json`: karakterler, statlar ve azamileri, paralar, envanterler, konumlar ve haritaları, NPC notları, duvarlar, keşfedilen yerler ve görülen duvarlar, hatırlanan eşyalar, yerdeki eşyalar, portallar, tur sırası, kayıt, ayarlar
- `haritalar/`, `esyalar/`, `tokenlar/` (pul resimleri; oyuncuların kendi yükledikleri `tokenlar/oyuncular/<kimlik>/` altında), `sapkalar/`

Kurulum komutu (güncelleme ve eski sürüme dönüş de) her çalıştığında bu klasörü siler: yeni sürüm eski sürümün kaydını okumaz, boş bir oyunla başlar. `.env` (şifreler ve alan adı) silinmez. Haritaların ve resimlerin aslını kendi bilgisayarında sakla; güncellemeden sonra GM ekranındaki **Dosyalar**'dan yeniden yüklersin. Kendi bilgisayarında çalıştırırken `oyun/` silinmez; sunucu başka bir sürümün `durum.json`'unu okumaz, kenara alıp yeni bir oyunla başlar.

Aynı sürümde yeni bir maça başlamak için GM ekranındaki **Maçı sıfırla** düğmesini kullan: maç bilgileri silinir; oyuncular, ayarlar, haritalar ve portallar kalır. Önceki hali `durum-sifirlamadan-once.json` olarak saklanır.

## Sınırlar

- Bir sunucu tek bir oyun çalıştırır.
- Duvarı GM çizer: harita resmindeki duvarları sunucu kendisi tanımaz. Duvar incedir, kapı ve pencere yoktur; karakterler birbirinin içinden geçebilir.
- Sesler tarayıcıda üretilir; tarayıcı ilk tıklamadan önce ses çalmaz.
