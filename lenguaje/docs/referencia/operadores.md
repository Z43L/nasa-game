# Referencia de operadores

## Precedencia

De menor a mayor precedencia. Los operadores binarios de una misma fila se asocian por la izquierda.

| Nivel | Operadores | Tipo | Asociatividad |
|---|---|---|---|
| 1 | `or` | binario | izquierda |
| 2 | `and` | binario | izquierda |
| 3 | `not` | prefijo | derecha |
| 4 | `==` `!=` `<` `>` `<=` `>=` `in` | binario | izquierda |
| 5 | `+` `-` | binario | izquierda |
| 6 | `*` `/` `%` `@` | binario | izquierda |
| 7 | `-` | prefijo (negación) | derecha |
| 8 | `(...)` llamada, `[...]` índice, `.campo` | postfijo | izquierda |
| 9 | literales, identificadores, `( expr )`, `[...]`, `{...}` | primario | |

Consecuencias prácticas:

- `not a == b` es `not (a == b)`.
- `-2 * 3` es `(-2) * 3`, y `2 * 3 + 4 * 5` es `26`.
- `a < b < c` es `(a < b) < c`. Como `a < b` es `bool`, el compilador lo rechaza. Para encadenar, usa `and`: `a < b and b < c`.
- `1 + 2 * 3 - 4 / 2` vale `5`.

No hay operador de potencia, de asignación compuesta (`+=`), ni de incremento.

## Tabla de operadores

| Operador | Operandos admitidos | Resultado | Notas |
|---|---|---|---|
| `or` | `bool`, `bool` | `bool` | cortocircuito: no evalúa el lado derecho si el izquierdo es `true` |
| `and` | `bool`, `bool` | `bool` | cortocircuito: no evalúa el lado derecho si el izquierdo es `false` |
| `not` | `bool` | `bool` | |
| `==` `!=` | `int`/`int`, `float`/`float`, `bool`/`bool`, `str`/`str`, struct/struct | `bool` | las structs necesitan método `eq(self, otro) -> bool` |
| `<` `>` `<=` `>=` | `int`/`int`, `float`/`float` | `bool` | ver nota sobre `str` |
| `in` | `K` `in` `Dict[K, V]`; `T` `in` `List[T]` | `bool` | el operando izquierdo debe ser compatible con la clave o el elemento |
| `+` | `int`/`int`, `float`/`float`, `str`/`str`, `Tensor`/`Tensor`, escalar/`Tensor`, `Tensor`/escalar | mismo tipo | `int` comprueba desbordamiento; `str` concatena; `Tensor` opera elemento a elemento; el escalar debe ser del tipo de los elementos |
| `-` (binario) | `int`/`int`, `float`/`float`, `Tensor`/`Tensor`, escalar/`Tensor`, `Tensor`/escalar | mismo tipo | `Tensor` elemento a elemento |
| `*` | `int`/`int`, `float`/`float`, `Tensor`/`Tensor`, escalar/`Tensor`, `Tensor`/escalar | mismo tipo | `Tensor` elemento a elemento |
| `/` | `int`/`int`, `float`/`float`, escalar/`Tensor`, `Tensor`/escalar | mismo tipo | `int` trunca hacia cero y falla con divisor 0; `Tensor`/`Tensor` no está soportado |
| `%` | `int`/`int` | `int` | sigue el signo del dividendo; falla con divisor 0 |
| `@` | `Tensor[float]` de rango 2 `@` `Tensor[float]` de rango 2 | `Tensor[float]` | `(m, k) @ (k, n) -> (m, n)` |
| `-` (prefijo) | `int`, `float` | mismo tipo | en `int` comprueba desbordamiento |
| `[i]` | `List[T]` con `int`; `Dict[K, V]` con `K`; `Tensor` con un `int` por dimensión | `T`, `V`, `Tensor` de menor rango o escalar | índices negativos cuentan desde el final; ver [tensores](../guia/08-tensores.md) |
| `.campo` | struct con ese campo | tipo del campo | |
| `.metodo(...)` | struct, `List`, `Dict`, `Tensor` | según el método | ver [builtins](builtins.md) |

