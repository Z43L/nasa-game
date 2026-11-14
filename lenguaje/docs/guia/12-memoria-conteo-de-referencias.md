# Capítulo 12. Memoria y conteo de referencias

En este lenguaje no escribes `malloc` ni `free`. Los valores que viven en el heap se liberan solos cuando ya nadie los usa, gracias al **conteo de referencias**. Este capítulo explica qué significa eso en la práctica, sobre todo para entender qué se comparte y qué se copia.

## Qué vive en el heap

| Tipo | Vive en | Se gestiona por conteo |
|---|---|---|
| `int`, `float`, `bool` | la pila, como valor | no |
| `str` | heap | sí |
| `List[T]`, `Dict[K, V]` | heap | sí |
| `Tensor[...]` | heap | sí |
| structs | heap | sí |

Cada objeto del heap lleva un contador con el número de referencias vivas. Cuando el contador llega a cero, el objeto se libera y, si contiene otros objetos (una lista con cadenas, por ejemplo), se liberan también sus hijos.

## Asignar comparte, no copia

Para `int`, `float` y `bool` asignar copia el valor. Para los tipos del heap, asignar **comparte** el objeto: las dos variables apuntan a lo mismo.

```lg
mut a = [1]
mut b = a
b.append(2)
print(a)
print(b)
```

```salida
[1, 2]
[1, 2]
```

Lo mismo pasa con las structs:

```lg
struct Punto:
    x: int
    y: int

mut p = Punto(x: 1, y: 2)
mut q = p
q.x = 50
print(p.x)
```

```salida
50
```

No hay una operación de copia integrada para listas, diccionarios o structs. Si necesitas un resultado independiente, construye uno nuevo: un `for` que añada los elementos a una lista nueva, o una struct con los mismos valores.

```lg
mut original = [1, 2, 3]
mut copia: List[int] = []
for x in original:
    copia.append(x)
copia.append(4)
print(original)
print(copia)
```

```salida
[1, 2, 3]
[1, 2, 3, 4]
```

## Los parámetros son préstamos

Cuando pasas un valor a una función, la función lo recibe prestado: no toma posesión ni lo libera. Como los parámetros son inmutables, no puedes modificar el objeto recibido desde la función; el capítulo 4 y el capítulo 7 muestran las reglas. Lo que sí puedes es leerlo y devolver un valor nuevo.

## Cuándo se libera

Un valor se libera cuando su último dueño desaparece:

- Una variable local se libera al salir de su bloque (fin del `if`, `for` o `while`, o fin de la función), también al hacer `break`, `continue` o `return`.
- Un valor temporal, por ejemplo el resultado de `from_list(...)` que se usa solo como argumento, se libera justo después de usarlo.
- Un elemento de una lista o un campo de una struct vive mientras viva su contenedor.

Por eso puedes crear objetos dentro de un bucle sin acumular memoria:

```lg
mut total = 0
for i in 0..100000:
    mut s = "x" + str(i)
    mut par = [s, s]
    total = total + len(par[0])
print(total)
```

```salida
588890
```

En macOS puedes comprobar que no quedan objetos sin liberar con `leaks --atExit -- ./programa`. Para el programa anterior el informe indica cero fugas.

## Limitación: las referencias circulares no se liberan

El conteo de referencias no detecta ciclos. Si un objeto se contiene a sí mismo con referencias fuertes, directa o indirectamente, su contador nunca llega a cero y la memoria no se recupera al terminar el ámbito. El lenguaje no tiene un recolector de ciclos, pero sí referencias débiles (`Debil[T]`) para romperlos.

```lg
struct Nodo:
    valor: int
    hijos: List[Nodo]

fn crear() -> int:
    mut a = Nodo(valor: 1, hijos: [])
    a.hijos.append(a)
    return a.valor

print(crear())
```

```salida
1
```

El programa imprime lo esperado, pero `leaks --atExit -- ./programa` informa de tres fugas: el nodo `a` se contiene a sí mismo en `hijos`, así que su contador nunca baja a cero. Evita estos ciclos: en estructuras de árbol, guarda referencias fuertes solo hacia los hijos. Si necesitas volver al padre, guarda esa referencia como `Debil[T]` (ver [tipos](../referencia/tipos.md#hilot-canalt-y-debilt)).

## Diccionarios y claves

Las claves `str` se retienen dentro del diccionario cuando la entrada es nueva, y el diccionario las libera al destruirse. El valor de una entrada nueva pasa a ser del diccionario. Como programador no necesitas hacer nada de esto, pero explica por qué `d[k] = v` es seguro aunque `k` sea una cadena temporal.

## Reglas para recordar

- Asignar un valor del heap comparte el objeto. Modificar a través de una variable se ve por la otra.
- Para independizar datos, construye una copia de forma explícita.
- Un parámetro no se modifica ni se libera dentro de la función.
- No hay `free`, ni punteros, ni recolector: lo que queda vivo al final es lo que sigue referenciado desde un ciclo.

## Ejercicios

1. Crea una lista `a`, asígnala a `b` con `mut b = a`, añade un elemento a `b` y muestra `a`. Explica por qué cambia.
2. Escribe una función que reciba una lista de `int` y devuelva una copia con los valores duplicados.

## Siguiente

Has terminado la guía. Para la sintaxis completa y los mensajes del compilador, consulta la [referencia](../referencia/sintaxis.md). Para el diseño interno, consulta la [arquitectura](../arquitectura.md).
