import { h, bildir, dosyaYukle, dosyaAdi, adKisalt, basHarfler, yaziRengi } from './ortak.js';
import { goster } from './pencere.js';

const GORSEL = /\.(png|jpe?g|gif|webp|svg)$/i;

/* Pencerede pulun nasıl görüneceği: haritadaki tokenla aynı yapı */
function onizleme(k) {
  const disk = h('div', { class: 'token-disc' }, h('span', { class: 'token-initials', text: basHarfler(k.ad) }));
  if (k.resim) disk.append(h('img', { class: 'token-img', src: k.resim, alt: '' }));
  const govde = h('div', { class: 'token-body' }, disk);
  if (k.sapka) govde.append(h('img', { class: 'token-hat', src: k.sapka, alt: '' }));
  return h(
    'div',
    { class: 'look-preview', 'aria-hidden': 'true' },
    h('div', { class: `token${k.npc ? ' is-npc' : ''}${k.resim ? ' has-img' : ''}`, vars: { '--ts': '76px', '--tc': k.renk, '--ti': yaziRengi(k.renk) } }, govde),
  );
}

/**
 * Token resmi ve şapka seçimi. Seçilen hemen herkesin haritasında görünür; GM onayı gerekmez.
 * karakterAl(): seçilen karakterin son hali ({ id, ad, renk, npc, resim, sapka, resim_yolu, sapka_yolu }),
 * durumAl(): son durum (gorunum.resimler / gorunum.sapkalar). GM ise yükleme düğmeleri de çıkar ve id gönderilir.
 * Döndürdüğü yenile() her yeni durumda çağrılır; pencere kapanınca kapandi() true olur.
 */
export function gorunumPenceresi({ karakterAl, durumAl, gonder, gm }) {
  const kap = h('div', { class: 'look' });
  /* Dosya seçici pencerenin içinde durur (pencere açıkken dışarısı tıklanamaz) ve yeniden çizimde silinmez */
  const girdi = h('input', { type: 'file', accept: 'image/png,image/jpeg,image/webp,image/gif,image/svg+xml', hidden: true });
  let yukleme = null; // { klasor, alan }
  let kapali = false;

  const sec = (alan, yol) => gonder({ t: 'gorunum', ...(gm ? { id: karakterAl().id } : {}), [alan]: yol });

  girdi.addEventListener('change', async () => {
    const f = girdi.files[0];
    girdi.value = '';
    if (!f || !yukleme) return;
    if (!GORSEL.test(f.name)) return bildir('PNG, JPG, WEBP, GIF ya da SVG seç.');
    const { klasor, alan } = yukleme;
    try {
      await dosyaYukle(klasor, f);
      sec(alan, `${klasor}/${f.name}`);
    } catch (e) {
      bildir(`${adKisalt(f.name)}: ${e.message}`);
    }
  });

  function yukle(klasor, alan) {
    yukleme = { klasor, alan };
    girdi.click();
  }

  function izgara(liste, secili, alan, bosMetin) {
    const secenek = (yol, url, ad) =>
      h(
        'li',
        {},
        h(
          'button',
          { class: `look-opt${secili === yol ? ' is-selected' : ''}`, type: 'button', 'aria-pressed': String(secili === yol), title: ad, onclick: () => sec(alan, yol) },
          url ? h('img', { src: url, alt: '', loading: 'lazy' }) : h('span', { class: 'look-none', text: '∅' }),
          h('span', { class: 'look-name', text: ad }),
        ),
      );
    return h('ul', { class: 'look-grid' }, secenek('', null, bosMetin), ...liste.map((d) => secenek(d.dosya, d.url, dosyaAdi(d.dosya))));
  }

  function ciz() {
    const k = karakterAl();
    const d = durumAl();
    if (!k || !d) return;
    const g = d.gorunum || { resimler: [], sapkalar: [] };
    kap.replaceChildren(
      ...[
      h('div', { class: 'look-head' }, onizleme(k), h('p', { class: 'modal-text', text: gm ? `${k.ad} için seçtiğin hemen herkesin haritasında görünür.` : 'Seçtiğin hemen herkesin haritasında görünür.' })),
      h('div', { class: 'sub-head' }, h('h3', { class: 'sub-title', text: 'Şapka' }), gm ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => yukle('sapkalar', 'sapka'), text: 'Şapka yükle' }) : null),
      izgara(g.sapkalar, k.sapka_yolu || '', 'sapka', 'Şapkasız'),
      h('div', { class: 'sub-head' }, h('h3', { class: 'sub-title', text: 'Resim' }), gm ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => yukle('tokenlar', 'resim'), text: 'Resim yükle' }) : null),
      g.resimler.length || gm ? izgara(g.resimler, k.resim_yolu || '', 'resim', 'Resimsiz') : null,
      g.resimler.length ? null : h('p', { class: 'hint', text: gm ? 'tokenlar klasörü boş. "Resim yükle" ile ekle.' : 'Henüz resim yok; GM tokenlar klasörüne resim ekleyince burada seçebilirsin.' }),
      ].filter(Boolean),
    );
  }

  ciz();
  goster({ baslik: gm ? `${karakterAl().ad}: görünüm` : 'Görünümün', icerik: [kap, girdi], genis: true }).then(() => {
    kapali = true;
  });
  return {
    yenile() {
      if (!kapali) ciz();
    },
    kapandi: () => kapali,
  };
}
