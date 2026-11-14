# Herramientas

Además del compilador `lgc`, el proyecto incluye tres herramientas. Se compilan con el resto del proyecto
y quedan en `build/bin/`:

| Herramienta | Para qué sirve |
|---|---|
| `lgc` | Compila un programa `.lg` a un ejecutable. Opciones en [primeros pasos](../guia/01-primeros-pasos.md). |
| `lgfmt` | Formatea código `.lg` de forma canónica. |
| `lglsp` | Servidor de lenguaje (LSP): diagnósticos, ir a definición, información al pasar el ratón y autocompletado para un editor. |
| `lgpm` | Gestor de paquetes: crea proyectos, declara dependencias por ruta o git y construye el proyecto. |

Los ejemplos de esta página se ejecutaron con las herramientas de `build/bin/` en un directorio temporal.

## lgfmt

```text
uso: lgfmt [-i | --check] archivo.lg [más.lg ...]
     lgfmt --dump-ast archivo.lg
```

| Uso | Efecto |
|---|---|
| `lgfmt archivo.lg` | Escribe el código formateado en la salida estándar. No toca el archivo. |
| `lgfmt a.lg b.lg` | Formatea varios archivos, en orden, a la salida estándar. |
| `lgfmt -i archivo.lg` | Reescribe el archivo en su sitio. |
| `lgfmt --check archivo.lg` | No escribe nada. Sale con código 1 si algún archivo no está formateado, y dice cuál en stderr. |
| `lgfmt --dump-ast archivo.lg` | Imprime el árbol sintáctico en forma de texto. Sirve para depurar el formateador y el parser. |

`-i` y `--check` no se pueden usar juntos (código de salida 2). Si el archivo tiene errores de sintaxis,
`lgfmt` escribe el diagnóstico en stderr, sale con código 1 y no modifica ningún archivo.

Ejemplo real. Entrada `sucio.lg`:

```text
x=1+2*3
fn f(a:int,b:int)->int:
  return a+b
print(f(x,2))   # suma
```

Salida de `lgfmt sucio.lg`:

```text
x = 1 + 2 * 3

fn f(a: int, b: int) -> int:
    return a + b

print(f(x, 2))  # suma
```

Con `lgfmt --check sucio.lg` la salida estándar está vacía, el mensaje `lgfmt: sucio.lg no está formateado`
va a stderr y el código de salida es 1. Tras `lgfmt -i sucio.lg`, `--check` sale con 0.

`--dump-ast` imprime el árbol del programa anterior así:

```text
(program (decls
  (fn f intern body (params (param a (type name int)) (param b (type name int))) ret (type name int) (block (return (bin add (id a) (id b))))))
 (stmts
  (assign (id x) (bin add (int 1) (bin mul (int 2) (int 3))))
  (exprstmt (call (id print) [ ( (call (id f) [ ( (id x)) ( (int 2)) ])) ]))))
```

Limitaciones:

- Los comentarios de cola dentro de listas multilínea no se conservan (según el informe de la herramienta).
- El formato es fijo: no hay opciones para cambiar la anchura de línea ni el tamaño de la sangría.

## lglsp

`lglsp` habla el protocolo LSP por la entrada y la salida estándar, con las cabeceras `Content-Length`
de siempre. No tiene opciones: el editor lo lanza como comando y se comunica con él por sus tuberías.

Para conectarlo a un editor, indica como comando del servidor la ruta a `build/bin/lglsp` (ruta absoluta)
y asocia los archivos `.lg` al lenguaje `lg`. El servidor no necesita configuración: calcula los
diagnósticos de cada documento abierto con el mismo analizador que `lgc`, y usa el texto que el editor
envía, no el archivo del disco.

Puedes comprobar que responde sin editor enviándole una petición `initialize` y una `exit`:

```bash
{ printf 'Content-Length: %d\r\n\r\n' "$(wc -c < init.json)"; cat init.json
  printf 'Content-Length: %d\r\n\r\n' "$(wc -c < salir.json)"; cat salir.json; } | ./build/bin/lglsp
```

donde `init.json` contiene `{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}` y `salir.json`
contiene `{"jsonrpc":"2.0","method":"exit"}`. La respuesta comienza con sus `capabilities`
(`definitionProvider`, `hoverProvider`, `completionProvider` con el disparador `.`).

Métodos soportados:

| Método | Qué hace |
|---|---|
| `initialize`, `initialized`, `shutdown`, `exit` | Ciclo de vida del servidor. |
| `textDocument/didOpen`, `didChange`, `didSave`, `didClose` | Sincronización de documentos. `didChange` envía el texto completo. Los diagnósticos llegan con `textDocument/publishDiagnostics` tras abrir, cambiar o guardar. |
| `textDocument/definition` | Va a la declaración del nombre bajo el cursor (variable o función; comprobado con una variable). |
| `textDocument/hover` | Muestra la firma de la función bajo el cursor, por ejemplo `fn doble(x: int) -> int`. |
| `textDocument/completion` | Devuelve palabras clave, tipos, builtins y los nombres del documento (funciones y variables). El disparador es `.`. |

Cualquier otro método recibe un error `-32601` (`método no soportado`).

Limitaciones:

- No hay `textDocument/references`, `textDocument/rename` ni símbolos del documento.
- No hay formateo desde el editor: para formatear, usa `lgfmt`.
- La sincronización es completa (no incremental): cada cambio reenvía el documento entero.

