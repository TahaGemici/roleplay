import { h, svg, sinirla, basHarfler, yaziRengi, azHareket, sayiYaz } from './ortak.js';
import { sisOlustur } from './sis.js';
import { duvarlarKur, yolNoktasi, yolaUzaklik } from './duvar.js';
import { saldiriOynat, olumOynat, portalOynat } from './efekt.js';
import { ses } from './ses.js';

const EN_KUCUK = 0.05;
const EN_BUYUK = 6;
const TIKLAMA_PAYI = 6; // bu kadar pikselden az kayan dokunuş tıklamadır
const UZUN_BASMA_MS = 550;
const YURUME_MS_KARE = 170;
const BELIRME = 0.3; // görüşe giren ya da görüşten çıkan yürüyen bu kadar karede belirir / kaybolur
const UCA_YAPIS_PX = 12; // duvar çizerken bu kadar ekran pikseli yakındaki duvar ucuna yapışır
const DUVARA_YAPIS_PX = 8;

const yumusak = (p) => (p < 0.5 ? 2 * p * p : 1 - (-2 * p + 2) ** 2 / 2);

/**
 * Harita görünümü. Konumlar kare birimindedir: (0,0) haritanın sol üst köşesi, (0.5, 0.5) ilk karenin ortası.
 * Harita, GM'in seçtiği genişlik × yükseklik kadar kareye bölünür; resim bu alana gerilir.
 * Oyuncu (yalnızca kendi sırasında): bir yere tıklayınca yürüme isteği, bir karaktere (ya da kendine) tıklayınca
 * saldırı / büyü seçimi açılır; ikisi de GM'e gider.
 * GM: her tokenı ve yerdeki eşyaları sürükleyebilir, bekleyen yürüme isteğinin hedefini sürükleyerek düzeltebilir.
 * GM duvar çizer: duvarlar görüşü keser, üstünden geçilmez; yürüyüş duvarların etrafından dolaşır ve yolu çizilir.
 * Oyuncu yalnızca gördüğü duvarları bilir; yolu da onlarla hesaplanır (bilmediği bir duvar yolunu uzatabilir).
 * Pulun boyu karakterin yüküyle büyür; resmi ve şapkası varsa üstünde görünür.
 * Sağ tık (dokunmatikte uzun basma) herkese işaret gösterir.
 */
