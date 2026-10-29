# Zar Masası

Kendi sunucunda çalışan, Fallout 1 gibi sıra tabanlı bir sanal oyun masası. Oyuncular sesli konuşur; ekranda harita, tur sırası, zarlar, statlar ve envanter durur.

- **GM ekranı:** `https://alanadin/gm`, GM şifresiyle girilir. GM her şeyi görür ve yönetir.
- **Oyuncu ekranı:** `https://alanadin/`, isim ve oyun şifresiyle girilir. Oyuncu yalnızca kendi statlarını ve görüş menzilindekini görür.
- **Sıra tabanlı:** Sırası gelen oyuncu haritaya tıklayıp yürür ya da bir karaktere tıklayıp saldırır. İkisi de GM'in onayına gider.
- **Kurallar senin:** Enerji, hareket ve saldırı kurallarını `sunucu/kurallar.c` içinde C ile yazarsın.
- Sunucu C ile, arayüz düz HTML/CSS/JS ile yazıldı; bağımlılık yok.

## Kendi bilgisayarında dene

```bash
make
GM_SIFRESI=gizli-gm-sifresi OYUN_SIFRESI=deneme ./zar-masasi
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

İlk seferde `/var/lib/zar-masasi/.env` oluşur ve kurulum durur. `GM_SIFRESI`, `OYUN_SIFRESI` ve `ALAN_ADI` değerlerini yazıp komutu tekrar çalıştır:

```bash
sudo nano /var/lib/zar-masasi/.env
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
| Günlük | `journalctl -u systemd-nspawn@zar-masasi -f` |
| `.env` değişince | `sudo systemctl restart systemd-nspawn@zar-masasi` |
| Yedek al | `sudo tar -czf oyun-yedek.tgz -C /var/lib/zar-masasi oyun` |

Depo özelse sürümler herkese açık olmaz. Bu durumda sunucuda `gh auth login` ile giriş yapıp şunu çalıştır:

```bash
gh release download -R TahaGemici/roleplay -p zar-masasi.raw -p kur.sh --clobber && sudo sh kur.sh zar-masasi.raw
```

## Oyun kuralları: `sunucu/kurallar.c`

Sunucu üç fonksiyonunu çağırır:

- `her_tur(o)`: bir karakterin sırası başlarken. `o->enerji` önce GM'in ayarladığı değere doldurulur.
- `hareket(o, x, y)`: GM bir yürüyüşü onaylayınca. `1` döndürürsen karakter yürür, `0` döndürürsen yerinde kalır. Enerjiyi sen düşürürsün.
- `saldiri(saldiran, hedef, esya)`: GM bir saldırıyı onaylayınca.

Enerjisi 0 ya da altına inen karakterin sırası biter. Sırası bittiğinde canı 0 ya da altında olan herkes ölür.

```c
#include "kurallar.h"
#include <math.h>
#include <stddef.h>

const char *const esya_siniflari[] = { "silah", "zirh", "iksir", NULL };

void her_tur(Oyuncu *o)
{
    o->enerji = 5 + o->odak / 2;
}

int hareket(Oyuncu *o, float x, float y)
{
    float bedel = hypotf(x - o->x, y - o->y);       /* kare başına 1 enerji */
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
        int hasar = zar("1d8");
        hedef->can -= hasar;
        duyur("%s vurdu: %d hasar.", saldiran->ad, hasar);
    } else {
        duyur("%s ıskaladı.", saldiran->ad);
    }
}
```

**Kullanabileceklerin** (`sunucu/kurallar.h`):

- Karakter: `ad`, `x`, `y`, `can`, `odak`, `zirh`, `buyu_direnci`, `enerji`, `gorus`, `envanter[]` / `envanter_adet`, `npc`
- Eşya: `ad`, `sinif`, `resim`
- `zar("2d6+3")`, `zar_at(2, 6)`: zar atar
- `duyur(...)`: herkese, `bildir(o, ...)`: o oyuncuya ve GM'e, `gm_bildir(...)`: yalnızca GM'e yazar
- `envanter_ekle(o, ad, sinif, resim)`, `envanter_cikar(o, sira)`
- `harita.genislik`, `harita.yukseklik`, `tur_sayisi`

Fonksiyonlar hızlı bitmeli; çalışırken sunucu bekler. Değişiklikten sonra `make test` ile dene, `main`'e push'la ve sunucuda kurulum komutunu çalıştır.

## Nasıl oynanır

**GM:** Harita yükler ve açar (ilk açışta kaç kare olduğunu seçer). NPC'leri ve oyuncuları tur sırasına koyup "Turları başlat"a basar. Oyuncuların yürüme ve saldırı isteklerini onaylar ya da reddeder. Karakterlerin statlarını, envanterlerini ve oyun klasöründeki dosyaları GM ekranından yönetir.

**Oyuncu:** Sırası gelince haritaya tıklayıp yürür, bir karaktere tıklayıp eşyasıyla saldırır. Enerjisi bitince ya da "Turu bitir"e basınca sıra geçer. Zar tepsisi `d20`, `2d6+3`, `4d6kh3` gibi ifadeleri anlar.

## Sınırlar

- Bir sunucu tek bir oyun çalıştırır.
- Harita bir resimdir, duvarları bilmez: görüş duvarlardan geçer. Engelleri GM yönetir.
- Oyunun bütün verisi `oyun/` klasöründedir (sunucuda `/var/lib/zar-masasi/oyun`); yedek için bu klasörü kopyalamak yeter.
