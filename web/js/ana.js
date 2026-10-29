import { $, h, istek, bildir, dosyaAdi, paraYaz } from './ortak.js';
import { haritaOlustur } from './harita.js';
import { kayitOlustur } from './kayit.js';
import { turKartiOlustur, durumKartiCiz } from './tur.js';
import { envanterCiz, esyaKarti, grupla } from './envanter.js';
import { secimPenceresi, goster } from './pencere.js';
import { zarSimgesi } from './zar-sekilleri.js';
import { gorunumPenceresi } from './gorunum.js';
import { takasOlustur } from './takas.js';
import { ses, sesAcik, sesAyarla } from './ses.js';

// /gm GM ekranını, kök adres oyuncu ekranını açar.
const rol = location.pathname.replace(/\/+$/, '') === '/gm' ? 'gm' : 'oyuncu';
const dar = window.matchMedia('(max-width: 900px)');

let bilgi = { oyun_adi: 'Roleplay', sifre_var: true };
let ws = null;
let durum = null;
let cikildi = false;
let bekleme = 1000;
let okunmamis = 0;
let siraBende = false;
let sonIstek = null;
let gorunumAcik = null; // açık görünüm penceresi (yeni durumda yenilenir)
let ganimetAcik = null; // açık ganimet penceresi: { hedef, kap }
let ganimetSessiz = null; // "Hepsini al" ile kapanan pencere sunucunun yanıtıyla yeniden açılmasın
let harita, kayit, tur, gm, dosyalar, takas;

function gonder(mesaj) {
  if (ws?.readyState === WebSocket.OPEN) ws.send(JSON.stringify(mesaj));
  else bildir('Sunucuyla bağlantı yok; birazdan yeniden denenecek.');
}

// ------------------------------------------------------------ giriş

function bootKaldir() {
  $('#boot')?.remove();
}

function girisGoster(hata = '') {
  bootKaldir();
  $('#uygulama').hidden = true;
  $('#giris').hidden = false;
  const gmMi = rol === 'gm';
  document.title = gmMi ? `GM – ${bilgi.oyun_adi}` : bilgi.oyun_adi;
  $('#giris-baslik').textContent = bilgi.oyun_adi;
  $('#giris-alt').textContent = gmMi ? 'GM ekranı' : 'Oyuna katıl';
  $('#giris-ad-alani').hidden = gmMi;
  $('#giris-sifre-etiket').textContent = gmMi ? 'GM şifresi' : 'Oyun şifresi';
  $('#giris-dugme').textContent = gmMi ? 'GM olarak gir' : 'Oyuna gir';
  const not = $('#giris-not');
  if (gmMi) {
    not.hidden = false;
    not.textContent = 'GM şifresi, sunucudaki .env dosyasında GM_SIFRESI olarak yazılıdır. Bir kez girince bu tarayıcı seni hatırlar.';
  } else {
    not.hidden = bilgi.sifre_var;
    not.textContent = bilgi.sifre_var ? '' : 'GM henüz oyun şifresini belirlemedi. GM şifreyi ayarlayınca buradan girebilirsin.';
  }
  $('#giris-hata').textContent = hata;
  if (!gmMi) {
    try {
      const son = localStorage.getItem('roleplay.ad');
      if (son && !$('#giris-ad').value) $('#giris-ad').value = son;
    } catch {
      /* önemsiz */
    }
  }
  (gmMi || $('#giris-ad').value ? $('#giris-sifre') : $('#giris-ad')).focus();
}

$('#giris-formu').addEventListener('submit', async (e) => {
  e.preventDefault();
  const gmMi = rol === 'gm';
  const ad = $('#giris-ad').value.trim();
  const sifre = $('#giris-sifre').value;
  const hata = $('#giris-hata');
  if (!gmMi && !ad) {
    hata.textContent = 'Oyunda görünecek bir isim yaz.';
    return $('#giris-ad').focus();
  }
  if (!sifre) {
    hata.textContent = gmMi ? 'GM şifresini yaz.' : 'Oyun şifresini yaz.';
    return $('#giris-sifre').focus();
  }
  const dugme = $('#giris-dugme');
  dugme.disabled = true;
  hata.textContent = '';
  try {
    if (gmMi) {
      await istek('/api/gm-giris', { yontem: 'POST', govde: { sifre } });
    } else {
      await istek('/api/giris', { yontem: 'POST', govde: { ad, sifre } });
      try {
        localStorage.setItem('roleplay.ad', ad);
      } catch {
        /* önemsiz */
      }
    }
    $('#giris-sifre').value = '';
    cikildi = false;
    baglan();
  } catch (err) {
    hata.textContent = err.message;
    $('#giris-sifre').select();
  } finally {
    dugme.disabled = false;
  }
});