## lgpm

`lgpm` crea proyectos, gestiona sus dependencias y los construye con `lgc`. Su manifiesto es `lg.toml`
y genera `lg.lock` al resolver las dependencias.

```text
uso: lgpm <comando>
  nuevo NOMBRE                       crea un proyecto nuevo
  resolver                           resuelve dependencias y escribe lg.lock
  construir                          compila el proyecto con lgc -> build/NOMBRE
  ejecutar [args...]                 construye y ejecuta el programa
  añadir NOMBRE --ruta R             añade una dependencia por ruta
  añadir NOMBRE --git URL [--rev X]  añade una dependencia git
```

### Crear y construir

```bash
lgpm nuevo mi_app
cd mi_app
lgpm construir
./build/mi_app
```

Salida real de `lgpm nuevo mi_app`:

```text
proyecto 'mi_app' creado en mi_app/
  construir: cd mi_app && lgpm construir
```

`nuevo` crea esta estructura:

```text
mi_app/
  lg.toml
  src/main.lg        print("Hola desde mi_app")
```

`construir` escribe el ejecutable en `build/mi_app`, dentro del proyecto. Los mensajes de progreso van a
stderr. `ejecutar` construye y después ejecuta `build/mi_app` con los argumentos que le pases; su salida
es la del programa. Los errores de compilación de `lgc` se muestran tal cual, y `lgpm` termina con un
mensaje propio:

```text
lgpm: compilando mi_app 0.1.0 con 0 dependencia(s)
lgpm: construido build/mi_app
Hola desde mi_app
```

`lgpm` busca `lg.toml` en la carpeta actual y en sus superiores, así que puedes ejecutarlo desde un
subdirectorio del proyecto.

### lg.toml

```toml
[proyecto]
nombre = "mi_app"
version = "0.1.0"

[dependencias]
mates = { ruta = "../libs/mates" }
cuatro_pkg = { git = "https://ejemplo.com/cuatro.git", rev = "v1.0.0" }
```

- `nombre` es obligatorio. Solo admite letras, cifras, `_` y `-`. `version` es informativa: `lgpm` no
  compara versiones.
- Cada dependencia es una tabla en línea con **exactamente una** de estas claves: `ruta` (carpeta local,
  relativa al `lg.toml`) o `git` (URL). `rev` (etiqueta, rama o commit) solo se acepta junto con `git`.
- Una dependencia por ruta o git aporta una carpeta de búsqueda para `lgc`: `<raíz>/src` si existe, o la
  raíz misma si no.

### Añadir dependencias

```bash
lgpm añadir mates --ruta ../libs/mates
lgpm añadir cuatro_pkg --git https://ejemplo.com/cuatro.git --rev v1.0.0
```

`añadir` escribe la línea en `[dependencias]` y conserva el resto del archivo. Rechaza un nombre que ya
existe en `lg.toml`, por ejemplo con `lgpm: error: ya existe la dependencia 'mates' en lg.toml`. Con
`--ruta`, la ruta se guarda relativa al `lg.toml`.

Ejemplo real: con `libs/mates/src/mates.lg` que define `doble`, y `src/main.lg` con
`import mates` y `print(doble(21))`:

```text
$ lgpm añadir mates --ruta ../libs/mates
dependencia 'mates' añadida a .../mi_app/lg.toml
  mates = { ruta = "../libs/mates" }
$ lgpm ejecutar
lgpm: compilando mi_app 0.1.0 con 1 dependencia(s)
lgpm: construido build/mi_app
42
```

Para una dependencia git, `lgpm` clona el repositorio en `~/.lgpm/cache`, fija la revisión con
`git checkout` y la usa desde ahí. Una revisión que no existe en el repositorio da un error que indica
revisar la etiqueta, rama o commit en `lg.toml`.

### Resolver y lg.lock

`lgpm resolver` comprueba todas las dependencias y escribe `lg.lock`. Para una dependencia git, anota la
revisión y el commit exacto:

```toml
# lg.lock generado por lgpm. No editar a mano.

[cuatro_pkg]
git = "https://ejemplo.com/cuatro.git"
rev = "v1.0.0"
commit = "5d5e43bb7f08c0b09812a8d708cf23541a3da75c"
ruta = "/Users/ana/.lgpm/cache/cuatro-6e86f2eba131b75b"

[mates]
ruta = "/Users/ana/proyectos/libs/mates"
```

`construir` y `ejecutar` no leen `lg.lock`: resuelven de nuevo las dependencias de `lg.toml`. El
`lg.lock` documenta qué versión se resolvió.

### Limitaciones

- **Sin dependencias transitivas**: `lgpm` solo lee el `lg.toml` del proyecto. Si una dependencia importa
  un módulo que necesita otra biblioteca, hay que añadir esa biblioteca también en el `lg.toml` del
  proyecto. Si no, `lgc` falla con `no se puede abrir el módulo ... no se encuentra en ninguna ruta de búsqueda`.
- **Sin registro ni resolución de versiones**: no hay repositorio central ni rangos de versión; una
  dependencia es una ruta o una revisión git concreta.
- Requiere `git` en el `PATH` para las dependencias git.

## Ver también

- [Módulos e import](../guia/09-modulos-e-import.md): cómo se buscan los módulos que importa un programa.
- [Depuración](depuracion.md): `lgc -g` y lldb.
- [Limitaciones](../limitaciones.md).
