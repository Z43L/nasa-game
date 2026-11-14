# Capítulo 1. Primeros pasos

En este capítulo escribes tu primer programa, lo compilas con `lgc` y ves cómo se leen los mensajes del compilador. Antes de empezar, compila el proyecto siguiendo el [README](../../README.md#compilación).

## Un programa es una lista de sentencias

Un archivo `.lg` contiene declaraciones (`fn`, `struct`, `impl`, `import`, `extern`) y sentencias sueltas. Las sentencias sueltas se ejecutan en orden, de arriba abajo, cuando arranca el programa:

```lg
print("Hola")
print("Adiós")
```

```salida
Hola
Adiós
```

## Sintaxis básica

- **Indentación con espacios.** Un bloque empieza después de una línea que termina en `:` y sigue con líneas más indentadas. El lenguaje no admite tabuladores.
- **Sin punto y coma.** Cada sentencia ocupa una línea. Para escribir dos sentencias en la misma línea hay un error de sintaxis.
- **Comentarios** con `#` hasta el final de la línea.

```lg
# Un comentario ocupa toda la línea o va al final de una sentencia.
print("con comentario al final") # esto se ignora
```

```salida
con comentario al final
```

## print

`print` acepta cualquier número de argumentos, los separa con un espacio y termina con un salto de línea. Sin argumentos imprime una línea vacía.

```lg
print("a", 1, true)
print()
print("fin")
```

```salida
a 1 true

fin
```

Un valor de tipo `str` se imprime tal cual. Dentro de listas, diccionarios y structs las cadenas aparecen entre comillas; eso lo verás en los capítulos 05 y 07.

## Compilar y ejecutar

El flujo normal es compilar con `lgc` y ejecutar el binario resultante:

```bash
./build/bin/lgc primeros.lg -o primeros
./primeros
```

`-o primeros` indica el nombre del ejecutable. Sin `-o`, el ejecutable toma el nombre del archivo sin extensión (`primeros`). Para ver el código intermedio de LLVM, usa `--emit-llvm`, que escribe `primeros.ll`.

## Leer un error de compilación

Si el programa tiene un error, `lgc` no genera ejecutable. Muestra la ubicación, el mensaje, la línea del código con un marcador y una sugerencia:

```lg
x: int = "hola"
print(x)
```

```error
programa.lg:1:10: error: tipo esperado int, recibido str
   1 | x: int = "hola"
     |          ^
sugerencia: usa int(x) para convertir el texto
```

El número `1:10` es línea 1, columna 10. El compilador puede reportar varios errores en una sola pasada. Los mensajes completos y sus soluciones están en [errores comunes](../referencia/errores-comunes.md).

## Un error de ejecución

Algunos errores solo aparecen al ejecutar. Por ejemplo, leer fuera de los límites de una lista. El programa termina con código distinto de cero y escribe el error en stderr:

```lg
numeros = [10, 20]
print(numeros[0])
print(numeros[5])
```

```rterror
error de ejecución: índice 5 fuera de rango (longitud 2)
```

La primera línea (`10`) se imprime antes del error: la salida ya escrita no se pierde.

## Primer programa completo

```lg
nombre = "Ana"
print("Hola,", nombre)
print("Tu nombre tiene", len(nombre), "letras")
```

```salida
Hola, Ana
Tu nombre tiene 3 letras
```

`len` cuenta bytes UTF-8. Con texto en español los acentos cuentan como dos bytes; lo verás en el capítulo 06.

## Ejercicios

1. Escribe un programa que imprima tu nombre y tu edad en una sola línea.
2. Escribe un programa que imprima tres líneas: una con un comentario, una vacía y una con `true`.

## Siguiente

[Capítulo 2. Variables y tipos](02-variables-y-tipos.md)
