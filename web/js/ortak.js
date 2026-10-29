// Küçük yardımcılar. Sunucudan gelen her metin textContent ile yazılır, asla innerHTML ile değil.

export const $ = (secici, kok = document) => kok.querySelector(secici);

export function h(etiket, ozellik = {}, ...cocuklar) {
  const el = document.createElement(etiket);
  for (const [k, v] of Object.entries(ozellik || {})) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'class') el.className = v;
    else if (k === 'text') el.textContent = v;
    else if (k === 'dataset') Object.assign(el.dataset, v);
    else if (k === 'vars') for (const [a, b] of Object.entries(v)) el.style.setProperty(a, b);
    else if (k.startsWith('on')) el.addEventListener(k.slice(2), v);
    else el.setAttribute(k, v === true ? '' : v);
  }
  for (const c of cocuklar.flat()) {
    if (c === null || c === undefined || c === false) continue;
    el.append(c instanceof Node ? c : document.createTextNode(String(c)));
  }
  return el;
}

const SVG_NS = 'http://www.w3.org/2000/svg';
export function svg(etiket, ozellik = {}, ...cocuklar) {
  const el = document.createElementNS(SVG_NS, etiket);
  for (const [k, v] of Object.entries(ozellik)) {
    if (v === null || v === undefined || v === false) continue;
    if (k === 'text') el.textContent = v;
    else el.setAttribute(k, v);
  }
  for (const c of cocuklar.flat()) if (c) el.append(c);
  return el;
}

export const sinirla = (v, a, b) => Math.min(b, Math.max(a, v));

/* Türkçe sayı: 4 → "4", 4.25 → "4,3"; yuvarlanınca sıfır olan küçük eksiler "-0" diye görünmez */
export const sayiYaz = (v) => {
  const n = Number(v);
  const r = Number.isFinite(n) ? Math.round(n * 10) / 10 : 0;
  return (r === 0 ? 0 : r).toLocaleString('tr-TR', { maximumFractionDigits: 1 });
};

/* Bildirimlerde uzun dosya adı: baştan ve uzantılı sondan birer parça */
export const adKisalt = (ad, n = 40) => {
  const k = Array.from(String(ad ?? ''));
  return k.length <= n ? k.join('') : `${k.slice(0, n - 12).join('')}…${k.slice(-10).join('')}`;
};

export function basHarfler(ad) {
  const kelimeler = String(ad || '').trim().split(/\s+/).filter(Boolean);
  if (!kelimeler.length) return '?';
  const ilk = (k) => Array.from(k)[0] || '';
  const s = kelimeler.length > 1 ? ilk(kelimeler[0]) + ilk(kelimeler[1]) : Array.from(kelimeler[0]).slice(0, 2).join('');
  return s.toLocaleUpperCase('tr');
}

// Arka plan rengine göre okunaklı yazı rengi
export function yaziRengi(hex) {
  const n = parseInt(String(hex).slice(1), 16);
  if (Number.isNaN(n)) return '#f6efe0';
  const lin = (c) => {
    const s = c / 255;
    return s <= 0.03928 ? s / 12.92 : ((s + 0.055) / 1.055) ** 2.4;
  };
  const L = 0.2126 * lin((n >> 16) & 255) + 0.7152 * lin((n >> 8) & 255) + 0.0722 * lin(n & 255);
  return L > 0.18 ? '#1b130e' : '#f6efe0';
}

export const saat = (ms) => new Date(ms).toLocaleTimeString('tr-TR', { hour: '2-digit', minute: '2-digit' });
export const tarihSaat = (sn) =>
  new Date(sn * 1000).toLocaleString('tr-TR', { day: '2-digit', month: '2-digit', year: '2-digit', hour: '2-digit', minute: '2-digit' });
export const azHareket = () => window.matchMedia('(prefers-reduced-motion: reduce)').matches;

export function boyutYaz(b) {
  if (b < 1024) return `${b} B`;
  if (b < 1024 * 1024) return `${(b / 1024).toLocaleString('tr-TR', { maximumFractionDigits: 0 })} KB`;
  return `${(b / 1024 / 1024).toLocaleString('tr-TR', { maximumFractionDigits: 1 })} MB`;
}

// "esyalar/silahlar/Uzun_Kılıç.png" → "Uzun Kılıç" (alt çizgi ve tire boşluk olur)
export function dosyaAdi(yol, uzantisiz = true) {
  const ad = String(yol).split('/').pop();
  if (!uzantisiz) return ad;
  const nokta = ad.lastIndexOf('.');
  const govde = nokta > 0 ? ad.slice(0, nokta) : ad;
  return govde.replace(/[_-]+/g, ' ').trim() || govde;
}

export const uzanti = (yol) => {
  const ad = dosyaAdi(yol, false);
  const n = ad.lastIndexOf('.');
  return n > 0 ? ad.slice(n + 1).toLowerCase() : '';
};

