# Capítulo 3. Control de flujo

Con `if`, `elif`, `else`, `while` y `for` decides qué código se ejecuta y cuántas veces. La condición de cada uno debe ser `bool`: el lenguaje no convierte números ni textos a verdadero o falso.

## if, elif y else

```lg
nota = 7
if nota >= 9:
    print("sobresaliente")
elif nota >= 5:
    print("aprobado")
else:
    print("suspenso")
```

```salida
aprobado
```

- `elif` puede repetirse todas las veces que quieras.
- `else` es opcional y va siempre al final de la cadena.
- Escribe `else:` con dos puntos. `else if` no existe; usa `elif`.

Si la condición no es `bool`, el compilador lo indica:

```lg
x = 1
if x:
    print("sí")
```

```error
programa.lg:2:4: error: la condición de 'if' debe ser bool, recibido int
   2 | if x:
     |    ^
sugerencia: usa una comparación, por ejemplo x > 0
```

## while

`while` repite el bloque mientras la condición sea `true`:

```lg
mut i = 0
mut suma = 0
while i < 5:
    i = i + 1
    suma = suma + i
print(suma)
```

```salida
15
```

La variable que controla el bucle debe ser `mut`. Un `while` no tiene cláusula `else`.

## for con rango

`for variable in inicio..fin:` recorre los enteros desde `inicio` hasta `fin - 1`. El extremo final **no** se incluye:

```lg
for i in 0..3:
    print(i)
```

```salida
0
1
2
```

Si `inicio` es mayor o igual que `fin`, el bucle no se ejecuta. La variable del `for` es inmutable dentro del bucle:

```lg
for i in 5..2:
    print("nunca")
print("fin")
```

```salida
fin
```

Los extremos pueden ser expresiones `int` cualesquiera, por ejemplo `for i in 0..n * 2:`.

## for sobre una lista

Con una lista, la variable toma cada elemento en orden:

```lg
for x in [4, 5, 6]:
    print(x * 2)
```

```salida
8
10
12
```

El `for` solo admite rangos y listas. Para recorrer un diccionario, consulta el capítulo 05.

## break y continue

- `break` sale del bucle más interno.
- `continue` salta a la siguiente vuelta.

```lg
for i in 0..10:
    if i == 2:
        continue
    if i == 4:
        break
    print(i)
```

```salida
0
1
3
```

Ambos solo son válidos dentro de `for` o `while`. Fuera de un bucle son errores de compilación.

## and, or y not

Los operadores lógicos evalúan de forma **perezosa**: `and` no evalúa el lado derecho si el izquierdo es `false`, y `or` no lo evalúa si el izquierdo es `true`.

```lg
fn comprueba(x: int) -> bool:
    print("evalúo", x)
    return x > 0

print(false and comprueba(1))
print(true or comprueba(2))
print(comprueba(3))
```

```salida
false
true
evalúo 3
true
```

Las funciones que llamas en el lado derecho no se ejecutan cuando el resultado ya se conoce. `comprueba(1)` y `comprueba(2)` no imprimen nada.

## Ejemplo: buscar un número primo

```lg
fn es_primo(n: int) -> bool:
    if n < 2:
        return false
    mut d = 2
    while d * d <= n:
        if n % d == 0:
            return false
        d = d + 1
    return true

for n in 1..12:
    if es_primo(n):
        print(n)
```

```salida
2
3
5
7
11
```

La función `es_primo` se explica en el [capítulo 4](04-funciones.md). Los `return` dentro de los bucles y de los `if` salen de la función inmediatamente.

## Ejercicios

1. Escribe un bucle `for` que imprima los números pares entre 0 y 10. Usa `%` para comprobar la paridad.
2. Usa un `while` para calcular la mayor potencia de 2 menor que 1000.

## Siguiente

[Capítulo 4. Funciones](04-funciones.md)
