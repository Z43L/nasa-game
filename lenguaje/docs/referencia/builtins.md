# Builtins y métodos integrados

Esta página lista todas las funciones y métodos que el analizador semántico (`src/sema/sema.cpp`) reconoce como integrados, y lo que genera el compilador para cada uno. Cualquier otra llamada que no sea una función del usuario, una struct o un método de struct es un error de compilación.

## Funciones integradas

| Función | Firma | Devuelve | Notas |
|---|---|---|---|
| `print` | `print(a, b, ...)` | `void` | separa con espacio y termina con salto de línea; sin argumentos imprime una línea vacía |
| `len` | `len(x)` con `x` `List`, `Dict`, `str` o `Tensor` | `int` | en `str` cuenta bytes; en `Tensor` devuelve la primera dimensión |
| `int` | `int(x)` con `x` `int`, `float`, `bool` o `str` | `int` | `float` se trunca; `float` fuera de rango o `inf`/`nan` falla en ejecución; `str` falla si no es un entero |
| `float` | `float(x)` con `x` `int`, `float` o `str` | `float` | `str` falla en ejecución si no es un número |
| `str` | `str(x)` | `str` | en el generador de código acepta `int`, `float`, `bool` y `str` |
| `zeros` | `zeros(d1, d2, ...)` | `Tensor[float, forma]` | todo a cero |
| `ones` | `ones(d1, d2, ...)` | `Tensor[float, forma]` | todo a uno |
| `randn` | `randn(d1, d2, ...)` | `Tensor[float, forma]` | normal estándar, semilla por defecto 42 |
| `from_list` | `from_list(lista, d1, d2, ...)` | `Tensor[E, forma]` | `lista` es `List[int]` o `List[float]`; `E` es el de la lista |
| `Some` | `Some(x)` | `Option[T]` | `T` es el tipo de `x`; con un contexto `Option[T]` (anotación, parámetro, `return`), `x` se comprueba contra ese `T`. Para `None` no hay función: se escribe `None` |

Reglas comunes:

- Las dimensiones son argumentos enteros posicionales. Un literal fija la dimensión en compilación; cualquier otra expresión `int` la deja desconocida y se comprueba en ejecución.
- `zeros`, `ones` y `randn` necesitan al menos una dimensión.
- Una dimensión negativa es un error de ejecución: `dimensión negativa (-1) en la forma del tensor`.
- Los argumentos con nombre no se admiten en ninguna función integrada.

### Ejemplos verificados

```lg
print(len("abc"), len([1, 2]), len({"a": 1}))
print(int("42") + 1)
print(float(3))
print(str(2.5))
print(zeros(2, 3).forma())
print(ones(2).numel())
print(from_list([1, 2, 3, 4], 2, 2))
```

```salida
3 2 1
43
3.0
2.5
[2, 3]
2
Tensor[int, (2, 2)]
[[1, 2], [3, 4]]

```

### Semilla de randn

`randn` usa un generador propio. Sin configuración, la semilla es `42` y la secuencia es siempre la misma. La variable de entorno `LG_SEED` (un entero decimal) cambia la semilla:

```bash
LG_SEED=7 ./programa
```

Si `LG_SEED` no es un entero válido, se usa la semilla por defecto.

## Métodos integrados

Los métodos se llaman con la sintaxis `objeto.metodo(argumentos)`. Solo existen para los tipos indicados.

### List[T]

| Método | Firma | Devuelve | Requiere `mut` |
|---|---|---|---|
| `append` | `lista.append(x)` | `void` | sí |
| `pop` | `lista.pop()` | `T` | sí |
| `len` | `lista.len()` | `int` | no |

- `append` exige que `x` sea compatible con `T`.
- `pop` quita y devuelve el último elemento. Sobre una lista vacía falla en ejecución: `pop de una lista vacía`.

### Dict[K, V]

