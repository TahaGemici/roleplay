# Roleplay

Kendi sunucunda çalışan, Fallout 1 gibi sıra tabanlı bir sanal oyun masası. Oyuncular sesli konuşur; ekranda harita, tur sırası, zarlar, statlar ve envanter durur.

- **GM ekranı:** `https://alanadin/gm`, GM şifresiyle girilir. GM her şeyi görür ve yönetir.
- **Oyuncu ekranı:** `https://alanadin/`, isim ve oyun şifresiyle girilir. Oyuncu yalnızca kendi statlarını ve görüş menzilindekini görür.
- **Sıra tabanlı:** Sırası gelen oyuncu haritaya tıklayıp yürür; bir karaktere tıklayıp saldırır ya da büyü yapar. Hepsi GM'in onayına gider.
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

İlk seferde `/var/lib/roleplay/.env` oluşur ve kurulum durur. `GM_SIFRESI`, `OYUN_SIFRESI` ve `ALAN_ADI` değerlerini yazıp komutu tekrar çalıştır:

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
| Güncelle | kurulum komutunun aynısı |
| Eski bir sürüme dön | kurulum komutunun sonuna `-s v41` ekle |
| Günlük | `journalctl -u systemd-nspawn@roleplay -f` |
| `.env` değişince | `sudo systemctl restart systemd-nspawn@roleplay` |
| Yedek al | `sudo tar -czf oyun-yedek.tgz -C /var/lib/roleplay oyun` |

Depo özelse sürümler herkese açık olmaz. Bu durumda sunucuda `gh auth login` ile giriş yapıp şunu çalıştır:

```bash
gh release download -R TahaGemici/roleplay -p roleplay.raw -p kur.sh --clobber && sudo sh kur.sh roleplay.raw
```

## Oyun kuralları: `sunucu/kurallar.c`

Sunucu dört fonksiyonunu çağırır:

- `her_tur(o)`: bir karakterin sırası başlarken. `o->enerji` önce GM'in ayarladığı değere doldurulur.
- `hareket(o, x, y)`: GM bir yürüyüşü onaylayınca. `1` döndürürsen karakter yürür, `0` döndürürsen yerinde kalır. Enerjiyi sen düşürürsün.
- `saldiri(saldiran, hedef, esya)`: GM bir saldırıyı onaylayınca.
- `buyu(yapan, hedef, buyu_adi)`: GM bir büyüyü onaylayınca. Hedef, yapanın kendisi de olabilir.

Ayrıca üç liste tanımlarsın: eşya sınıfları (`esya_siniflari`), büyüler (`buyuler`) ve herkeste olan bırakılamaz eşya (`yumruk`).

Enerjisi 0 ya da altına inen karakterin sırası biter. Saldırı ya da büyüyle canı 0 ya da altına inen hemen ölür; başka yoldan inen sırası bitince ölür.

```c
#include "kurallar.h"
#include <math.h>
#include <stddef.h>
#include <string.h>

const char *const esya_siniflari[] = { "silah", "zirh", "iksir", NULL };
const char *const buyuler[] = { "Ateş topu", "İyileştir", NULL };
const Esya yumruk = { .ad = "Yumruk", .sinif = "silah" };

void her_tur(Oyuncu *o)
{
    o->enerji = 5 + o->odak / 2;
}

int hareket(Oyuncu *o, float x, float y)
{
    float bedel = hypotf(x - o->x, y - o->y) + o->agirlik / 10; /* yük yürümeyi zorlaştırır */
    if (bedel > o->enerji) {
        bildir(o, "Enerjin yetmiyor.");
        return 0;
    }
    o->enerji -= bedel;
    return 1;
}

void saldiri(Oyuncu *saldiran, Oyuncu *hedef, Esya *esya)
{
    saldiran->enerji -= 4;
    if (zar("1d20") + saldiran->odak > 10 + hedef->zirh) {
        int hasar = esya->sabit ? zar("1d4") : zar("1d8"); /* yumruk daha az vurur */
        hedef->can -= hasar;
        duyur("%s vurdu: %d hasar.", saldiran->ad, hasar);
    } else {
        duyur("%s ıskaladı.", saldiran->ad);
    }
}

void buyu(Oyuncu *yapan, Oyuncu *hedef, const char *buyu_adi)
{
    yapan->enerji -= 2;
    if (strcmp(buyu_adi, "İyileştir") == 0) hedef->can += zar("1d6");
    else hedef->can -= zar("1d8");
}
```

