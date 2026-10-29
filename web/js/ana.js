import { $, h, istek, bildir, dosyaAdi } from './ortak.js';
import { haritaOlustur } from './harita.js';
import { kayitOlustur } from './kayit.js';
import { turKartiOlustur, durumKartiCiz } from './tur.js';
import { envanterCiz, esyaKarti, grupla } from './envanter.js';
import { secimPenceresi } from './pencere.js';
import { zarSimgesi } from './zar-sekilleri.js';

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
let harita, kayit, tur, gm, dosyalar;

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

// ------------------------------------------------------------ saldırı seçimi (oyuncu)

async function saldiriSec(hedefId) {
  if (!durum || durum.ben.gm) return;
  const hedef = harita.token(hedefId);
  if (!hedef) return;
  const s = durum.sira;
  if (!s.aktif) return bildir('Turlar başlamadı; sıran gelince saldırabilirsin.');
  if (!s.sirada_ben) return bildir('Sıra sende değil.');
  if (durum.olu) return bildir('Ölüler saldıramaz.');
  if (hedef.olu) return bildir(`${hedef.ad} zaten ölü.`);
  if (!(durum.statlar.enerji > 0)) return bildir('Enerjin kalmadı.');
  if (!durum.envanter.length) return bildir('Saldırmak için envanterinde bir eşya olmalı. GM sana eşya verebilir.');
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
  harita.on('saldir', (id) => saldiriSec(id));
  harita.on('istekTasi', () => gm?.istekTasindi());

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
    if (b) gonder({ t: 'zar', ifade: b.dataset.roll, gizli: $('#zar-gizli').checked });
  });
  $('#zar-formu').addEventListener('submit', (e) => {
    e.preventDefault();
    const ifade = $('#zar-ifade').value.trim();
    if (!ifade) return $('#zar-ifade').focus();
    gonder({ t: 'zar', ifade, gizli: $('#zar-gizli').checked });
    $('#zar-ifade').select();
  });
  $('#gizli-etiket').title =
    rol === 'gm' ? 'Sonucu yalnızca GM görür; oyuncular gizli bir zar atıldığını görür.' : 'Sonucu yalnızca sen ve GM görürsünüz; diğerleri gizli bir zar atıldığını görür.';

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
      gm = gmPaneliOlustur({ gonder, harita });
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
    $('#bos-baslik').textContent = 'Henüz açık bir harita yok';
    $('#bos-metin').textContent = 'GM bir harita açtığında burada belirecek. Bu arada zar atabilirsin.';
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

// GM'in durumunda tokenler karakter listesinden gelir
function normallestir(m) {
  if (m.ben.gm) m.tokenler = m.harita ? m.karakterler.map((k) => ({ ...k, canli: true, ben: false })) : [];
  return m;
}

function istekBildirimi(m) {
  if (!m.ben.gm) return;
  const i = m.istek;
  if (durum && i && i.id !== sonIstek) {
    const kim = m.karakterler.find((k) => k.id === i.kim);
    bildir(i.tur === 'hareket' ? `${kim?.ad || '?'} yürümek istiyor.` : `${kim?.ad || '?'} → ${i.hedef_ad}: saldırı isteği.`, 'ok');
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
        bildir('Sıra sende! Yürümek için haritaya, saldırmak için bir karaktere tıkla.', 'ok');
        if (dar.matches && !$('#uygulama').classList.contains('show-yan')) rozet(Math.max(okunmamis, 1));
      }
      siraBende = sonuc.siraBende;
      $('#ac-yan').classList.toggle('btn-primary', siraBende || Boolean(m.ben.gm && m.istek));
      if (!m.ben.gm) {
        durumKartiCiz($('#durum-karti'), m);
        envanterCiz($('#envanter-liste'), m.envanter);
        $('#envanter-sayi').textContent = m.envanter.length ? `(${m.envanter.length})` : '';
      }
      gm?.ayarla(m);
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
    case 'hata':
      bildir(m.metin);
      break;
    case 'bilgi':
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
