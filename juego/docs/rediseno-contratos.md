# Rediseño CRT de la nave: análisis y contratos entre módulos

## Análisis del estado actual (juego/ y lenguaje/)

- **Motor (src/sim.lg, src/misiones.lg)**: 4 misiones con reloj real, 7 sistemas (reactor, refrigeracion,
  energia, oxigeno, comunicaciones, co2, hidrogeno). Averías guardadas como claves de traducción.
- **Intérprete (src/shell.lg)**: órdenes con nombre en 4 idiomas, tuberías grep/contar/mayus,
  scripts ejecutados con lgc como procesos y FIFOs (protocolo @CMD / @FIN).
- **Manuales (src/manual.lg)**: man y man-code traducidos.
- **Interfaz actual (src/escritorio.lg, src/apps.lg, src/pantalla.lg)**: escritorio Windows 7 con
  aplicaciones gráficas. Se sustituye por el diseño CRT. Los estilos viven en `crt.lg`.
- **Servidor (src/main.lg)**: lib/web (rutas, respuestas), HTML generado con lib/ui. Sin JavaScript:
  toda interacción es enlace o formulario text/plain.
- **Traducciones (i18n.lg, traducciones.json)**: 4 idiomas, claves con puntos. Las claves nuevas se
  escriben en `i18n/pendientes/<modulo>.json` y se fusionan después.
- **Librerías de lenguaje usadas**: json (serialización de partidas), archivo (ficheros con descriptor),
  traduccion (i18n), proceso (scripts), tiempo, texto, cadena, web, ui.

## Restricciones del lenguaje que hay que respetar

- No hay `for x in lista`: usar `for i in 0..len(lista)` e indexar.
- No hay operador ternario ni indexación de `str`: usar `recortar(s, i, i + 1)`.
- Asignar a campos de struct o a listas/diccionarios exige `mut` (la estructura es un objeto compartido:
  `mut u = t` y luego `u.campo = ...` funciona).
- Las variables globales de un módulo solo se reasignan desde ese módulo: exponer setters/getters.
- `len` cuenta bytes (las tildes y el japonés cuentan más). No alinear columnas con `len`.
- Todas las funciones son globales al importar: los nombres deben ser únicos (prefijo por módulo).
- Sin JavaScript propio: los menús son enlaces `<a href>` y los formularios usan `enctype="text/plain"`.

## Propiedad de ficheros (un dueño por fichero, sin excepciones)

