/*
 * Duvarlar: görüş hattı, görüş alanı ve duvarların etrafından dolaşan en kısa yol. Birimler kare cinsindendir.
 *
 * sunucu/duvar.c'nin aynısıdır: tarayıcının gösterdiği yol ile sunucunun yürüttüğü yol aynı çıksın diye sabitler,
 * sıralamalar ve sayı işlemleri birebir aynıdır. Birini değiştirirsen öbürünü de değiştir.
 *
 * Duvar ince bir doğru parçasıdır: görüşü keser, karakterin merkezi üstünden geçemez. Tam duvarın üstünde duran bir
 * nokta iki yanı da görür ve iki yana da çıkabilir; bu yüzden yollar duvarın üstünde ya da dibinde bitmez (UC_PAYI).
 * Harita sınırı yürürken duvar sayılır.
 */

export const ACIKLIK = 0.1; // yol duvarlara en az bu kadar yaklaşır; bundan dar aralıklar kapalıdır
const KOSE_PAYI = 0.35; // yol köşeleri bu kadar açıktan döner; sığmazsa ikincisi denenir
const KOSE_PAYI_DAR = 0.15;
const UC_PAYI = 0.01; // yolun sonu bir duvara bunun yarısından yakınsa, geldiği yana doğru bu kadar uzağa alınır
const UST = 1e-7; // bundan yakın nokta duvarın üstündedir
const ACI_PAYI = 1e-9;
const KOSE_ESI = 1e-6;
const YAY_PARCA = 72;
const KOVA = 256;
const KAYIT_PAYI = KOSE_PAYI + 0.05;

const uz = (ax, ay, bx, by) => {
  const dx = bx - ax;
  const dy = by - ay;
  return Math.sqrt(dx * dx + dy * dy);
};

/* P'nin AB doğrusuna işaretli uzaklığı (solda artı) */
function yan(ax, ay, bx, by, px, py) {
  const dx = bx - ax;
  const dy = by - ay;
  const L = Math.sqrt(dx * dx + dy * dy);
  if (L < 1e-12) return 0;
  return (dx * (py - ay) - dy * (px - ax)) / L;
}

function noktaParca(px, py, ax, ay, bx, by) {
  const dx = bx - ax;
  const dy = by - ay;
  const L = dx * dx + dy * dy;
  let t = L > 0 ? ((px - ax) * dx + (py - ay) * dy) / L : 0;
  if (t < 0) t = 0;
  if (t > 1) t = 1;
  return uz(ax + t * dx, ay + t * dy, px, py);
}

/* AB ile w birbirini tam olarak (uçlar dışında) kesiyor mu; w = p[o..o+3] */
function capraz(ax, ay, bx, by, p, o) {
  const d1 = yan(p[o], p[o + 1], p[o + 2], p[o + 3], ax, ay);
  const d2 = yan(p[o], p[o + 1], p[o + 2], p[o + 3], bx, by);
  if (!((d1 > UST && d2 < -UST) || (d1 < -UST && d2 > UST))) return false;
  const d3 = yan(ax, ay, bx, by, p[o], p[o + 1]);
  const d4 = yan(ax, ay, bx, by, p[o + 2], p[o + 3]);
  return (d3 > UST && d4 < -UST) || (d3 < -UST && d4 > UST);
}

/* P, AB parçasının üstünde ve iki ucundan da uzakta mı */
function icNokta(px, py, ax, ay, bx, by) {
  const dx = bx - ax;
  const dy = by - ay;
  const L = dx * dx + dy * dy;
  if (L < 1e-18) return false;
  const t = ((px - ax) * dx + (py - ay) * dy) / L;
  if (t <= 0 || t >= 1) return false;
  if (Math.abs(dx * (py - ay) - dy * (px - ax)) / Math.sqrt(L) > UST) return false;
  return uz(px, py, ax, ay) > UST && uz(px, py, bx, by) > UST;
}

function engelliyor(ax, ay, bx, by, p, o) {
  return capraz(ax, ay, bx, by, p, o) || icNokta(p[o], p[o + 1], ax, ay, bx, by) || icNokta(p[o + 2], p[o + 3], ax, ay, bx, by);
}

function parcaMesafesi(ax, ay, bx, by, p, o) {
  if (capraz(ax, ay, bx, by, p, o)) return 0;
  let m = noktaParca(ax, ay, p[o], p[o + 1], p[o + 2], p[o + 3]);
  m = Math.min(m, noktaParca(bx, by, p[o], p[o + 1], p[o + 2], p[o + 3]));
  m = Math.min(m, noktaParca(p[o], p[o + 1], ax, ay, bx, by));
  return Math.min(m, noktaParca(p[o + 2], p[o + 3], ax, ay, bx, by));
}

