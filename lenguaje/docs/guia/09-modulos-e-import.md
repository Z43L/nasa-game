# Capítulo 9. Módulos e import

Un programa puede repartirse en varios archivos `.lg`. Un archivo importa otro con `import`, y las funciones, structs y sentencias del archivo importado pasan a estar disponibles en el programa.

## Importar un módulo

Un módulo es un archivo `.lg`. Se importa escribiendo su ruta sin la extensión, usando `/` para las carpetas. La ruta es relativa a la carpeta del archivo que hace el `import`:

```text
proyecto/
  programa.lg
  modulos/
    mates.lg
```

```lg archivo=modulos/mates.lg
fn doble(x: int) -> int:
    return x * 2
```

```lg
import modulos/mates

print(doble(21))
```

```salida
42
```

El `import` debe estar al nivel principal del archivo: dentro de un bloque da error.

## Dónde se buscan los módulos

Para un `import a/b`, `lgc` busca `a/b.lg` en estas carpetas, en este orden, y usa la primera que lo contiene:

1. La carpeta del archivo que hace el `import`.
2. Las carpetas de la variable de entorno `LG_PATH`, separadas por `:`, en orden.
3. Las carpetas de las opciones `-I`, en el orden de la línea de comandos.
4. La biblioteca estándar, `stdlib/` (`import mat`, `import red`, `import datos`).

Por eso un módulo de la carpeta del archivo tapa a uno con el mismo nombre en otra carpeta, y `LG_PATH` gana a `-I`.
Si ninguna carpeta lo contiene, el error lista todas las rutas probadas.

```bash
LG_PATH=$HOME/libs ./build/bin/lgc programa.lg -I ./vendor -o programa
```

Dentro de un proyecto no hace falta escribir estas rutas a mano: `lgpm` pasa a `lgc` una carpeta de búsqueda por cada dependencia.

## Dependencias con lgpm

Un proyecto creado con `lgpm nuevo` declara sus bibliotecas en `lg.toml`, por ruta o por git:

```toml
[dependencias]
mates = { ruta = "../libs/mates" }
```

Después, `import mates` en `src/main.lg` encuentra `mates.lg` en la carpeta `src/` de esa biblioteca (o en su raíz, si no tiene `src/`). `lgpm construir` y `lgpm ejecutar` pasan las rutas a `lgc` automáticamente.

Las dependencias no son transitivas: si `mates` importa a su vez otra biblioteca, esa biblioteca también debe aparecer en el `lg.toml` del proyecto. Detalles en [herramientas](../referencia/herramientas.md#lgpm).

## Qué se incorpora

Un `import` trae al programa todo lo que está al nivel principal del módulo:

- funciones, structs e `impl`;
- las sentencias sueltas, que se ejecutan antes que las del archivo que importa.

Hay espacios de nombres por módulo. El alias es el último segmento de la ruta: con `import mates`, sus funciones se llaman `mates.suma(...)` y sus structs `mates.Punto`. Un nombre sin cualificar sigue funcionando si es único entre los módulos cargados; si es ambiguo, el compilador da error y sugiere usar el alias. Un archivo no puede importar dos módulos con el mismo último segmento (no hay `as`).

## Orden de ejecución

El compilador carga primero las dependencias, en profundidad, y cada módulo se carga una sola vez aunque se importe varias veces. Las sentencias de cada módulo se ejecutan en ese orden, y al final las del programa principal.

```lg archivo=otro.lg
print("cargando otro")
fn util() -> int:
    return 7
```

```lg archivo=sub/tercero.lg
fn sumar(a: int) -> int:
    return a + 7
```

```lg
import otro
import sub/tercero
print("principal")
print(sumar(1))
print(util())
```

```salida
cargando otro
principal
8
7
```

Observa que `sub/tercero.lg` no importa `otro`, así que no se repite. Si lo importara, la sentencia `print` de `otro` se ejecutaría una sola vez.

## Las rutas son relativas al archivo que importa

Dentro de `sub/tercero.lg`, un `import otro` busca `sub/otro.lg`, no `otro.lg` en la raíz. Por eso no hay forma de subir a una carpeta superior: el compilador no admite `..` en los imports.

```lg archivo=sub/tercero.lg
import otro
fn sumar(a: int) -> int:
    return a + util()
```

```lg archivo=otro.lg
fn util() -> int:
    return 7
```

```lg
import sub/tercero
print(sumar(1))
```

```error
sub/tercero.lg:1:1: error: no se puede abrir el módulo 'otro': no se encuentra en ninguna ruta de búsqueda. Rutas buscadas:
    sub/otro.lg
    /Users/david/Desktop/nasa/lenguaje/stdlib/otro.lg
   1 | import otro
     | ^
sugerencia: crea 'otro.lg' en una de esas carpetas, añade su carpeta con LG_PATH o 'lgc -I carpeta', o declara la dependencia en lg.toml con 'lgpm añadir'
```

La lista de rutas buscadas depende de dónde esté instalado el compilador: la última ruta es la carpeta `stdlib/` del compilador. El orden completo de búsqueda está en [herramientas](../referencia/herramientas.md) y en [arquitectura](../arquitectura.md).

## Ciclos y colisiones

Dos módulos que se importan entre sí son un error:

```lg archivo=ciclo_a.lg
import ciclo_b
fn a() -> int:
    return 1
```

```lg archivo=ciclo_b.lg
import ciclo_a
fn b() -> int:
    return 2
```

```lg
import ciclo_a
print(a())
```

```error
ciclo_b.lg:1:1: error: import circular: 'ciclo_a.lg' forma un ciclo de imports
   1 | import ciclo_a
     | ^
sugerencia: elimina uno de los imports del ciclo (un módulo no puede importarse directa ni indirectamente a sí mismo)
```

Y dos definiciones con el mismo nombre también:

```lg archivo=dos/a.lg
fn saluda() -> str:
    return "hola desde a"
```

```lg archivo=dos/b.lg
fn saluda() -> str:
    return "hola desde b"
```

```lg
import dos/a
import dos/b
print(saluda())
```

```error
dos/b.lg:1:1: error: 'saluda' ya está definida
   1 | fn saluda() -> str:
     | ^
sugerencia: elige otro nombre para la función
```

## Organizar un proyecto

- Pon las funciones reutilizables en módulos pequeños, cada uno con un tema.
- Usa carpetas para agrupar módulos relacionados, por ejemplo `geometria/punto.lg` y `geometria/figura.lg`.
- Evita las sentencias sueltas en los módulos, porque se ejecutan al importarlos.

## Ejercicios

1. Crea `utilidades/textos.lg` con una función `repite(s: str, n: int) -> str` que devuelva `s` repetida `n` veces con un bucle, e importa el módulo desde un programa principal.
2. Crea dos módulos que definan funciones con nombres distintos y un programa que los use a los dos.

## Siguiente

[Capítulo 10. Interop con C: extern](10-interop-c-extern.md)
