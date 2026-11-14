# lib/app: componentes y estilos (estilo React Native) para lib/ui

Primitivas que devuelven `Nodo` (de `lib/ui/html.lg`), estilos tipados que generan CSS con nombre de
clase estable, responsive por puntos de ruptura y el documento HTML completo. Se renderiza con
`lib/ui/render.lg`, así que el escape y el filtro de atributos peligrosos son los mismos.

## Módulos

| Archivo | Contenido |
|---|---|
| `estilo.lg` | `struct Estilo`, `estilo_nuevo`, `estilo_clase`, `estilo_css`, `estilo_copia` |
| `primitivas.lg` | `caja`, `fila`, `columna`, `texto`, `boton`, `entrada`, `imagen`, `lista` |
| `responsive.lg` | `responsive_movil`, `responsive_tableta`, `responsive_columna_movil`, `unidad_pct/vw/rem` |
| `hoja.lg` | `hoja_css(estilos)`: un `<style>` con una regla por estilo distinto |
| `app.lg` | `pagina(titulo, estilos, cuerpo)`: documento HTML completo |
| `rutas.lg` | tabla de rutas `ruta`, `rutas_agregar`, `rutas_resolver` (F2) |
| `estado.lg` | estado en globales: `estado_nuevo`, `estado_get`, `estado_set`, `estado_sumar`... (F3) |
| `app_puente.js` | `AppPuente.montar`: navegación con `history`, eventos y re-render con entradas (F2, F3) |

Uso: `-I lib/app -I lib/ui -I stdlib`. `import app` resuelve a `lib/app/app.lg` solo si `lib/web`
no está antes en `LG_PATH` (lib/web también tiene un `app.lg`).

## Estilos

```lg
mut e = estilo_nuevo()          # todo vacío = sin estilo
e.relleno = "16px"
e.fondo = "#ffffff"
e.direccion = "fila"            # "fila" | "columna"
e = responsive_columna_movil(e) # en móvil (< 600 px) pasa a columna
```

- Un campo vacío no emite nada. `centrar: true` centra en ambos ejes y anula `justificar`.
- El nombre de clase es `"e" + hash(contenido)`. El hash combina dos módulos primos (sin XOR, que el
  lenguaje no tiene, y con comprobación de desbordamiento). Mismo estilo, mismo nombre.
- Los valores se filtran: solo letras, dígitos, espacio y `#%.,()-+/_`.
- Asignar un struct o una lista comparte el objeto. Por eso las funciones que derivan un estilo
  (`fila`, `columna`, `responsive_*`) trabajan sobre una copia (`estilo_copia`).

## Responsive

| Punto | Regla generada |
|---|---|
| movil (< 600 px) | `@media (max-width: 599px)` |
| tableta (600–1023 px) | `@media (min-width: 600px) and (max-width: 1023px)` |
| escritorio (>= 1024 px) | la regla base |

## Regla de uso: pasa todos los estilos a `pagina`

Las primitivas solo añaden la clase. La regla CSS sale de la lista de estilos que pasas a
`pagina(titulo, estilos, cuerpo)`. Si usas un estilo y no está en la lista, su clase no tiene regla.
`fila` y `columna` fijan la dirección en una copia: para que la regla exista, fija
`direccion` en el propio estilo y regístralo.

```lg
import app
import primitivas

fn vista() -> str:
    mut t = estilo_nuevo()
    t.relleno = "16px"
    cuerpo = caja(t, [texto(t, "Hola <mundo>")])
    return pagina("Inicio", [t], cuerpo)
```

## Ejemplos y pruebas

- `tests/app/run_app.sh <lgc>`: pruebas `tests/app/lib_app_*.lg` con salida esperada (estilo_css,
  unicidad del nombre de clase con 500 estilos aleatorios, primitivas, hoja y pagina).
- `tests/app/tienda.lg`: tienda con una fila responsive. Nativo y wasm
  (`tests/wasm_ui/navegador/tienda_app.html`, que sirve `tienda_app.wasm`).

## Rutas (F2)

```lg
import rutas

rutas_nuevas()
rutas_agregar(ruta("/", 1))
rutas_agregar(ruta("/detalle/:id", 2))
m = rutas_resolver("/detalle/42?x=1")   # m.encontrada = true, m.pantalla = 2, m.parametros = ["42"]
```

- Patrón: segmentos separados por `/`; `:nombre` casa con cualquier segmento no vacío y su valor va a
  `parametros`, en orden. Gana la primera ruta agregada que case.
