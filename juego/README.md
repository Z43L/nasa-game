# Nave: control de errores (terminal web)

Simulación de una nave espacial en una terminal web. Diagnosticas y reparas sistemas averiados,
completas misiones contra reloj y automatizas tareas escribiendo scripts en **Lenguaje**.

Todo el juego está escrito en Lenguaje (`lg`) y usa el framework web de `lenguaje/`
(`lib/web` para el servidor, `lib/ui` para el HTML y `lib/red` para HTTP).

## Compilar y ejecutar

Desde esta carpeta (`juego/`), con `lenguaje/` compilado (ver su README):

```bash
../lenguaje/build/bin/lgc src/main.lg -I src -I lib -I ../lenguaje/lib/web -I ../lenguaje/lib/red \
  -I ../lenguaje/lib/ui -I ../lenguaje/lib/datos -I ../lenguaje/lib/nucleo -I ../lenguaje/stdlib \
  -o build/nave
./build/nave
```

Abre **http://localhost:8080** en el navegador. Ejecuta el servidor siempre desde `juego/`,
porque los scripts, los FIFOs de ejecución y los ficheros se buscan en rutas relativas.

## Órdenes

Escribe `ayuda` para la lista. Cada orden tiene su página: `man ORDEN`.

| Orden | Para qué sirve |
|---|---|
| `estado [sistema]` / `sensores` | Lectura de los sistemas |
| `valor SISTEMA` | Valor numérico (útil en scripts) |
| `diagnostico SISTEMA` | Localiza la avería (necesario antes de reparar) |
| `reparar SISTEMA` | Repara un sistema diagnosticado |
| `encender` / `apagar SISTEMA` | Activa o desactiva un sistema |
| `ajustar SISTEMA VALOR` | Fija el objetivo de un sistema regulado |
| `misiones`, `mision`, `iniciar N` | Lista, estado e inicio de misiones |
| `esperar SEGUNDOS` | Deja pasar tiempo real (1 a 60) |
| `ls`, `cat FICHERO`, `editar FICHERO`, `ejecutar FICHERO` | Ficheros y scripts |
| `man ORDEN`, `man-code TEMA` | Manuales de órdenes y del lenguaje |

Las órdenes admiten tuberías: `estado | grep AVERIA`, `estado | contar`, `estado | mayus`.

Temas de `man-code`: `sintaxis`, `variables`, `condiciones`, `bucles`, `funciones`,
`listas`, `diccionarios`, `structs`, `cadenas`, `nave` (API de scripts) y `scripts`.

## Misiones

| N | Misión | Límite | Objetivo |
|---|---|---|---|
| 1 | Refrigeración de emergencia | 120 s | Reparar la refrigeración y bajar el reactor de 480 °C |
| 2 | Fuga de oxígeno | 90 s | Reparar el oxígeno y mantenerlo en 95 kPa o más |
| 3 | Batería agotándose | 150 s | Reparar el reactor y mantener la energía en 30 % o más |
| 4 | Piloto automático | 180 s | Todos los sistemas sanos durante 15 s seguidos (requiere un script) |

Una misión falla si el reactor supera 650 °C, si la energía llega a 0 o si se acaba el tiempo.
Solo se desbloquea la siguiente misión al completar la anterior. Las completadas se pueden repetir.

## Scripts

1. `editar mi_script.lg` abre el editor (se guarda en `scripts/`).
2. `ejecutar mi_script.lg` lo compila con `lgc` y lo ejecuta.

Los scripts importan la API de la nave con `import nave` (ver `man-code nave`).
`scripts/ejemplo_vigilante.lg` resuelve la misión 4.

Un script tiene 60 segundos y no puede ejecutar otro script. Si falla la compilación,
el error aparece en la terminal.

## Arquitectura

| Archivo | Función |
|---|---|
| `src/main.lg` | Servidor HTTP (rutas, formularios, editor) |
| `src/shell.lg` | Intérprete de órdenes, tuberías y ejecución de scripts |
| `src/sim.lg` | Modelo de la nave y simulación por segundos |
| `src/misiones.lg` | Misiones, reloj real, objetivos y puntuación |
| `src/manual.lg` | Páginas de `man` y `man-code` |
| `src/pantalla.lg` | HTML de la terminal y del editor, registro de salida |
| `lib/nave.lg` | API que importan los scripts (`nave_*`) |

Los scripts se ejecutan como procesos aparte. Se comunican con el servidor por dos tuberías
FIFO (`run/in` y `run/out`): el script pide órdenes con `@CMD` y el servidor responde con `@FIN`.

Este juego necesita una primitiva de procesos que se añadió al runtime de Lenguaje:
`lib/nucleo/proceso.lg` y `src/runtime/std/proceso.c` (en `lenguaje/`).
