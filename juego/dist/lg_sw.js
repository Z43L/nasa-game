// Service worker de una PWA de lib/app. Escrito a mano, sin dependencias.
// Configuración: el shell (rutas relativas a la ubicación de este script, o a la del script que lo
// importa) se define en self.LG_PWA_SHELL antes de importScripts. Sin esa variable, solo "./".
// Estrategias: shell = cache-first; el resto de GET del mismo origen = network-first con copia
// de respaldo. Versionado: cambiar VERSION invalida la caché anterior en activate.
// Solo se cachean respuestas 200. Las peticiones que no son GET no se tocan.
const VERSION = 'v1';
const PREFIJO = 'lg-pwa-';
const CACHE = PREFIJO + VERSION;
const BASE = self.location.href;
const SHELL = (self.LG_PWA_SHELL || ['./']).map((r) => new URL(r, BASE).href);
const EN_SHELL = new Set(SHELL);

self.addEventListener('install', (evento) => {
  evento.waitUntil(
    caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting())
  );
});

self.addEventListener('activate', (evento) => {
  evento.waitUntil(
    caches.keys()
      .then((claves) => Promise.all(
        claves.filter((k) => k.startsWith(PREFIJO) && k !== CACHE).map((k) => caches.delete(k))
      ))
      .then(() => self.clients.claim())
  );
});

async function cacheFirst(peticion) {
  const c = await caches.open(CACHE);
  const copia = await c.match(peticion);
  if (copia) return copia;
  const red = await fetch(peticion);
  if (red.status === 200) c.put(peticion, red.clone());
  return red;
}

async function networkFirst(peticion) {
  const c = await caches.open(CACHE);
  try {
    const red = await fetch(peticion);
    if (red.status === 200) c.put(peticion, red.clone());
    return red;
  } catch (error) {
    const copia = await c.match(peticion);
    if (copia) return copia;
    if (peticion.mode === 'navigate') {
      const inicio = await c.match(SHELL[0]);
      if (inicio) return inicio;
    }
    return new Response('Sin conexión y sin copia de este recurso.', {
      status: 503,
      headers: { 'Content-Type': 'text/plain; charset=utf-8' },
    });
  }
}

self.addEventListener('fetch', (evento) => {
  const peticion = evento.request;
  if (peticion.method !== 'GET') return;
  const url = new URL(peticion.url);
  if (url.origin !== self.location.origin) return;
  evento.respondWith(EN_SHELL.has(url.href) ? cacheFirst(peticion) : networkFirst(peticion));
});