function isinDogru(ox, oy, ux, uy, p, o) {
  const sx = p[o + 2] - p[o];
  const sy = p[o + 3] - p[o + 1];
  const den = ux * sy - uy * sx;
  if (Math.abs(den) < 1e-15) return -1;
  return ((p[o] - ox) * sy - (p[o + 1] - oy) * sx) / den;
}

function isinParca(ox, oy, ux, uy, p, o) {
  const sx = p[o + 2] - p[o];
  const sy = p[o + 3] - p[o + 1];
  const den = ux * sy - uy * sx;
  if (Math.abs(den) < 1e-15) return -1;
  const t = ((p[o] - ox) * sy - (p[o + 1] - oy) * sx) / den;
  const s = ((p[o] - ox) * uy - (p[o + 1] - oy) * ux) / den;
  if (t < 0 || s < 0 || s > 1) return -1;
  return t;
}

function kovaNo(a) {
  const k = Math.floor(((a + Math.PI) / (2 * Math.PI)) * KOVA);
  return ((k % KOVA) + KOVA) % KOVA;
}

const yari = (x, y) => y < 0 || (y === 0 && x < 0);
function onceMi(ax, ay, bx, by) {
  const ya = yari(ax, ay);
  const yb = yari(bx, by);
  if (ya !== yb) return ya < yb;
  return ax * by - ay * bx > 0;
}

/**
 * Bir haritanın duvarları. liste: [[x1, y1, x2, y2], ...] (kare); g, y: haritanın boyutu.
 */
