import { h, bildir, istek, dosyaYukle, dosyaAdi, adKisalt, basHarfler, yaziRengi } from './ortak.js';
import { goster, onay } from './pencere.js';

const GORSEL = /\.(png|jpe?g|gif|webp|svg)$/i;
const OYUNCU_GORSELI = /\.(png|jpe?g|gif|webp)$/i; /* oyuncular SVG yükleyemez */

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
 * durumAl(): son durum (gorunum.resimler / gorunum.sapkalar). GM bütün resimleri görür, tokenlar ve sapkalar
 * klasörlerine yükler; id gönderilir. Oyuncu yalnızca kendi yüklediği resimleri görür (GM'in hazırladıkları ona
 * görünmez), kendi pulu için resim yükler ve yüklediklerini siler; şapkalar herkesin.
 * Döndürdüğü yenile() her yeni durumda çağrılır; pencere kapanınca kapandi() true olur.
 */
export function gorunumPenceresi({ karakterAl, durumAl, gonder, gm }) {
  const kap = h('div', { class: 'look' });
  /* Dosya seçici pencerenin içinde durur (pencere açıkken dışarısı tıklanamaz) ve yeniden çizimde silinmez */
  const girdi = h('input', { type: 'file', accept: gm ? 'image/png,image/jpeg,image/webp,image/gif,image/svg+xml' : 'image/png,image/jpeg,image/webp,image/gif', hidden: true });
  let yukleme = null; // { klasor, alan }
  let kapali = false;

  const sec = (alan, yol) => gonder({ t: 'gorunum', ...(gm ? { id: karakterAl().id } : {}), [alan]: yol });

  girdi.addEventListener('change', async () => {
    const f = girdi.files[0];
    girdi.value = '';
    if (!f || !yukleme) return;
    if (!(gm ? GORSEL : OYUNCU_GORSELI).test(f.name)) return bildir(gm ? 'PNG, JPG, WEBP, GIF ya da SVG seç.' : 'PNG, JPG, WEBP ya da GIF seç.');
    const { klasor, alan } = yukleme;
    try {
      const sonuc = await dosyaYukle(klasor, f, { oyuncu: !gm, uzerine: !gm });
      sec(alan, sonuc?.yol || `${klasor}/${f.name}`);
    } catch (e) {
      bildir(`${adKisalt(f.name)}: ${e.message}`);
    }
  });

  function yukle(klasor, alan) {
    yukleme = { klasor, alan };
    girdi.click();
  }

  /* Oyuncu kendi yüklediği bir resmi siler */
  async function sil(d) {
    if (!(await onay({ baslik: `${dosyaAdi(d.dosya)} silinsin mi?`, metin: 'Pulundaysa pulun resimsiz kalır.', dugme: 'Sil', tehlikeli: true }))) return;
    try {
      await istek('/api/oyuncu-sil', { yontem: 'POST', govde: { ad: dosyaAdi(d.dosya, false) } });
    } catch (e) {
      bildir(e.message);
    }
  }

  function izgara(liste, secili, alan, bosMetin) {
    const secenek = (yol, url, ad, d) =>
      h(
        'li',
        { class: 'look-item' },
        h(
          'button',
          { class: `look-opt${secili === yol ? ' is-selected' : ''}`, type: 'button', 'aria-pressed': String(secili === yol), title: ad, onclick: () => sec(alan, yol) },
          url ? h('img', { src: url, alt: '', loading: 'lazy' }) : h('span', { class: 'look-none', text: '∅' }),
          h('span', { class: 'look-name', text: ad }),
        ),
        !gm && d?.benim ? h('button', { class: 'icon-btn look-del', type: 'button', title: 'Bu resmi sil', 'aria-label': `${ad}: sil`, onclick: () => sil(d), text: '×' }) : null,
      );
    return h('ul', { class: 'look-grid' }, secenek('', null, bosMetin), ...liste.map((d) => secenek(d.dosya, d.url, dosyaAdi(d.dosya), d)));
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
        h('div', { class: 'sub-head' }, h('h3', { class: 'sub-title', text: 'Resim' }), h('button', { class: 'btn btn-sm', type: 'button', onclick: () => yukle('tokenlar', 'resim'), text: 'Resim yükle' })),
        izgara(g.resimler, k.resim_yolu || '', 'resim', 'Resimsiz'),
        h('p', {
          class: 'hint',
          text: gm
            ? g.resimler.length
              ? 'tokenlar klasöründeki bütün resimler (oyuncuların yükledikleri de: tokenlar/oyuncular).'
              : 'tokenlar klasörü boş. "Resim yükle" ile ekle.'
            : 'Burada yalnızca senin yüklediğin resimler görünür (PNG, JPG, WEBP ya da GIF; en fazla 8 MB).',
        }),
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
