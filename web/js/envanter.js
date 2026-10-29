import { h, uzanti } from './ortak.js';
import { goster } from './pencere.js';

/* Aynı eşyanın kopyalarını tek kartta topla: [{ esya, adet, siralar: [envanterdeki sıralar] }] */
export function grupla(envanter) {
  const gruplar = [];
  const anahtar = new Map();
  envanter.forEach((e, i) => {
    const k = `${e.ad}\u0000${e.sinif}\u0000${e.resim}`;
    let g = anahtar.get(k);
    if (!g) {
      g = { esya: e, adet: 0, siralar: [] };
      anahtar.set(k, g);
      gruplar.push(g);
    }
    g.adet++;
    g.siralar.push(i);
  });
  return gruplar;
}

function gorsel(e) {
  if (e.url && e.gorsel && e.var !== false) return h('img', { class: 'item-img', src: e.url, alt: '', loading: 'lazy', decoding: 'async' });
  const yazi = e.resim ? (uzanti(e.resim) || 'dosya').toLocaleUpperCase('tr') : (e.ad || '?').slice(0, 2).toLocaleUpperCase('tr');
  return h('span', { class: 'item-icon', 'aria-hidden': 'true', text: yazi });
}

// Eşya kartı: resim, ad ve sınıf
export function esyaKarti(e, { adet, tikla, baslik, secili } = {}) {
  return h(
    'button',
    { class: `item${e.var === false && e.resim ? ' is-missing' : ''}${secili ? ' is-selected' : ''}`, type: 'button', title: baslik || e.ad, onclick: tikla },
    gorsel(e),
    h('span', { class: 'item-name', text: e.ad }),
    e.sinif ? h('span', { class: 'item-class', text: e.sinif }) : null,
    adet > 1 ? h('span', { class: 'item-count', text: `×${adet}` }) : null,
  );
}

export function esyaOnizle(e, adet = 1) {
  const icerik = [];
  if (e.sinif) icerik.push(h('p', { class: 'modal-text' }, 'Sınıf: ', h('b', { text: e.sinif })));
  if (e.url && e.gorsel && e.var !== false) icerik.push(h('img', { class: 'preview-img', src: e.url, alt: e.ad }));
  else if (e.url && e.var !== false) icerik.push(h('p', { class: 'modal-text' }, h('a', { href: e.url, target: '_blank', rel: 'noopener', text: 'Dosyayı yeni sekmede aç' })));
  else if (e.resim) icerik.push(h('p', { class: 'modal-text', text: 'Bu eşyanın resmi artık oyun klasöründe yok.' }));
  if (adet > 1) icerik.push(h('p', { class: 'modal-text', text: `Sende ${adet} tane var.` }));
  return goster({ baslik: e.ad, icerik, genis: Boolean(e.url && e.gorsel) });
}

// Oyuncunun kendi envanteri
export function envanterCiz(liste, envanter) {
  if (!envanter.length) {
    liste.replaceChildren(h('li', { class: 'inventory-empty', text: 'Envanterin boş. GM sana eşya verdiğinde burada görünecek.' }));
    return;
  }
  liste.replaceChildren(...grupla(envanter).map((g) => h('li', {}, esyaKarti(g.esya, { adet: g.adet, tikla: () => esyaOnizle(g.esya, g.adet) }))));
}
