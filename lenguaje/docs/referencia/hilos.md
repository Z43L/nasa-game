# Hilos, canales y async/await

Esta página describe la concurrencia del lenguaje: lanzar funciones en hilos, esperar su resultado, enviar valores entre hilos con canales y la azúcar `async`/`await`. Las referencias débiles (`Debil[T]`) están en [tipos](tipos.md#hilot-canalt-y-debilt).

Estado: implementado y verificado en macOS (arm64). En Linux compila con pthread según el diseño, pero **no está verificado**. Windows no se soporta.

## Hilo[T]: `hilo f(args)`

`hilo f(args)` ejecuta la función `f` en un hilo nuevo y devuelve un `Hilo[T]`, donde `T` es el tipo que devuelve `f`:

```lg
fn suma(a: int, b: int) -> int:
    return a + b

h = hilo suma(1, 2)
print(h.esperar())   # 3
```

```salida
3
```

Reglas:

- Solo se admiten funciones de nivel principal, definidas con `fn`. No se admiten clausuras (funciones anidadas), builtins, constructores de struct, métodos ni funciones `extern`.
- La función debe devolver un valor. Una función sin `->` es error: `'hilo' requiere una función que devuelva un valor`.
- Los argumentos se comprueban igual que en una llamada normal (número, tipos). Como en una llamada normal a una función, no se admiten argumentos con nombre.
- `h.esperar()` bloquea hasta que el hilo termina y devuelve su resultado de tipo `T`. Se puede llamar varias veces: devuelve el mismo valor.

Los argumentos heap (`str`, `List`, structs...) se comparten con el hilo mediante conteo de referencias: el hilo tiene su propia referencia y la suelta al terminar. El valor no se copia.

## Globales mut y hilos

Una función lanzada con `hilo f(...)` o `async fn` no puede usar una variable global `mut` del programa, ni directamente ni a través de las funciones y métodos que llama. Es error de compilación:

```
mut cuenta = 0

fn leer() -> int:
    return cuenta

fn f(x: int) -> int:
    return leer() + x

h = hilo f(1)   # error: la función 'f' lanzada con hilo no puede usar la variable global mutable 'cuenta'
```

El mensaje sugiere pasar el valor como parámetro y devolver el resultado. El hilo principal sí puede leer y asignar las globales. Limitación: la comprobación sigue las llamadas directas por nombre y las llamadas a métodos; no sigue las llamadas por valor (una función guardada en una variable `fn(...)`).

## await y async fn

`async fn` declara una función que, al llamarla, se ejecuta en un hilo nuevo. La llamada devuelve `Hilo[T]`; `await h` espera ese hilo y devuelve `T`:

```lg
async fn cuadrado(x: int) -> int:
    return x * x

r = await cuadrado(7)        # 49
h = cuadrado(3)              # Hilo[int], el hilo ya está en marcha
print(await h)               # 9
```

```salida
9
```

`await h` equivale exactamente a `h.esperar()`. Reglas:

- `async fn` solo se declara al nivel principal y exige `-> tipo`.
- `await` solo admite valores `Hilo[T]`.

**Lo que no es async/await:** no hay ejecutor, ni planificador, ni cooperación. Cada llamada a una `async fn` crea un hilo del sistema; no hay tareas ligeras ni un bucle de eventos que reparta trabajo entre llamadas. Muchas llamadas a la vez significan muchos hilos. Esta es la forma más simple de tener `async`/`await` y es intencional en esta versión.

## Canales: Canal[T]

Un canal transmite valores de tipo `T` entre hilos:

```lg
c = Canal[int]()
c.enviar(42)
r = c.recibir()             # Option[int]: Some(42)
c.cerrar()
print(r.desempacar_o(0))     # 42
```

```salida
42
```

| Operación | Resultado | Descripción |
|---|---|---|
| `Canal[T]()` | `Canal[T]` | Crea un canal vacío y abierto. |
| `c.enviar(x)` | `void` | Envía `x` (de tipo `T`). No bloquea: la cola crece sin límite. Enviar en un canal cerrado es un error de ejecución. |
| `c.recibir()` | `Option[T]` | Bloquea hasta que hay un valor (`Some(x)`) o hasta que el canal está cerrado y vacío (`None`). |
| `c.cerrar()` | `void` | Cierra el canal. Los valores ya enviados siguen disponibles para `recibir`. |

Los canales son el único punto de sincronización entre hilos. Un canal cerrado y vacío devuelve `None`, así que un consumidor puede terminar sin contar cuántos mensajes llegarán:

```lg
c = Canal[int]()
c.enviar(1)
c.enviar(2)
c.cerrar()

mut fin = false
while not fin:
    r = c.recibir()
    if r.es_some():
        print(r.desempacar_o(0))
    else:
        fin = true
```

```salida
1
2
```

Ejemplo completo: cuatro hilos envían sumas parciales por un canal y el hilo principal las recoge:

```lg
fn trabajador(id: int, c: Canal[int]) -> int:
    mut s = 0
    for i in 0..1000:
        s = s + i * id
    c.enviar(s)
    return s

c = Canal[int]()
mut hs: List[Hilo[int]] = []
for k in 1..5:
    hs.append(hilo trabajador(k, c))

mut recibido = 0
for i in 0..4:
    recibido = recibido + c.recibir().desempacar_o(0)
print(recibido)              # 4995000
```

```salida
4995000
```

Los `Canal` se pueden pasar como argumento a una función lanzada con `hilo` o `async`.

## Garantías y límites

- **Un hilo por llamada.** No hay grupos de hilos ni límite de hilos activos. Lanzar miles de hilos consume memoria del sistema.
- **Soltar un hilo espera a que termine.** Si el `Hilo[T]` deja de referenciarse sin haberse esperado, el programa espera a que el hilo acabe antes de seguir. Una llamada `hilo f()` cuyo resultado no se guarda ejecuta `f` y espera, así que no hay concurrencia real si no guardas el `Hilo`.
- **Errores de ejecución en hilos.** Un error de ejecución dentro de un hilo (división por cero, índice fuera de rango, envío en canal cerrado...) imprime el mensaje con su ubicación y termina **todo el programa** con código 1. No hay captura de errores.
- **Globales mutables.** Una función lanzada no puede usar globales `mut` (ver [Globales mut y hilos](#globales-mut-y-hilos)). Esto evita carreras sobre variables del programa.
- **Objetos compartidos sin cerrojos.** Las listas, los diccionarios y los tensores no tienen sincronización. Si dos hilos modifican el mismo objeto, hay una carrera de datos. Pasa el valor por un canal o trabaja con copias inmutables (`str`, valores escalares, objetos que no cambian después de crearse). Los conteos de referencias sí son atómicos.
- **Deadlock posible.** Un `recibir` sin remitente vivo, o un `esperar` sobre un hilo bloqueado, espera para siempre. No hay tiempos de espera ni `select`.
- **Pila de hilo.** Cada hilo tiene 8 MiB de pila. La recursión muy profunda dentro de un hilo termina el proceso por una señal, sin el mensaje de "desbordamiento de la pila" (en macOS, código de salida 132). El mensaje solo lo da el hilo principal.
- **Plataformas.** Usa pthread: macOS y Linux. Windows no se soporta.

## Verificación

- Pruebas: `tests/unit/concurrencia_test.cpp` (sintaxis y sema) y `tests/e2e/conc_*.lg`, `tests/e2e/debil_*.lg`.
- Carrera: el programa de cuatro hilos se ejecutó 50 veces con la misma salida.
- ThreadSanitizer: se enlaza a mano el objeto generado con el runtime compilado con `-fsanitize=thread`. Ejemplo:

```text
lgc programa.lg -o programa --keep-obj
cc -g -O1 -fsanitize=thread -I src/runtime -o programa_tsan programa.o \
   src/runtime/lg_rt.c src/runtime/std/hilos.c src/runtime/std/datos.c src/runtime/std/red.c -lm
./programa_tsan
```

## Ver también

- [Tipos](tipos.md): `Hilo[T]`, `Canal[T]` y `Debil[T]` en la tabla de tipos.
- [Limitaciones](../limitaciones.md): lo que el lenguaje no implementa.
