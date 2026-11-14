# Capítulo 11. Errores y depuración

Hay tres momentos en los que un programa puede fallar:

1. **Al leer el código** (errores de sintaxis): el parser no entiende la línea.
2. **Al comprobar tipos y reglas** (errores semánticos): el código se lee bien, pero no cumple las reglas del lenguaje. Estos errores también se reportan antes de generar código.
3. **Al ejecutarse** (errores de ejecución): una división por cero, un índice fuera de rango, una conversión imposible.

Los dos primeros impiden generar el ejecutable. El tercero termina el programa con código de salida 1 y un mensaje en stderr.

## Leer un error de compilación

Cada error tiene cuatro partes: la ubicación (archivo, línea y columna), el tipo de diagnóstico, el mensaje y una sugerencia. Además el compilador muestra la línea de código y un marcador `^` en la columna del error:

```lg
x = 1
mut y: int = x + "2"
```

```error
programa.lg:2:16: error: operador '+' no admite int y str
   2 | mut y: int = x + "2"
     |                ^
sugerencia: usa str(x) para convertir un valor a texto
```

El compilador imprime como máximo 20 errores y, si hay más, indica cuántos omitió (`demasiados errores, se omiten N más`). Corrige los errores de arriba hacia abajo. Un error temprano suele provocar errores falsos más abajo (por ejemplo, una variable que no se declaró porque su línea falló). El parser se recupera y reporta varios errores de sintaxis en una sola pasada, pero no siempre acierta con los siguientes.

### Errores de sintaxis típicos

Un `:` que falta al final de un `if`:

```lg
if true
    print(1)
```

```error
programa.lg:1:8: error: se esperaba ':' después de la condición del if
   1 | if true
     |        ^
sugerencia: añade ':' al final de la línea
```

Una cadena sin cerrar, que a menudo provoca errores adicionales porque el paréntesis queda abierto:

```lg
print("hola)
```

```error
programa.lg:1:7: error: cadena sin cerrar
   1 | print("hola)
     |       ^
sugerencia: cierra la cadena con " antes del fin de línea
programa.lg:1:6: error: '(' sin cerrar
   1 | print("hola)
     |      ^
sugerencia: añade ')' para cerrar el paréntesis
programa.lg:2:1: error: se esperaba ')' para cerrar la llamada, pero se encontró fin de línea
   2 | 
     | ^
sugerencia: revisa que cada '(' tenga su ')'
```

Un operador compuesto que no existe, como `+=`:

```lg
mut x = 1
x += 1
```

```error
programa.lg:2:4: error: se esperaba una expresión, pero se encontró '='
   2 | x += 1
     |    ^
```

## Errores semánticos frecuentes

| Mensaje (parte) | Causa habitual | Solución |
|---|---|---|
| `variable inmutable 'x'` | reasignar una variable sin `mut` | declárala con `mut` |
| `tipo esperado int, recibido str` | un valor de otro tipo en una declaración, un argumento o un `return` | convierte con `int(x)`, `str(x)`, o cambia el tipo |
| `variable no definida 'x'` | se usa antes de declararla o fuera de su bloque | declárala antes o en el ámbito correcto |
| `la función no puede usar la variable global 'x'` | una función lee una variable del nivel principal | pásala como parámetro |
| `la función 'f' no devuelve un valor en todos los caminos` | falta un `return` en una rama | añade el `return` que falta |
| `no se mezclan int y float` | aritmética entre `int` y `float` | convierte con `float(x)` o `int(x)` |
| `faltan campos en 'P': x` | una struct se construye sin todos sus campos | añade el campo |

Consulta [errores comunes](../referencia/errores-comunes.md) para la lista completa con sus causas y soluciones.

## Errores de ejecución

Un error de ejecución termina el programa con código 1 y escribe un mensaje en stderr. La salida que ya se escribió con `print` se conserva. Hay dos formas de mensaje:

- Con ubicación, para errores de aritmética y de formas de tensor: `programa.lg:2: error de ejecución: división por cero`. La ubicación indica la línea.
- Sin ubicación, para errores de colecciones y conversiones:

```lg
mut pila: List[int] = []
print(pila.pop())
```

```rterror
error de ejecución: pop de una lista vacía
```

