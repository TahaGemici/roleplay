import { h, svg, sinirla, basHarfler, yaziRengi, azHareket, sayiYaz } from './ortak.js';
import { sisOlustur } from './sis.js';
import { saldiriOynat, olumOynat } from './efekt.js';

const EN_KUCUK = 0.05;
const EN_BUYUK = 6;
const TIKLAMA_PAYI = 6; // bu kadar pikselden az kayan dokunuş tıklamadır
const UZUN_BASMA_MS = 550;
const YURUME_MS_KARE = 170;

const yumusak = (p) => (p < 0.5 ? 2 * p * p : 1 - (-2 * p + 2) ** 2 / 2);

/**
 * Harita görünümü. Konumlar kare birimindedir: (0,0) haritanın sol üst köşesi, (0.5, 0.5) ilk karenin ortası.
 * Harita, GM'in seçtiği genişlik × yükseklik kadar kareye bölünür; resim bu alana gerilir.
 * Oyuncu (yalnızca kendi sırasında): bir yere tıklayınca yürüme isteği, bir karaktere (ya da kendine) tıklayınca
 * saldırı / büyü seçimi açılır; ikisi de GM'e gider.
 * GM: her tokenı ve yerdeki eşyaları sürükleyebilir, bekleyen yürüme isteğinin hedefini sürükleyerek düzeltebilir.
 * Pulun boyu karakterin yüküyle büyür; resmi ve şapkası varsa üstünde görünür.
 * Sağ tık (dokunmatikte uzun basma) herkese işaret gösterir.
 */
