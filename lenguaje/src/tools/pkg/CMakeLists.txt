# Herramienta lgpm (gestor de paquetes, B5): lg.toml, lg.lock y llamadas a lgc.
add_executable(lgpm
  ${CMAKE_CURRENT_SOURCE_DIR}/lgpm.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/toml.cpp)
target_include_directories(lgpm PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_options(lgpm PRIVATE -Wall -Wextra)
# lgpm invoca lgc para compilar: se pasa su ruta en tiempo de compilación (LGPM_LGC env la sobrescribe).
target_compile_definitions(lgpm PRIVATE LGPM_LGC_DEFAULT="$<TARGET_FILE:lgc>")
# lgpm app: raíz del repositorio (lib/app, lib/ui, stdlib, plantillas). LGPM_RAIZ la sobrescribe.
target_compile_definitions(lgpm PRIVATE LGPM_RAIZ_DEFAULT="${CMAKE_SOURCE_DIR}")
add_dependencies(lgpm lgc)
set_target_properties(lgpm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(LG_BUILD_TESTS)
  # enable_testing() aquí: el del raíz llega después de add_subdirectory y no registra este test.
  enable_testing()
  add_test(NAME pkg
    COMMAND ${CMAKE_SOURCE_DIR}/tests/v2/pkg/run_pkg_test.sh $<TARGET_FILE:lgc> $<TARGET_FILE:lgpm>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

if(LG_BUILD_TESTS)
  # lgpm app (F5): nueva, construir a wasm + dist/, levantar (servidor estático) y errores.
  add_test(NAME pkg_app
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/v2/pkg/run_app_test.sh $<TARGET_FILE:lgc> $<TARGET_FILE:lgpm>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
