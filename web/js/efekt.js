import { h, azHareket } from './ortak.js';
import { ses } from './ses.js';

/*
 * Saldırı ve ölüm animasyonları. Haritanın verdiği bağlamla çalışır:
 *   ctx.katman      efektlerin çizildiği katman (dünya koordinatlarında)
 *   ctx.boyut()     bir karenin piksel boyu
 *   ctx.px(x, y)    kare → dünya pikseli
 *   ctx.govde(id)   tokenın hareket ettirilebilen gövdesi (konumundan bağımsız)
 */

const sayi = (v) => Math.abs(v).toLocaleString('tr-TR', { maximumFractionDigits: 1 });

function yerlestir(el, x, y) {
  el.style.transform = `translate(${x}px, ${y}px)`;
}

function kaldir(el, anim) {
  anim.onfinish = () => el.remove();
  anim.oncancel = () => el.remove();
}

function mermi(ctx, resim, s) {
  const el = h('div', { class: `missile${resim ? '' : ' is-spark'}`, vars: { '--ms': `${Math.round(s * 0.62)}px` } });
  if (resim) {
    const img = h('img', { src: resim, alt: '', draggable: 'false' });
    img.onerror = () => {
      img.remove();
      el.classList.add('is-spark');
    };
    el.append(img);
  }
  ctx.katman.append(el);
  return el;
}

/* Büyü küresi: hedefe uçan parlak top */
function kure(ctx, s) {
  const el = h('div', { class: 'missile is-orb', vars: { '--ms': `${Math.round(s * 0.5)}px` } });
  ctx.katman.append(el);
  return el;
}

function kivilcimlar(ctx, x, y, s, renk) {
  const n = 7;
  for (let i = 0; i < n; i++) {
    const a = (Math.PI * 2 * i) / n + Math.random() * 0.6;
    const d = s * (0.45 + Math.random() * 0.35);
    const el = h('div', { class: 'spark', vars: { '--sc': renk } });
    yerlestir(el, x, y);
    ctx.katman.append(el);
    const anim = el.animate(
      [
        { transform: `translate(${x}px, ${y}px) scale(1)`, opacity: 1 },
        { transform: `translate(${x + Math.cos(a) * d}px, ${y + Math.sin(a) * d}px) scale(.3)`, opacity: 0 },
      ],
      { duration: 420 + Math.random() * 160, easing: 'cubic-bezier(.2,.7,.3,1)', fill: 'forwards' },
    );
    kaldir(el, anim);
  }
}

function halka(ctx, x, y, s, sinif) {
  const el = h('div', { class: `ring-burst ${sinif}`, vars: { '--rs': `${Math.round(s)}px` } });
  yerlestir(el, x, y);
  ctx.katman.append(el);
  const anim = el.animate(
    [
      { transform: `translate(${x}px, ${y}px) scale(.35)`, opacity: 0.95 },
      { transform: `translate(${x}px, ${y}px) scale(1.25)`, opacity: 0 },
    ],
    { duration: 560, easing: 'cubic-bezier(.2,.6,.3,1)', fill: 'forwards' },
  );
  kaldir(el, anim);
}

function yuzenSayi(ctx, x, y, s, hasar) {
  const iyilesme = hasar < 0;
  const el = h('div', { class: `floater${iyilesme ? ' is-heal' : ''}`, text: `${iyilesme ? '+' : '−'}${sayi(hasar)}` });
  yerlestir(el, x, y - s * 0.35);
  ctx.katman.append(el);
  const anim = el.animate(
    [
      { transform: `translate(${x}px, ${y - s * 0.35}px) scale(.6)`, opacity: 0 },
      { transform: `translate(${x}px, ${y - s * 0.62}px) scale(1.12)`, opacity: 1, offset: 0.18 },
      { transform: `translate(${x}px, ${y - s * 1.15}px) scale(1)`, opacity: 0 },
    ],
    { duration: 1300, easing: 'ease-out', fill: 'forwards' },
  );
  kaldir(el, anim);
}

/**
 * Saldırı: saldıran hedefe doğru atılır, eşyası (resmi) hedefe uçar ya da yakındaysa savrulur;
 * isabette hedef sarsılır ve kızarır, ıskada yana kaçar. Büyüde (m.buyu) hedefe bir küre uçar; can artırdıysa hedef
 * yeşil parlar, kendine yapılan büyüde yalnızca bir halka açılır. Hasar sayısını yalnızca GM ve hedefin kendisi
 * görür (sunucu yalnızca onlara gönderir).
 */