export function duvarlarKur(liste, g, y) {
  const G = g > 0 ? g : 1;
  const Y = y > 0 ? y : 1;
  const gercek = liste.length;
  const n = gercek + 4;
  const p = new Float64Array(4 * n);
  liste.forEach((w, i) => p.set(w, 4 * i));
  p.set([0, 0, G, 0, G, 0, G, Y, G, Y, 0, Y, 0, Y, 0, 0], 4 * gercek);

  // ------------------------------------------------------------ ızgara dizini
  const h = Math.max(1.0, Math.max(G, Y) / 128);
  const hx = Math.max(1, Math.ceil(G / h));
  const hy = Math.max(1, Math.ceil(Y / h));
  const hucreX = (x) => {
    const i = Math.floor(x / h);
    return i < 0 ? 0 : i >= hx ? hx - 1 : i;
  };
  const hucreY = (v) => {
    const i = Math.floor(v / h);
    return i < 0 ? 0 : i >= hy ? hy - 1 : i;
  };
  const hn = hx * hy;
  const bas = new Int32Array(hn + 1);
  let dizi;
  {
    const say = new Int32Array(hn + 1);
    for (let gecis = 0; gecis < 2; gecis++) {
      if (gecis === 1) {
        for (let c = 0; c < hn; c++) bas[c + 1] = bas[c] + say[c];
        dizi = new Int32Array(Math.max(1, bas[hn]));
        say.fill(0);
      }
      for (let i = 0; i < n; i++) {
        const o = 4 * i;
        const x0 = hucreX(Math.min(p[o], p[o + 2]) - KAYIT_PAYI);
        const x1 = hucreX(Math.max(p[o], p[o + 2]) + KAYIT_PAYI);
        const y0 = hucreY(Math.min(p[o + 1], p[o + 3]) - KAYIT_PAYI);
        const y1 = hucreY(Math.max(p[o + 1], p[o + 3]) + KAYIT_PAYI);
        for (let cy = y0; cy <= y1; cy++)
          for (let cx = x0; cx <= x1; cx++) {
            const c = cy * hx + cx;
            if (gecis === 1) dizi[bas[c] + say[c]] = i;
            say[c]++;
          }
      }
    }
  }
  const damga = new Uint32Array(Math.max(1, n));
  let damgaNo = 0;
  const damgaYenile = () => {
    damgaNo = (damgaNo + 1) >>> 0;
    if (damgaNo === 0) {
      damga.fill(0);
      damgaNo = 1;
    }
  };
  const aday = new Int32Array(Math.max(1, n));
  function hucreDuvarlari(cx, cy, out, k) {
    const c = cy * hx + cx;
    for (let q = bas[c]; q < bas[c + 1]; q++) {
      const i = dizi[q];
      if (damga[i] === damgaNo) continue;
      damga[i] = damgaNo;
      out[k++] = i;
    }
    return k;
  }
  /* Parçanın geçtiği hücreler (sunucudaki gezginin aynısı) */
  function parcaYakini(ax, ay, bx, by, out) {
    damgaYenile();
    const x0 = ax / h;
    const y0 = ay / h;
    const x1 = bx / h;
    const y1 = by / h;
    let cx = hucreX(ax);
    let cy = hucreY(ay);
    const ex = hucreX(bx);
    const ey = hucreY(by);
    const sx = ex > cx ? 1 : ex < cx ? -1 : 0;
    const sy = ey > cy ? 1 : ey < cy ? -1 : 0;
    const axx = Math.abs(x1 - x0);
    const ayy = Math.abs(y1 - y0);
    const tdx = sx && axx > 0 ? 1 / axx : Infinity;
    const tdy = sy && ayy > 0 ? 1 / ayy : Infinity;
    let tmx = sx > 0 ? (cx + 1 - x0) * tdx : sx < 0 ? (x0 - cx) * tdx : Infinity;
    let tmy = sy > 0 ? (cy + 1 - y0) * tdy : sy < 0 ? (y0 - cy) * tdy : Infinity;
    let kalan = Math.abs(ex - cx) + Math.abs(ey - cy);
    let k = hucreDuvarlari(cx, cy, out, 0);
    while (kalan > 0) {
      kalan--;
      if (cy === ey || (cx !== ex && tmx < tmy)) {
        cx += sx;
        tmx += tdx;
      } else {
        cy += sy;
        tmy += tdy;
      }
      k = hucreDuvarlari(cx, cy, out, k);
    }
    return k;
  }
  function noktaYakini(x, v, out) {
    damgaYenile();
    return hucreDuvarlari(hucreX(x), hucreY(v), out, 0);
  }

  // ------------------------------------------------------------ duvarların kesiştiği noktalar
  const kes = [];
  for (let i = 0; i < gercek; i++) {
    const o = 4 * i;
    const m = parcaYakini(p[o], p[o + 1], p[o + 2], p[o + 3], aday);
    for (let k = 0; k < m; k++) {
      const j = aday[k];
      if (j <= i || j >= gercek) continue;
      const q = 4 * j;
      if (!capraz(p[o], p[o + 1], p[o + 2], p[o + 3], p, q)) continue;
      const rx = p[o + 2] - p[o];
      const ry = p[o + 3] - p[o + 1];
      const sx = p[q + 2] - p[q];
      const sy = p[q + 3] - p[q + 1];
      const den = rx * sy - ry * sx;
      if (Math.abs(den) < 1e-18) continue;
      const t = ((p[q] - p[o]) * sy - (p[q + 1] - p[o + 1]) * sx) / den;
      kes.push(p[o] + t * rx, p[o + 1] + t * ry);
    }
  }

  // ------------------------------------------------------------ yol düğümleri
  const dxy = [];
  function dugumGecerli(px, py, nx, ny) {
    if (nx < ACIKLIK || ny < ACIKLIK || nx > G - ACIKLIK || ny > Y - ACIKLIK) return false;
    let m = noktaYakini(nx, ny, aday);
    for (let k = 0; k < m; k++) {
      const o = 4 * aday[k];
      if (noktaParca(nx, ny, p[o], p[o + 1], p[o + 2], p[o + 3]) < ACIKLIK + 1e-6) return false;
    }
    m = parcaYakini(px, py, nx, ny, aday);
    for (let k = 0; k < m; k++) if (capraz(px, py, nx, ny, p, 4 * aday[k])) return false;
    return true;
  }
  function dugumEkle(px, py, vx, vy) {
    const L = Math.sqrt(vx * vx + vy * vy);
    if (L < 1e-12) return;
    vx /= L;
    vy /= L;
    let nx = px + KOSE_PAYI * vx;
    let ny = py + KOSE_PAYI * vy;
    if (!dugumGecerli(px, py, nx, ny)) {
      nx = px + KOSE_PAYI_DAR * vx;
      ny = py + KOSE_PAYI_DAR * vy;
      if (!dugumGecerli(px, py, nx, ny)) return;
    }
    dxy.push(nx, ny);
  }
  const yakin = new Int32Array(Math.max(1, n));
  for (let i = 0; i < gercek; i++) {
    for (let e = 0; e < 2; e++) {
      const px = p[4 * i + 2 * e];
      const py = p[4 * i + 2 * e + 1];
      const m = noktaYakini(px, py, yakin);
      let bakildi = false;
      for (let k = 0; k < m && !bakildi; k++) {
        const j = yakin[k];
        if (j >= gercek) continue;
        for (let f = 0; f < 2; f++) if (2 * j + f < 2 * i + e && uz(px, py, p[4 * j + 2 * f], p[4 * j + 2 * f + 1]) < KOSE_ESI) bakildi = true;
      }
      if (bakildi) continue;
      const yon = []; // [x, y] açıya göre sıralı
      for (let k = 0; k < m && yon.length < 62; k++) {
        const o = 4 * yakin[k];
        const v = [];
        if (uz(px, py, p[o], p[o + 1]) < KOSE_ESI) v.push([p[o + 2] - px, p[o + 3] - py]);
        else if (uz(px, py, p[o + 2], p[o + 3]) < KOSE_ESI) v.push([p[o] - px, p[o + 1] - py]);
        else if (noktaParca(px, py, p[o], p[o + 1], p[o + 2], p[o + 3]) < KOSE_ESI) {
          v.push([p[o + 2] - p[o], p[o + 3] - p[o + 1]]);
          v.push([p[o] - p[o + 2], p[o + 1] - p[o + 3]]);
        }
        for (const [vx, vy] of v) {
          const L = Math.sqrt(vx * vx + vy * vy);
          if (L < KOSE_ESI) continue;
          const ux = vx / L;
          const uy = vy / L;
          let ayni = false;
          for (const [rx, ry] of yon) if (Math.abs(ux * ry - uy * rx) < ACI_PAYI && ux * rx + uy * ry > 0) ayni = true;
          if (ayni) continue;
          let s = yon.length;
          yon.push(null);
          while (s > 0 && onceMi(ux, uy, yon[s - 1][0], yon[s - 1][1])) {
            yon[s] = yon[s - 1];
            s--;
          }
          yon[s] = [ux, uy];
        }
      }
      if (!yon.length) continue;
      if (yon.length === 1) {
        const [ax, ay] = yon[0];
        dugumEkle(px, py, -ax - ay, -ay + ax);
        dugumEkle(px, py, -ax + ay, -ay - ax);
        continue;
      }
      for (let s = 0; s < yon.length; s++) {
        const a = yon[s];
        const b = yon[(s + 1) % yon.length];
        const x = a[0] * b[0] + a[1] * b[1];
        const yy = a[0] * b[1] - a[1] * b[0];
        if (yy >= -ACI_PAYI) continue;
        let bx = -(a[0] + b[0]);
        let by = -(a[1] + b[1]);
        const L = Math.sqrt(bx * bx + by * by);
        if (L < 1e-12) continue;
        bx /= L;
        by /= L;
        if (x > 0) {
          dugumEkle(px, py, bx - a[1], by + a[0]);
          dugumEkle(px, py, bx + b[1], by - b[0]);
        } else {
          dugumEkle(px, py, bx, by);
        }
      }
    }
  }
  const dn = dxy.length / 2;
  const komsu = new Array(dn).fill(null);

  // ------------------------------------------------------------ görüş

  /* Açısal aralıklar: aci (sıralı), duvar (aralıkta ilk çarpılan duvar ya da -1) */
  function araliklar(ox, oy, r) {
    const yay = Number.isFinite(r);
    const ad = [];
    if (gercek) {
      damgaYenile();
      let x0 = 0;
      let x1 = hx - 1;
      let y0 = 0;
      let y1 = hy - 1;
      if (yay) {
        x0 = hucreX(ox - r) - 1;
        x1 = hucreX(ox + r) + 1;
        y0 = hucreY(oy - r) - 1;
        y1 = hucreY(oy + r) + 1;
      }
      for (let cy = Math.max(0, y0); cy <= y1 && cy < hy; cy++)
        for (let cx = Math.max(0, x0); cx <= x1 && cx < hx; cx++) {
          const c = cy * hx + cx;
          for (let q = bas[c]; q < bas[c + 1]; q++) {
            const i = dizi[q];
            if (i >= gercek || damga[i] === damgaNo) continue;
            damga[i] = damgaNo;
            const o = 4 * i;
            const m = noktaParca(ox, oy, p[o], p[o + 1], p[o + 2], p[o + 3]);
            if (m <= r && m > UST) ad.push(i);
          }
        }
    }
    let aci = [];
    if (yay) for (let i = 0; i < YAY_PARCA; i++) aci.push(-Math.PI + (2 * Math.PI * i) / YAY_PARCA);
    const uc = new Float64Array(2 * ad.length);
    ad.forEach((w, k) => {
      const o = 4 * w;
      for (let e = 0; e < 2; e++) {
        uc[2 * k + e] = Math.atan2(p[o + 2 * e + 1] - oy, p[o + 2 * e] - ox);
        if (!yay || uz(ox, oy, p[o + 2 * e], p[o + 2 * e + 1]) <= r) aci.push(uc[2 * k + e]);
      }
      if (!yay) return;
      const dx = p[o + 2] - p[o];
      const dy = p[o + 3] - p[o + 1];
      const fx = p[o] - ox;
      const fy = p[o + 1] - oy;
      const qa = dx * dx + dy * dy;
      const qb = 2 * (fx * dx + fy * dy);
      const qc = fx * fx + fy * fy - r * r;
      const disk = qb * qb - 4 * qa * qc;
      if (qa > 0 && disk > 0) {
        const kok = Math.sqrt(disk);
        for (const s of [-1, 1]) {
          const t = (-qb + s * kok) / (2 * qa);
          if (t > 0 && t < 1) aci.push(Math.atan2(p[o + 1] + t * dy - oy, p[o] + t * dx - ox));
        }
      }
    });
    if (ad.length) for (let k = 0; k < kes.length; k += 2) if (uz(ox, oy, kes[k], kes[k + 1]) <= r) aci.push(Math.atan2(kes[k + 1] - oy, kes[k] - ox));
    aci.sort((a, b) => a - b);
    const u = [];
    for (const a of aci) if (!u.length || a - u[u.length - 1] > 1e-9) u.push(a);
    aci = u;
    if (aci.length > 1 && aci[aci.length - 1] - aci[0] > 2 * Math.PI - 1e-9) aci.pop();
    let kovaBas = null;
    let kova = null;
    if (ad.length) {
      const say = new Int32Array(KOVA + 1);
      const ara = [];
      for (let k = 0; k < ad.length; k++) {
        const a1 = uc[2 * k];
        const a2 = uc[2 * k + 1];
        let fark = a2 - a1;
        while (fark > Math.PI) fark -= 2 * Math.PI;
        while (fark < -Math.PI) fark += 2 * Math.PI;
        const k0 = kovaNo(fark >= 0 ? a1 : a2);
        const adet = Math.min(KOVA, Math.floor((Math.abs(fark) / (2 * Math.PI)) * KOVA) + 2);
        ara.push([k0, adet]);
        for (let q = 0; q < adet; q++) say[(k0 + q) % KOVA]++;
      }
      kovaBas = new Int32Array(KOVA + 1);
      for (let q = 0; q < KOVA; q++) kovaBas[q + 1] = kovaBas[q] + say[q];
      kova = new Int32Array(Math.max(1, kovaBas[KOVA]));
      say.fill(0);
      for (let k = 0; k < ad.length; k++)
        for (let q = 0; q < ara[k][1]; q++) {
          const kk = (ara[k][0] + q) % KOVA;
          kova[kovaBas[kk] + say[kk]++] = k;
        }
    }
    const duvar = new Int32Array(aci.length);
    for (let i = 0; i < aci.length; i++) {
      const a0 = aci[i];
      const a1 = i + 1 < aci.length ? aci[i + 1] : aci[0] + 2 * Math.PI;
      const orta = (a0 + a1) / 2;
      const ux = Math.cos(orta);
      const uy = Math.sin(orta);
      let en = r;
      duvar[i] = -1;
      if (!ad.length) continue;
      const kk = kovaNo(orta);
      for (let q = kovaBas[kk]; q < kovaBas[kk + 1]; q++) {
        const t = isinParca(ox, oy, ux, uy, p, 4 * ad[kova[q]]);
        if (t >= 0 && t < en) {
          en = t;
          duvar[i] = ad[kova[q]];
        }
      }
    }
    return { aci, duvar };
  }

  function araliktaGorunur(a, i, ox, oy, vx, vy) {
    const w = a.duvar[i];
    if (w < 0) return true;
    const L = uz(ox, oy, vx, vy);
    if (L < 1e-12) return true;
    const t = isinDogru(ox, oy, (vx - ox) / L, (vy - oy) / L, p, 4 * w);
    return t < 0 || L <= t + 1e-6;
  }

  function gorunenDugumler(ox, oy, haric) {
    const a = araliklar(ox, oy, Infinity);
    const out = [];
    const N = a.aci.length;
    for (let v = 0; v < dn; v++) {
      if (v === haric) continue;
      const vx = dxy[2 * v];
      const vy = dxy[2 * v + 1];
      if (!N) {
        out.push(v);
        continue;
      }
      const t = Math.atan2(vy - oy, vx - ox);
      let lo = 0;
      let hi = N - 1;
      let i = N - 1;
      while (lo <= hi) {
        const o = (lo + hi) >> 1;
        if (a.aci[o] <= t) {
          i = o;
          lo = o + 1;
        } else {
          hi = o - 1;
        }
      }
      if (t < a.aci[0]) i = N - 1;
      let gor = araliktaGorunur(a, i, ox, oy, vx, vy);
      if (!gor && Math.abs(t - a.aci[i]) < 1e-7) gor = araliktaGorunur(a, (i + N - 1) % N, ox, oy, vx, vy);
      if (!gor && Math.abs((i + 1 < N ? a.aci[i + 1] : a.aci[0] + 2 * Math.PI) - t) < 1e-7) gor = araliktaGorunur(a, (i + 1) % N, ox, oy, vx, vy);
      if (gor) out.push(v);
    }
    return out;
  }

  // ------------------------------------------------------------ yol

  function kenarGecerli(ax, ay, aUc, bx, by, bUc) {
    const m = parcaYakini(ax, ay, bx, by, aday);
    for (let k = 0; k < m; k++) {
      const o = 4 * aday[k];
      if (engelliyor(ax, ay, bx, by, p, o)) return false;
      let gerek = ACIKLIK;
      if (aUc) gerek = Math.min(gerek, noktaParca(ax, ay, p[o], p[o + 1], p[o + 2], p[o + 3]) - 1e-9);
      if (bUc) gerek = Math.min(gerek, noktaParca(bx, by, p[o], p[o + 1], p[o + 2], p[o + 3]) - 1e-9);
      if (gerek > 0 && parcaMesafesi(ax, ay, bx, by, p, o) < gerek) return false;
    }
    return true;
  }

  /* (x,y)'ye en yakın gerçek duvar (harita sınırı sayılmaz) ve uzaklığı: [sıra, uzaklık]; KAYIT_PAYI'ndan uzaktakiler
     yok sayılır (yakında duvar yoksa [-1, KAYIT_PAYI]) */
  function enYakinDuvar(x, v) {
    const m = noktaYakini(x, v, aday);
    let en = -1;
    let u = KAYIT_PAYI;
    for (let k = 0; k < m; k++) {
      const i = aday[k];
      if (i >= gercek) continue;
      const o = 4 * i;
      const w = noktaParca(x, v, p[o], p[o + 1], p[o + 2], p[o + 3]);
      if (w < u || (w === u && en >= 0 && i < en)) {
        u = w;
        en = i;
      }
    }
    return [en, u];
  }

  /* B, o'daki duvardan UC_PAYI uzağa itilir: tam duvarın üstündeyse A'dan geldiği yana, değilse kendi yanına. Yön belli
     değilse (A da duvarın doğrusunda) null. */
  function duvardanIt(o, ax, ay, bx, by) {
    const dx = p[o + 2] - p[o];
    const dy = p[o + 3] - p[o + 1];
    const L2 = dx * dx + dy * dy;
    let t = L2 > 0 ? ((bx - p[o]) * dx + (by - p[o + 1]) * dy) / L2 : 0;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    const nx = p[o] + t * dx;
    const ny = p[o + 1] + t * dy;
    let ux = bx - nx;
    let uy = by - ny;
    let u = Math.sqrt(ux * ux + uy * uy);
    if (u > UST) {
      ux /= u;
      uy /= u;
    } else if (t > 0 && t < 1) {
      const s = yan(p[o], p[o + 1], p[o + 2], p[o + 3], ax, ay);
      const L = Math.sqrt(L2);
      if (Math.abs(s) <= UST) return null;
      ux = (s > 0 ? -dy : dy) / L;
      uy = (s > 0 ? dx : -dx) / L;
    } else {
      ux = ax - bx;
      uy = ay - by;
      u = Math.sqrt(ux * ux + uy * uy);
      if (u <= UST) return null;
      ux /= u;
      uy /= u;
    }
    return [nx + ux * UC_PAYI, ny + uy * UC_PAYI];
  }

  /* Yolun son kenarı A'dan B'ye: karakter duvarın üstünde ya da dibinde durmasın (sunucudaki uca_cek'in aynısı).
     Döner: [x, y] */
  function ucaCek(ax, ay, aUc, bx, by) {
    let [w, u] = enYakinDuvar(bx, by);
    if (w < 0 || u >= UC_PAYI / 2) return [bx, by];
    const gerek = Math.min(UC_PAYI / 2, enYakinDuvar(ax, ay)[1]);
    if (gerek <= UST) return [bx, by];
    let x = bx;
    let y2 = by;
    for (let k = 0; k < 4 && w >= 0 && u < gerek; k++) {
      const itilen = duvardanIt(4 * w, ax, ay, x, y2);
      if (!itilen) break;
      [x, y2] = itilen;
      [w, u] = enYakinDuvar(x, y2);
    }
    if (u >= gerek && x >= 0 && y2 >= 0 && x <= G && y2 <= Y && kenarGecerli(ax, ay, aUc, x, y2, true)) return [x, y2];
    let lo = 0;
    let hi = 1;
    for (let k = 0; k < 48; k++) {
      const t = (lo + hi) / 2;
      if (enYakinDuvar(bx + (ax - bx) * t, by + (ay - by) * t)[1] >= gerek) hi = t;
      else lo = t;
    }
    return [bx + (ax - bx) * hi, by + (ay - by) * hi];
  }

  function komsular(u) {
    if (!komsu[u]) {
      const ux = dxy[2 * u];
      const uy = dxy[2 * u + 1];
      komsu[u] = gorunenDugumler(ux, uy, u).filter((v) => kenarGecerli(ux, uy, false, dxy[2 * v], dxy[2 * v + 1], false));
    }
    return komsu[u];
  }

  /* İkili yığın: (f, i) küçükten büyüğe; eşitlikte küçük sıra numarası */
  function yigin() {
    const f = [];
    const ix = [];
    const once = (a, b) => f[a] < f[b] || (f[a] === f[b] && ix[a] < ix[b]);
    const takas = (a, b) => {
      [f[a], f[b]] = [f[b], f[a]];
      [ix[a], ix[b]] = [ix[b], ix[a]];
    };
    return {
      it(fv, i) {
        let k = f.length;
        f.push(fv);
        ix.push(i);
        while (k > 0) {
          const ust = (k - 1) >> 1;
          if (!once(k, ust)) break;
          takas(k, ust);
          k = ust;
        }
      },
      al() {
        if (!f.length) return null;
        const sonuc = [f[0], ix[0]];
        const sf = f.pop();
        const si = ix.pop();
        if (f.length) {
          f[0] = sf;
          ix[0] = si;
          let k = 0;
          for (;;) {
            const a = 2 * k + 1;
            const b = a + 1;
            let en = k;
            if (a < f.length && once(a, en)) en = a;
            if (b < f.length && once(b, en)) en = b;
            if (en === k) break;
            takas(k, en);
            k = en;
          }
        }
        return sonuc;
      },
    };
  }

  return {
    get bos() {
      return gercek === 0;
    },
    /** A ile B arasında görüşü kesen duvar yok mu */
    gorusAcik(ax, ay, bx, by) {
      if (!gercek) return true;
      const m = parcaYakini(ax, ay, bx, by, aday);
      for (let k = 0; k < m; k++) if (aday[k] < gercek && engelliyor(ax, ay, bx, by, p, 4 * aday[k])) return false;
      return true;
    },
    /** (ox,oy)'dan r yarıçapla görülen alan: [x0, y0, x1, y1, ...], açıya göre sıralı */
    gorusCokgeni(ox, oy, r) {
      if (!(r > 0) || !Number.isFinite(r)) return [];
      const a = araliklar(ox, oy, r);
      const out = [];
      const N = a.aci.length;
      for (let i = 0; i < N; i++) {
        const a0 = a.aci[i];
        const a1 = i + 1 < N ? a.aci[i + 1] : a.aci[0] + 2 * Math.PI;
        const u0x = Math.cos(a0);
        const u0y = Math.sin(a0);
        const u1x = Math.cos(a1);
        const u1y = Math.sin(a1);
        let t0 = r;
        let t1 = r;
        if (a.duvar[i] >= 0) {
          const o = 4 * a.duvar[i];
          t0 = Math.min(r, Math.max(0, isinDogru(ox, oy, u0x, u0y, p, o)));
          t1 = Math.min(r, Math.max(0, isinDogru(ox, oy, u1x, u1y, p, o)));
        }
        for (const [x, y2] of [
          [ox + t0 * u0x, oy + t0 * u0y],
          [ox + t1 * u1x, oy + t1 * u1y],
        ]) {
          const k = out.length;
          if (k && Math.abs(out[k - 2] - x) < 1e-9 && Math.abs(out[k - 1] - y2) < 1e-9) continue;
          out.push(x, y2);
        }
      }
      const k = out.length;
      if (k > 2 && Math.abs(out[0] - out[k - 2]) < 1e-9 && Math.abs(out[1] - out[k - 1]) < 1e-9) out.length -= 2;
      return out;
    },
    /** S'den G'ye duvarların etrafından dolaşan en kısa yol: { xy: [x0, y0, ...], uzunluk } ya da yol yoksa null */
    yolBul(sx, sy, gx, gy) {
      if (uz(sx, sy, gx, gy) < 1e-9 || kenarGecerli(sx, sy, true, gx, gy, true)) {
        if (uz(sx, sy, gx, gy) >= 1e-9) [gx, gy] = ucaCek(sx, sy, true, gx, gy);
        return { xy: [sx, sy, gx, gy], uzunluk: uz(sx, sy, gx, gy) };
      }
      const V = dn;
      const S = V;
      const H = V + 1;
      const X = (i) => (i === S ? sx : i === H ? gx : dxy[2 * i]);
      const Yk = (i) => (i === S ? sy : i === H ? gy : dxy[2 * i + 1]);
      const g = new Float64Array(V + 2).fill(Infinity);
      const ata = new Int32Array(V + 2).fill(-1);
      const kapali = new Uint8Array(V + 2);
      g[S] = 0;
      const yg = yigin();
      for (const v of gorunenDugumler(sx, sy, -1)) {
        if (!kenarGecerli(sx, sy, true, X(v), Yk(v), false)) continue;
        g[v] = uz(sx, sy, X(v), Yk(v));
        ata[v] = S;
        yg.it(g[v] + uz(X(v), Yk(v), gx, gy), v);
      }
      for (let o = yg.al(); o; o = yg.al()) {
        const u = o[1];
        if (kapali[u]) continue;
        if (u === H) break;
        kapali[u] = 1;
        if (kenarGecerli(X(u), Yk(u), false, gx, gy, true)) {
          const c = g[u] + uz(X(u), Yk(u), gx, gy);
          if (c < g[H]) {
            g[H] = c;
            ata[H] = u;
            yg.it(c, H);
          }
        }
        for (const v of komsular(u)) {
          if (kapali[v]) continue;
          const c = g[u] + uz(X(u), Yk(u), X(v), Yk(v));
          if (c < g[v]) {
            g[v] = c;
            ata[v] = u;
            yg.it(c + uz(X(v), Yk(v), gx, gy), v);
          }
        }
      }
      if (ata[H] < 0) return null;
      const nk = [];
      for (let i = H; ; i = ata[i]) {
        nk.push(X(i), Yk(i));
        if (i === S) break;
      }
      const xy = [];
      for (let k = nk.length - 2; k >= 0; k -= 2) xy.push(nk[k], nk[k + 1]);
      const q = ata[H];
      const son = ucaCek(X(q), Yk(q), q === S, xy[xy.length - 2], xy[xy.length - 1]);
      xy[xy.length - 2] = son[0];
      xy[xy.length - 1] = son[1];
      return { xy, uzunluk: g[q] + uz(X(q), Yk(q), son[0], son[1]) };
    },
  };
}

