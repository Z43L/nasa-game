#!/usr/bin/env bash
# Batería QA de robustez del framework web (lib/web) y de su capa HTTP (lib/red/http.lg).
# Uso: tests/qa/web/run_web_qa.sh <ruta_lgc>
#
# Secciones:
#   1. Servidores HTTP de prueba (plantillas/servidor_http.lg, sobre lib/red/http.lg):
#      basura, timeouts, carga con fugas de descriptores y peticiones enormes.
#   2. Los mismos casos de red contra el framework (plantillas/servidor_web.lg), si lib/web existe.
#   3. Fuzz de enrutamiento (fuzz_rutas.py) contra una referencia Python, si lib/web existe.
#   4. Casos del compilador en casos/: NOMBRE.lg + NOMBRE.error (lgc debe salir con 1 y no
#      caerse) o NOMBRE.expected (compila, ejecuta y compara salida). Un caso con la línea
#      "# requiere: web" necesita lib/web.
#
# Un caso que requiere lib/web y no encuentra lib/web/router.lg y lib/web/app.lg cuenta como
# PENDIENTE, y cuenta como fallo: el script sale con 0 solo si todo pasa.
# Puertos: cada caso de red usa un puerto único entre 23000 y 23999.
# Código de salida: 0 todo pasa, 1 algún fallo o pendiente, 2 uso incorrecto.
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
ROOT="$(cd "$HERE/../../.." && pwd)"
CASOS="$HERE/casos"
PLANT="$HERE/plantillas"
LIMITE=120        # segundos máximos por ejecución (compilación, servidor o cliente)
TIMEOUT_MS=1000   # timeout de lectura del servidor de prueba (fijar_tiempo / web_recibir)
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT

INC_BASE=(-I "$ROOT/lib/red" -I "$ROOT/lib/datos" -I "$ROOT/stdlib")
INC_WEB=(-I "$ROOT/lib/web" "${INC_BASE[@]}")

WEB_OK=0
if [ -s "$ROOT/lib/web/router.lg" ] && [ -s "$ROOT/lib/web/app.lg" ]; then
  WEB_OK=1
fi

pass=0
fail=0
pend=0
RC=0

ok()         { pass=$((pass + 1)); echo "PASS  $1"; }
bad()        { fail=$((fail + 1)); echo "FAIL  $1: $2"; }
pendiente()  { pend=$((pend + 1)); fail=$((fail + 1)); echo "PENDIENTE  $1: $2"; }

# Ejecuta "$@" con límite de tiempo LIMITE. Deja el código de salida en RC (137 si se mató).
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

# Verdadero si el proceso existe y no es zombi.
proceso_vivo() {
  local st
  st="$(ps -o stat= -p "$1" 2>/dev/null)"
  st="${st// /}"
  [ -n "$st" ] && [ "${st#Z}" = "$st" ]
}

# Compila $2 con los -I de $3 (nombre de array) y deja el binario en $TMP/bin_$1.
compilar() {
  local nombre="$1" src="$2"
  local -a inc
  if [ "$3" = web ]; then inc=("${INC_WEB[@]}"); else inc=("${INC_BASE[@]}"); fi
  con_limite "$LGC" "$src" "${inc[@]}" -o "$TMP/bin_$nombre" 2>"$TMP/$nombre.cerr" >/dev/null
}

# Caso de compilación con error esperado. $1 nombre, $2 fuente, $3 base|web, $4 fichero .error
caso_error_compilacion() {
  local nombre="$1" src="$2" alcance="$3" esperado="$4"
  compilar "$nombre" "$src" "$alcance"
  if [ "$RC" -ge 128 ]; then
    bad "$nombre" "lgc se cayó (código $RC)"
  elif [ "$RC" -ne 1 ]; then
    bad "$nombre" "lgc salió con código $RC (se esperaba 1)"
  elif [ ! -s "$TMP/$nombre.cerr" ]; then
    bad "$nombre" "lgc no escribió ningún mensaje de error"
  elif [ -s "$esperado" ] && ! grep -qF -- "$(cat "$esperado")" "$TMP/$nombre.cerr"; then
    bad "$nombre" "el mensaje no contiene '$(cat "$esperado")'"
  else
    ok "$nombre"
  fi
}

