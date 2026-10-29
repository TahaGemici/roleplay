import { $, h, istek, dosyaYukle, bildir, boyutYaz, tarihSaat, dosyaSimgesi, yolKodla, uzanti, adKisalt, METIN_UZANTILARI, GORSEL_UZANTILARI } from './ortak.js';
import { soru, onay, goster, duzenle } from './pencere.js';

const KORUNAN = new Set(['durum.json', 'haritalar', 'esyalar']);
const birlestir = (a, b) => (a ? `${a}/${b}` : b);

/** GM'in oyun klasörüne tam erişimi: gezin, yükle, indir, düzenle, yeniden adlandır, taşı, sil. */
export function dosyaYoneticisiOlustur() {
  const d = $('#dosyalar');
  const tablo = $('#dosya-liste');
  const bos = $('#dosya-bos');
  const durumSatiri = $('#dosya-durum');
  let yol = '';
  let ogeler = [];

  async function git(klasor) {
    try {
      const veri = await istek(`/api/dosyalar?yol=${encodeURIComponent(klasor)}`);
      yol = veri.yol;
      ogeler = veri.ogeler;
      ciz();
    } catch (e) {
      bildir(e.message);
      if (klasor) git('');
    }
  }
  const yenile = () => git(yol);

  function kirintilar() {
    const nav = $('#dosya-yol');
    const parcalar = yol ? yol.split('/') : [];
    const dugmeler = [h('button', { class: 'crumb', type: 'button', 'aria-current': parcalar.length ? null : 'true', onclick: () => git(''), text: 'oyun' })];
    parcalar.forEach((p, i) => {
      const hedef = parcalar.slice(0, i + 1).join('/');
      dugmeler.push(h('span', { 'aria-hidden': 'true', text: '/' }));
      dugmeler.push(h('button', { class: 'crumb', type: 'button', 'aria-current': i === parcalar.length - 1 ? 'true' : null, onclick: () => git(hedef), text: p }));
    });
    nav.replaceChildren(...dugmeler);
  }

  function ciz() {
    kirintilar();
    bos.hidden = ogeler.length > 0;
    tablo.replaceChildren(...ogeler.map(satir));
  }

  function tur(o) {
    if (o.klasor) return 'klasor';
    if (o.gorsel || GORSEL_UZANTILARI.has(uzanti(o.ad))) return 'gorsel';
    if (METIN_UZANTILARI.has(uzanti(o.ad))) return 'metin';
    return 'diger';
  }

  function satir(o) {
    const tam = birlestir(yol, o.ad);
    const korunan = !yol && KORUNAN.has(o.ad);
    const t = tur(o);
    const ac = () => {
      if (t === 'klasor') git(tam);
      else if (t === 'gorsel') onizle(tam);
      else if (t === 'metin' && o.boyut <= 1024 * 1024) duzenleAc(tam, o.ad);
      else indir(tam);
    };
    const islemler = [];
    if (!o.klasor) islemler.push(h('button', { class: 'btn btn-ghost', type: 'button', onclick: () => indir(tam), text: 'İndir' }));
    if (t === 'metin' && !korunan) islemler.push(h('button', { class: 'btn btn-ghost', type: 'button', onclick: () => duzenleAc(tam, o.ad), text: 'Düzenle' }));
    if (!korunan) {
      islemler.push(
        h('button', { class: 'btn btn-ghost', type: 'button', onclick: () => yenidenAdlandir(tam, o.ad), text: 'Yeniden adlandır' }),
        h('button', { class: 'btn btn-ghost', type: 'button', onclick: () => tasi(tam), text: 'Taşı' }),
        h('button', { class: 'btn btn-ghost', type: 'button', onclick: () => sil(tam, o), text: 'Sil' }),
      );
    }
    return h(
      'tr',
      {},
      h(
        'td',
        {},
        h('button', { class: `file-name${o.klasor ? ' is-folder' : ''}`, type: 'button', onclick: ac }, dosyaSimgesi(t), h('span', { text: o.ad })),
        korunan ? h('span', { class: 'file-tag', text: o.klasor ? 'oyun kullanıyor' : 'oyunun kayıt dosyası' }) : null,
      ),
      h('td', { class: 'num', text: o.klasor ? '—' : boyutYaz(o.boyut) }),
      h('td', { class: 'num', text: tarihSaat(o.degisim) }),
      h('td', {}, h('div', { class: 'row-actions' }, islemler)),
    );
  }

  function indir(tam) {
    const a = h('a', { href: `/oyun/${yolKodla(tam)}?indir=1`, download: '' });
    document.body.append(a);
    a.click();
    a.remove();
  }

  function onizle(tam) {
    const url = `/oyun/${yolKodla(tam)}?t=${Date.now()}`;
    goster({ baslik: tam, icerik: h('img', { class: 'preview-img', src: url, alt: '' }) });
  }

  async function duzenleAc(tam, ad) {
    let metin;
    try {
      const r = await fetch(`/oyun/${yolKodla(tam)}`, { cache: 'no-store' });
      if (!r.ok) throw new Error('Dosya açılamadı.');
      metin = await r.text();
    } catch (e) {
      bildir(e.message);
      return;
    }
    const yeni = await duzenle({ baslik: tam, metin });
    if (yeni === null || yeni === metin) return;
    const klasor = tam.includes('/') ? tam.slice(0, tam.lastIndexOf('/')) : '';
    try {
      await dosyaYukle(klasor, new File([yeni], ad, { type: 'text/plain' }), { uzerine: true });
      bildir(`${ad} kaydedildi.`, 'ok');
      yenile();
    } catch (e) {
      bildir(e.message);
    }
  }

  async function yenidenAdlandir(tam, ad) {
    const yeni = await soru({ baslik: 'Yeniden adlandır', deger: ad, dugme: 'Adı değiştir' });
    if (!yeni || yeni === ad) return;
    await tasiIstegi(tam, birlestir(yol, yeni));
  }

  async function tasi(tam) {
    const hedef = await soru({
      baslik: 'Taşı',
      metin: 'Yeni yolu oyun klasörüne göre yaz. Örnek: esyalar/silahlar/kılıç.png',
      deger: tam,
      dugme: 'Taşı',
    });
    if (!hedef || hedef === tam) return;
    await tasiIstegi(tam, hedef.replace(/^\/+|\/+$/g, ''));
  }

  async function tasiIstegi(kaynak, hedef) {
    try {
      await istek('/api/tasi', { yontem: 'POST', govde: { kaynak, hedef } });
      yenile();
    } catch (e) {
      bildir(e.message);
    }
  }

  async function sil(tam, o) {
    const evet = await onay({
      baslik: o.klasor ? `“${o.ad}” klasörü silinsin mi?` : `“${o.ad}” silinsin mi?`,
      metin: o.klasor ? 'İçindeki her şey de silinir. Bu geri alınamaz.' : 'Bu geri alınamaz. Eşyaysa envanterlerden de kalkar.',
      dugme: 'Sil',
      tehlikeli: true,
    });
    if (!evet) return;
    try {
      await istek('/api/sil', { yontem: 'POST', govde: { yol: tam } });
      yenile();
    } catch (e) {
      bildir(e.message);
    }
  }

  async function yeniKlasor() {
    const ad = await soru({ baslik: 'Yeni klasör', metin: yol ? `${yol} içinde oluşturulacak.` : 'Oyun klasörünün içinde oluşturulacak.', dugme: 'Oluştur' });
    if (!ad) return;
    try {
      await istek('/api/klasor', { yontem: 'POST', govde: { yol: birlestir(yol, ad) } });
      yenile();
    } catch (e) {
      bildir(e.message);
    }
  }

  // Dosyaları sırayla yükler; aynı adda dosya varsa sorar.
  async function yukle(dosyalar, klasor = yol) {
    for (const dosya of dosyalar) {
      try {
        await dosyaYukle(klasor, dosya, { ilerleme: (o) => (durumSatiri.textContent = `Yükleniyor: ${adKisalt(dosya.name)} %${Math.round(o * 100)}`) });
      } catch (e) {
        if (e.veri?.var) {
          const evet = await onay({ baslik: `“${adKisalt(dosya.name)}” zaten var`, metin: 'Yeni dosya eskisinin yerine geçsin mi?', dugme: 'Üzerine yaz', tehlikeli: true });
          if (evet) {
            try {
              await dosyaYukle(klasor, dosya, { uzerine: true });
            } catch (e2) {
              bildir(e2.message);
            }
          }
        } else {
          bildir(`${adKisalt(dosya.name)}: ${e.message}`);
        }
      }
    }
    durumSatiri.textContent = '';
    if (d.open && klasor === yol) yenile();
  }

  $('#dosya-klasor').addEventListener('click', yeniKlasor);
  $('#dosyalar-kapat').addEventListener('click', () => d.close());
  $('#dosya-yukle').addEventListener('click', () => {
    const girdi = $('#dosya-sec');
    girdi.accept = '';
    girdi.onchange = () => {
      const dosyalar = [...girdi.files];
      girdi.value = '';
      yukle(dosyalar);
    };
    girdi.click();
  });

  const alan = $('#dosya-alani');
  const ipucu = $('#dosya-birak');
  let derinlik = 0;
  const dosyaVar = (e) => [...(e.dataTransfer?.types || [])].includes('Files');
  alan.addEventListener('dragenter', (e) => {
    if (!dosyaVar(e)) return;
    e.preventDefault();
    derinlik++;
    ipucu.hidden = false;
  });
  alan.addEventListener('dragover', (e) => {
    if (dosyaVar(e)) e.preventDefault();
  });
  alan.addEventListener('dragleave', (e) => {
    if (!dosyaVar(e)) return;
    derinlik = Math.max(0, derinlik - 1);
    if (!derinlik) ipucu.hidden = true;
  });
  alan.addEventListener('drop', (e) => {
    if (!dosyaVar(e)) return;
    e.preventDefault();
    derinlik = 0;
    ipucu.hidden = true;
    yukle([...e.dataTransfer.files]);
  });

  return {
    ac(klasor = '') {
      if (!d.open) d.showModal();
      git(klasor);
    },
    yukle,
  };
}
