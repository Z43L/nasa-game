# Lenguaje

Lenguaje es un lenguaje de programación compilado con una sintaxis parecida a Python: la indentación marca los bloques, no hay punto y coma y cada variable tiene un tipo estático que se infiere o se escribe. El compilador, `lgc`, está escrito en C++17 sobre LLVM y genera código nativo. El runtime, en C, implementa cadenas, listas, diccionarios, tensores, hilos y canales, y la gestión de memoria por conteo de referencias con referencias débiles.

La documentación describe **solo lo que el compilador hace hoy**. Lo que la especificación contempla y todavía no está implementado está en [docs/limitaciones.md](docs/limitaciones.md).

## Estado actual

- **Lenguaje (v1)**: tipos escalares, listas, diccionarios, structs con métodos, tensores, `Option[T]`, funciones anidadas con captura, módulos con `import`, interop con C mediante `extern`.
- **Concurrencia (v2)**: `hilo`, `Hilo[T]`, `Canal[T]`, `async`/`await` y referencias débiles `Debil[T]`. Verificado en macOS (arm64).
- **Herramientas (v2)**: formateador, servidor de lenguaje, gestor de paquetes y depuración con lldb (ver la lista abajo).
- **Fuera de alcance**: GPU y Windows. **Sin verificar en ejecución**: Linux (ver [instalación en Linux](docs/instalacion-linux.md)).

El detalle del avance está en [docs/plan.md](docs/plan.md).

## Herramientas

Tras compilar, `build/bin/` contiene:

| Herramienta | Para qué sirve |
|---|---|
| `lgc` | Compilador: genera el ejecutable de un programa `.lg`. |
| `lgfmt` | Formateador: imprime el código en formato canónico, o lo reescribe con `-i`; `--check` para CI. |
| `lglsp` | Servidor de lenguaje (LSP) para un editor: diagnósticos, ir a definición, información al pasar el ratón y autocompletado. |
| `lgpm` | Gestor de paquetes: crea proyectos con `lg.toml`, añade dependencias por ruta o git, y construye y ejecuta. |

Uso y ejemplos de cada herramienta: [docs/referencia/herramientas.md](docs/referencia/herramientas.md).

## Índice de la documentación

| Documento | Para qué sirve |
|---|---|
| [Guía](docs/guia/01-primeros-pasos.md) | Tutorial por capítulos, con ejemplos ejecutables y su salida real |
| [Sintaxis (EBNF)](docs/referencia/sintaxis.md) | Gramática completa tal como la implementa el parser |
| [Tipos](docs/referencia/tipos.md) | Tipos, conversiones y reglas de compatibilidad |
| [Operadores](docs/referencia/operadores.md) | Operadores con su precedencia y tipos admitidos |
| [Builtins y métodos](docs/referencia/builtins.md) | Funciones y métodos integrados, lista exacta de `sema` |
| [Biblioteca estándar](docs/referencia/stdlib.md) | Módulos `mat`, `red` y `datos` |
| [Hilos y canales](docs/referencia/hilos.md) | `hilo`, `async`/`await`, `Canal[T]` y sus garantías |
| [Depuración](docs/referencia/depuracion.md) | `lgc -g` y comandos de lldb |
| [Herramientas](docs/referencia/herramientas.md) | `lgfmt`, `lglsp` y `lgpm` |
| [Palabras reservadas](docs/referencia/palabras-reservadas.md) | Palabras clave y nombres que no se pueden usar |
| [Errores comunes](docs/referencia/errores-comunes.md) | Mensajes del compilador y del runtime, con causa y solución |
| [Arquitectura](docs/arquitectura.md) | Pipeline, módulos y contratos entre ellos |
| [Limitaciones](docs/limitaciones.md) | Lo que no está implementado respecto a la especificación |
| [Instalación en Linux](docs/instalacion-linux.md) | Pasos para compilar y probar en Linux (no verificados en ejecución) |

## Requisitos

- **LLVM 17 o superior.** Se ha probado con LLVM 23.1.5 de Homebrew.
- **CMake 3.20 o superior** y, recomendado, **Ninja**.
- **Compilador C++17.** En macOS usa el clang de Homebrew (`/opt/homebrew/opt/llvm/bin/clang++`) para que coincida con la libc++ de LLVM.
- **`cc`** del sistema: `lgc` lo usa para enlazar el programa con el runtime y con `libm`.
- **`git`**: solo si usas dependencias git con `lgpm`.
- **GoogleTest** (opcional): si está instalado, CMake compila también `lg_unit_tests`.
- **lldb** (opcional): para la prueba de depuración. Sin lldb, esa prueba se omite.

## Compilación

En macOS con Homebrew:

```bash
cmake -B build -G Ninja \
  -DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path) \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_CXX_FLAGS=-stdlib=libc++ \
  -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm
cmake --build build
```

