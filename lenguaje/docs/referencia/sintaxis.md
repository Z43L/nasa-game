# Referencia de sintaxis

Esta página describe la gramática que implementa el parser (`src/parser/parser.cpp`) y el analizador léxico (`src/lexer/lexer.cpp`). Las reglas de tipos que el analizador semántico aplica encima de esta gramática están en [tipos](tipos.md) y [operadores](operadores.md).

## Notación

- `"texto"` es un token literal.
- `IDENT`, `ENTERO`, `DECIMAL`, `CADENA` son tokens léxicos (abajo).
- `NL` es el fin de línea lógico, `INDENT` y `DEDENT` marcan el inicio y el fin de un bloque. Los produce el lexer; no se escriben.
- `[ x ]` es opcional, `{ x }` es repetición de cero o más, `x | y` es alternativa.

## Léxico

```ebnf
IDENT      = ( letra | "_" ) { letra | dígito | "_" } ;     (* solo ASCII; no es palabra reservada *)
ENTERO     = dígito { dígito } ;
DECIMAL    = dígito { dígito } "." dígito { dígito } [ exponente ]
           | dígito { dígito } exponente ;
exponente  = ( "e" | "E" ) [ "+" | "-" ] dígito { dígito } ;   (* "1e3", "2.5E-3" *)
CADENA     = '"' { carácter | escape } '"' ;                 (* no admite saltos de línea dentro *)
escape     = "\n" | "\r" | "\t" | "\\" | "\"" ;                  (* cualquier otro escape es error *)
comentario = "#" { carácter } NL_físico ;                    (* se ignora hasta fin de línea *)
```

Notas del léxico:

- Un decimal necesita dígitos a ambos lados del punto: `1.0` es válido, `1.` y `.5` no.
- `1..10` es un rango: el `.` seguido de otro `.` no forma parte de un número.
- Un entero que no cabe en 64 bits con signo es un error de compilación.
- Los tabuladores fuera de cadenas son errores; para indentar se usan espacios.
- Los caracteres no ASCII solo se admiten dentro de cadenas y comentarios.
- `;` y `!` (sin `=`) son errores de carácter.
- Las líneas dentro de `(`, `[` o `{` no generan `NL` ni `INDENT`/`DEDENT`, así que una llamada o un literal pueden ocupar varias líneas. Se permite una coma final antes del cierre.

### Operadores y separadores

```text
( ) [ ] { } , : . .. -> = == != < > <= >= + - * / % @
```

`+=`, `-=`, `**`, `&&`, `||` y `!` no existen.

## Programa

```ebnf
programa   = { declaracion | sentencia } ;
```

Las declaraciones pueden aparecer en cualquier orden al nivel principal. El compilador las recoge antes de analizar el código, así que una función puede llamarse antes de declararse. Las sentencias sueltas se ejecutan en orden al arrancar el programa.

