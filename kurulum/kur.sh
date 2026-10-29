#!/bin/sh
# Zar Masası'nı systemd-nspawn konteyneri olarak kurar, başlatır ve günceller. Hepsi aynı komut:
#
#   curl -fsSL https://github.com/TahaGemici/roleplay/releases/latest/download/kur.sh | sudo sh
#
# GitHub'daki en son sürümü indirir, SHA-256 ile doğrular, çalışanın yerine koyar ve yeniden başlatır.
#   ... | sudo sh -s v42               belirli bir sürüm (ör. geri dönmek için)
#   sudo sh kur.sh zar-masasi.raw      elindeki imaj (özel depo ya da make imaj)
#
# Sunucuya yazdıkları:
#   /var/lib/machines/zar-masasi.raw            imaj (salt okunur)
#   /etc/systemd/nspawn/zar-masasi.nspawn       konteyner ayarları (imajın içinden)
#   /etc/systemd/system/systemd-nspawn@zar-masasi.service.d/zar-masasi.conf  servis ayarı (imajın içinden)
#   /var/lib/zar-masasi/                        .env ve oyun/ (konteynerde /veri); içeriğine dokunulmaz
set -eu

# GitHub deposu (kullanici/zar-masasi). Sürümlerdeki kur.sh'ye imaj-olustur.sh yazar.
DEPO=

AD=zar-masasi
IMAJ=/var/lib/machines/$AD.raw
VERI=/var/lib/$AD
SERVIS=systemd-nspawn@$AD.service
KOMUT="curl -fsSL https://github.com/$DEPO/releases/latest/download/kur.sh | sudo sh"

hata() {
	echo "kur.sh: $*" >&2
	exit 1
}

indir() {
	curl -fsSL --proto '=https' --retry 3 -o "$2" "$1"
}

main() {
	[ "$(id -u)" = 0 ] || hata "root olarak çalıştır (sudo)."
	command -v systemd-dissect >/dev/null 2>&1 || hata "systemd-nspawn yok. Kur: sudo apt install systemd-container"
	if [ -f "${1:-}" ]; then
		YENIDEN="sudo sh $0 $1"
		GUNCELLE="sudo sh $0 <yeni imaj>"
	else
		[ -n "$DEPO" ] || hata "hangi depodan indireceğimi bilmiyorum; imaj dosyasını ver: sudo sh $0 cikti/$AD.raw"
		command -v curl >/dev/null 2>&1 || hata "curl yok. Kur: sudo apt install curl"
		YENIDEN="$KOMUT${1:+ -s $1}"
		GUNCELLE=$KOMUT
	fi

	mkdir -p /var/lib/machines
	GECICI=$(mktemp -d "/var/lib/machines/.$AD.XXXXXX")
	trap 'rm -rf "$GECICI"' EXIT
	trap 'exit 1' INT TERM

	# 1) İmaj: dosyadan ya da GitHub sürümünden
	if [ -f "${1:-}" ]; then
		cp "$1" "$GECICI/imaj"
	else
		if [ -n "${1:-}" ]; then
			ADRES=https://github.com/$DEPO/releases/download/$1
		else
			ADRES=https://github.com/$DEPO/releases/latest/download
		fi
		echo "İndiriliyor: $ADRES/$AD.raw"
		{ indir "$ADRES/$AD.raw" "$GECICI/imaj" && indir "$ADRES/SHA256SUMS" "$GECICI/SHA256SUMS"; } ||
			hata "indirilemedi. Sürümler: https://github.com/$DEPO/releases (push'tan sonra Actions'ın bitmesini bekle)"
		BEKLENEN=$(awk -v a="$AD.raw" '$2 == a { print $1 }' "$GECICI/SHA256SUMS")
		[ "$BEKLENEN" = "$(sha256sum "$GECICI/imaj" | cut -d' ' -f1)" ] ||
			hata "imajın SHA-256 özeti tutmuyor (indirme bozuk ya da tam o anda yeni sürüm çıktı); tekrar dene."
	fi

	# 2) Ayarlar imajın içinden gelir; her sürüm kendi ayarlarını getirir
	systemd-dissect --copy-from "$GECICI/imaj" "/usr/share/$AD/kurulum" "$GECICI/kurulum" >/dev/null 2>&1 ||
		hata "bu bir Zar Masası imajı değil."
	SURUM=$(systemd-dissect --copy-from "$GECICI/imaj" /usr/lib/os-release - 2>/dev/null | sed -n 's/^IMAGE_VERSION=//p')
	install -D -m 0644 "$GECICI/kurulum/$AD.nspawn" "/etc/systemd/nspawn/$AD.nspawn"
	install -D -m 0644 "$GECICI/kurulum/servis.conf" "/etc/systemd/system/$SERVIS.d/$AD.conf"

	# 3) Veri klasörü konteynerin root'una, yani sunucudaki ayrıcalıksız UID'ye (PrivateUsers=) aittir
	KULLANICI=$(sed -n 's/^PrivateUsers=\([0-9][0-9]*\)$/\1/p' "/etc/systemd/nspawn/$AD.nspawn")
	mkdir -p "$VERI"
	if [ ! -f "$VERI/.env" ]; then
		cp "$GECICI/kurulum/env.example" "$VERI/.env"
		echo "Ayar dosyası oluşturuldu: $VERI/.env"
	fi
	chmod 0700 "$VERI"
	chmod 0600 "$VERI/.env"
	chown -R "$KULLANICI:$KULLANICI" "$VERI" # elle konan dosyalar da konteynerin olsun

	# 4) Yerine koy ve başlat
	chmod 0444 "$GECICI/imaj"
	mv -f "$GECICI/imaj" "$IMAJ"
	systemctl daemon-reload
	systemctl enable --quiet "$SERVIS" machines.target
	echo "Zar Masası $SURUM kuruldu."

	if ! grep -Eq '^[[:space:]]*GM_SIFRESI[[:space:]]*=[[:space:]]*[^[:space:]#]' "$VERI/.env"; then
		cat <<EOF

Başlatmadan önce ayarları yaz (GM_SIFRESI zorunlu, en az 8 karakter):
    sudo nano $VERI/.env
Sonra aynı komutu yeniden çalıştır; konteyneri başlatır ve açıldığını gösterir:
    $YENIDEN
EOF
		exit 0
	fi

	BASLA=$(date +%s)
	systemctl reset-failed "$SERVIS" 2>/dev/null || true
	systemctl restart "$SERVIS" || true # açılmazsa da aşağıda günlüğü göster
	CALISTI=
	for _ in 1 2 3 4 5 6 7 8 9 10; do
		sleep 1
		if journalctl -u "$SERVIS" --since "@$BASLA" -o cat --no-pager 2>/dev/null | grep -q 'Zar Masası çalışıyor'; then
			CALISTI=1
			break
		fi
	done
	journalctl -u "$SERVIS" --since "@$BASLA" -o cat --no-pager 2>/dev/null | tail -n 15

	if [ -z "$CALISTI" ]; then
		cat <<EOF >&2

Zar Masası açılmadı. Yukarıdaki hatayı düzelt (çoğunlukla $VERI/.env), sonra:
    sudo systemctl restart $SERVIS
EOF
		exit 1
	fi
	cat <<EOF

Zar Masası $SURUM konteynerde çalışıyor; sunucu açılınca kendiliğinden başlar.
  Günlük     journalctl -u $SERVIS -f
  Ayarlar    sudo nano $VERI/.env   sonra   sudo systemctl restart $SERVIS
  Güncelle   $GUNCELLE
EOF
}

main "$@"
