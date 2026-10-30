/**
 * Görüş sisi. Üç katman:
 *   - hiç görülmemiş yer: koyu,
 *   - daha önce görülmüş yer: haritanın kendisi yarı karanlık ve hafif bulanık (oradaki karakterler eski halleriyle),
 *   - şu an görülen yer: açık ve keskin. Duvarların arkası görülmez; görüş alanını harita (duvarlarla) hesaplayıp verir.
 * Keşfedilen alan sunucudan gelen ince bir ızgaradır (bir karede en fazla 8×8 hücre). Izgara olduğu gibi büyütülürse
 * kenarı merdiven gibi tırtıklı olur: tuvalin çözünürlüğüne yumuşatılarak büyütülür ve eşiklenir; kenar düzgün ve
 * kenar yumuşatmalı (antialiasing) bir eğri olur. Kendi yürüyüşün sürerken görüş alanın bu tuvale hemen (vektör olarak)
 * boyanır, sunucunun hesabı yürüyüş bitince gelir.
 * Bulanıklık ayrı bir katmandır (backdrop-filter): görüş alanının dışını kaplar, sis tuvalinin altında, pulların
 * üstünde durur.
 * Tuval haritanın her kenarından KENAR piksel taşar: tuvalin kenarı yumuşatılırken yarı saydam kalan pikseller
 * haritanın dışına düşer, haritanın kenarı sisin altından sızmaz.
 */
const AZAMI_PIKSEL = 2048;
const KENAR = 6; // tuval pikseli
const KARANLIK = 'rgb(9, 11, 10)';
const HATIRA_ACIKLIGI = 0.42; // görülmüş yerin ne kadar aydınlık kalacağı
/* Keşif ızgarası önce hücre düzeyinde bulanıklaştırılır (Gauss'a yakın, iki geçişte 1-4-6-4-1), sonra büyütülür ve
   0,4 civarında eşiklenir: merdiven gibi basamaklar düzgün bir eğriye döner, ince şeritler kaybolmaz. */
const ESIK_ALT = 0.3;
const ESIK_UST = 0.5;

/* Eşik tablosu: bulanık ızgaranın saydamlığı (0..255) → kenarı kenar yumuşatmalı (2-3 piksel) düzgün maske */
const ESIK = new Uint8ClampedArray(256);
for (let a = 0; a < 256; a++) {
  const t = Math.min(1, Math.max(0, (a / 255 - ESIK_ALT) / (ESIK_UST - ESIK_ALT)));
  ESIK[a] = Math.round(255 * t * t * (3 - 2 * t));
}

/* 1-4-6-4-1 çekirdeğiyle yatay ve dikey bulanıklaştırma (kenarlar kendini tekrarlar) */
function bulaniklastir(v, g, y) {
  const ara = new Float32Array(v.length);
  const k = [1 / 16, 4 / 16, 6 / 16, 4 / 16, 1 / 16];
  for (let j = 0; j < y; j++) {
    const s = j * g;
    for (let i = 0; i < g; i++) {
      let t = 0;
      for (let q = -2; q <= 2; q++) t += k[q + 2] * v[s + Math.min(g - 1, Math.max(0, i + q))];
      ara[s + i] = t;
    }
  }
  const son = new Float32Array(v.length);
  for (let j = 0; j < y; j++) {
    for (let i = 0; i < g; i++) {
      let t = 0;
      for (let q = -2; q <= 2; q++) t += k[q + 2] * ara[Math.min(y - 1, Math.max(0, j + q)) * g + i];
      son[j * g + i] = t;
    }
  }
  return son;
}

/* Sunucunun sıkıştırması: sırayla 0 ve 1 dizilerinin uzunlukları (7 bitlik parçalarla), base64 */
function cozumle(veri, adet) {
  const ham = atob(veri || '');
  const bit = new Uint8Array(adet);
  let yer = 0;
  let deger = 0;
  for (let i = 0; i < ham.length; ) {
    let v = 0;
    let kaydir = 0;
    for (;;) {
      if (i >= ham.length) return bit;
      const c = ham.charCodeAt(i++);
      v += (c & 0x7f) * 2 ** kaydir;
      kaydir += 7;
      if (!(c & 0x80)) break;
    }
    const son = Math.min(adet, yer + v);
    if (deger) bit.fill(1, yer, son);
    yer = son;
    deger = 1 - deger;
  }
  return bit;
}

