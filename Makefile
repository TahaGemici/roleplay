# Roleplay — derleme
#   make          sunucuyu derler (./roleplay)
#   make test     birim testlerini çalıştırır
#   make imaj     sürüm dosyalarını üretir: cikti/ (imaj, kur.sh; musl-tools ve squashfs-tools gerekir)
#   make temizle  derleme çıktılarını siler

CC      ?= cc
CFLAGS  ?= -O2 -g
override CFLAGS  += -std=c11 -D_GNU_SOURCE -Wall -Wextra -Wno-unused-parameter -Wno-format-truncation -pthread
override LDFLAGS += -pthread
override LDLIBS  += -lm

# Kendi kurallarını başka bir dosyada tutmak istersen:  make KURALLAR=benim_kurallarim.c
KURALLAR ?= sunucu/kurallar.c
HEDEF    ?= roleplay
# Sürüm etiketi (GitHub Actions v42 gibi verir). Tarayıcılar sürüm değişince belleklerini siler.
SURUM    ?= $(shell git describe --tags --match 'v[0-9]*' --always --dirty 2>/dev/null || echo yerel)

KAYNAK = sunucu/main.c sunucu/oyun.c $(KURALLAR) sunucu/http.c sunucu/ws.c sunucu/json.c sunucu/zar.c \
         sunucu/gorsel.c sunucu/dosya.c sunucu/geometri.c sunucu/duvar.c sunucu/kesif.c sunucu/ortak.c
BASLIK = $(wildcard sunucu/*.h)

$(HEDEF): $(KAYNAK) $(BASLIK)
	$(CC) $(CFLAGS) -DSURUM='"$(SURUM)"' -Isunucu -o $@ $(KAYNAK) $(LDFLAGS) $(LDLIBS)

TEST_KAYNAK = test/birim.c sunucu/json.c sunucu/zar.c sunucu/gorsel.c sunucu/dosya.c sunucu/geometri.c \
              sunucu/duvar.c sunucu/kesif.c sunucu/ortak.c

test/birim: $(TEST_KAYNAK) $(BASLIK)
	$(CC) $(CFLAGS) -Isunucu -o $@ $(TEST_KAYNAK) $(LDFLAGS) $(LDLIBS)

test: test/birim
	./test/birim

imaj:
	kurulum/imaj-olustur.sh

temizle:
	rm -f roleplay roleplay-test roleplay-tsan test/birim
	rm -rf cikti

.PHONY: test imaj temizle
