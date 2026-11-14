// Service worker del ejemplo pwa_app.html. Define el shell (rutas relativas a esta carpeta) y
// carga el sw genérico de lib/app/pwa/sw.js. Su ámbito es tests/wasm_ui/navegador/.
self.LG_PWA_SHELL = [
  './',
  'pwa_app.html',
  'pwa_app.wasm',
  '../../../lib/ui/puente.js',
  '../../../lib/app/pwa/registro.js',
  '../../../lib/app/pwa/manifest.webmanifest',
  '../../../lib/app/pwa/icons/icon-192.png',
  '../../../lib/app/pwa/icons/icon-512.png'
];
importScripts('../../../lib/app/pwa/sw.js');
