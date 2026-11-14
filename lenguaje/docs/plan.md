# Plan: lenguaje totalmente funcional (v1 completa + v2)

Estado: v1 (Fase A) hecha en macOS. v2 (Fase B) hecha en macOS salvo lo que se marca como pendiente o fuera de
alcance. Lo que no está implementado en el lenguaje está en [limitaciones](limitaciones.md).

Las casillas marcadas son lo que está verificado: programas de la documentación (`verify_docs.py`), pruebas de
`ctest --test-dir build` (8 pruebas) o una comprobación manual anotada.

## Fase A: completar v1

### A1. Option[T] (tipo opcional): HECHO
- [x] Tipo `Option[T]` con `Some(x)` y `None`, sin valores nulos.
- [x] `opt.es_some()`, `opt.es_none()` y `opt.desempacar_o(valor_por_defecto)`. Desempaquetar sin comprobar no compila.
- [x] Pruebas e2e de ejecución y errores; ejemplos de la guía verificados.

### A2. Funciones anidadas con captura (clausuras): HECHO
- [x] `fn` dentro de `fn` con acceso de lectura a variables externas.
- [x] Captura por valor para variables inmutables; captura por referencia para variables `mut` (decisión tomada, ver abajo). Documentado en [tipos](referencia/tipos.md#funciones-anidadas-y-clausuras).
- [x] Limitaciones conocidas: un ciclo de clausuras no se libera (ver [limitaciones](limitaciones.md#memoria)).

### A3. Portabilidad Linux y Windows
- [ ] Linux: compilar y ejecutar la suite completa en una máquina Linux. Pendiente: la guía [instalación en Linux](instalacion-linux.md) no se ha ejecutado en un Linux real.
- [ ] CI con matriz de sistemas. Pendiente.
- Windows: fuera de alcance de esta versión. El runtime usa pthread y el driver enlaza con `cc`.

## Fase B: v2 (herramientas y ecosistema)

### B1. Concurrencia y async: HECHO (macOS; Linux sin verificar)
- [x] Conteo de referencias atómico (`__atomic` en `lg_retain`/`lg_release`). Coste medido: 1,26 ns por par retain/release sin atómicas frente a 3,23 ns con atómicas (mediana de 7 ejecuciones de 10 millones, Apple M4).
- [x] `hilo f(args)` y `Hilo[T]` con `esperar()`; `Canal[T]` con `enviar`, `recibir` (devuelve `Option[T]`) y `cerrar`. Runtime en `src/runtime/std/hilos.c` (pthread).
- [x] `async fn` y `await` como azúcar sobre hilos. Sin ejecutor: un hilo por llamada.
- [x] Pruebas: unitarias `tests/unit/concurrencia_test.cpp`; e2e `conc_*`. Carrera: 50 ejecuciones del programa de 4 hilos con el mismo resultado; ThreadSanitizer sin advertencias en los programas de canal.
- [ ] Sanitizador de hilos integrado en CTest. Pendiente: hoy se ejecuta a mano (ver [hilos](referencia/hilos.md#verificación)).

### B2. Referencias débiles: HECHO (macOS; Linux sin verificar)
- [x] `Debil[T]` con `debil(x)` y `d.fuerte()` (`Option[T]`). Cabecera `LgHeader` con contador débil (cambio de ABI, ver `src/runtime/lg_rt.h`).
- [x] Ciclo padre-hijo con débil sin fuga; débil tras liberar devuelve `None`. Verificado contando bloques de malloc al salir (interposición con DYLD_INSERT_LIBRARIES): igual a la línea base, 177 bloques. `leaks` no funciona en este equipo.

### B3. GPU para tensores: FUERA DE ALCANCE
- No se implementa backend Metal ni CUDA. Los tensores se calculan en CPU. Ver [limitaciones](limitaciones.md#gpu).

### B4. Biblioteca estándar
- [x] `mat` (álgebra básica de tensores de forma concreta), `red` (activaciones `sigmoide` y `relu`, y `perdida_mse`) y `datos` (CSV numérico, lectura y escritura de texto). Escritas en el lenguaje con nativos C en `src/runtime/std/` para `datos`.
- [x] Ejemplos de la [referencia de stdlib](referencia/stdlib.md) verificados.
- Pendiente: capas lineales en `red`; un módulo `tensor` separado con operaciones ampliadas (reducciones, `reshape`, difusión). Ver [limitaciones](limitaciones.md#tensores).

### B5. Gestor de paquetes: HECHO con limitaciones
- [x] Manifiesto `lg.toml` (`[proyecto]` y `[dependencias]`) y `lg.lock` generado por `lgpm resolver`.
- [x] Dependencias por ruta y por repositorio git (con `rev`), caché en `~/.lgpm/cache`.
- [x] `lgpm nuevo`, `construir`, `ejecutar`, `añadir` y `resolver`. Prueba: `tests/v2/pkg/run_pkg_test.sh` (11 casos).
- Pendiente: dependencias transitivas y resolución de versiones. Ver [herramientas](referencia/herramientas.md#lgpm).

### B6. Herramientas
- [x] Formateador `lgfmt`: formato a stdout, `-i`, `--check` y `--dump-ast`. Prueba: `tests/v2/fmt/run_fmt_tests.sh`. Pendiente: comentarios de cola en listas multilínea.
- [x] Servidor de lenguaje `lglsp`: diagnósticos, ir a definición, información al pasar el ratón y autocompletado. Pruebas: `tests/v2/lsp/`. Pendiente: `references` y `rename`.
- [x] Depurador: `lgc -g` emite DWARF (DICompileUnit, DIFile por archivo, DISubprogram por función, DILocation por sentencia y expresión, DILocalVariable para parámetros y locales). En macOS genera `programa.dSYM` con `dsymutil`, así que no hace falta `--keep-obj`. Sin `-g` el IR no cambia. Prueba: `tests/v2/debug/run_debug_tests.sh` (ctest `debug_lldb`; skip con código 77 si no hay lldb). Detalle y comandos en [depuración](referencia/depuracion.md).
  - Pendiente: `str`, `List`, `Dict` y `Tensor` son tipos opacos en el depurador; las referencias recursivas son `void *`; `-O2 -g` compila pero sin verificar la depuración optimizada; solo lldb y solo macOS (arm64) verificados.

## Riesgos
- Alcance grande. Lo pendiente de v1 (A3) y de v2 (sanitizador en CTest, dependencias transitivas, `references`/`rename`) va por separado.
- Linux no está verificado en ejecución: la portabilidad real depende de hacer esa prueba.

## Decisiones tomadas
1. Clausuras: las variables `mut` capturadas se capturan por referencia (viven en una celda del heap); el resto, por valor.
2. GPU: fuera de alcance. Solo se mantiene la CPU.
3. Windows: fuera de alcance de esta versión.
