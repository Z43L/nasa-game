#!/usr/bin/env bash
# Pruebas end-to-end del compilador lgc.
# Uso: tests/e2e/run_e2e.sh <ruta_lgc>
# Por cada tests/e2e/NOMBRE.lg (solo nivel superior; los módulos auxiliares van en subcarpetas):
#   NOMBRE.expected -> compila, ejecuta y compara stdout exactamente.
#   NOMBRE.error    -> la compilación debe fallar y stderr del compilador debe contener el texto.
#   NOMBRE.rterror  -> compila, el programa debe terminar con código != 0 y stderr contener el texto.
# Sale con código 1 si algún caso falla.
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

HERE="$(cd "$(dirname "$0")" && pwd)"
# Carpeta de módulos para probar la búsqueda por LG_PATH (import_lg_path.lg).
# Bibliotecas del repositorio (lib/*): cada subcarpeta es una raíz de búsqueda de imports.
ROOT="$(cd "$HERE/../.." && pwd)"
export LG_PATH="$HERE/lg_path:$ROOT/lib/nucleo:$ROOT/lib/datos:$ROOT/lib/red:$ROOT/lib/web:$ROOT/lib/ui${LG_PATH:+:$LG_PATH}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
failed=()

ok()   { pass=$((pass + 1)); echo "PASS  $1"; }
bad()  { fail=$((fail + 1)); failed+=("$1"); echo "FAIL  $1: $2"; }

for src in "$HERE"/*.lg; do
  [ -e "$src" ] || continue
  name="$(basename "$src" .lg)"
  bin="$TMP/$name"
  cerr="$TMP/$name.cerr"

  if [ -f "$HERE/$name.error" ]; then
    # Caso de error de compilación.
    if "$LGC" "$src" -o "$bin" </dev/null >/dev/null 2>"$cerr"; then
      bad "$name" "se esperaba error de compilación y compiló"
      continue
    fi
    expect="$(cat "$HERE/$name.error")"
    if grep -qF -- "$expect" "$cerr"; then
      ok "$name"
    else
      bad "$name" "stderr del compilador no contiene '$expect'"
    fi
    continue
  fi

  if ! "$LGC" "$src" -o "$bin" </dev/null >/dev/null 2>"$cerr"; then
    bad "$name" "fallo al compilar: $(head -n 3 "$cerr" | tr '\n' ' ')"
    continue
  fi

  if [ -f "$HERE/$name.rterror" ]; then
    # Caso de error de ejecución: debe fallar con el mensaje esperado.
    out="$TMP/$name.out"; err="$TMP/$name.err"
    if "$bin" </dev/null >"$out" 2>"$err"; then
      bad "$name" "se esperaba error de ejecución y terminó con código 0"
      continue
    fi
    expect="$(cat "$HERE/$name.rterror")"
    if grep -qF -- "$expect" "$err"; then
      ok "$name"
    else
      bad "$name" "stderr del programa no contiene '$expect'"
    fi
    continue
  fi

  if [ -f "$HERE/$name.expected" ]; then
    out="$TMP/$name.out"
    if ! "$bin" </dev/null >"$out" 2>"$TMP/$name.err"; then
      bad "$name" "el programa terminó con error: $(head -n 2 "$TMP/$name.err" | tr '\n' ' ')"
      continue
    fi
    if diff -u "$HERE/$name.expected" "$out" >"$TMP/$name.diff"; then
      ok "$name"
    else
      bad "$name" "salida distinta: $(head -n 8 "$TMP/$name.diff" | tr '\n' ' ')"
    fi
    continue
  fi

  bad "$name" "sin archivo .expected, .error ni .rterror"
done

echo
echo "Resumen: $pass PASS, $fail FAIL"
if [ "$fail" -gt 0 ]; then
  printf '  - %s\n' "${failed[@]}"
  exit 1
fi
exit 0
