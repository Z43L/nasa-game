# Capítulo 6. Cadenas

Una cadena (`str`) es texto UTF-8 inmutable. Se crea con comillas dobles y se combina con `+`. Las cadenas no se pueden modificar en su sitio: cada operación produce una cadena nueva.

## Literales y escapes

```lg
print("hola")
print("tab:\tfin")
print("barra: \\ comillas: \"x\"")
print("dos\nlíneas")
```

```salida
hola
tab:	fin
barra: \ comillas: "x"
dos
líneas
```

Los escapes válidos son `\n` (salto de línea), `\r` (retorno de carro), `\t` (tabulador), `\\` (barra invertida) y `\"` (comilla doble). Cualquier otra barra invertida es un error de compilación. Una cadena no puede ocupar varias líneas del código fuente: el salto de línea cierra la cadena.

## Concatenar y comparar

El operador `+` concatena dos cadenas. Los operadores `==` y `!=` comparan contenido:

```lg
a = "hola"
b = a + " mundo"
print(b)
print(a == "hola")
print(a != b)
```

```salida
hola mundo
true
true
```

`+` solo acepta dos `str`. Para unir un número hay que convertirlo antes:

```lg
edad = 30
print("Tengo " + str(edad) + " años")
```

```salida
Tengo 30 años
```

Mezclar tipos da error:

```lg
edad = 30
print("Tengo " + edad)
```

```error
programa.lg:2:16: error: operador '+' no admite str y int
   2 | print("Tengo " + edad)
     |                ^
sugerencia: usa str(x) para convertir un valor a texto
```

## Longitud

`len(texto)` devuelve el número de **bytes** UTF-8, no de caracteres. Las letras sin acento ocupan un byte; la `ñ` y las vocales acentuadas ocupan dos:

```lg
print(len("abc"))
print(len("ñu"))
```

```salida
3
3
```

## Convertir texto a números

```lg
print(int("42") + 1)
print(float("3.5") * 2.0)
```

```salida
43
7.0
```

Convertir un texto que no es un número termina el programa con error de ejecución (`no se puede convertir 'abc' a int`). Para convertir un número a texto usa `str(x)`, que acepta `int`, `float`, `bool` y `str`:

```lg
print(str(3.0))
print(str(true))
```

```salida
3.0
true
```

## Lo que no hay

Las cadenas no se pueden indexar ni recorrer con `for`, no tienen métodos y no admiten `in`. Estas operaciones dan error de compilación:

```lg
s = "hola"
print(s[0])
```

```error
programa.lg:2:7: error: no se puede indexar un valor de tipo str
   2 | print(s[0])
     |       ^
sugerencia: solo List y Dict admiten []
```

No hay forma de leer un carácter ni de trocear una cadena: el lenguaje todavía no ofrece esas operaciones (ver [limitaciones](../limitaciones.md)).

Tampoco se admite el orden alfabético con `<`:

```lg
print("a" < "b")
```

```error
programa.lg:1:11: error: operador no soportado para el tipo str
   1 | print("a" < "b")
     |           ^
```

## Cadenas dentro de contenedores

Cuando una cadena aparece dentro de una lista, un diccionario o una struct, se imprime entre comillas. Una cadena suelta se imprime sin comillas:

```lg
nombres = ["ana", "luis"]
print(nombres)
print(nombres[0])
```

```salida
["ana", "luis"]
ana
```

## Ejemplo: formatear una línea

```lg
producto = "café"
precio = 2.5
cantidad = 3
total = precio * float(cantidad)
print("Producto: " + producto + " | total: " + str(total))
```

```salida
Producto: café | total: 7.5
```

## Ejercicios

1. Crea dos cadenas con tu nombre y tu apellido y muestra el nombre completo separado por un espacio.
2. Escribe un programa que calcule la longitud en bytes de una frase con acentos y compárala con la de la misma frase sin acentos.

## Siguiente

[Capítulo 7. Structs e impl](07-structs-e-impl.md)
