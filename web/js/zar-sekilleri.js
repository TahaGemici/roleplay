import { svg } from './ortak.js';

// Zarların iki boyutlu siluetleri (40×40 birimlik çizim alanı). Aynı şekiller tepside ve kayıtta kullanılır.
const SEKIL = {
  4: { noktalar: '20,3 37.5,35.5 2.5,35.5', ty: 26.5 },
  6: { kare: true, ty: 20.5 },
  8: { noktalar: '20,1.5 38.5,20 20,38.5 1.5,20', ty: 20.5 },
  10: { noktalar: '20,1.5 38.5,15.5 20,38.5 1.5,15.5', ty: 18.5 },
  12: { noktalar: '20,1.5 38.5,15 31.5,37.5 8.5,37.5 1.5,15', ty: 22 },
  20: { noktalar: '20,1.5 37,11 37,29 20,38.5 3,29 3,11', ty: 20.5 },
};
SEKIL[100] = SEKIL[10];

export function zarSimgesi(yuz, etiket, { siniflar = '', elenmis = false } = {}) {
  const s = SEKIL[yuz];
  const el = svg('svg', { viewBox: '0 0 40 40', class: `die ${siniflar}`.trim(), 'aria-hidden': 'true', focusable: 'false' });
  if (!s) el.append(svg('circle', { class: 'die-shape', cx: 20, cy: 20, r: 18 }));
  else if (s.kare) el.append(svg('rect', { class: 'die-shape', x: 3, y: 3, width: 34, height: 34, rx: 7 }));
  else el.append(svg('polygon', { class: 'die-shape', points: s.noktalar }));
  const metin = String(etiket);
  el.append(
    svg('text', {
      x: 20,
      y: s ? s.ty : 20.5,
      'text-anchor': 'middle',
      'dominant-baseline': 'central',
      class: metin.length >= 4 ? 't4' : metin.length === 3 ? 't3' : null,
      text: metin,
    }),
  );
  if (elenmis) el.append(svg('line', { class: 'die-strike', x1: 9, y1: 31, x2: 31, y2: 9 }));
  return el;
}
