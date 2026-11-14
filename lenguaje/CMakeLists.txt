cmake_minimum_required(VERSION 3.20)
project(lenguaje LANGUAGES C CXX)

# Requisitos:
#   - LLVM 17 o superior (probado con 23.1.5 de Homebrew; CI usa LLVM 23)
#   - Compilador C++17 y C del sistema (o el que indiques con CMAKE_C_COMPILER/CMAKE_CXX_COMPILER)
#   - LLVM se localiza con find_package. Si CMake no lo encuentra, indica su ruta con -DLLVM_DIR.
#
# Ejemplos:
#   macOS (Homebrew):
#     cmake -B build -G Ninja \
#       -DCMAKE_C_COMPILER=$(brew --prefix llvm)/bin/clang \
#       -DCMAKE_CXX_COMPILER=$(brew --prefix llvm)/bin/clang++ \
#       -DLLVM_DIR=$(brew --prefix llvm)/lib/cmake/llvm
#   Linux (paquetes apt de LLVM 23, ver docs/instalacion-linux.md):
#     cmake -B build -G Ninja -DLLVM_DIR=/usr/lib/llvm-23/lib/cmake/llvm
#   cmake --build build
#   ctest --test-dir build

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

option(LG_BUILD_TESTS "Compilar pruebas unitarias y end-to-end" ON)

find_package(LLVM REQUIRED CONFIG)
if(LLVM_PACKAGE_VERSION VERSION_LESS 17)
  message(FATAL_ERROR "Se requiere LLVM 17 o superior (encontrado ${LLVM_PACKAGE_VERSION})")
endif()
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} en ${LLVM_DIR}")

include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

llvm_map_components_to_libnames(LG_LLVM_LIBS
  Core Support IRReader Target TargetParser X86 AArch64 ARM WebAssembly
  Analysis Passes Object MC BitWriter
  ProfileData WindowsDriver)

# Dependencias del sistema que LLVM declara (zlib, zstd, libm, pthread...). Con LLVM estático
# son necesarias en el enlace; LLVM las expone como propiedad del destino LLVMSupport.
set(LG_LLVM_SYSTEM_LIBS "")
if(TARGET LLVMSupport)
  get_property(LG_LLVM_SYSTEM_LIBS TARGET LLVMSupport PROPERTY LLVM_SYSTEM_LIBS)
endif()

