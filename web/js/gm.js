import { $, h, throttle, debounce, bildir, dosyaYukle, dosyaAdi, sayiYaz, adKisalt, yolKodla, SIKKELER, sikkeSayisi, TEPKILER } from './ortak.js';
import { onay, formAc, goster, secimPenceresi } from './pencere.js';
import { esyaKarti, esyaOnizle, grupla, agirlikYaz, esyaResmi, resimli, esyaSayisi } from './envanter.js';
import { STATLAR, statYaz } from './tur.js';

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
function depoMetinOku(ad) {
  try {
    return localStorage.getItem(ad);
  } catch {
    return null;
  }
}
function depoMetinYaz(ad, deger) {
  try {
    if (deger === null) localStorage.removeItem(ad);
    else localStorage.setItem(ad, deger);
  } catch {
    /* önemsiz */
  }
}

/** GM paneli: harita, duvarlar, görüş, ızgara, tur sırası, karakterler (haritalarına göre açılır kapanır gruplar,
 * yerleştirme, statlar ve azamileri, NPC notları, envanter, görünüm, NPC ekleme ve kopyalama, tepkiler), eşyalar,
 * yerdeki eşyaların seçimi ve kopyalanması, ayarlar ve istekler. Karakterler ve eşyalar listeden haritaya sürüklenip
 * bırakılır; haritada bir karaktere tıklayınca düzenleyicisi bir pencerede açılır. */