async function cikis() {
  cikildi = true;
  ws?.close();
  try {
    await istek(`/api/cikis?rol=${rol}`, { yontem: 'POST', govde: {} });
  } catch {
    /* sunucuya ulaşılamasa da çıkıldı */
  }
  durum = null;
  girisGoster();
}

// ------------------------------------------------------------ bağlantı

function baglantiDurumu(metin) {
  const el = $('#baglanti');
  el.textContent = metin;
  el.hidden = !metin;
}

function baglan() {
  if (!harita) kur();
  ws?.close();
  const proto = location.protocol === 'https:' ? 'wss' : 'ws';
  const soket = new WebSocket(`${proto}://${location.host}/ws?rol=${rol}`);
  ws = soket;
  soket.onopen = () => {
    bekleme = 1000;
    baglantiDurumu('');
  };
  soket.onmessage = (e) => {
    let m;
    try {
      m = JSON.parse(e.data);
    } catch {
      return;
    }
    mesaj(m);
  };
  soket.onclose = () => {
    if (ws !== soket || cikildi) return;
    baglantiDurumu('Bağlantı koptu, yeniden bağlanılıyor…');
    setTimeout(yenidenDene, bekleme);
    bekleme = Math.min(bekleme * 2, 10000);
  };
}

async function yenidenDene() {
  if (cikildi) return;
  try {
    await istek(`/api/oturum?rol=${rol}`);
    baglan();
  } catch (e) {
    if (e.durum === 401) {
      baglantiDurumu('');
      girisGoster(rol === 'gm' ? 'GM oturumun kapandı; GM şifresiyle yeniden gir.' : 'Oturumun kapandı; oyun şifresi değişmiş olabilir. Yeniden gir.');
    } else {
      setTimeout(yenidenDene, bekleme);
      bekleme = Math.min(bekleme * 2, 10000);
    }
  }
}

// ------------------------------------------------------------ saldırı ve büyü seçimi (oyuncu)

/* Bir karaktere tıklandı. Ölüyse üstü aranır. Başkasıysa saldır, büyü yap ya da takas; kendisiyse büyü yap.
   Saldırı ve büyü yalnızca kendi sıranda, takas her zaman. */
async function etkilesimSec(hedefId) {
  if (!durum || durum.ben.gm) return;
  const hedef = harita.token(hedefId);
  if (!hedef) return;
  if (durum.olu) return bildir('Öldün; GM seni diriltebilir.');
  if (hedef.olu) return gonder({ t: 'ganimet_bak', hedef: hedefId });
  const s = durum.sira;
  const engel = !s.aktif ? 'Turlar başlamadı.' : !s.sirada_ben ? 'Sıra sende değil.' : !(durum.statlar.enerji > 0) ? 'Enerjin kalmadı.' : null;
  if (hedef.ben) return engel ? bildir(engel) : buyuSec(hedef);
  const secenek = (ad, sinif, simge, baslik, alt, kapali) =>
    h(
      'button',
      { class: `choice ${sinif}`, type: 'button', disabled: kapali, title: kapali || null, dataset: { secim: ad } },
      h('span', { class: 'choice-icon', 'aria-hidden': 'true', text: simge }),
      h('b', { text: baslik }),
      h('small', { text: kapali || alt }),
    );
  const secim = await secimPenceresi({
    baslik: hedef.ad,
    icerik: (sec) => {
      const satir = h(
        'div',
        { class: 'choice-row is-three' },
        secenek('saldir', 'is-attack', '⚔', 'Saldır', 'Envanterindeki bir eşyayla', engel),
        secenek('buyu', 'is-spell', '✦', 'Büyü yap', 'Büyü listesinden', engel),
        secenek('takas', 'is-trade', '⇄', 'Takas', 'Eşya ya da para değiş', null),
      );
      satir.addEventListener('click', (e) => {
        const b = e.target.closest('[data-secim]');
        if (b && !b.disabled) sec(b.dataset.secim);
      });
      return [h('p', { class: 'modal-text', text: 'Ne yapacaksın? Saldırı ve büyü GM\'in onayına gider; takası iki taraf onaylar.' }), satir];
    },
  });
  if (secim === 'saldir') saldiriSec(hedef);
  else if (secim === 'buyu') buyuSec(hedef);
  else if (secim === 'takas') gonder({ t: 'takas_baslat', hedef: hedefId });
}

