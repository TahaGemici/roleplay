/*
 * Sürüm ve tarayıcının belleği. Sayfa, sunucunun o anki sürümüyle gelir (index.html'deki roleplay-surum). Bellekte
 * (localStorage) başka bir sürüm kayıtlıysa önce hepsi silinir: güncellemeden sonra eski sürümün kalıntısı
 * karışmasın. Açık bir sayfanın sunucusu güncellenirse (yeniden bağlanırken sürüm tutmaz) bellek silinir ve sayfa
 * kendini yeniler.
 */
const ANAHTAR = 'roleplay.surum';
const YENILENDI = 'roleplay.yenilendi'; // aynı sürüm için bir kez yenile; yine tutmazsa döngüye girme

export const SURUM = document.querySelector('meta[name="roleplay-surum"]')?.content || '';

async function hepsiniSil() {
  try {
    localStorage.clear();
  } catch {
    /* tarayıcı izin vermiyor */
  }
  try {
    sessionStorage.clear();
  } catch {
    /* tarayıcı izin vermiyor */
  }
  try {
    if (window.caches) for (const ad of await caches.keys()) await caches.delete(ad);
  } catch {
    /* önemsiz */
  }
  try {
    for (const k of (await navigator.serviceWorker?.getRegistrations?.()) || []) await k.unregister();
  } catch {
    /* önemsiz */
  }
  try {
    /* Sunucu HTTP önbelleğini sildirir (Clear-Site-Data) */
    await fetch('/api/temizle', { method: 'POST', cache: 'no-store', credentials: 'same-origin' });
  } catch {
    /* sunucuya ulaşılamadı */
  }
}

/** Sayfa açılırken: bellek başka bir sürümünse silinir, bu sürüm yazılır. */
export async function bellegiDenetle() {
  let kayitli = null;
  try {
    kayitli = localStorage.getItem(ANAHTAR);
  } catch {
    return;
  }
  if (kayitli === SURUM) return;
  await hepsiniSil();
  try {
    localStorage.setItem(ANAHTAR, SURUM);
  } catch {
    /* önemsiz */
  }
}

/** Sunucu bu sayfayla aynı sürümde: yenileme kaydı kalkar. */
export function surumTuttu() {
  try {
    sessionStorage.removeItem(YENILENDI);
  } catch {
    /* önemsiz */
  }
}

/** Sunucu başka bir sürümde: bellek silinir ve sayfa yenilenir. Bu sürüm için zaten yenilendiyse false döner. */
export async function yeniSurumeGec(yeni) {
  let son = null;
  try {
    son = sessionStorage.getItem(YENILENDI);
  } catch {
    /* önemsiz */
  }
  if (son && son === String(yeni)) return false;
  await hepsiniSil();
  try {
    sessionStorage.setItem(YENILENDI, String(yeni));
  } catch {
    /* önemsiz */
  }
  location.reload();
  return true;
}
