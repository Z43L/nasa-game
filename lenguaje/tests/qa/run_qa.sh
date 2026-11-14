#!/usr/bin/env bash
# Batería QA de robustez: stdlib (cadena, tcp, datos, mat, red), compilador lgc y fuzzing.
# Uso: tests/qa/run_qa.sh <ruta_lgc>
#
# Casos en tests/qa/casos/NOMBRE.lg (solo nivel superior):
#   NOMBRE.expected -> compila (código 0), ejecuta (código 0) y stdout exacto.
#   NOMBRE.rterror  -> compila, el programa debe salir con código 1 y stderr contener el texto.
#   NOMBRE.error    -> lgc debe salir con código 1 y stderr contener el texto.
#   NOMBRE.cliente  -> script bash lanzado en paralelo mientras corre el programa (cliente TCP).
# Los casos BUG_*.lg son bugs conocidos: se ejecutan en una sección aparte y se listan al final.
# Fuzzing: fuzz_cadena.py (cadena contra referencia Python) y fuzz_compilador.py (mutaciones).
# Sale con 0 si todo pasa; 1 si algo falla (incluidos los BUG_); 2 por uso incorrecto.
set -u

if [ $# -lt 1 ]; then
  echo "uso: $0 <ruta_lgc>" >&2
  exit 2
fi
LGC_ARG="$1"
if [ ! -x "$LGC_ARG" ]; then
  echo "no existe el ejecutable lgc: $LGC_ARG" >&2
  exit 2
fi
LGC="$(cd "$(dirname "$LGC_ARG")" && pwd)/$(basename "$LGC_ARG")"
HERE="$(cd "$(dirname "$0")" && pwd)"
CASOS="$HERE/casos"
# Módulos de prueba para los imports circulares (y los de LG_PATH existentes).
export LG_PATH="$HERE/modulos${LG_PATH:+:$LG_PATH}"
LIMITE=60   # segundos máximos por ejecución de un programa
TMP="$(mktemp -d)"
CONSERVAR=0
trap '[ "$CONSERVAR" -eq 1 ] || rm -rf "$TMP"' EXIT

pass=0
fail=0
failed=()
RC=0

ok()  { pass=$((pass + 1)); echo "PASS  $1"; }
bad() { fail=$((fail + 1)); failed+=("$1"); echo "FAIL  $1: $2"; }

# Ejecuta "$@" con límite de tiempo. Deja el código de salida en RC (137 si se mató por tiempo).
con_limite() {
  "$@" &
  local pid=$!
  ( sleep "$LIMITE" && kill -9 "$pid" ) 2>/dev/null &
  local vigilante=$!
  wait "$pid"
  RC=$?
  pkill -P "$vigilante" 2>/dev/null
  kill "$vigilante" 2>/dev/null
  wait "$vigilante" 2>/dev/null
  return 0
}

# Compila y ejecuta un caso. $1 = fuente, $2 = carpeta con los ficheros de resultado esperado.
ejecutar_caso() {
  local src="$1" meta="$2"
  local name; name="$(basename "$src" .lg)"
  local bin="$TMP/bin_$name" cerr="$TMP/$name.cerr"
  local expect

  if [ -f "$meta/$name.error" ]; then
    con_limite "$LGC" "$src" -o "$bin" 2>"$cerr" >/dev/null
    if [ "$RC" -ne 1 ]; then
      bad "$name" "lgc salió con código $RC (se esperaba 1)"
      return
    fi
    expect="$(cat "$meta/$name.error")"
    if [ -s "$cerr" ] && grep -qF -- "$expect" "$cerr"; then
      ok "$name"
    else
      bad "$name" "stderr de lgc no contiene '$expect'"
    fi
    return
  fi

  con_limite "$LGC" "$src" -o "$bin" 2>"$cerr" >/dev/null
  if [ "$RC" -ne 0 ]; then
    bad "$name" "fallo al compilar (código $RC): $(head -n 3 "$cerr" | tr '\n' ' ')"
    return
  fi

  local work="$TMP/run_$name" out="$TMP/$name.out" err="$TMP/$name.err" cli=""
  mkdir -p "$work"
  if [ -f "$meta/$name.cliente" ]; then
    ( cd "$work" && exec bash "$meta/$name.cliente" ) >/dev/null 2>&1 &
    cli=$!
  fi
  cd "$work" || return
  con_limite "$bin" >"$out" 2>"$err"
  cd "$HERE" || return
  if [ -n "$cli" ]; then wait "$cli" 2>/dev/null; fi

  if [ -f "$meta/$name.expected" ]; then
    if [ "$RC" -ne 0 ]; then
      local pista=""
      [ "$RC" -eq 137 ] && pista=" (posible cuelgue)"
      bad "$name" "el programa terminó con código $RC$pista: $(head -n 2 "$err" | tr '\n' ' ')"
      return
    fi
    if diff -u "$meta/$name.expected" "$out" >"$TMP/$name.diff"; then
      ok "$name"
    else
      bad "$name" "salida distinta: $(head -n 8 "$TMP/$name.diff" | tr '\n' ' ')"
    fi
    return
  fi

  if [ -f "$meta/$name.rterror" ]; then
    if [ "$RC" -ne 1 ]; then
      bad "$name" "código $RC (se esperaba error de ejecución con código 1)"
      return
    fi
    expect="$(cat "$meta/$name.rterror")"
    if grep -qF -- "$expect" "$err"; then
      ok "$name"
    else
      bad "$name" "stderr del programa no contiene '$expect'"
    fi
    return
  fi

  bad "$name" "sin archivo .expected, .rterror ni .error"
}

echo "== Casos de stdlib y compilador =="
for src in "$CASOS"/*.lg; do
  [ -e "$src" ] || continue
  case "$(basename "$src")" in
    BUG_*) continue ;;
  esac
  ejecutar_caso "$src" "$CASOS"
done

echo
echo "== Fuzzing de cadena (referencia en Python) =="
GEN="$TMP/gen"
if ! command -v python3 >/dev/null 2>&1; then
  bad "fuzz_cadena" "no hay python3"
elif ! python3 "$HERE/fuzz_cadena.py" "$GEN" 20260 300; then
  bad "fuzz_cadena" "el generador falló"
else
  ejecutar_caso "$GEN/cadena_fuzz.lg" "$GEN"
fi

echo
echo "== Fuzzing del compilador (mutaciones de tests/e2e) =="
if ! command -v python3 >/dev/null 2>&1; then
  bad "fuzz_compilador" "no hay python3"
else
  FZ="$TMP/fuzz_comp"
  if python3 "$HERE/fuzz_compilador.py" "$LGC" "$FZ" 7 300 >"$TMP/fuzz_comp.txt" 2>&1; then
    ok "fuzz_compilador"
  else
    bad "fuzz_compilador" "$(tr '\n' ' ' <"$TMP/fuzz_comp.txt" | head -c 600)"
    CONSERVAR=1
    echo "      (entradas que fallan conservadas en $FZ/fallos)"
  fi
  cat "$TMP/fuzz_comp.txt"
fi

echo
echo "== Bugs conocidos (BUG_*), ejecutados aparte =="
bugs_hallados=0
for src in "$CASOS"/BUG_*.lg; do
  [ -e "$src" ] || continue
  bugs_hallados=$((bugs_hallados + 1))
  ejecutar_caso "$src" "$CASOS"
done
[ "$bugs_hallados" -eq 0 ] && echo "(ninguno)"

echo
n_bug=0
if [ "${#failed[@]}" -gt 0 ]; then
  for f in "${failed[@]}"; do
    case "$f" in BUG_*) n_bug=$((n_bug + 1)) ;; esac
  done
fi
echo "Resumen: $pass PASS, $fail FAIL ($n_bug de ellos BUG_ conocidos)"
if [ "$fail" -gt 0 ]; then
  printf '  - %s\n' "${failed[@]}"
  exit 1
fi
exit 0