export function yolKodla(yol) {
  return String(yol).split('/').map(encodeURIComponent).join('/');
}

export function throttle(fn, ms) {
  let son = 0;
  let zamanlayici = null;
  let bekleyen = null;
  const calistir = () => {
    son = Date.now();
    zamanlayici = null;
    const a = bekleyen;
    bekleyen = null;
    fn(...a);
  };
  return (...a) => {
    bekleyen = a;
    const bekle = ms - (Date.now() - son);
    if (bekle <= 0) {
      clearTimeout(zamanlayici);
      calistir();
    } else if (!zamanlayici) {
      zamanlayici = setTimeout(calistir, bekle);
    }
  };
}

export function debounce(fn, ms) {
  let z = null;
  return (...a) => {
    clearTimeout(z);
    z = setTimeout(() => fn(...a), ms);
  };
}

export function bildir(metin, tur = 'hata') {
  const kutu = document.getElementById('bildirimler');
  if (!kutu) return;
  const el = h('div', { class: tur === 'ok' ? 'toast is-ok' : 'toast', role: tur === 'ok' ? 'status' : 'alert', text: metin });
  kutu.append(el);
  while (kutu.children.length > 3) kutu.firstElementChild.remove();
  setTimeout(() => el.classList.add('is-leaving'), 4200);
  setTimeout(() => el.remove(), 4600);
}

// JSON istekleri: hata metnini sunucudan alır
export async function istek(yol, { yontem = 'GET', govde } = {}) {
  const r = await fetch(yol, {
    method: yontem,
    headers: govde !== undefined ? { 'Content-Type': 'application/json' } : undefined,
    body: govde !== undefined ? JSON.stringify(govde) : undefined,
    credentials: 'same-origin',
  });
  let veri = null;
  try {
    veri = await r.json();
  } catch {
    veri = null;
  }
  if (!r.ok) {
    const hata = new Error(veri?.hata || `İstek başarısız oldu (HTTP ${r.status}).`);
    hata.durum = r.status;
    hata.veri = veri;
    throw hata;
  }
  return veri;
}

// Dosyayı XHR ile yükler (ilerleme gösterebilmek için)
export function dosyaYukle(klasor, dosya, { uzerine = false, ilerleme } = {}) {
  return new Promise((coz, reddet) => {
    const xhr = new XMLHttpRequest();
    const sorgu = new URLSearchParams({ yol: klasor, ad: dosya.name });
    if (uzerine) sorgu.set('uzerine', '1');
    xhr.open('POST', `/api/yukle?${sorgu}`);
    xhr.setRequestHeader('Content-Type', 'application/octet-stream');
    if (ilerleme) {
      xhr.upload.onprogress = (e) => {
        if (e.lengthComputable) ilerleme(e.loaded / e.total);
      };
    }
    xhr.onload = () => {
      let veri = null;
      try {
        veri = JSON.parse(xhr.responseText);
      } catch {
        veri = null;
      }
      if (xhr.status === 200) coz(veri);
      else {
        const hata = new Error(veri?.hata || `Yükleme başarısız oldu (HTTP ${xhr.status}).`);
        hata.durum = xhr.status;
        hata.veri = veri;
        reddet(hata);
      }
    };
    xhr.onerror = () => reddet(new Error('Yükleme sırasında bağlantı koptu.'));
    xhr.send(dosya);
  });
}

// Basit dosya simgeleri (satır içi SVG)
export function dosyaSimgesi(tur) {
  const yol = {
    klasor: 'M3 6.5A1.5 1.5 0 0 1 4.5 5h4l2 2h9A1.5 1.5 0 0 1 21 8.5v9a1.5 1.5 0 0 1-1.5 1.5h-15A1.5 1.5 0 0 1 3 17.5z',
    gorsel: 'M4 5h16v14H4zM4 15l4.5-4.5 4 4 2.5-2.5L20 17M15.5 9.5a1 1 0 1 0 0-.01',
    metin: 'M6 3h9l4 4v14H6zM14 3v5h5M9 12h7M9 15.5h7M9 19h4',
    diger: 'M6 3h9l4 4v14H6zM14 3v5h5',
  }[tur] || '';
  return svg(
    'svg',
    { viewBox: '0 0 24 24', 'aria-hidden': 'true', focusable: 'false' },
    svg('path', { d: yol, fill: 'none', stroke: 'currentColor', 'stroke-width': '1.6', 'stroke-linejoin': 'round', 'stroke-linecap': 'round' }),
  );
}

export const METIN_UZANTILARI = new Set(['txt', 'md', 'json', 'csv', 'c', 'h', 'log', 'ini', 'yml', 'yaml', 'xml']);
export const GORSEL_UZANTILARI = new Set(['png', 'jpg', 'jpeg', 'gif', 'webp', 'svg']);
