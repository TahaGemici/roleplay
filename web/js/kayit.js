import { h, saat, azHareket } from './ortak.js';
import { zarSimgesi, zarEtiketi } from './zar-sekilleri.js';
import { ses } from './ses.js';

const AZAMI = 300;

/** Olay kaydı: zar sonuçları (kuralların attıkları da), sıra değişimleri, saldırılar, büyüler, ölümler, tepkiler,
 * kuralların duyuruları, eşya bildirimleri. Her satırda o kaydı kimlerin gördüğü yazar ("herkes", "sen ve GM"...). */
export function kayitOlustur(liste) {
  const altaYakin = () => liste.scrollHeight - liste.scrollTop - liste.clientHeight < 80;
  const sonaKaydir = () => {
    liste.scrollTop = liste.scrollHeight;
  };

  /* Saat ve kimlerin gördüğü */
  function zaman(g) {
    return h(
      'span',
      { class: 'entry-meta' },
      g.gor ? h('span', { class: `seen${g.gor === 'herkes' ? ' is-all' : ''}`, title: `Bu kaydı görenler: ${g.gor}`, text: g.gor }) : null,
      h('time', { class: 'entry-time', datetime: new Date(g.zaman).toISOString(), text: saat(g.zaman) }),
    );
  }

  function aralik(sonuc) {
    let en = 0;
    let ust = 0;
    for (const p of sonuc.parcalar) {
      const lo = p.zar ? p.zarlar.filter((d) => d.t).length : p.deger;
      const hi = p.zar ? lo * p.yuz : p.deger;
      if (p.isaret < 0) {
        en -= hi;
        ust -= lo;
      } else {
        en += lo;
        ust += hi;
      }
    }
    return [en, ust];
  }

  function zarKarti(g, canli) {
    const s = g.sonuc;
    const zarParcalari = s.parcalar.filter((p) => p.zar);
    const tutulanD20 = zarParcalari.length === 1 && zarParcalari[0].yuz === 20 ? zarParcalari[0].zarlar.filter((d) => d.t) : [];
    const dogal = tutulanD20.length === 1 ? tutulanD20[0].v : null;

    const zarlar = h('div', { class: 'roll-dice' });
    let sira = 0;
    s.parcalar.forEach((p, i) => {
      if (i > 0 || p.isaret < 0) zarlar.append(h('span', { class: 'roll-op', text: p.isaret < 0 ? '−' : '+' }));
      if (!p.zar) {
        zarlar.append(h('span', { class: 'roll-num', text: String(p.deger) }));
        return;
      }
      /* Simgede zarın türü (d4 için üçgende 4), yanında çıkan sayı */
      for (const d of p.zarlar) {
        const siniflar = [!d.t && 'is-dropped', d.t && d.v === p.yuz && 'is-max', d.t && d.v === 1 && 'is-min'].filter(Boolean).join(' ');
        const simge = zarSimgesi(p.yuz, zarEtiketi(p.yuz), { siniflar, elenmis: !d.t });
        simge.style.setProperty('--i', String(Math.min(sira++, 10)));
        zarlar.append(h('span', { class: `roll-die ${siniflar}`.trim(), title: `d${p.yuz}: ${d.v}${d.t ? '' : ' (sayılmadı)'}` }, simge, h('b', { class: 'roll-val', text: String(d.v) })));
      }
    });

    const toplam = h('output', { class: 'roll-total', 'aria-hidden': 'true', text: String(s.toplam) });
    const bayrak = dogal === 20 ? 'doğal 20' : dogal === 1 ? 'doğal 1' : null;
    const siniflar = ['entry', 'roll'];
    if (g.kural) siniflar.push('is-rule');
    if (dogal === 20) siniflar.push('is-crit');
    if (dogal === 1) siniflar.push('is-fumble');
    if (canli) siniflar.push('is-fresh');
    const kuralEtiketi = g.kural ? (g.baglam ? `kurallar · ${g.baglam}` : 'kurallar') : null;
    const okunan = `${g.kim}${kuralEtiketi ? ` (${kuralEtiketi})` : ''}${s.etiket ? `, ${s.etiket}` : ''}: ${s.gosterim} = ${s.toplam}${bayrak ? `, ${bayrak}` : ''}`;

    const kart = h(
      'li',
      { class: siniflar.join(' '), vars: { '--c': g.renk } },
      h(
        'div',
        { class: 'entry-head' },
        h('span', { class: 'who', text: g.kim }),
        kuralEtiketi ? h('span', { class: 'tag', text: kuralEtiketi }) : null,
        zaman(g),
      ),
      s.etiket ? h('p', { class: 'roll-label', text: s.etiket }) : null,
      h('div', { class: 'roll-body' }, zarlar, h('div', { class: 'roll-result' }, toplam, bayrak ? h('span', { class: 'roll-flag', text: bayrak }) : null)),
      h('p', { class: 'roll-notation', text: s.gosterim }),
      h('span', { class: 'sr-only', text: okunan }),
    );

    if (canli) ses.zar();
    if (canli && dogal === 20) ses.kritik(0.45); /* kartın parlamasıyla birlikte */
    if (canli && !azHareket()) {
      kart.classList.add('is-rolling');
      const [en, ust] = aralik(s);
      const bas = performance.now();
      const adim = (t) => {
        if (t - bas < 520) {
          toplam.textContent = String(en + Math.floor(Math.random() * (ust - en + 1)));
          requestAnimationFrame(adim);
        } else {
          toplam.textContent = String(s.toplam);
          setTimeout(() => kart.classList.remove('is-rolling'), 300);
        }
      };
      requestAnimationFrame(adim);
    }
    return kart;
  }

  function satir(g, sinif, etiket) {
    return h(
      'li',
      { class: `entry ${sinif}` },
      h('div', { class: 'entry-line' }, etiket ? h('span', { class: 'tag', text: etiket }) : null, h('span', { class: 'text', text: g.metin }), zaman(g)),
    );
  }

  function ciz(g, canli) {
    switch (g.tur) {
      case 'zar':
        return zarKarti(g, canli);
      case 'sira':
        return satir(g, 'turn');
      case 'kural':
        return satir(g, 'rule');
      case 'gm':
        return satir(g, 'gm-note');
      case 'ozel':
        return satir(g, 'private');
      case 'tepki':
        return satir(g, 'reaction');
      case 'esya':
        return satir(g, 'item', 'envanter');
      case 'saldiri':
        return satir(g, 'attack');
      case 'buyu':
        return satir(g, 'spell');
      case 'olum':
        return satir(g, 'death');
      default:
        return satir(g, 'info');
    }
  }

  function kirp() {
    while (liste.children.length > AZAMI) liste.firstElementChild.remove();
  }

  return {
    ekle(g, canli = true) {
      const yapis = altaYakin();
      liste.append(ciz(g, canli));
      kirp();
      if (yapis || canli) sonaKaydir();
    },
    sifirla(girdiler) {
      liste.textContent = '';
      for (const g of girdiler) liste.append(ciz(g, false));
      kirp();
      requestAnimationFrame(sonaKaydir);
    },
    sonaKaydir,
  };
}
