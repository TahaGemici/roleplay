/* WebSocket (RFC 6455): el sıkışma ve çerçeve okuma/yazma. */
#ifndef WS_H
#define WS_H

#include <stddef.h>
#include "http.h"

#define WS_METIN 1
#define WS_IKILI 2
#define WS_KAPAT 8
#define WS_PING 9
#define WS_PONG 10

/* İstek geçerli bir WebSocket yükseltmesiyse 101 yanıtını gönderir ve 1 döner. */
int ws_el_sikis(int fd, const HttpIstek *r);

/* Bir mesajı okur (parçalı çerçeveleri birleştirir). Tür kodunu ya da hata için -1 döner.
 * Dönen veri her durumda NUL ile biter ve free ile bırakılmalıdır. */
int ws_mesaj_oku(int fd, char **veri, size_t *n, size_t azami);

/* Tek çerçeve yazar (sunucudan istemciye maskesiz). */
int ws_cerceve_yaz(int fd, int tur, const void *veri, size_t n);

#endif
