// Registro del service worker desde la página. Escrito a mano, sin dependencias.
// Lo carga pwa_cabeceras() con <script src="...registro.js" data-sw="sw.js">: la ruta del
// service worker se lee de data-sw y se resuelve contra la página. Nunca bloquea la carga:
// el registro se pospone al evento load y cualquier error solo se registra en consola.
(function () {
  'use strict';
  try {
    if (!('serviceWorker' in navigator)) return;
    const etiqueta = document.currentScript;
    const sw = (etiqueta && etiqueta.getAttribute('data-sw')) || 'sw.js';
    const registrar = () => {
      try {
        navigator.serviceWorker.register(sw).catch((e) => console.warn('service worker no registrado:', e));
      } catch (e) {
        console.warn('service worker no disponible:', e);
      }
    };
    if (document.readyState === 'complete') registrar();
    else window.addEventListener('load', registrar);
  } catch (e) {
    console.warn('registro de service worker omitido:', e);
  }
})();
