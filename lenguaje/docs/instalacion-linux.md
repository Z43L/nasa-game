# Instalación y pruebas en Linux

Esta guía describe cómo compilar `lgc` y ejecutar sus pruebas en Linux. El objetivo declarado es
Ubuntu 24.04 o más reciente con LLVM 23 de los repositorios de apt.llvm.org.

> **Estado de verificación.** Los pasos de esta guía no se han ejecutado en un Linux real.
> Lo que sí se comprobó desde macOS está marcado en la sección "Verificado". Todo lo demás
> está marcado como **no verificado**.

## 1. Paquetes del sistema

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build libgtest-dev python3 wget ca-certificates
```

- `build-essential` aporta `cc`, que `lgc` usa para enlazar los programas generados.
- `libgtest-dev` aporta GoogleTest para `lg_unit_tests`.
- `python3` ejecuta el corredor de pruebas adversariales.

## 2. LLVM 23

Los repositorios oficiales de Ubuntu no ofrecen LLVM 23. Para LLVM 23 se usa el script oficial de
apt.llvm.org:

```bash
wget https://apt.llvm.org/llvm.sh
chmod +x llvm.sh
sudo ./llvm.sh 23 all
sudo apt-get install -y llvm-23-dev clang-23
```

Esto instala `/usr/lib/llvm-23/lib/cmake/llvm`, que es la ruta que CMake necesita en `LLVM_DIR`.

## 3. Configurar y compilar

```bash
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER=clang-23 \
  -DCMAKE_CXX_COMPILER=clang++-23 \
  -DLLVM_DIR=/usr/lib/llvm-23/lib/cmake/llvm
cmake --build build
```

El compilador C++ puede ser `g++` si se prefiere; en ese caso omite las dos líneas de
`CMAKE_C_COMPILER` y `CMAKE_CXX_COMPILER` y mantén `LLVM_DIR`.

## 4. Pruebas

```bash
build/lg_unit_tests                                  # pruebas unitarias (GoogleTest)
tests/e2e/run_e2e.sh build/bin/lgc                   # programas de ejemplo con salida esperada
python3 tests/adversarial/run_adversarial.py build/bin/lgc   # casos adversariales
```

También puedes lanzar las tres con `ctest --test-dir build --output-on-failure`.

## 5. Notas de enlace

- `lgc` enlaza con `cc -o salida objeto liblg_runtime.a -pthread -lm`. El runtime se compila con
  `_GNU_SOURCE` (lo define CMake en Linux), porque usa `pthread_getattr_np`.
- Si compilas `src/runtime/lg_rt.c` a mano (por ejemplo con `tests/runtime/runtime_test.c`), usa
  `-D_GNU_SOURCE`. Sin esa macro, `lg_rt.c` se niega a compilar con un `#error` explícito.
- No se usa `-no-pie`: el objeto que genera el compilador es PIC y el runtime también se compila
  PIC, así que el enlace funciona con ejecutables PIE (valor por defecto).

## Verificado (desde macOS, sin Linux real)

- `lg_rt.c` compilado con `--target=x86_64-linux-gnu` y cabeceras del SDK de macOS: el único
  error restante es `pthread_getattr_np` no declarada. Es esperable, porque esa función solo la
  declara glibc con `_GNU_SOURCE`, y las cabeceras de macOS no la tienen. Con cabeceras de glibc
  el error debería desaparecer (**no verificado** con glibc real).
- Las fuentes C++ de `src/` no contienen `#ifdef` de plataforma ni llamadas específicas de macOS
  (búsqueda con grep). La compilación cruzada de C++ no fue posible: no hay cabeceras de
  libstdc++ ni de glibc en el equipo, y las de libc++ no funcionan con un destino Linux sin
  sysroot.
- `CMakeLists.txt` configura correctamente en macOS con LLVM 23.1.5 (`cmake -S . -B ...`).
- `.github/workflows/ci.yml` es YAML válido (comprobado con Ruby).
- `tests/adversarial/run_adversarial.py` analiza los 294 archivos `.expect` sin errores. Se
  ejecutó una vez contra el `lgc` de macOS: 293 de 294 pasan. El caso que falla es
  `semantica/import_inexistente.lg`: el `.expect` espera el texto "no se puede abrir", pero el
  compilador imprime "no se encuentra el módulo". Es una discrepancia entre prueba y compilador,
  no un problema de Linux.

## No verificado

- Compilar, enlazar y ejecutar en Linux real (ninguna prueba se ha ejecutado allí).
- Que `apt.llvm.org/llvm.sh 23 all` y los paquetes `llvm-23-dev` y `clang-23` estén disponibles
  en el momento de usarlos.
- Que `libgtest-dev` exponga la configuración CMake de GoogleTest (`GTestConfig.cmake`) en tu
  versión de Ubuntu.
- Que `lg_unit_tests`, `run_e2e.sh` y los casos adversariales pasen en Linux. En particular, las
  pruebas de desbordamiento de pila (`rec_profunda_10M`, `rt_pila_profunda`) dependen del manejador
  de SIGSEGV con `sigaltstack` y no se han visto en glibc.
- Que `lgc` funcione en Linux cuando el sistema solo tiene `clang` y no `cc` (`lgc` llama a `cc`).
- `tests/runtime/runtime_test.c` no forma parte de la CI y no se ha ejecutado en ninguna plataforma.