- La consulta (`?`) y el fragmento (`#`) se ignoran; las barras sobrantes también. La raíz es `/`.
- Sin coincidencia: `encontrada = false`, `pantalla = 404` (`rutas_pantalla_404()`).
- El path no se decodifica (`%20` queda tal cual) y la comparación distingue mayúsculas.
- La tabla es global (`mut rutas_tabla`): `rutas_nuevas()` la vacía.

### Navegación desde el navegador

`lib/app/app_puente.js` envuelve `LgPuente` (no lo modifica). `AppPuente.montar` carga el `.wasm`, pinta
la vista y maneja la navegación:

```html
<script src="lib/ui/puente.js"></script>
<script src="lib/app/app_puente.js"></script>
<script>
  AppPuente.montar({ wasm: 'app.wasm', base: '/mi-app', contenedor: document.getElementById('app') });
</script>
```

- `app.ir('/detalle/42')`: `history.pushState`, resuelve la ruta y re-pinta. Atrás/adelante (`popstate`) hacen lo mismo.
- Enlaces internos: `<a href="/detalle/42" data-ruta="/detalle/42">` (o un `button`). El clic se intercepta.
- `base`: carpeta de la app; las rutas son relativas a ella.
- Los exports wasm de la app: `vista() -> str`, `ruta_inicio() -> int`, `ruta_byte(b: int) -> int`,
  `ruta_cambio() -> int` (devuelve la pantalla), y `evento(id: int) -> int`. Los exports solo reciben
  `int`, `float` o `bool`, así que el path viaja byte a byte: `ruta_inicio` vacía el búfer, `ruta_byte`
  añade cada byte (ASCII; los demás se sustituyen por `?`) y `ruta_cambio` resuelve. Un path de un
  navegador ya viene codificado, así que es ASCII.
- Recargar una ruta profunda (`/detalle/42`) necesita que el servidor sirva la página en cualquier ruta
  (respuesta de SPA). Un servidor estático sin esa regla da 404 al recargar.

## Estado (F3)

```lg
import estado

mut contador_id = estado_nuevo(0)       # global: se crea al inicio del programa
fn pulsar() -> int:
    return estado_sumar(contador_id, 1) # escribe y devuelve el valor nuevo
```

- Los enteros (`estado_nuevo`, `estado_get`, `estado_set`, `estado_sumar`, `estado_reiniciar`) y los
  textos (`estado_nuevo_texto`, `estado_get_texto`, `estado_set_texto`) tienen ids independientes.
- Un id fuera de rango detiene el programa (índice de lista).
- Un evento es un export `evento(id: int) -> int` que llama a las funciones de estado. JS lo llama y re-pinta.
  Eventos en el HTML: `data-accion="N"` (clic en un botón) o `data-cambio="N"` (evento `input`: se pone
  en el elemento que envuelve la entrada, p. ej. un `div`).

### Entradas al re-pintar

`innerHTML` regenera el HTML entero. `app_puente.js` guarda antes de pintar el valor de cada `input` y
`textarea` del contenedor, y el foco y la selección de la entrada activa; después los restaura. La clave
es el `id`, o el `name` si no hay `id` (`entrada` de `primitivas.lg` no da `id`). Probado en el navegador:
escribir en medio de un texto mantiene el cursor; un `button.click()` por código mantiene el foco.

Limitaciones de esa solución:
- Un clic real con el ratón en un botón mueve el foco al botón antes del evento, así que el foco se pierde
  (la entrada conserva su valor, pero `activeElement` es el botón). Es comportamiento del navegador.
- Una entrada sin `id` ni `name` no se restaura.
- Si Lenguaje quiere imponer un valor a una entrada, el valor del DOM gana: el puente restaura lo que el usuario escribió.

## Ejemplos y pruebas (F2, F3)

- `tests/app/rutas_resolver.lg`: exacta, parámetros en orden, 404, raíz, consulta ignorada, prioridad, mayúsculas y transferencia de bytes.
- `tests/app/estado_basico.lg`, `tests/app/estado_globales.lg`: incremento, reinicio, textos, globales independientes.
- `tests/app/app_rutas.lg`: dos pantallas (`/` y `/detalle/:id`) y un contador. Wasm en
  `tests/wasm_ui/navegador/app_rutas.html`.
- `tests/app/app_formulario.lg`: entrada, botón y contador de cambios. Wasm en
  `tests/wasm_ui/navegador/app_formulario.html`.

## Limitaciones

- Sin valores por defecto ni genéricos: `estilo_nuevo()` y asignación de campos.
- Los estilos se registran a mano en `pagina`. Las globales `mut` (F0) permitirían registrarlos solos.
- Re-render completo: ver «Entradas al re-pintar».

