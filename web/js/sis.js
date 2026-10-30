/**
 * Görüş sisi. Üç katman:
 *   - hiç görülmemiş yer: koyu,
 *   - daha önce görülmüş yer: haritanın kendisi yarı karanlık (oradaki karakterler eski halleriyle),
 *   - şu an görülen yer: açık. Duvarların arkası görülmez; görüş alanını harita (duvarlarla) hesaplayıp verir.
 * Keşfedilen alan sunucudan gelen ince bir ızgaradır (bir karede en fazla 8×8 hücre); kendi yürüyüşün sürerken görüş
 * alanın ona hemen boyanır, sunucunun hesabı yürüyüş bitince gelir.
 * Tuval haritadan düşük çözünürlüklüdür; kenarlar zaten yumuşak. Tuval haritanın her kenarından KENAR piksel taşar:
 * tuvalin kenarı yumuşatılırken yarı saydam kalan pikseller haritanın dışına düşer, haritanın kenarı sisin altından
 * sızmaz.
 */
const AZAMI_PIKSEL = 1600;
const KENAR = 6; // tuval pikseli
const KARANLIK = 'rgb(9, 11, 10)';
const HATIRA_ACIKLIGI = 0.42; // görülmüş yerin ne kadar aydınlık kalacağı

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

export function sisOlustur(tuval) {
  const ctx = tuval.getContext('2d');
  const hatira = document.createElement('canvas'); // keşfedilen alan: hücre başına bir piksel
  const hctx = hatira.getContext('2d');
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
    if (hatira.width > 1) {
      ctx.globalAlpha = HATIRA_ACIKLIGI;
      ctx.imageSmoothingEnabled = true;
      ctx.drawImage(hatira, KENAR, KENAR, (hatira.width / hk) * kare * sx, (hatira.height / hk) * kare * sy);
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

  return {
    get etkin() {
      return etkin;
    },
    /** Sisi aç/kapat; dünyanın piksel boyutunu ve bir karenin piksel boyunu ver. */
    ayarla({ acik, genislik, yukseklik, kare: yeniKare }) {
      etkin = Boolean(acik);
      tuval.hidden = !etkin;
      if (!etkin) return;
      kare = yeniKare;
      if (genislik !== w || yukseklik !== h) {
        w = genislik;
        h = yukseklik;
        k = Math.min(1, AZAMI_PIKSEL / Math.max(w, h, 1));
        const gw = Math.max(1, Math.round(w * k));
        const gh = Math.max(1, Math.round(h * k));
        tuval.width = gw + 2 * KENAR;
        tuval.height = gh + 2 * KENAR;
        /* Tuvalin bir pikseli dünyada w / gw piksel; taşan kenar da o ölçekle */
        sx = gw / w;
        sy = gh / h;
        tuval.style.left = `${-KENAR / sx}px`;
        tuval.style.top = `${-KENAR / sy}px`;
        tuval.style.width = `${w + (2 * KENAR) / sx}px`;
        tuval.style.height = `${h + (2 * KENAR) / sy}px`;
      }
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
      const bit = cozumle(kesif.veri, g * y);
      const resim = hctx.createImageData(g, y);
      const d = resim.data;
      for (let i = 0; i < bit.length; i++) {
        if (!bit[i]) continue;
        const o = 4 * i;
        d[o] = d[o + 1] = d[o + 2] = 255;
        d[o + 3] = 255;
      }
      hctx.putImageData(resim, 0, 0);
      sonraCiz();
    },
    /** Kendi yürüyüşünde anlık keşif: görülen alan hemen hatırlanır (sunucunun hesabı yürüyüş bitince gelir) */
    yol(cokgen) {
      if (hatira.width <= 1 || cokgen.length < 6) return;
      hctx.fillStyle = '#fff';
      cokgenYolu(hctx, cokgen, (x, y) => [x * hk, y * hk]);
      hctx.fill();
    },
    /** Şu anki görüş: (x, y)'den r yarıçapla, duvarlarla kesilmiş çokgen (kare cinsinden) */
    gorus(x, y, r, cokgen) {
      gorus = { x, y, r, cokgen };
      sonraCiz();
    },
  };
}
