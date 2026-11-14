// Ejecuta un programa wasm generado por lgc con WASI (node:wasi).
// Uso: node node_wasi_run.js <programa.wasm>
// Devuelve como código de salida el que pone el programa (exit/proc_exit); un trap sale con 1.
const fs = require('node:fs');
const { WASI } = require('node:wasi');

const file = process.argv[2];
// Directorio actual como preopen: los programas pueden leer y escribir ficheros relativos.
const wasi = new WASI({
  version: 'preview1',
  args: [file],
  env: {},
  preopens: { '.': process.cwd() },
  returnOnExit: true,
});
(async () => {
  const mod = await WebAssembly.compile(fs.readFileSync(file));
  const inst = await WebAssembly.instantiate(mod, wasi.getImportObject());
  process.exitCode = wasi.start(inst);
})().catch((e) => {
  console.error(e && e.message ? e.message : String(e));
  process.exitCode = 1;
});
