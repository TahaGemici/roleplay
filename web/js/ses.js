/*
 * Ses efektleri. Hepsi WebAudio ile anında üretilir (ses dosyası yok); her görsel efektin bir sesi vardır:
 * yürüme, saldırı (savurma, fırlatma, isabet, ıska), büyü (yapma, küre, çarpma, iyileşme), ölüm ve dirilme, işaret,
 * zar ve kritik, sıra, GM'e istek, portal, para ve eşya alma, eşya kırma, tepkiler (alkış, kahkaha, ağlama, bağırma).
 * Tarayıcı ses çalmaya ilk tıklamadan sonra izin verir.
 * Açık/kapalı tercihi bu tarayıcıda saklanır.
 */

const ANAHTAR = 'roleplay.ses';
let ctx = null;
let ana = null;
let gurultuTamponu = null;
let acik = (() => {
  try {
    return localStorage.getItem(ANAHTAR) !== '0';
  } catch {
    return true;
  }
})();

function baglam() {
  if (!acik) return null;
  if (!ctx) {
    const Ctx = window.AudioContext || window.webkitAudioContext;
    if (!Ctx) return null;
    ctx = new Ctx();
    ana = ctx.createGain();
    ana.gain.value = 0.45;
    const sikistir = ctx.createDynamicsCompressor();
    ana.connect(sikistir);
    sikistir.connect(ctx.destination);
  }
  if (ctx.state === 'suspended') ctx.resume().catch(() => {});
  return ctx.state === 'running' ? ctx : null;
}

/* Tarayıcı sesi ancak bir kullanıcı hareketinden sonra açar */
for (const olay of ['pointerdown', 'keydown']) document.addEventListener(olay, () => baglam(), { capture: true, passive: true });

export const sesAcik = () => acik;

export function sesAyarla(yeni) {
  acik = Boolean(yeni);
  try {
    localStorage.setItem(ANAHTAR, acik ? '1' : '0');
  } catch {
    /* önemsiz */
  }
  if (acik) baglam();
  else if (ctx) ctx.suspend().catch(() => {});
}

function gurultu(c) {
  if (!gurultuTamponu) {
    gurultuTamponu = c.createBuffer(1, c.sampleRate, c.sampleRate);
    const v = gurultuTamponu.getChannelData(0);
    for (let i = 0; i < v.length; i++) v[i] = Math.random() * 2 - 1;
  }
  const k = c.createBufferSource();
  k.buffer = gurultuTamponu;
  k.loop = true;
  return k;
}

/* Kazanç zarfı: t'de başlar, atak sürede tepeye çıkar, düşüş sürede söner */
function zarf(c, t, tepe, atak, dusus) {
  const g = c.createGain();
  g.gain.setValueAtTime(0.0001, t);
  g.gain.exponentialRampToValueAtTime(Math.max(tepe, 0.0002), t + atak);
  g.gain.exponentialRampToValueAtTime(0.0001, t + atak + dusus);
  return g;
}

function ton(c, { tur = 'sine', f1, f2 = f1, t = c.currentTime, sure = 0.2, tepe = 0.3, atak = 0.005, hedef = ana }) {
  const o = c.createOscillator();
  o.type = tur;
  o.frequency.setValueAtTime(f1, t);
  if (f2 !== f1) o.frequency.exponentialRampToValueAtTime(f2, t + sure);
  const g = zarf(c, t, tepe, atak, sure);
  o.connect(g);
  g.connect(hedef);
  o.start(t);
  o.stop(t + atak + sure + 0.05);
}

function hisirti(c, { t = c.currentTime, sure = 0.2, tepe = 0.3, atak = 0.005, filtre = 'bandpass', f1 = 1000, f2 = f1, q = 1 }) {
  const k = gurultu(c);
  const f = c.createBiquadFilter();
  f.type = filtre;
  f.Q.value = q;
  f.frequency.setValueAtTime(f1, t);
  if (f2 !== f1) f.frequency.exponentialRampToValueAtTime(f2, t + atak + sure);
  const g = zarf(c, t, tepe, atak, sure);
  k.connect(f);
  f.connect(g);
  g.connect(ana);
  k.start(t);
  k.stop(t + atak + sure + 0.05);
}