Los errores de ejecución que cubre el lenguaje son estos:

| Mensaje | Cuándo ocurre |
|---|---|
| `división por cero` | `/` o `%` con divisor 0 (enteros) |
| `desbordamiento de entero en '+'` (o `-`, `*`) | el resultado no cabe en `int` |
| `índice N fuera de rango (longitud M)` | acceso a una lista fuera de sus límites |
| `clave no encontrada` | lectura `d[k]` de una clave que no existe |
| `pop de una lista vacía` | `pop()` sobre una lista vacía |
| `no se puede convertir 'x' a int` (o `float`) | `int(s)` o `float(s)` sobre texto no numérico |
| `forma incompatible: se esperaba ... pero el tensor tiene forma ...` | un tensor no cumple la forma declarada |
| `desbordamiento de la pila (recursión demasiado profunda)` | recursión sin caso base o demasiado profunda |
| `conversión de float a int fuera de rango: inf` | `int(x)` con un `float` que no cabe en `int`, `inf` o `nan` |
| `índice N fuera de rango (dimensión D de tamaño S)` | acceso a un tensor fuera de sus límites |

Un ejemplo de desbordamiento:

```lg
mut x = 9223372036854775807
x = x + 1
print(x)
```

```rterror
programa.lg:2: error de ejecución: desbordamiento de entero en '+'
```

`int` tiene 64 bits. El compilador comprueba cada suma, resta y multiplicación, así que nunca produce un valor incorrecto en silencio.

## Estrategias de depuración

### Imprime valores

La herramienta más útil es `print`. Puedes imprimir cualquier valor, incluidas listas, diccionarios, structs y tensores:

```lg
fn media(valores: List[float]) -> float:
    mut suma = 0.0
    for v in valores:
        suma = suma + v
    print("suma parcial:", suma, "de", len(valores))
    return suma / float(len(valores))

print(media([1.0, 2.0, 4.5]))
```

```salida
suma parcial: 7.5 de 3
2.5
```

### Reproduce el problema con un caso mínimo

Reduce el programa hasta la línea que falla y prueba con entradas pequeñas. Muchos errores de ejecución aparecen con valores límite: listas vacías, cero, o un número grande.

### Mira el código generado

`--emit-llvm` escribe el código intermedio de LLVM en `<salida>.ll` sin enlazar. Es útil para entender cómo el compilador traduce una construcción o para reportar un fallo del compilador:

```bash
./build/bin/lgc programa.lg --emit-llvm -o programa
less programa.ll
```

### Ajusta la optimización

Por defecto se usa `-O0`, que mantiene el código cerca de la fuente. Si un programa da resultados distintos con `-O0` y con `-O2`, repórtalo con un caso mínimo: es un error del compilador o del runtime, no un comportamiento esperado del lenguaje.

### Usa la semilla de randn

Los valores de `randn` dependen de la semilla. Para reproducir un fallo, deja la semilla por defecto o fija `LG_SEED` con el mismo valor en cada ejecución.

### Depurar con lldb

`lgc -g` añade información de depuración: lldb puede poner breakpoints por línea del archivo `.lg`, parar ahí y leer las variables y los parámetros.

```bash
./build/bin/lgc programa.lg -g -o programa
xcrun lldb ./programa
```

En macOS, `lgc -g` genera también `programa.dSYM`, que lldb usa automáticamente; no hace falta ninguna opción extra. Los comandos y las limitaciones (tipos opacos como `str` y `List`, solo lldb verificado) están en [depuración](../referencia/depuracion.md). Para un fallo que no necesita el depurador, combina `print` con la ubicación que informa el error de ejecución.

## Códigos de salida

| Código | Significado |
|---|---|
| 0 | compilación correcta o programa terminado bien |
| 1 | error de compilación, error de enlace o error de ejecución |
| 2 | uso incorrecto de `lgc` (opción desconocida o sin archivo) |

## Ejercicios

1. Provoca a propósito un error de división por cero dentro de una función y observa el mensaje.
2. Escribe un programa que lea de una lista de textos, convierta cada uno con `int(...)` y muestre cuál falla.

## Siguiente

[Capítulo 12. Memoria y conteo de referencias](12-memoria-conteo-de-referencias.md)