export function haritaOlustur(el) {
  const { gorus, dunya, resim, izgara, duvarKatmani, portalKatmani, yerKatmani, tokenKatmani, sisTuvali, katman, etiketler, efektKatmani, isaretKatmani, yakinlik } = el;
  const gor = { olcek: 1, tx: 0, ty: 0 };
  let durum = null;
  let harita = null; // görünür harita (gizliyse null)
  let boyut = { w: 0, h: 0 };
  let yuklenenUrl = null;
  let sigdirBekliyor = false;
  const tokenler = new Map(); // id → token
  let secili = null;
  let onizleme = null; // oyuncunun imleçle gösterdiği hedef
  let hedefUstunde = null; // imlecin üstünde durduğu saldırılabilir karakter
  const istekAyar = new Map(); // GM: istek kimliği → düzeltilmiş hedef
  let halkalar = true;
  const dinleyici = { yuru() {}, yolYok() {}, tasi() {}, sec() {}, isaret() {}, etkiles() {}, istekTasi() {}, esyaTasi() {}, esyaTikla() {}, portalTasi() {}, portalTikla() {}, duvarEkle() {}, duvarSil() {} };
  const yerdekiler = new Map(); // yerdeki eşyalar: id → { veri, el }
  const portallar = new Map(); // portallar: id → { veri, el }
  let bekleyenDurum = null; // portaldan geçerken yeni harita, kendi yürüyüşüm bitince açılır
  const sonSaldiri = new Map(); // id → saldırı animasyonunun bittiği an (ölüm ondan sonra oynasın)
  const sis = sisOlustur(sisTuvali);
  let sonHatira = null; // sunucunun en son gönderdiği keşif ızgarası (kendi yürüyüşüm sürerken gelen, pul varınca açılır)
  let duvarlar = []; // [[id, x1, y1, x2, y2], ...] kare cinsinden (oyuncu: bildiği duvarlar)
  let duvarImza = '';
  let dizin = duvarlarKur([], 1, 1); // duvarların geometrisi: görüş ve yol
  let duvarKipi = null; // GM: 'ciz' | 'sil' | null
  let duvarGoster = true;
  let izgaraYapis = true;
  let zincir = null; // çizilmekte olan duvarın son noktası
  let fareNoktasi = null; // imlecin (yapışmış) yeri, duvar çizerken
  let silinecek = null; // silme kipinde imlecin üstündeki duvar
  let shift = false;

  const ben = () => durum?.ben;
  const gm = () => Boolean(ben()?.gm);
  let kare = 70; // bir karenin dünya pikseli: resmin genişliği / haritanın kare cinsinden genişliği
  const px = (x, y) => ({ x: x * kare, y: y * kare });
  const kareye = (wx, wy) => ({ x: wx / kare, y: wy / kare });

  // ------------------------------------------------------------ görünüm

  function uygula() {
    dunya.style.transform = `translate(${gor.tx}px, ${gor.ty}px) scale(${gor.olcek})`;
    dunya.style.setProperty('--inv', String(1 / gor.olcek));
  }
  const kutu = () => gorus.getBoundingClientRect();

  /* Oyuncu sisli haritada haritanın ne kadar büyük olduğunu bilmez: "sığdır" kendi pulunu görüş menziline göre ortalar */
  const siskapali = () => gm() || !harita?.izgara?.sis;

  function sigdir() {
    const r = kutu();
    if (!boyut.w || r.width < 10 || r.height < 10) {
      sigdirBekliyor = true;
      return;
    }
    sigdirBekliyor = false;
    if (!siskapali()) {
      const t = benimTokenim();
      const capi = Math.max(2, menzil()) * 2.6 * kare;
      gor.olcek = sinirla(Math.min(r.width, r.height) / capi, EN_KUCUK, EN_BUYUK);
      const p = t ? px(t.goster.x, t.goster.y) : { x: boyut.w / 2, y: boyut.h / 2 };
      merkezle(p.x, p.y);
      return;
    }
    const s = sinirla(Math.min(r.width / boyut.w, r.height / boyut.h) * 0.94, EN_KUCUK, EN_BUYUK);
    gor.olcek = s;
    gor.tx = (r.width - boyut.w * s) / 2;
    gor.ty = (r.height - boyut.h * s) / 2;
    uygula();
  }

  function yakinlastir(kat, cx, cy) {
    const yeni = sinirla(gor.olcek * kat, EN_KUCUK, EN_BUYUK);
    const wx = (cx - gor.tx) / gor.olcek;
    const wy = (cy - gor.ty) / gor.olcek;
    gor.olcek = yeni;
    gor.tx = cx - wx * yeni;
    gor.ty = cy - wy * yeni;
    uygula();
  }
  const ortadanYakinlastir = (kat) => {
    const r = kutu();
    yakinlastir(kat, r.width / 2, r.height / 2);
  };

  function dunyaya(cx, cy) {
    const r = kutu();
    return { x: (cx - r.left - gor.tx) / gor.olcek, y: (cy - r.top - gor.ty) / gor.olcek };
  }
  function merkezle(wx, wy) {
    const r = kutu();
    gor.tx = r.width / 2 - wx * gor.olcek;
    gor.ty = r.height / 2 - wy * gor.olcek;
    uygula();
  }

  new ResizeObserver(() => {
    if (sigdirBekliyor) sigdir();
  }).observe(gorus);

  // ------------------------------------------------------------ durum

  function ayarla(yeni) {
    /* Portaldan geçerken: kendi yürüyüşüm bitmeden yeni harita açılmasın */
    if (!yeni.ben?.gm && harita && (bekleyenDurum || (yeni.harita?.dosya !== harita.dosya && benimTokenim()?.anim))) {
      bekleyenDurum = yeni;
      return;
    }
    const portaldan = !yeni.ben?.gm && harita && yeni.harita?.dosya && yeni.harita.dosya !== harita.dosya;
    durum = yeni;
    const hr = yeni.harita?.url ? yeni.harita : null;
    if (!hr) {
      harita = null;
      yuklenenUrl = null;
      boyut = { w: 0, h: 0 };
      dunya.hidden = true;
      yakinlik.hidden = true;
      resim.removeAttribute('src');
      tokenleriKur([]);
      yerdekileriKur([], true);
      portallariKur([]);
      duvarlariAyarla([]);
      gorus.classList.remove('is-fog');
      sis.ayarla({ acik: false });
      sonHatira = null;
      onizleme = null;
      zincir = null;
      katmanCiz();
      return;
    }
    const yeniDosya = !harita || harita.dosya !== hr.dosya;
    const ilk = !boyut.w || yeniDosya;
    harita = hr;
    dunya.hidden = false;
    yakinlik.hidden = false;
    izgaraUygula();
    if (yuklenenUrl !== hr.url) {
      yuklenenUrl = hr.url;
      if (ilk) {
        dunya.classList.add('is-loading');
        sigdir();
      }
      const url = hr.url;
      resim.onload = () => {
        if (yuklenenUrl !== url) return;
        dunya.classList.remove('is-loading');
      };
      resim.onerror = () => dunya.classList.remove('is-loading');
      resim.src = url;
    }
    if (yeniDosya) {
      for (const t of [...tokenler.values()]) tokenSil(t, true);
      istekAyar.clear();
      zincir = null;
      sonHatira = null;
    }
    /* Oyuncuya duvarlar yalnızca değişince gelir (gördükçe öğrenir); GM'e her zaman */
    if (yeni.duvarlar) duvarlariAyarla(yeni.duvarlar);
    else if (yeniDosya) duvarlariAyarla([]);
    gorus.classList.toggle('is-fog', !siskapali());
    yakinlik.querySelector('[data-zoom="fit"]')?.setAttribute('title', siskapali() ? 'Haritayı ekrana sığdır' : 'Kendi puluna dön');
    tokenleriKur(yeni.tokenler || []);
    yerdekileriKur(yeni.yerdekiler || [], yeniDosya);
    portallariKur(yeni.portallar || []);
    sisAyarla(yeni);
    if (yeniDosya && !siskapali()) sigdir(); /* kendi pulumu ortala */
    if (portaldan) {
      ses.portal();
      const t = benimTokenim();
      if (t) portalOynat(efektBaglami, px(t.goster.x, t.goster.y), true);
    }
    for (const id of istekAyar.keys()) if (yeni.istek?.id !== id) istekAyar.delete(id);
    if (!hareketEdebilir()) onizleme = null;
    katmanCiz();
    imlec();
  }

  /* Dünya: resmin genişliği kadar piksel; yüksekliği haritanın kare oranından gelir (resim gerilir). */
  function izgaraUygula() {
    const g = harita.izgara;
    kare = harita.gen / harita.genislik;
    boyut = { w: harita.gen, h: harita.yukseklik * kare };
    dunya.style.width = `${boyut.w}px`;
    dunya.style.height = `${boyut.h}px`;
    dunya.style.setProperty('--cell', `${kare}px`);
    izgara.hidden = !g.gorunur;
    izgara.classList.toggle('is-light', g.acik);
    izgara.style.opacity = String(g.opaklik);
    for (const k of [katman, duvarKatmani]) {
      k.setAttribute('viewBox', `0 0 ${boyut.w} ${boyut.h}`);
      k.setAttribute('width', String(boyut.w));
      k.setAttribute('height', String(boyut.h));
    }
    if (kare !== cizilenKare) {
      cizilenKare = kare;
      halkaOnbellegi.clear();
      duvarlariCiz();
    }
  }
  let cizilenKare = 0;

  // ------------------------------------------------------------ görüş sisi (yalnızca oyuncu)

  function sisAyarla(d) {
    const acik = !gm() && Boolean(harita?.izgara?.sis);
    sis.ayarla({ acik, genislik: boyut.w, yukseklik: boyut.h, kare });
    if (!acik) return;
    if (d.kesif) {
      sonHatira = d.kesif;
      /* Kendi yürüyüşüm sürerken gelen keşif, pul varınca açılır (görüşüm oraya ulaşmadan sis kalkmasın) */
      if (!benimTokenim()?.anim) sis.hatiraAyarla(d.kesif);
    }
    sisGorusu();
  }

  const menzil = () => durum?.gorus ?? 0;

  /* (x, y)'den görülen alan: menzil içinde, duvarların önü */
  const gorusAlani = (x, y) => dizin.gorusCokgeni(x, y, menzil());

  function sisGorusu() {
    if (!sis.etkin) return;
    const t = benimTokenim();
    if (!t) return sis.gorus(0, 0, 0, []);
    sis.gorus(t.goster.x, t.goster.y, menzil(), gorusAlani(t.goster.x, t.goster.y));
  }

  // ------------------------------------------------------------ duvarlar

  /* Duvarlar ya da harita boyutu değişince geometri yeniden kurulur */
  function duvarlariAyarla(liste) {
    const imza = `${harita?.genislik}x${harita?.yukseklik}:${JSON.stringify(liste)}`;
    if (imza === duvarImza) return;
    duvarImza = imza;
    duvarlar = liste.map(([id, x1, y1, x2, y2]) => [id, x1 / 1000, y1 / 1000, x2 / 1000, y2 / 1000]);
    dizin = duvarlarKur(
      duvarlar.map((w) => w.slice(1)),
      harita?.genislik || 1,
      harita?.yukseklik || 1,
    );
    if (silinecek !== null && !duvarlar.some((w) => w[0] === silinecek)) silinecek = null;
    halkaOnbellegi.clear();
    duvarlariCiz();
  }

  /* GM'in gördüğü duvarlar (oyunculara çizilmez: harita resminde zaten vardır) */
  function duvarlariCiz() {
    duvarKatmani.replaceChildren();
    const goster = gm() && harita && (duvarGoster || duvarKipi);
    duvarKatmani.classList.toggle('is-editing', Boolean(duvarKipi));
    if (!goster) return;
    const g = svg('g', {});
    for (const [id, x1, y1, x2, y2] of duvarlar) {
      const a = px(x1, y1);
      const b = px(x2, y2);
      g.append(svg('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: `wall${silinecek === id ? ' is-hover' : ''}` }));
    }
    if (duvarKipi) {
      const uclar = new Set();
      for (const [, x1, y1, x2, y2] of duvarlar)
        for (const [x, y] of [
          [x1, y1],
          [x2, y2],
        ]) {
          const k = `${x},${y}`;
          if (uclar.has(k)) continue;
          uclar.add(k);
          const p = px(x, y);
          g.append(svg('circle', { cx: p.x, cy: p.y, r: 4, class: 'wall-end' }));
        }
    }
    duvarKatmani.append(g);
  }

  /* Duvar çizerken imlecin yapışacağı yer: yakındaki duvar ucu, (açıksa) ızgaranın köşesi, yakındaki duvarın üstü */
  function yapistir(q) {
    const ekranKare = kare * gor.olcek; // bir karenin ekran pikseli
    const sinirli = { x: sinirla(q.x, 0, harita.genislik), y: sinirla(q.y, 0, harita.yukseklik) };
    if (shift) return sinirli;
    let en = UCA_YAPIS_PX / ekranKare;
    let uc = null;
    for (const [, x1, y1, x2, y2] of duvarlar)
      for (const [x, y] of [
        [x1, y1],
        [x2, y2],
      ]) {
        const d = Math.hypot(x - q.x, y - q.y);
        if (d < en) {
          en = d;
          uc = { x, y };
        }
      }
    if (uc) return uc;
    if (izgaraYapis) return { x: sinirla(Math.round(q.x), 0, Math.floor(harita.genislik)), y: sinirla(Math.round(q.y), 0, Math.floor(harita.yukseklik)) };
    en = DUVARA_YAPIS_PX / ekranKare;
    let ust = null;
    for (const [, x1, y1, x2, y2] of duvarlar) {
      const dx = x2 - x1;
      const dy = y2 - y1;
      const L = dx * dx + dy * dy;
      const t = L ? sinirla(((q.x - x1) * dx + (q.y - y1) * dy) / L, 0, 1) : 0;
      const p = { x: x1 + t * dx, y: y1 + t * dy };
      const d = Math.hypot(p.x - q.x, p.y - q.y);
      if (d < en) {
        en = d;
        ust = p;
      }
    }
    return ust || sinirli;
  }

  /* İmlecin üstündeki duvar (silme kipi) */
  function duvarBul(q) {
    let en = DUVARA_YAPIS_PX / (kare * gor.olcek);
    let bulunan = null;
    for (const [id, x1, y1, x2, y2] of duvarlar) {
      const d = yolaUzaklik([x1, y1, x2, y2], q.x, q.y);
      if (d < en) {
        en = d;
        bulunan = id;
      }
    }
    return bulunan;
  }

  /* Duvar kipinde haritaya tıklandı */
  function duvarTiklandi(cx, cy) {
    const p = dunyaya(cx, cy);
    const q = kareye(p.x, p.y);
    if (duvarKipi === 'sil') {
      const id = duvarBul(q);
      if (id !== null) dinleyici.duvarSil(id);
      return;
    }
    const n = yapistir(q);
    if (zincir && Math.hypot(zincir.x - n.x, zincir.y - n.y) < 1e-6) {
      zincir = null; /* aynı noktaya ikinci kez: çizgi biter */
    } else {
      if (zincir) dinleyici.duvarEkle(zincir.x, zincir.y, n.x, n.y);
      zincir = n;
    }
    fareNoktasi = n;
    katmanCiz();
  }

  function zinciriBitir() {
    if (!zincir) return false;
    zincir = null;
    katmanCiz();
    return true;
  }

  const halkaOnbellegi = new Map(); // GM: oyuncuların görüş alanları (konum → çokgen)

  // ------------------------------------------------------------ tokenler

  const benimTokenim = () => (!gm() && ben()?.id ? tokenler.get(ben().id) : null);

  function yeniToken(veri) {
    const harfler = h('span', { class: 'token-initials' });
    const foto = h('img', { class: 'token-img', alt: '', draggable: 'false', hidden: true });
    const sapka = h('img', { class: 'token-hat', alt: '', draggable: 'false', hidden: true });
    const disk = h('div', { class: 'token-disc' }, harfler, foto);
    const govde = h('div', { class: 'token-body' }, disk, sapka);
    const isim = h('span', { class: 'token-name' });
    const t = {
      veri,
      el: h('div', { class: 'token', dataset: { id: veri.id } }, govde, isim),
      govde,
      disk,
      harfler,
      foto,
      sapka,
      isim,
      goster: { x: veri.x, y: veri.y },
      anim: null,
      sonraki: null,
      kaldir: false,
      bekle: false,
      oluyor: false,
      birakildi: null,
    };
    tokenler.set(veri.id, t);
    tokenKatmani.append(t.el);
    return t;
  }

  function tokenSil(t, hemen = false) {
    tokenler.delete(t.veri.id);
    animler.delete(t);
    for (const y of t.alinacak || []) y.el.remove();
    t.alinacak = null;
    if (secili === t.veri.id) sec(null, true);
    if (hemen || azHareket()) {
      t.el.remove();
      return;
    }
    t.el.classList.add('is-leaving');
    setTimeout(() => t.el.remove(), 260);
  }

  function tokenleriKur(liste) {
    const gorulen = new Set();
    const yuruyorum = Boolean(benimTokenim()?.anim);
    for (const veri of liste) {
      gorulen.add(veri.id);
      let t = tokenler.get(veri.id);
      if (!t) {
        t = yeniToken(veri);
        if (yuruyorum && !veri.ben && sis.etkin) {
          /* Kendi yürüyüşümde görünür olan: görüşüm ona ulaşınca belirsin */
          t.bekle = true;
          t.el.classList.add('is-waiting');
        }
      }
      t.kaldir = false;
      if (t.anim || t.oluyor) {
        t.sonraki = veri;
        continue;
      }
      if (t.veri.olu && !veri.olu && t.veri.canli !== false && veri.canli !== false) ses.iyiles(); /* dirildi: ayağa kalkar */
      t.veri = veri;
      if (surukle?.id !== veri.id) t.goster = { x: veri.x, y: veri.y };
      tokenCiz(t);
    }
    for (const t of [...tokenler.values()]) {
      if (gorulen.has(t.veri.id)) continue;
      if (t.anim || t.oluyor) {
        t.kaldir = true;
        continue;
      }
      tokenSil(t);
    }
  }

  const olcek = (veri) => (Number.isFinite(veri?.olcek) ? sinirla(veri.olcek, 0.5, 2.5) : 1);

  function konumla(t) {
    const p = px(t.goster.x, t.goster.y);
    const b = kare * olcek(t.veri);
    t.el.style.transform = `translate(${p.x - b / 2}px, ${p.y - b / 2}px)`;
  }

  function siradakiId() {
    const s = durum?.sira;
    if (!s?.aktif) return null;
    if (gm()) return s.sirada;
    return s.sirada_ben ? ben().id : s.sirada?.id || null;
  }

  /* Değişmediyse yeniden yüklemesin */
  function resimAyarla(img, url) {
    img.hidden = !url;
    if (!url) img.removeAttribute('src');
    else if (img.getAttribute('src') !== url) img.src = url;
  }

  function tokenCiz(t) {
    const { veri, el } = t;
    el.style.setProperty('--ts', `${kare * olcek(veri)}px`);
    el.style.setProperty('--tc', veri.renk);
    el.style.setProperty('--ti', yaziRengi(veri.renk));
    konumla(t);
    t.harfler.textContent = basHarfler(veri.ad);
    resimAyarla(t.foto, veri.resim || null);
    resimAyarla(t.sapka, veri.sapka || null);
    el.classList.toggle('has-img', Boolean(veri.resim));
    t.isim.textContent = veri.gizli ? `${veri.ad} (gizli)` : veri.ad;
    const anilar = veri.canli === false;
    el.classList.toggle('is-npc', Boolean(veri.npc));
    el.classList.toggle('is-mine', Boolean(veri.ben));
    el.classList.toggle('is-turn', siradakiId() === veri.id && !veri.olu);
    el.classList.toggle('is-offline', !veri.npc && veri.cevrimici === false);
    el.classList.toggle('is-hidden', Boolean(veri.gizli));
    el.classList.toggle('is-dead', Boolean(veri.olu) && !t.oluyor);
    el.classList.toggle('is-memory', anilar);
    el.classList.toggle('is-movable', gm());
    el.classList.toggle('is-selected', secili === veri.id);
    el.classList.toggle('is-target', hedefUstunde === veri.id);
    el.title = anilar ? `${veri.ad} (en son burada görüldü)` : veri.npc ? `${veri.ad} (NPC)` : veri.ad;
  }

  function sec(id, sessiz = false) {
    const yeni = id && tokenler.has(id) ? id : null;
    if (yeni === secili) return;
    const eski = secili;
    secili = yeni;
    if (eski && tokenler.has(eski)) tokenCiz(tokenler.get(eski));
    if (yeni) tokenCiz(tokenler.get(yeni));
    if (!sessiz) dinleyici.sec(yeni);
  }

  function odakla(id) {
    const t = tokenler.get(id);
    if (!t) return;
    const p = px(t.goster.x, t.goster.y);
    merkezle(p.x, p.y);
  }

  // ------------------------------------------------------------ yerdeki eşyalar

  function yerEsyasiCiz(y) {
    const e = y.veri.esya;
    const b = kare * 0.62;
    y.el.style.setProperty('--gs', `${b}px`);
    const p = px(y.goster.x, y.goster.y);
    y.el.style.transform = `translate(${p.x - b / 2}px, ${p.y - b / 2}px)`;
    y.el.title = y.veri.hatira ? `${e.ad} (en son burada gördün)` : e.sinif ? `${e.ad} (${e.sinif})` : e.ad;
    y.el.classList.toggle('is-movable', gm());
    y.el.classList.toggle('is-memory', Boolean(y.veri.hatira));
    const url = e.url && e.gorsel && e.var !== false ? e.url : null;
    resimAyarla(y.img, url);
    y.yazi.hidden = Boolean(url);
    y.yazi.textContent = basHarfler(e.ad);
  }

  /* Görünen bir eşya yerden kalkınca (biri aldı) ses çıkar; harita değişirken sessiz. Yürüyen biri yolu üstünde
     aldıysa eşya, pul oraya varınca kalkar. */
  function yerdekileriKur(liste, sessiz = false) {
    const gorulen = new Set();
    for (const veri of liste) {
      gorulen.add(veri.id);
      let y = yerdekiler.get(veri.id);
      if (!y) {
        const img = h('img', { alt: '', draggable: 'false', hidden: true });
        const yazi = h('span', { class: 'ground-text' });
        y = { veri, img, yazi, el: h('div', { class: 'ground-item', dataset: { yer: String(veri.id) } }, img, yazi) };
        yerdekiler.set(veri.id, y);
        yerKatmani.append(y.el);
      }
      y.veri = veri;
      if (yerSurukle?.id !== veri.id) y.goster = { x: veri.x, y: veri.y };
      yerEsyasiCiz(y);
    }
    let alindi = false;
    for (const [id, y] of [...yerdekiler]) {
      if (gorulen.has(id)) continue;
      yerdekiler.delete(id);
      const alan = sessiz || y.veri.hatira ? null : [...animler].find((t) => yolUstunde(t.anim, y.veri.x, y.veri.y));
      if (alan) {
        alan.alinacak = [...(alan.alinacak || []), y];
        continue;
      }
      if (!y.veri.hatira) alindi = true;
      y.el.remove();
    }
    if (alindi && !sessiz) ses.para();
  }

  // ------------------------------------------------------------ portallar

  function portalCiz(p) {
    const b = kare * 0.9;
    p.el.style.setProperty('--ps', `${b}px`);
    const q = px(p.goster.x, p.goster.y);
    p.el.style.transform = `translate(${q.x - b / 2}px, ${q.y - b / 2}px)`;
    p.el.classList.toggle('is-movable', gm());
    p.el.classList.toggle('is-unlinked', gm() && !p.veri.hedef);
    p.isim.textContent = gm() ? p.veri.ad : '';
    p.el.title = gm() ? `${p.veri.ad}${p.veri.hedef ? '' : ' (bir yere bağlı değil)'}` : 'Portal';
  }

  function portallariKur(liste) {
    const gorulen = new Set();
    for (const veri of liste) {
      gorulen.add(veri.id);
      let p = portallar.get(veri.id);
      if (!p) {
        const isim = h('span', { class: 'portal-name' });
        p = { veri, isim, el: h('div', { class: 'portal', dataset: { portal: String(veri.id) } }, h('span', { class: 'portal-swirl' }), isim) };
        portallar.set(veri.id, p);
        portalKatmani.append(p.el);
      }
      p.veri = veri;
      if (portalSurukle?.id !== veri.id) p.goster = { x: veri.x, y: veri.y };
      portalCiz(p);
    }
    for (const [id, p] of [...portallar]) {
      if (gorulen.has(id)) continue;
      p.el.remove();
      portallar.delete(id);
    }
  }

  /** Biri portaldan geçti: o noktada portal efekti (geçen yürüyorsa yürüyüşü bitince) */
  function portalOlayi(m) {
    if (!harita) return;
    const t = tokenler.get(m.id);
    const oynat = () => {
      ses.portal();
      portalOynat(efektBaglami, px(m.x, m.y), m.varis);
    };
    if (t?.anim && !m.varis) t.sonrakiEfekt = oynat;
    else oynat();
  }

  // ------------------------------------------------------------ yürüme animasyonu

  const animler = new Set();
  let rafNo = 0;

  /* a: { parcalar: [{ bas, son, xy }], L, sure, gir, cik }. Yürüyen L uzunluğundaki yolu sure ms'de yürür; bu ekran yalnızca
     parçaları görür (uzunluk cinsinden bas..son), aralarda pul görünmez. */
  function animBaslat(t, a) {
    t.anim = { ...a, bas: performance.now() };
    ses.yurume(0, a.sure, t === benimTokenim() ? 1 : 0.5);
    t.el.classList.add('is-walking');
    if (a.gir) t.el.style.opacity = '0';
    animler.add(t);
    if (!rafNo) rafNo = requestAnimationFrame(adim);
  }

  /* Yürüyüşün s uzunluğundaki yeri ve ne kadar göründüğü (görülen parçaların dışında null) */
  function animKonumu(a, s) {
    for (const pr of a.parcalar) {
      if (s < pr.bas - 1e-9 || s > pr.son + 1e-9) continue;
      const k = yolNoktasi(pr.xy, s - pr.bas);
      let o = 1;
      if (pr.bas > 1e-6) o = Math.min(o, (s - pr.bas) / BELIRME);
      if (pr.son < a.L - 1e-6) o = Math.min(o, (pr.son - s) / BELIRME);
      return { x: k.x, y: k.y, opaklik: sinirla(o, 0, 1) };
    }
    return null;
  }

  function adim(simdi) {
    for (const t of [...animler]) {
      const a = t.anim;
      const p = Math.min(1, (simdi - a.bas) / a.sure);
      const k = animKonumu(a, a.L * yumusak(p));
      if (k) {
        t.goster = { x: k.x, y: k.y };
        konumla(t);
        t.el.style.opacity = k.opaklik < 1 ? String(k.opaklik) : '';
      } else {
        t.el.style.opacity = '0';
      }
      if (t.alinacak) alinanlariKaldir(t, false);
      if (t === benimTokenim() && sis.etkin) {
        /* Görüşümün değdiği her yer hemen hatırlanır */
        const c = gorusAlani(t.goster.x, t.goster.y);
        sis.yol(c);
        sis.gorus(t.goster.x, t.goster.y, menzil(), c);
        bekleyenleriGoster(t.goster);
      }
      if (p >= 1) animBitir(t, true);
    }
    rafNo = animler.size ? requestAnimationFrame(adim) : 0;
    if (onizleme || durum?.istek) katmanCiz();
  }

  /* Yürüyüşün (görülen parçalarının) bir noktaya uzaklığı yeterince az mı (kare) */
  function yolUstunde(a, x, y) {
    return a.parcalar.some((pr) => yolaUzaklik(pr.xy, x, y) <= 0.75);
  }

  /* Yürüyen pul yolundaki eşyaya varınca eşya kalkar (hepsi: yürüyüş bitti, kalanlar da kalkar) */
  function alinanlariKaldir(t, hepsi) {
    const kalan = t.alinacak.filter((y) => {
      if (!hepsi && Math.hypot(t.goster.x - y.veri.x, t.goster.y - y.veri.y) > 0.6) return true;
      y.el.remove();
      ses.para();
      return false;
    });
    t.alinacak = kalan.length ? kalan : null;
  }

  function animBitir(t, dogal) {
    animler.delete(t);
    const a = t.anim;
    t.anim = null;
    t.el.classList.remove('is-walking');
    t.el.style.opacity = '';
    if (dogal && a) {
      const son = a.parcalar[a.parcalar.length - 1];
      const k = yolNoktasi(son.xy, son.son - son.bas);
      t.goster = { x: k.x, y: k.y };
    }
    if (t.alinacak) alinanlariKaldir(t, true);
    if (t.sonrakiEfekt) {
      const fn = t.sonrakiEfekt;
      t.sonrakiEfekt = null;
      fn();
    }
    if (t === benimTokenim() && bekleyenDurum) {
      const d = bekleyenDurum;
      bekleyenDurum = null;
      animler.delete(t);
      t.anim = null;
      ayarla(d);
      return;
    }
    if (t.kaldir) {
      tokenSil(t, a?.cik);
      return;
    }
    if (t.sonraki) {
      t.veri = t.sonraki;
      t.sonraki = null;
    }
    t.goster = { x: t.veri.x, y: t.veri.y };
    tokenCiz(t);
    if (t === benimTokenim()) {
      /* Yürürken bildiğim duvarlarla boyadığım yerlerin yerine sunucunun hesabı (o bütün duvarları bilir) */
      if (sonHatira) sis.hatiraAyarla(sonHatira);
      bekleyenleriGoster(null);
      sisGorusu();
      katmanCiz();
      imlec();
    }
  }

  /* Kendi yürüyüşümde görünür olan tokenlar görüşüm onlara ulaşınca (menzil ve duvar) belirir; konum yoksa hepsi */
  function bekleyenleriGoster(konum) {
    for (const t of tokenler.values()) {
      if (!t.bekle) continue;
      if (konum && (Math.hypot(t.goster.x - konum.x, t.goster.y - konum.y) > menzil() || !dizin.gorusAcik(konum.x, konum.y, t.goster.x, t.goster.y))) continue;
      t.bekle = false;
      t.el.classList.remove('is-waiting');
    }
  }

  /* Biri yürüdü: m.uzunluk uzunluğundaki yolun bu ekranın gördüğü parçaları (m.parcalar: { bas, son, yol }) */
  function yuruOlayi(m) {
    if (!harita) return;
    const parcalar = (m.parcalar || []).filter((p) => p.yol?.length).map((p) => ({ bas: p.bas, son: p.son, xy: p.yol.flat() }));
    if (!parcalar.length) return;
    const ilk = parcalar[0];
    const son = parcalar[parcalar.length - 1];
    const L = Math.max(m.uzunluk || 0, son.son);
    const gir = ilk.bas > 1e-6;
    const cik = son.son < L - 1e-6;
    const varis = yolNoktasi(son.xy, son.son - son.bas);
    let t = tokenler.get(m.id);
    if (!t) {
      if (!gir && !gm()) return;
      t = yeniToken({ id: m.id, ad: m.ad, renk: m.renk, npc: m.npc, olu: m.olu, canli: true, x: ilk.xy[0], y: ilk.xy[1], resim: m.resim, sapka: m.sapka, olcek: m.olcek });
      tokenCiz(t);
    }
    if (t.birakildi && Math.hypot(t.birakildi.x - varis.x, t.birakildi.y - varis.y) < 0.02) {
      /* GM'in kendi sürükleyip bıraktığı yer: zaten orada */
      t.birakildi = null;
      t.goster = { ...varis };
      konumla(t);
      return;
    }
    t.birakildi = null;
    if (t.veri.canli === false) {
      /* Hatırasını gördüğü karakter yeniden görüşe yürüyor: artık canlı */
      t.veri = { ...t.veri, canli: true, x: varis.x, y: varis.y };
      tokenCiz(t);
    }
    /* Baştan görülen yürüyüş pulun ekrandaki yerinden başlar (önceki yürüyüşü yarıda kaldıysa sıçramasın) */
    if (!gir && Math.hypot(t.goster.x - ilk.xy[0], t.goster.y - ilk.xy[1]) < 2) ilk.xy.splice(0, 2, t.goster.x, t.goster.y);
    if (gir) t.goster = { x: ilk.xy[0], y: ilk.xy[1] };
    const sure = azHareket() ? 1 : sinirla(L * YURUME_MS_KARE, 260, 3600);
    animBaslat(t, { parcalar, L, sure, gir, cik });
  }

  // ------------------------------------------------------------ saldırı ve ölüm

  const efektBaglami = {
    katman: efektKatmani,
    boyut: () => kare,
    px,
    govde: (id) => tokenler.get(id)?.govde || null,
    merkez: (id) => {
      const t = tokenler.get(id);
      return t ? px(t.goster.x, t.goster.y) : null;
    },
  };

  function saldiriOlayi(m) {
    if (!harita) return;
    saldiriOynat(efektBaglami, m);
    /* Saldırı ya da büyüyle hemen ölen hedef, vuruş animasyonu bitince devrilsin */
    if (!azHareket()) sonSaldiri.set(m.hedef, performance.now() + 1150);
  }

  function olumOlayi(m) {
    const t = tokenler.get(m.id);
    if (!t || t.oluyor) return;
    t.oluyor = true;
    const bekle = (sonSaldiri.get(m.id) ?? 0) - performance.now();
    sonSaldiri.delete(m.id);
    if (bekle > 0) {
      setTimeout(() => devril(t, m.id), bekle);
      return;
    }
    devril(t, m.id);
  }

  function devril(t, id) {
    olumOynat(efektBaglami, id, () => {
      t.oluyor = false;
      if (t.kaldir) {
        tokenSil(t);
        return;
      }
      if (t.sonraki && !t.anim) {
        t.veri = t.sonraki;
        t.sonraki = null;
      }
      t.veri = { ...t.veri, olu: true };
      tokenCiz(t);
    });
  }

  // ------------------------------------------------------------ hareket hedefi ve katman

  /** Oyuncu yalnızca kendi sırasında, enerjisi varken yürüyüp saldırabilir. */
  function hareketEdebilir() {
    return !gm() && Boolean(harita && benimTokenim()) && !durum.olu && durum.sira.aktif && durum.sira.sirada_ben && (durum.statlar?.enerji ?? 0) > 0;
  }

  /** Kare koordinatını harita içine alır; "kare ortasına otursun" açıksa kare ortasına oturtur. Sisli haritada
      oyuncunun hedefi kenara yapışmaz (haritanın sınırını belli etmesin); sunucu yine de içeri alır. */
  function oturtulmus({ x, y }) {
    const sinirli = siskapali();
    const ortala = (v, ust) => (harita.izgara.oturt ? (sinirli ? Math.min(Math.floor(v), Math.max(0, Math.floor(ust) - 1)) : Math.floor(v)) + 0.5 : v);
    const al = (v, ust) => (sinirli ? sinirla(v, 0, ust) : v);
    return { x: ortala(al(x, harita.genislik), harita.genislik), y: ortala(al(y, harita.yukseklik), harita.yukseklik) };
  }

  /* A'dan B'ye bilinen duvarların etrafından dolaşan yol (harita içinde): { xy, uzunluk } ya da yol yoksa null */
  function yolBul(ax, ay, bx, by) {
    if (!harita) return null;
    const G = harita.genislik;
    const Y = harita.yukseklik;
    return dizin.yolBul(sinirla(ax, 0, G), sinirla(ay, 0, Y), sinirla(bx, 0, G), sinirla(by, 0, Y));
  }

  /** Oyuncunun imleç noktasına göre gideceği yer ve oraya giden yol (bildiği duvarların etrafından). Yol yoksa yok: true.
      Enerjiyi kurallar.c belirler. */
  function hedefHesapla(wx, wy) {
    const t = benimTokenim();
    if (!t || !harita) return null;
    const { x, y } = oturtulmus(kareye(wx, wy));
    if (Math.hypot(x - t.goster.x, y - t.goster.y) < 0.02) return null;
    const yol = yolBul(t.goster.x, t.goster.y, x, y);
    if (!yol) return { x, y, yok: true };
    const sonX = yol.xy[yol.xy.length - 2];
    const sonY = yol.xy[yol.xy.length - 1];
    return { x: sonX, y: sonY, yol: yol.xy, mesafe: yol.uzunluk };
  }

  function etiket(x, y, metin, sinif = '', resimUrl = null) {
    const e = h('div', { class: `map-label ${sinif}` }, h('div', { class: 'map-label-in' }, resimUrl ? h('img', { src: resimUrl, alt: '' }) : null, h('span', { text: metin })));
    e.style.transform = `translate(${x}px, ${y}px)`;
    etiketler.append(e);
  }

  function cizgi(g, a, b, sinif) {
    g.append(svg('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: sinif }));
  }

  /* Yol: kare cinsinden [x0, y0, x1, y1, ...] */
  function yolCiz(g, xy, sinif) {
    const nk = [];
    for (let i = 0; i + 1 < xy.length; i += 2) {
      const p = px(xy[i], xy[i + 1]);
      nk.push(`${p.x},${p.y}`);
    }
    g.append(svg('polyline', { points: nk.join(' '), class: sinif }));
  }

  /* Gidilemeyen hedef: kırmızı bir halka ve "Yol yok" */
  function yolYok(g, b, ek = '') {
    g.append(svg('circle', { cx: b.x, cy: b.y, r: kare * 0.42, class: `ghost is-blocked${ek}` }));
    etiket(b.x, b.y + kare * 0.5, 'Yol yok', 'is-blocked');
  }

  /* Bekleyen yürüme isteğinin yolu: GM sunucunun bulduğunu (hedefi kaydırdıysa yenisini), oyuncu bildiği duvarlarla
     kendi hesapladığını görür */
  function istekYolu(ist) {
    const kim = tokenler.get(ist.kim);
    if (!kim) return null;
    const ayar = gm() ? istekAyar.get(ist.id) : null;
    if (gm() && !ayar && ist.yol?.length) return { xy: ist.yol.flat(), uzunluk: ist.uzunluk, hedef: { x: ist.x2, y: ist.y2 } };
    const hedef = ayar || { x: ist.x2, y: ist.y2 };
    const yol = yolBul(kim.goster.x, kim.goster.y, hedef.x, hedef.y);
    return yol ? { ...yol, hedef } : { xy: null, uzunluk: null, hedef };
  }

  /* Bekleyen isteğin yolu ya da saldırı çizgisi. GM yürüme hedefini sürükleyerek kaydırabilir. */
  function istekCiz(g, ist) {
    const kim = tokenler.get(ist.kim);
    if (!kim) return;
    const a = px(kim.goster.x, kim.goster.y);
    const ek = gm() ? 'is-request' : 'is-pending';
    if (ist.tur === 'hareket') {
      const yol = istekYolu(ist);
      const b = px(yol.hedef.x, yol.hedef.y);
      if (yol.xy) yolCiz(g, yol.xy, `path-line ${ek}`);
      else g.append(svg('circle', { cx: b.x, cy: b.y, r: kare * 0.5, class: 'ghost is-blocked' }));
      const hayaletEl = svg('circle', { cx: b.x, cy: b.y, r: kare * 0.42, class: `ghost ${ek}`, 'data-istek': gm() ? String(ist.id) : null });
      hayaletEl.style.setProperty('--rc', kim.veri.renk); /* CSSOM: CSP satır içi style özniteliğine izin vermez */
      g.append(hayaletEl);
      const yazi = !gm() ? 'GM onayı bekleniyor' : yol.xy ? `${kim.veri.ad} · ${sayiYaz(yol.uzunluk)} kare` : `${kim.veri.ad} · yol yok`;
      etiket(b.x, b.y + kare * 0.5, yazi, yol.xy ? ek : `${ek} is-blocked`);
    } else {
      const hedefT = tokenler.get(ist.hedef);
      if (!hedefT) return;
      const b = px(hedefT.goster.x, hedefT.goster.y);
      const buyu = ist.tur === 'buyu';
      const sinif = buyu ? 'spell-line' : 'attack-line';
      const yazi = gm() ? (buyu ? `✦ ${ist.buyu}` : ist.esya?.ad || 'saldırı') : 'GM onayı bekleniyor';
      if (hedefT === kim) {
        /* Kendine büyü: pulun çevresinde bir halka, etiket (şapkanın da) üstünde */
        g.append(svg('circle', { cx: a.x, cy: a.y, r: kare * olcek(kim.veri) * 0.62, class: `${sinif}${gm() ? '' : ' is-pending'}` }));
        etiket(a.x, a.y - kare * olcek(kim.veri) * 0.95, yazi, 'is-spell is-above');
        return;
      }
      cizgi(g, a, b, gm() ? sinif : `${sinif} is-pending`);
      etiket((a.x + b.x) / 2, (a.y + b.y) / 2, yazi, buyu ? 'is-spell' : 'is-attack', !buyu && ist.esya?.gorsel && ist.esya?.var ? ist.esya.url : null);
    }
  }

  function katmanCiz() {
    katman.replaceChildren();
    etiketler.replaceChildren();
    if (!harita || !durum) return;
    const g = svg('g', {});
    katman.append(g);
    if (gm() && halkalar && harita.izgara.sis) {
      /* Oyuncuların görüş alanları: menzil içinde, duvarların önü */
      for (const t of tokenler.values()) {
        if (t.veri.npc || !t.veri.statlar) continue;
        let halka;
        if (dizin.bos) {
          const p = px(t.goster.x, t.goster.y);
          halka = svg('circle', { cx: p.x, cy: p.y, r: Math.max(0, menzil() * kare), class: 'vision-ring' });
        } else {
          const anahtar = `${t.goster.x},${t.goster.y},${menzil()}`;
          let c = halkaOnbellegi.get(t.veri.id);
          if (!c || c.anahtar !== anahtar) {
            const cokgen = gorusAlani(t.goster.x, t.goster.y);
            const nk = [];
            for (let i = 0; i + 1 < cokgen.length; i += 2) {
              const p = px(cokgen[i], cokgen[i + 1]);
              nk.push(`${p.x},${p.y}`);
            }
            c = { anahtar, noktalar: nk.join(' ') };
            halkaOnbellegi.set(t.veri.id, c);
          }
          halka = svg('polygon', { points: c.noktalar, class: 'vision-ring' });
        }
        halka.style.setProperty('--rc', t.veri.renk);
        g.append(halka);
      }
    }
    if (durum.istek) istekCiz(g, durum.istek);
    const t = benimTokenim();
    if (onizleme && t && !t.anim) {
      const b = px(onizleme.x, onizleme.y);
      if (onizleme.yok) {
        yolYok(g, b);
      } else {
        yolCiz(g, onizleme.yol, 'path-line is-preview');
        g.append(svg('circle', { cx: b.x, cy: b.y, r: kare * 0.42, class: 'ghost is-preview' }));
        etiket(b.x, b.y + kare * 0.5, `${sayiYaz(onizleme.mesafe)} kare`, 'is-preview');
      }
    }
    if (gm() && duvarKipi === 'ciz' && fareNoktasi) {
      /* Çizilmekte olan duvar ve imlecin yapıştığı yer */
      const b = px(fareNoktasi.x, fareNoktasi.y);
      if (zincir) {
        const a = px(zincir.x, zincir.y);
        cizgi(g, a, b, 'wall-draft');
        const L = Math.hypot(fareNoktasi.x - zincir.x, fareNoktasi.y - zincir.y);
        if (L > 0.01) etiket((a.x + b.x) / 2, (a.y + b.y) / 2, `${sayiYaz(L)} kare`, 'is-wall');
      }
      g.append(svg('circle', { cx: b.x, cy: b.y, r: 6, class: 'wall-snap' }));
    }
  }

  function imlec() {
    gorus.classList.toggle('can-walk', hareketEdebilir());
    gorus.classList.toggle('can-attack', Boolean(hedefUstunde));
  }

  function hedefGuncelle(id) {
    if (id === hedefUstunde) return;
    const eski = hedefUstunde;
    hedefUstunde = id;
    if (eski && tokenler.has(eski)) tokenCiz(tokenler.get(eski));
    if (id && tokenler.has(id)) tokenCiz(tokenler.get(id));
    imlec();
  }

  // ------------------------------------------------------------ işaret

  function isaretGoster({ x, y, renk, ad, odak }) {
    if (odak) merkezle(x, y);
    ses.isaret();
    const e = h(
      'div',
      { class: 'ping', vars: { '--pc': renk } },
      h('span', { class: 'ping-ring' }),
      h('span', { class: 'ping-ring' }),
      h('span', { class: 'ping-name', text: ad }),
    );
    e.style.transform = `translate(${x}px, ${y}px)`;
    isaretKatmani.append(e);
    setTimeout(() => e.remove(), 2600);
  }

  // ------------------------------------------------------------ fare ve dokunma

  const parmaklar = new Map();
  let kip = null; // 'kaydir' | 'surukle' | 'yer' | 'hayalet' | 'kistir'
  let kaydir = null;
  let surukle = null;
  let yerSurukle = null; // GM yerdeki bir eşyayı sürüklüyor
  let portalSurukle = null; // GM bir portalı sürüklüyor
  let hayalet = null;
  let kistir = null;
  let bas = null;
  let uzunBasma = null;

  const canliToken = (hedef) => {
    const tokEl = hedef.closest?.('.token');
    const t = tokEl ? tokenler.get(tokEl.dataset.id) : null;
    return t && t.veri.canli !== false && !t.bekle ? t : null;
  };

  gorus.addEventListener('pointerdown', (e) => {
    if (!harita || (e.pointerType === 'mouse' && e.button !== 0 && e.button !== 1)) return;
    if (e.button === 1) e.preventDefault();
    gorus.classList.add('is-pointer-focus');
    gorus.focus({ preventScroll: true, focusVisible: false });
    parmaklar.set(e.pointerId, { x: e.clientX, y: e.clientY });
    try {
      gorus.setPointerCapture(e.pointerId);
    } catch {
      /* önemsiz */
    }
    if (parmaklar.size === 2) return kistirBasla();
    if (parmaklar.size > 2) return;

    if (gm() && duvarKipi) {
      /* Duvar kipinde: dokunuş nokta koyar (ya da duvar siler), sürükleme haritayı kaydırır */
      bas = { x: e.clientX, y: e.clientY, id: null, kaydi: false };
      kip = 'kaydir';
      kaydir = { x: e.clientX, y: e.clientY, tx: gor.tx, ty: gor.ty };
      return;
    }
    const ghost = e.button === 0 && gm() ? e.target.closest?.('[data-istek]') : null;
    const t = e.button === 0 ? canliToken(e.target) : null;
    const yerEl = e.button === 0 && gm() && !t ? e.target.closest?.('.ground-item') : null;
    const portalEl = e.button === 0 && gm() && !t && !yerEl ? e.target.closest?.('.portal') : null;
    bas = { x: e.clientX, y: e.clientY, id: t ? t.veri.id : null, kaydi: false };
    if (ghost) {
      kip = 'hayalet';
      hayalet = { id: Number(ghost.dataset.istek) };
    } else if (portalEl && portallar.has(Number(portalEl.dataset.portal))) {
      const p = portallar.get(Number(portalEl.dataset.portal));
      const q = dunyaya(e.clientX, e.clientY);
      const kp = px(p.goster.x, p.goster.y);
      portalSurukle = { id: p.veri.id, dx: q.x - kp.x, dy: q.y - kp.y };
      kip = 'portal';
    } else if (yerEl && yerdekiler.has(Number(yerEl.dataset.yer))) {
      const y = yerdekiler.get(Number(yerEl.dataset.yer));
      const p = dunyaya(e.clientX, e.clientY);
      const kp = px(y.goster.x, y.goster.y);
      yerSurukle = { id: y.veri.id, dx: p.x - kp.x, dy: p.y - kp.y };
      kip = 'yer';
    } else if (gm() && t) {
      sec(t.veri.id);
      const p = dunyaya(e.clientX, e.clientY);
      const kp = px(t.goster.x, t.goster.y);
      surukle = { id: t.veri.id, dx: p.x - kp.x, dy: p.y - kp.y };
      kip = 'surukle';
    } else {
      kip = 'kaydir';
      kaydir = { x: e.clientX, y: e.clientY, tx: gor.tx, ty: gor.ty };
    }
    if (e.pointerType === 'touch') {
      clearTimeout(uzunBasma);
      uzunBasma = setTimeout(() => {
        if (bas && !bas.kaydi && parmaklar.size === 1) {
          const p = dunyaya(bas.x, bas.y);
          dinleyici.isaret(p.x, p.y, false);
          bas = null;
          kip = null;
          surukle = null;
          yerSurukle = null;
          portalSurukle = null;
          hayalet = null;
        }
      }, UZUN_BASMA_MS);
    }
  });

  function uzerinde(e) {
    /* Fare üzerinde dolaşırken: saldırı ya da büyü hedefi mi (kendisi de olabilir), yürüme hedefi mi */
    const t = canliToken(e.target);
    hedefGuncelle(t && !t.veri.olu && hareketEdebilir() ? t.veri.id : null);
    if (hareketEdebilir() && !t) {
      const p = dunyaya(e.clientX, e.clientY);
      onizleme = hedefHesapla(p.x, p.y);
    } else {
      onizleme = null;
    }
    katmanCiz();
  }

  /* Duvar kipinde fare dolaşırken: çizilecek duvarın ucu ya da silinecek duvar */
  function duvarUzerinde(e) {
    const p = dunyaya(e.clientX, e.clientY);
    const q = kareye(p.x, p.y);
    if (duvarKipi === 'ciz') {
      fareNoktasi = yapistir(q);
      katmanCiz();
    } else {
      const id = duvarBul(q);
      if (id === silinecek) return;
      silinecek = id;
      duvarlariCiz();
    }
  }

  gorus.addEventListener('pointermove', (e) => {
    if (!parmaklar.has(e.pointerId)) {
      if (e.pointerType === 'mouse' && !kip && !gm()) uzerinde(e);
      else if (e.pointerType === 'mouse' && !kip && gm() && duvarKipi && harita) duvarUzerinde(e);
      return;
    }
    parmaklar.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (kip === 'kistir') return kistirGuncelle();
    if (!bas) return;
    if (!bas.kaydi && Math.hypot(e.clientX - bas.x, e.clientY - bas.y) > TIKLAMA_PAYI) {
      bas.kaydi = true;
      clearTimeout(uzunBasma);
      if (kip === 'kaydir') gorus.classList.add('is-panning');
      if (onizleme) {
        onizleme = null;
        katmanCiz();
      }
    }
    if (!bas.kaydi) return;
    if (kip === 'kaydir') {
      gor.tx = kaydir.tx + (e.clientX - kaydir.x);
      gor.ty = kaydir.ty + (e.clientY - kaydir.y);
      uygula();
    } else if (kip === 'surukle') {
      const t = tokenler.get(surukle.id);
      if (!t) return;
      const p = dunyaya(e.clientX, e.clientY);
      t.el.classList.add('is-dragging');
      t.goster = kareye(p.x - surukle.dx, p.y - surukle.dy);
      konumla(t);
      katmanCiz();
    } else if (kip === 'portal') {
      const p = portallar.get(portalSurukle.id);
      if (!p) return;
      const q = dunyaya(e.clientX, e.clientY);
      p.el.classList.add('is-dragging');
      p.goster = kareye(q.x - portalSurukle.dx, q.y - portalSurukle.dy);
      portalCiz(p);
    } else if (kip === 'yer') {
      const y = yerdekiler.get(yerSurukle.id);
      if (!y) return;
      const p = dunyaya(e.clientX, e.clientY);
      y.el.classList.add('is-dragging');
      y.goster = kareye(p.x - yerSurukle.dx, p.y - yerSurukle.dy);
      yerEsyasiCiz(y);
    } else if (kip === 'hayalet') {
      const p = dunyaya(e.clientX, e.clientY);
      istekAyar.set(hayalet.id, oturtulmus(kareye(p.x, p.y)));
      katmanCiz();
      dinleyici.istekTasi(hayalet.id);
    }
  });

  function bitir(e) {
    if (!parmaklar.has(e.pointerId)) return;
    parmaklar.delete(e.pointerId);
    clearTimeout(uzunBasma);
    if (kip === 'kistir') {
      if (parmaklar.size === 1) {
        const [p] = parmaklar.values();
        kip = 'kaydir';
        kaydir = { x: p.x, y: p.y, tx: gor.tx, ty: gor.ty };
        bas = { x: p.x, y: p.y, id: null, kaydi: true };
      } else if (!parmaklar.size) {
        kip = null;
        bas = null;
      }
      return;
    }
    if (kip === 'surukle' && surukle) {
      const t = tokenler.get(surukle.id);
      if (t) {
        t.el.classList.remove('is-dragging');
        if (bas?.kaydi && e.type === 'pointerup') {
          const k = oturtulmus(t.goster);
          t.goster = k;
          t.birakildi = k;
          konumla(t);
          dinleyici.tasi(t.veri.id, k.x, k.y);
        } else {
          t.goster = { x: t.veri.x, y: t.veri.y };
          konumla(t);
        }
      }
    }
    if (kip === 'portal' && portalSurukle) {
      const p = portallar.get(portalSurukle.id);
      if (p) {
        p.el.classList.remove('is-dragging');
        if (bas?.kaydi && e.type === 'pointerup') {
          p.goster = oturtulmus(p.goster);
          dinleyici.portalTasi(p.veri.id, p.goster.x, p.goster.y);
        } else {
          p.goster = { x: p.veri.x, y: p.veri.y };
          if (e.type === 'pointerup' && bas && !bas.kaydi) dinleyici.portalTikla(p.veri.id);
        }
        portalCiz(p);
      }
    }
    if (kip === 'yer' && yerSurukle) {
      const y = yerdekiler.get(yerSurukle.id);
      if (y) {
        y.el.classList.remove('is-dragging');
        if (bas?.kaydi && e.type === 'pointerup') {
          /* Bir tokenın üstüne bırakılırsa ona gider; sunucu karar verir */
          y.goster = { x: sinirla(y.goster.x, 0, harita.genislik), y: sinirla(y.goster.y, 0, harita.yukseklik) };
          dinleyici.esyaTasi(y.veri.id, y.goster.x, y.goster.y);
        } else {
          y.goster = { x: y.veri.x, y: y.veri.y };
          if (e.type === 'pointerup' && bas && !bas.kaydi) dinleyici.esyaTikla(y.veri.id);
        }
        yerEsyasiCiz(y);
      }
    }
    if (e.type === 'pointerup' && bas && !bas.kaydi && kip !== 'hayalet' && kip !== 'yer' && kip !== 'portal') dokun(e, bas);
    gorus.classList.remove('is-panning');
    kip = null;
    surukle = null;
    yerSurukle = null;
    portalSurukle = null;
    hayalet = null;
    kaydir = null;
    bas = null;
  }
  gorus.addEventListener('pointerup', bitir);
  gorus.addEventListener('pointercancel', bitir);
  gorus.addEventListener('pointerleave', (e) => {
    if (e.pointerType === 'mouse' && !kip) {
      onizleme = null;
      fareNoktasi = null;
      hedefGuncelle(null);
      if (silinecek !== null) {
        silinecek = null;
        duvarlariCiz();
      }
      katmanCiz();
    }
  });

  function dokun(e, b) {
    if (gm()) {
      if (duvarKipi) duvarTiklandi(e.clientX, e.clientY);
      else if (!b.id) sec(null);
      return;
    }
    if (b.id) {
      /* Başkasına: saldır ya da büyü yap; kendine: büyü yap */
      dinleyici.etkiles(b.id);
      return;
    }
    if (!hareketEdebilir()) return;
    const p = dunyaya(e.clientX, e.clientY);
    const hedef = hedefHesapla(p.x, p.y);
    if (!hedef) return;
    onizleme = null;
    if (hedef.yok) {
      katmanCiz();
      dinleyici.yolYok();
      return;
    }
    dinleyici.yuru(hedef.x, hedef.y);
  }

  gorus.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (!harita) return;
    if (gm() && duvarKipi) {
      zinciriBitir();
      return;
    }
    const p = dunyaya(e.clientX, e.clientY);
    dinleyici.isaret(p.x, p.y, gm() && e.shiftKey);
  });
  gorus.addEventListener('dblclick', () => {
    if (gm() && duvarKipi === 'ciz') zinciriBitir();
  });
  /* Shift basılıyken duvar ucu yapışmaz */
  for (const olay of ['keydown', 'keyup'])
    window.addEventListener(olay, (e) => {
      if (e.key !== 'Shift' || shift === e.shiftKey) return;
      shift = e.shiftKey;
      if (gm() && duvarKipi === 'ciz' && fareNoktasi) katmanCiz();
    });

  function kistirBasla() {
    clearTimeout(uzunBasma);
    if (kip === 'surukle' && surukle) {
      const t = tokenler.get(surukle.id);
      if (t) {
        t.el.classList.remove('is-dragging');
        t.goster = { x: t.veri.x, y: t.veri.y };
        konumla(t);
      }
      surukle = null;
    }
    if (kip === 'yer' && yerSurukle) {
      const y = yerdekiler.get(yerSurukle.id);
      if (y) {
        y.el.classList.remove('is-dragging');
        y.goster = { x: y.veri.x, y: y.veri.y };
        yerEsyasiCiz(y);
      }
      yerSurukle = null;
    }
    if (kip === 'portal' && portalSurukle) {
      const p = portallar.get(portalSurukle.id);
      if (p) {
        p.el.classList.remove('is-dragging');
        p.goster = { x: p.veri.x, y: p.veri.y };
        portalCiz(p);
      }
      portalSurukle = null;
    }
    hayalet = null;
    gorus.classList.remove('is-panning');
    const [a, b] = [...parmaklar.values()];
    kip = 'kistir';
    bas = null;
    kistir = {
      mesafe: Math.max(1, Math.hypot(a.x - b.x, a.y - b.y)),
      orta: { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 },
      olcek: gor.olcek,
      tx: gor.tx,
      ty: gor.ty,
    };
  }
  function kistirGuncelle() {
    const [a, b] = [...parmaklar.values()];
    if (!a || !b) return;
    const r = kutu();
    const mesafe = Math.max(1, Math.hypot(a.x - b.x, a.y - b.y));
    const orta = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    const yeni = sinirla((kistir.olcek * mesafe) / kistir.mesafe, EN_KUCUK, EN_BUYUK);
    const wx = (kistir.orta.x - r.left - kistir.tx) / kistir.olcek;
    const wy = (kistir.orta.y - r.top - kistir.ty) / kistir.olcek;
    gor.olcek = yeni;
    gor.tx = orta.x - r.left - wx * yeni;
    gor.ty = orta.y - r.top - wy * yeni;
    uygula();
  }

  gorus.addEventListener(
    'wheel',
    (e) => {
      if (!harita) return;
      e.preventDefault();
      let dy = e.deltaY;
      if (e.deltaMode === 1) dy *= 16;
      else if (e.deltaMode === 2) dy *= 400;
      const r = kutu();
      yakinlastir(Math.exp(-dy * (e.ctrlKey ? 0.01 : 0.0015)), e.clientX - r.left, e.clientY - r.top);
    },
    { passive: false },
  );

  gorus.addEventListener('blur', () => gorus.classList.remove('is-pointer-focus'));
  gorus.addEventListener('keydown', (e) => {
    if (!harita || e.target !== gorus) return;
    const adimPx = 80;
    const tuslar = {
      '+': () => ortadanYakinlastir(1.25),
      '=': () => ortadanYakinlastir(1.25),
      '-': () => ortadanYakinlastir(0.8),
      0: () => sigdir(),
      ArrowLeft: () => ((gor.tx += adimPx), uygula()),
      ArrowRight: () => ((gor.tx -= adimPx), uygula()),
      ArrowUp: () => ((gor.ty += adimPx), uygula()),
      ArrowDown: () => ((gor.ty -= adimPx), uygula()),
    };
    if (e.key === 'Escape') {
      /* Çizilen duvarı bitirir; bitirecek bir şey yoksa olay yukarı gider (GM paneli duvar kipinden çıkar) */
      if (zinciriBitir()) e.preventDefault();
      else sec(null);
      return;
    }
    const fn = tuslar[e.key];
    if (fn) {
      e.preventDefault();
      fn();
    }
  });

  yakinlik.addEventListener('click', (e) => {
    const islem = e.target.closest('button')?.dataset.zoom;
    if (islem === 'in') ortadanYakinlastir(1.25);
    else if (islem === 'out') ortadanYakinlastir(0.8);
    else if (islem === 'fit') sigdir();
  });

  return {
    ayarla,
    isaretGoster,
    yuruOlayi,
    saldiriOlayi,
    olumOlayi,
    portalOlayi,
    sec,
    odakla,
    on(olay, fn) {
      dinleyici[olay] = fn;
    },
    token: (id) => tokenler.get(id)?.veri ?? null,
    yerEsyasi: (id) => yerdekiler.get(id)?.veri ?? null,
    portal: (id) => portallar.get(id)?.veri ?? null,
    /** Ekrandaki noktanın haritadaki yeri, kare cinsinden (harita yoksa null) */
    kareKonumu(cx, cy) {
      if (!harita) return null;
      const p = dunyaya(cx, cy);
      return kareye(p.x, p.y);
    },
    halkalariGoster(acik) {
      halkalar = acik;
      katmanCiz();
    },
    /** GM: bekleyen yürüme isteğinin (sürükleyerek düzeltilmiş) hedefi */
    istekHedefi: (id) => istekAyar.get(id) || null,
    istekHedefiniSifirla(id) {
      istekAyar.delete(id);
      katmanCiz();
    },
    /** Bekleyen yürüme isteğinin yolunun uzunluğu (kare; yol yoksa null) */
    istekUzunlugu(ist) {
      if (!ist || ist.tur !== 'hareket' || !harita) return null;
      return istekYolu(ist)?.uzunluk ?? null;
    },
    /** GM: duvar kipi ('ciz', 'sil' ya da null) */
    duvarKipi(yeni) {
      duvarKipi = gm() ? yeni || null : null;
      zincir = null;
      fareNoktasi = null;
      silinecek = null;
      gorus.classList.toggle('is-wall-draw', duvarKipi === 'ciz');
      gorus.classList.toggle('is-wall-erase', duvarKipi === 'sil');
      duvarlariCiz();
      katmanCiz();
    },
    duvarlariGoster(acik) {
      duvarGoster = acik;
      duvarlariCiz();
    },
    izgarayaYapis(acik) {
      izgaraYapis = acik;
    },
    zinciriBitir,
    get duvarSayisi() {
      return duvarlar.length;
    },
  };
}