Lo que **no** existe:

- Operaciones entre `int` y `float` sin convertir. Da el error `no se mezclan int y float`. Esto incluye un escalar `int` con un `Tensor[float]`.
- Comparaciones de listas, diccionarios, tensores o structs sin `eq`.
- `%` sobre `float`.
- `/` entre dos `Tensor`.
- Indexación de `str`.
- Varios índices sobre `List` o `Dict`: solo los tensores admiten `t[i, j]`.

## Igualdad

- `==` compara por valor para `int`, `float`, `bool` y `str`.
- Para structs, `==` llama al método `eq` que hayas definido. Sin `eq` es un error de compilación.
- `!=` es la negación de `==` para los tipos que lo admiten.
- `float` sigue la semántica IEEE: `nan` no es igual a sí mismo.

## Comparación con `str`

El analizador semántico acepta `<`, `>`, `<=` y `>=` sobre `str`, pero el generador de código no implementa esa comparación: el compilador falla con `operador no soportado para el tipo str`. Trátalo como no disponible hasta que se implemente; ver [limitaciones](../limitaciones.md).

## Aritmética entera

```lg
print(7 / 2)
print(-7 / 2)
print(7 % 3)
print(-7 % 3)
print(7 % -3)
```

```salida
3
-3
1
-1
1
```

La división trunca hacia cero, y el resto tiene el signo del dividendo, igual que en C. Un divisor cero termina el programa con `división por cero`.

## Desbordamiento

Las operaciones `+`, `-` (binaria y prefija), `*` y `/` sobre `int` comprueban que el resultado cabe en 64 bits. Si no cabe, el programa termina con `desbordamiento de entero en '<op>'`. Los `float` nunca desbordan a error: producen `inf`.

## Cortocircuito

`and` y `or` evalúan el lado derecho solo cuando hace falta:

```lg
fn lado(x: int) -> bool:
    print("lado", x)
    return x > 0

print(false and lado(1))
print(true and lado(2))
```

```salida
false
lado 2
true
```

## Concatenación y tensores

`+` entre `str` concatena. `+`, `-` y `*` entre tensores requieren el mismo tipo de elemento y formas compatibles. El resultado usa las dimensiones conocidas de cualquiera de los dos operandos. Un escalar del mismo tipo que los elementos se combina con cada elemento:

```lg
a: Tensor[float, (2, 2)] = from_list([1.0, 2.0, 3.0, 4.0], 2, 2)
print(a * 2.0)
```

```salida
Tensor[float, (2, 2)]
[[2.0, 4.0], [6.0, 8.0]]

```

```lg
a: Tensor[float, (2, 2)] = from_list([1.0, 2.0, 3.0, 4.0], 2, 2)
b: Tensor[float, (2, 2)] = from_list([1.0, 0.0, 0.0, 1.0], 2, 2)
print(a @ b)
print(a * b)
```

```salida
Tensor[float, (2, 2)]
[[1.0, 2.0], [3.0, 4.0]]

Tensor[float, (2, 2)]
[[1.0, 0.0], [0.0, 4.0]]

```

## Errores de operador

| Mensaje | Causa |
|---|---|
| `operador '+' no admite int y str` | tipos incompatibles para el operador |
| `no se mezclan int y float en '*'` | mezcla de `int` y `float` |
| `'%' solo admite int, recibido float y float` | `%` sobre `float` |
| `'and' requiere bool, recibido int y int` | operando no booleano |
| `'not' requiere bool, recibido int` | `not` sobre no booleano |
| `'-' requiere int o float, recibido str` | negación de un tipo que no es numérico |
| `no se pueden comparar int y bool` | comparación de tipos distintos |
| `no se puede comparar 'P' con ==` | struct sin método `eq` |
| `'in' requiere un Dict o una List a la derecha` | `in` sobre `str` u otro tipo |
| `formas incompatibles para '@'` | multiplicación de matrices con dimensiones que no encajan |
| `no se mezclan int y Tensor[float, (2, 2)] en '*'` | escalar `int` con tensor `float` |
