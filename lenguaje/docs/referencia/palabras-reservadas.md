# Palabras reservadas y nombres restringidos

## Palabras clave

Estas 23 palabras están reservadas por el lexer. No pueden usarse como nombre de variable, función, struct, campo, método ni parámetro:

| Palabra | Uso |
|---|---|
| `fn` | declarar una función o un método |
| `mut` | variable mutable, o `mut self` en un método |
| `if`, `elif`, `else` | condicionales |
| `for`, `in` | bucle sobre un rango o una lista; operador de pertenencia |
| `while` | bucle condicional |
| `break`, `continue` | salir o saltar en un bucle |
| `return` | devolver un valor desde una función |
| `struct` | declarar una struct |
| `impl` | declarar los métodos de una struct |
| `extern` | declarar una función de C |
| `import` | importar un módulo |
| `true`, `false` | literales `bool` |
| `and`, `or`, `not` | operadores lógicos |
| `hilo` | lanza una función en un hilo nuevo (ver [hilos](hilos.md)) |
| `async` | declara una función que se ejecuta en un hilo al llamarla |
| `await` | espera el resultado de un `Hilo[T]` |

Ejemplo de un uso incorrecto:

```lg
fn = 3
```

```error
programa.lg:1:4: error: se esperaba el nombre de la función, pero se encontró '='
   1 | fn = 3
     |    ^
sugerencia: escribe: fn nombre(a: int) -> int:
```

## Nombres de tipo

`int`, `float`, `bool` y `str` son nombres de tipo y también nombres de funciones integradas (conversiones). No pueden usarse como nombre de variable, función o struct:

```lg
float = 2.0
```

```error
programa.lg:1:1: error: 'float' es un nombre reservado
   1 | float = 2.0
     | ^
sugerencia: elige otro nombre para la variable
```

`List`, `Dict` y `Tensor` solo tienen significado en una posición de tipo. Fuera de ella no son palabras reservadas, así que puedes declarar una variable con ese nombre, aunque no es recomendable por confusión.

## Nombres de funciones integradas

Estos nombres están reservados y no pueden usarse para variables, funciones ni structs:

`print`, `len`, `int`, `float`, `str`, `bool`, `zeros`, `ones`, `randn`, `from_list`, `Some`, `debil`, `Canal`, y los nombres `None`, `Option`, `Hilo` y `Debil`, que tienen significado en el código.

```lg
print = 2
```

```error
programa.lg:1:1: error: 'print' es un nombre reservado
   1 | print = 2
     | ^
sugerencia: elige otro nombre para la variable
```

`bool` no es una función integrada, pero también está reservado como nombre.

## self

`self` no es palabra reservada. Solo tiene significado como **primer parámetro** de un método dentro de un bloque `impl`. Fuera de ese contexto es un nombre normal: puedes declarar `self = 1` a nivel principal, aunque no es recomendable.

## Nombres de funciones externas

Una función `extern "C"` usa su nombre C tal cual, sin prefijo. Por eso no puede llamarse igual que otra función de tu programa: el compilador reporta `'sqrt' ya está definida`.

## Reglas de unicidad

- Las funciones y las structs comparten espacio de nombres: no puedes tener una función y una struct con el mismo nombre.
- Dentro de un mismo ámbito no puedes declarar dos veces la misma variable.
- Los campos de una struct son únicos dentro de ella, y un método no puede tener el nombre de un campo.
- Los métodos de una struct son únicos dentro de ella.

## Identificadores

Un identificador empieza por letra ASCII o `_`, y continúa con letras, dígitos o `_`. Las mayúsculas y minúsculas son distintas: `Punto` y `punto` son nombres diferentes.
