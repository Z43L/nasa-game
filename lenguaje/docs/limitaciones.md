# Limitaciones y pendiente

Esta página lista lo que el lenguaje **no** implementa hoy, respecto a lo que se espera de un lenguaje de este tipo y a la especificación del proyecto. Cada punto indica qué pasa realmente si lo intentas, para que no tengas que adivinarlo.

No hay en el repositorio un documento de especificación formal contra el que comparar punto por punto. La lista se basa en las funciones que el proyecto anuncia como pendientes y en lo que el compilador hace al probar construcciones habituales. Si la especificación define algo que aquí no aparece, consideralo también pendiente hasta que se documente lo contrario.

Lo que sí está implementado (indexación y asignación de tensores `t[i, j]`, operaciones entre escalares y tensores, recursión con control de desbordamiento de pila y límites de anidamiento) se documenta en sus capítulos y en la referencia, no aquí.

## Tipos y valores

### Funciones anidadas

Las funciones anidadas están implementadas (ver [tipos](referencia/tipos.md#funciones-anidadas-y-clausuras)). Quedan fuera:

- No hay funciones anónimas (lambdas): una función anidada siempre tiene nombre y se declara como sentencia.
- Una función anidada solo se ve después de su declaración. No hay recursión mutua entre dos funciones anidadas que se declaran una después de otra.
- No hay sobrecarga ni valores por defecto en los parámetros.

### Funciones como valor

Solo las funciones anidadas son valores, de tipo `fn(...) -> R`. Una función declarada al nivel principal no se puede asignar a una variable ni pasar como argumento: `h = sq` da error. Llamarla sí funciona.

### Otros tipos

No hay tuplas, conjuntos, enumeraciones, tipos de punto flotante de 32 bits, enteros sin signo ni tipos genéricos definidos por el usuario. Los únicos constructores de tipo son `List[T]`, `Dict[K, V]`, `Tensor[E, forma]`, `Option[T]` y `fn(...) -> R`.

## Cadenas

- No se pueden indexar (`s[0]`) ni trocear. Error: `no se puede indexar un valor de tipo str`.
- No tienen métodos: no hay `upper`, `split`, `replace` ni equivalentes.
- No admiten `in` ni búsqueda de subcadenas.
- Las comparaciones de orden no están implementadas. El analizador las acepta, pero el generador de código falla:

```lg
print("a" < "b")
```

```error
programa.lg:1:11: error: operador no soportado para el tipo str
   1 | print("a" < "b")
     |           ^
```

Por eso `==` y `!=` sí funcionan, pero `<`, `>`, `<=` y `>=` no.

- `str(x)` solo admite escalares en el generador. `str([1, 2])` falla:

```lg
print(str([1, 2]))
```

```error
programa.lg:1:7: error: str() no aplicable a List[int]
   1 | print(str([1, 2]))
     |       ^
```

## Colecciones

- Listas: solo `append`, `pop` y `len`. No hay `insert`, `remove`, `sort`, `copy`, `reverse` ni rebanadas `l[a:b]`.
- Diccionarios: solo `tiene` y `len`. No hay `keys()`, `values()`, `get()` ni `remove`. No se puede recorrer un diccionario con `for`:

```lg
d = {"a": 1}
print(d.keys())
```

```error
programa.lg:2:7: error: el tipo Dict[str, int] no tiene el método 'keys'
   2 | print(d.keys())
     |       ^
sugerencia: revisa el nombre del método
```

- Las claves de diccionario solo pueden ser `int`, `str` o `bool`.
- No hay copia integrada: asignar una lista o un diccionario comparte el objeto (capítulo 12).

## Operadores y sintaxis

- No existen operadores compuestos (`+=`, `-=`), ni potencia `**`, ni operadores bit a bit.

```lg
mut x = 1
x += 1
```

```error
programa.lg:2:4: error: se esperaba una expresión, pero se encontró '='
   2 | x += 1
     |    ^
```

- No existe `else if`. Usa `elif`:

```lg
x = 1
if x == 2:
    print(2)
else if x == 1:
    print(1)
```

```error
programa.lg:4:6: error: se esperaba ':' después de 'else'
   4 | else if x == 1:
     |      ^
sugerencia: añade ':' al final de la línea
programa.lg:4:6: error: se esperaba un bloque indentado después de ':'
   4 | else if x == 1:
     |      ^
sugerencia: escribe las sentencias en las líneas siguientes, con más indentación
```

- No existen `try`, excepciones, `match`, `with` ni `yield`.
- No hay argumentos con nombre en funciones ni métodos. El parser los lee, pero el analizador los rechaza:

```lg
fn resta(a: int, b: int) -> int:
    return a - b

print(resta(b: 1, a: 5))
```

```error
programa.lg:4:16: error: la función 'resta' no admite argumentos con nombre
   4 | print(resta(b: 1, a: 5))
     |                ^
sugerencia: quita el nombre del argumento
```

Los argumentos con nombre solo funcionan en la construcción de structs.

- No hay valores por defecto en parámetros ni en campos de struct.
- No hay funciones variádicas: no puedes declarar una función como `printf` que acepte un número variable de argumentos.

## Funciones y módulos

- Sin genéricos: una función trabaja con tipos fijos.
- Sin sobrecarga: no puedes tener dos funciones con el mismo nombre y distinta firma.
- Los imports no admiten `..`, ni `from ... import ...`, ni `as`. Hay espacios de nombres por módulo con alias igual al último segmento de la ruta (capítulo 9).
- Las variables de nivel superior de un módulo no se exportan. Una función puede leer y asignar las variables globales `mut` de su propio archivo (ver [sintaxis](referencia/sintaxis.md#variables-globales)). Las inmutables de nivel superior no son visibles desde funciones (`la función no puede usar la variable global`): pásalas como parámetro.
- Una función lanzada con `hilo` o `async` no puede usar una global `mut`, ni directamente ni a través de las funciones y métodos que llama: es error de compilación (ver [hilos](referencia/hilos.md#globales-mut-y-hilos)). La comprobación no sigue las llamadas hechas por valor (`f(x)` con `f` una variable de tipo `fn(...)`), así que una función que llame a través de una clausura guardada en una variable puede usar la global sin que el compilador lo avise. Las globales `mut` no tienen cerrojos, así que no son seguras entre hilos: por eso el compilador impide usarlas desde funciones lanzadas.
- Una global `mut` vale cero (`0`, `false`, `""`) o `null` hasta que su declaración se ejecuta en el orden del programa. Una función que la lea antes de esa línea ve ese valor inicial; con una lista o un diccionario, usarla antes de inicializarla no es válido.
- Un parámetro no se puede mutar en su campo (`e.rutas.append(...)` falla con `variable inmutable`). Solución: `mut r = e.rutas` y luego `r.append(...)`, porque las listas se comparten por referencia.
- No hay forma de crear un byte arbitrario (no hay `chr` ni `ord`).
- Los imports solo se permiten al nivel principal.

## Structs

- Sin herencia, interfaces ni traits.
- Sin constructores con cuerpo; solo la construcción por campos con nombre.
- Un campo puede tener el tipo de la propia struct (`siguiente: Nodo`) y el compilador lo acepta, pero no hay forma de construir el primer valor. Para estructuras recursivas usa `List[Nodo]`.

## Tensores

Los tensores tienen indexación, asignación y operaciones con escalares, pero les faltan operaciones habituales:

- Solo `zeros`, `ones`, `randn` y `from_list` crean tensores. `zeros`, `ones` y `randn` producen siempre `float`; no hay creación de tensores `int` salvo con `from_list` a partir de una lista de enteros.
- No hay `reshape`, `concat`, `stack`, ni rebanadas con rangos (`t[0:2]`).
- No hay reducciones (`suma`, `máximo`, `media`).
- No hay difusión (broadcasting): los operandos deben tener la misma forma, salvo los escalares.
- No hay división entre dos tensores (`a / b`).
- `@` solo multiplica tensores `float` de rango 2.
- No hay derivadas automáticas ni entrenamiento.
- No hay lectura ni escritura de tensores en archivos.

Un ejemplo de una operación no soportada:

```lg
a: Tensor[float, (2, 2)] = ones(2, 2)
b: Tensor[float, (2, 2)] = ones(2, 2)
print(a / b)
```

```error
programa.lg:3:9: error: operador '/' no admite Tensor[float, (2, 2)] y Tensor[float, (2, 2)]
   3 | print(a / b)
     |         ^
```

## GPU

Fuera de alcance: no hay soporte para GPU ni otros aceleradores. Los tensores viven en la memoria de la CPU y las operaciones se ejecutan en el runtime en C. No hay forma de elegir dispositivo ni de transferir datos.

## Entrada y salida

- No hay lectura desde la entrada estándar: `input()` no existe y el error es `función no definida 'input'`.
- No hay argumentos de línea de comandos para el programa.
- Los archivos solo se leen y escriben con la biblioteca `datos` (texto completo y CSV numérico, ver [biblioteca estándar](referencia/stdlib.md#datos)). No hay API de archivos general ni acceso a la red, salvo las funciones C que puedas declarar con `extern`.
- `print` escribe siempre en la salida estándar.

## Interop con C

- `extern "C"` solo acepta la ABI `"C"`.
- Las bibliotecas enlazadas son las de `libc` y `libm`. `lgc` no tiene una opción para enlazar bibliotecas adicionales.
- Los tipos compuestos (`str`, `List`, `Dict`, `Tensor`, structs) no son seguros en una llamada `extern`: se pasa el puntero al objeto del runtime, no los datos que C espera.

## Memoria

- Las referencias fuertes que forman un ciclo no se liberan: un objeto que se contiene a sí mismo queda en memoria hasta terminar el proceso. Para romper el ciclo, guarda una de las referencias como `Debil[T]` (capítulo 12). No hay recolector de ciclos: es responsabilidad del programador.
- Una clausura guardada en una variable `mut` que ella misma captura forma un ciclo: la clausura contiene la celda de la variable y la celda contiene la clausura. Ejemplo: si `g` lee una variable `mut h` y después se hace `h = g`, la clausura `g` y la celda no se liberan hasta que termina el proceso.

## Herramientas

- `lgc` no emite avisos (warnings): solo errores.
- No hay generación de documentación a partir del código.
- **`lgfmt`** no conserva los comentarios de cola (`# ...` al final de línea) dentro de listas que ocupan varias líneas. Según el informe de la herramienta, esos comentarios se pierden al formatear.
- **`lglsp`** no implementa `textDocument/references` ni `textDocument/rename`. Tampoco hay formateo ni símbolos del documento desde el editor. Los métodos soportados están en [herramientas](referencia/herramientas.md#lglsp).
- **`lgpm`** no resuelve dependencias transitivas: una biblioteca que importa a otra obliga a declarar también la segunda en el `lg.toml` del proyecto. No hay registro central ni resolución de versiones.

## Plataformas y pruebas

- **Windows no soportado.** El runtime usa pthread y el enlazado usa `cc`; no hay port a Windows.
- **Linux no verificado en ejecución.** La guía [instalación en Linux](instalacion-linux.md) describe los pasos, pero no se han ejecutado en un Linux real. Todo lo verificado está hecho en macOS (arm64, LLVM 23).
- **ThreadSanitizer no integrado en CTest.** Se ejecuta a mano, enlazando el objeto con el runtime compilado con `-fsanitize=thread` (ver [hilos](referencia/hilos.md#verificación)). `ctest` no lo ejecuta.

## Resumen

| Área | Estado |
|---|---|
| Tipos escalares, listas, diccionarios, structs, métodos | implementados, con las limitaciones de esta página |
| Tensores: creación, `@`, operaciones elemento a elemento y con escalares, indexación y asignación | implementados |
| Lambdas (funciones anónimas) y funciones de nivel superior como valor | no implementado |
| Comparaciones de orden sobre `str` | no implementado (el generador falla) |
| Reducciones, `reshape`, rebanadas, difusión de tensores | no implementado |
| GPU | fuera de alcance |
| Windows | no soportado |
| Linux | no verificado en ejecución |
| Entrada estándar y argumentos | no implementado |
| Formateo de comentarios de cola en listas multilínea (`lgfmt`) | limitación conocida |
| `references` y `rename` (`lglsp`) | no implementado |
| Dependencias transitivas (`lgpm`) | no implementado |
