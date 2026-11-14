#!/usr/bin/env bash
# Pruebas de lib/app. Uso: tests/app/run_app.sh <ruta_lgc>
# Por cada tests/app/NOMBRE.lg con NOMBRE.expected: compila con -I lib/app -I lib/ui -I stdlib -I lib/nucleo,
# ejecuta y compara stdout exactamente. Sin LG_PATH: "import app" debe resolver a lib/app/app.lg
# (lib/web/app.lg colisiona si lib/web está en LG_PATH). Sale con 1 si algún caso falla.
set -u
if [ $# -lt 1 ]; then
  echo "uso: $0 <ruta_lgc>" >&2
  exit 2
fi
LGC="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
unset LG_PATH
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
for src in "$HERE"/*.lg; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .lg)"
  [ -f "$HERE/$name.expected" ] || continue
  if ! "$LGC" "$src" -I "$ROOT/lib/app" -I "$ROOT/lib/ui" -I "$ROOT/stdlib" -I "$ROOT/lib/nucleo" -o "$TMP/$name" >/dev/null 2>"$TMP/$name.cerr"; then
    fail=$((fail + 1)); echo "FAIL  $name: no compila: $(head -n 3 "$TMP/$name.cerr" | tr '\n' ' ')"; continue
  fi
  if ! "$TMP/$name" </dev/null >"$TMP/$name.out" 2>"$TMP/$name.err"; then
    fail=$((fail + 1)); echo "FAIL  $name: el programa terminó con error"; continue
  fi
  if diff -u "$HERE/$name.expected" "$TMP/$name.out" >"$TMP/$name.diff"; then
    pass=$((pass + 1)); echo "PASS  $name"
  else
    fail=$((fail + 1)); echo "FAIL  $name: salida distinta: $(head -n 8 "$TMP/$name.diff" | tr '\n' ' ')"
  fi
done
echo
echo "Resumen: $pass PASS, $fail FAIL"
[ "$fail" -eq 0 ]