/* Ölünün üstü: eşyaları ve parası tek tek ya da hepsi birden alınır. Sunucu her alıştan sonra yenisini gönderir. */
function ganimetGoster(m) {
  if (!ganimetAcik && ganimetSessiz === m.hedef) {
    ganimetSessiz = null;
    return;
  }
  const kap = ganimetAcik?.hedef === m.hedef ? ganimetAcik.kap : h('div', { class: 'loot' });
  const al = (ek) => {
    ses.para();
    gonder({ t: 'ganimet_al', hedef: m.hedef, ...ek });
  };
  const bos = !m.envanter.length && !(m.para > 0);
  kap.replaceChildren(
    ...[
      m.para > 0
        ? h('div', { class: 'loot-coins' }, h('span', { class: 'coin-icon', 'aria-hidden': 'true', text: '◉' }), h('span', { text: paraYaz(m.para) }), h('button', { class: 'btn btn-sm', type: 'button', onclick: () => al({ para: true }), text: 'Parayı al' }))
        : null,
      m.envanter.length
        ? h('ul', { class: 'item-grid pick-grid' }, ...m.envanter.map((x) => h('li', {}, esyaKarti(x.esya, { baslik: `${x.esya.ad}: al`, tikla: () => al({ sira: x.sira, ad: x.esya.ad }) }))))
        : null,
      h('p', { class: 'modal-text', text: bos ? 'Üstünde alınacak bir şey kalmadı.' : 'Almak istediğine tıkla.' }),
    ].filter(Boolean),
  );
  $('#pencere-dugmeler [value="hepsi"]')?.toggleAttribute('hidden', bos);
  if (ganimetAcik?.hedef === m.hedef) return;
  ganimetAcik = { hedef: m.hedef, kap };
  const ekDugmeler = bos ? [] : [{ metin: 'Hepsini al', deger: 'hepsi', sinif: 'btn-primary' }];
  goster({ baslik: `${m.ad}: üstünü ara`, icerik: kap, genis: true, ekDugmeler }).then((sonuc) => {
    if (ganimetAcik?.kap === kap) ganimetAcik = null;
    if (sonuc !== 'hepsi') return;
    ganimetSessiz = m.hedef;
    al({ hepsi: true });
  });
}

async function buyuSec(hedef) {
  const liste = durum.buyuler || [];
  if (!liste.length) return bildir('Kurallarda büyü yok (kurallar.c\'deki buyuler listesi boş).');
  const kendim = hedef.id === durum.ben.id;
  const secim = await secimPenceresi({
    baslik: kendim ? 'Kendine büyü yap' : `Büyü: ${hedef.ad}`,
    icerik: (sec) => [
      h('p', { class: 'modal-text', text: 'Hangi büyüyü yapacaksın? İsteğin GM\'e gider; onaylarsa olur.' }),
      h('ul', { class: 'spell-list' }, ...liste.map((b) => h('li', {}, h('button', { class: 'spell', type: 'button', onclick: () => sec(b) }, h('span', { class: 'spell-icon', 'aria-hidden': 'true', text: '✦' }), h('span', { text: b }))))),
    ],
  });
  if (secim) gonder({ t: 'buyu', hedef: hedef.id, buyu: secim });
}

async function saldiriSec(hedef) {
  const hedefId = hedef.id;
  const gruplar = grupla(durum.envanter);
  const secim = await secimPenceresi({
    baslik: `Saldır: ${hedef.ad}`,
    genis: gruplar.length > 4,
    icerik: (sec) => [
      h('p', { class: 'modal-text', text: 'Hangi eşyayı kullanacaksın? İsteğin GM\'e gider; onaylarsa saldırırsın.' }),
      h('ul', { class: 'item-grid pick-grid' }, ...gruplar.map((g) => h('li', {}, esyaKarti(g.esya, { adet: g.adet, tikla: () => sec(g.siralar[0]) })))),
    ],
  });
  if (secim === null || secim === undefined) return;
  const e = durum.envanter[secim];
  if (!e) return;
  gonder({ t: 'saldir', hedef: hedefId, esya: secim, esya_ad: e.ad });
}

// ------------------------------------------------------------ arayüz

