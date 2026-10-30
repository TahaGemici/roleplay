import { h, sayiYaz } from './ortak.js';
import { goster, onay } from './pencere.js';

/* Resmi olmayan (ya da resmi görsel olmayan) eşyanın varsayılan resmi: sınıfına göre kılıç, kalkan, şişe; öbürleri kese */
const CIZGI = 'stroke="#22180f" stroke-width="2.6" stroke-linejoin="round" stroke-linecap="round"';
const VARSAYILAN_RESIM = {
  silah: `<path d="M56 8 54.5 17.5 29 43 21 35 46.5 9.5Z" fill="#dfe3e8" ${CIZGI}/><path d="M48 12 27 33" stroke="#a9b0b8" stroke-width="2"/><path d="M15 33 31 49" stroke="#c9a24a" stroke-width="7" stroke-linecap="round"/><path d="M15 33 31 49" fill="none" ${CIZGI} stroke-width="1.6"/><path d="M23 41 12 52" stroke="#7a5b3c" stroke-width="6.5" stroke-linecap="round"/><circle cx="10" cy="54" r="4.5" fill="#c9a24a" ${CIZGI}/>`,
  kalkan: `<path d="M32 6 54 14V30C54 44 44 53 32 58 20 53 10 44 10 30V14Z" fill="#8a6a2f" ${CIZGI}/><path d="M32 11 49 17.5V30C49 41 41.5 48.5 32 52.5 22.5 48.5 15 41 15 30V17.5Z" fill="#a77d38"/><path d="M32 11V52M15.5 29H48.5" stroke="#e2c071" stroke-width="4"/><circle cx="32" cy="29" r="5.5" fill="#e2c071" ${CIZGI}/>`,
  sise: `<rect x="24" y="5" width="16" height="7" rx="2" fill="#7a5b3c" ${CIZGI}/><path d="M27 12H37V23C45 26 50 32 50 40 50 50 42 58 32 58 22 58 14 50 14 40 14 32 19 26 27 23Z" fill="#e8eef2" fill-opacity=".85" ${CIZGI}/><path d="M16.3 42H47.7C46.6 51 40 56 32 56 24 56 17.4 51 16.3 42Z" fill="#c9503b"/><circle cx="25" cy="36" r="3" fill="#fff" fill-opacity=".8"/>`,
  kese: `<path d="M23 21C15 29 10 39 12 48 14 56 22 59 32 59 42 59 50 56 52 48 54 39 49 29 41 21Z" fill="#8a6440" ${CIZGI}/><path d="M24 21 19 9 28 13 32 7 36 13 45 9 40 21" fill="#a47a4e" ${CIZGI}/><path d="M22 21H42" stroke="#c9a24a" stroke-width="4.5" stroke-linecap="round"/><path d="M24 40C28 43 36 43 40 40" fill="none" stroke="#6b4c30" stroke-width="2"/>`,
};
const SINIF_RESMI = { silah: 'silah', silahlar: 'silah', kilic: 'silah', 'kılıç': 'silah', zirh: 'kalkan', 'zırh': 'kalkan', kalkan: 'kalkan', iksir: 'sise', iksirler: 'sise', 'şişe': 'sise' };
const resimOnbellegi = new Map();

export function varsayilanEsyaResmi(sinif) {
  const ad = SINIF_RESMI[String(sinif || '').toLocaleLowerCase('tr')] || 'kese';
  let url = resimOnbellegi.get(ad);
  if (!url) {
    url = `data:image/svg+xml,${encodeURIComponent(`<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64">${VARSAYILAN_RESIM[ad]}</svg>`)}`;
    resimOnbellegi.set(ad, url);
  }
  return url;
}

/* Eşyanın kendi resmi varsa o (ve true), yoksa sınıfının varsayılan resmi */
export const resimli = (e) => Boolean(e?.url && e.gorsel && e.var !== false);
export const esyaResmi = (e) => (resimli(e) ? e.url : varsayilanEsyaResmi(e?.sinif));

/* Envanterdeki eşya sayısı: yumruk yer kaplamaz */
export const esyaSayisi = (envanter) => (envanter || []).filter((e) => !e.sabit).length;

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
  if (e.sabit && !resimli(e)) return h('span', { class: 'item-icon is-fist', 'aria-hidden': 'true', text: '✊' });
  return h('img', { class: `item-img${resimli(e) ? '' : ' is-default'}`, src: esyaResmi(e), alt: '', loading: 'lazy', decoding: 'async' });
}

export const agirlikYaz = (e) => (e.sabit ? 'bırakılamaz' : `ağırlık ${sayiYaz(e.agirlik)} · day. ${sayiYaz(e.dayaniklilik)}`);

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
  else satir.append('Ağırlık: ', h('b', { text: sayiYaz(e.agirlik) }), ' · ');
  satir.append('Dayanıklılık: ', h('b', { text: sayiYaz(e.dayaniklilik) }));
  const icerik = [satir];
  if (resimli(e)) icerik.push(h('img', { class: 'preview-img', src: e.url, alt: e.ad }));
  else if (!e.sabit) {
    icerik.push(h('img', { class: 'preview-img is-default', src: esyaResmi(e), alt: e.ad }));
    if (e.resim && e.var === false) icerik.push(h('p', { class: 'modal-text', text: 'Bu eşyanın resmi artık oyun klasöründe yok; varsayılan resmi görünüyor.' }));
  }
  if (adet > 1) icerik.push(h('p', { class: 'modal-text', text: `Sende ${adet} tane var.` }));
  return goster({ baslik: e.ad, icerik, genis: resimli(e), ekDugmeler });
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
  const n = esyaSayisi(envanter);
  if (!n) kartlar.push(h('li', { class: 'inventory-empty', text: 'GM sana eşya verince ya da yerdeki bir eşyanın üstüne yürüyünce burada görünür. Yumruk yer kaplamaz.' }));
  else if (azami && n >= azami) kartlar.push(h('li', { class: 'inventory-empty', text: `Envanterin dolu (${azami} eşya; yumruk sayılmaz). Yeni bir şey almak için bir eşyayı yere bırak.` }));
  liste.replaceChildren(...kartlar);
}
