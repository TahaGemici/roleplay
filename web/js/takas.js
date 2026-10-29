import { $, h, paraYaz, paraParcala, paraBirlestir, sayiYaz } from './ortak.js';
import { grupla, esyaKarti } from './envanter.js';
import { ses } from './ses.js';

const YUVA = 4;

/* Teklifteki eşyanın türü: sunucu bu alanlarla eşleştirir */
const tur = (e) => ({ ad: e.ad, sinif: e.sinif, resim: e.resim, agirlik: e.agirlik, dayaniklilik: e.dayaniklilik });
const ayni = (a, b) => a && b && a.ad === b.ad && a.sinif === b.sinif && a.resim === b.resim && a.agirlik === b.agirlik && a.dayaniklilik === b.dayaniklilik;

/**
 * Takas penceresi. Solda kendi teklifin (en fazla 4 çeşit: eşya ya da para), sağda karşı tarafın teklifi. Kimse
 * karşısının envanterini görmez; yalnızca ortaya konanlar görünür. İki taraf da onaylayınca takas olur; teklif
 * değişince onaylar düşer. Oyuncu kendi takasını, GM NPC'lerin takaslarını (NPC adına) yürütür.
 */
export function takasOlustur({ gonder }) {
  const d = $('#takas');
  const benimKap = $('#takas-benim');
  const onunKap = $('#takas-onun');
  let durum = null;
  let takas = null; // { id, karsi, benim, onun, onayim, onayi, kendim: {ad, envanter, para} }
  let sonImza = '';

  /* Oyuncunun ya da GM'in (NPC adına) yürüttüğü takas, ortak bir biçime çevrilir */
  function bul() {
    if (!durum) return null;
    if (!durum.ben.gm) {
      const t = durum.takas;
      return t ? { ...t, kendim: { ad: durum.ben.ad, envanter: durum.envanter, para: durum.ben.para } } : null;
    }
    for (const t of durum.takaslar || []) {
      const s = t.taraflar.findIndex((x) => x.npc);
      if (s < 0) continue;
      const npc = durum.karakterler.find((k) => k.id === t.taraflar[s].id);
      if (!npc) continue;
      return {
        id: t.id,
        karsi: t.taraflar[1 - s],
        benim: t.yuva[s],
        onun: t.yuva[1 - s],
        onayim: t.onay[s],
        onayi: t.onay[1 - s],
        kendim: { ad: npc.ad, envanter: npc.envanter, para: npc.para },
      };
    }
    return null;
  }

  function teklifGonder(yuvalar) {
    gonder({ t: 'takas_teklif', id: takas.id, yuvalar });
  }

  /* Şu anki teklif, değiştirilecek yuvayla birlikte */
  function teklif(i, yeni) {
    const y = takas.benim.map((x) => (x ? (x.tur === 'esya' ? { tur: 'esya', esya: tur(x.esya), adet: x.adet } : { tur: 'para', para: x.para }) : null));
    y[i] = yeni;
    return y;
  }

  function benimYuva(i, yuva, gruplar) {
    const sec = h('select', { class: 'input', 'aria-label': `${i + 1}. yuva` });
    sec.append(h('option', { value: '', text: '— boş —' }));
    if (takas.kendim.para > 0) sec.append(h('option', { value: 'para', text: `Para (${paraYaz(takas.kendim.para)})` }));
    gruplar.forEach((g, n) => {
      const e = g.esya;
      sec.append(h('option', { value: `e${n}`, text: `${e.ad}${g.adet > 1 ? ` ×${g.adet}` : ''}${e.dayaniklilik !== undefined ? ` · day. ${sayiYaz(e.dayaniklilik)}` : ''}` }));
    });
    const ek = h('div', { class: 'trade-extra' });
    if (yuva?.tur === 'para') {
      sec.value = 'para';
      const p = paraParcala(yuva.para);
      const alan = (ad, deger) => h('label', { class: 'coin' }, h('input', { class: 'input', type: 'number', min: '0', step: '1', inputmode: 'numeric', value: String(deger), dataset: { para: ad } }), h('span', { text: ad }));
      ek.append(alan('altın', p.altin), alan('gümüş', p.gumus), alan('bakır', p.bakir));
      ek.addEventListener('change', () => {
        const [a, g, b] = [...ek.querySelectorAll('input')].map((x) => x.value);
        const toplam = paraBirlestir(a, g, b);
        teklifGonder(teklif(i, toplam > 0 ? { tur: 'para', para: toplam } : null));
      });
    } else if (yuva?.tur === 'esya') {
      const n = gruplar.findIndex((g) => ayni(g.esya, yuva.esya));
      if (n >= 0) {
        sec.value = `e${n}`;
        const adet = h('input', { class: 'input', type: 'number', min: '1', max: String(gruplar[n].adet), step: '1', value: String(yuva.adet), 'aria-label': 'Adet' });
        adet.addEventListener('change', () => teklifGonder(teklif(i, { tur: 'esya', esya: tur(yuva.esya), adet: Math.max(1, Math.min(gruplar[n].adet, Number(adet.value) || 1)) })));
        ek.append(h('label', { class: 'coin' }, adet, h('span', { text: 'adet' })));
      }
    }
    sec.addEventListener('change', () => {
      if (!sec.value) return teklifGonder(teklif(i, null));
      if (sec.value === 'para') return teklifGonder(teklif(i, { tur: 'para', para: Math.min(takas.kendim.para, 1) }));
      const g = gruplar[Number(sec.value.slice(1))];
      teklifGonder(teklif(i, { tur: 'esya', esya: tur(g.esya), adet: 1 }));
    });
    return h('li', { class: 'trade-slot' }, sec, ek);
  }

  function onunYuva(yuva) {
    if (!yuva) return h('li', { class: 'trade-slot is-empty', text: '—' });
    if (yuva.tur === 'para') return h('li', { class: 'trade-slot is-coin' }, h('span', { class: 'coin-icon', 'aria-hidden': 'true', text: '◉' }), h('span', { text: paraYaz(yuva.para) }));
    return h('li', { class: 'trade-slot' }, esyaKarti(yuva.esya, { adet: yuva.adet }), yuva.esya.dayaniklilik !== undefined ? h('span', { class: 'hint', text: `dayanıklılık ${sayiYaz(yuva.esya.dayaniklilik)}` }) : null);
  }

  function ciz() {
    $('#takas-baslik').textContent = durum.ben.gm ? `Takas: ${takas.kendim.ad} (NPC) ↔ ${takas.karsi.ad}` : `Takas: ${takas.karsi.ad}`;
    $('#takas-sol').textContent = durum.ben.gm ? `${takas.kendim.ad} veriyor` : 'Senin teklifin';
    $('#takas-sag').textContent = `${takas.karsi.ad} veriyor`;
    /* Teklif ya da envanter değiştiyse sol taraf yeniden çizilir; odaktaki girdi yerinde kalır. Değişmediyse
       odaktaki girdi (ör. yazılmakta olan adet) yeniden çizilip silinmez. */
    const gruplar = grupla(takas.kendim.envanter.filter((e) => !e.sabit));
    const imza = JSON.stringify([takas.benim, gruplar.map((g) => [g.esya.ad, g.adet]), takas.kendim.para]);
    const odak = document.activeElement;
    if (imza !== sonImza || !benimKap.contains(odak)) {
      const yuvaNo = [...benimKap.children].findIndex((li) => li.contains(odak));
      const neydi = odak?.tagName === 'SELECT' ? 'select' : odak?.dataset?.para ? `[data-para="${odak.dataset.para}"]` : 'input';
      benimKap.replaceChildren(...Array.from({ length: YUVA }, (_, i) => benimYuva(i, takas.benim[i], gruplar)));
      if (yuvaNo >= 0) benimKap.children[yuvaNo]?.querySelector(neydi)?.focus();
      sonImza = imza;
    }
    onunKap.replaceChildren(...Array.from({ length: YUVA }, (_, i) => onunYuva(takas.onun[i])));
    $('#takas-durum').textContent = [takas.onayim ? 'Sen onayladın ✓' : 'Henüz onaylamadın', takas.onayi ? `${takas.karsi.ad} onayladı ✓` : `${takas.karsi.ad} henüz onaylamadı`].join(' · ');
    const onay = $('#takas-onay');
    onay.textContent = takas.onayim ? 'Onayı geri al' : 'Onayla';
    onay.classList.toggle('btn-primary', !takas.onayim);
  }

  $('#takas-onay').addEventListener('click', () => takas && gonder({ t: 'takas_onay', id: takas.id, onay: !takas.onayim }));
  $('#takas-vazgec').addEventListener('click', () => takas && gonder({ t: 'takas_iptal', id: takas.id }));
  d.addEventListener('cancel', (e) => {
    e.preventDefault(); /* Esc: vazgeç */
    if (takas) gonder({ t: 'takas_iptal', id: takas.id });
  });

  return {
    ayarla(yeni) {
      durum = yeni;
      const eski = takas;
      takas = bul();
      if (!takas) {
        if (d.open) d.close();
        return;
      }
      if (!eski || eski.id !== takas.id) {
        sonImza = ''; /* yeni takas: yuvalar baştan çizilsin */
        ses.para();
      }
      ciz();
      if (!d.open) d.showModal();
    },
  };
}
