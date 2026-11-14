#!/usr/bin/env bash
# Fase 0 (globales mut): comprueba que los programas sin globales mut usadas por funciones generan
# exactamente el mismo IR (--emit-llvm) con el lgc nuevo que con el lgc de referencia.
# Uso: tests/fase0/comprobar_ir.sh <lgc_nuevo> <lgc_referencia>
#   Ejemplo: tests/fase0/comprobar_ir.sh build-f0/bin/lgc build/bin/lgc
# Corpus: tests/e2e/*.lg (salvo fase0_globales_*, que sí usan globales) y tests/wasm/*.lg.
# Sale con 0 si todos son idénticos (o fallan igual en ambos), 1 si hay diferencias.
set -u

if [ $# -ne 2 ]; then
  echo "uso: $0 <lgc_nuevo> <lgc_referencia>" >&2
  exit 2
fi
NUEVO="$1"
REF="$2"
for b in "$NUEVO" "$REF"; do
  if [ ! -x "$b" ]; then
    echo "no existe el ejecutable lgc: $b" >&2
    exit 2
  fi
done

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
export LG_PATH="$ROOT/tests/e2e/lg_path:$ROOT/lib/nucleo:$ROOT/lib/datos:$ROOT/lib/red:$ROOT/lib/web:$ROOT/lib/ui${LG_PATH:+:$LG_PATH}"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

same=0
diff_n=0
for src in "$ROOT"/tests/e2e/*.lg "$ROOT"/tests/wasm/*.lg; do
  [ -e "$src" ] || continue
  case "$(basename "$src")" in fase0_globales_*) continue ;; esac
  name="$(basename "$(dirname "$src")")_$(basename "$src" .lg)"
  out_new="$TMP/$name.new"
  out_ref="$TMP/$name.ref"
  "$NUEVO" --emit-llvm "$src" -o "$out_new" >/dev/null 2>&1
  st_new=$?
  "$REF" --emit-llvm "$src" -o "$out_ref" >/dev/null 2>&1
  st_ref=$?
  if [ "$st_new" -ne "$st_ref" ]; then
    diff_n=$((diff_n + 1))
    echo "DIFF  $name: código de salida $st_new frente a $st_ref"
    continue
  fi
  if [ "$st_new" -ne 0 ]; then
    same=$((same + 1))  # falla igual en ambos: no hay IR que comparar
    continue
  fi
  if cmp -s "$out_new.ll" "$out_ref.ll"; then
    same=$((same + 1))
  else
    diff_n=$((diff_n + 1))
    echo "DIFF  $name: el IR cambia"
  fi
done

echo "IR idéntico: $same programas; diferencias: $diff_n"
[ "$diff_n" -eq 0 ]
