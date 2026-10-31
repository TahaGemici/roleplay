import { h, sinirla, sayiYaz, SIKKELER } from './ortak.js';
import { ses } from './ses.js';

const AZAMI_ISIK = 20;

/**
 * Tur kartı: kimin sırası, sıra şeridi, Fallout'taki gibi yanan enerji ışıkları, turun iki aşaması (önce yürü, sonra
 * eylem), bekleyen istek, "Yürümeden geç" ve "Turu bitir". GM için enerji düzeltme, sırayı geçme ve durdurma düğmeleri
 * de burada.
 */
export function turKartiOlustur(kok, { gonder, yolUzunlugu }) {
  let durum = null;
  let sonSiradaki = null;

  /* Işıklar kesirli dolar: 3,5 enerji = üç tam, bir yarım ışık. Enerjinin azamisi varsa o kadar ışık; yoksa sıra
     başındaki enerji kadar (enerji ondan fazlaysa enerji kadar). */
  function enerjiSatiri(statlar, toplam) {
    const enerji = statlar.enerji;
    const azami = statlar.azami?.enerji > 0 ? statlar.azami.enerji : 0;
    const n = Math.min(Math.ceil(Math.max(azami || toplam, enerji, 0) - 1e-6), AZAMI_ISIK);
    const kap = h('div', { class: 'ap-pips', role: 'img', 'aria-label': `${statYaz(statlar, 'enerji')} enerji` });
    for (let i = 0; i < n; i++) {
      const yanik = sinirla(enerji - i, 0, 1);
      kap.append(h('span', { class: `pip${yanik > 0 ? ' is-on' : ''}`, vars: { '--lit': String(yanik) } }));
    }
    return h('div', { class: 'ap' }, h('span', { class: 'ap-label', text: 'Enerji' }), kap, h('span', { class: 'ap-num', text: statYaz(statlar, 'enerji') }));
  }

  /* Turun aşamaları: 1 Yürü, 2 Eylem (saldırı ya da büyü); biten aşama ✓ olur. */
  function asamalar(asama) {
    const eylemde = asama === 'eylem';
    const adim = (no, ad, simdiki, bitti) =>
      h(
        'li',
        { class: `turn-step${simdiki ? ' is-current' : ''}${bitti ? ' is-done' : ''}`, 'aria-current': simdiki ? 'step' : null },
        h('span', { class: 'step-num', 'aria-hidden': 'true', text: bitti ? '✓' : String(no) }),
        h('span', { class: 'step-name', text: ad }),
      );
    return h('ol', { class: 'turn-steps', 'aria-label': 'Turun aşamaları' }, adim(1, 'Yürü', !eylemde, eylemde), adim(2, 'Eylem', eylemde, false));
  }

  function bekleyenIstek(ist) {
    if (!ist) return null;
    const L = ist.tur === 'hareket' ? yolUzunlugu?.(ist) : null;
    const metin =
      ist.tur === 'hareket'
        ? `Yürüme isteği${L !== null && L !== undefined ? ` · ${sayiYaz(L)} kare` : ''}`
        : ist.tur === 'buyu'
          ? `Büyü: ${ist.hedef === durum.ben.id ? 'kendine' : ist.hedef_ad} (${ist.buyu})`
          : `Saldırı: ${ist.hedef_ad} (${ist.esya?.ad || 'eşya'})`;
    return h(
      'div',
      { class: 'pending' },
      h('span', { class: 'pending-dot', 'aria-hidden': 'true' }),
      h('span', { class: 'pending-text' }, h('b', { text: 'GM onayı bekleniyor' }), h('span', { text: metin })),
      h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => gonder({ t: 'istek_iptal' }), text: 'Vazgeç' }),
    );
  }

  function oyuncuSeridi(s, ben) {
    const serit = h('ol', { class: 'order-strip', 'aria-label': 'Tur sırası' });
    for (const o of s.liste) {
      const ad = o.ad ?? '?';
      const sinif = ['order-chip', o.simdiki && 'is-current', o.olu && 'is-dead', !o.cevrimici && 'is-offline', o.ad === null && 'is-unknown'].filter(Boolean).join(' ');
      serit.append(h('li', { class: sinif, title: o.ad === null ? 'Görmediğin biri' : o.olu ? `${ad} (ölü)` : ad, text: o.ben ? `${ad} (sen)` : ad }));
    }
    return serit;
  }

  function gmSeridi(s) {
    const serit = h('ol', { class: 'order-strip', 'aria-label': 'Tur sırası' });
    s.liste.forEach((id, i) => {
      const k = durum.karakterler.find((x) => x.id === id);
      if (!k) return;
      const sinif = ['order-chip', i === s.indeks && 'is-current', k.olu && 'is-dead', !k.npc && !k.cevrimici && 'is-offline'].filter(Boolean).join(' ');
      serit.append(h('li', { class: sinif, title: k.olu ? `${k.ad} (ölü)` : k.ad, text: k.npc ? `${k.ad} ·NPC` : k.ad }));
    });
    return serit;
  }

  function ciz() {
    if (!durum) return;
    const s = durum.sira;
    const gm = durum.ben.gm;
    kok.textContent = '';
    kok.classList.remove('is-mine');

    if (!s.aktif) {
      kok.append(
        h('div', { class: 'turn-head' }, h('span', { class: 'turn-label', text: 'Sıra kimsede değil' })),
        h('p', {
          class: 'turn-free',
          text: gm
            ? 'Turlar başlamadı: kimse yürüyemez, saldıramaz. Haritayı aç, oyuncuları soldaki listeden haritaya sürükleyerek yerleştir ve turları başlat.'
            : durum.olu
              ? 'Öldün. GM seni diriltebilir.'
              : !durum.ben.yerde
                ? 'Henüz haritada değilsin. GM seni haritaya yerleştirecek.'
                : 'Turlar başlamadı. GM turları başlatınca sıran geldiğinde yürüyebilir, saldırabilir, büyü yapabilirsin.',
        }),
      );
      return;
    }

    if (gm) {
      const k = durum.karakterler.find((x) => x.id === s.sirada);
      kok.append(
        h('div', { class: 'turn-head' }, h('span', { class: 'turn-label', text: 'Sıradaki' }), h('span', { class: 'turn-round', text: `Tur ${s.tur}` })),
        h('p', { class: 'turn-who', vars: { '--c': k?.renk || 'var(--bone)' }, text: k ? (k.npc ? `${k.ad} (NPC)` : k.ad) : '—' }),
        gmSeridi(s),
      );
      if (k) kok.append(enerjiSatiri(k.statlar, s.toplam_enerji), asamalar(s.asama));
      kok.append(
        h(
          'div',
          { class: 'gm-turn-tools' },
          k
            ? h('button', { class: 'icon-btn', type: 'button', title: '1 enerji eksilt', 'aria-label': '1 enerji eksilt', onclick: () => gonder({ t: 'karakter_guncelle', id: k.id, enerji: Math.max(0, k.statlar.enerji - 1) }), text: '−' })
            : null,
          k
            ? h('button', { class: 'icon-btn', type: 'button', title: '1 enerji ekle', 'aria-label': '1 enerji ekle', onclick: () => gonder({ t: 'karakter_guncelle', id: k.id, enerji: k.statlar.enerji + 1 }), text: '+' })
            : null,
          h('span', { class: 'spacer' }),
          k?.harita && !k.burada ? h('button', { class: 'btn btn-sm btn-ghost', type: 'button', title: `${k.ad} başka bir haritada`, onclick: () => gonder({ t: 'harita_ac', dosya: k.harita }), text: 'Haritasına git' }) : null,
          k?.npc && s.asama !== 'eylem'
            ? h('button', { class: 'btn btn-sm btn-ghost', type: 'button', title: 'NPC bu tur yürümez; eylem aşaması başlar', onclick: () => gonder({ t: 'yurumeden_gec' }), text: 'Yürümeden geç' })
            : null,
          h('button', { class: 'btn btn-sm', type: 'button', title: 'Eylem yapmadan sırasını bitirir', onclick: () => gonder({ t: 'tur_bitir' }), text: 'Sıradakine geç' }),
          h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => gonder({ t: 'sira_durdur' }), text: 'Durdur' }),
        ),
      );
      if (k?.npc)
        kok.append(
          h('p', {
            class: 'hint turn-hint',
            text:
              s.asama !== 'eylem'
                ? 'NPC\'nin sırası: önce onu sürükleyerek yürüt ya da "Yürümeden geç". Sonra Karakterler bölümünden saldırt ya da büyü yaptır.'
                : 'NPC yürüdü: Karakterler bölümünden saldırt ya da büyü yaptır (olunca sırası biter) ya da "Sıradakine geç".',
          }),
        );
      return;
    }

    const benim = s.sirada_ben;
    kok.classList.toggle('is-mine', benim);
    const ad = benim ? durum.ben.ad : s.sirada ? s.sirada.ad ?? 'Görmediğin biri' : '—';
    kok.append(
      h('div', { class: 'turn-head' }, h('span', { class: 'turn-label', text: benim ? 'Sıra sende' : 'Sıradaki' }), h('span', { class: 'turn-round', text: `Tur ${s.tur}` })),
      h('p', { class: 'turn-who', vars: { '--c': (benim ? durum.ben.renk : s.sirada?.renk) || 'var(--bone)' }, text: ad }),
      oyuncuSeridi(s, durum.ben),
    );
    if (benim) {
      const eylemde = s.asama === 'eylem';
      kok.append(enerjiSatiri(durum.statlar, s.toplam_enerji), asamalar(s.asama));
      const b = bekleyenIstek(durum.istek);
      if (b) kok.append(b);
      else
        kok.append(
          h('p', {
            class: 'hint turn-hint',
            text: eylemde
              ? 'Şimdi eylem: saldırmak ya da büyü yapmak için bir karaktere (kendine büyü için kendine) tıkla. Olunca turun biter; eylem yapmayacaksan turu bitir.'
              : 'Önce yürü: haritada bir yere tıkla ya da yürümeden geç. Sonra saldırmak ya da büyü yapmak için bir karaktere tıkla. İsteklerin GM\'e gider.',
          }),
        );
      kok.append(
        h(
          'div',
          { class: 'turn-actions' },
          eylemde ? null : h('button', { class: 'btn skip-walk', type: 'button', title: 'Bu tur yürümezsin; eylem aşaması başlar', onclick: () => gonder({ t: 'yurumeden_gec' }), text: 'Yürümeden geç' }),
          h('button', { class: 'btn btn-primary end-turn', type: 'button', title: 'Eylem yapmadan turunu bitirirsin', onclick: () => gonder({ t: 'tur_bitir' }), text: 'Turu bitir' }),
        ),
      );
    }
  }

  return {
    ayarla(yeni) {
      durum = yeni;
      const s = yeni.sira;
      const benim = s.aktif && !yeni.ben.gm && s.sirada_ben;
      const simdiki = s.aktif ? `${s.tur}:${yeni.ben.gm ? s.sirada : benim ? 'ben' : s.sirada?.ad}` : null;
      const yeniSira = Boolean(simdiki) && simdiki !== sonSiradaki;
      sonSiradaki = simdiki;
      ciz();
      if (yeniSira && benim) {
        ses.sira();
        kok.classList.remove('is-fresh');
        void kok.offsetWidth;
        kok.classList.add('is-fresh');
      }
      return { siraBende: benim, yeniSira };
    },
  };
}