En otros sistemas basta con indicar el compilador de LLVM y `LLVM_DIR`, por ejemplo:

```bash
cmake -B build -G Ninja -DLLVM_DIR=/ruta/a/llvm/lib/cmake/llvm
cmake --build build
```

**Linux**: los pasos (paquetes, LLVM 23 desde apt.llvm.org, `CMAKE_C_COMPILER=clang-23` y `LLVM_DIR=/usr/lib/llvm-23/lib/cmake/llvm`) están en [docs/instalacion-linux.md](docs/instalacion-linux.md). Esa guía no se ha ejecutado en un Linux real; lo que sí se ha comprobado está marcado en ella.

Tras compilar quedan estos artefactos:

- `build/bin/lgc`, `build/bin/lgfmt`, `build/bin/lglsp` y `build/bin/lgpm`: las herramientas.
- `build/lib/liblg_runtime.a`: el runtime. Su ruta absoluta se graba en `lgc` durante la compilación, así que no muevas la carpeta `build` después de compilar.

## Primer programa

Crea un archivo `hola.lg`:

```lg
print("Hola, mundo")
```

```salida
Hola, mundo
```

Compílalo y ejecútalo:

```bash
./build/bin/lgc hola.lg -o hola
./hola
```

`lgc archivo.lg -o salida` produce un ejecutable llamado `salida`. Sin `-o`, el nombre es el del archivo sin extensión. Otras opciones:

| Opción | Efecto |
|---|---|
| `-O0` ... `-O3` | Nivel de optimización de LLVM (por defecto `-O0`) |
| `-I ruta` | Añade una carpeta de búsqueda para los `import` (se puede repetir) |
| `-g` | Añade información de depuración. En macOS genera `<salida>.dSYM` con `dsymutil` |
| `--emit-llvm` | Escribe el IR textual en `<salida>.ll` y no enlaza |
| `--keep-obj` | Conserva el objeto `<salida>.o` |
| `-h`, `--help` | Muestra el uso |

Los `import` buscan los módulos en la carpeta del archivo, en `LG_PATH`, en las rutas de `-I` y en la biblioteca estándar, en ese orden. Ver [módulos e import](docs/guia/09-modulos-e-import.md).

Si hay errores de compilación, `lgc` los imprime en stderr con archivo, línea, columna y una sugerencia, y termina con código 1.

Para depurar con lldb: `./build/bin/lgc programa.lg -g -o programa` y después `xcrun lldb ./programa`. Ver [depuración](docs/referencia/depuracion.md).

## Pruebas

Desde la raíz del repositorio, tras compilar:

```bash
ctest --test-dir build
```

`ctest` ejecuta estas 8 pruebas:

| Prueba | Qué comprueba |
|---|---|
| `unit` | `build/lg_unit_tests` (GoogleTest): lexer, parser y sema. Solo si GoogleTest estaba instalado al configurar. |
| `e2e` | `tests/e2e/run_e2e.sh`: compila y ejecuta cada `tests/e2e/*.lg` y compara su salida o su error. |
| `debug_lldb` | Breakpoints y variables con lldb sobre `tests/v2/debug/factorial_dbg.lg`. Se omite si no hay lldb. |
| `adversarial` | Casos adversariales de `tests/adversarial/run_adversarial.py`. |
| `lgfmt` | Formateador: `tests/v2/fmt/run_fmt_tests.sh`. |
| `lsp_json` y `lsp_protocol` | Servidor de lenguaje: JSON y protocolo (`tests/v2/lsp/`). |
| `pkg` | Gestor de paquetes: `tests/v2/pkg/run_pkg_test.sh`, sin red. |

También puedes lanzar cada parte por separado:

```bash
./build/lg_unit_tests
tests/e2e/run_e2e.sh ./build/bin/lgc
```

Cada caso de `tests/e2e` tiene uno de estos archivos de expectativa:

- `NOMBRE.expected`: la salida estándar debe coincidir exactamente.
- `NOMBRE.error`: la compilación debe fallar y su stderr debe contener el texto.
- `NOMBRE.rterror`: el programa debe terminar con error y su stderr debe contener el texto.

Los módulos auxiliares (por ejemplo `tests/e2e/modulos/mates.lg`) están en subcarpetas y no se ejecutan por sí mismos.

La prueba de ThreadSanitizer no forma parte de `ctest`: se ejecuta a mano, ver [hilos](docs/referencia/hilos.md#verificación).

## Siguientes pasos

Empieza por la [guía de primeros pasos](docs/guia/01-primeros-pasos.md). Si ya conoces el lenguaje, la [referencia de sintaxis](docs/referencia/sintaxis.md) y la de [builtins](docs/referencia/builtins.md) contienen todo lo necesario.
