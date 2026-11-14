# Capítulo 7. Structs e impl

Una `struct` agrupa campos con nombre y tipo. Un bloque `impl` añade métodos a una struct. Juntas permiten modelar datos propios, como un punto o una cuenta bancaria.

## Declarar una struct

```text
struct Punto:
    x: float
    y: float
```

Cada campo se escribe `nombre: tipo`, uno por línea. Los campos pueden tener cualquier tipo, incluidos listas, diccionarios, tensores u otras structs. Declarar una struct no imprime nada; sirve para crear valores:

```lg
struct Punto:
    x: float
    y: float

p = Punto(x: 3.0, y: 4.0)
print(p)
print(p.x)
```

```salida
Punto(x: 3.0, y: 4.0)
3.0
```

## Crear instancias

Una instancia se crea llamando al nombre de la struct con **argumentos con nombre**, uno por campo:

- Todos los campos son obligatorios. Un campo que falta es un error de compilación.
- No hay argumentos posicionales: `Punto(3.0, 4.0)` no es válido.
- Puedes escribir los campos en cualquier orden.

```lg
struct Persona:
    nombre: str
    edad: int

p = Persona(edad: 30, nombre: "Ana")
print(p)
```

```salida
Persona(nombre: "Ana", edad: 30)
```

Si falta un campo:

```lg
struct Persona:
    nombre: str
    edad: int

p = Persona(nombre: "Ana")
```

```error
programa.lg:5:5: error: faltan campos en 'Persona': edad
   5 | p = Persona(nombre: "Ana")
     |     ^
sugerencia: añade edad: <valor>
```

## Leer y escribir campos

Leer un campo es `objeto.campo`. Escribirlo exige que la variable sea `mut`:

```lg
struct Punto:
    x: float
    y: float

mut p = Punto(x: 1.0, y: 2.0)
p.x = 10.0
print(p)
```

```salida
Punto(x: 10.0, y: 2.0)
```

Sin `mut` el compilador lo rechaza:

```lg
struct Punto:
    x: float
    y: float

p = Punto(x: 1.0, y: 2.0)
p.x = 10.0
```

```error
programa.lg:6:1: error: variable inmutable 'p'
   6 | p.x = 10.0
     | ^
sugerencia: usa 'mut p' para poder modificarla
```

## Métodos con impl

Los métodos se declaran dentro de un bloque `impl NombreDeLaStruct:`. El primer parámetro es `self`, que representa el objeto sobre el que se llama:

```lg
struct Punto:
    x: float
    y: float

impl Punto:
    fn norma2(self) -> float:
        return self.x * self.x + self.y * self.y

p = Punto(x: 3.0, y: 4.0)
print(p.norma2())
```

```salida
25.0
```

Un método que **no** modifica el objeto se declara con `self`. Uno que sí lo modifica se declara con `mut self`. Solo los métodos `mut self` pueden asignar a campos de `self`, y solo se pueden llamar sobre variables `mut`:

```lg
struct Contador:
    valor: int

impl Contador:
    fn incrementa(mut self):
        self.valor = self.valor + 1

    fn leer(self) -> int:
        return self.valor

mut c = Contador(valor: 0)
c.incrementa()
c.incrementa()
print(c.leer())
```

```salida
2
```

Si intentas llamar a un método `mut self` sobre una variable inmutable, el error explica qué falta:

```lg
struct Contador:
    valor: int

impl Contador:
    fn incrementa(mut self):
        self.valor = self.valor + 1

c = Contador(valor: 0)
c.incrementa()
```

```error
programa.lg:9:1: error: 'incrementa' modifica el objeto y la variable 'c' es inmutable
   9 | c.incrementa()
     | ^
sugerencia: usa 'mut c' para poder modificarla
```

Los métodos se llaman siempre con argumentos posicionales. Los parámetros de un método siguen las mismas reglas que los de una función: son inmutables.

```lg
struct Saludo:
    texto: str

impl Saludo:
    fn decir(self, nombre: str) -> str:
        return self.texto + ", " + nombre

s = Saludo(texto: "Buenos días")
print(s.decir("Ana"))
```

```salida
Buenos días, Ana
```

Un método puede llamar a otro método de la misma struct con `self.otro()`:

```lg
struct Rectangulo:
    ancho: float
    alto: float

impl Rectangulo:
    fn area(self) -> float:
        return self.ancho * self.alto

    fn doble_area(self) -> float:
        return self.area() * 2.0

r = Rectangulo(ancho: 2.0, alto: 3.0)
print(r.area(), r.doble_area())
```

```salida
6.0 12.0
```

## Igualdad con eq

`==` y `!=` sobre structs solo funcionan si la struct define un método `eq` que reciba otro objeto de su mismo tipo y devuelva `bool`:

```lg
struct Punto:
    x: float
    y: float

impl Punto:
    fn eq(self, otro: Punto) -> bool:
        return self.x == otro.x and self.y == otro.y

print(Punto(x: 1.0, y: 2.0) == Punto(x: 1.0, y: 2.0))
print(Punto(x: 1.0, y: 2.0) != Punto(x: 1.0, y: 5.0))
```

```salida
true
true
```

Sin `eq`, comparar dos structs es un error de compilación.

## Structs dentro de structs y listas

Un campo puede ser otra struct, y una lista puede contener structs:

```lg
struct Punto:
    x: int
    y: int

struct Figura:
    nombre: str
    origen: Punto

f = Figura(nombre: "cuadrado", origen: Punto(x: 1, y: 2))
print(f.origen.x)
print(f)

puntos = [Punto(x: 1, y: 1), Punto(x: 2, y: 3)]
for p in puntos:
    print(p.x + p.y)
```

```salida
1
Figura(nombre: "cuadrado", origen: Punto(x: 1, y: 2))
2
5
```

## Modificar un campo a través de otro objeto

Para modificar `f.origen.x`, la cadena de campos debe partir de una variable `mut`. Este es el caso de `c.pos.mover(5)` en el ejemplo siguiente:

```lg
struct Dir:
    x: int
    y: int

impl Dir:
    fn mover(mut self, dx: int):
        self.x = self.x + dx

struct Coche:
    pos: Dir

impl Coche:
    fn avanza(mut self):
        self.pos.mover(5)

mut c = Coche(pos: Dir(x: 1, y: 2))
c.avanza()
print(c.pos.x)
```

```salida
6
```

## Reglas que conviene recordar

- Un método no puede tener el mismo nombre que un campo de la struct.
- No hay constructores con cuerpo ni valores por defecto: todos los campos se dan al crear la instancia.
- Para estructuras recursivas (un árbol, por ejemplo) usa un `List` del mismo tipo en un campo: `hijos: List[Nodo]`. Un campo de tipo `Nodo` dentro de `Nodo` se acepta, pero no hay forma de crear el primer valor, así que no sirve.
- Una struct es un objeto compartido: asignarla a otra variable no la copia. El capítulo 12 lo explica con ejemplos.

## Ejercicios

1. Declara `struct Cuenta` con `titular: str` y `saldo: float`. Añade un método `mut self` `ingresa(cantidad: float)` y muestra el saldo tras dos ingresos.
2. Declara `struct Punto` con `eq` y usa una lista de puntos para comprobar si un punto concreto está en ella con un bucle `for`.

## Siguiente

[Capítulo 8. Tensores](08-tensores.md)