# ---------- Runtime en C (se enlaza con cada programa compilado) ----------
# Todos los .c de src/runtime y src/runtime/std forman parte del runtime enlazable.
file(GLOB LG_RUNTIME_SRC ${CMAKE_SOURCE_DIR}/src/runtime/*.c ${CMAKE_SOURCE_DIR}/src/runtime/std/*.c)
add_library(lg_runtime STATIC ${LG_RUNTIME_SRC})
target_include_directories(lg_runtime PUBLIC ${CMAKE_SOURCE_DIR}/src/runtime)
target_compile_options(lg_runtime PRIVATE -Wall -Wextra)
# PIC: el .a puede enlazarse tanto en ejecutables PIE como no PIE.
set_target_properties(lg_runtime PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
  POSITION_INDEPENDENT_CODE ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # pthread_getattr_np y sigaction/SA_SIGINFO son extensiones de glibc.
  target_compile_definitions(lg_runtime PRIVATE _GNU_SOURCE)
endif()

# ---------- Runtime para WebAssembly (opcional: lgc --target wasm32) ----------
# Se compila con el clang de LLVM (para wasm32-wasip1) contra wasi-libc y se archiva con llvm-ar.
# Si falta alguna herramienta, el target no se crea y lgc --target wasm32 da un error claro.
find_program(LG_WASM_CC NAMES clang HINTS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
find_program(LG_LLVM_AR NAMES llvm-ar HINTS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
if(NOT LG_WASM_CC)
  set(LG_WASM_CC "")
endif()
if(NOT LG_LLVM_AR)
  set(LG_LLVM_AR "")
endif()
set(LG_WASI_SYSROOT "")
foreach(cand /opt/homebrew/share/wasi-sysroot /usr/local/share/wasi-sysroot /usr/share/wasi-sysroot)
  if(NOT LG_WASI_SYSROOT AND EXISTS "${cand}/lib/wasm32-wasip1/libc.a")
    set(LG_WASI_SYSROOT "${cand}")
  endif()
endforeach()
set(LG_WASM_RUNTIME_LIB "")
if(LG_WASM_CC AND LG_LLVM_AR AND LG_WASI_SYSROOT)
  set(LG_WASM_RT_DIR ${CMAKE_BINARY_DIR}/wasm-runtime)
  file(MAKE_DIRECTORY ${LG_WASM_RT_DIR})
  set(LG_WASM_RT_OBJS "")
  # builtins_wasm.c: partes de compiler-rt que wasm32 necesita y wasi-libc no trae.
  foreach(src ${LG_RUNTIME_SRC} ${CMAKE_SOURCE_DIR}/src/runtime/wasm/builtins_wasm.c)
    get_filename_component(base ${src} NAME_WE)
    get_filename_component(dir ${src} DIRECTORY)
    get_filename_component(dname ${dir} NAME)
    set(obj ${LG_WASM_RT_DIR}/${dname}_${base}.o)
    add_custom_command(OUTPUT ${obj}
      COMMAND ${LG_WASM_CC} --target=wasm32-wasip1 --sysroot=${LG_WASI_SYSROOT}
              -D_WASI_EMULATED_SIGNAL -O2 -Wall -Wextra -I${CMAKE_SOURCE_DIR}/src/runtime
              -c ${src} -o ${obj}
      DEPENDS ${src}
      COMMENT "Compilando runtime wasm32: ${base}.c")
    list(APPEND LG_WASM_RT_OBJS ${obj})
  endforeach()
  set(LG_WASM_RUNTIME_LIB ${LG_WASM_RT_DIR}/liblg_runtime_wasm32.a)
  add_custom_command(OUTPUT ${LG_WASM_RUNTIME_LIB}
    COMMAND ${CMAKE_COMMAND} -E rm -f ${LG_WASM_RUNTIME_LIB}
    COMMAND ${LG_LLVM_AR} rcs ${LG_WASM_RUNTIME_LIB} ${LG_WASM_RT_OBJS}
    DEPENDS ${LG_WASM_RT_OBJS}
    COMMENT "Archivando liblg_runtime_wasm32.a")
  add_custom_target(lg_runtime_wasm DEPENDS ${LG_WASM_RUNTIME_LIB})
  message(STATUS "wasm32: runtime con ${LG_WASM_CC} y sysroot ${LG_WASI_SYSROOT}")
else()
  message(STATUS "wasm32: desactivado (falta clang de LLVM, llvm-ar o wasi-libc)")
endif()

# ---------- Núcleo del compilador (C++) ----------
add_library(lg_core STATIC
  src/common/diagnostics.cpp
  src/lexer/lexer.cpp
  src/parser/parser.cpp
  src/sema/types.cpp
  src/sema/sema.cpp
  src/codegen/codegen.cpp
  src/codegen/debug_info.cpp
  src/driver/loader.cpp)
target_include_directories(lg_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_include_directories(lg_core PUBLIC ${LLVM_INCLUDE_DIRS})
target_compile_definitions(lg_core PUBLIC ${LLVM_DEFINITIONS_LIST})
target_link_libraries(lg_core PUBLIC ${LG_LLVM_LIBS} ${LG_LLVM_SYSTEM_LIBS})
target_compile_options(lg_core PRIVATE -Wall -Wextra)

# ---------- Ejecutable: lgc ----------
add_executable(lgc src/driver/main.cpp)
target_link_libraries(lgc PRIVATE lg_core)
target_compile_definitions(lgc PRIVATE
  LG_RUNTIME_LIB="$<TARGET_FILE:lg_runtime>"
  LG_RUNTIME_INCLUDE="${CMAKE_SOURCE_DIR}/src/runtime"
  LG_STDLIB_DIR="${CMAKE_SOURCE_DIR}/stdlib"
  LG_WASM_CC="${LG_WASM_CC}"
  LG_WASM_SYSROOT="${LG_WASI_SYSROOT}"
  LG_WASM_RUNTIME_LIB="${LG_WASM_RUNTIME_LIB}")
if(TARGET lg_runtime_wasm)
  add_dependencies(lgc lg_runtime_wasm)
endif()

# ---------- Herramientas (v2): cada una en su carpeta ----------
add_subdirectory(src/tools/fmt)
add_subdirectory(src/tools/lsp)
add_subdirectory(src/tools/pkg)
set_target_properties(lgc PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
add_dependencies(lgc lg_runtime)

# ---------- Pruebas ----------
if(LG_BUILD_TESTS)
  enable_testing()
  find_package(GTest CONFIG)
  if(GTest_FOUND)
    file(GLOB LG_UNIT_TESTS ${CMAKE_SOURCE_DIR}/tests/unit/*_test.cpp)
    add_executable(lg_unit_tests ${LG_UNIT_TESTS})
    target_link_libraries(lg_unit_tests PRIVATE lg_core GTest::gtest GTest::gtest_main)
    add_test(NAME unit COMMAND lg_unit_tests)
  endif()
  add_test(NAME e2e
    COMMAND ${CMAKE_SOURCE_DIR}/tests/e2e/run_e2e.sh $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  # Depurador (B6): lldb debe resolver breakpoints por línea y leer variables locales de lgc -g.
  # Sin lldb en el sistema, el script sale con 77 y ctest lo cuenta como skip.
  add_test(NAME debug_lldb
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/v2/debug/run_debug_tests.sh $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  set_tests_properties(debug_lldb PROPERTIES SKIP_RETURN_CODE 77)
  if(LG_WASM_RUNTIME_LIB AND LG_WASM_CC)
    find_program(LG_NODE node)
    if(LG_NODE)
      add_test(NAME wasm
        COMMAND ${CMAKE_SOURCE_DIR}/tests/wasm/run_wasm.sh $<TARGET_FILE:lgc>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
      set_tests_properties(wasm PROPERTIES SKIP_RETURN_CODE 77)
      # UI wasm: exports con nombre (--export), cadenas a JS, lib/ui/puente.js e IR nativo intacto.
      add_test(NAME wasm_ui
        COMMAND ${LG_NODE} --no-warnings ${CMAKE_SOURCE_DIR}/tests/wasm_ui/run_ui.js $<TARGET_FILE:lgc>
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
      set_tests_properties(wasm_ui PROPERTIES SKIP_RETURN_CODE 77)
    endif()
  endif()
  add_test(NAME qa
    COMMAND ${CMAKE_SOURCE_DIR}/tests/qa/run_qa.sh $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME app
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/app/run_app.sh $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  add_test(NAME qa_web
    COMMAND ${CMAKE_SOURCE_DIR}/tests/qa/web/run_web_qa.sh $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  find_program(LG_PYTHON3 python3)
  if(LG_PYTHON3)
    add_test(NAME adversarial
      COMMAND ${LG_PYTHON3} ${CMAKE_SOURCE_DIR}/tests/adversarial/run_adversarial.py $<TARGET_FILE:lgc>
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
  endif()
endif()
