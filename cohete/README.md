# Cohete: simulador de vuelo con API REST y minishell

Juego de simulación de un cohete. El backend es una API REST escrita en **Lenguaje** (`../lenguaje`),
y el jugador controla el cohete desde una terminal simulada (minishell) con comandos para encender
motores, ajustar el rumbo y diagnosticar o reparar fallos en vuelo.

Las bibliotecas y el framework se escriben en el propio lenguaje y son reutilizables fuera de este
proyecto. Este proyecto es el primer usuario real de ellos.

## Arquitectura prevista

| Capa | Ubicación | Estado |
|---|---|---|
| Sockets TCP | `lenguaje/stdlib/tcp.lg` (nativos en `lenguaje/src/runtime/std/tcp.c`) | Hecho |
| Búsqueda y recorte de cadenas | `lenguaje/stdlib/cadena.lg` (nativos en `cadena.c`) | Hecho |
| Servidor HTTP de prueba | `lenguaje/examples/http_hola.lg` | Hecho (responde a `curl`) |
| Entrada estándar (leer línea) | `lenguaje/stdlib/terminal.lg` | Pendiente |
| JSON (codificar y decodificar) | `lenguaje/stdlib/json.lg` | Pendiente |
| Framework HTTP (rutas, petición, respuesta) | `lenguaje/stdlib/web.lg` | Pendiente |
| Motor de simulación del cohete | `cohete/src/simulacion.lg` | Pendiente |
| Minishell (comandos) | `cohete/src/minishell.lg` | Pendiente |
| API REST del juego | `cohete/src/main.lg` | Pendiente |

## Limitaciones del lenguaje que condicionan el diseño

Confirmadas en `lenguaje/docs/limitaciones.md` y en pruebas directas:

- No hay entrada estándar (`input()` no existe). La minishell necesita una función nativa para leer líneas.
- Las cadenas no tienen métodos ni indexación; se usan `cadena.buscar` y `cadena.recortar`.
- No hay `<` entre cadenas: los comandos se despachan con `==`, en cadena de `if`/`elif`.
- No se puede recorrer un diccionario con `for`, así que las rutas de la API se despachan con `if`/`elif`
  sobre método y ruta, no con una tabla.
- Las funciones de nivel principal no son valores: un manejador de ruta no se puede guardar en un diccionario.
- Los nombres `sub`, `len` y otros son palabras reservadas.

## Decisiones tomadas

1. Los errores de red devuelven `-1` en lugar de detener el programa, para que el servidor siga atendiendo.
2. La biblioteca de red se llama `tcp` y no `red`, porque `red` ya es la biblioteca de redes neuronales.
3. El `\r` se añadió al lenguaje (lexer) porque HTTP lo necesita. Tiene prueba e2e (`escape_retorno`).
