import { h, uzanti, sayiYaz } from './ortak.js';
import { goster, onay } from './pencere.js';

/* Aynı eşyanın kopyalarını tek kartta topla: [{ esya, adet, siralar: [envanterdeki sıralar] }] */
export function grupla(envanter) {
  const gruplar = [];
  const anahtar = new Map();
  envanter.forEach((e, i) => {
    const k = `${e.ad}\u0000${e.sinif}\u0000${e.resim}\u0000${e.agirlik}\u0000${e.dayaniklilik}\u0000${e.sabit ? 1 : 0}`;
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
  if (e.sabit) return h('span', { class: 'item-icon is-fist', 'aria-hidden': 'true', text: '✊' });
  const yazi = e.resim ? (uzanti(e.resim) || 'dosya').toLocaleUpperCase('tr') : (e.ad || '?').slice(0, 2).toLocaleUpperCase('tr');
  return h('span', { class: 'item-icon', 'aria-hidden': 'true', text: yazi });
}

export const agirlikYaz = (e) => (e.sabit ? 'bırakılamaz' : `ağırlık ${sayiYaz(e.agirlik ?? 1)} · day. ${sayiYaz(e.dayaniklilik ?? 100)}`);

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

export function esyaOnizle(e, adet = 1, ekDugmeler = []) {
  const satir = h('p', { class: 'modal-text' });
  if (e.sinif) satir.append('Sınıf: ', h('b', { text: e.sinif }), ' · ');
  if (e.sabit) satir.append('Herkeste var; bırakılamaz. · ');
  else satir.append('Ağırlık: ', h('b', { text: sayiYaz(e.agirlik ?? 1) }), ' · ');
  satir.append('Dayanıklılık: ', h('b', { text: sayiYaz(e.dayaniklilik ?? 100) }));
  const icerik = [satir];
  if (e.url && e.gorsel && e.var !== false) icerik.push(h('img', { class: 'preview-img', src: e.url, alt: e.ad }));
  else if (e.url && e.var !== false) icerik.push(h('p', { class: 'modal-text' }, h('a', { href: e.url, target: '_blank', rel: 'noopener', text: 'Dosyayı yeni sekmede aç' })));
  else if (e.resim) icerik.push(h('p', { class: 'modal-text', text: 'Bu eşyanın resmi artık oyun klasöründe yok.' }));
  if (adet > 1) icerik.push(h('p', { class: 'modal-text', text: `Sende ${adet} tane var.` }));
  return goster({ baslik: e.ad, icerik, genis: Boolean(e.url && e.gorsel), ekDugmeler });
}

/* Oyuncunun kendi envanteri. birak(sira, esya) verilirse önizlemede "Yere bırak", imha(sira, esya) verilirse "Yok et"
   düğmesi çıkar (yumrukta ikisi de çıkmaz). */
export function envanterCiz(liste, envanter, { azami, birak, imha } = {}) {
  const kartlar = grupla(envanter).map((g) =>
    h(
      'li',
      {},
      esyaKarti(g.esya, {
        adet: g.adet,
        tikla: async () => {
          const dugme = [];
          if (imha && !g.esya.sabit) dugme.push({ metin: 'Yok et', deger: 'imha', sinif: 'btn-ghost btn-danger-text' });
          if (birak && !g.esya.sabit) dugme.push({ metin: 'Yere bırak', deger: 'birak', sinif: 'btn-ghost' });
          const sonuc = await esyaOnizle(g.esya, g.adet, dugme);
          const sira = g.siralar[g.siralar.length - 1];
          if (sonuc === 'birak') birak(sira, g.esya);
          else if (sonuc === 'imha' && (await onay({ baslik: `${g.esya.ad} yok edilsin mi?`, metin: 'Eşya tamamen kaybolur; geri alınamaz.', dugme: 'Yok et', tehlikeli: true })))
            imha(sira, g.esya);
        },
      }),
    ),
  );
  if (envanter.length <= 1) kartlar.push(h('li', { class: 'inventory-empty', text: 'GM sana eşya verince ya da yerdeki bir eşyanın üstüne yürüyünce burada görünür.' }));
  else if (azami && envanter.length >= azami) kartlar.push(h('li', { class: 'inventory-empty', text: `Envanterin dolu (${azami} eşya). Yeni bir şey almak için bir eşyayı yere bırak.` }));
  liste.replaceChildren(...kartlar);
}