function kur() {
  const uyg = $('#uygulama');
  uyg.classList.toggle('is-gm', rol === 'gm');

  harita = haritaOlustur({
    gorus: $('#gorus'),
    dunya: $('#dunya'),
    resim: $('#harita-resim'),
    izgara: $('#izgara'),
    portalKatmani: $('#portallar'),
    yerKatmani: $('#yerdekiler'),
    tokenKatmani: $('#tokenler'),
    sisTuvali: $('#sis'),
    katman: $('#katman'),
    etiketler: $('#etiketler'),
    efektKatmani: $('#efektler'),
    isaretKatmani: $('#isaretler'),
    yakinlik: $('#yakinlik'),
  });
  harita.on('yuru', (x, y) => gonder({ t: 'hareket', x, y }));
  harita.on('tasi', (id, x, y) => gonder({ t: 'tasi', id, x, y }));
  harita.on('isaret', (x, y, odak) => gonder({ t: 'isaret', x, y, odak }));
  harita.on('sec', (id) => gm?.secimDegisti(id));
  harita.on('etkiles', (id) => etkilesimSec(id));
  harita.on('istekTasi', () => gm?.istekTasindi());
  harita.on('esyaTasi', (id, x, y) => gonder({ t: 'esya_birak', kaynak: 'yer', id, x, y }));
  harita.on('esyaTikla', (id) => gm?.yerEsyasiTiklandi(id));
  harita.on('portalTasi', (id, x, y) => gonder({ t: 'portal_guncelle', id, x, y }));
  harita.on('portalTikla', (id) => gm?.portalTiklandi(id));

  takas = takasOlustur({ gonder });

  // Ses açık / kapalı
  const sesDugme = $('#ses');
  const sesCiz = () => {
    sesDugme.textContent = sesAcik() ? '🔊' : '🔇';
    sesDugme.setAttribute('aria-pressed', String(sesAcik()));
    sesDugme.title = sesAcik() ? 'Sesi kapat' : 'Sesi aç';
  };
  sesCiz();
  sesDugme.addEventListener('click', () => {
    sesAyarla(!sesAcik());
    sesCiz();
  });

  kayit = kayitOlustur($('#kayit'));
  tur = turKartiOlustur($('#tur-karti'), { gonder });

  // Zar tepsisi
  const tepsi = $('#zarlar');
  for (const yuz of [4, 6, 8, 10, 12, 20, 100]) {
    tepsi.append(
      h('button', { class: 'die-btn', type: 'button', title: `1d${yuz} at`, 'aria-label': `1d${yuz} at`, dataset: { roll: `1d${yuz}` } }, zarSimgesi(yuz, yuz === 100 ? '%' : yuz)),
    );
  }
  $('.tray').addEventListener('click', (e) => {
    const b = e.target.closest('[data-roll]');
    if (b) gonder({ t: 'zar', ifade: b.dataset.roll });
  });
  $('#zar-formu').addEventListener('submit', (e) => {
    e.preventDefault();
    const ifade = $('#zar-ifade').value.trim();
    if (!ifade) return $('#zar-ifade').focus();
    gonder({ t: 'zar', ifade });
    $('#zar-ifade').select();
  });

  // Oyuncunun kendi görünümü ve envanterinden yere bırakma
  $('#durum-karti').addEventListener('click', (e) => {
    if (!e.target.closest('[data-gorunum]') || !durum || durum.ben.gm) return;
    gorunumAcik = gorunumPenceresi({ karakterAl: () => ({ ...durum.ben, npc: false }), durumAl: () => durum, gonder, gm: false });
  });

  // Sekmeler
  const sekmeSec = (ad) => {
    const kayitMi = ad === 'kayit';
    $('#sekme-kayit').setAttribute('aria-selected', String(kayitMi));
    $('#sekme-envanter').setAttribute('aria-selected', String(!kayitMi));
    $('#kayit').hidden = !kayitMi;
    $('#envanter').hidden = kayitMi;
    if (kayitMi) kayit.sonaKaydir();
  };
  $('#sekme-kayit').addEventListener('click', () => sekmeSec('kayit'));
  $('#sekme-envanter').addEventListener('click', () => sekmeSec('envanter'));

  // Dar ekranda çekmeceler
  const cekmece = (ad, acik) => {
    uyg.classList.toggle(`show-${ad}`, acik);
    $(`#ac-${ad}`).setAttribute('aria-expanded', String(acik));
    if (ad === 'yan' && acik) rozet(0);
  };
  $('#ac-yan').addEventListener('click', () => {
    const acik = !uyg.classList.contains('show-yan');
    cekmece('gm', false);
    cekmece('yan', acik);
  });
  $('#ac-gm').addEventListener('click', () => {
    const acik = !uyg.classList.contains('show-gm');
    cekmece('yan', false);
    cekmece('gm', acik);
  });
  $('#masa').addEventListener('pointerdown', () => {
    if (!dar.matches) return;
    cekmece('yan', false);
    cekmece('gm', false);
  });

  $('#cikis').addEventListener('click', cikis);
  if (rol === 'gm') $('#ac-yan-metin').textContent = 'Sıra ve istekler';

  if (rol === 'gm') {
    import('./gm.js').then(({ gmPaneliOlustur }) => {
      gm = gmPaneliOlustur({
        gonder,
        harita,
        gorunumAc: (id) => {
          gorunumAcik = gorunumPenceresi({ karakterAl: () => durum?.karakterler.find((k) => k.id === id), durumAl: () => durum, gonder, gm: true });
        },
      });
      if (durum) gm.ayarla(durum);
    });
    import('./dosyalar.js').then(({ dosyaYoneticisiOlustur }) => {
      dosyalar = dosyaYoneticisiOlustur();
      $('#dosyalar-ac').addEventListener('click', () => dosyalar.ac());
    });
  }
}

