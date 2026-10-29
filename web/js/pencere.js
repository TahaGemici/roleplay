import { $, h } from './ortak.js';

// Tek bir <dialog> ile soru, onay, önizleme ve metin düzenleme pencereleri.
const pencere = () => $('#pencere');

function ac({ baslik, icerik, dugmeler, genis = false, odak }) {
  const d = pencere();
  return new Promise((coz) => {
    d.classList.toggle('is-wide', genis);
    $('#pencere-baslik').textContent = baslik;
    $('#pencere-icerik').replaceChildren(...[].concat(icerik).filter(Boolean));
    const kutu = $('#pencere-dugmeler');
    kutu.replaceChildren(
      ...dugmeler.map((b) =>
        h('button', {
          class: `btn ${b.sinif || ''}`,
          type: b.varsayilan ? 'submit' : 'button',
          value: b.deger,
          text: b.metin,
          onclick: b.varsayilan ? null : () => d.close(b.deger),
        }),
      ),
    );
    const kapaninca = () => {
      d.removeEventListener('close', kapaninca);
      coz(d.returnValue);
    };
    d.returnValue = '';
    d.addEventListener('close', kapaninca);
    $('#pencere-form').onsubmit = (e) => {
      e.preventDefault();
      d.close(e.submitter?.value || 'tamam');
    };
    d.showModal();
    if (odak) {
      odak.focus();
      if (odak.tagName === 'INPUT') odak.select();
    }
  });
}

export async function soru({ baslik, metin, deger = '', dugme = 'Tamam' }) {
  const girdi = h('input', { class: 'input', value: deger, maxlength: 255, spellcheck: 'false', autocomplete: 'off' });
  const sonuc = await ac({
    baslik,
    icerik: [metin ? h('p', { class: 'modal-text', text: metin }) : null, girdi],
    dugmeler: [
      { metin: 'Vazgeç', deger: 'iptal', sinif: 'btn-ghost' },
      { metin: dugme, deger: 'tamam', sinif: 'btn-primary', varsayilan: true },
    ],
    odak: girdi,
  });
  return sonuc === 'tamam' ? girdi.value.trim() : null;
}

export async function onay({ baslik, metin, dugme = 'Evet', tehlikeli = false }) {
  const sonuc = await ac({
    baslik,
    icerik: metin ? h('p', { class: 'modal-text', text: metin }) : null,
    dugmeler: [
      { metin: 'Vazgeç', deger: 'iptal', sinif: 'btn-ghost' },
      { metin: dugme, deger: 'tamam', sinif: tehlikeli ? 'btn-danger' : 'btn-primary', varsayilan: true },
    ],
  });
  return sonuc === 'tamam';
}

/* Ek düğmelerden biri birincilse (ör. "Kaydet") Enter onu seçer ve "Kapat" sade kalır */
export function goster({ baslik, icerik, genis = true, ekDugmeler = [] }) {
  const birincil = ekDugmeler.some((b) => b.sinif?.includes('btn-primary'));
  const dugmeler = birincil
    ? [{ metin: 'Kapat', deger: 'kapat', sinif: 'btn-ghost' }, ...ekDugmeler.map((b) => (b.sinif?.includes('btn-primary') ? { ...b, varsayilan: true } : b))]
    : [...ekDugmeler, { metin: 'Kapat', deger: 'kapat', sinif: 'btn-primary', varsayilan: true }];
  return ac({ baslik, icerik, genis, dugmeler });
}

export async function duzenle({ baslik, metin }) {
  const alan = h('textarea', { class: 'input editor', spellcheck: 'false' });
  alan.value = metin;
  const sonuc = await ac({
    baslik,
    genis: true,
    icerik: alan,
    dugmeler: [
      { metin: 'Vazgeç', deger: 'iptal', sinif: 'btn-ghost' },
      { metin: 'Kaydet', deger: 'kaydet', sinif: 'btn-primary', varsayilan: true },
    ],
    odak: alan,
  });
  return sonuc === 'kaydet' ? alan.value : null;
}

/**
 * Birkaç alanlı form. alanlar: [{ ad, etiket, tur: 'metin' | 'secim' | 'sayi', deger, secenekler: [{ deger, metin }],
 * oneriler: [...], en, ust }]. Onaylanırsa { ad: değer } döner, vazgeçilirse null.
 */
export async function formAc({ baslik, metin, alanlar, dugme = 'Tamam', tehlikeli = false }) {
  const girdiler = {};
  const icerik = [];
  if (metin) icerik.push(h('p', { class: 'modal-text', text: metin }));
  for (const a of alanlar) {
    let girdi;
    if (a.tur === 'secim') {
      girdi = h('select', { class: 'input' }, ...a.secenekler.map((s) => h('option', { value: s.deger, text: s.metin })));
      if (a.deger !== undefined) girdi.value = a.deger;
    } else if (a.tur === 'sayi') {
      girdi = h('input', { class: 'input', type: 'number', min: a.en, max: a.ust, step: 'any', inputmode: 'decimal', value: a.deger ?? '' });
    } else {
      const listeId = a.oneriler?.length ? `oneri-${a.ad}` : null;
      girdi = h('input', { class: 'input', value: a.deger ?? '', maxlength: a.azami || 120, spellcheck: 'false', autocomplete: 'off', list: listeId });
      if (listeId) icerik.push(h('datalist', { id: listeId }, ...a.oneriler.map((o) => h('option', { value: o }))));
    }
    girdiler[a.ad] = girdi;
    icerik.push(h('label', { class: 'field' }, h('span', { class: 'field-label', text: a.etiket }), girdi));
  }
  const ilk = Object.values(girdiler)[0];
  const sonuc = await ac({
    baslik,
    icerik,
    dugmeler: [
      { metin: 'Vazgeç', deger: 'iptal', sinif: 'btn-ghost' },
      { metin: dugme, deger: 'tamam', sinif: tehlikeli ? 'btn-danger' : 'btn-primary', varsayilan: true },
    ],
    odak: ilk,
  });
  if (sonuc !== 'tamam') return null;
  return Object.fromEntries(Object.entries(girdiler).map(([k, g]) => [k, g.value]));
}

/** İçinde düğmeler olan seçim penceresi: secim(deger) çağrılınca kapanır ve o değeri döner. */
export function secimPenceresi({ baslik, icerik: kur, genis = false }) {
  const d = $('#pencere');
  let secilen = null;
  const kapat = (deger) => {
    secilen = deger;
    d.close('secildi');
  };
  return ac({ baslik, icerik: kur(kapat), genis, dugmeler: [{ metin: 'Vazgeç', deger: 'iptal', sinif: 'btn-ghost', varsayilan: true }] }).then((r) =>
    r === 'secildi' ? secilen : null,
  );
}