# Compila, ejecuta y compara la salida estándar con $3. $1 nombre, $2 fuente, $4 alcance.
caso_salida_exacta() {
  local nombre="$1" src="$2" esperado="$3" alcance="$4"
  compilar "$nombre" "$src" "$alcance"
  if [ "$RC" -ne 0 ]; then
    bad "$nombre" "fallo al compilar (código $RC): $(head -n 3 "$TMP/$nombre.cerr" | tr '\n' ' ')"
    return
  fi
  local work="$TMP/run_$nombre" previo="$PWD"
  mkdir -p "$work"
  cd "$work" || return
  con_limite "$TMP/bin_$nombre" >"$TMP/$nombre.out" 2>"$TMP/$nombre.err"
  cd "$previo" || return
  if [ "$RC" -ne 0 ]; then
    local pista=""
    [ "$RC" -eq 137 ] && pista=" (posible cuelgue)"
    bad "$nombre" "el programa terminó con código $RC$pista: $(head -n 2 "$TMP/$nombre.err" | tr '\n' ' ')"
    return
  fi
  if diff -u "$esperado" "$TMP/$nombre.out" >"$TMP/$nombre.diff"; then
    ok "$nombre"
  else
    bad "$nombre" "salida distinta: $(head -n 8 "$TMP/$nombre.diff" | tr '\n' ' ')"
  fi
}

# Caso de red: compila plantilla, lanza el servidor y ejecuta fuzz_http.py con los argumentos $5...
# Después envía /salir y exige que el servidor termine con código 0.
# $1 nombre, $2 plantilla, $3 puerto, $4 base|web, $5... subcomando de fuzz_http.py
caso_red() {
  local nombre="$1" plantilla="$2" puerto="$3" alcance="$4"
  shift 4
  if [ "$alcance" = web ] && [ "$WEB_OK" -ne 1 ]; then
    pendiente "$nombre" "lib/web incompleto (faltan lib/web/router.lg o lib/web/app.lg)"
    return
  fi
  local src="$TMP/$nombre.lg" log="$TMP/$nombre.srv.log" salida="$TMP/$nombre.py.out"
  sed -e "s/__PUERTO__/$puerto/g" -e "s/__TIMEOUT__/$TIMEOUT_MS/g" "$plantilla" >"$src"
  compilar "$nombre" "$src" "$alcance"
  if [ "$RC" -ne 0 ]; then
    bad "$nombre" "fallo al compilar el servidor (código $RC): $(head -n 3 "$TMP/$nombre.cerr" | tr '\n' ' ')"
    return
  fi

  "$TMP/bin_$nombre" >"$log" 2>&1 &
  local spid=$!
  con_limite python3 -I "$HERE/fuzz_http.py" "$@" --puerto "$puerto" --pid "$spid" >"$salida" 2>&1
  local rc_py=$RC
  if [ "$rc_py" -eq 0 ]; then
    con_limite python3 -I "$HERE/fuzz_http.py" salir --puerto "$puerto" --pid "$spid" >>"$salida" 2>&1
    rc_py=$RC
  fi

  # Espera a que el servidor termine por sí mismo (tras /salir); si no, se mata.
  local i=0
  while proceso_vivo "$spid" && [ "$i" -lt 200 ]; do
    sleep 0.1
    i=$((i + 1))
  done
  local srv_rc=0 srv_colgado=0
  if proceso_vivo "$spid"; then
    srv_colgado=1
    kill -9 "$spid" 2>/dev/null
  fi
  wait "$spid" 2>/dev/null
  srv_rc=$?

  grep '^METRICA' "$salida" | sed 's/^/      /'
  if [ "$rc_py" -ne 0 ]; then
    local motivo
    motivo="$(grep '^FALLO' "$salida" | tail -n 1)"
    [ "$rc_py" -eq 137 ] && motivo="$motivo (posible cuelgue del cliente)"
    bad "$nombre" "${motivo:-cliente salió con código $rc_py} | servidor: $(head -n 2 "$log" | tr '\n' ' ')"
  elif [ "$srv_colgado" -eq 1 ]; then
    bad "$nombre" "el servidor no terminó tras GET /salir (se mató)"
  elif [ "$srv_rc" -ne 0 ]; then
    bad "$nombre" "el servidor terminó con código $srv_rc tras las pruebas: $(head -n 2 "$log" | tr '\n' ' ')"
  else
    ok "$nombre"
  fi
}