/* GM'in düzenleyebildiği statlar (her birinin azamisi de var). Ağırlık (70 + eşyalar) sunucudan gelir; yalnızca
   gösterilir, azamisi yoktur. */
const STATLAR = [
  ['can', 'Can'],
  ['odak', 'Odak'],
  ['zirh', 'Zırh'],
  ['buyu_direnci', 'Büyü direnci'],
  ['enerji', 'Enerji'],
];

/* "22/35": azamisi olan stat (azami 0: azamisi yok, yalnızca değer) */
export const statYaz = (statlar, k) => {
  const az = statlar?.azami?.[k];
  return az > 0 ? `${sayiYaz(statlar[k])}/${sayiYaz(az)}` : sayiYaz(statlar?.[k]);
};

/** Oyuncunun kendi statları (başkalarınınkini yalnızca GM görür): azamisi olanlar "22/35" diye; para, statlar gibi
 *  altın, gümüş ve bakır ayrı ayrı. */
export function durumKartiCiz(kok, durum) {
  const st = durum.statlar;
  const para = durum.ben.para || {};
  kok.classList.toggle('is-dead', Boolean(durum.olu));
  kok.replaceChildren(
    h(
      'div',
      { class: 'stat-head' },
      h('span', { class: 'stat-name', vars: { '--c': durum.ben.renk }, text: durum.ben.ad }),
      durum.olu ? h('span', { class: 'tag tag-dead', text: 'öldün' }) : !durum.ben.yerde ? h('span', { class: 'tag', text: 'haritada değil' }) : null,
      h('button', { class: 'btn btn-sm btn-ghost stat-look', type: 'button', dataset: { gorunum: '1' }, title: 'Pulunun resmi ve şapkası', text: 'Görünüm' }),
    ),
    h(
      'dl',
      { class: 'stats' },
      ...STATLAR.map(([k, ad]) => h('div', { class: `stat stat-${k}` }, h('dt', { text: ad }), h('dd', { text: statYaz(st, k) }))),
      h('div', { class: 'stat stat-agirlik' }, h('dt', { text: 'Ağırlık' }), h('dd', { text: sayiYaz(st.agirlik) })),
      ...SIKKELER.map(([k, ad]) => h('div', { class: `stat stat-para stat-${k}` }, h('dt', { text: ad.charAt(0).toLocaleUpperCase('tr') + ad.slice(1) }), h('dd', { text: String(para[k] ?? 0) }))),
    ),
  );
}

export { STATLAR };
