# Capítulo 8. Tensores

Un tensor es un array denso de números con forma fija, por ejemplo una matriz de 2 filas y 3 columnas. Los tensores son el tipo para álgebra lineal y para cálculos como una capa de red neuronal. Su tipo incluye la forma: `Tensor[float, (2, 3)]` es un tensor de elementos `float` con dos filas y tres columnas.

## Tipos de tensor

- El elemento es `float` o `int`. Ningún otro.
- La forma es una lista de dimensiones enteras y positivas entre paréntesis: `(4)`, `(2, 3)`, `(2, 3, 4)`.
- Una dimensión que el compilador no conoce (porque viene de una variable) se trata como desconocida y se comprueba en ejecución.

```lg
t: Tensor[float, (2, 3)] = zeros(2, 3)
print(t.forma())
```

```salida
[2, 3]
```

## Crear tensores

| Función | Resultado | Notas |
|---|---|---|
| `zeros(d1, d2, ...)` | `Tensor[float, ...]` lleno de ceros | |
| `ones(d1, d2, ...)` | `Tensor[float, ...]` lleno de unos | |
| `randn(d1, d2, ...)` | `Tensor[float, ...]` con valores pseudoaleatorios | distribución normal |
| `from_list(lista, d1, d2, ...)` | `Tensor` con los elementos de la lista | el tipo del tensor es el de la lista |

`from_list` recibe los elementos en orden de filas (row-major) y comprueba que su número coincide con el producto de las dimensiones:

```lg
a: Tensor[float, (2, 3)] = from_list([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 2, 3)
print(a)
```

```salida
Tensor[float, (2, 3)]
[[1.0, 2.0, 3.0], [4.0, 5.0, 6.0]]

```

Al imprimir un tensor aparecen dos líneas: la cabecera con el tipo y la forma, y las filas entre corchetes. Después `print` añade su salto de línea, por eso hay una línea vacía tras el tensor.

Si la lista tiene un número de elementos distinto al de la forma, y el número se conoce al compilar, el error es de compilación:

```lg
x = from_list([1.0, 2.0, 3.0], 2, 2)
```

```error
programa.lg:1:5: error: from_list: la lista tiene 3 elementos pero la forma requiere 4
   1 | x = from_list([1.0, 2.0, 3.0], 2, 2)
     |     ^
sugerencia: ajusta la lista o las dimensiones
```

Las dimensiones que vienen de variables se comprueban al ejecutar:

```lg
n = 2
m = 2
t: Tensor[float, (2, 3)] = zeros(n, m)
print(t.forma())
```

```rterror
programa.lg:3: error de ejecución: forma incompatible: se esperaba (2, 3) pero el tensor tiene forma (2, 2)
```

## Consultar la forma

```lg
t = zeros(2, 4)
print(t.forma())
print(t.numel())
print(len(t))
```

```salida
[2, 4]
8
2
```

- `t.forma()` devuelve una `List[int]` con las dimensiones.
- `t.numel()` devuelve el número total de elementos.
- `len(t)` devuelve la primera dimensión.

## Transponer y multiplicar

`t.transpose()` intercambia filas y columnas de un tensor de rango 2. El operador `@` es la multiplicación de matrices: exige tensores `float` de rango 2 y que las columnas de la izquierda coincidan con las filas de la derecha.

```lg
a: Tensor[float, (2, 3)] = from_list([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 2, 3)
b = a.transpose()
c = a @ b
print(b.forma())
print(c.forma())
print(c)
```

```salida
[3, 2]
[2, 2]
Tensor[float, (2, 2)]
[[14.0, 32.0], [32.0, 77.0]]

```

Multiplicar formas incompatibles es un error de compilación si las formas son conocidas:

```lg
a: Tensor[float, (2, 3)] = from_list([1.0, 2.0, 3.0, 4.0, 5.0, 6.0], 2, 3)
c = a @ a
```

```error
programa.lg:2:7: error: formas incompatibles para '@': Tensor[float, (2, 3)] y Tensor[float, (2, 3)]
   2 | c = a @ a
     |       ^
sugerencia: las columnas de la izquierda deben coincidir con las filas de la derecha
```

## Operaciones elemento a elemento

`+`, `-` y `*` operan entre dos tensores de la **misma** forma y el mismo tipo de elemento. El resultado tiene esa forma:

```lg
a: Tensor[float, (2, 2)] = from_list([1.0, 2.0, 3.0, 4.0], 2, 2)
b: Tensor[float, (2, 2)] = ones(2, 2)
print(a + b)
print(a - b)
print(a * b)
```

```salida
Tensor[float, (2, 2)]
[[2.0, 3.0], [4.0, 5.0]]

Tensor[float, (2, 2)]
[[0.0, 1.0], [2.0, 3.0]]

Tensor[float, (2, 2)]
[[1.0, 2.0], [3.0, 4.0]]

```

