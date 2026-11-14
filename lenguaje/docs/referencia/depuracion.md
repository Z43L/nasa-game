# Depuración

`lgc -g` añade información de depuración DWARF al programa: lldb (o gdb) puede poner breakpoints
por línea del archivo `.lg`, parar ahí y leer las variables locales y los parámetros.

## Compilar con -g

```bash
lgc programa.lg -g -o programa
```

Sin `-g` el ejecutable y el IR (`--emit-llvm`) son los de siempre: no hay metadatos de depuración.

### Dónde queda la información

- **macOS**: el DWARF se escribe en el objeto `.o`, y `lgc` lo copia con `dsymutil` a una carpeta
  `programa.dSYM` junto al ejecutable. Esa copia es la que lldb lee; no hace falta ninguna opción
  extra (`--keep-obj` ya no es necesario). Si `dsymutil` falla, `lgc` avisa en stderr y el ejecutable
  se genera igualmente, pero sin información de depuración. Si copias el ejecutable a otra carpeta,
  copia también su `.dSYM`.
- **Linux**: el DWARF va dentro del propio ejecutable (ELF). No se genera ninguna carpeta extra.
  Linux no está verificado en ejecución (ver [limitaciones](../limitaciones.md)).

Para depurar, usa `-O0` (el valor por defecto). Con `-O2` el módulo compila con `-g` (comprobado),
pero no se ha verificado que lldb muestre bien las variables optimizadas.

## Comandos de lldb

Ejemplo sobre `tests/v2/debug/factorial_dbg.lg` (salida real, abreviada):

```text
$ lgc factorial_dbg.lg -g -o fact_g
$ xcrun lldb -b -o "breakpoint set -f factorial_dbg.lg -l 5" -o run -o "frame variable" -o continue -o "frame variable" ./fact_g
(lldb) breakpoint set -f factorial_dbg.lg -l 5
Breakpoint 1: where = fact_g`u_factorial + 76 at factorial_dbg.lg:5:15, address = 0x0000000100000634
(lldb) run
    frame #0: 0x0000000100000634 fact_g`u_factorial(n=5) at factorial_dbg.lg:5:15
   5   	        acc = acc * i
(lldb) frame variable
(long) n = 5
(long) acc = 1
(long) i = 2
(lldb) continue
(lldb) frame variable
(long) n = 5
(long) acc = 2
(long) i = 3
```

Otros comandos verificados con el mismo tipo de programa (struct `Punto` con método `norma2`, una
función que devuelve `Option[int]` y un módulo importado `mates.lg`):

| Comando | Qué hace | Salida real |
|---|---|---|
| `breakpoint set -f archivo.lg -l N` | breakpoint en la línea N | `Breakpoint 1: where = ... at archivo.lg:N:C` |
| `breakpoint set -f mates.lg -l 2` (módulo importado) | breakpoint en un archivo importado | `where = imp_g`u_doble + 28 at mates.lg:2:12` |
| `frame variable` | parámetros y variables locales del marco actual | `(Punto *) self = 0x0000000100569170` |
| `frame variable *p` | struct desreferenciado | `(Punto) *p = (x = 3, y = 4, activo = true)` |
| `frame variable *o` | `Option[int]` | `(Option[int]) *o = (etiqueta = 1, valor = 3)` |
| `expression -- self.x` | expresión sobre una variable | `(double) $0 = 3` |
| `bt` | pila de llamadas con líneas del `.lg` | `frame #1: ... main at import_modulo.lg:3:7` |

Los enteros aparecen como `(long)` porque lldb usa su nombre interno para enteros de 64 bits con signo;
el tipo DWARF se llama `int`. Los `float` aparecen como `(double)`, por la misma razón.

Un breakpoint en la línea de declaración de una función (`fn ...:`) se resuelve en el prólogo, antes
de que lleguen los parámetros: para ver los parámetros, pon el breakpoint en la primera sentencia.

## Prueba automática

`tests/v2/debug/run_debug_tests.sh <lgc>` compila `factorial_dbg.lg` con `-g`, usa lldb en
modo por lotes y comprueba: el breakpoint de la línea 5 se resuelve y para en ella, las variables `n`,
`acc` e `i` tienen los valores esperados en dos pasadas del bucle, y sin `-g` el mismo breakpoint no
tiene ubicación. Si no hay lldb, sale con código 77 y ctest lo cuenta como skip (`debug_lldb`).

## Qué se describe y qué no

Se emite:

- **Archivos**: un `DIFile` por archivo fuente (el principal y cada import), con ruta absoluta.
- **Funciones**: un `DISubprogram` por función, método (con nombre `Tipo.metodo`), función anidada o
  clausura y por `main`. La línea de cada función es la de su declaración.
- **Líneas**: cada sentencia y cada expresión tiene su `DILocation` (línea y columna). Las sentencias
  de `main` que vienen de un import usan el archivo del import.
- **Variables**: parámetros (incluido `self`) y variables locales `mut` o no, con la línea de su
  declaración. No hay bloques léxicos: lldb muestra las variables de toda la función, también antes de
  su declaración, con valor 0 o basura (comprobado: en la línea 2 de `factorial_dbg.lg`, `acc = 0`).
- **Tipos**:
  - `int`, `float` y `bool` son tipos básicos (`DIBasicType`).
  - Un `struct` es un tipo compuesto con su nombre y sus campos, con su desplazamiento en memoria.
  - `Option[T]` es un tipo compuesto con `etiqueta` y `valor`.
  - Una clausura `fn(...) -> R` es un tipo compuesto con su nombre y sin campos.
  - `str`, `List[T]`, `Dict[K, V]` y `Tensor` son punteros a un tipo compuesto opaco con su nombre.

## Limitaciones

- **Tipos opacos**: `str`, `List`, `Dict` y `Tensor` no muestran su contenido en el depurador. Las
  clausuras tampoco muestran sus capturas.
- **Referencias recursivas**: si un tipo se contiene a sí mismo (por ejemplo, un struct con un
  `Option` de su propio tipo), esa referencia aparece como `void *`.
- **Nombres**: lldb muestra `(long)` en lugar de `int`. En la pila aparece el símbolo del enlazador
  (`u_factorial`, `u_Punto_norma2`); el nombre del lenguaje (`Punto.norma2`) va en la información de la
  función.
- **Variables capturadas**: una función anidada no declara las variables que captura del ámbito
  exterior. Las variables propias sí aparecen.
- **Optimización**: no verificada más allá de compilar con `-O2 -g`.
- **Solo lldb verificado**: la prueba automática usa lldb (macOS, arm64, LLVM 23). gdb y Linux no se han probado.
- **Ruta de los archivos**: las rutas se guardan en absoluto; si mueves el código después de compilar,
  el depurador no encontrará el fuente.
