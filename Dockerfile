# 1) Derleme: C kaynağını Alpine'in derleyicisiyle derler
FROM alpine:3 AS derleme
RUN apk add --no-cache build-base
WORKDIR /src
COPY Makefile ./
COPY sunucu ./sunucu
RUN make

# 2) Çalıştırma: yalnızca program ve web dosyaları
FROM alpine:3
RUN adduser -D -H -u 10001 masa && mkdir -p /veri/oyun && chown masa /veri/oyun
WORKDIR /uygulama
COPY --from=derleme /src/zar-masasi ./zar-masasi
COPY web ./web
USER masa
ENV HOST=0.0.0.0 PORT=3000 OYUN_KLASORU=/veri/oyun WEB_KLASORU=/uygulama/web
EXPOSE 3000
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s CMD wget -qO- http://127.0.0.1:3000/saglik || exit 1
CMD ["./zar-masasi"]