echo "== 1. Robustez HTTP (lib/red/http.lg, servidor sin framework) =="
caso_red garbage_http "$PLANT/servidor_http.lg" 23010 base garbage --n 1000
caso_red timeout_http "$PLANT/servidor_http.lg" 23020 base timeout --timeout-ms "$TIMEOUT_MS"
caso_red carga_http "$PLANT/servidor_http.lg" 23030 base carga --n 2000
caso_red grande_http "$PLANT/servidor_http.lg" 23040 base grande

echo
echo "== 2. Robustez del framework (lib/web) =="
caso_red garbage_web "$PLANT/servidor_web.lg" 23110 web garbage --n 1000
caso_red timeout_web "$PLANT/servidor_web.lg" 23120 web timeout --timeout-ms "$TIMEOUT_MS"
caso_red carga_web "$PLANT/servidor_web.lg" 23130 web carga --n 2000
caso_red grande_web "$PLANT/servidor_web.lg" 23140 web grande

echo
echo "== 3. Fuzz de enrutamiento contra referencia Python =="
if [ "$WEB_OK" -ne 1 ]; then
  pendiente fuzz_rutas "lib/web incompleto (faltan lib/web/router.lg o lib/web/app.lg)"
else
  RUTAS="$TMP/rutas"
  if ! python3 -I "$HERE/fuzz_rutas.py" --seed 4242 --n 5000 --salida "$RUTAS" >"$TMP/rutas.gen" 2>&1; then
    bad fuzz_rutas "el generador falló: $(head -n 3 "$TMP/rutas.gen" | tr '\n' ' ')"
  else
    grep '^METRICA' "$TMP/rutas.gen" | sed 's/^/      /'
    caso_salida_exacta fuzz_rutas "$RUTAS/rutas.lg" "$RUTAS/rutas.expected" web
  fi
fi

echo
echo "== 4. Compilador: errores de uso y casos de salida =="
for src in "$CASOS"/*.lg; do
  [ -e "$src" ] || continue
  nombre="$(basename "$src" .lg)"
  alcance=base
  if grep -q '^# requiere: web' "$src"; then alcance=web; fi
  if [ "$alcance" = web ] && [ "$WEB_OK" -ne 1 ]; then
    pendiente "$nombre" "lib/web incompleto (faltan lib/web/router.lg o lib/web/app.lg)"
    continue
  fi
  if [ -f "$CASOS/$nombre.error" ]; then
    caso_error_compilacion "$nombre" "$src" "$alcance" "$CASOS/$nombre.error"
  elif [ -f "$CASOS/$nombre.expected" ]; then
    caso_salida_exacta "$nombre" "$src" "$CASOS/$nombre.expected" "$alcance"
  else
    bad "$nombre" "sin archivo .error ni .expected"
  fi
done

# Fuente binaria aleatoria: lgc debe rechazarla con código 1, nunca caerse.
head -c 4096 /dev/urandom >"$TMP/basura_binaria.lg"
caso_error_compilacion compil_basura_binaria "$TMP/basura_binaria.lg" base /dev/null

echo
echo "RESUMEN: pasan=$pass fallan=$fail (pendientes=$pend)"
if [ "$fail" -eq 0 ]; then
  exit 0
fi
exit 1
