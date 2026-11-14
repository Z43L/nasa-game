// Pruebas de la UI wasm: exports con nombre, cadenas devueltas a JS y el puente lib/ui/puente.js.
// Uso: node tests/wasm_ui/run_ui.js <ruta_lgc>
// Sale con 1 si algún caso falla; 77 si no hay node (lo gestiona ctest).
// Caso 4 (ruta nativa intacta): compara el IR nativo (--emit-llvm) de tests/wasm/*.lg y tests/e2e/*.lg
// entre <ruta_lgc> y ../../build/bin/lgc (si existe). Ambos binarios deben dar el mismo IR byte a byte.
'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');

const RAIZ = path.resolve(__dirname, '..', '..');
const lgc = process.argv[2];
if (!lgc) {
  console.error('uso: node tests/wasm_ui/run_ui.js <ruta_lgc>');
  process.exit(2);
}
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'lg_wasm_ui-'));
const puente = require(path.join(RAIZ, 'lib', 'ui', 'puente.js'));

let pasan = 0, fallan = 0;
const ok = (nombre) => { pasan++; console.log('PASS  ' + nombre); };
const mal = (nombre, motivo) => { fallan++; console.log('FAIL  ' + nombre + ': ' + motivo); };
function caso(nombre, fn) {
  try {
    const r = fn();
    if (r instanceof Promise) return r.then(() => ok(nombre), (e) => mal(nombre, e.message));
    ok(nombre);
  } catch (e) {
    mal(nombre, e.message);
  }
}
function compilar(src, salida, exportaciones) {
  const args = ['--target', 'wasm32'];
  for (const e of exportaciones) args.push('--export', e);
  args.push(src, '-o', salida);
  return spawnSync(lgc, args, { encoding: 'utf8' });
}
const afirma = (cond, msg) => { if (!cond) throw new Error(msg); };