/* İnsan sesine benzeyen bir hece: testere dalgası, iki formant süzgecinden geçer (a sesi: ~800 ve ~1200 Hz) */
function hece(c, { t, f1, f2 = f1, sure = 0.15, tepe = 0.2, formant = [800, 1200], titreme = 0 }) {
  const o = c.createOscillator();
  o.type = 'sawtooth';
  o.frequency.setValueAtTime(f1, t);
  if (f2 !== f1) o.frequency.exponentialRampToValueAtTime(f2, t + sure);
  if (titreme) {
    const lfo = c.createOscillator();
    const lg = c.createGain();
    lfo.frequency.value = 7;
    lg.gain.value = titreme;
    lfo.connect(lg);
    lg.connect(o.frequency);
    lfo.start(t);
    lfo.stop(t + sure + 0.05);
  }
  const g = zarf(c, t, tepe, Math.min(0.03, sure / 4), sure);
  for (const f of formant) {
    const b = c.createBiquadFilter();
    b.type = 'bandpass';
    b.frequency.value = f;
    b.Q.value = 6;
    o.connect(b);
    b.connect(g);
  }
  g.connect(ana);
  o.start(t);
  o.stop(t + sure + 0.08);
}

/* Bir sesi bir süre sonra çalmak için (gecikme saniye) */
const calis = (fn) => (gecikme = 0, ...arg) => {
  const c = baglam();
  if (c) fn(c, c.currentTime + Math.max(0, gecikme), ...arg);
};

