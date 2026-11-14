# Errores comunes

Cada error de compilación tiene este formato:

```text
archivo:línea:columna: error: mensaje
   línea | código fuente
         | ^
sugerencia: cómo corregirlo
```

Los errores de ejecución tienen la forma `error de ejecución: mensaje`, y van precedidos de `archivo:línea:` cuando el compilador conoce la línea. El programa termina con código 1 en ambos casos de error de ejecución. Los errores de uso de `lgc` (opción desconocida, falta el archivo) terminan con código 2 o 1, según el caso.

Las tablas siguen el orden de las fases del compilador: léxico, sintaxis, semántica, código generado y ejecución. Los mensajes van exactamente como los escribe el compilador; las partes entre `<>` cambian según el código.

## Léxico

| Mensaje | Causa | Solución |
|---|---|---|
| `tabulación no permitida` | un tabulador en la indentación o en el código | usa espacios |
| `indentación inconsistente` | una línea no coincide con ningún nivel de indentación abierto | alinea la línea con un bloque abierto |
| `cadena sin cerrar` | falta la comilla de cierre antes del fin de línea | cierra la cadena con `"` |
| `secuencia de escape desconocida '\<x>'` | escape distinto de `\n`, `\r`, `\t`, `\\` y `\"` | usa un escape válido |
| `carácter inesperado ';'` | punto y coma | termina la sentencia con un salto de línea |
| `carácter inesperado '!'` | `!` sin `=` | usa `not` para negar, `!=` para distinto |
| `carácter inesperado '<x>'` | un carácter que no forma parte del lenguaje | elimínalo |
| `carácter no válido fuera de una cadena` | un carácter no ASCII en el código | usa ASCII fuera de cadenas y comentarios |
| `entero demasiado grande: <n>` | un literal que no cabe en `int` | el máximo es `9223372036854775807` |
| `'(' sin cerrar` (o `'['`, `'{'`) | un paréntesis abierto sin su cierre | añade el cierre |
| `literal numérico no válido '1.5e'` | exponente sin dígitos, o un literal con prefijo como `0x1` | escribe el literal completo, p. ej. `1e3` o `1` |
| `demasiados errores, se omiten N más` | el compilador imprime como máximo 20 errores | corrige los primeros y vuelve a compilar |
| `')' sin '(' correspondiente` | un cierre sin apertura | elimínalo o abre antes |
| `')' no coincide con '[' abierto en la línea <n>` | cierre de otro tipo | cierra primero el abierto |

## Sintaxis

| Mensaje | Causa | Solución |
|---|---|---|
| `se esperaba ':' después de 'else'` | `else if` (el lenguaje usa `elif`) | escribe `elif cond:` |
| `se esperaba ':' después de la condición del if` | falta `:` en `if`, `elif`, `while`, `for` o `fn` | añade `:` al final de la línea |
| `se esperaba un bloque indentado después de ':'` | la línea siguiente no está más indentada | escribe el cuerpo indentado |
| `indentación inesperada` | una línea está más indentada sin un `:` previo | quita espacios o termina la línea anterior en `:` |
| `'else' sin 'if' correspondiente` (o `'elif'`) | un `else` no sigue a un `if` del mismo nivel | ponlo justo después del bloque del `if` |
| `'struct' solo puede declararse a nivel superior` (o `'impl'`, `'extern'`, `'import'`) | una declaración dentro de un bloque | sácala al nivel principal |
| `se esperaba fin de línea, pero se encontró el identificador 'y'` | dos sentencias en una línea, o un punto y coma implícito | pon cada sentencia en su línea |
| `se esperaba una expresión, pero se encontró '='` | un operador que no existe, como `+=` | escribe `x = x + 1` |
| `se esperaba el nombre de la función, pero se encontró '='` | `fn` usado como nombre | `fn` no puede ser un nombre |
| `se esperaba '=' en la declaración de 'x'` | `mut x` o `x: T` sin valor inicial | añade `= valor` |
| `'mut' solo puede usarse como 'mut self' en la firma de un método` | `mut` en un parámetro normal | declara `nombre: tipo` y copia en una variable `mut` |
| `'self' solo es válido en métodos` | `self` como primer parámetro de una función | mueve la función a un `impl` |
| `las funciones extern no tienen cuerpo` | `:` tras la firma de un `extern` | quita los dos puntos |
| `destino de asignación no válido` | asignar a algo que no es variable, índice o campo | asigna a una variable, `lista[i]` o `objeto.campo` |
| `'..' solo puede usarse en la cabecera de un for` | un rango fuera de `for` | escribe `for i in 0..n:` |
| `argumento posicional después de un argumento con nombre` | `f(a: 1, 2)` | pon primero los posicionales |
| `en un bloque 'impl' solo se permiten funciones` | una sentencia dentro de `impl` | declara solo métodos con `fn` |

