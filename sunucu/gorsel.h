/* Görsel dosyalarının türünü ve boyutunu ilk baytlarından okur (PNG, JPEG, GIF, WEBP).
 * JPEG'lerde telefonların yazdığı EXIF yön bilgisi hesaba katılır. */
#ifndef GORSEL_H
#define GORSEL_H

typedef struct {
    int genislik;
    int yukseklik;
    const char *tur; /* "png", "jpg", "gif", "webp" */
} GorselBilgi;

/* Tanınan bir görselse 1 döner. */
int gorsel_bilgi(const char *yol, GorselBilgi *g);

/* Dosya adına bakarak görsel mi (haritalar ve eşya simgeleri için). */
int gorsel_uzantili(const char *ad);

#endif