export function gmPaneliOlustur({ gonder, harita, gorunumAc }) {
  let durum = null;
  let secili = null;
  let bekleyenNpc = null;
  let bekleyenOdak = null; // başka haritadaki bir karaktere gidiliyor: harita açılınca ona odaklan
  let bekleyenPortal = null; // yeni konan portalın düzenleyicisi açılacak
  let bekleyenKopya = null; // kopyalanan yerdeki eşyalar: durum gelince seçilsin
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
    else if (veri.tur === 'npc') gonder({ t: 'npc_ekle', x: yer.x, y: yer.y });
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
      if (e.button !== 0 || !durum?.harita || el.disabled) return;
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

  // ------------------------------------------------------------ duvarlar

  let duvarKipi = null; // 'ciz' | 'sil' | null
  const geriAlinacak = []; // bu oturumda çizilen duvarların kimlikleri (son çizilen sonda)

  function kipSec(kip) {
    duvarKipi = duvarKipi === kip ? null : kip;
    harita.duvarKipi(duvarKipi);
    $('#duvar-ciz').setAttribute('aria-pressed', String(duvarKipi === 'ciz'));
    $('#duvar-sil').setAttribute('aria-pressed', String(duvarKipi === 'sil'));
    $('#duvar-ciz').classList.toggle('btn-primary', duvarKipi === 'ciz');
    $('#duvar-sil').classList.toggle('btn-danger-solid', duvarKipi === 'sil');
  }
  function duvarGeriAl() {
    const duran = new Set((durum?.duvarlar || []).map((w) => w[0])); /* sonradan silinenler atlanır */
    let id;
    do id = geriAlinacak.pop();
    while (id !== undefined && !duran.has(id));
    if (id === undefined) return bildir('Geri alınacak duvar yok.');
    gonder({ t: 'duvar_sil', id });
  }
  $('#duvar-ciz').addEventListener('click', () => kipSec('ciz'));
  $('#duvar-sil').addEventListener('click', () => kipSec('sil'));
  $('#duvar-geri').addEventListener('click', duvarGeriAl);
  $('#duvar-temizle').addEventListener('click', async () => {
    const evet = await onay({
      baslik: 'Bu haritadaki bütün duvarlar silinsin mi?',
      metin: 'Oyuncular duvarların arkasını görmeye ve her yere düz yürümeye başlar. Geri alınamaz.',
      dugme: 'Hepsini sil',
      tehlikeli: true,
    });
    if (!evet) return;
    geriAlinacak.length = 0;
    gonder({ t: 'duvarlari_sil' });
  });
  const duvarIzgara = $('#duvar-izgara');
  duvarIzgara.checked = depoOku('roleplay.duvar-izgara', true);
  harita.izgarayaYapis(duvarIzgara.checked);
  duvarIzgara.addEventListener('change', () => {
    depoYaz('roleplay.duvar-izgara', duvarIzgara.checked);
    harita.izgarayaYapis(duvarIzgara.checked);
  });
  const duvarGoster = $('#duvar-goster');
  duvarGoster.checked = depoOku('roleplay.duvar-goster', true);
  harita.duvarlariGoster(duvarGoster.checked);
  duvarGoster.addEventListener('change', () => {
    depoYaz('roleplay.duvar-goster', duvarGoster.checked);
    harita.duvarlariGoster(duvarGoster.checked);
  });
  /* Esc: çizilen çizgiyi bitirir, çizgi yoksa duvar kipinden çıkar. Ctrl+Z: son çizilen duvarı geri alır. */
  window.addEventListener('keydown', (e) => {
    if (e.defaultPrevented || !durum?.harita || e.target.closest?.('input, textarea, select, [contenteditable], dialog')) return;
    if (e.key === 'Escape' && duvarKipi) {
      if (!harita.zinciriBitir()) kipSec(duvarKipi);
      e.preventDefault();
    } else if ((e.ctrlKey || e.metaKey) && !e.shiftKey && e.key.toLowerCase() === 'z' && geriAlinacak.length) {
      e.preventDefault();
      duvarGeriAl();
    }
  });

  function duvarlarCiz() {
    const hr = durum.harita;
    $('#duvar-ayar').hidden = !hr;
    if (!hr) {
      if (duvarKipi) kipSec(duvarKipi);
      return;
    }
    const n = durum.duvarlar?.length || 0;
    $('#duvar-sayi').textContent = n ? `Bu haritada ${n} duvar var.` : 'Bu haritada duvar yok.';
    $('#duvar-temizle').disabled = !n;
    $('#duvar-geri').disabled = !geriAlinacak.length;
  }

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
    /* Konumlar tam sayıyken (Ayarlar) herkes zaten kare ortasında durur */
    $('#iz-oturt').checked = g.oturt || Boolean(durum.tam_sayi);
    $('#iz-oturt').disabled = Boolean(durum.tam_sayi);
    $('#iz-oturt').closest('label').title = durum.tam_sayi ? 'Ayarlar\'da "Konumlar tam sayı olsun" açık: herkes kare ortasında durur.' : '';
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
    const imza = [liste.map((id) => [id, karakter(id).ad, karakter(id).renk, karakter(id).olu, karakter(id).cevrimici, Boolean(karakter(id).harita)]), s.aktif, s.sirada];
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
          const meta = k.olu ? 'ölü' : !k.harita ? 'haritada değil' : !k.npc && !k.cevrimici ? 'bağlı değil' : k.npc ? 'NPC' : null;
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

  /* Açılıp kapanan karakter grupları: kapalı olanların anahtarları bu tarayıcıda saklanır */
  const GRUP_ANAHTARI = 'roleplay.kapali-gruplar';
  const kapaliGruplar = new Set(
    (() => {
      try {
        const v = JSON.parse(depoMetinOku(GRUP_ANAHTARI) || '[]');
        return Array.isArray(v) ? v.filter((x) => typeof x === 'string') : [];
      } catch {
        return [];
      }
    })(),
  );
  function grupAcKapa(anahtar) {
    if (kapaliGruplar.has(anahtar)) kapaliGruplar.delete(anahtar);
    else kapaliGruplar.add(anahtar);
    depoMetinYaz(GRUP_ANAHTARI, JSON.stringify([...kapaliGruplar]));
    karakterleriCiz();
  }

  /* Karakterler gruplarda: haritada olmayanlar, sonra her harita ayrı (baktığın harita işaretli). Gruplar açılıp
     kapanır. Hepsi listeden baktığın haritaya sürüklenip bırakılabilir. */
  function karakterleriCiz() {
    const liste = durum.karakterler;
    if (secili && !karakter(secili)) secili = null;
    const kap = $('#karakter-listesi');
    const imza = [
      liste.map((k) => [k.id, k.ad, k.renk, k.npc, k.olu, k.gizli, k.harita, k.burada, k.cevrimici, k.statlar, k.sirada]),
      secili,
      durum.harita?.dosya,
      durum.haritalar.map((m) => m.dosya),
      [...kapaliGruplar],
    ];
    degisince(kap, imza, () => karakterListesi(liste, kap));
    const bekleyen = liste.filter((k) => !k.npc && !k.harita).length;
    $('#yerlestir-ipucu').hidden = !bekleyen;
    $('#yerlestir-ipucu').textContent = bekleyen
      ? `${bekleyen} oyuncu hiçbir haritada değil. Onları listeden haritaya sürükleyip bırakarak yerleştir; yerleşmeyen oyuncunun sırası geçilir.`
      : '';
    editorCiz();
    katalogCiz();
  }

  function karakterListesi(liste, kap) {
    const satir = (k) => {
      const durumu = k.olu ? h('span', { class: 'row-meta is-dead', text: 'ölü' }) : !k.npc && !k.cevrimici ? h('span', { class: 'row-meta', text: 'bağlı değil' }) : null;
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
            h(
              'span',
              { class: 'row-tags' },
              k.npc ? h('span', { class: 'row-meta', text: 'NPC' }) : null,
              k.gizli ? h('span', { class: 'row-meta', text: 'gizli' }) : null,
              durumu,
            ),
            h('span', { class: 'mini-stats', text: `${statYaz(k.statlar, 'can')} can · ${statYaz(k.statlar, 'enerji')} enerji` }),
          ),
          { tur: 'karakter', id: k.id },
          k.ad,
        ),
      );
    };
    if (!liste.length) {
      kap.replaceChildren(h('li', { class: 'hint', text: 'Henüz karakter yok. Oyuncular girince burada görünür; NPC eklemek için aşağıdaki düğmeye bas.' }));
      return;
    }
    const gruplar = [{ anahtar: 'yok', ad: 'Haritada olmayanlar', uyeler: liste.filter((k) => !k.harita) }];
    const haritalar = [...durum.haritalar.map((m) => m.dosya)];
    for (const k of liste) if (k.harita && !haritalar.includes(k.harita)) haritalar.push(k.harita);
    for (const dosya of haritalar) {
      const uyeler = liste.filter((k) => k.harita === dosya);
      if (uyeler.length) gruplar.push({ anahtar: `harita:${dosya}`, ad: dosyaAdi(dosya), dosya, uyeler, burada: durum.harita?.dosya === dosya });
    }
    kap.replaceChildren(
      ...gruplar
        .filter((g) => g.uyeler.length)
        .map((g) => {
          const kapali = kapaliGruplar.has(g.anahtar);
          return h(
            'li',
            { class: `char-group${kapali ? ' is-closed' : ''}${g.burada ? ' is-here' : ''}` },
            h(
              'div',
              { class: 'group-head' },
              h(
                'button',
                { class: 'group-toggle', type: 'button', 'aria-expanded': String(!kapali), title: kapali ? 'Grubu aç' : 'Grubu kapat', onclick: () => grupAcKapa(g.anahtar) },
                h('span', { class: 'group-caret', 'aria-hidden': 'true' }),
                h('span', { class: 'group-name', text: g.ad }),
                g.burada ? h('span', { class: 'group-here', text: 'bakıyorsun' }) : null,
                h('span', { class: 'group-count', text: String(g.uyeler.length) }),
              ),
              g.dosya && !g.burada ? h('button', { class: 'icon-btn group-go', type: 'button', title: `${g.ad} haritasını aç`, 'aria-label': `${g.ad} haritasını aç`, onclick: () => gonder({ t: 'harita_ac', dosya: g.dosya }), text: '↗' }) : null,
            ),
            kapali ? null : h('ul', { class: 'char-list group-list' }, ...g.uyeler.map(satir)),
          );
        }),
    );
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
  /* Her stat için değer ve azami (azami boş ya da 0: azamisi yok; sunucu değeri azaminin üstünde bırakmaz) */
  const statGirdileri = {};
  const azamiGirdileri = {};
  const enterBirak = (girdi) =>
    girdi.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') girdi.blur();
    });
  $('#kd-statlar').replaceChildren(
    ...STATLAR.map(([k, ad]) => {
      const girdi = h('input', { class: 'input', type: 'number', step: '0.5', inputmode: 'decimal', 'aria-label': ad });
      const azami = h('input', { class: 'input stat-max', type: 'number', min: '0', step: '0.5', inputmode: 'decimal', placeholder: 'yok', 'aria-label': `${ad}: azami`, title: `${ad}: azami değer (boş: azamisi yok)` });
      girdi.addEventListener('change', () => {
        const v = Number(girdi.value);
        if (!secili || girdi.value === '' || !Number.isFinite(v)) return;
        gonder({ t: 'karakter_guncelle', id: secili, [k]: v });
      });
      azami.addEventListener('change', () => {
        const v = azami.value === '' ? 0 : Number(azami.value);
        if (!secili || !Number.isFinite(v)) return;
        gonder({ t: 'karakter_guncelle', id: secili, [`${k}_azami`]: Math.max(0, v) });
      });
      enterBirak(girdi);
      enterBirak(azami);
      statGirdileri[k] = girdi;
      azamiGirdileri[k] = azami;
      return h('div', { class: `stat-input stat-${k}` }, h('span', { text: ad }), h('div', { class: 'stat-pair' }, girdi, h('span', { class: 'stat-slash', 'aria-hidden': 'true', text: '/' }), azami));
    }),
  );

  /* NPC notu: yazdıkça (biraz bekleyerek) kaydedilir */
  const kdNot = $('#kd-not');
  const notKaydet = debounce((id, metin) => gonder({ t: 'karakter_guncelle', id, not: metin }), 500);
  kdNot.addEventListener('input', () => secili && notKaydet(secili, kdNot.value));
  kdNot.addEventListener('blur', () => secili && karakter(secili)?.not !== kdNot.value && gonder({ t: 'karakter_guncelle', id: secili, not: kdNot.value }));

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
    for (const [ad] of SIKKELER) {
      const girdi = $(`#kd-para-${ad}`);
      if (document.activeElement !== girdi) girdi.value = String(k.para?.[ad] ?? 0);
    }
    $('#kd-npc').hidden = !k.npc;
    if (k.npc) {
      if (document.activeElement !== kdAd) kdAd.value = k.ad;
      for (const sw of $('#kd-renkler').children) sw.setAttribute('aria-checked', String(sw.dataset.renk === k.renk));
      $('#kd-gizli').checked = k.gizli;
      if (document.activeElement !== kdNot && kdNot.dataset.id !== `${k.id}:${k.not}`) {
        kdNot.value = k.not || '';
        kdNot.dataset.id = `${k.id}:${k.not}`;
      }
    }
    const yuvarla = (v) => String(Math.round(v * 100) / 100);
    for (const [ad, girdi] of Object.entries(statGirdileri)) {
      if (document.activeElement !== girdi) girdi.value = yuvarla(k.statlar[ad]);
      const az = azamiGirdileri[ad];
      const azami = k.statlar.azami?.[ad] || 0;
      if (document.activeElement !== az) az.value = azami > 0 ? yuvarla(azami) : '';
    }
    $('#kd-agirlik').textContent = sayiYaz(k.statlar.agirlik);
    /* Tepkiler: NPC bir haritadayken GM onun adına tepki verir */
    const tepkiKap = $('#kd-tepki');
    tepkiKap.hidden = !(k.npc && k.harita && !k.olu);
    degisince(tepkiKap, [k.id], () =>
      tepkiKap.replaceChildren(
        h('span', { class: 'reaction-label', text: 'Tepki:' }),
        ...TEPKILER.map(([tur, simge, ad]) => h('button', { class: 'reaction-btn', type: 'button', title: `${k.ad}: ${ad}`, 'aria-label': `${k.ad}: ${ad}`, onclick: () => gonder({ t: 'tepki', id: k.id, tur }), text: simge })),
      ),
    );
    degisince($('#kd-islemler'), [k.id, k.ad, k.olu, k.harita, k.burada, k.npc, k.sirada, durum.sira.aktif, durum.sira.sirada, durum.buyuler.length, durum.esyalar.length], () => {
      const islem = [];
      islem.push(
        k.olu
          ? h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gonder({ t: 'dirilt', id: k.id }), text: 'Dirilt' })
          : h('button', { class: 'btn btn-sm btn-danger', type: 'button', onclick: () => gonder({ t: 'oldur', id: k.id }), text: 'Öldür' }),
      );
      islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => gorunumAc(k.id), text: 'Görünüm…' }));
      if (durum.esyalar.length) islem.push(h('button', { class: 'btn btn-sm', type: 'button', title: 'Katalogdan bir eşya ver', onclick: () => esyaVer(k), text: 'Eşya ver…' }));
      if (k.harita && !k.burada) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => haritasinaGit(k), text: 'Haritasına git' }));
      if (k.burada && !k.olu && durum.sira.aktif && durum.sira.sirada === k.id) {
        islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => saldirt(k), text: 'Saldırt…' }));
        if (durum.buyuler.length) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => buyuYaptir(k), text: 'Büyü yaptır…' }));
      }
      if (k.npc && k.burada && !k.olu) islem.push(h('button', { class: 'btn btn-sm', type: 'button', onclick: () => takasYaptir(k), text: 'Takas…' }));
      if (k.harita) {
        islem.push(h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => gonder({ t: 'haritadan_kaldir', id: k.id }), text: 'Haritadan kaldır' }));
      }
      if (k.npc) {
        islem.push(
          h('button', {
            class: 'btn btn-sm',
            type: 'button',
            title: k.harita
              ? 'Aynı adla; aynı statlar, para, eşyalar, not ve görünümle yanında bir tane daha'
              : 'Aynı adla; aynı statlar, para, eşyalar, not ve görünümle bir tane daha (o da haritada olmaz)',
            onclick: () => gonder({ t: 'npc_kopyala', id: k.id }),
            text: 'Kopyala',
          }),
        );
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
      $('#kd-env-sayi').textContent = `${esyaSayisi(k.envanter)}/${durum.envanter_azami} eşya (yumruk sayılmaz)`;
      env.replaceChildren(
        ...gruplar.map((g) => {
          const e = g.esya;
          const sonSira = g.siralar[g.siralar.length - 1];
          const satir = h(
            'li',
            { class: `inv-row${e.sabit ? ' is-fixed' : ''}`, title: e.sabit ? `${e.ad}: herkeste var, bırakılamaz` : `${e.ad}: haritaya sürükleyip bırakarak yere koy ya da başka birine ver` },
            e.sabit && !resimli(e) ? h('span', { class: 'inv-thumb is-fist', text: '✊' }) : h('img', { class: `inv-thumb${resimli(e) ? '' : ' is-default'}`, src: esyaResmi(e), alt: '' }),
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
      if (!esyaSayisi(k.envanter)) env.append(h('li', { class: 'hint', text: 'Eşyalar bölümünden birine tıklayınca (ya da "Eşya ver…") bu karaktere verilir; haritadaki pulunun üstüne sürükleyip bırakarak da verebilirsin.' }));
    });
  }

  /* Katalogdan seçilen eşya bu karaktere verilir (karakter penceresi açıkken panel tıklanamaz) */
  async function esyaVer(k) {
    const secim = await secimPenceresi({
      baslik: `${k.ad}: eşya ver`,
      genis: true,
      icerik: (sec) => [
        h('p', { class: 'modal-text', text: 'Hangi eşya verilsin?' }),
        h(
          'ul',
          { class: 'item-grid pick-grid' },
          ...durum.esyalar.map((e) => h('li', {}, esyaKarti({ ad: e.ad, sinif: e.sinif, url: e.url, gorsel: e.gorsel, resim: `esyalar/${e.dosya}` }, { tikla: () => sec(e.dosya) }))),
        ),
      ],
    });
    if (secim) gonder({ t: 'esya_ver', karakter: k.id, esya: secim });
  }

  async function esyaDuzenle(k, sira, e) {
    const sonuc = await formAc({
      baslik: 'Eşyayı düzenle',
      alanlar: [
        { ad: 'ad', etiket: 'Ad', deger: e.ad, azami: 60 },
        { ad: 'sinif', etiket: 'Sınıf', deger: e.sinif, azami: 31, oneriler: durum.siniflar },
        { ad: 'agirlik', etiket: 'Ağırlık', tur: 'sayi', deger: e.agirlik, en: 0, ust: 1000000 },
        { ad: 'dayaniklilik', etiket: 'Dayanıklılık (hesabını kurallar.c yapar)', tur: 'sayi', deger: e.dayaniklilik },
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

  /* Para: her sikke ayrı; biri ötekine çevrilmez */
  for (const [ad] of SIKKELER) {
    $(`#kd-para-${ad}`).addEventListener('change', (e) => {
      if (!secili) return;
      gonder({ t: 'karakter_guncelle', id: secili, [ad]: sikkeSayisi(e.target.value) });
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

  /* Haritada yerdeki bir eşyanın ayrıntısı (seçiliyken bir daha tıklanınca ya da çift tıklanınca) */
  async function yerEsyasiTiklandi(id) {
    const y = harita.yerEsyasi(id);
    if (!y) return;
    const sonuc = await esyaOnizle(y.esya, 1, [
      { metin: 'Haritadan kaldır', deger: 'sil', sinif: 'btn-danger' },
      { metin: 'Kopyala', deger: 'kopyala', sinif: 'btn' },
    ]);
    if (sonuc === 'sil') gonder({ t: 'yer_esya_sil', id });
    else if (sonuc === 'kopyala') yerKopyala([id]);
  }

  // ------------------------------------------------------------ yerdeki eşyaların seçimi: kopyala, yapıştır, sil

  let pano = null; // Ctrl+C ile kopyalanan yerdeki eşyalar: [{ id, x, y }] (yapıştırınca imlecin yerine)

  /* Seçili eşyaların yanına (bir kare sağ-aşağı) kopyaları konur; kopyalar seçili olur, sürüklenip yerleştirilir */
  function yerKopyala(ids, hedef = null) {
    const ogeler = ids.map((id) => harita.yerEsyasi(id) && { id, ...harita.yerEsyasi(id) }).filter(Boolean);
    if (!ogeler.length) return;
    const boyut = harita.boyut;
    let dx = 1;
    let dy = 1;
    if (hedef) {
      /* Yapıştır: grubun ortası imlecin yerine gelir, aralarındaki düzen kalır */
      const ox = ogeler.reduce((t, o) => t + o.x, 0) / ogeler.length;
      const oy = ogeler.reduce((t, o) => t + o.y, 0) / ogeler.length;
      dx = hedef.x - ox;
      dy = hedef.y - oy;
    }
    const sinirli = (v, ust) => Math.min(Math.max(v, 0), ust);
    gonder({
      t: 'yer_esya_kopyala',
      ogeler: ogeler.map((o) => {
        const p = harita.oturt({ x: sinirli(o.x + dx, boyut?.genislik ?? 1e6), y: sinirli(o.y + dy, boyut?.yukseklik ?? 1e6) });
        return { id: o.id, x: p.x, y: p.y };
      }),
    });
  }

  function yerSil(ids) {
    if (ids.length) gonder({ t: 'yer_esya_sil', ids });
  }

  /* Haritanın altındaki araç çubuğu: kaç eşya seçili, kopyala, sil, seçimi bırak */
  function secimCiz(ids) {
    const kap = $('#secim-araclari');
    kap.hidden = !ids.length;
    if (!ids.length) return kap.replaceChildren();
    kap.replaceChildren(
      h('span', { class: 'selection-count', text: ids.length === 1 ? `${harita.yerEsyasi(ids[0])?.esya?.ad || 'Eşya'} seçili` : `${ids.length} eşya seçili` }),
      ids.length === 1 ? h('button', { class: 'btn btn-sm btn-ghost', type: 'button', onclick: () => yerEsyasiTiklandi(ids[0]), text: 'Ayrıntı' }) : null,
      h('button', { class: 'btn btn-sm', type: 'button', title: 'Kopyala (Ctrl+D); Ctrl+C ve Ctrl+V ile imlecin yerine yapıştır', onclick: () => yerKopyala(harita.yerSecimi()), text: 'Kopyala' }),
      h('button', { class: 'btn btn-sm btn-danger', type: 'button', title: 'Haritadan kaldır (Delete)', onclick: () => yerSil(harita.yerSecimi()), text: 'Kaldır' }),
      h('button', { class: 'icon-btn', type: 'button', title: 'Seçimi bırak (Esc)', 'aria-label': 'Seçimi bırak', onclick: () => harita.yerSeciminiAyarla([]), text: '×' }),
    );
  }

  window.addEventListener('keydown', (e) => {
    if (e.defaultPrevented || !durum?.harita || e.target.closest?.('input, textarea, select, [contenteditable], dialog')) return;
    const ids = harita.yerSecimi();
    const ctrl = e.ctrlKey || e.metaKey;
    const tus = e.key.toLowerCase();
    if ((e.key === 'Delete' || e.key === 'Backspace') && ids.length) {
      e.preventDefault();
      yerSil(ids);
    } else if (ctrl && tus === 'd' && ids.length) {
      e.preventDefault();
      yerKopyala(ids);
    } else if (ctrl && tus === 'c' && ids.length && !String(window.getSelection?.() || '')) {
      pano = ids.slice();
      bildir(`${ids.length} eşya kopyalandı; Ctrl+V ile imlecin olduğu yere yapıştır.`, 'ok');
    } else if (ctrl && tus === 'v' && pano?.length) {
      e.preventDefault();
      const yer = harita.fareKonumu();
      const kalan = pano.filter((id) => harita.yerEsyasi(id));
      if (!kalan.length) bildir('Kopyalanan eşyalar artık bu haritada değil.');
      else yerKopyala(kalan, yer);
    } else if (e.key === 'Escape' && ids.length) {
      harita.yerSeciminiAyarla([]);
    }
  });

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

  /* Tıklayınca hiçbir haritada olmayan bir NPC; haritaya sürüklenince bırakıldığı yerde */
  $('#npc-ekle').addEventListener('click', () => gonder({ t: 'npc_ekle' }));
  suruklenebilir($('#npc-ekle'), { tur: 'npc' }, 'Yeni NPC');
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

  /* Yeni eşya: adı ve sınıfı yeter, resim isteğe bağlı. Resimsiz eşya esyalar/<sınıf>/<ad>.esya adlı boş bir dosyadır
     ve sınıfına göre varsayılan bir resimle görünür. Resim seçilirse onun dosyası eşya olur (birden çok resim
     seçilirse her biri, kendi adıyla, ayrı bir eşya). */
  $('#esya-yukle').addEventListener('click', async () => {
    const sonuc = await formAc({
      baslik: 'Eşya ekle',
      metin: 'Eşyanın sınıfı, konduğu klasördür; sınıfları kurallar.c\'deki esya_siniflari listesinde belirlersin. Resim zorunlu değil: resimsiz eşya sınıfına göre varsayılan bir resimle görünür.',
      alanlar: [
        { ad: 'ad', etiket: 'Ad', azami: 60, deger: '' },
        { ad: 'sinif', etiket: 'Sınıf', tur: 'secim', secenekler: [...durum.siniflar.map((s) => ({ deger: s, metin: s })), { deger: '', metin: 'sınıfsız' }] },
        { ad: 'resim', etiket: 'Resim (isteğe bağlı)', tur: 'dosya', kabul: 'image/png,image/jpeg,image/webp,image/gif', coklu: true, ipucu: 'Birden çok resim seçersen her biri kendi adıyla ayrı bir eşya olur.' },
      ],
      dugme: 'Ekle',
    });
    if (!sonuc) return;
    const klasor = sonuc.sinif ? `esyalar/${sonuc.sinif}` : 'esyalar';
    /* Dosya adı olabilecek ad: / ve \ olmasın, nokta ile başlamasın */
    const temiz = (s) => String(s || '').replace(/[/\\]+/g, '-').replace(/^\.+/, '').trim();
    const ad = temiz(sonuc.ad);
    const resimler = sonuc.resim || [];
    if (!resimler.length && !ad) return bildir('Eşyanın adını yaz ya da bir resim seç.');
    const yukle = async (dosya, dosyaAd) => {
      try {
        await dosyaYukle(klasor, dosya, { ad: dosyaAd });
        bildir(`${adKisalt(dosyaAd)} eşyası eklendi (${sonuc.sinif || 'sınıfsız'}).`, 'ok');
      } catch (e) {
        bildir(`${adKisalt(dosyaAd)}: ${e.message}`);
      }
    };
    if (!resimler.length) return yukle(new File([], `${ad}.esya`), `${ad}.esya`);
    for (const f of resimler) {
      if (!GORSEL.test(f.name)) {
        bildir(`${adKisalt(f.name)}: eşya resmi PNG, JPG, WEBP ya da GIF olmalı.`);
        continue;
      }
      const uzanti = f.name.slice(f.name.lastIndexOf('.'));
      await yukle(f, resimler.length === 1 && ad ? `${ad}${uzanti}` : f.name);
    }
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
    let yolYok = false;
    if (ist.tur === 'hareket') {
      const ayar = harita.istekHedefi(ist.id);
      const L = harita.istekUzunlugu(ist);
      yolYok = L === null && Boolean(kim?.burada);
      const mesafe = L !== null ? `${sayiYaz(L)} kare yol · ` : yolYok ? 'oraya yol yok · ' : '';
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
        h('p', { class: 'req-meta req-item' }, e.sabit && !resimli(e) ? h('span', { class: 'spell-icon', 'aria-hidden': 'true', text: '✊' }) : e.ad ? h('img', { src: esyaResmi(e), alt: '' }) : null, h('span', { text: e.sinif ? `${e.ad} · ${e.sinif}` : e.ad || 'eşya' })),
      ];
    }
    /* Saldırı ya da büyü yürüme aşamasında istendiyse: onaylanınca önce yürümeden geçer */
    if (ist.tur !== 'hareket' && durum.sira.aktif && durum.sira.sirada === ist.kim && durum.sira.asama !== 'eylem')
      govde.push(h('p', { class: 'req-meta', text: 'Bu tur henüz yürümedi: onaylarsan yürümeden geçmiş sayılır.' }));
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
          h('button', { class: `btn btn-sm ${ist.tur === 'hareket' ? 'btn-primary' : 'btn-danger-solid'}`, type: 'button', disabled: yolYok, title: yolYok ? 'Bu hedefe yol yok; hedefi kaydır ya da reddet' : null, onclick: onayla, text: 'Onayla' }),
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
    const gorus = Number($('#ayar-gorus').value);
    gonder({
      t: 'ayarlar',
      oyun_adi: $('#ayar-ad').value,
      sifre,
      tam_sayi: $('#ayar-tam-sayi').checked,
      ...($('#ayar-gorus').value !== '' && Number.isFinite(gorus) ? { gorus: Math.max(0, gorus) } : {}),
    });
    formDegisti = false;
    bildir('Ayarlar kaydedildi.', 'ok');
  });

  $('#mac-sifirla').addEventListener('click', async () => {
    const evet = await onay({
      baslik: 'Maç sıfırlansın mı?',
      metin:
        'NPC\'ler, oyuncuların statları ve azamileri, envanterleri (yumruk kalır), paraları, haritalardaki yerleri, keşifleri, yerdeki eşyalar, takaslar, tur sırası ve kayıt silinir. Oyuncular, ayarlar, haritalar, portallar ve eşya resimleri kalır. Önceki hali oyun klasöründe durum-sifirlamadan-once.json olarak saklanır.',
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
      $('#ayar-gorus').value = String(a.gorus);
      $('#ayar-tam-sayi').checked = Boolean(a.tam_sayi);
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

  // ------------------------------------------------------------ karakter penceresi

  /* Haritada bir karaktere tıklayınca düzenleyicisi (soldaki panelin karakter düzenleyicisi) bir pencerede açılır;
     pencere kapanınca panele geri döner. */
  const pencere = $('#karakter-penceresi');
  const duzenleyici = $('#karakter-duzen');
  const duzenleyiciYeri = { ust: duzenleyici.parentElement, sonraki: duzenleyici.nextElementSibling };
  function karakterPenceresi(id) {
    if (!karakter(id)) return;
    sec(id);
    if (!pencere.open) {
      $('#karakter-penceresi-govde').prepend(duzenleyici);
      pencere.showModal();
      pencere.querySelector('#karakter-penceresi-kapat').focus();
    }
  }
  pencere.addEventListener('close', () => duzenleyiciYeri.ust.insertBefore(duzenleyici, duzenleyiciYeri.sonraki));
  $('#karakter-penceresi-kapat').addEventListener('click', () => pencere.close());
  /* Pencerenin dışına tıklayınca kapanır */
  pencere.addEventListener('click', (e) => {
    if (e.target === pencere) pencere.close();
  });

  // ------------------------------------------------------------ panelin genişliği

  /* Panelin sağ kenarı sürüklenerek genişletilir (bu tarayıcıda saklanır); çift tıklama varsayılana döndürür */
  const uyg = $('#uygulama');
  const boyutlandirici = $('#gm-boyut');
  const GENISLIK_ANAHTARI = 'roleplay.gm-genislik';
  const genislikAyarla = (px) => {
    if (!px) {
      uyg.style.removeProperty('--gm-w');
      depoMetinYaz(GENISLIK_ANAHTARI, null);
      return;
    }
    const g = Math.round(Math.min(Math.max(px, 240), Math.max(280, Math.min(640, window.innerWidth * 0.45))));
    uyg.style.setProperty('--gm-w', `${g}px`);
    depoMetinYaz(GENISLIK_ANAHTARI, String(g));
  };
  const kayitli = Number(depoMetinOku(GENISLIK_ANAHTARI));
  if (kayitli > 0) genislikAyarla(kayitli);
  boyutlandirici.addEventListener('pointerdown', (e) => {
    if (e.button !== 0) return;
    e.preventDefault();
    boyutlandirici.setPointerCapture(e.pointerId);
    const sol = $('#gm-panel').getBoundingClientRect().left;
    const hareket = (ev) => genislikAyarla(ev.clientX - sol);
    const bitir = () => {
      boyutlandirici.removeEventListener('pointermove', hareket);
      boyutlandirici.removeEventListener('pointerup', bitir);
      boyutlandirici.removeEventListener('pointercancel', bitir);
      boyutlandirici.classList.remove('is-active');
    };
    boyutlandirici.classList.add('is-active');
    boyutlandirici.addEventListener('pointermove', hareket);
    boyutlandirici.addEventListener('pointerup', bitir);
    boyutlandirici.addEventListener('pointercancel', bitir);
  });
  boyutlandirici.addEventListener('dblclick', () => genislikAyarla(0));
  boyutlandirici.addEventListener('keydown', (e) => {
    const simdi = $('#gm-panel').getBoundingClientRect().width;
    if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      e.preventDefault();
      genislikAyarla(simdi + (e.key === 'ArrowRight' ? 20 : -20));
    }
  });

  // ------------------------------------------------------------ dışarıya açılanlar

  return {
    ayarla(yeni) {
      durum = yeni;
      haritalariCiz();
      if (bekleyenKopya) {
        const ids = bekleyenKopya.filter((id) => harita.yerEsyasi(id));
        if (ids.length) {
          harita.yerSeciminiAyarla(ids);
          bekleyenKopya = null;
        }
      }
      secimCiz(harita.yerSecimi());
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
      duvarlarCiz();
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
    karakterTiklandi: karakterPenceresi,
    yerSecimDegisti: (ids) => secimCiz(ids),
    yerKopyalandi(ids) {
      /* Kopyalar seçili olsun: harita onları durumla alır */
      bekleyenKopya = ids;
      const var_ = ids.filter((id) => harita.yerEsyasi(id));
      if (var_.length === ids.length) {
        harita.yerSeciminiAyarla(ids);
        bekleyenKopya = null;
      }
    },
    portalTiklandi: (id) => portalDuzenle(id),
    duvarEklendi(id) {
      geriAlinacak.push(id);
      if (geriAlinacak.length > 500) geriAlinacak.shift();
      if (durum) duvarlarCiz();
    },
    portalEklendi(id) {
      bekleyenPortal = id;
      if (harita.portal(id)) {
        bekleyenPortal = null;
        portalDuzenle(id);
      }
    },
  };
}
