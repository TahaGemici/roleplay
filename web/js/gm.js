import { $, h, throttle, debounce, bildir, dosyaYukle, dosyaAdi, sayiYaz, adKisalt, yolKodla, paraParcala, paraBirlestir } from './ortak.js';
import { onay, formAc, goster } from './pencere.js';
import { esyaKarti, esyaOnizle, grupla, agirlikYaz } from './envanter.js';
import { STATLAR } from './tur.js';

const NPC_RENKLERI = ['#a8402f', '#4f5a63', '#7a5b3c', '#2f6b4f', '#6b3f7a', '#8a6a2f', '#6fa8dc', '#e3798b', '#8cc084', '#d9d2c3'];
const GORSEL = /\.(png|jpe?g|gif|webp)$/i;
const SURUKLEME_PAYI = 6; // bu kadar pikselden az kayan basış tıklamadır

function depoOku(ad, varsayilan) {
  try {
    const v = localStorage.getItem(ad);
    return v === null ? varsayilan : v === '1';
  } catch {
    return varsayilan;
  }
}
function depoYaz(ad, deger) {
  try {
    localStorage.setItem(ad, deger ? '1' : '0');
  } catch {
    /* önemsiz */
  }
}

/** GM paneli: harita, görüş, ızgara, tur sırası, karakterler (yerleştirme, statlar, envanter, görünüm), eşyalar,
 * ayarlar ve istekler. Karakterler ve eşyalar listeden haritaya sürüklenip bırakılır. */