## PWA y móvil

Una vista de `lib/app` puede ser una PWA instalable. Piezas en `lib/app/pwa/` y `lib/app/pwa.lg`:

| Archivo | Contenido |
|---|---|
| `pwa/manifest.webmanifest` | Nombre, `display: standalone`, colores, `start_url`, iconos 192 y 512 |
| `pwa/icons/*.png` | Iconos generados por `pwa/generar_iconos.py` (zlib + struct, sin dependencias) |
| `pwa/sw.js` | Service worker escrito a mano: shell cache-first, resto network-first, caché versionada |
| `pwa/registro.js` | Registra el service worker desde la página; no bloquea la carga |
| `pwa.lg` | `pwa_cabeceras(base, sw)`: etiquetas `<head>` y el `<script>` de registro |

Uso: `pagina_extra("Mi app", estilos, cuerpo, pwa_cabeceras("", "sw.js"))`. El manifest, el service worker
y los iconos deben estar en la raíz de la app (el ámbito del service worker es su carpeta). Antes de
publicar, copiar `sw.js` y definir `self.LG_PWA_SHELL` con las rutas del shell (ver
`tests/wasm_ui/navegador/pwa_sw.js`, ejemplo).

Área segura: `pwa_cabeceras` añade `padding` con `env(safe-area-inset-*)`. Esos valores solo son distintos
de 0 con `viewport-fit=cover` en el `viewport`, que `pagina()` y `pagina_extra()` ya ponen.

## lgpm app: crear, construir y levantar una app (F5)

Con `lgpm` (`src/tools/pkg/`) una app web en Lenguaje se crea, se compila a wasm y se sirve en tres comandos:

```
lgpm app nueva demo       # crea demo/: lg.toml, src/main.lg, web/index.html y web/icons/
cd demo
lgpm app construir        # compila src/main.lg a build/app.wasm y genera dist/
lgpm app levantar 8080    # sirve dist/ en http://localhost:8080 (y la IP de red, para el móvil)
```

- `nueva`: la app de ejemplo usa `lib/app` (rutas `/` y `/detalle/:id`, contador en una global, formulario
  con una entrada, primitivas y responsive). `web/index.html` carga los puentes y el wasm con rutas absolutas.
- `construir`: `lgc --target wasm32` con los exports `vista`, `ruta_inicio`, `ruta_byte`, `ruta_cambio` y
  `evento`. Copia a `dist/` la página, el `.wasm`, `puente.js`, `app_puente.js`, `registro.js`, los iconos,
  `manifest.webmanifest` (`start_url` y `scope` `"./"`) y `sw.js` (shell + `lg_sw.js`, copia de `pwa/sw.js`).
- `levantar [puerto]`: servidor estático propio (sockets POSIX), 8080 por defecto, escucha en 0.0.0.0.
  Tipos MIME correctos (`application/wasm`, `application/manifest+json`), `..` da 403, una ruta sin
  extensión que no existe sirve `index.html` (fallback SPA: `/detalle/7` al recargar), y un puerto ocupado
  da error con la sugerencia del siguiente. Ctrl+C sale limpio.

Ruta del compilador: `LGPM_LGC` si está definida; si no, el `lgc` compilado junto con `lgpm`. La raíz del
repositorio (`lib/`, `stdlib/`) es la de la compilación de `lgpm`; `LGPM_RAIZ` la sobrescribe. El `lgc`
debe tener el objetivo wasm32 (con LLVM y el sysroot de WASI en la configuración de CMake).

Limitaciones: la página no funciona abierta como `file://` (hace falta `levantar`), y se sirve en la raíz
del servidor (las rutas son absolutas). El service worker requiere contexto seguro (localhost o HTTPS).

Pruebas: `tests/v2/pkg/run_app_test.sh` (ctest `pkg_app`).

Límites conocidos:
- iOS: no hay notificaciones push en PWA y algunas APIs no están disponibles. La instalación es manual:
  Safari, Compartir → Añadir a pantalla de inicio.
- Android: Chrome ofrece instalar la app cuando se cumplen los criterios (manifest válido, iconos y
  service worker en HTTPS o localhost).
- Offline: solo cubre el shell y los recursos ya visitados. Las respuestas que no son 200 no se cachean.
- El service worker solo funciona en contexto seguro (HTTPS o localhost).

Pruebas: `tests/app/pwa_cabeceras.lg` (salida esperada de `pwa_cabeceras` y `pagina_extra`). El ejemplo
navegador es `tests/wasm_ui/navegador/pwa_app.html`.
