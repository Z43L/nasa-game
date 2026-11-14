# Compilar y ejecutar programas en WebAssembly (`lgc --target wasm32`)

`lgc` genera `wasm32-wasip1` (WASI preview 1) y enlaza con `wasm-ld` y wasi-libc. El programa se
ejecuta con `node` usando su WASI (`node:wasi`). Sin `--target`, `lgc` funciona como siempre.

## Requisitos

- LLVM con el backend WebAssembly (probado con LLVM 23.1.5 de Homebrew, `/opt/homebrew/opt/llvm`).
- `wasm-ld` en el PATH (paquete `lld`).
- wasi-libc en `/opt/homebrew/share/wasi-sysroot` (u otra ruta de la lista de `CMakeLists.txt`).
- Node.js con `node:wasi` (probado con v26.12.1).

## Compilar lgc con el objetivo wasm

```sh
cmake -B build-wasm -G Ninja -DLLVM_DIR=/opt/homebrew/opt/llvm/lib/cmake/llvm
cmake --build build-wasm
```

CMake crea `build-wasm/wasm-runtime/liblg_runtime_wasm32.a` si encuentra clang, llvm-ar y el
sysroot. Si falta alguna herramienta, `lgc --target wasm32` avisa y no compila.

## Compilar y ejecutar un programa

```sh
build-wasm/bin/lgc --target wasm32 tests/wasm/hola.lg -o hola.wasm
node --no-warnings tests/wasm/node_wasi_run.js hola.wasm
```

`node_wasi_run.js` da al programa el directorio actual como preopen (`.`), así que puede leer y
escribir ficheros relativos. El código de salida es el del programa. Un trap de wasm sale con 1.

Otras opciones (`-O2`, `-g`, `--emit-llvm`, `-I`) funcionan igual que en nativo. `--emit-llvm`
escribe el IR para `wasm32-wasip1`.

## Pruebas

```sh
tests/wasm/run_wasm.sh build-wasm/bin/lgc
ctest --test-dir build-wasm -R wasm
```

Cada caso `NOMBRE.lg` tiene uno de estos ficheros:

- `NOMBRE.expected`: la salida debe coincidir. El mismo programa compilado para el host debe dar la
  misma salida (comprobación cruzada).
- `NOMBRE.rterror`: el programa debe terminar con código distinto de 0 y su stderr debe contener el
  texto.
- `NOMBRE.error`: `lgc --target wasm32` debe fallar y su stderr debe contener el texto.

## No soportado en wasm32

- Hilos (`hilo`, `async fn`) y canales (`Canal`): dan un error de compilación, porque wasm32 no
  tiene pthread en este runtime. El mensaje dice que el programa usa `lg_hilo_*` o `lg_canal_*`.
- `import tcp` y los nativos `lg_std_tcp_*`: error de compilación (no hay sockets).
- Manejo de señales: no hay manejador de `lg_rt.c`. Un desbordamiento de pila por recursión profunda
  termina con código 1 y el trap del motor (`memory access out of bounds`), sin el mensaje
  "desbordamiento de la pila" del nativo.
- `libclang_rt` (compiler-rt) no está instalada para wasm32. El enlace usa `-nodefaultlibs -lc`.
  Las funciones de compiler-rt que hacen falta se dan en `src/runtime/wasm/builtins_wasm.c`
  (de momento solo `__multi3`, usada por la comprobación de desbordamiento de enteros).
- Ficheros: solo funcionan las rutas dentro del directorio desde el que se ejecuta node.
- Los programas wasm no tienen argumentos de línea de órdenes; `args` contiene solo el nombre.