export function gmPaneliOlustur({ gonder, harita, gorunumAc }) {
  let durum = null;
  let secili = null;
  let bekleyenNpc = null;
  let bekleyenOdak = null; // başka haritadaki bir karaktere gidiliyor: harita açılınca ona odaklan
  let bekleyenPortal = null; // yeni konan portalın düzenleyicisi açılacak
  const karakter = (id) => durum?.karakterler.find((k) => k.id === id) || null;

  /* Liste yalnızca gösterdiği şey değişince yeniden çizilir; araya giren bir durum, tıklanmakta olan düğmeyi
     silip tıklamayı yutmasın */
  const imzalar = new WeakMap();
  function degisince(kap, imza, ciz) {
    const s = JSON.stringify(imza);
    if (imzalar.get(kap) === s) return;
    imzalar.set(kap, s);
    ciz();
  }

  /* Bir karakterin haritasına geç ve ona odaklan */
  function haritasinaGit(k) {
    if (!k?.harita) return;
    if (k.burada) return harita.odakla(k.id);
    bekleyenOdak = k.id;
    gonder({ t: 'harita_ac', dosya: k.harita });
  }

  // ------------------------------------------------------------ listeden haritaya sürükle-bırak

  const masa = $('#masa');
  const haritaUstunde = (e) => Boolean(document.elementFromPoint(e.clientX, e.clientY)?.closest('#gorus'));

  /* Haritaya bırakılan: karakter yerleşir (ya da oraya yürür); eşya oradaki karaktere gider ya da yere konur */
  function birak(veri, cx, cy) {
    const yer = harita.kareKonumu(cx, cy);
    if (!yer || !durum.harita) return bildir('Önce bir harita aç.');
    if (veri.tur === 'karakter') gonder({ t: 'yerlestir', id: veri.id, x: yer.x, y: yer.y });
    else if (veri.tur === 'portal') gonder({ t: 'portal_koy', x: yer.x, y: yer.y });
    else if (veri.tur === 'katalog') gonder({ t: 'esya_birak', kaynak: 'katalog', esya: veri.esya, x: yer.x, y: yer.y });
    else if (veri.tur === 'envanter') gonder({ t: 'esya_birak', kaynak: 'envanter', karakter: veri.karakter, sira: veri.sira, ad: veri.ad, x: yer.x, y: yer.y });
  }

  /* Öğeyi haritaya sürüklenebilir yapar (fare, kalem ve dokunma; HTML5 sürükle-bırak düğmelerde her tarayıcıda çalışmaz).
     Sürükleme öğenin kendi üstünde biterse arkasından gelen tıklama yutulur. */
  function suruklenebilir(el, veri, etiket) {
    let yut = false;
    el.classList.add('is-draggable');
    el.addEventListener(
      'click',
      (e) => {
        if (!yut) return;
        yut = false;
        e.stopImmediatePropagation();
        e.preventDefault();
      },
      true,
    );
    el.addEventListener('pointerdown', (e) => {
      if (e.button !== 0 || !durum?.harita) return;
      e.preventDefault(); /* sürüklerken yazı seçilmesin */
      const bas = { x: e.clientX, y: e.clientY };
      let hayalet = null;
      const hareket = (ev) => {
        if (!hayalet) {
          if (Math.hypot(ev.clientX - bas.x, ev.clientY - bas.y) < SURUKLEME_PAYI) return;
          hayalet = h('div', { class: 'drag-ghost', text: etiket });
          document.body.append(hayalet);
          el.classList.add('is-dragging');
        }
        hayalet.style.transform = `translate(${ev.clientX + 12}px, ${ev.clientY + 10}px)`;
        masa.classList.toggle('is-drop', haritaUstunde(ev));
      };
      const bitir = (ev) => {
        window.removeEventListener('pointermove', hareket);
        window.removeEventListener('pointerup', bitir);
        window.removeEventListener('pointercancel', bitir);
        if (!hayalet) return;
        hayalet.remove();
        el.classList.remove('is-dragging');
        masa.classList.remove('is-drop');
        if (ev.type === 'pointerup' && el.contains(document.elementFromPoint(ev.clientX, ev.clientY))) {
          /* tıklama aynı görevde gelir; gelmezse bayrak sonrakini yutmasın */
          yut = true;
          setTimeout(() => {
            yut = false;
          });
        }
        if (ev.type === 'pointerup' && haritaUstunde(ev)) birak(veri, ev.clientX, ev.clientY);
      };
      window.addEventListener('pointermove', hareket);
      window.addEventListener('pointerup', bitir);
      window.addEventListener('pointercancel', bitir);
    });
    return el;
  }

  // ------------------------------------------------------------ harita

  function haritalariCiz() {
    const kap = $('#haritalar');
    const aktif = durum.harita?.dosya;
    const imza = [durum.haritalar.map((m) => [m.dosya, m.url, m.genislik]), aktif, durum.harita?.gizli, durum.karakterler.filter((k) => !k.npc).map((k) => [k.ad, k.harita])];
    degisince(kap, imza, () => haritaListesi(kap, aktif));
  }

  function haritaListesi(kap, aktif) {
    if (!durum.haritalar.length) {
      kap.replaceChildren(h('li', { class: 'hint', text: 'haritalar klasörü boş. Bir harita görseli yükle.' }));
      return;
    }
    /* Her haritada kimler var: oyuncular kendi haritalarındadır; GM'in baktığı harita onları taşımaz */
    kap.replaceChildren(
      ...durum.haritalar.map((m) => {
        const burada = durum.karakterler.filter((k) => !k.npc && k.harita === m.dosya).map((k) => k.ad);
        const meta = m.dosya === aktif && durum.harita.gizli ? 'Bakıyorsun · oyunculardan gizli' : burada.length ? `Burada: ${burada.join(', ')}` : 'Oyuncu yok';
        return h(
          'li',
          {},
          h(
            'button',
            { class: 'lib-open', type: 'button', 'aria-current': m.dosya === aktif ? 'true' : null, title: m.dosya, onclick: () => haritaAc(m) },
            h('img', { class: 'lib-thumb', src: m.url, alt: '', loading: 'lazy', decoding: 'async' }),
            h('span', { class: 'lib-text' }, h('span', { class: 'lib-name', text: dosyaAdi(m.dosya) }), h('span', { class: 'lib-meta', text: meta })),
          ),
        );
      }),
    );
  }

  async function haritaYukle(dosyalar) {
    let ilk = null;
    for (const f of dosyalar) {
      if (!GORSEL.test(f.name)) {
        bildir(`${adKisalt(f.name)}: harita PNG, JPG, WEBP ya da GIF olmalı.`);
        continue;
      }
      try {
        await dosyaYukle('haritalar', f);
        ilk = ilk || f.name;
      } catch (e) {
        bildir(`${adKisalt(f.name)}: ${e.message}`);
      }
    }
    if (ilk) {
      bildir(`${adKisalt(ilk)} haritalar klasörüne yüklendi.`, 'ok');
      if (!durum.harita) haritaAc({ dosya: ilk, url: `/oyun/haritalar/${yolKodla(ilk)}` });
    }
  }

  // Haritayı açar. İlk kez açılıyorsa önce boyutunu (kaç kare olduğunu) sorar.
  async function haritaAc(m) {
    if (m.genislik) return gonder({ t: 'harita_ac', dosya: m.dosya });
    const boyut = await boyutSor(m.url, dosyaAdi(m.dosya));
    if (boyut) gonder({ t: 'harita_ac', dosya: m.dosya, ...boyut });
  }

  // Önerilen boyut: resimde 70 piksel bir kare
  async function boyutSor(url, ad) {
    let gen = 0;
    let yuk = 0;
    try {
      const r = new Image();
      r.src = url;
      await r.decode();
      gen = r.naturalWidth;
      yuk = r.naturalHeight;
    } catch {
      /* resim okunamazsa öneri 20 × 15 */
    }
    const oneri = (piksel, yedek) => (piksel ? Math.max(1, Math.round(piksel / 70)) : yedek);
    const sonuc = await formAc({
      baslik: `${ad}: harita boyutu`,
      metin: `${gen ? `Resim ${gen} × ${yuk} piksel. ` : ''}Harita kaç kareye bölünsün? Resimde çizili kareler varsa onların sayısını yaz. Konumlar, görüş ve yürüyüş bu karelerle ölçülür.`,
      alanlar: [
        { ad: 'genislik', etiket: 'Genişlik (kare)', tur: 'sayi', deger: oneri(gen, 20), en: 1, ust: 1000 },
        { ad: 'yukseklik', etiket: 'Yükseklik (kare)', tur: 'sayi', deger: oneri(yuk, 15), en: 1, ust: 1000 },
      ],
      dugme: 'Haritayı aç',
    });
    if (!sonuc) return null;
    const genislik = Number(sonuc.genislik);
    const yukseklik = Number(sonuc.yukseklik);
    if (!(genislik >= 1 && yukseklik >= 1)) {
      bildir('Genişlik ve yükseklik en az 1 kare olmalı.');
      return null;
    }
    return { genislik, yukseklik };
  }

  function dosyaSec(kabul, isle) {
    const girdi = $('#dosya-sec');
    girdi.accept = kabul;
    girdi.onchange = () => {
      const f = [...girdi.files];
      girdi.value = '';
      isle(f);
    };
    girdi.click();
  }

  $('#harita-yukle').addEventListener('click', () => dosyaSec('image/png,image/jpeg,image/webp,image/gif', haritaYukle));
  $('#harita-kapat').addEventListener('click', () => gonder({ t: 'harita_ac', dosya: '' }));
  $('#harita-gizli').addEventListener('change', (e) => gonder({ t: 'harita_gizle', gizli: e.target.checked }));
  for (const [id, alan] of [['#iz-sis', 'sis'], ['#iz-oturt', 'oturt'], ['#iz-gorunur', 'gorunur'], ['#iz-acik', 'acik']]) {
    $(id).addEventListener('change', (e) => gonder({ t: 'izgara', [alan]: e.target.checked }));
  }
  const halkalar = $('#halkalar');
  halkalar.checked = depoOku('roleplay.halkalar', true);
  harita.halkalariGoster(halkalar.checked);
  halkalar.addEventListener('change', () => {
    depoYaz('roleplay.halkalar', halkalar.checked);
    harita.halkalariGoster(halkalar.checked);
  });
  $('#kesif-sifirla').addEventListener('click', async () => {
    const evet = await onay({
      baslik: 'Keşif sıfırlansın mı?',
      metin: 'Bu haritada oyuncuların gördüğü yerler ve hatırladıkları karakterler unutulur. Herkes yalnızca şu an görüş menzilindekini görür.',
      dugme: 'Sıfırla',
      tehlikeli: true,
    });
    if (evet) gonder({ t: 'kesif_sifirla' });
  });

  // Haritanın üstüne bırakılan görseller haritalar klasörüne yüklenir
  const ipucu = $('#birak');
  let derinlik = 0;
  const dosyaVar = (e) => [...(e.dataTransfer?.types || [])].includes('Files');
  masa.addEventListener('dragenter', (e) => {
    if (!dosyaVar(e)) return;
    e.preventDefault();
    derinlik++;
    ipucu.hidden = false;
  });
  masa.addEventListener('dragover', (e) => {
    if (dosyaVar(e)) e.preventDefault();
  });
  masa.addEventListener('dragleave', (e) => {
    if (!dosyaVar(e)) return;
    derinlik = Math.max(0, derinlik - 1);
    if (!derinlik) ipucu.hidden = true;
  });
  masa.addEventListener('drop', (e) => {
    if (!dosyaVar(e)) return;
    e.preventDefault();
    derinlik = 0;
    ipucu.hidden = true;
    haritaYukle([...e.dataTransfer.files]);
  });

  // ------------------------------------------------------------ ızgara: harita boyutu ve çizgiler

  const izGen = $('#iz-gen');
  const izYuk = $('#iz-yuk');
  const izOp = $('#iz-op');
  const opGonder = throttle((opaklik) => gonder({ t: 'izgara', opaklik }), 120);
  izOp.addEventListener('input', () => {
    $('#iz-op-o').textContent = `%${Math.round(izOp.value * 100)}`;
    opGonder(Number(izOp.value));
  });
  for (const el of [izGen, izYuk]) {
    el.addEventListener('change', () => {
      const genislik = Number(izGen.value);
      const yukseklik = Number(izYuk.value);
      if (genislik >= 1 && yukseklik >= 1) gonder({ t: 'izgara', genislik, yukseklik });
    });
  }

  function izgaraCiz() {
    const hr = durum.harita;
    $('#izgara-ayar').hidden = !hr;
    $('#gorus-ayar').hidden = !hr;
    if (!hr) return;
    const g = hr.izgara;
    $('#iz-sis').checked = g.sis;
    $('#iz-oturt').checked = g.oturt;
    $('#iz-gorunur').checked = g.gorunur;
    $('#iz-acik').checked = g.acik;
    $('#kesif-sifirla').disabled = !g.sis;
    if (document.activeElement !== izGen) izGen.value = String(hr.genislik);
    if (document.activeElement !== izYuk) izYuk.value = String(hr.yukseklik);
    if (document.activeElement !== izOp) {
      izOp.value = String(g.opaklik);
      $('#iz-op-o').textContent = `%${Math.round(g.opaklik * 100)}`;
    }
  }

  // ------------------------------------------------------------ tur sırası

  function siraCiz() {
    const s = durum.sira;
    const liste = s.liste.filter((id) => karakter(id));
    const kap = $('#sira-listesi');
    const imza = [liste.map((id) => [id, karakter(id).ad, karakter(id).renk, karakter(id).olu, karakter(id).cevrimici]), s.aktif, s.sirada];
    degisince(kap, imza, () => siraListesi(s, liste, kap));
    const baslat = $('#sira-baslat');
    baslat.textContent = s.aktif ? 'Turları durdur' : 'Turları başlat';
    baslat.className = s.aktif ? 'btn' : 'btn btn-primary';
  }

  function siraListesi(s, liste, kap) {
    if (!liste.length) {
      kap.replaceChildren(h('li', { class: 'hint', text: 'Henüz kimse yok. Oyuncular adres ve şifreyle girince burada sıralanır.' }));
    } else {
      kap.replaceChildren(
        ...liste.map((id, i) => {
          const k = karakter(id);
          const tasi = (yon) => {
            const yeni = [...liste];
            const j = i + yon;
            [yeni[i], yeni[j]] = [yeni[j], yeni[i]];
            gonder({ t: 'sira_duzenle', liste: yeni });
          };
          const meta = k.olu ? 'ölü' : !k.npc && !k.cevrimici ? 'bağlı değil' : k.npc ? 'NPC' : null;
          return h(
            'li',
            { class: `order-row${s.aktif && s.sirada === id ? ' is-current' : ''}${!k.npc && !k.cevrimici ? ' is-offline' : ''}${k.olu ? ' is-dead' : ''}`, vars: { '--c': k.renk } },
            h('span', { class: 'order-num', text: String(i + 1) }),
            h('span', { class: `dot${k.npc ? ' is-npc' : ''}` }),
            h('span', { class: 'row-name', text: k.ad }),
            meta ? h('span', { class: 'row-meta', text: meta }) : null,
            h('button', { class: 'icon-btn', type: 'button', 'aria-label': `${k.ad} yukarı`, disabled: i === 0, onclick: () => tasi(-1), text: '↑' }),
            h('button', { class: 'icon-btn', type: 'button', 'aria-label': `${k.ad} aşağı`, disabled: i === liste.length - 1, onclick: () => tasi(1), text: '↓' }),
            k.npc ? h('button', { class: 'icon-btn', type: 'button', 'aria-label': `${k.ad} sıradan çıkar`, title: 'Sıradan çıkar', onclick: () => gonder({ t: 'siradan_cikar', id }), text: '×' }) : null,
          );
        }),
      );
    }
  }
  $('#sira-zarla').addEventListener('click', () => gonder({ t: 'sira_zarla' }));
  $('#sira-baslat').addEventListener('click', () => gonder({ t: durum.sira.aktif ? 'sira_durdur' : 'sira_baslat' }));

  // ------------------------------------------------------------ karakterler

  function sec(id) {
    secili = id;
    harita.sec(id, true);
    karakterleriCiz();
  }

  /* Karakterler üç grupta: GM'in baktığı haritadakiler, başka haritadakiler, hiçbir haritada olmayanlar.
     Hepsi listeden baktığın haritaya sürüklenip bırakılabilir. */
  function karakterleriCiz() {
    const liste = durum.karakterler;
    if (secili && !karakter(secili)) secili = null;
    const kap = $('#karakter-listesi');
    const imza = [liste.map((k) => [k.id, k.ad, k.renk, k.npc, k.olu, k.gizli, k.harita, k.burada, k.cevrimici, k.statlar.can, k.statlar.enerji]), secili, Boolean(durum.harita)];
    degisince(kap, imza, () => karakterListesi(liste, kap));
    const bekleyen = liste.filter((k) => !k.npc && !k.harita).length;
    $('#yerlestir-ipucu').hidden = !bekleyen;
    $('#yerlestir-ipucu').textContent = bekleyen
      ? `${bekleyen} oyuncu hiçbir haritada değil. Onları listeden haritaya sürükleyip bırakarak yerleştir; yerleşmeyen oyuncunun sırası geçilir.`
      : '';
    $('#npc-ekle').disabled = !durum.harita;
    editorCiz();
    katalogCiz();
  }

  function karakterListesi(liste, kap) {
    const satir = (k) => {
      const meta = k.olu
        ? h('span', { class: 'row-meta is-dead', text: 'ölü' })
        : !k.harita
          ? h('span', { class: 'row-meta is-unplaced', text: 'haritada değil' })
          : !k.burada
            ? h('span', { class: 'row-meta is-elsewhere', text: dosyaAdi(k.harita) })
            : !k.npc && !k.cevrimici
              ? h('span', { class: 'row-meta', text: 'bağlı değil' })
              : null;
      return h(
        'li',
        {},
        suruklenebilir(
          h(
            'button',
            {
              class: `char-row${secili === k.id ? ' is-selected' : ''}${k.olu ? ' is-dead' : ''}${!k.harita ? ' is-unplaced' : ''}`,
              type: 'button',
              vars: { '--c': k.renk },
              title: durum.harita ? `${k.ad}: haritaya sürükleyip bırakarak ${k.burada ? 'taşı' : 'buraya getir'}` : k.ad,
              onclick: () => {
                sec(k.id);
                if (k.burada) harita.odakla(k.id);
              },
            },
            h('span', { class: `dot${k.npc ? ' is-npc' : ''}` }),
            h('span', { class: 'row-name', text: k.ad }),
            k.gizli ? h('span', { class: 'row-meta', text: 'gizli' }) : null,
            meta,
            h('span', { class: 'mini-stats', text: `${sayiYaz(k.statlar.can)} can · ${sayiYaz(k.statlar.enerji)} en.` }),
          ),
          { tur: 'karakter', id: k.id },
          k.ad,
        ),
      );
    };
    const gruplar = [
      ['Bu haritada', liste.filter((k) => k.burada)],
      ['Başka haritalarda', liste.filter((k) => k.harita && !k.burada)],
      ['Hiçbir haritada değil', liste.filter((k) => !k.harita)],
    ].filter(([, g]) => g.length);
    if (!liste.length) kap.replaceChildren(h('li', { class: 'hint', text: 'Henüz karakter yok.' }));
    else kap.replaceChildren(...gruplar.flatMap(([ad, g]) => [h('li', { class: 'list-group', text: ad }), ...g.map(satir)]));
  }

  // Seçili karakterin düzenleyicisi
  const kdAd = $('#kd-ad');
  const adGuncelle = debounce((id, ad) => gonder({ t: 'karakter_guncelle', id, ad }), 300);
  kdAd.addEventListener('input', () => {
    if (secili && kdAd.value.trim()) adGuncelle(secili, kdAd.value.trim());
  });
  kdAd.addEventListener('keydown', (e) => {
    if (e.key === 'Enter') kdAd.blur();
  });
  $('#kd-gizli').addEventListener('change', (e) => secili && gonder({ t: 'karakter_guncelle', id: secili, gizli: e.target.checked }));
  $('#kd-renkler').replaceChildren(
    ...NPC_RENKLERI.map((renk) =>
      h('button', { class: 'swatch', type: 'button', role: 'radio', 'aria-checked': 'false', 'aria-label': renk, vars: { '--c': renk }, dataset: { renk }, onclick: () => secili && gonder({ t: 'karakter_guncelle', id: secili, renk }) }),
    ),
  );
  const statGirdileri = {};
  $('#kd-statlar').replaceChildren(
    ...STATLAR.map(([k, ad]) => {
      const girdi = h('input', { class: 'input', type: 'number', step: '0.5', inputmode: 'decimal', 'aria-label': ad });
      girdi.addEventListener('change', () => {
        const v = Number(girdi.value);
        if (!secili || girdi.value === '' || !Number.isFinite(v)) return;
        gonder({ t: 'karakter_guncelle', id: secili, [k]: v });
      });
      girdi.addEventListener('keydown', (e) => {
        if (e.key === 'Enter') girdi.blur();
      });
      statGirdileri[k] = girdi;
      return h('label', { class: `stat-input stat-${k}` }, h('span', { text: ad }), girdi);
    }),
  );

  function editorCiz() {
    const k = karakter(secili);
    $('#karakter-duzen').hidden = !k;
    if (!k) return;
    $('#kd-nokta').style.setProperty('--c', k.renk);
    $('#kd-nokta').classList.toggle('is-npc', k.npc);
    $('#kd-baslik').textContent = k.ad;
    const etiketler = [k.npc ? 'NPC' : 'oyuncu', k.olu && 'ölü', k.gizli && 'gizli', !k.npc && !k.cevrimici && 'bağlı değil'].filter(Boolean);
    $('#kd-etiket').textContent = etiketler.join(' · ');
    $('#kd-harita').textContent = k.harita ? (k.burada ? 'Baktığın haritada' : `Haritası: ${dosyaAdi(k.harita)}`) : 'Hiçbir haritada değil (listeden haritaya sürükle)';
    const p = paraParcala(k.para);
    for (const [ad, deger] of [['altin', p.altin], ['gumus', p.gumus], ['bakir', p.bakir]]) {
      const girdi = $(`#kd-para-${ad}`);
      if (document.activeElement !== girdi) girdi.value = String(deger);
    }
    $('#kd-npc').hidden = !k.npc;
    if (k.npc) {
      if (document.activeElement !== kdAd) kdAd.value = k.ad;
      for (const sw of $('#kd-renkler').children) sw.setAttribute('aria-checked', String(sw.dataset.renk === k.renk));
      $('#kd-gizli').checked = k.gizli;
    }
    for (const [ad, girdi] of Object.entries(statGirdileri)) {
      if (document.activeElement !== girdi) girdi.value = String(Math.round(k.statlar[ad] * 100) / 100);
    }
    $('#kd-agirlik').textContent = sayiYaz(k.statlar.agirlik);
    degisince($('#kd-islemler'), [k.id, k.ad, k.olu, k.harita, k.burada, k.npc, k.sirada, durum.sira.aktif, durum.sira.sirada, durum.buyuler.length], () => {
      const islem = [];
      islem.push(
        k.olu
          ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gonder({ t: 'dirilt', id: k.id }), text: 'Dirilt' })
          : h('button', { class: 'btn btn-sm btn-danger', type: 'button', onclick: () => gonder({ t: 'oldur', id: k.id }), text: 'Öldür' }),
      );
      islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gorunumAc(k.id), text: 'Görünüm…' }));
      if (k.harita && !k.burada) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => haritasinaGit(k), text: 'Haritasına git' }));
      if (k.burada && !k.olu && durum.sira.aktif && durum.sira.sirada === k.id) {
        islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => saldirt(k), text: 'Saldırt…' }));
        if (durum.buyuler.length) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => buyuYaptir(k), text: 'Büyü yaptır…' }));
      }
      if (k.npc && k.burada && !k.olu) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => takasYaptir(k), text: 'Takas…' }));
      if (!k.npc && k.harita) {
        islem.push(h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => gonder({ t: 'haritadan_kaldir', id: k.id }), text: 'Haritadan kaldır' }));
      }
      if (k.npc) {
        islem.push(
          k.sirada
            ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gonder({ t: 'siradan_cikar', id: k.id }), text: 'Sıradan çıkar' })
            : h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gonder({ t: 'siraya_ekle', id: k.id }), text: 'Sıraya ekle' }),
        );
        islem.push(
          h('button', {
            class: 'btn btn-sm btn-ghost',
            type: 'button',
            text: 'NPC\'yi sil',
            onclick: async () => {
              if (await onay({ baslik: `${k.ad} silinsin mi?`, metin: 'Statları ve envanteri de silinir.', dugme: 'Sil', tehlikeli: true })) gonder({ t: 'npc_sil', id: k.id });
            },
          }),
        );
      }
      $('#kd-islemler').replaceChildren(...islem);
    });

    degisince($('#kd-envanter'), [k.id, k.envanter, durum.envanter_azami], () => {
      const env = $('#kd-envanter');
      const gruplar = grupla(k.envanter);
      $('#kd-env-sayi').textContent = `${k.envanter.length}/${durum.envanter_azami} eşya`;
      env.replaceChildren(
        ...gruplar.map((g) => {
          const e = g.esya;
          const sonSira = g.siralar[g.siralar.length - 1];
          const satir = h(
            'li',
            { class: `inv-row${e.sabit ? ' is-fixed' : ''}`, title: e.sabit ? `${e.ad}: herkeste var, bırakılamaz` : `${e.ad}: haritaya sürükleyip bırakarak yere koy ya da başka birine ver` },
            e.url && e.gorsel && e.var ? h('img', { class: 'inv-thumb', src: e.url, alt: '' }) : h('span', { class: `inv-thumb${e.sabit ? ' is-fist' : ''}`, text: e.sabit ? '✊' : '' }),
            h('span', { class: 'row-name', text: e.resim && !e.var ? `${e.ad} (resmi yok)` : e.ad }),
            e.sinif ? h('span', { class: 'tag', text: e.sinif }) : null,
            h('span', { class: 'inv-weight', text: agirlikYaz(e) }),
            g.adet > 1 ? h('span', { class: 'inv-count', text: `×${g.adet}` }) : null,
            e.sabit ? null : h('button', { class: 'icon-btn', type: 'button', title: 'Adını, sınıfını, ağırlığını ya da dayanıklılığını değiştir', 'aria-label': `${e.ad} düzenle`, onclick: () => esyaDuzenle(k, sonSira, e), text: '✎' }),
            e.sabit ? null : h('button', { class: 'icon-btn', type: 'button', title: 'Bir tane al', 'aria-label': `${e.ad}: bir tane al`, onclick: () => gonder({ t: 'esya_al', karakter: k.id, sira: sonSira }), text: '−' }),
          );
          return e.sabit ? satir : suruklenebilir(satir, { tur: 'envanter', karakter: k.id, sira: sonSira, ad: e.ad }, e.ad);
        }),
      );
      if (k.envanter.length <= 1) env.append(h('li', { class: 'hint', text: 'Aşağıdaki eşyalardan birine tıklayınca bu karaktere verilir; haritadaki pulunun üstüne sürükleyip bırakarak da verebilirsin.' }));
    });
  }

  async function esyaDuzenle(k, sira, e) {
    const sonuc = await formAc({
      baslik: 'Eşyayı düzenle',
      alanlar: [
        { ad: 'ad', etiket: 'Ad', deger: e.ad, azami: 60 },
        { ad: 'sinif', etiket: 'Sınıf', deger: e.sinif, azami: 31, oneriler: durum.siniflar },
        { ad: 'agirlik', etiket: 'Ağırlık', tur: 'sayi', deger: e.agirlik ?? 1, en: 0, ust: 1000000 },
        { ad: 'dayaniklilik', etiket: 'Dayanıklılık (hesabını kurallar.c yapar)', tur: 'sayi', deger: e.dayaniklilik ?? 100 },
      ],
      dugme: 'Kaydet',
    });
    if (!sonuc) return;
    if (!sonuc.ad.trim()) return bildir('Eşyanın adı boş olamaz.');
    const agirlik = Number(sonuc.agirlik);
    const dayaniklilik = Number(sonuc.dayaniklilik);
    gonder({
      t: 'esya_duzenle',
      karakter: k.id,
      sira,
      ad: sonuc.ad.trim(),
      sinif: sonuc.sinif.trim(),
      ...(sonuc.agirlik !== '' && Number.isFinite(agirlik) ? { agirlik: Math.max(0, agirlik) } : {}),
      ...(sonuc.dayaniklilik !== '' && Number.isFinite(dayaniklilik) ? { dayaniklilik } : {}),
    });
  }

  /* GM bir NPC adına bir oyuncuyla takas başlatır; takas penceresi açılır */
  async function takasYaptir(npc) {
    const oyuncular = durum.karakterler.filter((k) => !k.npc && !k.olu && k.harita === npc.harita);
    if (!oyuncular.length) return bildir(`${npc.ad} ile aynı haritada takas yapacak oyuncu yok.`);
    const sonuc = await formAc({
      baslik: `${npc.ad} takas yapsın`,
      metin: 'Oyuncu NPC\'yi görmeli. İki taraf da onaylayınca takas olur.',
      alanlar: [{ ad: 'hedef', etiket: 'Kiminle', tur: 'secim', secenekler: oyuncular.map((k) => ({ deger: k.id, metin: k.ad })) }],
      dugme: 'Takası başlat',
    });
    if (sonuc) gonder({ t: 'takas_baslat', yapan: npc.id, hedef: sonuc.hedef });
  }

  /* Para: altın, gümüş, bakır girdilerinden bakır toplamı */
  for (const ad of ['altin', 'gumus', 'bakir']) {
    $(`#kd-para-${ad}`).addEventListener('change', () => {
      if (!secili) return;
      gonder({ t: 'karakter_guncelle', id: secili, para: paraBirlestir($('#kd-para-altin').value, $('#kd-para-gumus').value, $('#kd-para-bakir').value) });
    });
  }

  /* Haritada yerdeki bir eşyaya ya da bir portala tıklandı */
  async function portalDuzenle(id) {
    const p = harita.portal(id);
    if (!p) return;
    const ad = h('input', { class: 'input', value: p.ad, maxlength: 40 });
    const hedefSec = h('select', { class: 'input' }, h('option', { value: '', text: '— hiçbir yere (kapalı) —' }), ...durum.haritalar.map((m) => h('option', { value: m.dosya, text: dosyaAdi(m.dosya) })));
    hedefSec.value = p.hedef || '';
    const cikis = h('select', { class: 'input' });
    const hx = h('input', { class: 'input', type: 'number', step: 'any', value: String(p.hx ?? 0) });
    const hy = h('input', { class: 'input', type: 'number', step: 'any', value: String(p.hy ?? 0) });
    const konum = h('div', { class: 'row-2' }, h('label', { class: 'field' }, h('span', { class: 'field-label', text: 'Çıkış x (kare)' }), hx), h('label', { class: 'field' }, h('span', { class: 'field-label', text: 'Çıkış y (kare)' }), hy));
    const cikislar = () => {
      const oradaki = (durum.portal_listesi || []).filter((x) => x.harita === hedefSec.value && x.id !== id);
      cikis.replaceChildren(h('option', { value: 'konum', text: 'Şu kareye: x, y' }), h('option', { value: 'orta', text: 'Haritanın ortasına' }), ...oradaki.map((x) => h('option', { value: `p${x.id}`, text: `“${x.ad}” portalına` })));
      cikis.value = p.hedef === hedefSec.value ? 'konum' : oradaki.length ? `p${oradaki[0].id}` : 'orta';
      konum.hidden = cikis.value !== 'konum';
    };
    hedefSec.addEventListener('change', cikislar);
    cikis.addEventListener('change', () => {
      konum.hidden = cikis.value !== 'konum';
    });
    cikislar();
    const alan = (etiket, girdi) => h('label', { class: 'field' }, h('span', { class: 'field-label', text: etiket }), girdi);
    const sonuc = await goster({
      baslik: 'Portal',
      genis: false,
      icerik: [
        h('p', { class: 'modal-text', text: 'Yürüyüşü bu portalda biten oyuncu hedef haritaya geçer. Oyuncular portalı görür ama nereye çıktığını bilmez.' }),
        alan('Ad (yalnızca sen görürsün)', ad),
        alan('Hedef harita', hedefSec),
        alan('Nereye çıksın', cikis),
        konum,
      ],
      ekDugmeler: [
        { metin: 'Portalı sil', deger: 'sil', sinif: 'btn-danger' },
        { metin: 'Kaydet', deger: 'kaydet', sinif: 'btn-primary' },
      ],
    });
    if (sonuc === 'sil') {
      if (await onay({ baslik: 'Portal silinsin mi?', dugme: 'Sil', tehlikeli: true })) gonder({ t: 'portal_sil', id });
      return;
    }
    if (sonuc !== 'kaydet') return;
    const ileti = { t: 'portal_guncelle', id, ad: ad.value, hedef: hedefSec.value };
    if (cikis.value === 'konum') Object.assign(ileti, { hx: Number(hx.value) || 0, hy: Number(hy.value) || 0 });
    else if (cikis.value.startsWith('p')) ileti.hedef_portal = Number(cikis.value.slice(1));
    gonder(ileti);
  }

  /* Haritada yerdeki bir eşyaya tıklandı */
  async function yerEsyasiTiklandi(id) {
    const y = harita.yerEsyasi(id);
    if (!y) return;
    const sonuc = await esyaOnizle(y.esya, 1, [{ metin: 'Haritadan kaldır', deger: 'sil', sinif: 'btn-danger' }]);
    if (sonuc === 'sil') gonder({ t: 'yer_esya_sil', id });
  }

  async function buyuYaptir(a) {
    const hedefler = durum.karakterler.filter((k) => k.harita && k.harita === a.harita && !k.olu);
    const sonuc = await formAc({
      baslik: `${a.ad} büyü yapsın`,
      metin: 'GM\'in yaptırdığı büyü onay beklemez; kurallar.c\'deki buyu() hemen çağrılır.',
      alanlar: [
        { ad: 'hedef', etiket: 'Hedef', tur: 'secim', deger: a.id, secenekler: hedefler.map((k) => ({ deger: k.id, metin: k.id === a.id ? `${k.ad} (kendine)` : k.npc ? `${k.ad} (NPC)` : k.ad })) },
        { ad: 'buyu', etiket: 'Büyü', tur: 'secim', secenekler: durum.buyuler.map((b) => ({ deger: b, metin: b })) },
      ],
      dugme: 'Büyü yap',
    });
    if (sonuc) gonder({ t: 'gm_buyu', yapan: a.id, hedef: sonuc.hedef, buyu: sonuc.buyu });
  }

  async function saldirt(a) {
    const hedefler = durum.karakterler.filter((k) => k.id !== a.id && k.harita && k.harita === a.harita && !k.olu);
    if (!hedefler.length) return bildir('Saldırılacak kimse yok.');
    const esyalar = grupla(a.envanter);
    const sonuc = await formAc({
      baslik: `${a.ad} saldırsın`,
      metin: 'GM saldırısı onay beklemez; kurallar.c\'deki saldiri() hemen çağrılır.',
      alanlar: [
        { ad: 'hedef', etiket: 'Hedef', tur: 'secim', secenekler: hedefler.map((k) => ({ deger: k.id, metin: k.npc ? `${k.ad} (NPC)` : k.ad })) },
        { ad: 'esya', etiket: 'Eşya', tur: 'secim', secenekler: esyalar.map((g) => ({ deger: String(g.siralar[0]), metin: g.esya.sinif ? `${g.esya.ad} (${g.esya.sinif})` : g.esya.ad })) },
      ],
      dugme: 'Saldır',
      tehlikeli: true,
    });
    if (sonuc) gonder({ t: 'gm_saldiri', saldiran: a.id, hedef: sonuc.hedef, esya: Number(sonuc.esya) });
  }

  $('#npc-ekle').addEventListener('click', () => gonder({ t: 'npc_ekle' }));
  suruklenebilir($('#portal-koy'), { tur: 'portal' }, 'Portal');

  // ------------------------------------------------------------ eşya kataloğu (sınıflara göre)

  function katalogCiz() {
    const k = karakter(secili);
    const kap = $('#esyalar');
    $('#esya-ipucu').textContent = k
      ? `Bir eşyaya tıklayınca ${k.ad} karakterine verilir. Haritaya sürükleyip bırakırsan bırakılan yerdeki karaktere gider ya da yere konur.`
      : 'Bir eşyayı haritaya sürükleyip bırak: bir karakterin üstüne bırakırsan ona gider, boş yere bırakırsan yerde durur. Bir karakter seçiliyken eşyaya tıklarsan da ona verilir.';
    degisince(kap, [durum.esyalar, durum.siniflar, k?.id, k?.ad], () => katalogListesi(k, kap));
  }

  function katalogListesi(k, kap) {
    if (!durum.esyalar.length) {
      kap.replaceChildren(
        h('p', { class: 'hint', text: durum.siniflar.length ? `esyalar klasörü boş. "Eşya yükle" ile sınıflara (${durum.siniflar.join(', ')}) resim ekle.` : 'esyalar klasörü boş. Eşya yükle düğmesiyle resim ekle.' }),
      );
      return;
    }
    const siniflar = [...durum.siniflar];
    if (durum.esyalar.some((e) => !e.sinif)) siniflar.push('');
    kap.replaceChildren(
      ...siniflar
        .map((sinif) => {
          const esyalar = durum.esyalar.filter((e) => e.sinif === sinif);
          if (!esyalar.length) return null;
          return h(
            'section',
            { class: 'catalog-group' },
            h('h4', { class: 'catalog-title', text: sinif || 'sınıfsız' }),
            h(
              'ul',
              { class: 'item-grid' },
              ...esyalar.map((e) =>
                h(
                  'li',
                  {},
                  suruklenebilir(
                    esyaKarti(
                      { ad: e.ad, sinif: e.sinif, url: e.url, gorsel: e.gorsel, resim: `esyalar/${e.dosya}` },
                      {
                        baslik: k ? `${e.ad}: ${k.ad} karakterine ver (ya da haritaya sürükle)` : `${e.ad}: haritaya sürükleyip bırak`,
                        tikla: () => (k ? gonder({ t: 'esya_ver', karakter: k.id, esya: e.dosya }) : bildir('Önce bir karakter seç ya da eşyayı haritaya sürükle.')),
                      },
                    ),
                    { tur: 'katalog', esya: e.dosya },
                    e.ad,
                  ),
                ),
              ),
            ),
          );
        })
        .filter(Boolean),
    );
  }

  $('#esya-yukle').addEventListener('click', async () => {
    let klasor = 'esyalar';
    if (durum.siniflar.length) {
      const sonuc = await formAc({
        baslik: 'Eşya yükle',
        metin: 'Eşyanın sınıfı, konduğu klasördür. Sınıfları kurallar.c\'deki esya_siniflari listesinde belirlersin.',
        alanlar: [{ ad: 'sinif', etiket: 'Sınıf', tur: 'secim', secenekler: [...durum.siniflar.map((s) => ({ deger: s, metin: s })), { deger: '', metin: 'sınıfsız' }] }],
        dugme: 'Dosya seç',
      });
      if (!sonuc) return;
      if (sonuc.sinif) klasor = `esyalar/${sonuc.sinif}`;
    }
    dosyaSec('', async (dosyalar) => {
      for (const f of dosyalar) {
        try {
          await dosyaYukle(klasor, f);
          bildir(`${adKisalt(f.name)} ${klasor} klasörüne yüklendi.`, 'ok');
        } catch (e) {
          bildir(`${adKisalt(f.name)}: ${e.message}`);
        }
      }
    });
  });

  // ------------------------------------------------------------ istekler (sağ panel)

  // Sırası gelen karakterin bekleyen isteği (en fazla bir tane)
  function istekCiz() {
    const kap = $('#istekler');
    const ist = durum.istek;
    kap.hidden = !ist;
    if (!ist) return kap.replaceChildren();
    const kim = karakter(ist.kim);
    const ad = kim?.ad || '?';
    let govde;
    if (ist.tur === 'hareket') {
      const ayar = harita.istekHedefi(ist.id);
      const hedef = ayar || { x: ist.x2, y: ist.y2 };
      const mesafe = kim ? `${sayiYaz(Math.hypot(hedef.x - kim.x, hedef.y - kim.y))} kare · ` : '';
      govde = [
        h('p', { class: 'req-what' }, h('b', { text: ad }), ' yürümek istiyor'),
        h('p', { class: 'req-meta', text: `${mesafe}${ayar ? 'hedefi sen kaydırdın' : 'hedefi haritada sürükleyerek kaydırabilirsin'}` }),
      ];
    } else if (ist.tur === 'buyu') {
      govde = [
        ist.hedef === ist.kim
          ? h('p', { class: 'req-what' }, h('b', { text: ad }), ' kendine büyü yapmak istiyor')
          : h('p', { class: 'req-what' }, h('b', { text: ad }), ' → ', h('b', { text: ist.hedef_ad }), ' büyü yapmak istiyor'),
        h('p', { class: 'req-meta req-item' }, h('span', { class: 'spell-icon', 'aria-hidden': 'true', text: '✦' }), h('span', { text: ist.buyu })),
      ];
    } else {
      const e = ist.esya || {};
      govde = [
        h('p', { class: 'req-what' }, h('b', { text: ad }), ' → ', h('b', { text: ist.hedef_ad }), ' saldırmak istiyor'),
        h('p', { class: 'req-meta req-item' }, e.url && e.gorsel && e.var ? h('img', { src: e.url, alt: '' }) : e.sabit ? h('span', { class: 'spell-icon', 'aria-hidden': 'true', text: '✊' }) : null, h('span', { text: e.sinif ? `${e.ad} · ${e.sinif}` : e.ad || 'eşya' })),
      ];
    }
    const onayla = () => {
      const ayar = ist.tur === 'hareket' ? harita.istekHedefi(ist.id) : null;
      gonder({ t: 'istek_onayla', id: ist.id, ...(ayar ? { x: ayar.x, y: ayar.y } : {}) });
    };
    const reddet = () => {
      harita.istekHedefiniSifirla(ist.id);
      gonder({ t: 'istek_reddet', id: ist.id });
    };
    kap.replaceChildren(
      h('h2', { class: 'requests-title', text: 'Bekleyen istek' }),
      h(
        'article',
        { class: `request${ist.tur === 'saldiri' ? ' is-attack' : ist.tur === 'buyu' ? ' is-spell' : ''}`, vars: { '--c': kim?.renk || 'var(--bone)' } },
        h('div', { class: 'req-body' }, ...govde),
        h(
          'div',
          { class: 'req-actions' },
          h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => haritasinaGit(kim), text: kim && !kim.burada ? 'Haritasına git' : 'Göster' }),
          h('button', { class: 'btn btn-sm', type: 'button', onclick: reddet, text: 'Reddet' }),
          h('button', { class: `btn btn-sm ${ist.tur === 'hareket' ? 'btn-primary' : 'btn-danger-solid'}`, type: 'button', onclick: onayla, text: 'Onayla' }),
        ),
      ),
    );
  }

  // ------------------------------------------------------------ ayarlar

  const form = $('#ayar-formu');
  let formDegisti = false;
  form.addEventListener('input', () => {
    formDegisti = true;
  });
  form.addEventListener('submit', (e) => {
    e.preventDefault();
    const sifre = $('#ayar-sifre').value.trim();
    if (!sifre) {
      bildir('Oyun şifresi boş olamaz; oyuncular şifreyle girer.');
      return;
    }
    const enerji = Number($('#ayar-enerji').value);
    const gorus = Number($('#ayar-gorus').value);
    gonder({
      t: 'ayarlar',
      oyun_adi: $('#ayar-ad').value,
      sifre,
      tur_enerjisi: Number.isFinite(enerji) ? Math.max(0, enerji) : 10,
      ...($('#ayar-gorus').value !== '' && Number.isFinite(gorus) ? { gorus: Math.max(0, gorus) } : {}),
    });
    formDegisti = false;
    bildir('Ayarlar kaydedildi.', 'ok');
  });

  $('#mac-sifirla').addEventListener('click', async () => {
    const evet = await onay({
      baslik: 'Maç sıfırlansın mı?',
      metin:
        'NPC\'ler, oyuncuların statları, envanterleri (yumruk kalır), paraları, haritalardaki yerleri, keşifleri, yerdeki eşyalar, takaslar, tur sırası ve kayıt silinir. Oyuncular, ayarlar, haritalar, portallar ve eşya resimleri kalır. Önceki hali oyun klasöründe durum-sifirlamadan-once.json olarak saklanır.',
      dugme: 'Maçı sıfırla',
      tehlikeli: true,
    });
    if (evet) gonder({ t: 'mac_sifirla' });
  });

  function ayarlarCiz() {
    const a = durum.ayarlar;
    if (!formDegisti && !form.contains(document.activeElement)) {
      $('#ayar-ad').value = a.oyun_adi;
      $('#ayar-sifre').value = a.sifre;
      $('#ayar-enerji').value = String(a.tur_enerjisi);
      $('#ayar-gorus').value = String(a.gorus);
    }
    const oyuncular = durum.karakterler.filter((k) => !k.npc);
    $('#oyuncu-yonetim').replaceChildren(
      ...(oyuncular.length
        ? oyuncular.map((o) =>
            h(
              'li',
              { class: `admin-row${o.cevrimici ? '' : ' is-offline'}`, vars: { '--c': o.renk } },
              h('span', { class: 'dot' }),
              h('span', { class: 'row-name', text: o.ad }),
              h('button', {
                class: 'btn btn-sm btn-ghost',
                type: 'button',
                text: 'Oyundan çıkar',
                onclick: async () => {
                  const evet = await onay({
                    baslik: `${o.ad} oyundan çıkarılsın mı?`,
                    metin: 'Statları, envanteri, haritalardaki yeri ve hafızası silinir. Aynı adla tekrar girerse sıfırdan başlar.',
                    dugme: 'Çıkar',
                    tehlikeli: true,
                  });
                  if (evet) gonder({ t: 'oyuncu_sil', id: o.id });
                },
              }),
            ),
          )
        : [h('li', { class: 'hint', text: 'Henüz oyuncu yok.' })]),
    );
  }

  // ------------------------------------------------------------ dışarıya açılanlar

  return {
    ayarla(yeni) {
      durum = yeni;
      haritalariCiz();
      if (bekleyenOdak && karakter(bekleyenOdak)?.burada) {
        const id = bekleyenOdak;
        bekleyenOdak = null;
        requestAnimationFrame(() => harita.odakla(id));
      }
      if (bekleyenPortal && harita.portal(bekleyenPortal)) {
        const id = bekleyenPortal;
        bekleyenPortal = null;
        portalDuzenle(id);
      }
      $('#harita-gizli').checked = Boolean(durum.harita?.gizli);
      $('#harita-gizli').disabled = !durum.harita;
      $('#harita-kapat').disabled = !durum.harita;
      izgaraCiz();
      siraCiz();
      if (bekleyenNpc && karakter(bekleyenNpc)) {
        secili = bekleyenNpc;
        bekleyenNpc = null;
        harita.sec(secili, true);
        karakterleriCiz();
        $('#karakter-duzen').scrollIntoView({ block: 'nearest' });
        kdAd.focus();
        kdAd.select();
      } else {
        karakterleriCiz();
      }
      istekCiz();
      ayarlarCiz();
    },
    npcEklendi(id) {
      bekleyenNpc = id;
      if (karakter(id)) this.ayarla(durum);
    },
    secimDegisti(id) {
      secili = id;
      if (durum) karakterleriCiz();
    },
    istekTasindi() {
      if (durum) istekCiz();
    },
    yerEsyasiTiklandi,
    portalTiklandi: (id) => portalDuzenle(id),
    portalEklendi(id) {
      bekleyenPortal = id;
      if (harita.portal(id)) {
        bekleyenPortal = null;
        portalDuzenle(id);
      }
    },
  };
}
