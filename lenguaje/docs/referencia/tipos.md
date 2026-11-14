# Referencia de tipos

## Lista de tipos

| Tipo | Escritura | Representación | Valores |
|---|---|---|---|
| `int` | `int` | entero con signo de 64 bits | `-9223372036854775808` a `9223372036854775807` |
| `float` | `float` | doble precisión IEEE 754 | decimales, `inf`, `-inf`, `nan` |
| `bool` | `bool` | booleano | `true`, `false` |
| `str` | `str` | cadena UTF-8 inmutable | secuencia de bytes |
| `List[T]` | `List[T]` | lista mutable de elementos de tipo `T` | |
| `Dict[K, V]` | `Dict[K, V]` | diccionario mutable, orden de inserción | `K` es `int`, `str` o `bool` |
| `Tensor[E, (d1, ...)]` | `Tensor[E, (d1, d2)]` | array denso de forma fija | `E` es `int` o `float` |
| struct | nombre de la struct | objeto con campos nombrados | |
| `Option[T]` | `Option[T]` | caja en el heap con etiqueta y valor | `Some(x)` o `None` |
| `Hilo[T]` | `Hilo[T]` | objeto del runtime: un hilo del sistema con su resultado | producido por `hilo f(...)` o por llamar a una `async fn` |
| `Canal[T]` | `Canal[T]` | cola de mensajes entre hilos | creado con `Canal[T]()` |
| `Debil[T]` | `Debil[T]` | caja que observa un valor heap sin poseerlo | creado con `debil(x)` |
| función anidada | `fn(A, B) -> R` (sin `->`, `void`) | clausura en el heap: función y capturas | solo como valor de una función anidada (ver [clausuras](#funciones-anidadas-y-clausuras)) |

`void` no se escribe en el código: es el tipo de una función sin `->` y de cualquier valor que no devuelve nada. No se puede asignar, imprimir ni usar en una operación.

Los tipos `int`, `float` y `bool` son valores directos. `str`, `List`, `Dict`, `Tensor`, las structs, `Option`, las funciones anidadas, `Hilo`, `Canal` y `Debil` viven en el heap y se gestionan por conteo de referencias (capítulo 12).

## Hilo[T], Canal[T] y Debil[T]

`Hilo[T]` y `Canal[T]` se explican en [hilos](hilos.md). `Debil[T]` es una referencia que no mantiene vivo el valor:

```lg
struct Caja:
    v: int

mut x = Caja(v: 5)
d = debil(x)
print(d.fuerte().es_some())   # true: x sigue viva
x = Caja(v: 6)                # la caja antigua se libera
print(d.fuerte().es_none())   # true: la débil devuelve None
```

```salida
true
true
```

- `debil(x)` acepta cualquier valor heap: `str`, `List`, `Dict`, `Tensor`, struct, `Option` o función. Con `int`, `float` o `bool` es error.
- `d.fuerte()` devuelve `Option[T]`: `Some(x)` si el valor aún existe, `None` si ya se liberó. El `Some` es una referencia fuerte nueva: mientras la tengas, el valor sigue vivo.
- Una débil no impide la liberación. Úsala para romper ciclos: el padre guarda hijos con referencias fuertes y el hijo guarda una `Debil` hacia el padre (ver el ejemplo de [memoria](../guia/12-memoria-conteo-de-referencias.md)).

## Reglas de compatibilidad

- No hay conversiones implícitas. Un `int` no se usa donde se espera `float`, y viceversa. Usa `float(x)` o `int(x)`.
- Dos tipos son compatibles si son el mismo tipo. Los tipos compuestos se comparan por sus componentes: `List[int]` es distinto de `List[float]`.
- Un tensor con dimensiones desconocidas en compilación (las que vienen de variables) es compatible con cualquier forma en esa posición. Las dimensiones conocidas deben coincidir.
- No hay subtipos ni genéricos definidos por el usuario: `List[T]`, `Dict[K, V]`, `Tensor[E, forma]`, `Option[T]` y `fn(...) -> R` son los únicos constructores de tipos.

## Inferencia

- Un literal entero es `int`; un literal con punto o exponente es `float`; `true` y `false` son `bool`; una cadena es `str`.
- Una lista literal tiene el tipo de sus elementos, que deben ser todos compatibles entre sí: `[1, 2]` es `List[int]`.
- Una lista o un diccionario vacíos necesitan un tipo de contexto, es decir, una anotación de variable, un parámetro o un campo de struct: `r: List[int] = []`.
- Un diccionario literal tiene como tipo el de sus claves y valores. Las claves deben ser `int`, `str` o `bool`.
- `zeros`, `ones` y `randn` devuelven siempre `Tensor[float, ...]`. `from_list` devuelve un tensor cuyo elemento es el de su lista (`int` o `float`).
- Las dimensiones de un tensor se conocen en compilación cuando son literales. Si son variables, el tipo tiene dimensión desconocida y se comprueba en ejecución.
- `Some(x)` tiene tipo `Option[T]`, con `T` el tipo de `x`. `None` no tiene tipo por sí mismo: toma el de su contexto (una anotación, un parámetro, un campo o un `return`). Sin contexto es un error.

## Option[T]

`Option[T]` representa un valor que puede faltar, sin nulos. Un valor es `Some(x)` o `None`:

```lg
a: Option[int] = Some(5)
b: Option[int] = None
print(a)
print(b)
print(a.es_some(), b.es_none())
print(b.desempacar_o(7), a.desempacar_o(7))
print(a == None, b == None)
s = Some("hola")
print(s)
print(s.desempacar_o("otro"))
```

```salida
Some(5)
None
true true
7 5
false true
Some("hola")
hola
```

- `es_some()` y `es_none()` devuelven `bool`.
- `desempacar_o(valor)` devuelve el valor de `Some`, o `valor` si es `None`. El valor por defecto debe ser de tipo `T` y se evalúa siempre.
- No existe un desempaquetado sin comprobar: no hay forma de leer el valor de un `Option` sin dar un valor por defecto. Así no hay excepciones ni errores de ejecución por leer un `None`.
- `opt == None` y `opt != None` están permitidos. Comparar dos `Option` entre sí (`a == b`) es un error.
- `Some(x)` con un tipo de contexto `Option[T]` comprueba `x` contra `T`: `Option[float]` no acepta `Some(1)`, hay que escribir `Some(1.0)` o `Some(float(1))`. Sin contexto, el tipo es el de `x`.
- Un `Option` que contiene un valor heap (una `str`, una `List`, una struct...) lo gestiona el conteo de referencias como cualquier otro valor heap.

Un `int` no se convierte en `Option[int]` de forma implícita: `x: Option[int] = 3` es un error, y el compilador sugiere `Some(x)`.

## Funciones anidadas y clausuras

Una función declarada dentro de otra es un valor de tipo `fn(A, B) -> R`. Puede usar las variables de la función que la contiene, incluso después de que esta haya terminado:

```lg
fn contador() -> fn() -> int:
    mut n = 0
    fn siguiente() -> int:
        n = n + 1
        return n
    return siguiente

c = contador()
print(c())
print(c())
d = contador()
print(d())
```

```salida
1
2
1
```

Cada llamada a `contador()` crea su propia variable `n`: `c` y `d` no se comparten.

Reglas de captura:

- Una variable **inmutable** se captura **por valor**: la función recibe una copia en el momento en que se declara. Como no cambia, es lo mismo que compartirla.
- Una variable **`mut`** se captura **por referencia**: la función y la función externa comparten la misma celda, así que cada cambio se ve en los dos lados.
- Una función anidada solo se ve dentro del bloque donde se declara. Fuera de él, su nombre no existe.
- Una variable declarada en un bloque que ya terminó no se puede usar desde una función declarada después: el compilador lo indica con `su ámbito ya terminó`.
- Una función anidada puede llamarse a sí misma (recursión) y puede contener otras funciones anidadas, con todos los niveles de captura que haga falta.
- Un método `mut self` puede usarse desde una función anidada que lo modifica, con la misma regla de `mut`.

```lg
fn externa(base: int) -> int:
    k = 10
    fn medio(x: int) -> int:
        fn interna(y: int) -> int:
            return y + x + k + base
        return interna(1)
    return medio(100)

print(externa(1000))
```

```salida
1111
```

Una función anidada que se pasa como argumento tiene el tipo `fn(...)` de su firma:

```lg
fn aplica(f: fn(int) -> int, v: int) -> int:
    return f(v)

fn principal() -> int:
    factor = 3
    fn triple(x: int) -> int:
        return x * factor
    return aplica(triple, 7)

print(principal())
```

```salida
21
```

Las funciones de nivel superior no son valores: `h = sq` es un error si `sq` está declarada al nivel principal. Llamarlas sí funciona.

## Conversiones

| Llamada | Acepta | Devuelve | Notas |
|---|---|---|---|
| `int(x)` | `int`, `float`, `bool`, `str` | `int` | `float` se trunca hacia cero; un `float` fuera de rango, `inf` o `nan` termina el programa; `str` debe contener un entero válido |
| `float(x)` | `int`, `float`, `str` | `float` | `str` debe contener un número válido |
| `str(x)` | `int`, `float`, `bool`, `str` | `str` | en el generador de código, solo estos tipos; ver [limitaciones](../limitaciones.md) |

Ejemplos verificados:

```lg
print(int(7.9))
print(int(-7.9))
print(float(3))
print(str(true))
```

```salida
7
-7
3.0
true
```

## Formato de impresión

`print` escribe cada tipo así:

| Tipo | Ejemplo de salida |
|---|---|
| `int` | `42`, `-7` |
| `float` | ver la tabla de abajo |
| `bool` | `true`, `false` |
| `str` | el texto tal cual, sin comillas; dentro de contenedores y structs, entre comillas |
| `List[T]` | `[1, 2, 3]`, con elementos separados por `, ` |
| `Dict[K, V]` | `{"ana": 30, "luis": 25}`, en orden de inserción |
| struct | `Punto(x: 3.0, y: 4.0)`, campos en orden de declaración |
| `Tensor` | ver el ejemplo de abajo |

### Formato de float

Un `float` se imprime con la representación más corta que lo identifica sin error. Si el exponente decimal está en el intervalo `[-4, 16)`, se usa notación fija con al menos un decimal; si no, notación científica.

```lg
print(0.1 + 0.2)
print(1.0 / 3.0)
print(0.0001)
print(0.00001)
print(1e16)
print(9999999999999998.0)
print(1.5e-7)
print(-1.0 / 0.0)
```

```salida
0.30000000000000004
0.3333333333333333
0.0001
1e-05
1e+16
9999999999999998.0
1.5e-07
-inf
```

`nan` aparece para resultados no definidos, como `sqrt(-1.0)`. `inf` y `-inf` son el resultado de dividir un `float` entre cero; los `float` no producen error de división por cero (sí lo hacen los `int`).

### Formato de tensor

Un tensor se imprime en dos o más líneas: la cabecera con el tipo y la forma, y los valores por filas. Como `print` añade un salto de línea al final, queda una línea vacía tras el tensor:

```lg
print(from_list([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 2, 3))
```

```salida
Tensor[float, (2, 3)]
[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]

```

Las formas de una dimensión se escriben con coma final: `Tensor[float, (3,)]`.

Un `Option` se imprime como `Some(valor)` o `None`, con el valor entre comillas si es `str` (como en los contenedores). Una función anidada no se puede imprimir.

## Nombres de tipo en los mensajes

El compilador escribe los tipos así: `int`, `float`, `bool`, `str`, `List[int]`, `Dict[str, int]`, `Option[int]`, `fn(int, str) -> bool`, `Tensor[float, (2, 3)]`, `Tensor[int, (2,)]` (con coma final en una dimensión) y el nombre de la struct. Una dimensión que el compilador no conoce aparece como `?`, por ejemplo `Tensor[float, (?, 3)]`.

## Ejemplo: tipos en una función

```lg
fn normaliza(valores: List[float]) -> List[float]:
    mut suma = 0.0
    for v in valores:
        suma = suma + v
    mut r: List[float] = []
    for v in valores:
        r.append(v / suma)
    return r

print(normaliza([1.0, 3.0]))
```

```salida
[0.25, 0.75]
```

## Ver también

- [Operadores](operadores.md): qué tipos admite cada operador.
- [Builtins](builtins.md): los tipos que acepta y devuelve cada función integrada.
