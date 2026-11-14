# Capítulo 2. Variables y tipos

Una variable guarda un valor con nombre. En este lenguaje cada variable tiene un tipo fijo: lo puede deducir el compilador del valor inicial o puedes escribirlo tú. Además, por defecto una variable **no cambia**; para poder reasignarla hay que declararla con `mut`.

## Declarar una variable

Hay tres formas:

```lg
x = 10          # tipo inferido (int), inmutable
y: float = 2.5  # tipo escrito, inmutable
mut z = 0       # inferido, mutable
mut w: int = 1  # escrito y mutable
```

```salida
```

El primer programa no imprime nada porque solo declara variables. Las reglas son:

- `nombre = valor` declara una variable inmutable con el tipo del valor.
- `nombre: tipo = valor` declara una variable inmutable con el tipo escrito. El valor debe ser compatible con ese tipo.
- `mut nombre = valor` y `mut nombre: tipo = valor` declaran una variable mutable.
- Una declaración con `mut` o con tipo necesita siempre valor inicial. `x: int` a secas es un error.

Para cambiar una variable mutable se escribe solo la asignación:

```lg
mut contador = 0
contador = contador + 1
contador = contador + 1
print(contador)
```

```salida
2
```

Si intentas reasignar una variable inmutable, el compilador lo rechaza:

```lg
x = 1
x = 2
print(x)
```

```error
programa.lg:2:1: error: variable inmutable 'x'
   2 | x = 2
     | ^
sugerencia: usa 'mut x' para poder reasignarla
```

## Los tipos escalares

| Tipo | Valores | Ejemplo |
|---|---|---|
| `int` | enteros de 64 bits con signo | `42`, `-7` |
| `float` | decimales de doble precisión | `3.14`, `1e3`, `2.5e-3` |
| `bool` | `true` o `false` | `true` |
| `str` | cadenas UTF-8 inmutables | `"hola"` |

Los tipos compuestos (`List`, `Dict`, `Tensor` y las structs) se ven en sus capítulos.

### Literales numéricos

- Un literal entero es una secuencia de dígitos. Un literal con parte decimal o exponente es `float`.
- Un decimal necesita dígitos a ambos lados del punto: `1.0` vale, `1.` no.
- Un exponente va con `e` o `E`, con signo opcional: `1e3`, `2.5E-3`.
- Un entero fuera del rango de `int` es un error de compilación.

```lg
print(1.0)
print(1e3)
print(2.5E-3)
print(9223372036854775807)
```

```salida
1.0
1000.0
0.0025
9223372036854775807
```

Un `float` siempre se imprime con punto decimal (`1000.0`) o en notación científica para valores muy grandes o muy pequeños (`1e+16`, `1e-05`).

## No se mezclan int y float

Las operaciones aritméticas y las comparaciones exigen operandos del mismo tipo. Para mezclarlos, convierte uno de ellos:

```lg
a = 7
b = 2.0
print(float(a) / b)
print(a / 2)
```

```salida
3.5
3
```

`a / 2` da `3` porque la división de dos `int` es entera: trunca hacia cero. `int(7.9)` vale `7`, y `int(-7.9)` vale `-7`.

El intento de mezclarlos da error:

```lg
a = 7
print(a * 0.5)
```

```error
programa.lg:2:9: error: no se mezclan int y float en '*'
   2 | print(a * 0.5)
     |         ^
sugerencia: usa float(x) para convertir el int a float
```

## Conversiones

Las conversiones son funciones integradas. Cada una acepta un conjunto de tipos:

| Llamada | Acepta | Resultado |
|---|---|---|
| `int(x)` | `float`, `int`, `bool`, `str` | `int` |
| `float(x)` | `int`, `float`, `str` | `float` |
| `str(x)` | `int`, `float`, `bool`, `str` | `str` |

```lg
print(int("42") + 1)
print(float("3.5") + 1.0)
print(str(3.0) + "!")
print(int(true) + int(false))
```

```salida
43
4.5
3.0!
1
```

Convertir texto que no es un número se detecta en ejecución:

```lg
print(int("abc"))
```

```rterror
error de ejecución: no se puede convertir 'abc' a int
```

No existe conversión de `int` a `bool`. Usa comparaciones: `x != 0`.

## Booleanos

`true` y `false` son valores `bool`. Los operadores `and`, `or` y `not` trabajan solo con `bool`. Las reglas de precedencia están en [operadores](../referencia/operadores.md).

```lg
x = 5
print(x > 0 and x < 10)
print(not (x == 5))
```

```salida
true
false
```

## Ámbitos

Un bloque (el cuerpo de un `if`, `while` o `for`) crea su propio ámbito. Las variables declaradas dentro no existen fuera:

```lg
if true:
    dentro = 1
    print(dentro)
print(dentro)
```

```error
programa.lg:4:7: error: variable no definida 'dentro': su ámbito ya terminó
   4 | print(dentro)
     |       ^
sugerencia: la variable solo existe dentro del bloque donde se declaró; declárala de nuevo o pásala como parámetro
```

Al contrario, un bloque **sí puede reasignar** una variable mutable declarada fuera:

```lg
mut total = 0
if true:
    total = total + 5
print(total)
```

```salida
5
```

No puedes declarar dos veces el mismo nombre en el mismo ámbito. Si repites `x = 1` y luego `x: int = 2` en el mismo nivel, el segundo es un error. Para cambiar el valor, usa `mut` desde el principio.

## Ejercicios

1. Declara `mut` una variable con la temperatura en grados Celsius y calcula la temperatura en Fahrenheit (`f = c * 9.0 / 5.0 + 32.0`). Usa `float` para evitar mezclar tipos.
2. Predice la salida de `print(7 / 2)` y `print(7.0 / 2.0)` antes de ejecutarlo.

## Siguiente

[Capítulo 3. Control de flujo](03-control-de-flujo.md)
