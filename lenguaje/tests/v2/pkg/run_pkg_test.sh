#!/usr/bin/env bash
# Pruebas del gestor de paquetes lgpm (B5). Sin red: las dependencias git apuntan a un repositorio local.
# Uso: tests/v2/pkg/run_pkg_test.sh <ruta_lgc> <ruta_lgpm>
# Trabaja sobre una copia en una carpeta temporal y usa un HOME temporal (caché ~/.lgpm aislada).
# Sale con código 1 si algún caso falla.
set -u

if [ $# -lt 2 ]; then
  echo "uso: $0 <ruta_lgc> <ruta_lgpm>" >&2
  exit 2
fi
LGC="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
LGPM="$(cd "$(dirname "$2")" && pwd)/$(basename "$2")"
HERE="$(cd "$(dirname "$0")" && pwd)"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

export LGPM_LGC="$LGC"
export HOME="$TMP/home"
mkdir -p "$HOME"
unset LG_PATH

WORK="$TMP/work"
cp -R "$HERE" "$WORK"
rm -f "$WORK/run_pkg_test.sh"

pass=0
fail=0
failed=()
ok()  { pass=$((pass + 1)); echo "PASS  $1"; }
bad() { fail=$((fail + 1)); failed+=("$1"); echo "FAIL  $1: $2"; }

# 1. lgpm nuevo: crea el proyecto y se puede construir y ejecutar.
name="nuevo"
mkdir -p "$TMP/nuevos" && cd "$TMP/nuevos" || exit 1
if "$LGPM" nuevo mi_app >/dev/null 2>&1 && [ -f mi_app/lg.toml ] && [ -f mi_app/src/main.lg ] \
   && (cd mi_app && "$LGPM" construir >/dev/null 2>&1) \
   && [ "$(./mi_app/build/mi_app)" = "Hola desde mi_app" ]; then
  ok "$name"
else
  bad "$name" "lgpm nuevo/construir/ejecutar no produjo la salida esperada"
fi

# 2. Dependencia por ruta: resolver escribe lg.lock; construir y ejecutar dan la salida esperada.
APP="$WORK/proyecto_app"
name="ruta_resolver"
if (cd "$APP" && "$LGPM" resolver >/dev/null 2>&1) && grep -q '^\[utilidades\]' "$APP/lg.lock" \
   && grep -q '^ruta = ' "$APP/lg.lock"; then
  ok "$name"
else
  bad "$name" "lg.lock no generado o sin la dependencia"
fi

name="ruta_construir"
if (cd "$APP" && "$LGPM" construir >/dev/null 2>&1) && "$APP/build/proyecto_app" >"$TMP/app.out" 2>&1 \
   && diff -u "$APP/expected.txt" "$TMP/app.out" >"$TMP/app.diff"; then
  ok "$name"
else
  bad "$name" "salida distinta: $(head -n 6 "$TMP/app.diff" 2>/dev/null | tr '\n' ' ')"
fi

name="ruta_ejecutar"
if (cd "$APP" && "$LGPM" ejecutar >"$TMP/ejec.out" 2>/dev/null) && diff -u "$APP/expected.txt" "$TMP/ejec.out" >/dev/null; then
  ok "$name"
else
  bad "$name" "lgpm ejecutar no dejó en stdout solo la salida del programa"
fi

# 3. lgpm añadir por ruta: edita lg.toml, rechaza duplicados y el proyecto compila con la nueva dependencia.
mkdir -p "$TMP/anadir" && cd "$TMP/anadir" || exit 1
"$LGPM" nuevo anadir_app >/dev/null 2>&1
cd "$TMP/anadir/anadir_app" || exit 1
name="anadir_ruta"
if "$LGPM" añadir utilidades --ruta "$WORK/utilidades" >/dev/null 2>&1 \
   && grep -q '^utilidades = { ruta = "' lg.toml; then
  printf 'import utilidades\n\nprint(doble(5))\n' > src/main.lg
  if "$LGPM" ejecutar 2>/dev/null | grep -qx 10; then
    ok "$name"
  else
    bad "$name" "el proyecto no compiló/ejecutó con la dependencia añadida"
  fi
else
  bad "$name" "lgpm añadir --ruta no escribió lg.toml"
fi

name="anadir_duplicada"
if "$LGPM" añadir utilidades --ruta "$WORK/utilidades" >/dev/null 2>&1; then
  bad "$name" "se aceptó una dependencia duplicada"
else
  ok "$name"
fi

# 4. Dependencia git (repositorio local): clona en la caché, fija la revisión y la importa.
REPO="$TMP/repo_git"
mkdir -p "$REPO/src"
cd "$REPO" || exit 1
git init -q
printf 'fn valor_git() -> int:\n    return 4\n' > src/cuatro.lg
git add -A && git -c user.name=test -c user.email=test@example.com commit -q -m "v1"
git tag v1.0.0
printf 'fn valor_git() -> int:\n    return 5\n' > src/cuatro.lg
git add -A && git -c user.name=test -c user.email=test@example.com commit -q -m "v2"
git tag v2.0.0

mkdir -p "$TMP/git_work" && cd "$TMP/git_work" || exit 1
"$LGPM" nuevo git_app >/dev/null 2>&1
cd "$TMP/git_work/git_app" || exit 1
name="git_anadir"
if "$LGPM" añadir cuatro_pkg --git "$REPO" --rev v1.0.0 >/dev/null 2>&1; then
  ok "$name"
else
  bad "$name" "lgpm añadir --git falló"
fi
printf 'import cuatro\n\nprint(valor_git())\n' > src/main.lg

name="git_ejecutar"
if "$LGPM" ejecutar >"$TMP/git.out" 2>"$TMP/git.err" && [ "$(cat "$TMP/git.out")" = "4" ]; then
  ok "$name"
else
  bad "$name" "salida '$(cat "$TMP/git.out" 2>/dev/null)' (se esperaba 4; la revisión v1.0.0 devuelve 4): $(head -n 3 "$TMP/git.err" | tr '\n' ' ')"
fi

name="git_lock"
if "$LGPM" resolver >/dev/null 2>&1 && grep -q '^rev = "v1.0.0"' lg.lock && grep -q '^commit = "' lg.lock; then
  ok "$name"
else
  bad "$name" "lg.lock sin rev o commit"
fi

# 5. Errores: dependencia inexistente y módulo no encontrado.
name="err_dependencia_ruta"
mkdir -p "$TMP/err_dep" && cd "$TMP/err_dep" || exit 1
"$LGPM" nuevo dep_rota >/dev/null 2>&1
cd "$TMP/err_dep/dep_rota" || exit 1
printf '[proyecto]\nnombre = "dep_rota"\n\n[dependencias]\nfantasma = { ruta = "../no_existe" }\n' > lg.toml
if "$LGPM" construir >/dev/null 2>"$TMP/dep.err"; then
  bad "$name" "se aceptó una dependencia que no existe"
elif grep -q "que no es una carpeta" "$TMP/dep.err"; then
  ok "$name"
else
  bad "$name" "mensaje inesperado: $(head -n 2 "$TMP/dep.err" | tr '\n' ' ')"
fi

name="err_modulo_no_encontrado"
mkdir -p "$TMP/err_mod" && cd "$TMP/err_mod" || exit 1
"$LGPM" nuevo mod_rota >/dev/null 2>&1
cd "$TMP/err_mod/mod_rota" || exit 1
printf 'import falta_este_modulo\n\nprint(1)\n' > src/main.lg
if "$LGPM" construir >/dev/null 2>"$TMP/mod.err"; then
  bad "$name" "compiló con un import inexistente"
elif grep -q "no se puede abrir el módulo 'falta_este_modulo'" "$TMP/mod.err"; then
  ok "$name"
else
  bad "$name" "mensaje inesperado: $(head -n 2 "$TMP/mod.err" | tr '\n' ' ')"
fi

echo
echo "Resumen: $pass PASS, $fail FAIL"
if [ "$fail" -gt 0 ]; then
  printf '  - %s\n' "${failed[@]}"
  exit 1
fi
exit 0