async function main() {
  const dir = path.join(RAIZ, 'tests', 'wasm_ui');

  // 1. Contador: tres pulsaciones desde JS y el texto de la UI.
  const contadorWasm = path.join(tmp, 'contador.wasm');
  await caso('contador: exports con nombre del .lg', async () => {
    const r = compilar(path.join(dir, 'contador.lg'), contadorWasm, ['contador_pulsar', 'contador_texto']);
    afirma(r.status === 0, 'lgc falló: ' + r.stderr);
    const nombres = WebAssembly.Module.exports(new WebAssembly.Module(fs.readFileSync(contadorWasm)))
      .map((e) => e.name);
    afirma(nombres.includes('contador_pulsar') && nombres.includes('contador_texto'), 'faltan exports: ' + nombres);
    afirma(!nombres.some((n) => n.startsWith('u_')), 'aparece un nombre mangled: ' + nombres);
  });
  await caso('contador: 3 pulsaciones -> "Pulsaciones: 3"', async () => {
    const p = await puente.cargar(fs.readFileSync(contadorWasm));
    afirma(p.iniciar() === 0, 'el main no terminó con 0');
    let n = 0n;
    for (let i = 0; i < 3; i++) n = p.llamar('contador_pulsar', n);
    afirma(n === 3n, 'contador_pulsar devolvió ' + n);
    const t = p.texto('contador_texto', n);
    afirma(t === 'Pulsaciones: 3', 'texto = "' + t + '"');
  });

  // 2. Cadena de 100 000 caracteres, sin corrupción, y lectura repetida tras liberar.
  const largoWasm = path.join(tmp, 'largo.wasm');
  let p2 = null;
  await caso('largo: compilar con exports', () => {
    const r = compilar(path.join(dir, 'texto_largo.lg'), largoWasm,
      ['texto_largo', 'texto_utf8', 'es_par', 'media']);
    afirma(r.status === 0, 'lgc falló: ' + r.stderr);
  });
  await caso('largo: str de 100000 caracteres completo y correcto', async () => {
    p2 = await puente.cargar(fs.readFileSync(largoWasm));
    p2.iniciar();
    const s = p2.texto('texto_largo', 100000n);
    afirma(s.length === 100000, 'longitud ' + s.length);
    const base = 'abcdefghij';
    for (let i = 0; i < s.length; i++) {
      if (s[i] !== base[i % 10]) throw new Error('carácter corrupto en la posición ' + i);
    }
    const corto = p2.texto('texto_largo', 20n);
    afirma(corto === 'abcdefghijabcdefghij', 'segunda cadena: "' + corto + '"');
  });
  await caso('largo: UTF-8 (ñ, ú, ✓) y tipos bool/float', () => {
    afirma(p2.texto('texto_utf8') === 'ñandú ✓', 'utf8 mal decodificado');
    afirma(p2.llamar('es_par', 4n) === 1 && p2.llamar('es_par', 7n) === 0, 'bool mal');
    afirma(p2.llamar('media', 1.0, 2.0) === 1.5, 'float mal');
  });

  // 3. Errores de compilación claros (no fallos de enlace).
  const src = path.join(dir, 'contador.lg');
  const errores = [
    ['export inexistente', ['no_existe'], 'no existe la función de nivel superior \'no_existe\''],
    ['nombre con caracteres raros', ['contador-pulsar'], 'nombre de export no válido'],
    ['nombre reservado lg_', ['lg_str'], 'prefijo \'lg_\' está reservado'],
  ];
  for (const [nombre, exps, texto] of errores) {
    await caso('error: ' + nombre, () => {
      const r = compilar(src, path.join(tmp, 'x.wasm'), exps);
      afirma(r.status !== 0, 'debía fallar');
      afirma(r.stderr.includes(texto), 'stderr: ' + r.stderr.slice(0, 300));
      afirma(!/undefined symbol|wasm-ld|error al enlazar/.test(r.stderr), 'error de enlace: ' + r.stderr.slice(0, 200));
    });
  }
  await caso('error: --export sin --target wasm32', () => {
    const r = spawnSync(lgc, ['--export', 'contador_pulsar', src, '-o', path.join(tmp, 'n')], { encoding: 'utf8' });
    afirma(r.status !== 0 && r.stderr.includes('solo se admite con --target wasm32'), 'stderr: ' + r.stderr);
  });

  // 4. Ruta nativa intacta: IR idéntico al del lgc de referencia (build/bin/lgc).
  const ref = path.join(RAIZ, 'build', 'bin', 'lgc');
  if (!fs.existsSync(ref)) {
    console.log('SKIP  ir nativo: no existe ' + ref);
  } else {
    await caso('ir nativo idéntico a build/bin/lgc', () => {
      const progs = [];
      for (const d of ['tests/wasm', 'tests/e2e']) {
        for (const f of fs.readdirSync(path.join(RAIZ, d))) if (f.endsWith('.lg')) progs.push(path.join(RAIZ, d, f));
      }
      let comparados = 0;
      for (const src2 of progs) {
        const a = spawnSync(lgc, ['--emit-llvm', src2, '-o', path.join(tmp, 'nuevo')], { encoding: 'utf8' });
        const b = spawnSync(ref, ['--emit-llvm', src2, '-o', path.join(tmp, 'ref')], { encoding: 'utf8' });
        afirma(a.status === b.status, 'código distinto en ' + src2);
        if (a.status !== 0) {
          afirma(a.stderr === b.stderr, 'stderr distinto en ' + src2);
          continue;
        }
        const irA = fs.readFileSync(path.join(tmp, 'nuevo.ll'));
        const irB = fs.readFileSync(path.join(tmp, 'ref.ll'));
        afirma(irA.equals(irB), 'IR distinto en ' + src2);
        comparados++;
      }
      afirma(comparados > 0, 'ningún programa nativo compilado');
      console.log('      (' + comparados + ' programas comparados)');
    });
  }

  fs.rmSync(tmp, { recursive: true, force: true });
  console.log('wasm_ui: ' + pasan + ' pasaron, ' + fallan + ' fallaron');
  process.exitCode = fallan === 0 ? 0 : 1;
}

main().catch((e) => { console.error(e); process.exitCode = 1; });
