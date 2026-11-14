# Biblioteca estándar

La biblioteca estándar son módulos escritos en el propio lenguaje, en la carpeta `stdlib/`. Los nativos en C que necesitan están en `src/runtime/std/` y se enlazan en `lg_runtime`.

## Importar

```lg
import mat
import red
import datos
```

```salida
```

`lgc` busca cada módulo en este orden: la carpeta del fichero que importa, las rutas de `LG_PATH`, las de `-I`, y al final la carpeta `stdlib/` del compilador. Por eso `import mat` carga `stdlib/mat.lg` sin configuración adicional. Un fichero local `mat.lg` tiene prioridad sobre la biblioteca.

Las firmas de este documento fijan formas concretas de tensor. El lenguaje no tiene genéricos ni dimensiones dinámicas en firmas (ver [limitaciones](../limitaciones.md)), así que cada función trabaja con una forma determinada.

## mat

Álgebra básica sobre tensores de forma concreta. Escrito en el lenguaje, sin nativos.

| Función | Firma | Devuelve |
|---|---|---|
| `identidad2` | `identidad2()` | `Tensor[float, (2, 2)]`, matriz identidad |
| `identidad3` | `identidad3()` | `Tensor[float, (3, 3)]`, matriz identidad |
| `suma_total` | `suma_total(t: Tensor[float, (2, 2)])` | `float`, suma de los 4 elementos |
| `media` | `media(t: Tensor[float, (2, 2)])` | `float`, `suma_total(t) / 4.0` |
| `producto_punto` | `producto_punto(a: Tensor[float, (3,)], b: Tensor[float, (3,)])` | `float`, producto escalar |

`identidad(n)` con `n` variable y `producto_punto` para vectores de longitud arbitraria no se pueden escribir: el tipo de retorno y los parámetros necesitan dimensiones literales. Por eso hay una función por forma.

Ejemplo verificado:

```lg
import mat

i2 = identidad2()
print(i2)
print(suma_total(i2))
print(media(i2))
print(identidad3().forma())
print(producto_punto(from_list([1.0, 2.0, 3.0], 3), from_list([4.0, 5.0, 6.0], 3)))
print(media(from_list([1.0, 2.0, 3.0, 6.0], 2, 2)))
```

```salida
Tensor[float, (2, 2)]
[[1.0, 0.0], [0.0, 1.0]]

2.0
0.5
[3, 3]
32.0
3.0
```

## red

Capa lineal, activaciones y pérdida. Dos nativos elementwise en `src/runtime/std/red.c`, declarados como `extern "C"` en `stdlib/red.lg`.

| Nombre | Firma | Devuelve |
|---|---|---|
| `Lineal` | `struct Lineal` con `pesos: Tensor[float, (2, 2)]` y `sesgo: Tensor[float, (1, 2)]` | struct |
| `Lineal.adelante` | `adelante(self, x: Tensor[float, (1, 2)])` | `Tensor[float, (1, 2)]`, `x @ pesos + sesgo` |
| `sigmoide` | `sigmoide(t: Tensor[float, (1, 2)])` | `Tensor[float, (1, 2)]`, `1 / (1 + exp(-t))` |
| `relu` | `relu(t: Tensor[float, (1, 2)])` | `Tensor[float, (1, 2)]`, `max(t, 0)` |
| `perdida_mse` | `perdida_mse(pred: Tensor[float, (1, 2)], obj: Tensor[float, (1, 2)])` | `float`, media de `(pred - obj)^2` sobre los 2 elementos |

Ejemplo verificado (`examples/std_red.lg`, con los valores de entrada):

```lg
import red

entrada = from_list([1.0, 0.5], 1, 2)
pesos = from_list([0.5, -0.5, 0.25, 1.0], 2, 2)
sesgo = from_list([0.0, 0.1], 1, 2)
capa = Lineal(pesos: pesos, sesgo: sesgo)

z = capa.adelante(entrada)
print("salida lineal:", z)
print("sigmoide:", sigmoide(z))
print("relu:", relu(z))
print("pérdida MSE:", perdida_mse(z, from_list([0.5, 0.5], 1, 2)))
```

