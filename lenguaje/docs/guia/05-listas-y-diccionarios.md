# Capítulo 5. Listas y diccionarios

Las listas (`List[T]`) guardan una secuencia de valores del mismo tipo. Los diccionarios (`Dict[K, V]`) asocian claves con valores. Ambos son objetos mutables que viven en el heap y se gestionan con conteo de referencias (capítulo 12).

## Listas

### Crear una lista

```lg
numeros = [1, 2, 3]
decimales = [0.5, 1.5]
print(numeros)
print(decimales)
```

```salida
[1, 2, 3]
[0.5, 1.5]
```

Todos los elementos deben tener el mismo tipo. `[1, "a"]` es un error. Una lista vacía necesita un tipo escrito:

```lg
mut resultados: List[int] = []
resultados.append(7)
print(resultados)
```

```salida
[7]
```

Sin anotación, la lista vacía no tiene tipo:

```lg
mut vacia = []
```

```error
programa.lg:1:13: error: la lista vacía necesita un tipo
   1 | mut vacia = []
     |             ^
sugerencia: anótalo: x: List[int] = []
```

### Leer y escribir elementos

Los índices empiezan en `0`. Un índice negativo cuenta desde el final: `-1` es el último elemento.

```lg
mut l = [1, 2, 3]
print(l[0])
print(l[-1])
l[1] = 20
print(l)
```

```salida
1
3
[1, 20, 3]
```

Un índice fuera de rango termina el programa con error en tiempo de ejecución:

```lg
l = [1, 2]
print(l[5])
```

```rterror
error de ejecución: índice 5 fuera de rango (longitud 2)
```

Asignar a `l[i]` exige que `l` sea `mut`.

### Métodos de lista

| Método | Efecto | Requiere `mut` |
|---|---|---|
| `l.append(x)` | añade `x` al final | sí |
| `l.pop()` | quita el último elemento y lo devuelve | sí |
| `l.len()` | número de elementos | no |

```lg
mut pila = [10]
pila.append(20)
print(pila.pop())
print(pila.len())
print(len(pila))
```

```salida
20
1
1
```

`pop` sobre una lista vacía es un error de ejecución (`pop de una lista vacía`). `len(lista)` y `lista.len()` hacen lo mismo.

### Comprobar pertenencia y recorrer

El operador `in` indica si un valor está en la lista:

```lg
for x in [4, 5, 6]:
    if x in [5, 9]:
        print("encontrado", x)
```

```salida
encontrado 5
```

### Listas dentro de listas

```lg
matriz = [[1, 2], [3]]
print(matriz)
print(matriz[0][1])
```

```salida
[[1, 2], [3]]
2
```

## Diccionarios

### Crear y consultar

```lg
edades = {"ana": 30, "luis": 25}
print(edades)
print(edades["ana"])
```

```salida
{"ana": 30, "luis": 25}
30
```

Las claves pueden ser `int`, `str` o `bool`. No se admiten `float`, listas ni structs como claves. El diccionario conserva el orden de inserción al imprimirlo.

Leer una clave que no existe termina el programa:

```lg
d = {"a": 1}
print(d["zz"])
```

```rterror
error de ejecución: clave no encontrada
```

Antes de leer, comprueba si la clave existe con `tiene` o con `in`:

```lg
d = {"a": 1}
print(d.tiene("a"), d.tiene("zz"))
print("a" in d)
```

```salida
true false
true
```

### Escribir y sobrescribir

Asignar a `d[clave]` inserta la entrada si no existe y la reemplaza si existe. Como en las listas, requiere `mut`:

```lg
mut stock: Dict[str, int] = {}
stock["manzanas"] = 3
stock["peras"] = 1
stock["manzanas"] = 5
print(stock)
print(len(stock))
```

```salida
{"manzanas": 5, "peras": 1}
2
```

Sobrescribir una clave conserva su posición original en el orden de impresión.

Un diccionario vacío también necesita tipo: `d: Dict[str, int] = {}`.

### Métodos de diccionario

| Método | Efecto | Requiere `mut` |
|---|---|---|
| `d.tiene(k)` | `true` si la clave `k` existe | no |
| `d.len()` | número de entradas | no |

`len(d)` y `d.len()` son equivalentes. No existe `keys()`, `values()` ni `get()`; para recorrer un diccionario hay que guardar las claves tú mismo en una lista.

### Diccionarios con valores compuestos

```lg
mut grupos: Dict[str, List[int]] = {"pares": [], "impares": []}
for n in 1..7:
    if n % 2 == 0:
        grupos["pares"].append(n)
    else:
        grupos["impares"].append(n)
print(grupos)
```

```salida
{"pares": [2, 4, 6], "impares": [1, 3, 5]}
```

Un valor de tipo lista dentro de un diccionario se modifica directamente con `grupos["pares"].append(n)`. Esto funciona porque la lista es un objeto compartido: leer `grupos["pares"]` da una referencia a la misma lista, no una copia. El capítulo 12 explica por qué asignar una lista a otra variable también comparte el objeto.

## Un ejemplo completo

Contar palabras de una lista:

```lg
palabras = ["sol", "luna", "sol", "mar", "sol", "luna"]
mut cuenta: Dict[str, int] = {}
for p in palabras:
    if cuenta.tiene(p):
        cuenta[p] = cuenta[p] + 1
    else:
        cuenta[p] = 1
print(cuenta)
```

```salida
{"sol": 3, "luna": 2, "mar": 1}
```

## Ejercicios

1. Crea una lista con los números del 1 al 10 usando `append` dentro de un `for` y muestra la suma.
2. Crea un diccionario que asocie un nombre con su nota y muestra solo los aprobados (nota mayor o igual que 5).

## Siguiente

[Capítulo 6. Cadenas](06-cadenas.md)