**Kullanabileceklerin** (`sunucu/kurallar.h`):

- Karakter: `ad`, `x`, `y`, `can`, `odak`, `zirh`, `buyu_direnci`, `enerji`, `agirlik` (taşıdığı yük, salt okunur), `envanter[]` / `envanter_adet`, `npc`
- Eşya: `ad`, `sinif`, `resim`, `agirlik`, `sabit` (yumrukta 1)
- `zar("2d6+3")`, `zar_at(2, 6)`: zar atar. Sonucu GM ve o işteki oyuncular (yapan ve hedef) kayıtta görür.
- `duyur(...)`: herkese, `bildir(o, ...)`: o oyuncuya ve GM'e, `gm_bildir(...)`: yalnızca GM'e yazar
- `envanter_ekle(o, ad, sinif, resim)`, `envanter_cikar(o, sira)`: envanterde en fazla `ENVANTER_AZAMI` (12) eşya olur; `envanter[0]` her zaman yumruktur ve çıkarılamaz.
- `harita.genislik`, `harita.yukseklik`, `tur_sayisi`

Fonksiyonlar hızlı bitmeli; çalışırken sunucu bekler. Değişiklikten sonra `make test` ile dene, `main`'e push'la ve sunucuda kurulum komutunu çalıştır.

## Nasıl oynanır

**GM:**

- Harita yükler ve açar; ilk açışta kaç kare olduğunu seçer.
- Oyuncular girince kendiliğinden haritaya konmaz. GM onları soldaki listeden haritaya sürükleyip bırakarak yerleştirir; haritada olmayan oyuncunun sırası geçilir.
- Eşyaları katalogdan haritaya sürükler. Bir karakterin üstüne bırakılan eşya onun envanterine gider; boş yere bırakılan yerde durur ve üstüne yürüyen alır.
- Tur sırasını kurar, "Turları başlat"a basar; yürüme, saldırı ve büyü isteklerini onaylar ya da reddeder.
- Karakterlerin statlarını, eşyalarının ağırlığını, görünümünü (resim ve şapka) ve oyun klasöründeki dosyaları GM ekranından yönetir.
- Görüş menzilini Ayarlar'dan herkes için tek bir değer olarak belirler. Yeni bir maç için "Maçı sıfırla"ya basar.

**Oyuncu:**

- Sırası gelince haritaya tıklayıp yürür.
- Bir karaktere tıklayınca "Saldır" ya da "Büyü yap"ı seçer; kendine tıklayınca kendine büyü yapar.
- Envanterdeki bir eşyayı yere bırakabilir; yumruk bırakılamaz.
- Pulunun resmini ve şapkasını "Görünüm" düğmesinden kendisi seçer.
- Taşıdığı yük arttıkça haritadaki pulu büyür.
- Zar tepsisi `d20`, `2d6+3`, `4d6kh3` gibi ifadeleri anlar.

## Oyunun verisi ve güncellemeler

Oyunun bütün verisi `oyun/` klasöründedir (sunucuda `/var/lib/roleplay/oyun`):

- `durum.json`: karakterler, statlar, envanterler, konumlar, keşfedilen yerler, yerdeki eşyalar, tur sırası, kayıt, ayarlar
- `haritalar/`, `esyalar/`, `tokenlar/` (pul resimleri), `sapkalar/`

Güncellemeden sonra hiçbir şeyi silmen gerekmez: yeni sürüm eski `durum.json`'u okur, eksik alanları tamamlar. Yeni bir maça başlamak için GM ekranındaki **Maçı sıfırla** düğmesini kullan. Bu düğme `durum.json`'daki maç bilgilerini siler; oyuncular, ayarlar ve haritalar kalır. Önceki hali `durum-sifirlamadan-once.json` olarak saklanır.

## Sınırlar

- Bir sunucu tek bir oyun çalıştırır.
- Harita bir resimdir, duvarları bilmez: görüş duvarlardan geçer. Engelleri GM yönetir.
- Yedek için `oyun/` klasörünü kopyalamak yeter.
