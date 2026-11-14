// Pruebas de las apps de tests/ui en wasm, usando lib/ui/puente.js (mismo puente que el navegador).
// Uso: node tests/ui/run_ui_app.js [ruta_lgc_wasm]   (por defecto build-wasm/bin/lgc)
// Compila tests/ui/contador_app.lg y tests/ui/grande_app.lg con --target wasm32 y comprueba la vista
// y el despacho de eventos. Sale con 1 si falla algún caso.
'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const RAIZ = path.resolve(__dirname, '..', '..');
const lgc = process.argv[2] || path.join(RAIZ, 'build-wasm', 'bin', 'lgc');
const puente = require(path.join(RAIZ, 'lib', 'ui', 'puente.js'));
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lg_ui_app-'));

let pasan = 0, fallan = 0;
const afirma = (cond, msg) => { if (!cond) throw new Error(msg); };
async function caso(nombre, fn) {
  try { await fn(); pasan++; console.log('PASS  ' + nombre); }
  catch (e) { fallan++; console.log('FAIL  ' + nombre + ': ' + e.message); }
}
function compilar(src, salida, exps) {
  const args = ['--target', 'wasm32'];
  for (const e of exps) args.push('--export', e);
  args.push(path.join(RAIZ, src), '-I', path.join(RAIZ, 'lib', 'ui'), '-I', path.join(RAIZ, 'stdlib'), '-o', salida);
  const r = spawnSync(lgc, args, { encoding: 'utf8' });
  afirma(r.status === 0, 'lgc falló: ' + r.stderr);
}

async function main() {
  const contador = path.join(tmp, 'contador_app.wasm');
  const grande = path.join(tmp, 'grande_app.wasm');
  compilar('tests/ui/contador_app.lg', contador, ['vista', 'despachar']);
  compilar('tests/ui/grande_app.lg', grande, ['render_lista']);
  const p = await puente.cargar(fs.readFileSync(contador));
  afirma(p.iniciar() === 0, 'el main no terminó con 0');

  await caso('contador: vista inicial con 0 y escape del título', async () => {
    const v = p.texto('vista', 0n);
    afirma(v.includes('<p id="texto">Pulsaciones: 0</p>'), v);
    afirma(v.includes('<h1>Contador &amp; amigos</h1>'), 'título sin escapar: ' + v);
  });
  await caso('contador: 3 x despachar(+1) -> la vista contiene "3"', async () => {
    let n = 0n;
    for (let i = 0; i < 3; i++) n = p.llamar('despachar', 1n, n);
    afirma(n === 3n, 'estado ' + n);
    const v = p.texto('vista', n);
    afirma(v.includes('Pulsaciones: 3'), v);
  });
  await caso('contador: despachar(reiniciar) devuelve 0', async () => {
    const n = p.llamar('despachar', 2n, 3n);
    afirma(n === 0n, 'estado ' + n);
    afirma(p.texto('vista', n).includes('Pulsaciones: 0'), 'vista no reiniciada');
  });
  await caso('contador: botones con data-accion para JS', async () => {
    const v = p.texto('vista', 0n);
    afirma(v.includes('<button type="button" data-accion="1">+1</button>'), 'falta botón +1');
    afirma(v.includes('<button type="button" data-accion="2">Reiniciar</button>'), 'falta botón reiniciar');
  });
  await caso('grande: 10 000 elementos en wasm, sin escape roto', async () => {
    const g = await puente.cargar(fs.readFileSync(grande));
    g.iniciar();
    const t0 = Date.now();
    const s = g.texto('render_lista', 10000n);
    const ms = Date.now() - t0;
    let esperado = '<ul>';
    for (let i = 0; i < 10000; i++)
      esperado += '<li class="item">elemento ' + i + ' &amp; más &lt;x&gt;</li>';
    esperado += '</ul>';
    afirma(s === esperado, 'render distinto del esperado (longitud ' + s.length + ' vs ' + esperado.length + ')');
    afirma(!s.includes('<x>'), 'etiqueta sin escapar');
    console.log('      (10 000 nodos en ' + ms + ' ms)');
  });

  fs.rmSync(tmp, { recursive: true, force: true });
  console.log('ui_app: ' + pasan + ' pasaron, ' + fallan + ' fallaron');
  process.exitCode = fallan === 0 ? 0 : 1;
}
main().catch((e) => { console.error(e); process.exitCode = 1; });
