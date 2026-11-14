# lib/ui: interfaces HTML desde Lenguaje (nativo y WebAssembly)

Construye un árbol de nodos, lo serializa a HTML y lo pinta en el navegador con `puente.js`.
El estado vive en JavaScript: las funciones de Lenguaje no leen globales, así que se pasa como parámetro.

## Módulos

| Archivo | Contenido |
|---|---|
| `html.lg` | `struct Nodo`, constructores, `ui_escapar`, `ui_nombre_seguro`, `ui_unir` |
| `render.lg` | `ui_render(n: Nodo) -> str`, `ui_es_void` |
| `puente.js` | `LgPuente` (cargar, iniciar, llamar, texto, leerStr). No se modifica. |

Uso: `import html` y `import render`, con `-I lib/ui -I stdlib` (o `LG_PATH` con `lib/ui`).

## API

- `ui_elemento(etiqueta: str, atributos: List[str], hijos: List[Nodo]) -> Nodo`
  Atributos en lista plana alternando nombre y valor: `["class", "caja", "id", "x"]`.
- `ui_texto(s: str) -> Nodo`: texto, escapado al renderizar.
- `ui_boton(texto: str, accion: int) -> Nodo`: `<button type="button" data-accion="N">texto</button>`.
- `ui_escapar(s: str) -> str`: `& < > " '` pasan a `&amp; &lt; &gt; &quot; &#39;`.
- `ui_render(n: Nodo) -> str`: HTML. Atributos siempre entre comillas dobles y escapados. Los elementos void (`br`, `img`, `input`, ...) no llevan cierre. Nombres de etiqueta y atributo se sanean con `ui_nombre_seguro` (solo letras, dígitos, `-`, `_`, `:`); etiqueta vacía tras sanear, `div`.

Regla: todo texto de usuario entra por `ui_texto` o `ui_escapar`. No concatenar texto sin escapar.

## Contrato de eventos (wasm)

La vista se regenera entera. La app exporta dos funciones:

- `vista(estado: int) -> str`: HTML completo. JS hace `contenedor.innerHTML = p.texto('vista', estado)`.
- `despachar(accion: int, estado: int) -> int`: devuelve el nuevo estado.

JS escucha `click` en un contenedor, toma `data-accion` del botón pulsado (`closest('[data-accion]')`), llama a `despachar` con `BigInt` (los `int` de Lenguaje son i64) y vuelve a pintar con `vista`.

```
lgc --target wasm32 --export vista --export despachar -I lib/ui -I stdlib app.lg -o app.wasm
```

## Ejemplos y pruebas

- `tests/ui/contador_app.lg`: contador con +1 y reiniciar, sin HTML escrito a mano.
- `tests/ui/grande_app.lg`: 10 000 elementos (prueba de carga para wasm).
- `tests/wasm_ui/navegador/contador_app.html`: página que usa el contador en el navegador (sirve con un servidor HTTP; `fetch` de `.wasm` no funciona desde `file://`).
- `node tests/ui/run_ui_app.js [ruta_lgc_wasm]`: pruebas wasm en Node.
- `tests/e2e/lib_ui_*.lg`: pruebas nativas (escape, render, contador, 10 000 nodos). Necesitan `lib/ui` en `LG_PATH`.

## Limitaciones

- Sin DOM: el re-render completo con `innerHTML` pierde el foco y la selección de los campos de texto; pensado para vistas sin entrada de texto.
- Sin genéricos ni funciones de primera clase; `List` no se pasa mutable a funciones, así que los constructores devuelven listas.
- `len` de `str` cuenta bytes. El escape trabaja byte a byte, correcto porque los caracteres especiales son ASCII.