function rozet(n) {
  okunmamis = n;
  const r = $('#rozet');
  r.hidden = !n;
  r.textContent = n > 99 ? '99+' : String(n);
}

function baslikGuncelle() {
  const ad = durum.harita?.dosya ? dosyaAdi(durum.harita.dosya) : durum.oyun_adi;
  $('#sahne-adi').textContent = ad;
  const temel = durum.harita?.dosya ? `${ad} – ${durum.oyun_adi}` : durum.oyun_adi;
  if (durum.ben.gm) document.title = durum.istek ? `● İstek bekliyor – ${temel}` : `GM – ${temel}`;
  else document.title = siraBende ? `● Sıra sende – ${temel}` : temel;
}

function bosDurum() {
  const hr = durum.harita;
  const bos = $('#bos');
  bos.hidden = Boolean(hr?.url);
  if (rol === 'gm') {
    $('#bos-baslik').textContent = durum.haritalar?.length ? 'Bir harita aç' : 'Masaya ilk haritayı koy';
    $('#bos-metin').textContent = durum.haritalar?.length
      ? 'Soldaki listeden bir harita seç. Görseli haritanın üstüne sürükleyip bırakarak da yükleyebilirsin.'
      : 'Harita yükle düğmesiyle ya da görseli buraya sürükleyerek haritalar klasörüne ekle.';
  } else if (hr?.gizli) {
    $('#bos-baslik').textContent = 'GM haritayı hazırlıyor';
    $('#bos-metin').textContent = 'Harita hazır olunca burada açılacak.';
  } else {
    $('#bos-baslik').textContent = 'Henüz bir haritada değilsin';
    $('#bos-metin').textContent = 'GM seni bir haritaya yerleştirince burada açılacak. Bu arada zar atabilirsin.';
  }
  const etiket = $('#harita-durum');
  etiket.hidden = rol !== 'gm' || !hr?.url;
  if (!etiket.hidden) {
    etiket.classList.toggle('is-live', !hr.gizli);
    $('#harita-durum-metin').textContent = hr.gizli
      ? 'Oyuncular bu haritayı görmüyor (gizli)'
      : hr.izgara.sis
        ? 'Oyuncular bu haritayı görüş sisiyle görüyor'
        : 'Oyuncular bu haritanın tamamını görüyor';
  }
}

function kimlerCiz() {
  const cevrimici = durum.oyuncular.filter((o) => o.cevrimici);
  $('#kimler').replaceChildren(
    ...cevrimici.map((o) => h('li', { class: `who-chip${o.id === durum.ben.id ? ' is-me' : ''}`, vars: { '--c': o.renk }, title: o.ad }, o.ad)),
  );
}

// GM'in durumunda tokenler karakter listesinden gelir (baktığı haritadakiler)
function normallestir(m) {
  if (m.ben.gm) m.tokenler = m.harita ? m.karakterler.filter((k) => k.burada).map((k) => ({ ...k, canli: true, ben: false })) : [];
  return m;
}

