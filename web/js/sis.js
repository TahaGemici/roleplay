/**
 * Görüş sisi. Üç katman:
 *   - hiç görülmemiş yer: koyu,
 *   - daha önce görülmüş yer: haritanın kendisi yarı karanlık (oradaki karakterler eski halleriyle),
 *   - şu an görülen yer (görüş dairesi): açık.
 * Keşfedilen alan, sunucunun gönderdiği kapsüllerin birleşimidir: yürürken görüş dairesinin taradığı şerit.
 * Tuval haritadan düşük çözünürlüklüdür; kenarlar zaten yumuşak.
 */
const AZAMI_PIKSEL = 1600;
const KARANLIK = 'rgb(9, 11, 10)';
const HATIRA_ACIKLIGI = 0.42; // görülmüş yerin ne kadar aydınlık kalacağı

export function sisOlustur(tuval) {
  const ctx = tuval.getContext('2d');
  const maske = document.createElement('canvas');
  const mctx = maske.getContext('2d');
  let etkin = false;
  let w = 0;
  let h = 0;
  let k = 1; // tuval pikseli / dünya pikseli
  let kare = 70; // bir karenin dünya pikseli
  let kapsuller = [];
  let daire = null; // { x, y, r } kare cinsinden
  let cizimBekliyor = false;

  const px = (x, y) => [x * kare * k, y * kare * k];

  function kapsulCiz([x1, y1, x2, y2, r]) {
    const [ax, ay] = px(x1, y1);
    const [bx, by] = px(x2, y2);
    const rr = r * kare * k;
    if (rr <= 0) return;
    mctx.beginPath();
    if (Math.hypot(bx - ax, by - ay) < 0.5) {
      mctx.arc(ax, ay, rr, 0, Math.PI * 2);
      mctx.fill();
    } else {
      mctx.lineWidth = rr * 2;
      mctx.moveTo(ax, ay);
      mctx.lineTo(bx, by);
      mctx.stroke();
    }
  }

  function maskeyiYenidenCiz() {
    mctx.clearRect(0, 0, maske.width, maske.height);
    mctx.fillStyle = mctx.strokeStyle = '#fff';
    mctx.lineCap = 'round';
    for (const p of kapsuller) kapsulCiz(p);
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
    ctx.globalAlpha = HATIRA_ACIKLIGI;
    ctx.drawImage(maske, 0, 0);
    ctx.globalAlpha = 1;
    if (daire && daire.r > 0) {
      const [cx, cy] = px(daire.x, daire.y);
      const r = daire.r * kare * k;
      const g = ctx.createRadialGradient(cx, cy, r * 0.8, cx, cy, r);
      g.addColorStop(0, 'rgba(0,0,0,1)');
      g.addColorStop(1, 'rgba(0,0,0,0)');
      ctx.fillStyle = g;
      ctx.beginPath();
      ctx.arc(cx, cy, r, 0, Math.PI * 2);
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
      const boyutDegisti = genislik !== w || yukseklik !== h;
      const kareDegisti = yeniKare !== kare;
      kare = yeniKare;
      if (boyutDegisti) {
        w = genislik;
        h = yukseklik;
        k = Math.min(1, AZAMI_PIKSEL / Math.max(w, h, 1));
        tuval.width = maske.width = Math.max(1, Math.round(w * k));
        tuval.height = maske.height = Math.max(1, Math.round(h * k));
        tuval.style.width = `${w}px`;
        tuval.style.height = `${h}px`;
      }
      if (boyutDegisti || kareDegisti) maskeyiYenidenCiz();
      sonraCiz();
    },
    /** Keşif listesinin tamamı (sunucu sıfırla dediğinde) */
    sifirla(liste) {
      kapsuller = [...liste];
      maskeyiYenidenCiz();
      sonraCiz();
    },
    /** Yeni keşfedilen parçalar */
    ekle(liste) {
      if (!liste.length) return;
      mctx.fillStyle = mctx.strokeStyle = '#fff';
      mctx.lineCap = 'round';
      for (const p of liste) {
        kapsuller.push(p);
        kapsulCiz(p);
      }
      sonraCiz();
    },
    /** Yürürken anlık keşif (sunucunun listesi yürüyüş bitince gelir; aynı alan) */
    yol(x1, y1, x2, y2, r) {
      mctx.fillStyle = mctx.strokeStyle = '#fff';
      mctx.lineCap = 'round';
      kapsulCiz([x1, y1, x2, y2, r]);
    },
    /** Şu anki görüş dairesi (kare cinsinden) */
    gorus(x, y, r) {
      daire = { x, y, r };
      sonraCiz();
    },
  };
}
