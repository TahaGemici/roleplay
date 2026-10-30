/* Duvarlar: görüş hattı, görüş alanı ve duvarların etrafından dolaşan en kısa yol. Birimler kare cinsindendir.
 *
 * Duvar ince bir doğru parçasıdır. Görüşü keser; karakterin merkezi onun üstünden geçemez. Tam duvarın üstünde
 * duran bir nokta iki yanı da görür ve iki yana da çıkabilir; bu yüzden yollar duvarın üstünde ya da dibinde bitmez
 * (DUVAR_UC_PAYI). Aynı algoritma web/js/duvar.js'te de vardır: iki taraf aynı duvarlardan aynı yolu bulsun diye
 * sayılar ve sıralama birebir aynıdır. */
#ifndef DUVAR_H
#define DUVAR_H

/* Yol duvarlara en az bu kadar yaklaşır (başlangıç ve hedef yakındaysa onlar kadar). Bundan dar aralıklar kapalıdır. */
#define DUVAR_ACIKLIK 0.1
/* Yol, duvarların köşelerini bu kadar açıktan döner; sığmazsa ikincisi denenir. */
#define DUVAR_KOSE_PAYI 0.35
#define DUVAR_KOSE_PAYI_DAR 0.15
/* Yolun sonu bir duvara bunun yarısından yakınsa, geldiği yana doğru duvardan bu kadar uzağa alınır. */
#define DUVAR_UC_PAYI 0.01

typedef struct Duvarlar Duvarlar;

typedef struct {
    double *xy; /* x0,y0,x1,y1,... : ilk nokta başlangıç, son nokta hedef */
    int n;      /* nokta sayısı */
    double uzunluk;
} Yol;

/* parca: n duvar (x1,y1,x2,y2 sırayla). g, y: haritanın boyutu; harita sınırı da yürürken duvar sayılır. */
Duvarlar *duvarlar_kur(const double *parca, int n, double g, double y);
void duvarlar_sil(Duvarlar *d);

/* A ile B arasında görüş var mı (aradaki hiçbir duvar kesmiyor mu). Uçlardan biri duvarın üstündeyse o duvar
   kesmiş sayılmaz. Bir duvarın ucundan sıyırarak ya da iki duvarın birleştiği köşeden geçen çizgi kesilmiş sayılır. */
int duvar_gorus_acik(Duvarlar *d, double ax, double ay, double bx, double by);

/* (ox,oy)'dan r yarıçapla görülen alan: köşeleri açıya göre sıralı, (ox,oy)'a göre yıldız biçimli bir çokgen. *xy
   ayrılır (free ile bırakılır); köşe sayısı döner. gorulen verilirse (n bayt) görülen duvarların baytı 1 olur. */
int duvar_gorus_cokgeni(Duvarlar *d, double ox, double oy, double r, double **xy, unsigned char *gorulen);

/* V'den r yarıçapla bakınca A'dan B'ye parçanın görünen kısımları: t (0..1) aralıkları sırayla t[0],t[1],t[2],...'ye
   yazılır (en fazla azami tane); aralık sayısı döner. */
int duvar_gorunen(Duvarlar *d, double vx, double vy, double r, double ax, double ay, double bx, double by, double *t,
                  int azami);

/* S'den G'ye duvarların etrafından dolaşan en kısa yol. Bulursa 1 döner ve yol'u doldurur (yol_bosalt ile bırakılır).
   Yol yoksa 0 döner. */
int duvar_yol_bul(Duvarlar *d, double sx, double sy, double gx, double gy, Yol *yol);

/* A'dan B'ye düz giderken ilk çarpılan duvardan DUVAR_ACIKLIK önce durulan nokta (duvar yoksa B; orası bir duvarın
   dibindeyse yol sonu gibi duvardan biraz uzağa alınır). */
void duvar_carpma(Duvarlar *d, double ax, double ay, double bx, double by, double *cx, double *cy);

/* Bir yere konan (yürümeden) nokta duvarın üstünde ya da dibindeyse duvardan DUVAR_UC_PAYI uzağa alınır: kendi
   yanına, tam üstündeyse duvarın soluna. */
void duvar_uzaklastir(Duvarlar *d, double *x, double *y);

/* (x,y)'nin en yakın duvara uzaklığı; 0.4 kareden uzaktakiler sayılmaz (yakında duvar yoksa 0.4 döner). */
double duvar_yakinlik(Duvarlar *d, double x, double y);

/* Bir yolun uzunluğu, s uzunluk ilerisindeki noktası ve bir noktaya en yakın yeri (uzunluk cinsinden). */
void yol_bosalt(Yol *y);
void yol_kopyala(Yol *hedef, const Yol *kaynak);
void yol_duz(Yol *y, double ax, double ay, double bx, double by); /* iki noktalı yol */
void yol_noktada(const Yol *y, double s, double *x, double *yy);
double yol_en_yakin(const Yol *y, double px, double py, double *s);

#endif