/** Yolun uzunluğu */
export function yolUzunlugu(xy) {
  let L = 0;
  for (let i = 0; i + 3 < xy.length; i += 2) L += uz(xy[i], xy[i + 1], xy[i + 2], xy[i + 3]);
  return L;
}

/** Yolun s uzunluk ilerisindeki noktası */
export function yolNoktasi(xy, s) {
  if (xy.length < 2) return { x: 0, y: 0 };
  if (s <= 0) return { x: xy[0], y: xy[1] };
  for (let i = 0; i + 3 < xy.length; i += 2) {
    const L = uz(xy[i], xy[i + 1], xy[i + 2], xy[i + 3]);
    if (s <= L && L > 0) return { x: xy[i] + ((xy[i + 2] - xy[i]) * s) / L, y: xy[i + 1] + ((xy[i + 3] - xy[i + 1]) * s) / L };
    s -= L;
  }
  return { x: xy[xy.length - 2], y: xy[xy.length - 1] };
}

/** Bir noktanın yola uzaklığı */
export function yolaUzaklik(xy, px, py) {
  if (xy.length === 2) return uz(px, py, xy[0], xy[1]);
  let en = Infinity;
  for (let i = 0; i + 3 < xy.length; i += 2) en = Math.min(en, noktaParca(px, py, xy[i], xy[i + 1], xy[i + 2], xy[i + 3]));
  return en;
}
