// lib/app/app_puente.js: navegación por rutas y re-render con foco para apps de lib/app.
// Envuelve LgPuente (lib/ui/puente.js), que no se modifica. Escrito a mano, sin dependencias. Solo navegador.
//
//   const app = await AppPuente.montar({
//     wasm: 'app_rutas.wasm',
//     contenedor: document.getElementById('app'),
//     base: '/mi-app',                   // opcional: carpeta de la app; las rutas son relativas a ella
//     nombres: {},                       // opcional: nombres de los exports (valores por defecto: vista, ruta_inicio, ruta_byte, ruta_cambio, evento)
//     alCambiar: (valor) => {}           // opcional: recibe lo que devuelve evento(id)
//   });
//   app.ir('/detalle/42');               // pushState, ruta_cambio y re-pintado
//
// Contrato de exports (el archivo principal de la app los define; ver lib/app/README.md):
//   vista() -> str              HTML de la pantalla actual (se pone en innerHTML)
//   ruta_inicio() -> int        vacía el búfer del path
//   ruta_byte(b: int) -> int    añade un byte del path (el navegador lo da en ASCII)
//   ruta_cambio() -> int        resuelve el path del búfer y guarda la pantalla en el estado
//   evento(id: int) -> int      un elemento con data-accion="N" llama a evento(N) al pulsarlo;
//                               un elemento con data-cambio="N" (p. ej. un div que envuelve una entrada)
//                               llama a evento(N) en cada evento input
//
// Enlaces: un elemento con data-ruta="/x" (a con href, o button) navega sin recargar.
// Atrás y adelante: popstate resuelve la ruta de la URL y re-pinta.
// Entradas: antes de pintar se guardan los valores de todos los input y textarea del contenedor, y el foco y
// la selección de la entrada activa; tras pintar se restauran. Clave: id, o name si no hay id.
// Limitación: un clic de ratón en un botón mueve el foco al botón antes de pintar (comportamiento del
// navegador), así que el foco vuelve a la entrada solo con cambios de la entrada o con clics por código.
// Un valor que Lenguaje quiera imponer a una entrada no llega si el DOM ya tiene otro valor.
(function (ambito) {
  'use strict';

  const NOMBRES = { vista: 'vista', rutaInicio: 'ruta_inicio', rutaByte: 'ruta_byte', rutaCambio: 'ruta_cambio', evento: 'evento' };

  async function montar(opciones) {
    const nombres = Object.assign({}, NOMBRES, opciones.nombres || {});
    const cont = opciones.contenedor;
    // base: carpeta de la app en el servidor (p. ej. '/mi-app'). Las rutas de la app son relativas a ella.
    const base = (opciones.base || '').replace(/\/$/, '');
    const dentroBase = (pathname) => (pathname.startsWith(base) ? pathname.slice(base.length) || '/' : pathname);
    const p = await globalThis.LgPuente.cargar(opciones.wasm);
    p.iniciar();  // ejecuta el main: inicializa las globales antes de cualquier export

    const enc = new TextEncoder();

    // Valores de las entradas (input y textarea) y foco: el HTML se regenera entero, así que se guardan antes
    // y se restauran después. La clave es el id, o el name si no hay id (lib/app/primitivas no da id a entrada).
    const claveDe = (el) => (el.id ? 'id:' + el.id : el.getAttribute('name') !== null ? 'name:' + el.getAttribute('name') : null);
    const entradas = () => [...cont.querySelectorAll('input,textarea')];
    const guardarEntradas = () => {
      const activo = document.activeElement;
      return entradas().map((el) => {
        let sel = null;
        if (el === activo) { try { sel = [el.selectionStart, el.selectionEnd]; } catch (e) { sel = null; } }
        return { clave: claveDe(el), valor: el.value, enfocada: el === activo, sel: sel };
      });
    };
    const restaurarEntradas = (datos) => {
      for (const d of datos) {
        if (d.clave === null) continue;
        const el = entradas().find((c) => claveDe(c) === d.clave);
        if (!el) continue;
        if (el.value !== d.valor) el.value = d.valor;
        if (d.enfocada) {
          el.focus();
          if (d.sel && d.sel[0] !== null) { try { el.setSelectionRange(d.sel[0], d.sel[1]); } catch (e) { /* sin selección */ } }
        }
      }
    };

    const pinta = () => {
      const datos = guardarEntradas();
      cont.innerHTML = p.texto(nombres.vista);
      restaurarEntradas(datos);
    };

    // Pasa el path al módulo Lenguaje byte a byte (los exports solo reciben enteros).
    // Una app sin rutas (sin los exports de navegación) pinta la vista tal cual.
    const hayRutas = typeof p.exports[nombres.rutaCambio] === 'function';
    const resolver = (pathname) => {
      if (!hayRutas) return 0;
      p.llamar(nombres.rutaInicio);
      for (const b of enc.encode(pathname)) p.llamar(nombres.rutaByte, BigInt(b));
      return Number(p.llamar(nombres.rutaCambio));
    };

    // ruta: ruta de la app ('/detalle/42', con consulta opcional). La URL del navegador es base + ruta.
    const ir = (ruta) => {
      const sinConsulta = ruta.split(/[?#]/)[0];
      history.pushState({ lg: true }, '', base + ruta);
      const pantalla = resolver(sinConsulta);
      pinta();
      return pantalla;
    };

    cont.addEventListener('click', (e) => {
      const enlace = e.target.closest('[data-ruta]');
      if (enlace && cont.contains(enlace)) {
        if (e.button !== 0 || e.metaKey || e.ctrlKey || e.shiftKey || e.altKey) return;
        e.preventDefault();
        ir(enlace.dataset.ruta);
        return;
      }
      const boton = e.target.closest('[data-accion]');
      if (boton && cont.contains(boton)) {
        const valor = p.llamar(nombres.evento, BigInt(boton.dataset.accion));
        if (opciones.alCambiar) opciones.alCambiar(valor);
        pinta();
      }
    });

    // Cambio de una entrada (evento input): un elemento con data-cambio="N" (p. ej. un div que envuelve
    // la entrada) llama a evento(N) en cada tecla. Aquí el foco y la selección importan de verdad.
    cont.addEventListener('input', (e) => {
      const c = e.target.closest('[data-cambio]');
      if (!c || !cont.contains(c)) return;
      const valor = p.llamar(nombres.evento, BigInt(c.dataset.cambio));
      if (opciones.alCambiar) opciones.alCambiar(valor);
      pinta();
    });

    addEventListener('popstate', () => {
      resolver(dentroBase(location.pathname));
      pinta();
    });

    resolver(dentroBase(location.pathname));
    pinta();

    return {
      ir: ir,
      pinta: pinta,
      llamar: (nombre, ...args) => {
        const valor = p.llamar(nombre, ...args);
        pinta();
        return valor;
      },
      p: p,
    };
  }

  const api = { montar };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else ambito.AppPuente = api;
})(globalThis);