| Método | Firma | Devuelve | Requiere `mut` |
|---|---|---|---|
| `tiene` | `d.tiene(k)` | `bool` | no |
| `len` | `d.len()` | `int` | no |

- `tiene` exige que `k` sea compatible con `K`.

### Tensor

| Método | Firma | Devuelve | Notas |
|---|---|---|---|
| `transpose` | `t.transpose()` | `Tensor` | solo rango 2; intercambia las dimensiones |
| `forma` | `t.forma()` | `List[int]` | las dimensiones en orden |
| `numel` | `t.numel()` | `int` | número total de elementos |

```lg
t: Tensor[float, (2, 3)] = zeros(2, 3)
print(t.transpose().forma())
print(t.numel())
```

```salida
[3, 2]
6
```

### Option[T]

| Método | Firma | Devuelve | Notas |
|---|---|---|---|
| `es_some` | `opt.es_some()` | `bool` | `true` si el valor es `Some` |
| `es_none` | `opt.es_none()` | `bool` | `true` si el valor es `None` |
| `desempacar_o` | `opt.desempacar_o(valor)` | `T` | el valor de `Some`, o `valor` si es `None`; `valor` es de tipo `T` y se evalúa siempre |

No hay método para leer el valor sin dar un por defecto. Ejemplo verificado:

```lg
o: Option[str] = Some("hola")
n: Option[str] = None
print(o.es_some(), n.es_none())
print(o.desempacar_o("otro"), n.desempacar_o("otro"))
```

```salida
true true
hola otro
```

Más ejemplos en [tipos](tipos.md#option-t).

### Métodos sobre structs

Las structs no tienen métodos integrados. Sus métodos son los que declaras en el bloque `impl`. Ver [capítulo 7](../guia/07-structs-e-impl.md).

## Nombres reservados

Los nombres de las funciones integradas (`print`, `len`, `int`, `float`, `str`, `zeros`, `ones`, `randn`, `from_list`, `Some`), `bool`, `None` y `Option` no se pueden usar como nombre de función, struct o variable. Ver [palabras reservadas](palabras-reservadas.md).

## Lo que no hay

Estos nombres no existen como builtins; usarlos da error de compilación:

- `bool(x)`, `list(...)`, `dict(...)`, `input(...)`, `range(...)`.
- `keys()`, `values()`, `get()`, `insert()`, `remove()`, `sort()`, `copy()` sobre listas o diccionarios.
- Métodos de `str` (`upper`, `split`, `replace`, ...).
- `unwrap()` o un acceso al valor de un `Option` sin dar valor por defecto: usa `desempacar_o`.
- `abs`, `min`, `max`, `sum` y otras funciones matemáticas: para ellas usa `extern "C"` (capítulo 10).

Para recorrer un diccionario, guarda sus claves en una lista; para copiar una lista, usa un bucle `for` que añada los elementos a una nueva.

## Errores comunes

| Mensaje | Causa |
|---|---|
| `'zeros' necesita al menos una dimensión` | `zeros()` sin argumentos |
| `las dimensiones deben ser int, recibido List[int]` | se pasa una lista a `zeros` |
| `len no admite int` | `len` sobre un tipo que no es `List`, `Dict`, `str` o `Tensor` |
| `'tiene' espera 1 argumento(s), recibió 0` | número de argumentos incorrecto en un método |
| `el tipo List[int] no tiene el método 'push'` | método inexistente |
| `transpose requiere un tensor de rango 2` | `transpose` sobre un tensor de tres o más dimensiones |
| `no se puede saber el tipo de None` | `None` sin contexto de tipo `Option` |
| `Some() no admite valores void` | `Some(f())` con `f` sin retorno |
| `tipo esperado Option[int], recibido int` | se asigna un valor a un `Option` sin envolverlo; la sugerencia es `Some(x)` |
| `from_list: la lista tiene 3 elementos pero la forma requiere 4` | número de elementos distinto del producto de las dimensiones |
