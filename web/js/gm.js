import { $, h, throttle, debounce, bildir, dosyaYukle, dosyaAdi, sayiYaz, adKisalt, yolKodla } from './ortak.js';
import { onay, formAc } from './pencere.js';
import { esyaKarti, grupla } from './envanter.js';
import { STATLAR } from './tur.js';

const NPC_RENKLERI = ['#a8402f', '#4f5a63', '#7a5b3c', '#2f6b4f', '#6b3f7a', '#8a6a2f', '#6fa8dc', '#e3798b', '#8cc084', '#d9d2c3'];
const GORSEL = /\.(png|jpe?g|gif|webp)$/i;

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

/** GM paneli: harita, görüş, ızgara, tur sırası, karakterler (statlar, envanter), eşyalar, ayarlar ve istekler. */
export function gmPaneliOlustur({ gonder, harita }) {
  let durum = null;
  let secili = null;
  let bekleyenNpc = null;
  const karakter = (id) => durum?.karakterler.find((k) => k.id === id) || null;

  // ------------------------------------------------------------ harita

  function haritalariCiz() {
    const kap = $('#haritalar');
    const aktif = durum.harita?.dosya;
    if (!durum.haritalar.length) {
      kap.replaceChildren(h('li', { class: 'hint', text: 'haritalar klasörü boş. Bir harita görseli yükle.' }));
      return;
    }
    kap.replaceChildren(
      ...durum.haritalar.map((m) =>
        h(
          'li',
          {},
          h(
            'button',
            { class: 'lib-open', type: 'button', 'aria-current': m.dosya === aktif ? 'true' : null, onclick: () => haritaAc(m) },
            h('img', { class: 'lib-thumb', src: m.url, alt: '', loading: 'lazy', decoding: 'async' }),
            h(
              'span',
              { class: 'lib-text' },
              h('span', { class: 'lib-name', text: dosyaAdi(m.dosya) }),
              h('span', { class: 'lib-meta', text: m.dosya === aktif ? (durum.harita.gizli ? 'Açık, oyunculardan gizli' : 'Oyuncular burada') : m.dosya }),
            ),
          ),
        ),
      ),
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
  halkalar.checked = depoOku('zarmasasi.halkalar', true);
  harita.halkalariGoster(halkalar.checked);
  halkalar.addEventListener('change', () => {
    depoYaz('zarmasasi.halkalar', halkalar.checked);
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
  const masa = $('#masa');
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
    const baslat = $('#sira-baslat');
    baslat.textContent = s.aktif ? 'Turları durdur' : 'Turları başlat';
    baslat.className = s.aktif ? 'btn' : 'btn btn-primary';
  }
  $('#sira-zarla').addEventListener('click', () => gonder({ t: 'sira_zarla' }));
  $('#sira-baslat').addEventListener('click', () => gonder({ t: durum.sira.aktif ? 'sira_durdur' : 'sira_baslat' }));

  // ------------------------------------------------------------ karakterler

  function sec(id) {
    secili = id;
    harita.sec(id, true);
    karakterleriCiz();
  }

  function karakterleriCiz() {
    const liste = durum.karakterler;
    if (secili && !karakter(secili)) secili = null;
    const kap = $('#karakter-listesi');
    if (!liste.length) {
      kap.replaceChildren(h('li', { class: 'hint', text: 'Henüz karakter yok.' }));
    } else {
      kap.replaceChildren(
        ...liste.map((k) =>
          h(
            'li',
            {},
            h(
              'button',
              {
                class: `char-row${secili === k.id ? ' is-selected' : ''}${k.olu ? ' is-dead' : ''}`,
                type: 'button',
                vars: { '--c': k.renk },
                onclick: () => {
                  sec(k.id);
                  if (durum.harita) harita.odakla(k.id);
                },
              },
              h('span', { class: `dot${k.npc ? ' is-npc' : ''}` }),
              h('span', { class: 'row-name', text: k.ad }),
              k.gizli ? h('span', { class: 'row-meta', text: 'gizli' }) : null,
              k.olu ? h('span', { class: 'row-meta is-dead', text: 'ölü' }) : !k.npc && !k.cevrimici ? h('span', { class: 'row-meta', text: 'bağlı değil' }) : null,
              h('span', { class: 'mini-stats', text: `${sayiYaz(k.statlar.can)} can · ${sayiYaz(k.statlar.enerji)} en.` }),
            ),
          ),
        ),
      );
    }
    $('#npc-ekle').disabled = !durum.harita;
    editorCiz();
    katalogCiz();
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
    $('#kd-npc').hidden = !k.npc;
    if (k.npc) {
      if (document.activeElement !== kdAd) kdAd.value = k.ad;
      for (const sw of $('#kd-renkler').children) sw.setAttribute('aria-checked', String(sw.dataset.renk === k.renk));
      $('#kd-gizli').checked = k.gizli;
    }
    for (const [ad, girdi] of Object.entries(statGirdileri)) {
      if (document.activeElement !== girdi) girdi.value = String(Math.round(k.statlar[ad] * 100) / 100);
    }
    const islem = [];
    islem.push(
      k.olu
        ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gonder({ t: 'dirilt', id: k.id }), text: 'Dirilt' })
        : h('button', { class: 'btn btn-sm btn-danger', type: 'button', onclick: () => gonder({ t: 'oldur', id: k.id }), text: 'Öldür' }),
    );
    if (durum.harita && !k.olu && durum.sira.aktif && durum.sira.sirada === k.id) {
      islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => saldirt(k), text: 'Saldırt…' }));
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

    const env = $('#kd-envanter');
    const gruplar = grupla(k.envanter);
    $('#kd-env-sayi').textContent = k.envanter.length ? `${k.envanter.length} eşya` : '';
    if (!gruplar.length) {
      env.replaceChildren(h('li', { class: 'hint', text: 'Envanter boş. Aşağıdaki eşyalardan birine tıklayınca bu karaktere verilir.' }));
    } else {
      env.replaceChildren(
        ...gruplar.map((g) => {
          const e = g.esya;
          const sonSira = g.siralar[g.siralar.length - 1];
          return h(
            'li',
            { class: 'inv-row' },
            e.url && e.gorsel && e.var ? h('img', { class: 'inv-thumb', src: e.url, alt: '' }) : h('span', { class: 'inv-thumb' }),
            h('span', { class: 'row-name', text: e.resim && !e.var ? `${e.ad} (resmi yok)` : e.ad }),
            e.sinif ? h('span', { class: 'tag', text: e.sinif }) : null,
            g.adet > 1 ? h('span', { class: 'inv-count', text: `×${g.adet}` }) : null,
            h('button', { class: 'icon-btn', type: 'button', title: 'Adını ya da sınıfını değiştir', 'aria-label': `${e.ad} düzenle`, onclick: () => esyaDuzenle(k, sonSira, e), text: '✎' }),
            h('button', { class: 'icon-btn', type: 'button', title: 'Bir tane al', 'aria-label': `${e.ad}: bir tane al`, onclick: () => gonder({ t: 'esya_al', karakter: k.id, sira: sonSira }), text: '−' }),
          );
        }),
      );
    }
  }

  async function esyaDuzenle(k, sira, e) {
    const sonuc = await formAc({
      baslik: 'Eşyayı düzenle',
      alanlar: [
        { ad: 'ad', etiket: 'Ad', deger: e.ad, azami: 60 },
        { ad: 'sinif', etiket: 'Sınıf', deger: e.sinif, azami: 31, oneriler: durum.siniflar },
      ],
      dugme: 'Kaydet',
    });
    if (!sonuc) return;
    if (!sonuc.ad.trim()) return bildir('Eşyanın adı boş olamaz.');
    gonder({ t: 'esya_duzenle', karakter: k.id, sira, ad: sonuc.ad.trim(), sinif: sonuc.sinif.trim() });
  }

  async function saldirt(a) {
    const hedefler = durum.karakterler.filter((k) => k.id !== a.id && !k.olu);
    if (!a.envanter.length) return bildir(`${a.ad} için önce bir eşya ver; saldırı bir eşyayla yapılır.`);
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

  // ------------------------------------------------------------ eşya kataloğu (sınıflara göre)

  function katalogCiz() {
    const k = karakter(secili);
    const kap = $('#esyalar');
    $('#esya-ipucu').textContent = k ? `Bir eşyaya tıklayınca ${k.ad} karakterine verilir.` : 'Önce bir karakter seç; sonra bir eşyaya tıklayınca ona verilir.';
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
                  esyaKarti(
                    { ad: e.ad, sinif: e.sinif, url: e.url, gorsel: e.gorsel, resim: `esyalar/${e.dosya}` },
                    {
                      baslik: k ? `${e.ad}: ${k.ad} karakterine ver` : e.ad,
                      tikla: () => (k ? gonder({ t: 'esya_ver', karakter: k.id, esya: e.dosya }) : bildir('Önce bir karakter seç.')),
                    },
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
    } else {
      const e = ist.esya || {};
      govde = [
        h('p', { class: 'req-what' }, h('b', { text: ad }), ' → ', h('b', { text: ist.hedef_ad }), ' saldırmak istiyor'),
        h('p', { class: 'req-meta req-item' }, e.url && e.gorsel && e.var ? h('img', { src: e.url, alt: '' }) : null, h('span', { text: e.sinif ? `${e.ad} · ${e.sinif}` : e.ad || 'eşya' })),
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
        { class: `request${ist.tur === 'saldiri' ? ' is-attack' : ''}`, vars: { '--c': kim?.renk || 'var(--bone)' } },
        h('div', { class: 'req-body' }, ...govde),
        h(
          'div',
          { class: 'req-actions' },
          h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => harita.odakla(ist.kim), text: 'Göster' }),
          h('button', { class: 'btn btn-sm', type: 'button', onclick: reddet, text: 'Reddet' }),
          h('button', { class: `btn btn-sm ${ist.tur === 'saldiri' ? 'btn-danger-solid' : 'btn-primary'}`, type: 'button', onclick: onayla, text: 'Onayla' }),
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
    gonder({ t: 'ayarlar', oyun_adi: $('#ayar-ad').value, sifre, tur_enerjisi: Number.isFinite(enerji) ? Math.max(0, enerji) : 10 });
    formDegisti = false;
    bildir('Ayarlar kaydedildi.', 'ok');
  });

  function ayarlarCiz() {
    const a = durum.ayarlar;
    if (!formDegisti && !form.contains(document.activeElement)) {
      $('#ayar-ad').value = a.oyun_adi;
      $('#ayar-sifre').value = a.sifre;
      $('#ayar-enerji').value = String(a.tur_enerjisi);
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
  };
}