export function haritaOlustur(el) {
  const { gorus, dunya, resim, izgara, yerKatmani, tokenKatmani, sisTuvali, katman, etiketler, efektKatmani, isaretKatmani, yakinlik } = el;
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
  let bekleyenKesif = []; // kendi yürüyüşüm sürerken gelen keşif parçaları
  const dinleyici = { yuru() {}, tasi() {}, sec() {}, isaret() {}, etkiles() {}, istekTasi() {}, esyaTasi() {}, esyaTikla() {} };
  const yerdekiler = new Map(); // yerdeki eşyalar: id → { veri, el }
  const sonSaldiri = new Map(); // id → saldırı animasyonunun bittiği an (ölüm ondan sonra oynasın)
  const sis = sisOlustur(sisTuvali);

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

  function sigdir() {
    const r = kutu();
    if (!boyut.w || r.width < 10 || r.height < 10) {
      sigdirBekliyor = true;
      return;
    }
    sigdirBekliyor = false;
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
      yerdekileriKur([]);
      sis.ayarla({ acik: false });
      onizleme = null;
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
    }
    tokenleriKur(yeni.tokenler || []);
    yerdekileriKur(yeni.yerdekiler || []);
    sisAyarla(yeni);
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
    katman.setAttribute('viewBox', `0 0 ${boyut.w} ${boyut.h}`);
    katman.setAttribute('width', String(boyut.w));
    katman.setAttribute('height', String(boyut.h));
  }

  // ------------------------------------------------------------ görüş sisi (yalnızca oyuncu)

  function sisAyarla(d) {
    const acik = !gm() && Boolean(harita?.izgara?.sis);
    sis.ayarla({ acik, genislik: boyut.w, yukseklik: boyut.h, kare });
    if (!acik) return;
    if (d.kesif) {
      const benimYuruyusum = benimTokenim()?.anim;
      if (d.kesif.sifirla) {
        bekleyenKesif = [];
        sis.sifirla(d.kesif.liste);
      } else if (benimYuruyusum) {
        bekleyenKesif.push(...d.kesif.liste);
      } else {
        sis.ekle(d.kesif.liste);
      }
    }
    sisGorusu();
  }

  const menzil = () => durum?.gorus ?? 0;

  function sisGorusu() {
    if (!sis.etkin) return;
    const t = benimTokenim();
    sis.gorus(t ? t.goster.x : 0, t ? t.goster.y : 0, t ? menzil() : 0);
  }

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

  const olcek = (veri) => (Number.isFinite(veri?.olcek) ? sinirla(veri.olcek, 0.5, 2) : 1);

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
    y.el.title = e.sinif ? `${e.ad} (${e.sinif})` : e.ad;
    y.el.classList.toggle('is-movable', gm());
    const url = e.url && e.gorsel && e.var !== false ? e.url : null;
    resimAyarla(y.img, url);
    y.yazi.hidden = Boolean(url);
    y.yazi.textContent = basHarfler(e.ad);
  }

  function yerdekileriKur(liste) {
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
    for (const [id, y] of [...yerdekiler]) {
      if (gorulen.has(id)) continue;
      y.el.remove();
      yerdekiler.delete(id);
    }
  }

  // ------------------------------------------------------------ yürüme animasyonu

  const animler = new Set();
  let rafNo = 0;

  function animBaslat(t, a) {
    /* Süren bir yürüyüş varsa yenisi onun bulunduğu yerden devam eder */
    t.anim = { ...a, bas: performance.now() };
    t.el.classList.add('is-walking');
    if (a.gir) t.el.style.opacity = '0';
    animler.add(t);
    if (!rafNo) rafNo = requestAnimationFrame(adim);
  }

  function adim(simdi) {
    for (const t of [...animler]) {
      const a = t.anim;
      const p = Math.min(1, (simdi - a.bas) / a.sure);
      const e = yumusak(p);
      const once = t.goster;
      t.goster = { x: a.x1 + (a.x2 - a.x1) * e, y: a.y1 + (a.y2 - a.y1) * e };
      konumla(t);
      if (a.gir) t.el.style.opacity = String(Math.min(1, p * 5));
      if (a.cik && p > 0.8) t.el.style.opacity = String(Math.max(0, (1 - p) * 5));
      if (t === benimTokenim() && sis.etkin) {
        sis.yol(once.x, once.y, t.goster.x, t.goster.y, menzil());
        sisGorusu();
        bekleyenleriGoster(t.goster, menzil());
      }
      if (p >= 1) animBitir(t, true);
    }
    rafNo = animler.size ? requestAnimationFrame(adim) : 0;
    if (onizleme || durum?.istek) katmanCiz();
  }

  function animBitir(t, dogal) {
    animler.delete(t);
    const a = t.anim;
    t.anim = null;
    t.el.classList.remove('is-walking');
    t.el.style.opacity = '';
    if (dogal && a) t.goster = { x: a.x2, y: a.y2 };
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
      if (bekleyenKesif.length) {
        sis.ekle(bekleyenKesif);
        bekleyenKesif = [];
      }
      bekleyenleriGoster(null, Infinity);
      sisGorusu();
      katmanCiz();
      imlec();
    }
  }

  function bekleyenleriGoster(konum, r) {
    for (const t of tokenler.values()) {
      if (!t.bekle) continue;
      if (konum && Math.hypot(t.goster.x - konum.x, t.goster.y - konum.y) > r) continue;
      t.bekle = false;
      t.el.classList.remove('is-waiting');
    }
  }

  function yuruOlayi(m) {
    if (!harita) return;
    let t = tokenler.get(m.id);
    if (!t) {
      if (!m.gir && !gm()) return;
      t = yeniToken({ id: m.id, ad: m.ad, renk: m.renk, npc: m.npc, olu: m.olu, canli: true, x: m.x1, y: m.y1, resim: m.resim, sapka: m.sapka, olcek: m.olcek });
      tokenCiz(t);
    }
    if (t.birakildi && Math.hypot(t.birakildi.x - m.x2, t.birakildi.y - m.y2) < 0.02) {
      /* GM'in kendi sürükleyip bıraktığı yer: zaten orada */
      t.birakildi = null;
      t.goster = { x: m.x2, y: m.y2 };
      konumla(t);
      return;
    }
    t.birakildi = null;
    if (t.veri.canli === false) {
      /* Hatırasını gördüğü karakter yeniden görüşe yürüyor: artık canlı */
      t.veri = { ...t.veri, canli: true, x: m.x2, y: m.y2 };
      tokenCiz(t);
    }
    const mesafe = Math.hypot(m.x2 - m.x1, m.y2 - m.y1);
    const sure = azHareket() ? 1 : sinirla(mesafe * YURUME_MS_KARE, 260, 2400);
    if (m.gir) t.goster = { x: m.x1, y: m.y1 };
    else if (!t.anim) t.goster = { ...t.goster };
    animBaslat(t, { x1: m.gir ? m.x1 : t.goster.x, y1: m.gir ? m.y1 : t.goster.y, x2: m.x2, y2: m.y2, sure, gir: m.gir, cik: m.cik });
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

  /** Kare koordinatını harita içine alır; "kare ortasına otursun" açıksa kare ortasına oturtur. */
  function oturtulmus({ x, y }) {
    const ortala = (v, ust) => (harita.izgara.oturt ? Math.min(Math.floor(v), Math.max(0, Math.floor(ust) - 1)) + 0.5 : v);
    return { x: ortala(sinirla(x, 0, harita.genislik), harita.genislik), y: ortala(sinirla(y, 0, harita.yukseklik), harita.yukseklik) };
  }

  /** Oyuncunun imleç noktasına göre gideceği yer ve oraya uzaklığı (kare). Enerjiyi kurallar.c belirler. */
  function hedefHesapla(wx, wy) {
    const t = benimTokenim();
    if (!t || !harita) return null;
    const { x, y } = oturtulmus(kareye(wx, wy));
    const mesafe = Math.hypot(x - t.goster.x, y - t.goster.y);
    return mesafe < 0.02 ? null : { x, y, mesafe };
  }

  function etiket(x, y, metin, sinif = '', resimUrl = null) {
    const e = h('div', { class: `map-label ${sinif}` }, h('div', { class: 'map-label-in' }, resimUrl ? h('img', { src: resimUrl, alt: '' }) : null, h('span', { text: metin })));
    e.style.transform = `translate(${x}px, ${y}px)`;
    etiketler.append(e);
  }

  function cizgi(g, a, b, sinif) {
    g.append(svg('line', { x1: a.x, y1: a.y, x2: b.x, y2: b.y, class: sinif }));
  }

  /* Bekleyen isteğin yolu ya da saldırı çizgisi. GM yürüme hedefini sürükleyerek kaydırabilir. */
  function istekCiz(g, ist) {
    const kim = tokenler.get(ist.kim);
    if (!kim) return;
    const a = px(kim.goster.x, kim.goster.y);
    const ek = gm() ? 'is-request' : 'is-pending';
    if (ist.tur === 'hareket') {
      const hedef = (gm() && istekAyar.get(ist.id)) || { x: ist.x2, y: ist.y2 };
      const b = px(hedef.x, hedef.y);
      cizgi(g, a, b, `path-line ${ek}`);
      const hayaletEl = svg('circle', { cx: b.x, cy: b.y, r: kare * 0.42, class: `ghost ${ek}`, 'data-istek': gm() ? String(ist.id) : null });
      hayaletEl.style.setProperty('--rc', kim.veri.renk); /* CSSOM: CSP satır içi style özniteliğine izin vermez */
      g.append(hayaletEl);
      const yazi = gm() ? `${kim.veri.ad} · ${sayiYaz(Math.hypot(hedef.x - kim.veri.x, hedef.y - kim.veri.y))} kare` : 'GM onayı bekleniyor';
      etiket(b.x, b.y + kare * 0.5, yazi, ek);
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
      for (const t of tokenler.values()) {
        if (t.veri.npc || !t.veri.statlar) continue;
        const p = px(t.goster.x, t.goster.y);
        const halka = svg('circle', { cx: p.x, cy: p.y, r: Math.max(0, menzil() * kare), class: 'vision-ring' });
        halka.style.setProperty('--rc', t.veri.renk);
        g.append(halka);
      }
    }
    if (durum.istek) istekCiz(g, durum.istek);
    const t = benimTokenim();
    if (onizleme && t && !t.anim) {
      const a = px(t.goster.x, t.goster.y);
      const b = px(onizleme.x, onizleme.y);
      cizgi(g, a, b, 'path-line is-preview');
      g.append(svg('circle', { cx: b.x, cy: b.y, r: kare * 0.42, class: 'ghost is-preview' }));
      etiket(b.x, b.y + kare * 0.5, `${sayiYaz(onizleme.mesafe)} kare`, 'is-preview');
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

    const ghost = e.button === 0 && gm() ? e.target.closest?.('[data-istek]') : null;
    const t = e.button === 0 ? canliToken(e.target) : null;
    const yerEl = e.button === 0 && gm() && !t ? e.target.closest?.('.ground-item') : null;
    bas = { x: e.clientX, y: e.clientY, id: t ? t.veri.id : null, kaydi: false };
    if (ghost) {
      kip = 'hayalet';
      hayalet = { id: Number(ghost.dataset.istek) };
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

  gorus.addEventListener('pointermove', (e) => {
    if (!parmaklar.has(e.pointerId)) {
      if (e.pointerType === 'mouse' && !kip && !gm()) uzerinde(e);
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
    if (e.type === 'pointerup' && bas && !bas.kaydi && kip !== 'hayalet' && kip !== 'yer') dokun(e, bas);
    gorus.classList.remove('is-panning');
    kip = null;
    surukle = null;
    yerSurukle = null;
    hayalet = null;
    kaydir = null;
    bas = null;
  }
  gorus.addEventListener('pointerup', bitir);
  gorus.addEventListener('pointercancel', bitir);
  gorus.addEventListener('pointerleave', (e) => {
    if (e.pointerType === 'mouse' && !kip) {
      onizleme = null;
      hedefGuncelle(null);
      katmanCiz();
    }
  });

  function dokun(e, b) {
    if (gm()) {
      if (!b.id) sec(null);
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
    dinleyici.yuru(hedef.x, hedef.y);
  }

  gorus.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    if (!harita) return;
    const p = dunyaya(e.clientX, e.clientY);
    dinleyici.isaret(p.x, p.y, gm() && e.shiftKey);
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
      Escape: () => sec(null),
    };
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
    sec,
    odakla,
    on(olay, fn) {
      dinleyici[olay] = fn;
    },
    token: (id) => tokenler.get(id)?.veri ?? null,
    yerEsyasi: (id) => yerdekiler.get(id)?.veri ?? null,
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
  };
}