export const ses = {
  /** Yürüme: sure (ms) boyunca adımlar; guc 0..1 (başkasının adımları daha sessiz) */
  yurume: calis((c, t, sure = 600, guc = 1) => {
    const n = Math.max(1, Math.min(10, Math.round(sure / 300)));
    for (let i = 0; i < n; i++) {
      const s = t + i * (sure / 1000 / n) + Math.random() * 0.03;
      hisirti(c, { t: s, sure: 0.06, tepe: 0.16 * guc, filtre: 'lowpass', f1: 700 + Math.random() * 300, q: 0.7 });
      ton(c, { f1: 95 + Math.random() * 20, f2: 60, t: s, sure: 0.07, tepe: 0.12 * guc });
    }
  }),
  /** Yakın dövüşte eşyanın savrulması */
  savurma: calis((c, t) => hisirti(c, { t, sure: 0.2, tepe: 0.32, atak: 0.04, f1: 450, f2: 2200, q: 2.5 })),
  /** Uzaktan atılan eşya (sure ms) */
  firlatma: calis((c, t, sure = 400) => hisirti(c, { t, sure: sure / 1000, tepe: 0.22, atak: 0.05, f1: 1800, f2: 500, q: 3 })),
  /** İsabet: tok bir darbe ve çatırtı */
  isabet: calis((c, t) => {
    ton(c, { f1: 150, f2: 45, t, sure: 0.18, tepe: 0.55 });
    hisirti(c, { t, sure: 0.05, tepe: 0.35, filtre: 'highpass', f1: 2400 });
  }),
  /** Iska: boşluğa savrulan hafif rüzgâr */
  iska: calis((c, t) => hisirti(c, { t, sure: 0.22, tepe: 0.14, atak: 0.03, filtre: 'highpass', f1: 1200, f2: 3000 })),
  /** Büyü yapılırken parıltı */
  buyuYap: calis((c, t) => {
    [660, 990, 1320].forEach((f, i) => ton(c, { f1: f, f2: f * 1.02, t: t + i * 0.04, sure: 0.45, tepe: 0.09, atak: 0.06 }));
  }),
  /** Hedefe uçan büyü küresi (sure ms) */
  kure: calis((c, t, sure = 400) => ton(c, { tur: 'triangle', f1: 380, f2: 900, t, sure: sure / 1000, tepe: 0.12, atak: 0.04 })),
  /** Büyünün çarpması: çan gibi */
  buyuCarp: calis((c, t) => {
    ton(c, { tur: 'triangle', f1: 880, t, sure: 0.35, tepe: 0.28 });
    ton(c, { f1: 1320, t, sure: 0.3, tepe: 0.16 });
    hisirti(c, { t, sure: 0.12, tepe: 0.12, filtre: 'highpass', f1: 6000 });
  }),
  /** İyileşme: yükselen küçük bir arpej */
  iyiles: calis((c, t) => [1046, 1318, 1568].forEach((f, i) => ton(c, { f1: f, t: t + i * 0.07, sure: 0.4, tepe: 0.14 }))),
  /** Ölüm: inen alçak bir ses ve yere düşüş */
  olum: calis((c, t) => {
    ton(c, { tur: 'sawtooth', f1: 260, f2: 55, t, sure: 0.75, tepe: 0.12, atak: 0.02 });
    ton(c, { f1: 110, f2: 40, t: t + 0.55, sure: 0.25, tepe: 0.45 });
    hisirti(c, { t: t + 0.55, sure: 0.15, tepe: 0.2, filtre: 'lowpass', f1: 500 });
  }),
  /** Haritaya işaret */
  isaret: calis((c, t) => {
    ton(c, { f1: 1250, t, sure: 0.3, tepe: 0.2 });
    ton(c, { f1: 1870, t: t + 0.08, sure: 0.25, tepe: 0.1 });
  }),
  /** Zar tıkırtısı */
  zar: calis((c, t) => {
    let s = t;
    for (let i = 0; i < 7; i++) {
      s += 0.03 + Math.random() * 0.07;
      hisirti(c, { t: s, sure: 0.03, tepe: 0.22, f1: 2200 + Math.random() * 1800, q: 6 });
    }
  }),
  /** Zarda doğal 20: kartın parlamasıyla bir pırıltı */
  kritik: calis((c, t) => [1568, 2093, 2637].forEach((f, i) => ton(c, { tur: 'triangle', f1: f, t: t + i * 0.06, sure: 0.35, tepe: 0.12 }))),
  /** GM'e yeni istek geldi */
  istek: calis((c, t) => {
    ton(c, { f1: 988, t, sure: 0.18, tepe: 0.18 });
    ton(c, { f1: 1319, t: t + 0.1, sure: 0.3, tepe: 0.16 });
  }),
  /** Sıra sende */
  sira: calis((c, t) => {
    ton(c, { tur: 'triangle', f1: 523, t, sure: 0.25, tepe: 0.22, atak: 0.02 });
    ton(c, { tur: 'triangle', f1: 784, t: t + 0.16, sure: 0.4, tepe: 0.22, atak: 0.02 });
  }),
  /** Portaldan geçiş */
  portal: calis((c, t) => {
    hisirti(c, { t, sure: 0.9, tepe: 0.22, atak: 0.25, f1: 220, f2: 2400, q: 4 });
    ton(c, { f1: 220, f2: 880, t, sure: 0.8, tepe: 0.1, atak: 0.2 });
  }),
  /** Para ya da eşya el değiştirdi (alma, yağma, takas) */
  para: calis((c, t) => {
    ton(c, { tur: 'triangle', f1: 2100, t, sure: 0.12, tepe: 0.16 });
    ton(c, { tur: 'triangle', f1: 2700, t: t + 0.07, sure: 0.16, tepe: 0.13 });
  }),
  /** Eşya kırıldı / yok edildi */
  kir: calis((c, t) => {
    hisirti(c, { t, sure: 0.18, tepe: 0.35, filtre: 'lowpass', f1: 1600, f2: 400 });
    ton(c, { f1: 140, f2: 60, t, sure: 0.15, tepe: 0.3 });
  }),
  /** Tepki: alkış (art arda el çırpma); guc 0..1 */
  alkis: calis((c, t, guc = 1) => {
    let s = t;
    for (let i = 0; i < 7; i++) {
      hisirti(c, { t: s, sure: 0.05, tepe: 0.42 * guc, atak: 0.002, f1: 1100 + Math.random() * 900, q: 1.4 });
      s += 0.11 + Math.random() * 0.06;
    }
  }),
  /** Tepki: kahkaha ("ha ha ha", perdesi düşerek) */
  kahkaha: calis((c, t, guc = 1) => {
    for (let i = 0; i < 5; i++) {
      const f = 290 - i * 18 + Math.random() * 12;
      hece(c, { t: t + i * 0.16, f1: f * 1.12, f2: f, sure: 0.11, tepe: 0.3 * guc, formant: [750, 1150] });
      hisirti(c, { t: t + i * 0.16, sure: 0.05, tepe: 0.06 * guc, filtre: 'highpass', f1: 2500 });
    }
  }),
  /** Tepki: ağlama (titreyen, inen hıçkırıklar) */
  aglama: calis((c, t, guc = 1) => {
    [0, 0.42, 0.95].forEach((d, i) => {
      hece(c, { t: t + d, f1: 520 - i * 25, f2: 330, sure: 0.38, tepe: 0.2 * guc, formant: [450, 950], titreme: 18 });
    });
  }),
  /** Tepki: bağırma (yüksek, kısa bir "hey!"); uzaktan duyulunca daha kısık (guc) */
  bagir: calis((c, t, guc = 1) => {
    hece(c, { t, f1: 230, f2: 330, sure: 0.16, tepe: 0.45 * guc, formant: [850, 1300] });
    hece(c, { t: t + 0.15, f1: 330, f2: 250, sure: 0.4, tepe: 0.5 * guc, formant: [700, 1150], titreme: 6 });
    hisirti(c, { t, sure: 0.5, tepe: 0.08 * guc, f1: 1800, q: 0.8 });
  }),
};