### Operaciones con escalares

Un escalar `float` (o `int` si el tensor es `int`) puede combinarse con un tensor en `+`, `-`, `*` y `/`, en cualquiera de los dos órdenes:

```lg
a: Tensor[float, (2, 2)] = from_list([1.0, 2.0, 3.0, 4.0], 2, 2)
x = 2.0
print(2.0 * a)
print(a - x)
print(a / x)
```

```salida
Tensor[float, (2, 2)]
[[2.0, 4.0], [6.0, 8.0]]

Tensor[float, (2, 2)]
[[-1.0, 0.0], [1.0, 2.0]]

Tensor[float, (2, 2)]
[[0.5, 1.0], [1.5, 2.0]]

```

El escalar debe tener el mismo tipo que los elementos del tensor: `a * 2` con `a` de tipo `float` es un error de compilación, porque `2` es `int`:

```lg
a: Tensor[float, (2, 2)] = ones(2, 2)
print(a * 2)
```

```error
programa.lg:2:9: error: no se mezclan int y Tensor[float, (2, 2)] en '*'
   2 | print(a * 2)
     |         ^
sugerencia: usa int(x) o un tensor de int
```

### Indexar y modificar elementos

Un tensor se indexa con un índice por dimensión, separados por comas: `t[i, j]`. Un índice negativo cuenta desde el final de su dimensión. Si indexas con menos índices que dimensiones, obtienes un tensor de menor rango; con todos los índices obtienes un escalar:

```lg
a: Tensor[float, (2, 2)] = from_list([1.0, 2.0, 3.0, 4.0], 2, 2)
print(a[0, 1])
print(a[-1, 0])
print(a[1])
print(a[0][1])
```

```salida
2.0
3.0
Tensor[float, (2,)]
[3.0, 4.0]

2.0
```

`a[0][1]` encadena dos indexaciones y da el mismo resultado que `a[0, 1]`.

Para escribir un elemento, la variable debe ser `mut` e indexar con todas las dimensiones:

```lg
mut b: Tensor[float, (2, 2)] = zeros(2, 2)
b[0, 1] = 5.0
print(b)
```

```salida
Tensor[float, (2, 2)]
[[0.0, 5.0], [0.0, 0.0]]

```

Asignar a un tensor parcial (`b[0] = ...`) es un error de compilación. Un índice fuera de rango se detecta en compilación si el índice y la dimensión son literales, y en ejecución en otro caso:

```lg
a: Tensor[float, (2, 2)] = ones(2, 2)
print(a[2, 0])
```

```error
programa.lg:2:9: error: índice 2 fuera de rango (dimensión 0 de tamaño 2)
   2 | print(a[2, 0])
     |         ^
sugerencia: los índices válidos van de -2 a 1
```

Los índices de un tensor deben ser `int`. Las listas usan un solo índice; `lista[0, 1]` es un error.

### Lo que no hay

- División de un tensor entre otro tensor (`a / b`) no está soportada.
- Reducciones (suma, máximo), `reshape` y slicing con rangos no existen todavía; ver [limitaciones](../limitaciones.md).

## Un ejemplo: capa lineal

Una capa lineal calcula `x @ w + b`. Con tensores de una fila como entrada:

```lg
fn capa(x: Tensor[float, (1, 2)], w: Tensor[float, (2, 2)], b: Tensor[float, (1, 2)]) -> Tensor[float, (1, 2)]:
    return x @ w + b

x: Tensor[float, (1, 2)] = from_list([1.0, 0.5], 1, 2)
w: Tensor[float, (2, 2)] = from_list([0.5, -0.5, 0.25, 1.0], 2, 2)
b: Tensor[float, (1, 2)] = from_list([0.0, 0.1], 1, 2)
h = capa(x, w, b)
print("forma de salida:", h.forma())
```

```salida
forma de salida: [1, 2]
```

## Números aleatorios reproducibles

`randn` usa un generador pseudoaleatorio propio. Con la semilla por defecto (42) los valores son siempre los mismos. Para cambiarla, define la variable de entorno `LG_SEED` con un número entero antes de ejecutar el programa:

```lg
print(randn(3))
```

```salida
Tensor[float, (3,)]
[0.2960330150871692, 0.6443779794453866, 0.3760757562015089]

```

Con `LG_SEED=7 ./programa` la salida cambia, pero se repite en cada ejecución con la misma semilla.

## Ejercicios

1. Crea una matriz `2x2` con `from_list` y calcula su cuadrado con `@`.
2. Calcula `a + b` para dos tensores de forma `(3)` y comprueba que `a + c` con `c` de forma `(2)` da un error de compilación.

## Siguiente

[Capítulo 9. Módulos e import](09-modulos-e-import.md)
