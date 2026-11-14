# Capítulo 10. Interop con C: extern

Una declaración `extern "C"` le dice al compilador que una función existe en una biblioteca C y que debe llamarla con la convención de C. Así puedes usar funciones de la biblioteca estándar y de `libm`, como `sqrt` o `exp`, sin reimplementarlas.

## Declarar una función extern

Una declaración extern tiene la misma firma que una función normal, pero **sin cuerpo**: termina en su línea.

```lg
extern "C" fn sqrt(x: float) -> float

print(sqrt(16.0))
```

```salida
4.0
```

- La ABI debe ser `"C"`. Cualquier otra da error.
- El nombre de la función es el nombre C tal cual, sin prefijo.
- Si la función no devuelve nada, omite `->`. La llamada es entonces una sentencia sin valor.

Una función extern con cuerpo es un error:

```lg
extern "C" fn sqrt(x: float) -> float:
    return x
```

```error
programa.lg:1:38: error: las funciones extern no tienen cuerpo
   1 | extern "C" fn sqrt(x: float) -> float:
     |                                      ^
sugerencia: quita los dos puntos: la firma de una función extern termina en su línea
```

## Tipos permitidos

Los tipos que cruzan la frontera con C son los que tienen una representación de C directa:

| Tipo Lenguaje | Tipo C |
|---|---|
| `int` | `int64_t` (`long long`) |
| `float` | `double` |
| `bool` | `_Bool` (un byte) |

Un `int` del lenguaje viaja como entero de 64 bits con signo. Por eso solo debes declarar como `int` los parámetros que en C son `long long` o `int64_t`. Una función C que espera `int` de 32 bits no encaja con este tipo: el compilador no convierte los valores en la frontera.

Los tipos compuestos (`str`, `List`, `Dict`, `Tensor` y las structs) **no son seguros** en una llamada extern: se pasa el puntero al objeto del runtime, que no es un puntero a los datos que C espera. No los uses en firmas extern.

## Ejemplo: una función sigmoide

`exp` viene de `libm`. Con ella puedes construir funciones propias:

```lg
extern "C" fn exp(x: float) -> float

fn sigmoid(z: float) -> float:
    return 1.0 / (1.0 + exp(0.0 - z))

print(sigmoid(0.0))
print(sigmoid(2.0))
```

```salida
0.5
0.8807970779778823
```

Observa que `0.0 - z` se escribe con literales `float` en ambos lados, como exige el compilador.

## Qué bibliotecas están disponibles

El compilador enlaza cada programa con el runtime (`liblg_runtime.a`), con `libm` (`-lm`) y con la biblioteca estándar de C que aporta `cc`. Por eso puedes usar funciones como `sqrt`, `exp`, `pow`, `abs` o `rand`. No hay opción para enlazar bibliotecas adicionales: una función extern que no esté en esas bibliotecas falla al enlazar.

```lg
extern "C" fn pow(base: float, exponente: float) -> float

print(pow(2.0, 10.0))
```

```salida
1024.0
```

## Errores habituales

- **Olvidar `extern` en una función usada**: si llamas a `sqrt` sin declararla, sema dice que la función no existe.
- **Declarar una función extern con el mismo nombre que otra función tuya**: el compilador lo rechaza como función ya definida.
- **Pasar un tipo compuesto**: el programa puede compilar y fallar en ejecución o dar resultados absurdos.

## Ejercicios

1. Declara `extern "C" fn cos(x: float) -> float` y calcula `cos(0.0)`.
2. Escribe una función `hipotenusa(a: float, b: float) -> float` que use `sqrt`.

## Siguiente

[Capítulo 11. Errores y depuración](11-errores-y-depuracion.md)
