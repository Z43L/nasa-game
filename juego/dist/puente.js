// Puente JS para programas .lg compilados con `lgc --target wasm32 --export NOMBRE ...`.
// Escrito a mano, sin dependencias. Funciona en navegador y en Node (solo usa WebAssembly, fetch,
// TextDecoder, TextEncoder/crypto y console, presentes en ambos). Contrato completo en tests/wasm_ui/.
//   const p = await LgPuente.cargar('prog.wasm');   // o cargar(ArrayBuffer | Uint8Array)
//   p.iniciar();                                      // ejecuta el main (sentencias de nivel superior)
//   p.llamar('nombre', 3n);                           // int de .lg = i64 = BigInt; float = Number; bool = 0/1
//   p.texto('nombre', 3n);                            // export con retorno str -> string UTF-8
(function (ambito) {
  'use strict';
  class Salida extends Error {
    constructor(codigo) { super('proc_exit ' + codigo); this.codigo = codigo; }
  }
  async function cargar(fuente) {
    const bytes = ArrayBuffer.isView(fuente) || fuente instanceof ArrayBuffer ? fuente : await (await fetch(fuente)).arrayBuffer();
    const mod = await WebAssembly.compile(bytes);
    const dec = new TextDecoder();
    const enc = new TextEncoder();
    const pend = {};  // texto sin salto de línea pendiente, por descriptor (1 y 2)
    let mem = null;   // se consulta siempre: la memoria lineal puede crecer (cambia .buffer)
    const dv = () => new DataView(mem.buffer);
    const u8 = () => new Uint8Array(mem.buffer);
    const emitir = (fd, t) => {
      const partes = ((pend[fd] || '') + t).split('\n');
      pend[fd] = partes.pop();
      for (const l of partes) (fd === 2 ? console.error : console.log)(l);
    };
    const argv = enc.encode('programa\0');
    const wasi = {  // imports de wasi_snapshot_preview1 que usa el runtime; el resto da ENOSYS (52)
      fd_write(fd, iovs, n, nwritten) {
        let total = 0, texto = '';
        for (let i = 0; i < n; i++) {
          const p = dv().getUint32(iovs + 8 * i, true), len = dv().getUint32(iovs + 8 * i + 4, true);
          texto += dec.decode(u8().subarray(p, p + len)); total += len;
        }
        emitir(fd, texto);
        dv().setUint32(nwritten, total, true);
        return 0;
      },
      proc_exit(codigo) { throw new Salida(codigo); },
      clock_time_get(id, precision, salida) { dv().setBigUint64(salida, BigInt(Date.now()) * 1000000n, true); return 0; },
      random_get(buf, len) { crypto.getRandomValues(u8().subarray(buf, buf + len)); return 0; },
      args_sizes_get(argc, tam) { dv().setUint32(argc, 1, true); dv().setUint32(tam, argv.length, true); return 0; },
      args_get(punteros, buf) { u8().set(argv, buf); dv().setUint32(punteros, buf, true); return 0; },
      environ_sizes_get(n, tam) { dv().setUint32(n, 0, true); dv().setUint32(tam, 0, true); return 0; },
      environ_get() { return 0; },
    };
    for (const imp of WebAssembly.Module.imports(mod))
      if (imp.module === 'wasi_snapshot_preview1' && !(imp.name in wasi)) wasi[imp.name] = () => 52;
    const inst = await WebAssembly.instantiate(mod, { wasi_snapshot_preview1: wasi });
    const ex = inst.exports;
    mem = ex.memory;
    const leerStr = (h) => dec.decode(new Uint8Array(mem.buffer, ex.lg_wasm_str_ptr(h), Number(ex.lg_wasm_str_len(h))));
    const llamar = (nombre, ...args) => {
      if (typeof ex[nombre] !== 'function') throw new Error("el .wasm no exporta '" + nombre + "'");
      return ex[nombre](...args);
    };
    return {
      exports: ex,
      // Ejecuta _start (wasi-libc): corre el main y sale con proc_exit, que se captura aquí.
      // La memoria y los globales quedan vivos, así que los exports se pueden llamar después.
      iniciar() {
        let codigo = 0;
        try { ex._start(); } catch (e) { if (!(e instanceof Salida)) throw e; codigo = e.codigo; }
        for (const fd of [1, 2]) if (pend[fd]) { emitir(fd, '\n'); }
        return codigo;
      },
      llamar: llamar,
      // Handle de str -> texto. El handle vale hasta la siguiente exportación que devuelva str.
      leerStr,
      // Llama a un export con retorno str, copia el texto y libera el handle.
      texto: (nombre, ...args) => {
        const t = leerStr(llamar(nombre, ...args));
        ex.lg_wasm_str_liberar();
        return t;
      },
    };
  }
  const api = { cargar };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else ambito.LgPuente = api;
})(globalThis);
