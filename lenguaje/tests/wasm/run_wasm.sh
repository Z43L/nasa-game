#!/usr/bin/env bash
# Pruebas de lgc --target wasm32 (WASI preview 1, ejecutado con node:wasi).
# Uso: tests/wasm/run_wasm.sh <ruta_lgc>
# Por cada tests/wasm/NOMBRE.lg (solo nivel superior):
#   NOMBRE.expected -> compila con --target wasm32, ejecuta con node y compara stdout exactamente.
#                      Además compila el mismo .lg para el host y exige la misma salida (comprobación
#                      cruzada nativo/wasm).
#   NOMBRE.rterror  -> wasm debe terminar con código != 0 y stderr contener el texto (igual que nativo).
#   NOMBRE.error    -> la compilación para wasm32 debe fallar y stderr contener el texto.
# Sale con código 1 si algún caso falla; 77 (skip) si no hay node.
set -u

if [ $# -lt 1 ]; then
  echo "uso: $0 <ruta_lgc>" >&2
  exit 2
fi
LGC="$1"
if [ ! -x "$LGC" ]; then
  echo "no existe el ejecutable lgc: $LGC" >&2
  exit 2
fi
NODE="$(command -v node || true)"
if [ -z "$NODE" ]; then
  echo "no hay node: se omiten las pruebas wasm" >&2
  exit 77
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "PASS  $1"; }
bad() { fail=$((fail + 1)); echo "FAIL  $1: $2"; }

for src in "$HERE"/*.lg; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .lg)"
  wasm="$TMP/$name.wasm"
  nat="$TMP/$name.nat"
  cerr="$TMP/$name.cerr"

  if [ -f "$HERE/$name.error" ]; then
    if "$LGC" --target wasm32 "$src" -o "$wasm" 2> "$cerr"; then
      bad "$name" "la compilación para wasm32 debía fallar"
    elif grep -qF "$(cat "$HERE/$name.error")" "$cerr"; then
      ok "$name"
    else
      bad "$name" "stderr no contiene el texto esperado: $(head -c 300 "$cerr")"
    fi
    continue
  fi

  if ! "$LGC" --target wasm32 "$src" -o "$wasm" 2> "$cerr"; then
    bad "$name" "fallo al compilar para wasm32: $(head -c 300 "$cerr")"
    continue
  fi

  if [ -f "$HERE/$name.rterror" ]; then
    "$NODE" --no-warnings "$HERE/node_wasi_run.js" "$wasm" > "$TMP/$name.out" 2> "$TMP/$name.err"
    rc=$?
    if [ "$rc" -eq 0 ]; then
      bad "$name" "el programa wasm debía fallar y terminó con 0"
    elif grep -qF "$(cat "$HERE/$name.rterror")" "$TMP/$name.err"; then
      ok "$name"
    else
      bad "$name" "stderr wasm no contiene el texto esperado: $(head -c 300 "$TMP/$name.err")"
    fi
    # Mismo comportamiento en nativo.
    if "$LGC" "$src" -o "$nat" 2> "$cerr" && ! "$nat" > /dev/null 2> "$TMP/$name.nerr"; then
      grep -qF "$(cat "$HERE/$name.rterror")" "$TMP/$name.nerr" || bad "$name (nativo)" "stderr nativo distinto"
    else
      bad "$name (nativo)" "el nativo no falla como el wasm"
    fi
    continue
  fi

  if [ -f "$HERE/$name.expected" ]; then
    "$NODE" --no-warnings "$HERE/node_wasi_run.js" "$wasm" > "$TMP/$name.out" 2> "$TMP/$name.err"
    rc=$?
    if [ "$rc" -ne 0 ]; then
      bad "$name" "el programa wasm terminó con $rc: $(head -c 300 "$TMP/$name.err")"
      continue
    fi
    if ! cmp -s "$HERE/$name.expected" "$TMP/$name.out"; then
      bad "$name" "salida wasm distinta de $name.expected"
      continue
    fi
    # Comprobación cruzada: el mismo programa para el host.
    if ! "$LGC" "$src" -o "$nat" 2> "$cerr"; then
      bad "$name (nativo)" "fallo al compilar para el host: $(head -c 300 "$cerr")"
      continue
    fi
    if ! "$nat" > "$TMP/$name.nout" 2>/dev/null || ! cmp -s "$TMP/$name.out" "$TMP/$name.nout"; then
      bad "$name (cruzada)" "la salida nativa no coincide con la de wasm"
      continue
    fi
    ok "$name"
  fi
done

echo "wasm: $pass pasaron, $fail fallaron"
[ "$fail" -eq 0 ]
