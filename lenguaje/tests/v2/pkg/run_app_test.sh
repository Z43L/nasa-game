#!/usr/bin/env bash
# Pruebas de lgpm app (F5): nueva, construir (wasm + dist/), levantar (servidor estático) y errores.
# Uso: tests/v2/pkg/run_app_test.sh <ruta_lgc> <ruta_lgpm>
# Trabaja en una carpeta temporal con HOME temporal. Sin node, los casos de wasm salen como SKIP.
# Sale con código 1 si algún caso falla.
set -u

if [ $# -lt 2 ]; then
  echo "uso: $0 <ruta_lgc> <ruta_lgpm>" >&2
  exit 2
fi
LGC="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
LGPM="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
TMP="$(mktemp -d)"
SRV_PID=""
cleanup() {
  if [ -n "$SRV_PID" ]; then kill -INT "$SRV_PID" 2>/dev/null; wait "$SRV_PID" 2>/dev/null; fi
  rm -rf "$TMP"
}
trap cleanup EXIT

export LGPM_LGC="$LGC"
export LGPM_RAIZ="$ROOT"
export HOME="$TMP/home"
mkdir -p "$HOME"
unset LG_PATH

pass=0; fail=0; skip=0
ok()   { pass=$((pass + 1)); echo "PASS  $1"; }
bad()  { fail=$((fail + 1)); echo "FAIL  $1: $2"; }
skip() { skip=$((skip + 1)); echo "SKIP  $1: $2"; }

cd "$TMP" || exit 1

# 1. nueva: estructura, rechazo de duplicados y de nombres no válidos.
name="app_nueva"
if "$LGPM" app nueva demo >/dev/null 2>&1 \
   && [ -f demo/lg.toml ] && [ -f demo/src/main.lg ] && [ -f demo/web/index.html ] \
   && [ -f demo/web/icons/icon-192.png ] && [ -f demo/web/icons/icon-512.png ] \
   && cmp -s demo/web/icons/icon-192.png "$ROOT/lib/app/pwa/icons/icon-192.png"; then
  ok "$name"
else
  bad "$name" "faltan ficheros de la plantilla"
fi
name="app_nueva_duplicada"
if "$LGPM" app nueva demo >/dev/null 2>&1; then bad "$name" "se aceptó un proyecto ya existente"; else ok "$name"; fi
name="app_nueva_nombre_malo"
if "$LGPM" app nueva "mal nombre" >/dev/null 2>&1; then bad "$name" "se aceptó un nombre con espacio"; else ok "$name"; fi

# 2. construir: dist/ con la página, manifest (start_url y scope "./"), service worker, iconos y el .wasm.
name="app_construir"
cd "$TMP/demo" || exit 1
if "$LGPM" app construir >/dev/null 2>"$TMP/build.err" \
   && [ -f dist/index.html ] && [ -f dist/app.wasm ] && [ -f dist/manifest.webmanifest ] \
   && [ -f dist/sw.js ] && [ -f dist/lg_sw.js ] && [ -f dist/registro.js ] \
   && [ -f dist/puente.js ] && [ -f dist/app_puente.js ] \
   && [ -f dist/icons/icon-192.png ] && [ -f dist/icons/icon-512.png ] \
   && grep -q '"start_url": "./"' dist/manifest.webmanifest \
   && grep -q '"scope": "./"' dist/manifest.webmanifest \
   && grep -q '<title>demo</title>' dist/index.html \
   && [ "$(head -c 4 dist/app.wasm | od -An -c | tr -d ' ')" = '\0asm' ]; then
  ok "$name"
else
  bad "$name" "dist/ incompleto: $(head -n 3 "$TMP/build.err" | tr '\n' ' ')"
fi

# 3. Exports y comportamiento del wasm (con node, como tests/wasm_ui/run_ui.js).
if ! command -v node >/dev/null 2>&1; then
  skip "app_wasm_exports" "no hay node"
else
  cat > "$TMP/check.js" <<JS
const fs = require('node:fs');
const puente = require('$ROOT/lib/ui/puente.js');
const bytes = fs.readFileSync('$TMP/demo/dist/app.wasm');
const exps = WebAssembly.Module.exports(new WebAssembly.Module(bytes)).map((e) => e.name);
const need = ['vista', 'ruta_inicio', 'ruta_byte', 'ruta_cambio', 'evento'];
const faltan = need.filter((n) => !exps.includes(n));
if (faltan.length) { console.log('faltan exports: ' + faltan); process.exit(1); }
(async () => {
  const p = await puente.cargar(bytes);
  if (p.iniciar() !== 0) throw new Error('el main no terminó con 0');
  const ruta = (path) => { p.llamar('ruta_inicio'); for (const b of new TextEncoder().encode(path)) p.llamar('ruta_byte', BigInt(b)); return p.llamar('ruta_cambio'); };
  if (ruta('/') !== 1n) throw new Error('/ no es pantalla 1');
  if (ruta('/detalle/7') !== 2n) throw new Error('/detalle/7 no es pantalla 2');
  if (!p.texto('vista').includes('Parámetro id = 7')) throw new Error('el parámetro id no llega a la vista');
  if (ruta('/no-existe') !== 404n) throw new Error('404 mal');
  ruta('/');
  p.llamar('evento', 1n); if (p.llamar('evento', 1n) !== 2n) throw new Error('el contador no suma');
  if (!p.texto('vista').includes('Cuenta: 2')) throw new Error('la vista no muestra la cuenta');
  if (p.llamar('evento', 2n) !== 0n) throw new Error('reinicio mal');
  console.log('ok');
})().catch((e) => { console.log(e.message); process.exit(1); });
JS
  if out="$(node --no-warnings "$TMP/check.js" 2>&1)" && [ "$out" = "ok" ]; then
    ok "app_wasm_exports"
  else
    bad "app_wasm_exports" "$out"
  fi
fi

# 4. levantar: 200, MIME, fallback SPA, 403 con '..', 405 con POST, puerto ocupado y parada limpia.
name="app_levantar"
PUERTO=""
for p in 18091 18092 18093 18094 18095 18096 18097 18098 18099; do
  "$LGPM" app levantar "$p" >"$TMP/srv.log" 2>&1 &
  SRV_PID=$!
  sleep 0.5
  if kill -0 "$SRV_PID" 2>/dev/null; then PUERTO=$p; break; fi
  SRV_PID=""
done
if [ -z "$PUERTO" ]; then
  bad "$name" "no arranca el servidor en 18091-18099"
else
  P="http://localhost:$PUERTO"
  c() { curl -s -o /dev/null -w "%{http_code} %{content_type}" "$1"; }
  r_index="$(c "$P/")"
  r_wasm="$(c "$P/app.wasm")"
  r_man="$(c "$P/manifest.webmanifest")"
  r_deep="$(curl -s "$P/detalle/7" | cmp -s - "$TMP/demo/dist/index.html" && echo same)"
  r_404="$(c "$P/no-existe.png")"
  r_trav="$(curl --path-as-is -s -o /dev/null -w "%{http_code}" "$P/../../etc/passwd")"
  r_post="$(curl -s -o /dev/null -w "%{http_code}" -X POST "$P/")"
  if [ "$r_index" = "200 text/html; charset=utf-8" ] \
     && [ "$r_wasm" = "200 application/wasm" ] \
     && [ "$r_man" = "200 application/manifest+json" ] \
     && [ "$r_deep" = "same" ] && [ "$r_404" = "404 text/plain; charset=utf-8" ] \
     && [ "$r_trav" = "403" ] && [ "$r_post" = "405" ]; then
    ok "$name"
  else
    bad "$name" "index=$r_index wasm=$r_wasm man=$r_man deep=$r_deep 404=$r_404 trav=$r_trav post=$r_post"
  fi

  name="app_puerto_ocupado"
  if "$LGPM" app levantar "$PUERTO" >"$TMP/busy.err" 2>&1; then
    bad "$name" "arrancó en un puerto ocupado"
  elif grep -q "ya está en uso" "$TMP/busy.err"; then
    ok "$name"
  else
    bad "$name" "mensaje inesperado: $(head -n 1 "$TMP/busy.err")"
  fi

  name="app_parada_limpia"
  kill -INT "$SRV_PID"
  wait "$SRV_PID" 2>/dev/null
  SRV_PID=""
  if grep -q "servidor detenido" "$TMP/srv.log"; then ok "$name"; else bad "$name" "no escribió 'servidor detenido'"; fi
fi

# 5. Errores de construir: sin compilador, sin lg.toml y con un error de compilación.
cd "$TMP/demo" || exit 1
name="app_error_sin_compilador"
if LGPM_LGC="$TMP/no_existe/lgc" "$LGPM" app construir >/dev/null 2>"$TMP/e1.err"; then
  bad "$name" "construyó sin compilador"
elif grep -q "no se encuentra el compilador lgc" "$TMP/e1.err"; then ok "$name"; else bad "$name" "mensaje: $(head -n 1 "$TMP/e1.err")"; fi

name="app_error_sin_toml"
mkdir -p "$TMP/vacio" && cd "$TMP/vacio" || exit 1
if "$LGPM" app construir >/dev/null 2>"$TMP/e2.err"; then bad "$name" "construyó sin lg.toml"; else ok "$name"; fi
cd "$TMP/demo" || exit 1

name="app_error_compilacion"
cd "$TMP" || exit 1
"$LGPM" app nueva roto >/dev/null 2>&1
cd "$TMP/roto" || exit 1
printf 'fn vista() -> str:\n    return 5\n\nprint(vista())\n' > src/main.lg
if "$LGPM" app construir >/dev/null 2>"$TMP/e3.err"; then
  bad "$name" "construyó un programa con errores"
elif grep -q "falló la compilación a wasm" "$TMP/e3.err" && [ ! -d dist ]; then
  ok "$name"
else
  bad "$name" "mensaje: $(tail -n 1 "$TMP/e3.err")"
fi

echo
echo "Resumen: $pass PASS, $fail FAIL, $skip SKIP"
[ "$fail" -eq 0 ]
