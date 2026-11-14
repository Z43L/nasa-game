#!/usr/bin/env bash
# Pruebas de lgfmt.
# Uso: tests/v2/fmt/run_fmt_tests.sh <ruta_lgfmt> [ruta_lgc]
#
# Casos golden (tests/v2/fmt/NOMBRE.lg + NOMBRE.expected):
#   - la salida de `lgfmt NOMBRE.lg` es idéntica a NOMBRE.expected;
#   - NOMBRE.expected ya está formateado (--check sale con 0) y formatearlo otra vez no cambia nada;
#   - si NOMBRE.lg no está formateado, --check sale con 1;
#   - `lgfmt -i` deja NOMBRE.lg igual que NOMBRE.expected.
# Casos de error (NOMBRE.lg + NOMBRE.error): sale con 1, no escribe en stdout, stderr contiene el
#   texto de NOMBRE.error y `-i` no modifica el archivo.
# Corpus (examples/, stdlib/, tests/e2e/): todos formatean, el formato es idempotente y el volcado
#   del AST (--dump-ast) es el mismo antes y después. Si se pasa lgc, el IR emitido (--emit-llvm)
#   del original y del formateado es idéntico (se omiten archivos con import).
# Sale con 1 si algún caso falla.
set -u

if [ $# -lt 1 ]; then
  echo "uso: $0 <ruta_lgfmt> [ruta_lgc]" >&2
  exit 2
fi
LGFMT="$1"
LGC="${2:-}"
if [ ! -x "$LGFMT" ]; then
  echo "no existe el ejecutable lgfmt: $LGFMT" >&2
  exit 2
fi

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

pass=0
fail=0
ok()  { pass=$((pass + 1)); echo "PASS  $1"; }
bad() { fail=$((fail + 1)); echo "FAIL  $1: $2"; }

# ---------------------------------------------------------------- casos golden
for exp in "$HERE"/*.expected; do
  [ -e "$exp" ] || continue
  name="$(basename "$exp" .expected)"
  src="$HERE/$name.lg"
  if [ ! -f "$src" ]; then
    bad "golden $name" "falta $name.lg"
    continue
  fi

  if "$LGFMT" "$src" > "$TMP/out" 2> "$TMP/err" && cmp -s "$TMP/out" "$exp"; then
    ok "golden $name"
  else
    bad "golden $name" "la salida no coincide con $name.expected $(head -c 300 "$TMP/err")"
  fi

  if "$LGFMT" --check "$exp" > /dev/null 2>&1; then
    ok "--check acepta $name.expected"
  else
    bad "--check acepta $name.expected" "el resultado esperado no está en formato canónico"
  fi

  "$LGFMT" "$exp" > "$TMP/again" 2> /dev/null
  if cmp -s "$TMP/again" "$exp"; then
    ok "idempotencia $name"
  else
    bad "idempotencia $name" "formatear la salida otra vez cambia el resultado"
  fi

  if ! cmp -s "$src" "$exp"; then
    if "$LGFMT" --check "$src" > /dev/null 2>&1; then
      bad "--check detecta $name.lg" "debería salir con 1 porque no está formateado"
    else
      ok "--check detecta $name.lg sin formatear"
    fi
  fi

  cp "$src" "$TMP/inplace.lg"
  "$LGFMT" -i "$TMP/inplace.lg" > /dev/null 2>&1
  if cmp -s "$TMP/inplace.lg" "$exp"; then
    ok "-i $name"
  else
    bad "-i $name" "la reescritura in situ no coincide con $name.expected"
  fi
done

# ---------------------------------------------------------------- casos de error
for errf in "$HERE"/*.error; do
  [ -e "$errf" ] || continue
  name="$(basename "$errf" .error)"
  src="$HERE/$name.lg"
  "$LGFMT" "$src" > "$TMP/out" 2> "$TMP/err"
  rc=$?
  if [ "$rc" -eq 1 ] && [ ! -s "$TMP/out" ] && grep -qF "$(cat "$errf")" "$TMP/err"; then
    ok "error de parseo $name"
  else
    bad "error de parseo $name" "rc=$rc, se esperaba 1 y el texto '$(cat "$errf")' en stderr"
  fi
  cp "$src" "$TMP/errcopy.lg"
  "$LGFMT" -i "$TMP/errcopy.lg" > /dev/null 2>&1
  if cmp -s "$TMP/errcopy.lg" "$src"; then
    ok "-i no toca $name con errores"
  else
    bad "-i no toca $name con errores" "el archivo con errores fue modificado"
  fi
done

# ---------------------------------------------------------------- corpus del repositorio
while IFS= read -r src; do
  rel="${src#"$ROOT"/}"
  caso="$(mktemp -d "$TMP/caso.XXXX")"
  base="$(basename "$src")"
  if ! "$LGFMT" "$src" > "$caso/fmt.lg" 2> "$caso/err"; then
    bad "corpus $rel" "no formatea: $(head -c 300 "$caso/err")"
    continue
  fi

  if "$LGFMT" --dump-ast "$src" > "$caso/ast1" 2> /dev/null &&
     "$LGFMT" --dump-ast "$caso/fmt.lg" > "$caso/ast2" 2> /dev/null &&
     cmp -s "$caso/ast1" "$caso/ast2"; then
    ok "AST sin cambios $rel"
  else
    bad "AST sin cambios $rel" "el volcado del AST cambia tras formatear"
  fi

  "$LGFMT" "$caso/fmt.lg" > "$caso/fmt2.lg" 2> /dev/null
  if cmp -s "$caso/fmt.lg" "$caso/fmt2.lg"; then
    ok "idempotencia $rel"
  else
    bad "idempotencia $rel" "formatear dos veces cambia el resultado"
  fi

  if [ -n "$LGC" ] && ! grep -q '^import ' "$src" &&
     [ "$(wc -l < "$src")" -eq "$(wc -l < "$caso/fmt.lg")" ]; then
    # El IR incluye la ruta del archivo: se compila el original y el formateado en la misma ruta.
    # Si el formato cambia el número de líneas, los mensajes de error en tiempo de ejecución
    # (que llevan la línea) cambian: en ese caso no se compara el IR.
    mkdir -p "$caso/x"
    cp "$src" "$caso/x/$base"
    if "$LGC" "$caso/x/$base" -o "$caso/x/out" --emit-llvm > /dev/null 2>&1; then
      mv "$caso/x/out.ll" "$caso/ir_orig.ll"
      cp "$caso/fmt.lg" "$caso/x/$base"
      if "$LGC" "$caso/x/$base" -o "$caso/x/out" --emit-llvm > /dev/null 2>&1 &&
         cmp -s "$caso/ir_orig.ll" "$caso/x/out.ll"; then
        ok "IR idéntico $rel"
      else
        bad "IR idéntico $rel" "el IR cambia tras formatear"
      fi
    fi
  fi
done < <(find "$ROOT/examples" "$ROOT/stdlib" "$ROOT/tests/e2e" -name '*.lg' 2> /dev/null | sort)

echo "lgfmt: $pass pasaron, $fail fallaron"
[ "$fail" -eq 0 ]