export function saldiriOynat(ctx, m) {
  const s = ctx.boyut();
  const az = azHareket();
  const buyu = Boolean(m.buyu);
  const kendine = m.saldiran === m.hedef;
  const A = m.a && !kendine ? ctx.px(m.a.x, m.a.y) : null;
  const H = m.h ? ctx.px(m.h.x, m.h.y) : null;
  const aGovde = A ? ctx.govde(m.saldiran) : null;
  const hGovde = H ? ctx.govde(m.hedef) : null;
  let carpma = 0;

  if (A && aGovde) {
    const dx = H ? H.x - A.x : 0;
    const dy = H ? H.y - A.y : -1;
    const L = Math.hypot(dx, dy) || 1;
    const ileri = Math.min(0.4 * s, L * 0.35);
    aGovde.animate(
      [
        { transform: 'translate(0, 0)' },
        { transform: `translate(${(dx / L) * ileri}px, ${(dy / L) * ileri}px) scale(1.07)`, offset: 0.42 },
        { transform: 'translate(0, 0)' },
      ],
      { duration: az ? 1 : 440, easing: 'cubic-bezier(.3,.7,.4,1)' },
    );
    carpma = 190;
  }

  if (buyu) ses.buyuYap();
  if (kendine && H && !az) {
    halka(ctx, H.x, H.y, s * 1.2, 'is-magic');
    carpma = 260;
  }

  if (A && H && !az && buyu) {
    const mesafe = Math.hypot(H.x - A.x, H.y - A.y) / s;
    const el = kure(ctx, s);
    const yari = s * 0.25;
    const sure = Math.min(620, 220 + mesafe * 52);
    const anim = el.animate(
      [
        { transform: `translate(${A.x - yari}px, ${A.y - yari}px) scale(.3)`, opacity: 0 },
        { transform: `translate(${A.x + (H.x - A.x) * 0.12 - yari}px, ${A.y + (H.y - A.y) * 0.12 - yari}px) scale(1)`, opacity: 1, offset: 0.18 },
        { transform: `translate(${H.x - yari}px, ${H.y - yari}px) scale(1.25)`, opacity: 1 },
      ],
      { duration: sure, delay: 120, easing: 'cubic-bezier(.4,.1,.6,1)', fill: 'both' },
    );
    anim.onfinish = () => el.remove();
    anim.oncancel = () => el.remove();
    ses.kure(0.12, sure);
    carpma = 120 + sure;
  } else if (A && H && !az) {
    const mesafe = Math.hypot(H.x - A.x, H.y - A.y) / s;
    const aci = (Math.atan2(H.y - A.y, H.x - A.x) * 180) / Math.PI;
    const el = mermi(ctx, m.resim, s);
    const yari = s * 0.31;
    let anim;
    if (mesafe <= 1.6) {
      /* Yakın dövüş: eşya hedefin önünde savrulur */
      const mx = A.x + (H.x - A.x) * 0.6 - yari;
      const my = A.y + (H.y - A.y) * 0.6 - yari;
      anim = el.animate(
        [
          { transform: `translate(${mx}px, ${my}px) rotate(${aci - 85}deg) scale(.7)`, opacity: 0 },
          { transform: `translate(${mx}px, ${my}px) rotate(${aci - 40}deg) scale(1)`, opacity: 1, offset: 0.3 },
          { transform: `translate(${mx}px, ${my}px) rotate(${aci + 45}deg) scale(1)`, opacity: 1, offset: 0.75 },
          { transform: `translate(${mx}px, ${my}px) rotate(${aci + 60}deg) scale(.9)`, opacity: 0 },
        ],
        { duration: 420, delay: 70, easing: 'cubic-bezier(.4,0,.2,1)', fill: 'both' },
      );
      ses.savurma(0.07);
      carpma = 290;
    } else {
      /* Uzak: eşya hedefe uçar */
      const sure = Math.min(560, 170 + mesafe * 48);
      anim = el.animate(
        [
          { transform: `translate(${A.x - yari}px, ${A.y - yari}px) rotate(${aci}deg) scale(.6)`, opacity: 0 },
          { transform: `translate(${A.x + (H.x - A.x) * 0.15 - yari}px, ${A.y + (H.y - A.y) * 0.15 - yari}px) rotate(${aci + 90}deg) scale(1)`, opacity: 1, offset: 0.15 },
          { transform: `translate(${H.x - yari}px, ${H.y - yari}px) rotate(${aci + 540}deg) scale(.85)`, opacity: 1 },
        ],
        { duration: sure, delay: 110, easing: 'cubic-bezier(.35,.1,.7,1)', fill: 'both' },
      );
      anim.addEventListener('finish', () => el.animate([{ opacity: 1 }, { opacity: 0 }], { duration: 120, fill: 'forwards' }));
      ses.firlatma(0.11, sure);
      carpma = 110 + sure;
    }
    anim.onfinish = () => setTimeout(() => el.remove(), 130);
    anim.oncancel = () => el.remove();
  }

  if (H) {
    setTimeout(() => {
      const iyilesme = typeof m.hasar === 'number' && m.hasar < 0;
      if (buyu) (iyilesme && m.isabet ? ses.iyiles : ses.buyuCarp)();
      else (m.isabet ? ses.isabet : ses.iska)();
      if (hGovde && m.isabet && iyilesme) {
        hGovde.animate(
          [
            { transform: 'scale(1)', filter: 'none' },
            { transform: 'scale(1.1)', filter: 'brightness(1.5) saturate(1.4) hue-rotate(60deg)', offset: 0.3 },
            { transform: 'scale(1)', filter: 'none' },
          ],
          { duration: az ? 1 : 620, easing: 'ease-out' },
        );
        if (!az) {
          halka(ctx, H.x, H.y, s, 'is-heal');
          kivilcimlar(ctx, H.x, H.y, s, '#8fe0a0');
        }
      } else if (hGovde && m.isabet) {
        hGovde.animate(
          [
            { transform: 'translate(0,0)', filter: 'none' },
            { transform: `translate(${s * -0.09}px, 0) rotate(-6deg)`, filter: 'brightness(1.6) saturate(1.6) hue-rotate(-25deg)', offset: 0.15 },
            { transform: `translate(${s * 0.08}px, 0) rotate(5deg)`, offset: 0.35 },
            { transform: `translate(${s * -0.05}px, 0) rotate(-3deg)`, offset: 0.55 },
            { transform: `translate(${s * 0.025}px, 0)`, offset: 0.75 },
            { transform: 'translate(0,0)', filter: 'none' },
          ],
          { duration: az ? 1 : 460, easing: 'ease-out' },
        );
        if (!az) {
          halka(ctx, H.x, H.y, s, buyu ? 'is-magic' : 'is-hit');
          kivilcimlar(ctx, H.x, H.y, s, buyu ? '#c9a3ff' : 'var(--ember-hi)');
        }
      } else if (buyu) {
        /* Canı değiştirmeyen büyü: hedefin çevresinde bir halka (kendine yapılanınki zaten açıldı) */
        if (!az && !kendine) halka(ctx, H.x, H.y, s * 0.9, 'is-magic');
      } else if (hGovde && !az) {
        const dx = A ? H.x - A.x : 1;
        const dy = A ? H.y - A.y : 0;
        const L = Math.hypot(dx, dy) || 1;
        const kac = s * 0.24;
        hGovde.animate(
          [
            { transform: 'translate(0,0)' },
            { transform: `translate(${(-dy / L) * kac}px, ${(dx / L) * kac}px) rotate(${dx > 0 ? 8 : -8}deg)`, offset: 0.35 },
            { transform: 'translate(0,0)' },
          ],
          { duration: 420, easing: 'cubic-bezier(.3,.7,.4,1)' },
        );
        halka(ctx, H.x, H.y, s * 0.8, 'is-miss');
      }
      if (typeof m.hasar === 'number' && Math.abs(m.hasar) > 1e-6) yuzenSayi(ctx, H.x, H.y, s, m.hasar);
    }, az ? 0 : carpma);
  }
}