function istekBildirimi(m) {
  if (!m.ben.gm) return;
  const i = m.istek;
  if (durum && i && i.id !== sonIstek) {
    const kim = m.karakterler.find((k) => k.id === i.kim);
    const ad = kim?.ad || '?';
    const metin =
      i.tur === 'hareket'
        ? `${ad} yürümek istiyor.`
        : i.tur === 'buyu'
          ? i.hedef === i.kim
            ? `${ad} kendine büyü yapmak istiyor (${i.buyu}).`
            : `${ad} → ${i.hedef_ad}: büyü isteği (${i.buyu}).`
          : `${ad} → ${i.hedef_ad}: saldırı isteği.`;
    bildir(metin, 'ok');
    ses.istek();
    if (dar.matches && !$('#uygulama').classList.contains('show-yan')) rozet(okunmamis + 1);
  }
  sonIstek = i?.id ?? null;
}

function mesaj(m) {
  switch (m.t) {
    case 'durum': {
      if (!m.ben) return;
      normallestir(m);
      istekBildirimi(m);
      const ilk = !durum;
      durum = m;
      bilgi.oyun_adi = m.oyun_adi;
      bootKaldir();
      $('#giris').hidden = true;
      $('#uygulama').hidden = false;
      harita.ayarla(m);
      const sonuc = tur.ayarla(m);
      if (sonuc.yeniSira && sonuc.siraBende && !ilk) {
        bildir('Sıra sende! Yürümek için haritaya, saldırmak ya da büyü yapmak için bir karaktere tıkla.', 'ok');
        if (dar.matches && !$('#uygulama').classList.contains('show-yan')) rozet(Math.max(okunmamis, 1));
      }
      siraBende = sonuc.siraBende;
      $('#ac-yan').classList.toggle('btn-primary', siraBende || Boolean(m.ben.gm && m.istek));
      if (!m.ben.gm) {
        durumKartiCiz($('#durum-karti'), m);
        envanterCiz($('#envanter-liste'), m.envanter, {
          azami: m.envanter_azami,
          birak: m.ben.yerde && !m.olu ? (sira, e) => gonder({ t: 'esya_yere', sira, ad: e.ad }) : null,
          imha: (sira, e) => {
            ses.kir();
            gonder({ t: 'esya_imha', sira, ad: e.ad });
          },
        });
        $('#envanter-sayi').textContent = `(${m.envanter.length}/${m.envanter_azami})`;
      }
      gm?.ayarla(m);
      takas.ayarla(m);
      if (gorunumAcik?.kapandi()) gorunumAcik = null;
      gorunumAcik?.yenile();
      baslikGuncelle();
      bosDurum();
      kimlerCiz();
      break;
    }
    case 'kayit':
      kayit.ekle(m.g, true);
      if (dar.matches && !$('#uygulama').classList.contains('show-yan')) rozet(okunmamis + 1);
      break;
    case 'kayitlar':
      kayit.sifirla(m.g);
      break;
    case 'isaret':
      harita.isaretGoster(m);
      break;
    case 'yuru':
      harita.yuruOlayi(m);
      break;
    case 'saldiri':
      harita.saldiriOlayi(m);
      break;
    case 'olum':
      harita.olumOlayi(m);
      break;
    case 'portal':
      harita.portalOlayi(m);
      break;
    case 'ganimet':
      ganimetGoster(m);
      break;
    case 'portal_eklendi':
      gm?.portalEklendi(m.id);
      break;
    case 'hata':
      bildir(m.metin);
      break;
    case 'bilgi':
      if (m.metin === 'Takas tamamlandı.') ses.para();
      bildir(m.metin, 'ok');
      break;
    case 'npc_eklendi':
      gm?.npcEklendi(m.id);
      break;
    default:
      break;
  }
}

// ------------------------------------------------------------ başlangıç

/* 32x32 ve daha küçük resimler (ör. 16x16 piksel eşya simgeleri) büyütülünce bulanıklaşmasın, pikselleri keskin kalsın */
document.addEventListener(
  'load',
  (e) => {
    const r = e.target;
    if (r instanceof HTMLImageElement && r.naturalWidth > 0 && r.naturalWidth <= 32 && r.naturalHeight <= 32) r.classList.add('is-pixel');
  },
  true,
);

async function basla() {
  try {
    bilgi = await istek('/api/bilgi');
  } catch {
    /* sunucuya ulaşılamazsa bağlanırken tekrar denenecek */
  }
  document.title = bilgi.oyun_adi;
  try {
    await istek(`/api/oturum?rol=${rol}`);
    baglan();
  } catch {
    girisGoster();
  }
}

basla();
