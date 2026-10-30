#!/bin/sh
# Bir sürümün dosyalarını üretir. GitHub Actions her main push'unda bunları Releases'a koyar.
#   cikti/roleplay.raw     systemd-nspawn imajı
#   cikti/kur.sh           sunucuda kurar ve günceller (DEPO verildiyse o deponun sürümlerinden indirir)
#   cikti/SHA256SUMS
#
# İmaj salt okunur tek bir squashfs dosya sistemidir: statik derlenmiş sunucu, web arayüzü ve konteyner
# ayarları (roleplay.nspawn, servis.conf, .env.example). Kabuk ya da kütüphane yoktur; oyunun verisi
# imajda değil, sunucudaki /var/lib/roleplay'dedir.
#
# Gerekenler: sudo apt install make musl-tools squashfs-tools
# Kullanım:   make imaj   (root gerekmez)
set -eu

cd "$(dirname "$0")/.."
CIKTI=${CIKTI:-cikti}
CC=${CC:-musl-gcc}
DEPO=${DEPO:-}
SURUM=${SURUM:-$(git describe --tags --match 'v[0-9]*' --always --dirty 2>/dev/null || echo yerel)}
# Aynı kaynaktan bayt bayt aynı imaj çıksın: dosya tarihleri son commit'in tarihi olur.
SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-$(git log -1 --format=%ct 2>/dev/null || date +%s)}
export SOURCE_DATE_EPOCH

for arac in make "$CC" mksquashfs sha256sum; do
	command -v "$arac" >/dev/null 2>&1 || {
		echo "Eksik araç: $arac   (sudo apt install make musl-tools squashfs-tools)" >&2
		exit 1
	}
done
if [ -n "$DEPO" ] && ! echo "$DEPO" | grep -Eq '^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$'; then
	echo "DEPO kullanici/depo biçiminde olmalı: $DEPO" >&2
	exit 1
fi

KOK=$(mktemp -d)
trap 'rm -rf "$KOK"' EXIT
trap 'exit 1' INT TERM
mkdir -p "$CIKTI" "$KOK/usr/bin" "$KOK/usr/lib" "$KOK/usr/share/roleplay/kurulum" "$KOK/etc" \
	"$KOK/proc" "$KOK/sys" "$KOK/dev" "$KOK/run" "$KOK/tmp" "$KOK/veri"

# 1) Sunucu: musl ile statik (imajda kütüphane yok), adresleri her açılışta rastgele (static-pie), sembolsüz.
make -s HEDEF="$KOK/usr/bin/roleplay" CC="$CC" SURUM="$SURUM" \
	CFLAGS="-O2 -fPIE -fstack-protector-strong" LDFLAGS="-static-pie -s"

# 2) Web arayüzü ve konteyner ayarları (kur.sh ayarları imajdan alıp sunucuya yazar)
cp -R web "$KOK/usr/share/roleplay/web"
cp kurulum/roleplay.nspawn kurulum/servis.conf "$KOK/usr/share/roleplay/kurulum/"
cp .env.example "$KOK/usr/share/roleplay/kurulum/env.example"

# 3) nspawn'ın beklediği iskelet. İmaj salt okunur olduğu için bağlama noktaları (/proc, /veri...) baştan var.
cat >"$KOK/usr/lib/os-release" <<EOF
NAME="Roleplay"
PRETTY_NAME="Roleplay $SURUM"
ID=roleplay
IMAGE_ID=roleplay
IMAGE_VERSION=$SURUM
EOF
ln -s ../usr/lib/os-release "$KOK/etc/os-release"
: >"$KOK/etc/machine-id"
printf '127.0.0.1\tlocalhost\n::1\tlocalhost\n' >"$KOK/etc/hosts"
chmod -R u=rwX,go=rX "$KOK"
chmod 1777 "$KOK/tmp"

# 4) Paketle: hepsi root'un, genişletilmiş öznitelik yok; yeni dosyaya yaz, sonra yerine koy.
mksquashfs "$KOK" "$CIKTI/roleplay.raw.yeni" -noappend -all-root -no-xattrs -no-progress >/dev/null
mv -f "$CIKTI/roleplay.raw.yeni" "$CIKTI/roleplay.raw"

# 5) kur.sh hangi depodan indireceğini bilsin
sed "s|^DEPO=.*|DEPO=$DEPO|" kurulum/kur.sh >"$CIKTI/kur.sh"
(cd "$CIKTI" && sha256sum roleplay.raw kur.sh >SHA256SUMS)

echo "Hazır: $CIKTI/  (Roleplay $SURUM, imaj $(du -k "$CIKTI/roleplay.raw" | cut -f1) KB)"
