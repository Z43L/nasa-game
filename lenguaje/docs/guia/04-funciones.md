# Capítulo 4. Funciones

Una función agrupa código con un nombre, recibe parámetros y puede devolver un valor. Se declaran al nivel principal del archivo, nunca dentro de otro bloque.

## Declarar y llamar

```lg
fn suma(a: int, b: int) -> int:
    return a + b

print(suma(2, 3))
```

```salida
5
```

- Cada parámetro se escribe `nombre: tipo`.
- El tipo de retorno va después de `->`. Si se omite, la función no devuelve valor (es *void*).
- El cuerpo es un bloque indentado.

Una función sin tipo de retorno puede terminar con `return` sin valor, o simplemente llegar al final del bloque:

```lg
fn saluda(nombre: str):
    if nombre == "":
        return
    print("hola", nombre)

saluda("Ana")
saluda("")
print("fin")
```

```salida
hola Ana
fin
```

Una función void no puede usarse como valor: `x = saluda("Ana")` es un error.

## Todos los caminos devuelven

Si una función declara tipo de retorno, cada camino debe terminar en un `return`. El compilador comprueba que cada rama de un `if` devuelva un valor (con `else`) y no acepta un `return` dentro de un bucle como única salida:

```lg
fn signo(x: int) -> int:
    if x > 0:
        return 1
    elif x < 0:
        return -1
    else:
        return 0

print(signo(-4))
print(signo(0))
```

```salida
-1
0
```

Si falta una rama, el error aparece en la firma:

```lg
fn signo(x: int) -> int:
    if x > 0:
        return 1
```

```error
programa.lg:1:1: error: la función 'signo' no devuelve un valor en todos los caminos
   1 | fn signo(x: int) -> int:
     | ^
sugerencia: termina la función con 'return <valor>'
```

## Parámetros

Los parámetros son **inmutables**: no puedes reasignarlos ni modificarlos.

```lg
fn doble(x: int) -> int:
    x = x * 2
    return x

print(doble(4))
```

```error
programa.lg:2:5: error: variable inmutable 'x'
   2 |     x = x * 2
     |     ^
sugerencia: los parámetros no se reasignan; copia el valor en una variable 'mut'
```

La forma correcta de transformar un valor es declarar una variable nueva:

```lg
fn doble(x: int) -> int:
    mut resultado = x
    resultado = resultado * 2
    return resultado

print(doble(4))
```

```salida
8
```

Los argumentos se pasan por posición. Los nombres de argumento solo existen en la construcción de structs (capítulo 7); en una llamada a función, `f(b: 1)` es un error.

## Recursión y orden de declaración

Una función puede llamarse a sí misma, y puedes llamar a una función declarada más abajo en el archivo:

```lg
fn factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print(par(10))

fn par(n: int) -> bool:
    if n == 0:
        return true
    return impar(n - 1)

fn impar(n: int) -> bool:
    if n == 0:
        return false
    return par(n - 1)

print(factorial(5))
```

```salida
true
120
```

## Las funciones no ven las variables globales

Las sentencias del nivel principal son locales al programa. Una función no puede leer ni escribir esas variables; tiene que recibirlas como parámetros:

```lg
limite = 10

fn excede(x: int) -> bool:
    return x > limite

print(excede(3))
```

```error
programa.lg:4:16: error: la función no puede usar la variable global 'limite'
   4 |     return x > limite
     |                ^
sugerencia: pásala como parámetro
```

```lg
limite = 10

fn excede(x: int, limite: int) -> bool:
    return x > limite

print(excede(3, limite))
```

```salida
false
```

## Funciones con listas y tensores

Una lista pasada como parámetro es el mismo objeto que en el llamador, así que leerla es barato. Pero no puedes modificarla dentro de la función porque el parámetro es inmutable. Para producir un resultado nuevo, devuelve una lista:

```lg
fn cuadrados(n: int) -> List[int]:
    mut r: List[int] = []
    for i in 0..n:
        r.append(i * i)
    return r

print(cuadrados(4))
```

```salida
[0, 1, 4, 9]
```

## Ejemplo: máximo común divisor

```lg
fn mcd(a: int, b: int) -> int:
    mut x = a
    mut y = b
    while y != 0:
        mut r = x % y
        x = y
        y = r
    return x

print(mcd(48, 18))
print(mcd(17, 5))
```

```salida
6
1
```

## Ejercicios

1. Escribe `fn maximo(a: int, b: int) -> int` y úsala para encontrar el mayor de tres números.
2. Escribe `fn fibonacci(n: int) -> int` recursiva y muestra los 10 primeros valores.

## Siguiente

[Capítulo 5. Listas y diccionarios](05-listas-y-diccionarios.md)