export function sisOlustur(tuval, bulanik) {
  const ctx = tuval.getContext('2d');
  const hatira = document.createElement('canvas'); // keşfedilen alan: hücre başına bir piksel
  hatira.width = hatira.height = 1; // sunucunun keşfi gelene kadar boş
  const hctx = hatira.getContext('2d');
  const maske = document.createElement('canvas'); // keşfedilen alanın düzgün kenarlı hali: tuvalin çözünürlüğünde
  const mctx = maske.getContext('2d', { willReadFrequently: true });
  let etkin = false;
  let w = 0;
  let h = 0;
  let k = 1; // tuval pikseli / dünya pikseli
  let sx = 1; // yuvarlanmış tuval boyuyla tam oran (yatay, dikey)
  let sy = 1;
  let kare = 70; // bir karenin dünya pikseli
  let hk = 8; // keşif ızgarasında bir karedeki hücre
  let gorus = null; // { x, y, r, cokgen } kare cinsinden
  let cizimBekliyor = false;
  let maskeBos = true;

  const px = (x, y) => [x * kare * sx + KENAR, y * kare * sy + KENAR];

  function cokgenYolu(c, cokgen, donustur) {
    c.beginPath();
    for (let i = 0; i < cokgen.length; i += 2) {
      const [x, y] = donustur(cokgen[i], cokgen[i + 1]);
      if (i) c.lineTo(x, y);
      else c.moveTo(x, y);
    }
    c.closePath();
  }

  /* Izgarayı tuvalin çözünürlüğüne yumuşatarak büyüt, sonra eşikle: merdiven kenarlar düzgün eğrilere döner */
  function maskeKur() {
    const W = maske.width;
    const H = maske.height;
    mctx.clearRect(0, 0, W, H);
    maskeBos = hatira.width <= 1;
    if (maskeBos) return;
    mctx.imageSmoothingEnabled = true;
    mctx.imageSmoothingQuality = 'high';
    mctx.drawImage(hatira, 0, 0, (hatira.width / hk) * kare * sx, (hatira.height / hk) * kare * sy);
    const resim = mctx.getImageData(0, 0, W, H);
    const d = resim.data;
    for (let i = 3; i < d.length; i += 4) d[i] = ESIK[d[i]];
    mctx.putImageData(resim, 0, 0);
  }

  function ciz() {
    cizimBekliyor = false;
    if (!etkin || !tuval.width) return;
    const W = tuval.width;
    const H = tuval.height;
    ctx.globalCompositeOperation = 'source-over';
    ctx.globalAlpha = 1;
    ctx.clearRect(0, 0, W, H);
    ctx.fillStyle = KARANLIK;
    ctx.fillRect(0, 0, W, H);
    ctx.globalCompositeOperation = 'destination-out';
    if (!maskeBos) {
      ctx.globalAlpha = HATIRA_ACIKLIGI;
      ctx.drawImage(maske, KENAR, KENAR);
      ctx.globalAlpha = 1;
    }
    if (gorus && gorus.r > 0 && gorus.cokgen.length >= 6) {
      const [cx, cy] = px(gorus.x, gorus.y);
      const r = gorus.r * kare * k;
      const g = ctx.createRadialGradient(cx, cy, r * 0.8, cx, cy, r);
      g.addColorStop(0, 'rgba(0,0,0,1)');
      g.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = g;
      cokgenYolu(ctx, gorus.cokgen, px);
      ctx.fill();
    }
    ctx.globalCompositeOperation = 'source-over';
  }

  function sonraCiz() {
    if (cizimBekliyor) return;
    cizimBekliyor = true;
    requestAnimationFrame(ciz);
  }

  /* Bulanıklık katmanı görüş alanının dışını kaplar (dünya pikseli; görüş alanı çıkarılmış bir dikdörtgen) */
  function bulanikAyarla() {
    if (!bulanik) return;
    bulanik.hidden = !etkin;
    if (!etkin) return;
    let yol = `M0 0H${w}V${h}H0Z`;
    if (gorus && gorus.r > 0 && gorus.cokgen.length >= 6) {
      const c = gorus.cokgen;
      yol += ` M${(c[0] * kare).toFixed(1)} ${(c[1] * kare).toFixed(1)}`;
      for (let i = 2; i + 1 < c.length; i += 2) yol += `L${(c[i] * kare).toFixed(1)} ${(c[i + 1] * kare).toFixed(1)}`;
      yol += 'Z';
    }
    bulanik.style.clipPath = `path(evenodd, "${yol}")`;
  }

  return {
    get etkin() {
      return etkin;
    },
    /** Sisi aç/kapat; dünyanın piksel boyutunu ve bir karenin piksel boyunu ver. */
    ayarla({ acik, genislik, yukseklik, kare: yeniKare }) {
      etkin = Boolean(acik);
      tuval.hidden = !etkin;
      if (!etkin) {
        bulanikAyarla();
        return;
      }
      const kareDegisti = yeniKare !== kare;
      kare = yeniKare;
      if (genislik !== w || yukseklik !== h) {
        w = genislik;
        h = yukseklik;
        k = Math.min(1, AZAMI_PIKSEL / Math.max(w, h, 1));
        const gw = Math.max(1, Math.round(w * k));
        const gh = Math.max(1, Math.round(h * k));
        tuval.width = gw + 2 * KENAR;
        tuval.height = gh + 2 * KENAR;
        maske.width = gw;
        maske.height = gh;
        /* Tuvalin bir pikseli dünyada w / gw piksel; taşan kenar da o ölçekle */
        sx = gw / w;
        sy = gh / h;
        tuval.style.left = `${-KENAR / sx}px`;
        tuval.style.top = `${-KENAR / sy}px`;
        tuval.style.width = `${w + (2 * KENAR) / sx}px`;
        tuval.style.height = `${h + (2 * KENAR) / sy}px`;
        maskeKur();
      } else if (kareDegisti) {
        maskeKur();
      }
      if (bulanik) {
        bulanik.style.width = `${w}px`;
        bulanik.style.height = `${h}px`;
      }
      bulanikAyarla();
      sonraCiz();
    },
    /** Sunucunun gönderdiği keşif ızgarası: { k, g, y, veri } */
    hatiraAyarla(kesif) {
      const g = Math.max(1, kesif.g | 0);
      const y = Math.max(1, kesif.y | 0);
      hk = kesif.k > 0 ? kesif.k : 8;
      if (hatira.width !== g || hatira.height !== y) {
        hatira.width = g;
        hatira.height = y;
      }
      /* Hücre düzeyinde bulanıklaştırılmış keşif: maskeKur büyütüp eşikleyince kenar düzgün bir eğri olur */
      const bulanik = bulaniklastir(Float32Array.from(cozumle(kesif.veri, g * y)), g, y);
      const resim = hctx.createImageData(g, y);
      const d = resim.data;
      for (let i = 0; i < bulanik.length; i++) {
        if (bulanik[i] <= 0) continue;
        const o = 4 * i;
        d[o] = d[o + 1] = d[o + 2] = 255;
        d[o + 3] = Math.round(bulanik[i] * 255);
      }
      hctx.putImageData(resim, 0, 0);
      maskeKur();
      sonraCiz();
    },
    /** Kendi yürüyüşünde anlık keşif: görülen alan hemen hatırlanır (sunucunun hesabı yürüyüş bitince gelir) */
    yol(cokgen) {
      if (hatira.width <= 1 || cokgen.length < 6) return;
      mctx.fillStyle = '#fff';
      cokgenYolu(mctx, cokgen, (x, y) => [x * kare * sx, y * kare * sy]);
      mctx.fill();
      maskeBos = false;
    },
    /** Şu anki görüş: (x, y)'den r yarıçapla, duvarlarla kesilmiş çokgen (kare cinsinden) */
    gorus(x, y, r, cokgen) {
      gorus = { x, y, r, cokgen };
      bulanikAyarla();
      sonraCiz();
    },
  };
}
