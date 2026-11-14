#!/usr/bin/env bash
# Batería QA de lib/ui (html.lg / render.lg): escape, inyección por atributos, nodos, fuzzing y
# comprobación HTML con html.parser, y errores de uso de la API en el compilador.
# Uso: tests/qa/ui/run_ui_qa.sh <ruta_lgc>
#
# Casos en tests/qa/ui/casos/NOMBRE.lg (solo nivel superior, con -I lib/ui -I stdlib):
#   NOMBRE.expected -> compila (código 0), ejecuta (código 0) y stdout exacto.
#   NOMBRE.error    -> lgc sale con código 1 (sin fallo interno: sin señal ni código >= 128) y
#                      stderr no vacío. Si el fichero tiene texto, stderr debe contenerlo.
#                      Solo es concluyente si control_valido.lg compila (prerrequisito).
# Los casos BUG_*.lg son bugs conocidos: se marcan como BUG_ si fallan.
# Fuzzing y árboles: fuzz_escape.py genera programas (3000 cadenas, 1 MB, 509 árboles incluido
# uno de profundidad 1000) y comprueba la salida contra html.escape y html.parser.
# Sale con 0 si todo pasa; 1 si algo falla; 2 por uso incorrecto.
# Nota: el escape de la comilla simple se compara por equivalencia (&#39;, &apos; y &#x27; valen lo
# mismo); las referencias numéricas de UTF-8 se cuentan como DIF_UTF8 (anotado, no fallo).
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
RAIZ="$(cd "$HERE/../../.." && pwd)"
CASOS="$HERE/casos"
INCLUDES=(-I "$RAIZ/lib/ui" -I "$RAIZ/stdlib")
LIMITE=120  # segundos máximos por compilación o ejecución
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
failed=()
RC=0

ok()  { pass=$((pass + 1)); echo "PASS  $1"; }
bad() {
  fail=$((fail + 1)); failed+=("$1")
  if [[ "$1" == BUG_* ]]; then echo "BUG_  $1: $2"; else echo "FAIL  $1: $2"; fi
}

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

# Un código >= 128 indica señal (fallo interno del compilador o del programa).
es_fallo_interno() { [ "$1" -ge 128 ]; }

# Compila $1 a $2 (binario); deja stderr en $3. RC = código de lgc.
compilar() {
  con_limite "$LGC" "$1" -o "$2" "${INCLUDES[@]}" 2>"$3" >/dev/null
}

# ---------------------------------------------------------------- prerrequisito

CONTROL="$CASOS/control_valido.lg"
PREREQ_OK=1
compilar "$CONTROL" "$TMP/bin_control" "$TMP/control.cerr"
if [ "$RC" -ne 0 ]; then
  PREREQ_OK=0
  echo "AVISO prerrequisito: control_valido.lg no compila (código $RC): $(head -n 2 "$TMP/control.cerr" | tr '\n' ' ')"
fi

# ---------------------------------------------------------------- casos con metadatos

for src in "$CASOS"/*.lg; do
  name="$(basename "$src" .lg)"
  [ "$name" = "control_valido" ] && continue
  bin="$TMP/bin_$name"
  cerr="$TMP/$name.cerr"

  if [ -f "$CASOS/$name.error" ]; then
    compilar "$src" "$bin" "$cerr"
    if es_fallo_interno "$RC"; then
      bad "$name" "lgc terminó por señal o fallo interno (código $RC)"
      continue
    fi
    if [ "$RC" -ne 1 ]; then
      bad "$name" "lgc salió con código $RC (se esperaba 1)"
      continue
    fi
    if [ "$PREREQ_OK" -ne 1 ]; then
      bad "$name" "prerrequisito: control_valido.lg no compila, resultado no concluyente"
      continue
    fi
    if [ ! -s "$cerr" ]; then
      bad "$name" "lgc no dio mensaje de error"
      continue
    fi
    if grep -qiE 'segmentation|abort|assertion|panic|bus error|stack overflow' "$cerr"; then
      bad "$name" "mensaje de fallo interno: $(head -n 2 "$cerr" | tr '\n' ' ')"
      continue
    fi
    expect="$(cat "$CASOS/$name.error")"
    if [ -n "$expect" ] && ! grep -qF -- "$expect" "$cerr"; then
      bad "$name" "stderr de lgc no contiene '$expect'"
      continue
    fi
    ok "$name"
    continue
  fi

  if [ -f "$CASOS/$name.expected" ]; then
    compilar "$src" "$bin" "$cerr"
    if [ "$RC" -ne 0 ]; then
      bad "$name" "fallo al compilar (código $RC): $(head -n 3 "$cerr" | tr '\n' ' ')"
      continue
    fi
    out="$TMP/$name.out"
    err="$TMP/$name.err"
    con_limite "$bin" >"$out" 2>"$err"
    if [ "$RC" -ne 0 ]; then
      pista=""
      [ "$RC" -eq 137 ] && pista=" (posible cuelgue)"
      bad "$name" "el programa terminó con código $RC$pista: $(head -n 2 "$err" | tr '\n' ' ')"
      continue
    fi
    if diff -u "$CASOS/$name.expected" "$out" >"$TMP/$name.diff"; then
      ok "$name"
    else
      bad "$name" "salida distinta: $(head -n 8 "$TMP/$name.diff" | tr '\n' ' ')"
    fi
    continue
  fi

  echo "AVISO $name: sin .expected ni .error, se ignora"
done

# ---------------------------------------------------------------- fuzzing y árboles

GEN="$TMP/gen"
FUZZ_CADENAS=3000
if ! python3 -I "$HERE/fuzz_escape.py" generar "$GEN" 20260 "$FUZZ_CADENAS"; then
  bad "fuzz_escape" "no se pudo generar la batería"
else
  gen_ok=1
  for n in escape_fuzz escape_1mb arboles; do
    compilar "$GEN/$n.lg" "$TMP/bin_$n" "$TMP/$n.cerr"
    if [ "$RC" -ne 0 ]; then
      bad "$n" "fallo al compilar el generado (código $RC): $(head -n 3 "$TMP/$n.cerr" | tr '\n' ' ')"
      gen_ok=0
      continue
    fi
    con_limite "$TMP/bin_$n" >"$TMP/$n.out" 2>"$TMP/$n.err"
    if [ "$RC" -ne 0 ]; then
      bad "$n" "el programa terminó con código $RC: $(head -n 2 "$TMP/$n.err" | tr '\n' ' ')"
      gen_ok=0
    fi
  done

  if [ "$gen_ok" -eq 1 ]; then
    if python3 -I "$HERE/fuzz_escape.py" comprobar "$GEN" "$TMP/escape_fuzz.out" "$TMP/escape_1mb.out" "$TMP/arboles.out" >"$TMP/comprobar.txt" 2>&1; then
      ok "fuzz_escape ($FUZZ_CADENAS cadenas, 1 MB, árboles de html.parser)"
    else
      bad "fuzz_escape" "diferencias (ver detalle abajo)"
    fi
    cat "$TMP/comprobar.txt"
  else
    bad "fuzz_escape" "no se pudo ejecutar la batería generada"
  fi
fi

# ---------------------------------------------------------------- resumen

echo
echo "Resumen: $pass PASS, $fail FAIL (de $((pass + fail)) comprobaciones)"
if [ "$fail" -gt 0 ]; then
  printf 'Fallidos: %s\n' "${failed[*]}"
  exit 1
fi
exit 0