```salida
salida lineal: Tensor[float, (1, 2)]
[[0.625, 0.1]]

sigmoide: Tensor[float, (1, 2)]
[[0.6513548646660542, 0.52497918747894]]

relu: Tensor[float, (1, 2)]
[[0.625, 0.1]]

pérdida MSE: 0.08781250000000002
```

### Nativos de red

Los nativos no dependen de la forma: operan sobre los elementos contiguos del tensor. Las firmas de `red.lg` fijan `(1, 2)`. Para otra forma, copia la declaración `extern "C"` con la forma deseada.

| Símbolo C | Contrato en el lenguaje |
|---|---|
| `LgTensor* lg_std_exp_tensor(const LgTensor* t)` | `extern "C" fn lg_std_exp_tensor(t: Tensor[float, (1, 2)]) -> Tensor[float, (1, 2)]`; `exp` elemento a elemento |
| `LgTensor* lg_std_max_tensor(const LgTensor* t, double s)` | `extern "C" fn lg_std_max_tensor(t: Tensor[float, (1, 2)], s: float) -> Tensor[float, (1, 2)]`; `max(x, s)` elemento a elemento |

Ambos admiten solo `Tensor[float]` y devuelven un tensor nuevo de la misma forma.

## datos

Lectura de CSV numérico y lectura/escritura de texto. Nativos en `src/runtime/std/datos.c`.

| Función | Firma | Devuelve |
|---|---|---|
| `leer_csv_numeros` | `leer_csv_numeros(ruta: str)` | `List[float]` |
| `escribir_texto` | `escribir_texto(ruta: str, texto: str)` | nada (sobrescribe el fichero) |
| `leer_texto` | `leer_texto(ruta: str)` | `str`, contenido completo |

Las rutas se resuelven respecto al directorio de trabajo del programa, no respecto al fichero fuente.

`leer_csv_numeros` separa los campos por `,`, `\n` y `\r`, ignora los campos vacíos y convierte cada campo con `strtod`. Un campo que no es un número completo termina el programa con `valor no numérico en CSV: '...'`. Si el fichero no se puede abrir, termina con `no se puede abrir para leer: '...'`.

Ejemplo verificado (`examples/std_datos.lg`, abreviado):

```lg
import datos
import mat

escribir_texto("ventas.csv", "10.5, 12.0\n9.0,\n11.5\n")
ventas = leer_csv_numeros("ventas.csv")
print("ventas:", ventas)
print("cuántas:", len(ventas))

mut total = 0.0
for v in ventas:
    total = total + v
print("total:", total)

cuadricula = from_list([1.0, 2.0, 3.0, 6.0], 2, 2)
print("media de la cuadrícula:", media(cuadricula))
```

```salida
ventas: [10.5, 12.0, 9.0, 11.5]
cuántas: 4
total: 43.0
media de la cuadrícula: 3.0
```

### Nativos de datos

| Símbolo C | Contrato en el lenguaje |
|---|---|
| `LgList* lg_std_leer_csv_numeros(const LgStr* ruta)` | `extern "C" fn lg_std_leer_csv_numeros(ruta: str) -> List[float]`; devuelve una lista nueva |
| `void lg_std_escribir_texto(const LgStr* ruta, const LgStr* texto)` | `extern "C" fn lg_std_escribir_texto(ruta: str, texto: str)` |
| `LgStr* lg_std_leer_texto(const LgStr* ruta)` | `extern "C" fn lg_std_leer_texto(ruta: str) -> str`; devuelve un str nuevo |

## Pruebas

Los casos `tests/e2e/std_*.lg` cubren los tres módulos y los dos errores de ejecución de `datos` (archivo inexistente y valor no numérico). Se ejecutan con `tests/e2e/run_e2e.sh build/bin/lgc`. Los casos escriben en `build/`, así que el runner debe lanzarse desde la raíz del repositorio.
