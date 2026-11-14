# Arquitectura

Este documento describe cómo está organizado el compilador `lgc`, qué hace cada etapa, qué contratos hay entre ellas y cómo añadir una característica nueva. Está pensado para quien vaya a tocar el código de `src/`.

## Pipeline

```text
archivo.lg
   │
   ▼
[driver/loader]   carga el archivo y sus imports (búsqueda de módulos); los fusiona en un solo Program
   │  Program (decls + stmts), Diagnostics
   ▼
[lexer]           texto -> tokens con INDENT / DEDENT / NEWLINE
   │  std::vector<Token>
   ▼
[parser]          tokens -> AST (recursive descent, recuperación por sentencia)
   │  Program (AST sin tipos)
   ▼
[sema]            resuelve nombres, comprueba tipos y reglas; anota el AST
   │  Program anotado + TypeTable
   ▼
[codegen]         AST anotado -> módulo LLVM (+ DWARF con -g) -> verificación -> objeto nativo
   │  archivo.o
   ▼
[driver/main]     enlaza con `cc`, liblg_runtime.a y -lm -> ejecutable (+ .dSYM con -g en macOS)
   │
   ▼
[runtime/lg_rt.c] (enlazado) cadenas, listas, diccionarios, tensores, objetos con conteo atómico
                  y débil, panics, RNG; std/hilos.c (hilos y canales, pthread)
```

Los errores se acumulan en un único objeto `Diagnostics` que recorre todas las etapas. Si hay errores al final de una etapa, el driver los imprime y termina con código 1; no se pasa a la siguiente. La única excepción es el runtime, que no usa `Diagnostics`: los errores de ejecución los imprime y termina el proceso.