## Semántica: variables y ámbitos

| Mensaje | Causa | Solución |
|---|---|---|
| `variable inmutable 'x'` | reasignar una variable sin `mut` | declárala con `mut` |
| `variable no definida 'x'` | uso antes de declarar o fuera de su bloque | declara la variable en el ámbito correcto |
| `'x' ya está declarada en este ámbito` | declarar dos veces en el mismo ámbito | reasigna o usa otro nombre |
| `'x' es un nombre reservado` | nombre de función integrada o `float`, `bool`, etc. | usa otro nombre |
| `la función no puede usar la variable global 'x'` | una función usa una variable inmutable del nivel principal, o una `mut` de otro archivo | declara la variable con `mut` en el mismo archivo, o pásala como parámetro |
| `la función 'f' lanzada con hilo no puede usar la variable global mutable 'x': las globales mutables no son seguras entre hilos` | `hilo f(...)` o `async fn` llega, directa o indirectamente, a una global `mut` | pasa el valor como parámetro y devuelve el resultado; ver [hilos](hilos.md#globales-mut-y-hilos) |
| `'x' es una variable, no una función` | llamar a una variable con paréntesis | quita los paréntesis |
| `función no definida 'f'` | llamada a algo que no existe | declara la función o revisa el nombre |
| `'x' no es un valor` | usar una función o builtin sin llamarlo | añade los paréntesis |
| `'x' es una struct, no un valor` | usar el nombre de una struct como valor | crea una instancia con `x(campo: valor)` |
| `variable inmutable 'self'` | asignar a un campo de `self` sin `mut self` | declara el método con `mut self` |
| `variable inmutable 'x'` (parámetro) | reasignar un parámetro | copia el valor en una variable `mut` |
| `no se puede asignar un valor void a 'x'` | `x = f()` con `f` sin retorno | la función no devuelve valor |

## Semántica: tipos y conversiones

| Mensaje | Causa | Solución |
|---|---|---|
| `tipo esperado int, recibido str` | un valor de otro tipo en declaración, argumento, retorno o asignación | convierte con `int(...)`, `str(...)` o `float(...)` |
| `tipo esperado float, recibido int` | `x: float = 1` | escribe `1.0` o usa `float(1)` |
| `tipo desconocido 'Foo'` | un tipo que no existe | usa `int`, `float`, `bool`, `str`, una struct, `List`, `Dict` o `Tensor` |
| `Tensor solo admite elementos int o float` | `Tensor[bool, ...]` | usa `int` o `float` |
| `clave de Dict no válida: float` | clave que no es `int`, `str` ni `bool` | usa otra clave |
| `no se mezclan int y float en '<op>'` | aritmética o comparación entre `int` y `float` | convierte con `float(x)` o `int(x)` |
| `operador '<op>' no admite <A> y <B>` | operador sin sentido para esos tipos | revisa la [tabla de operadores](operadores.md) |
| `'and' requiere bool, recibido ...` | `and`/`or` sobre no booleanos | usa comparaciones |
| `'not' requiere bool, recibido int` | `not` sobre no booleano | usa `not` sobre `bool` |
| `expresión demasiado anidada (límite 1000 niveles)` | paréntesis, corchetes o llamadas anidados más de 1000 veces | simplifica la expresión con variables intermedias |
| `bloque demasiado anidado (límite 1000 niveles)` | bloques indentados anidados más de 1000 veces | reestructura el código en funciones |
| `'-' requiere int o float, recibido str` | negación de un tipo no numérico | aplica `-` solo a números |
| `'%' solo admite int, ...` | `%` sobre `float` | usa `int` |
| `no se pueden comparar int y bool` | comparar tipos distintos | compara del mismo tipo |
| `no se puede comparar List[int] con ==` | comparar listas, diccionarios o tensores | compara elementos uno a uno |
| `no se puede comparar 'P' con ==` | struct sin `eq` | define `fn eq(self, otro: P) -> bool` en el `impl` |
| `'in' requiere un Dict o una List a la derecha, recibido str` | `in` sobre otro tipo | usa `in` con listas o diccionarios |
| `la condición de 'if' debe ser bool, recibido int` | condición no booleana | usa una comparación |
| `la condición de 'while' debe ser bool, recibido int` | ídem en `while` | ídem |
| `'for' necesita un rango o una List, recibido Dict[str, int]` | `for` sobre un diccionario u otro tipo | recorre una lista de claves |
| `un rango solo es válido como iterable de 'for'` | `0..n` fuera de un `for` | usa `for i in 0..n:` |
| `la lista vacía necesita un tipo` | `[]` sin contexto | anota: `x: List[int] = []` |
| `el diccionario vacío necesita un tipo` | `{}` sin contexto | anota: `d: Dict[str, int] = {}` |
| `no se puede indexar un valor de tipo str` | indexar una cadena | las cadenas no se indexan; los tensores se indexan con `t[i, j]` |
| `no se puede usar un valor void en una operación` | un `void` como operando | la función no devuelve valor |
| `no se puede poner un valor void en una lista` | `[f()]` con `f` sin retorno | ídem |

## Semántica: funciones y retornos

| Mensaje | Causa | Solución |
|---|---|---|
| `'f' espera 2 argumento(s), recibió 3` | número incorrecto de argumentos | ajusta la llamada |
| `la función 'f' no admite argumentos con nombre` | `f(a: 1)` sobre una función | usa argumentos posicionales |
| `el método 'm' no admite argumentos con nombre` | `obj.m(a: 1)` | usa argumentos posicionales |
| `la función 'f' no devuelve un valor en todos los caminos` | falta un `return` en alguna rama | añade un `return` a cada camino |
| `'return' fuera de una función` | `return` en el nivel principal | quítalo o mueve el código a una función |
| `la función no declara tipo de retorno` | `return valor` en una función sin `->` | añade `-> tipo` |
| `'break' fuera de un bucle` (o `'continue'`) | `break` fuera de `for` o `while` | usa `break` solo dentro de un bucle |
| `el método 'f' debe recibir 'self' como primer parámetro` | `impl` con un método sin `self` | añade `self` al principio |
| `'x' ya está definida` | función, struct o `impl` duplicados | cambia el nombre |
| `el método 'm' ya está definido en 'T'` | dos métodos con el mismo nombre | cambia el nombre |
| `no se puede saber el tipo de None` | `None` sin contexto `Option[T]`, p. ej. `x = None` | anota la variable: `x: Option[int] = None` |
| `tipo esperado Option[int], recibido int` | un valor sin envolver donde se espera un `Option` | usa `Some(x)` |
| `variable no definida 'y': su ámbito ya terminó` | se usa una variable de un bloque que ya terminó, desde una función declarada después | declara la variable en el ámbito que la usa, o pásala como parámetro |
| `función no definida 'g'` | se llama a una función anidada fuera del bloque donde se declaró | la función solo existe dentro de su bloque |

## Semántica: structs y métodos

| Mensaje | Causa | Solución |
|---|---|---|
| `faltan campos en 'P': x` | una construcción sin todos los campos | añade los campos |
| `los argumentos de 'P' necesitan nombre` | `P(1, 2)` | usa `P(campo: valor, ...)` |
| `la struct 'P' no tiene el campo 'z'` | acceso a un campo inexistente | revisa los campos listados en la sugerencia |
| `la struct 'P' no tiene el método 'm'` | llamada a un método no definido | define `fn m` en el `impl` |
| `'m' es un método de 'P', ...` | leer un método como campo | llámalo con paréntesis |
| `'m' es un campo de 'P', no un método` | llamar a un campo | quita los paréntesis |
| `campo 'x' repetido en 'P'` | dos campos con el mismo nombre | renombra uno |
| `impl de tipo desconocido 'Q'` | `impl` de una struct que no existe | declara primero la struct |
| `'m' modifica el objeto y la variable 'p' es inmutable` | llamar a un método `mut self` sobre una variable inmutable | declara `mut p` |
| `'m' modifica un valor temporal` | método `mut self` sobre un resultado de función | guarda el valor en una variable `mut` |
| `no se puede asignar a esta expresión` | destino de asignación no válido | asigna a una variable, índice o campo |

## Semántica: tensores y builtins

| Mensaje | Causa | Solución |
|---|---|---|
| `forma de tensor incompatible: se esperaba Tensor[...], recibido Tensor[...]` | forma distinta de la declarada | ajusta la forma |
| `formas de tensor incompatibles en '+': ...` | suma de tensores con formas distintas | usa tensores con la misma forma |
| `tensores de distinto tipo de elemento en '+': ...` | `Tensor[int]` con `Tensor[float]` | convierte un lado |
| `operador '/' no admite Tensor[...] y Tensor[...]` | división entre tensores | no está soportada |
| `no se mezclan int y Tensor[float, (2, 2)] en '*'` | escalar `int` con tensor de `float` | usa un escalar `float` (`2.0`) |
| `demasiados índices para un tensor de rango 2: 3 índices recibidos` | más índices que dimensiones | indexa con una coordenada por dimensión |
| `demasiados índices para List[int]` | `lista[i, j]` | las listas usan un solo índice; los tensores admiten `t[i, j]` |
| `asignación a un tensor parcial` | `t[i] = v` con menos índices que dimensiones | asigna con todos los índices: `t[i, j] = v` |
| `índice 2 fuera de rango (dimensión 0 de tamaño 2)` | índice de tensor fuera de la dimensión (en compilación si es literal) | usa índices entre `-tamaño` y `tamaño - 1` |
| `formas incompatibles para '@': ...` | multiplicación con dimensiones que no encajan | las columnas izquierdas deben igualar las filas derechas |
| `'@' requiere Tensor[float]` | `@` sobre `Tensor[int]` | usa `float` |
| `transpose requiere un tensor de rango 2, recibido ...` | transponer un tensor de rango 3 o más | usa rango 2 |
| `'zeros' necesita al menos una dimensión` | `zeros()` sin argumentos | indica una dimensión |
| `las dimensiones deben ser int, recibido List[int]` | `zeros([2, 3])` | usa `zeros(2, 3)` |
| `from_list: la lista tiene N elementos pero la forma requiere M` | número de elementos distinto del producto | ajusta la lista o las dimensiones |
| `from_list espera List[int] o List[float], recibido ...` | `from_list` con otro tipo de lista | usa una lista de números |
| `len no admite int` | `len` sobre un tipo no admitido | `len` acepta `List`, `Dict`, `str` o `Tensor` |
| `el tipo List[int] no tiene el método 'push'` | método inexistente | revisa la [lista de métodos](builtins.md) |
| `'tiene' espera 1 argumento(s), recibió 0` | número de argumentos incorrecto | pasa la clave |

## Imports y módulos

| Mensaje | Causa | Solución |
|---|---|---|
| `no se puede abrir el módulo 'x': no se encuentra en ninguna ruta de búsqueda` | ningún `x.lg` está en la carpeta del archivo que importa, en `LG_PATH`, en `-I` ni en `stdlib/` (el error lista las rutas probadas) | crea `x.lg` en una de esas carpetas, añade su carpeta con `LG_PATH` o `-I`, o declara la dependencia en `lg.toml` con `lgpm añadir` |
| `import circular: 'x.lg' se importa a sí mismo` | dos módulos que se importan mutuamente | elimina uno de los imports del ciclo |
| `'import' solo puede declararse a nivel superior` | `import` dentro de un bloque | sácalo al principio del archivo |

## Errores que puede dar el generador de código

Estos mensajes salen en la fase de generación, después de un análisis que no encontró errores. Son limitaciones conocidas, no errores del programa:

| Mensaje | Causa | Solución |
|---|---|---|
| `operador no soportado para el tipo str` | `<`, `>`, `<=` o `>=` sobre `str` | compara la longitud o el contenido de otra forma; ver [limitaciones](../limitaciones.md) |
| `str() no aplicable a List[int]` | `str(...)` sobre un tipo que no es escalar | construye el texto a mano con los elementos |

## Errores de ejecución

| Mensaje | Causa | Solución |
|---|---|---|
| `error de ejecución: división por cero` | `/` o `%` con divisor 0 con `int` | comprueba el divisor antes |
| `error de ejecución: desbordamiento de entero en '<op>'` | resultado fuera de `int` | usa valores menores |
| `error de ejecución: índice N fuera de rango (longitud M)` | acceso a una lista fuera de sus límites | comprueba `len(lista)` |
| `error de ejecución: clave no encontrada` | `d[k]` sin la clave | comprueba con `d.tiene(k)` antes |
| `error de ejecución: desbordamiento de la pila (recursión demasiado profunda)` | recursión sin fin o demasiado profunda | añade un caso base o convierte la recursión en un bucle |
| `error de ejecución: conversión de float a int fuera de rango: inf` | `int(x)` con `inf`, `nan` o un valor que no cabe en `int` | comprueba el rango antes de convertir |
| `error de ejecución: pop de una lista vacía` | `pop()` sin elementos | comprueba `len(lista) > 0` |
| `error de ejecución: no se puede convertir 'x' a int` (o `float`) | `int(s)` o `float(s)` con texto inválido | valida el texto antes |
| `error de ejecución: forma incompatible: se esperaba (2, 3) pero el tensor tiene forma (2, 2)` | las dimensiones de variables no coinciden con la declaración | ajusta las variables |
| `error de ejecución: dimensión negativa (-1) en la forma del tensor` | `zeros` o `ones` con dimensión negativa | usa dimensiones mayores o iguales que cero |
| `error de ejecución: la lista tiene N elementos pero la forma ... requiere M` | `from_list` con lista de longitud variable | ajusta la lista o las dimensiones |

Ejemplo de un error de ejecución con ubicación:

```lg
mut z = 0
print(10 / z)
```

```rterror
programa.lg:2: error de ejecución: división por cero
```
