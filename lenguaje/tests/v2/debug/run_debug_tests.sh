#!/usr/bin/env bash
# Prueba de depuración con lldb (B6): lgc -g produce líneas y variables locales que lldb entiende.
# Uso: tests/v2/debug/run_debug_tests.sh <ruta_lgc>
#
# Compila factorial_dbg.lg con -g y comprueba con lldb (modo por lotes):
#   - un breakpoint por línea del .lg se resuelve en esa línea y para allí;
#   - `frame variable` muestra los parámetros y las variables locales con sus valores;
#   - tras `continue`, el segundo paso por el bucle muestra los valores nuevos;
#   - sin -g, el mismo breakpoint no tiene ubicación (control negativo).
# Sale con 77 (skip) si no hay lldb en el sistema. Sale con 1 si alguna comprobación falla.
#
# Nota macOS: el DWARF queda en el objeto .o, que el enlazador referencia. Por eso se usa --keep-obj.
set -u

if [ $# -lt 1 ]; then
  echo "uso: $0 <ruta_lgc>" >&2
  exit 2
fi
LGC="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
if [ ! -x "$LGC" ]; then
  echo "no existe el ejecutable lgc: $LGC" >&2
  exit 2
fi

LLDB="$(command -v lldb || true)"
if [ -z "$LLDB" ] || ! "$LLDB" --version >/dev/null 2>&1; then
  echo "SKIP: lldb no está disponible"
  exit 77
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
SRC="$HERE/factorial_dbg.lg"
WORK="$(mktemp -d "${TMPDIR:-/tmp}/lg_debug.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
cp "$SRC" "$WORK/factorial_dbg.lg"
cd "$WORK" || exit 1

fails=0
check() {  # check <descripcion> <salida> <patron-egrep>
  if printf '%s\n' "$2" | grep -Eq -- "$3"; then
    echo "ok: $1"
  else
    echo "FALLO: $1 (patron: $3)"
    fails=$((fails + 1))
  fi
}

# 1. Compilación con -g y ejecución normal (la salida no cambia).
if ! "$LGC" factorial_dbg.lg -g --keep-obj -o fact_g >compile.log 2>&1; then
  echo "FALLO: lgc -g no compila factorial_dbg.lg"; cat compile.log; exit 1
fi
run_out="$(./fact_g)"
check "la salida del programa con -g es 120" "$run_out" '^120$'

# 2. Breakpoint en la línea 5 (acc = acc * i, dentro del bucle) y variables locales.
out="$("$LLDB" -b \
  -o "breakpoint set -f factorial_dbg.lg -l 5" \
  -o "run" \
  -o "frame variable" \
  -o "continue" \
  -o "frame variable" \
  ./fact_g 2>&1)"
echo "$out" | sed -n '1,40p'
check "el breakpoint de la línea 5 se resuelve" "$out" 'Breakpoint 1: where = .* at factorial_dbg.lg:5:'
check "el programa para en la línea 5" "$out" 'stop reason = breakpoint 1\.1'
check "la parada muestra la línea 5 del .lg" "$out" '^->? *5 .*acc = acc \* i'
check "primera parada: n = 5" "$out" '\((long|int)\) n = 5$'
check "primera parada: acc = 1" "$out" '\((long|int)\) acc = 1$'
check "primera parada: i = 2" "$out" '\((long|int)\) i = 2$'
check "segunda parada: acc = 2" "$out" '\((long|int)\) acc = 2$'
check "segunda parada: i = 3" "$out" '\((long|int)\) i = 3$'

# 3. Control negativo: sin -g no hay tabla de líneas para el archivo.
"$LGC" factorial_dbg.lg --keep-obj -o fact_nog >/dev/null 2>&1
out_nog="$("$LLDB" -b -o "breakpoint set -f factorial_dbg.lg -l 5" ./fact_nog 2>&1)"
check "sin -g el breakpoint por línea no resuelve" "$out_nog" 'no locations|0 locations'

if [ "$fails" -ne 0 ]; then
  echo "$fails comprobacion(es) fallida(s)"
  exit 1
fi
echo "debug: todas las comprobaciones pasan"
exit 0