Las declaraciones `struct`, `impl`, `extern` e `import` solo se admiten al nivel principal. Una sentencia `fn` dentro de un bloque declara una función anidada, visible solo en ese bloque (ver [tipos](tipos.md#funciones-anidadas-y-clausuras)).

## Declaraciones

```ebnf
declaracion = funcion | asincrona | struct | impl | extern | import ;

funcion     = "fn" IDENT "(" [ parametros ] ")" [ "->" tipo ] ":" bloque ;
asincrona   = "async" funcion ;   (* exige "-> tipo"; solo de nivel principal; llamarla devuelve Hilo[T] *)

extern      = "extern" CADENA "fn" IDENT "(" [ parametros ] ")" [ "->" tipo ] NL ;
              (* CADENA debe ser "C"; sin bloque *)

struct      = "struct" IDENT ":" NL INDENT { IDENT ":" tipo NL } DEDENT ;

impl        = "impl" IDENT ":" NL INDENT { metodo } DEDENT ;
metodo      = "fn" IDENT "(" [ parametros_metodo ] ")" [ "->" tipo ] ":" bloque ;

import      = "import" IDENT { "/" IDENT } NL ;

parametros        = parametro { "," parametro } [ "," ] ;
parametro         = IDENT ":" tipo ;
parametros_metodo = ( [ "mut" ] "self" | parametro ) { "," parametro } [ "," ] ;
```

Notas:

- Un parámetro normal no admite `mut`. En un método, `mut self` es el único lugar donde aparece `mut` en una lista de parámetros.
- `self` solo es válido como primer parámetro de un método dentro de un bloque `impl`.
- Una función sin `->` no devuelve valor.
- `extern` no admite cuerpo: el parser da error si encuentra `:`.
- `import` toma una ruta de identificadores separados por `/`, relativa a la carpeta del archivo que importa. No admite `..`.

## Tipos

```ebnf
tipo        = IDENT                                   (* int, float, bool, str o nombre de struct *)
            | "List" "[" tipo "]"
            | "Dict" "[" tipo "," tipo "]"
            | "Option" "[" tipo "]"
            | "Hilo" "[" tipo "]"                     (* hilo en curso o terminado (ver hilos.md) *)
            | "Canal" "[" tipo "]"                    (* canal de mensajes de tipo tipo *)
            | "Debil" "[" tipo "]"                    (* referencia débil a un valor heap *)
            | "Tensor" "[" tipo_elem "," "(" ENTERO { "," ENTERO } [ "," ] ")" "]"
            | "fn" "(" [ tipo { "," tipo } [ "," ] ] ")" [ "->" tipo ] ;   (* tipo de función; sin "->" es void *)
tipo_elem   = "int" | "float" ;                        (* el parser solo admite nombres int o float *)
```

Las dimensiones de un `Tensor` son enteros mayores que cero. Un `Tensor` necesita al menos una dimensión. El parser acepta una coma final: `Tensor[float, (2,)]` es un vector de longitud 2.

## Bloques

```ebnf
bloque      = NL INDENT sentencia { sentencia } DEDENT ;
```

Un bloque comienza tras `:` y siempre en una línea nueva, más indentada. Si el bloque no tiene sentencias, es un error de compilación: `if x:` sin cuerpo no es válido.

## Sentencias

```ebnf
sentencia   = si
            | mientras
            | para
            | "break" NL
            | "continue" NL
            | "return" [ expresion ] NL
            | funcion                                (* función anidada: solo visible en su bloque *)
            | declaracion_var
            | asignacion
            | expresion NL ;

si          = "if" expresion ":" bloque [ sino ] ;
sino        = "elif" expresion ":" bloque [ sino ]
            | "else" ":" bloque ;

mientras    = "while" expresion ":" bloque ;

para        = "for" IDENT "in" expresion [ ".." expresion ] ":" bloque ;

declaracion_var = [ "mut" ] IDENT [ ":" tipo ] "=" expresion NL ;

asignacion  = destino "=" expresion NL ;
destino     = IDENT
            | expresion_postfija "[" expresion { "," expresion } "]"
            | expresion_postfija "." IDENT ;
```

Notas:

- `elif` se convierte internamente en un `else` que contiene un `if`.
- En `para`, la forma `expresion ".." expresion` produce un rango, que solo es válido como iterable de `for`. En cualquier otro sitio es un error.
- `x = e` sin `mut` ni tipo es una declaración si `x` no existe en el ámbito visible, y una reasignación si existe. Si `x` es inmutable, la reasignación es un error. El analizador comprueba esto, no el parser.
- `mut x` y `x: T` necesitan `= valor`.
- Una sentencia `x = e` donde `x` no es un nombre sino una llamada u otra expresión es un error de destino de asignación.
- Asignar a `x.campo`, `lista[i]` o `t[i, j]` no declara nada: exige que la base sea mutable. Varios índices solo se admiten en tensores, y la asignación exige todos los índices (no un tensor parcial).

### Variables globales

Una variable `mut` de nivel principal es global: las funciones y los métodos del mismo archivo la leen y la asignan directamente, sin pasarla como parámetro:

```
mut cuenta = 0

fn sumar():
    cuenta = cuenta + 1

sumar()
print(cuenta)   # 1
```

- Solo las `mut` del mismo archivo son visibles desde una función. Un módulo importado no ve las variables del programa que lo importa.
- Dentro de una función, `x = e` asigna la global si `x` es una global `mut` visible y la función no la ha declarado antes en su cuerpo.
- La inicialización es la sentencia de nivel principal: hasta que se ejecuta, la global vale cero o `null`. Las globales no se liberan durante la ejecución: su memoria la recupera el sistema al terminar el programa.
- Una función lanzada con `hilo` o `async` no puede usar globales `mut` (error de compilación). Ver [hilos](hilos.md#globales-mut-y-hilos).

## Expresiones

```ebnf
expresion        = conjuncion { "or" conjuncion } ;
conjuncion       = negacion { "and" negacion } ;
negacion         = "not" negacion | comparacion ;
comparacion      = suma { ( "==" | "!=" | "<" | ">" | "<=" | ">=" | "in" ) suma } ;
suma             = termino { ( "+" | "-" ) termino } ;
termino          = unaria { ( "*" | "/" | "%" | "@" ) unaria } ;
unaria           = "-" unaria | "await" unaria | "hilo" postfija | postfija ;
postfija         = primaria { "(" argumentos ")" | "[" expresion { "," expresion } "]" | "." IDENT } ;
argumentos       = [ argumento { "," argumento } [ "," ] ] ;
argumento        = [ IDENT ":" ] expresion ;
primaria         = ENTERO | DECIMAL | CADENA | "true" | "false" | IDENT
                 | "Canal" "[" tipo "]" "(" ")"        (* canal vacío *)
                 | "(" expresion ")"
                 | "[" [ expresion { "," expresion } [ "," ] ] "]"
                 | "{" [ expresion ":" expresion { "," expresion ":" expresion } [ "," ] ] "}" ;
```

Notas:

- La precedencia va de menor a mayor: `or`, `and`, `not`, comparaciones, `+ -`, `* / % @`, `-` unario, postfijos. Detalle en [operadores](operadores.md).
- Las comparaciones se encadenan de izquierda a derecha: `a < b < c` es `(a < b) < c`, y como `a < b` es `bool`, el analizador lo rechaza.
- `t[i, j]` indexa tensores con un índice por dimensión; `lista[i, j]` es un error. Los índices de una cadena no existen.
- Los argumentos con nombre solo se aceptan en la construcción de structs (`Punto(x: 1.0, y: 2.0)`). El parser los acepta en cualquier llamada, pero el analizador los rechaza en funciones, métodos y builtins. Un argumento posicional no puede ir después de uno con nombre.
- Una lista o un diccionario vacíos (`[]`, `{}`) necesitan tipo por contexto: una anotación o un parámetro.
- `None` es una expresión primaria; su tipo lo fija el contexto (ver [tipos](tipos.md#option-t)). `Some(x)` es una llamada a un builtin.
- `hilo f(args)` lanza una función de nivel principal en un hilo nuevo y devuelve `Hilo[T]`. `await h` espera un `Hilo[T]` y devuelve `T`. Detalle en [hilos](hilos.md).

## Palabras reservadas

Ver [palabras reservadas](palabras-reservadas.md).

## Ejemplo completo de la gramática

```lg
struct Punto:
    x: float
    y: float

impl Punto:
    fn distancia2(self, otro: Punto) -> float:
        dx = self.x - otro.x
        dy = self.y - otro.y
        return dx * dx + dy * dy

fn es_cercano(p: Punto, q: Punto, umbral: float) -> bool:
    return p.distancia2(q) <= umbral * umbral

a = Punto(x: 0.0, y: 0.0)
b = Punto(x: 3.0, y: 4.0)
print(a.distancia2(b))
print(es_cercano(a, b, 6.0))
```

```salida
25.0
true
```