| Fichero | Dueño | Contenido |
|---|---|---|
| src/crt.lg (nuevo) | agente crt-ui | CSS completo, página base, piezas de menú/arranque/ajustes/créditos |
| src/sim.lg, src/misiones.lg | agente motor | dificultad, misiones 5 y 6, serialización JSON, getters de misiones |
| src/partidas.lg (nuevo) | agente partidas | guardar y cargar 3 ranuras |
| src/consola.lg (nuevo) | agente consola | editor de Lenguaje, plantillas, ejecución |
| src/pantalla_misiones.lg (nuevo) | agente misiones-ui | selección de misiones con filtros y briefing |
| src/main.lg, src/escritorio.lg, src/apps.lg, src/pantalla.lg, src/shell.lg, src/manual.lg, src/i18n.lg | integrador (lead) | rutas, pantallas, integración |
| traducciones.json | integrador (lead) | fusión de i18n/pendientes/*.json |
| i18n/pendientes/<agente>.json | cada agente | claves nuevas en es, en, fr, ja |
| tests/ | agente pruebas (fase 2) | programas de prueba con salida esperada |

## Contratos entre módulos (funciones que deben existir exactamente con estas firmas)

### crt.lg (agente crt-ui)
- `crt_css() -> str`: bloque `<style>` completo. Variables de color en `:root`; temas `data-theme`
  green (por defecto), amber, cyan, white; clases `.menu-item`, `.menu-item.active`, `.config-btn`,
  `.launch-mission-btn`, `.mission-layout`, `.mission-list`, `.mission-card`, `.filter-btn`,
  `.lg-console-layout`, `.lg-editor-pane`, `.lg-output-pane`, `.lg-output-terminal`, `.lg-status-bar`,
  `.back-hint`, y las clases de las aplicaciones (`.app-contenido`, `.tarjeta`, `.fila`, `.chip`...).
- `crt_pagina(titulo: str, cuerpo: Nodo) -> str`: HTML completo: cabecera (SYS_ID, reloj estático,
  selector de tema), capa de scanlines, cuerpo y pie con ayuda. Usa `crt_tema_actual()` y
  `crt_scanlines_activas()`.
- `crt_cambiar_tema(nombre: str) -> bool`, `crt_tema_actual() -> str`,
  `crt_alternar_scanlines()`, `crt_scanlines_activas() -> bool`.
- `crt_titulo(texto: str) -> Nodo` (h2 con `// ...`), `crt_volver(href: str, etiqueta: str) -> Nodo`.
- `crt_opcion(numero: int, etiqueta: str, sub: str, href: str, activa: bool) -> Nodo`
  (elemento de menú con `<a>`), `crt_menu(opciones: List[Nodo]) -> Nodo`.
- `crt_arranque(lineas: List[str], href_continuar: str) -> Nodo`.
- `crt_ajustes(...)`: `crt_fila_config(etiqueta: str, valor: str, href: str) -> Nodo`.
- `crt_creditos() -> Nodo`.
- `crt_banner() -> Nodo` (ASCII del título del juego).

### motor (agente motor): sim.lg y misiones.lg
- `mision_total() -> int` (6), `mision_titulo(i: int) -> str` (texto traducido),
  `mision_briefing(i: int) -> str` (traducido), `mision_limite(i: int) -> int`,
  `mision_dificultad(i: int) -> str` (clave: `dif.facil`, `dif.media`, `dif.dificil`, `dif.extrema`),
  `mision_estado_de(i: int) -> int` (0 bloqueada, 1 disponible, 2 en curso, 3 completada, 4 fallida),
  `mision_seleccionable(i: int) -> bool`.
- Serialización: `sim_a_json() -> str`, `sim_desde_json(texto: str) -> str` ("" si va bien, si no
  mensaje), `mision_a_json() -> str`, `mision_desde_json(texto: str) -> str`. Usan lib/datos/json.lg.
- `mision_iniciar(n: int) -> str`, `mision_info() -> str`, `mision_listar() -> str`, `mision_avanzar()`
  y el resto de la API actual se mantienen con las mismas firmas.

### partidas (agente partidas): partidas.lg
- `partida_guardar(ranura: int) -> str` (vacío si va bien; si no, mensaje traducido).
- `partida_cargar(ranura: int) -> str` (vacío si va bien).
- `partida_resumen(ranura: int) -> str` (vacío si la ranura está libre; si no, una línea traducida).
- Ficheros: `saves/partida_N.json` (N = 1..3), creados con lib/nucleo/archivo.lg.

### consola (agente consola): consola.lg
- `consola_plantillas() -> List[str]` (ids: `hola`, `json`, `mision`, `buffer`).
- `consola_plantilla(id: str) -> str` (código Lenguaje de la plantilla).
- `consola_ejecutar(codigo: str) -> str`: escribe `scripts/consola.lg` y llama a
  `shell_ejecutar_script("consola.lg")`; devuelve la salida (o el error de compilación).
- `consola_guardar_salida(s: str)`, `consola_ultima_salida() -> str`, `consola_nodo(codigo: str,
  salida: str) -> Nodo` (pantalla completa con editor, plantillas y salida; usa las clases de crt.lg).

### misiones-ui (agente misiones-ui): pantalla_misiones.lg
- `pantalla_misiones_nodo() -> Nodo`: lista, filtros y panel de briefing. Usa `mision_total()` y los
  getters de motor.
- `misiones_filtrar(f: str)` (ALL, EASY, MEDIUM, HARD, EXTREME; guarda el filtro),
  `misiones_seleccionar(i: int)`, `misiones_seleccionada() -> int`.
- El botón de lanzamiento es un formulario `POST /mision/lanzar` (campo `numero`); lo atiende el integrador.

### integrador (lead): main.lg y resto
- Rutas nuevas: `/` (arranque o menú según estado), `/menu`, `/juego`, `/misiones`, `/misiones/:f`,
  `/mision/:n` (selecciona), `/mision/lanzar`, `/consola`, `/consola/ejecutar`, `/consola/plantilla/:id`,
  `/ajustes`, `/tema/:nombre`, `/escanlineas`, `/creditos`, `/partidas`, `/partida/guardar/:n`,
  `/partida/cargar/:n`.

## Claves de traducción
Cada agente escribe `i18n/pendientes/<agente>.json` con las 4 lenguas (es, en, fr, ja) y las mismas
claves. Prefijos: `crt.*` (crt-ui), `mis.*` motor y misiones-ui (`mision.*` para textos de misión),
`part.*` (partidas), `lg.*` (consola), `dif.*`. El integrador fusiona todo en traducciones.json.