Alrededor del compilador hay tres herramientas que reutilizan las mismas etapas: `lgfmt` (lexer, parser y un impresor sobre el AST), `lglsp` (lexer, parser y sema para los diagnósticos y la navegación) y `lgpm` (lee `lg.toml` y llama a `lgc`). Ver [herramientas auxiliares](#herramientas-auxiliares).

## Módulos

| Ruta | Responsabilidad |
|---|---|
| `src/common/diagnostics.{h,cpp}` | `Diagnostic`, `Diagnostics`: acumula errores y avisos, y los imprime con la línea fuente y el marcador `^` |
| `src/lexer/lexer.{h,cpp}` | Tokeniza, produce INDENT/DEDENT/NEWLINE y `tokenKindName` |
| `src/ast/ast.h` | Nodos del AST: `Program`, `Decl` (`FnDecl`, `StructDecl`, `ImplDecl`, `ImportDecl`), `Stmt`, `Expr`, `TypeExpr` |
| `src/parser/parser.{h,cpp}` | `parseProgram(file, source, diags)`: un parser descendente por sentencia |
| `src/driver/loader.{h,cpp}` | `loadProgram(mainFile, prog, diags)`: resuelve `import` en profundidad, sin repetir y detectando ciclos; busca cada módulo en la carpeta del archivo, `LG_PATH`, `-I` y `stdlib/` |
| `src/sema/types.{h,cpp}` | `Type`, `TypeTable`: tipos internados, `compatible`, texto de tipos |
| `src/sema/sema.{h,cpp}` | `analyze(prog, types, diags)`: nombres, tipos, mutabilidad, llamadas |
| `src/codegen/codegen.{h,cpp}` | `emitObjectFile`, `emitLLVMIR`: traducción a LLVM IR y objeto |
| `src/codegen/debug_info.{h,cpp}` | Metadatos DWARF (`DICompileUnit`, `DISubprogram`, `DILocation`, variables y tipos) que emite el codegen con `-g` |
| `src/runtime/lg_rt.{h,c}` | ABI del runtime en C: objetos con conteo atómico y débil, panics, cadenas, listas, diccionarios, tensores |
| `src/runtime/std/*.c` | Nativos de la biblioteca estándar (`hilos.c`, `datos.c`, `red.c`, `README.c`) |
| `src/driver/main.cpp` | Interfaz de línea de comandos y enlace |
| `src/tools/fmt/` | `lgfmt`: `format.cpp` (formato canónico), `printer.cpp`, `comments.cpp` (comentarios), `ast_dump.cpp` (`--dump-ast`), `main.cpp` |
| `src/tools/lsp/` | `lglsp`: `server.cpp` (JSON-RPC y métodos LSP), `analysis.cpp` (diagnósticos y consultas sobre el análisis), `json.cpp` |
| `src/tools/pkg/` | `lgpm`: `lgpm.cpp` (comandos y resolución de dependencias), `toml.cpp` (lectura de `lg.toml`) |
| `tests/v2/` | Pruebas de las herramientas y de depuración: `fmt/`, `lsp/`, `pkg/`, `debug/` |
| `tests/unit/*_test.cpp` | Pruebas GoogleTest de lexer y parser (`parser_test.cpp`) y de sema (`sema_test.cpp`) |
| `tests/runtime/runtime_test.c` | Pruebas del runtime en C; se compilan a mano (no están en CMake) |
| `tests/e2e/` | Programas `.lg` con expectativas y `run_e2e.sh` |
| `examples/` | Programas de ejemplo |

Las cabeceras de `src/runtime/lg_rt.h` y `src/sema/types.h` se marcan como contratos: su cambio afecta a varias etapas a la vez.

## Contratos entre etapas

### Lexer -> parser

Un `Token` tiene `kind`, `line`, `col` (ambos desde 1), `text` (lexema o contenido de cadena), `intValue` y `floatValue`. El lexer emite:

- `NEWLINE` al final de cada línea con contenido, salvo dentro de `()`, `[]` o `{}`.
- `INDENT` y `DEDENT` según la indentación. Las líneas vacías y de solo comentario no cambian la indentación.
- `END` al final.

Los errores léxicos se registran en `Diagnostics` y el lexer sigue. Una cadena sin cerrar produce un error y un token `String` igualmente, para que el parser avance.

### Parser -> AST

`parseProgram` devuelve un `Program` con `decls` (declaraciones al nivel principal) y `stmts` (sentencias sueltas), en el orden del archivo. Cada nodo lleva un `Loc` (archivo, línea, columna).

Reglas de la gramática que el resto del compilador da por supuestas:

- Los `elif` se desugaran: un `IfStmt` cuyo `elseBody` contiene otro `IfStmt`.
- Un `for` con rango usa un `RangeExpr` como `iter`; sema lo rechaza en cualquier otra posición.
- Una asignación solo acepta como destino `Ident`, `Index` o `Member`.
- Los argumentos con nombre se guardan en `Call::argNames`; sema decide si se admiten.

Los errores de sintaxis lanzan una excepción interna `ParseError` que se captura por sentencia: se resincroniza en el `NEWLINE` del mismo nivel y, si la sentencia abría bloque, se salta el bloque entero. Así se reportan varios errores sin cascadas.

### Loader -> sema

`loadProgram` analiza el archivo principal y, antes que sus sentencias, las de sus imports (en profundidad, cada archivo una sola vez). Después fusiona todo en un único `Program`: las declaraciones de todos los módulos y las sentencias en orden de carga. Los imports se resuelven relativos al directorio del archivo que importa.

### Sema -> codegen

`analyze` no crea un árbol nuevo: anota el AST existente y devuelve `true` si no hubo errores. Las anotaciones que consume el codegen son:

| Nodo | Campo | Contenido |
|---|---|---|
| `Expr` | `type` | tipo del valor (`const Type*`) |
| `Call` | `callKind` | `Function`, `Extern`, `Method`, `StructCtor`, `Builtin`, `BuiltinMethod` o `Closure` (llamada a un valor de tipo `fn(...)`) |
| `Call` | `target` | nombre LLVM de la función, nombre C de un extern, nombre de la struct o nombre del builtin (`"List.append"`, `"Tensor.forma"`, ...) |
| `Member` | `isField` | `true` si es acceso a campo, `false` si es método |
| `Assign` | `declares` | `true` si la asignación declara una variable nueva |
| `VarDecl` | `type` | tipo de la variable |
| `ForStmt` | `varType` | tipo de la variable del bucle |
| `Param`, `FnDecl` | `type`, `retType` | tipos de parámetros y retorno; `FnDecl::mangled` |
| `StructDecl::fields` | `type` | tipo de cada campo |
| `FnDecl` (anidada) | `funcType`, `captures` | tipo `fn(...)` de la función y variables externas que usa (`Capture::byRef` si son `mut`) |
| `VarDecl`, `Param` | `captured` | `true` si una función anidada captura esa variable `mut` por referencia (vive en una celda del heap) |

El analizador interna los tipos en `TypeTable`: dos tipos iguales son el mismo puntero, así que el codegen puede comparar tipos con `==`. Los tensores con dimensiones desconocidas (`-1`) son la excepción y se comparan con `TypeTable::compatible`.

Orden de sema: primero recoge structs, luego firmas de funciones y métodos, después analiza las sentencias del nivel principal (ámbito global) y por último los cuerpos de funciones y métodos. Las funciones no ven el ámbito global, así que sema lo comprueba al resolver identificadores.

### Loader: orden de búsqueda

Para `import a/b`, el loader busca `a/b.lg` en este orden y toma el primero que existe: la carpeta del archivo que importa, las carpetas de `LG_PATH` (separadas por `:`), las de `-I` en orden de línea de comandos y, al final, la `stdlib/` del propio compilador. Si ninguna lo contiene, el error lista todas las rutas probadas. `lgpm` no añade nada al loader: pasa cada dependencia como una opción `-I`.

### Codegen -> objeto y enlace

`emitObjectFile` construye un módulo LLVM con el host como objetivo, lo verifica (`verifyModule`), aplica el nivel de optimización pedido y emite un objeto `.o`. `emitLLVMIR` hace lo mismo sin optimizar y devuelve el texto IR para `--emit-llvm`.

Con `-g`, el codegen crea los metadatos de depuración (`src/codegen/debug_info.cpp`) y sin `-g` el IR no cambia. El driver enlaza con `cc -o salida salida.o liblg_runtime.a -lm` (en Linux, además, `-pthread`). La ruta del runtime se fija en tiempo de compilación de `lgc` (definición `LG_RUNTIME_LIB`). En macOS, con `-g`, el driver ejecuta `dsymutil` sobre el ejecutable para generar `salida.dSYM`, que es donde lldb busca el DWARF; el `.o` se borra salvo con `--keep-obj`.

### Codegen -> runtime (ABI)

El codegen solo llama a funciones declaradas en `lg_rt.h`. Convenciones:

- Todo objeto del heap empieza con `LgHeader { int64_t rc; lg_destroy_fn destroy; int64_t weak; }`. Una struct del lenguaje tiene el layout LLVM `{ i64 rc, ptr destroy, i64 weak, campos... }`, que coincide con esa cabecera. El campo `weak` cuenta las referencias débiles (`Debil[T]`) más un implícito mientras `rc > 0`: así una débil puede observar un objeto ya liberado (`rc == 0`) sin tocar memoria libre. El conteo `rc` y `weak` se modifica con `__atomic`, así que los objetos pueden compartirse entre hilos.
- Hilos y canales: `lg_hilo_lanzar` crea un hilo pthread que ejecuta una función generada por el codegen con sus argumentos copiados en un vector de slots; `lg_hilo_esperar` devuelve el resultado con +1 si es heap. `lg_canal_enviar` y `lg_canal_recibir` son el único punto de sincronización entre hilos, y `recibir` devuelve la posesión del valor al llamador. Las listas, diccionarios y tensores no tienen cerrojos.
- Los valores que caben en 64 bits viajan como *slot* en listas y diccionarios: `int` directo, `float` como bits de `double`, `bool` como 0/1, heap como puntero convertido a entero.
- `lg_dict_set` toma posesión del valor; `lg_list_append` toma posesión del slot; `lg_list_get` y `lg_dict_get_or_panic` no retienen.
- Los panics de desbordamiento y de división por cero reciben `file` y `line` desde el codegen. Los de colecciones (índice, clave, `pop`) y de conversión no reciben ubicación: el runtime los reporta sin `archivo:línea`.

## Modelo de memoria del codegen

El codegen sigue las reglas documentadas al inicio de `src/codegen/codegen.cpp`:

1. Toda expresión de tipo heap produce un valor poseído (+1).
2. Leer una variable (`Ident`) retiene el valor: la variable conserva su referencia.
3. Almacenar en variable, campo, elemento o contenedor transfiere la posesión.
4. Un temporal que no se almacena se libera justo después de usarlo.
5. Los parámetros son préstamos: la función no los libera ni los reasigna.
6. Las funciones devuelven valores poseídos.
7. Las variables locales heap se liberan al salir de su ámbito, también en `break`, `continue` y `return`. Los `alloca` se inicializan a null para que liberar una variable aún no declarada sea un no-op.

Consecuencia: no hay recolector de ciclos. Un ciclo de referencias nunca libera su memoria.

## Nombres generados

Detalle interno del backend: estos nombres no forman parte del lenguaje ni hay que escribirlos en el código fuente.

| Entidad | Nombre LLVM |
|---|---|
| función de usuario `f` | `u_f` |
| método `m` de la struct `S` | `u_S_m` |
| función extern `f` | `f` (sin prefijo) |
| `main` del programa | `main`, generado a partir de las sentencias del nivel principal |
| destructor de la struct `S` | `destroy_S` (función interna) |

Las sentencias del nivel principal son locales de `main`; no hay variables globales.

## Diagnósticos y errores de ejecución

- Errores de compilación: `Diagnostics::error(loc, mensaje, sugerencia)`. El driver los imprime en orden de aparición, con la línea fuente y el marcador.
- Errores de ejecución: el runtime escribe `archivo:línea: error de ejecución: mensaje` (o sin ubicación si no la conoce) en stderr, vacía stdout con `fflush` y llama a `exit(1)`.

## Herramientas auxiliares

Las herramientas viven en `src/tools/` y se compilan junto a `lgc`. No añaden reglas al lenguaje: reutilizan el lexer, el parser y el analizador.

- **`lgfmt`** (`src/tools/fmt/`): `format.cpp` imprime el AST en forma canónica y comprueba que el resultado se vuelve a parsear. `comments.cpp` lee los comentarios del texto fuente, porque el lexer los descarta; el lexer no cambia. `printer.cpp` y `ast_dump.cpp` implementan la impresión y `--dump-ast`. Los comentarios de cola en listas multilínea no se conservan (limitación conocida, ver [limitaciones](../limitaciones.md#herramientas)).
- **`lglsp`** (`src/tools/lsp/`): servidor JSON-RPC por stdin/stdout con cabeceras `Content-Length`. `server.cpp` despacha los métodos (`initialize`, `textDocument/didOpen`, `definition`, `hover`, `completion`...). `analysis.cpp` parsea y analiza cada documento abierto, con sus imports, y construye un índice propio de definiciones y referencias sobre el AST anotado. No se modifica `ast.h`. No hay `references` ni `rename`.
- **`lgpm`** (`src/tools/pkg/`): `toml.cpp` es un lector mínimo de TOML (tablas, claves, cadenas y tablas en línea; sin arrays ni tablas con puntos). `lgpm.cpp` resuelve cada dependencia a una carpeta (ruta local, o clon git en `~/.lgpm/cache` con la revisión fijada) y ejecuta `lgc` con una opción `-I` por dependencia. Las dependencias de las dependencias no se leen. El binario de `lgc` que usa se elige con la variable `LGPM_LGC` y, por defecto, es el que se compiló con `lgpm`.

Pruebas de estas herramientas: `tests/v2/fmt/run_fmt_tests.sh`, `tests/v2/lsp/` (`json_test.cpp` y `lsp_test.py`), `tests/v2/pkg/run_pkg_test.sh` y `tests/v2/debug/run_debug_tests.sh`. Todas se ejecutan con `ctest --test-dir build`.

## Cómo añadir una característica

### Regla de alcance

El repositorio no define por escrito una regla de alcance. La que sigue se deduce de cómo están separadas las etapas y de los contratos de `lg_rt.h` y `types.h`:

1. **Una característica entra completa o no entra.** Una construcción que se parsea pero no se analiza, o que se analiza pero no se genera, deja el compilador en un estado inconsistente. Si no puedes completar las etapas, no la añadas y documéntala en [limitaciones](limitaciones.md).
2. **Cambia solo lo que necesita cambiar.** Si una característica requiere modificar un contrato compartido (un `Token`, un nodo del AST, un `Type`, una función de `lg_rt.h`), hazlo en un cambio aparte y describe el impacto en las demás etapas.
3. **Toda característica tiene prueba.** Como mínimo, un programa en `tests/e2e/` con su `.expected` (o `.error` o `.rterror`). Si toca el parser o sema, añade casos a `tests/unit`.
4. **La documentación sigue al código.** La sintaxis va a [sintaxis](referencia/sintaxis.md), los tipos y operadores a sus páginas, los mensajes de error a [errores comunes](referencia/errores-comunes.md) y la guía si la característica es de uso general.

### Pasos

Para una sentencia o expresión nueva, en este orden:

1. **Léxico** (`lexer.cpp`): si introduces una palabra clave o un token, añádelo a `TokenKind`, a la tabla `keywords()` y a `tokenKindName`.
2. **Parser** (`parser.cpp`): añade la regla al método adecuado (`parseStatement`, `parsePrimary`, ...). Reporta errores con `fail` o `failExpected` y una sugerencia.
3. **AST** (`ast.h`): añade el nodo con su `kind` y su `Loc`.
4. **Sema** (`sema.cpp`): añade la regla de tipos y de mutabilidad. Anota los campos que el codegen necesite (tipo, `callKind`, `target`, ...).
5. **Codegen** (`codegen.cpp`): emite el IR. Respeta el modelo de memoria: cada temporal heap tiene un dueño claro.
6. **Runtime** (`lg_rt.h`, `lg_rt.c`): si hace falta una función nueva, declárala en la cabecera y implementala en C con los mismos convenios de posesión.
7. **Pruebas**: `tests/e2e/NOMBRE.lg` más su expectativa, y casos en `tests/unit/parser_test.cpp` o `sema_test.cpp`.
8. **Documentación**: actualiza [sintaxis](referencia/sintaxis.md) y, si procede, la guía y [limitaciones](limitaciones.md).

Para un builtin o método integrado nuevo, el camino es más corto: `sema.cpp` (el nombre, la firma y `target`), `codegen.cpp` (`emitBuiltin` o `emitBuiltinMethod`), y si hace falta, `lg_rt.c`. Después, la lista de [builtins](referencia/builtins.md).

### Comprobar el cambio

```bash
cmake --build build
ctest --test-dir build
```

`ctest` ejecuta las pruebas unitarias, las e2e, las adversariales, las de `lgfmt`, las de `lglsp`, las de `lgpm` y la de depuración con lldb (ver [README](../README.md#pruebas)).

## Dónde mirar cuando algo falla

| Síntoma | Etapa probable |
|---|---|
| error de sintaxis incorrecto o en cascada | lexer (indentación, tokens) o parser (recuperación) |
| error de tipo falso o falta un error | sema |
| `compile error` de LLVM o `llvm::verifyModule` falla | codegen |
| `undefined reference` al enlazar | runtime o `-lm`; una función extern que no existe en libc o libm |
| el programa termina con `error de ejecución` inesperado | runtime (`lg_rt.c`) o codegen (posesión) |
| memoria que no se libera | ciclo de referencias (limitación conocida) o un fallo de posesión en codegen |
| lldb no resuelve breakpoints | `lgc` sin `-g`, o en macOS el `.dSYM` no está junto al ejecutable (`codegen` / driver) |
| `lgpm` no encuentra un módulo de una dependencia | la dependencia no está en el `lg.toml` del proyecto (no hay dependencias transitivas) |