/** Ölüm: token sarsılır, yana devrilir, solar; bittiğinde bitti() çağrılır (token ölü haline geçer). */
export function olumOynat(ctx, id, bitti) {
  const g = ctx.govde(id);
  const P = ctx.merkez(id);
  if (!g || !P) {
    bitti();
    return;
  }
  const s = ctx.boyut();
  ses.olum();
  if (!azHareket()) {
    halka(ctx, P.x, P.y, s * 1.3, 'is-death');
    kivilcimlar(ctx, P.x, P.y, s * 1.1, '#8a1f14');
  }
  const anim = g.animate(
    [
      { transform: 'none', filter: 'none', opacity: 1 },
      { transform: `translateY(${-s * 0.12}px) rotate(10deg) scale(1.05)`, filter: 'brightness(1.4)', opacity: 1, offset: 0.2 },
      { transform: 'rotate(-30deg) scale(.95)', filter: 'grayscale(.6) brightness(.9)', opacity: 0.95, offset: 0.55 },
      { transform: 'rotate(-90deg) scale(.86)', filter: 'grayscale(1) brightness(.7)', opacity: 0.85 },
    ],
    { duration: azHareket() ? 1 : 1000, easing: 'cubic-bezier(.45,0,.55,1)', fill: 'forwards' },
  );
  let bitti1 = false;
  const son = () => {
    if (bitti1) return;
    bitti1 = true;
    bitti();
    anim.cancel();
  };
  anim.onfinish = son;
  setTimeout(son, 1600); /* sekme arka plandaysa animasyon beklemeye kalabilir */
}

/** Portal: geçilen ya da varılan yerde dönen bir halka ve parıltılar. P: dünya pikseli. */
export function portalOynat(ctx, P, varis) {
  if (!P || azHareket()) return;
  const s = ctx.boyut();
  halka(ctx, P.x, P.y, s * (varis ? 1.4 : 1.1), 'is-portal');
  kivilcimlar(ctx, P.x, P.y, s * 0.9, '#8fd3ff');
}
