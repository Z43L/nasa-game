# Componente src/tools/lsp: servidor de lenguaje lglsp (LSP por stdin/stdout).
# Sin dependencias externas: JSON propio (json.*), análisis e índice propios (analysis.*).

add_library(lg_lsp_core STATIC
  json.cpp
  analysis.cpp
  server.cpp)
target_include_directories(lg_lsp_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(lg_lsp_core PUBLIC lg_core)
target_compile_options(lg_lsp_core PRIVATE -Wall -Wextra)

add_executable(lglsp main.cpp)
target_link_libraries(lglsp PRIVATE lg_lsp_core)
set_target_properties(lglsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(LG_BUILD_TESTS)
  # El enable_testing() del raíz va después de add_subdirectory: se activa aquí también.
  enable_testing()
  add_executable(lglsp_json_test ${CMAKE_SOURCE_DIR}/tests/v2/lsp/json_test.cpp)
  target_link_libraries(lglsp_json_test PRIVATE lg_lsp_core)
  set_target_properties(lglsp_json_test PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
  add_test(NAME lsp_json COMMAND lglsp_json_test)
  add_test(NAME lsp_protocol
    COMMAND python3 ${CMAKE_SOURCE_DIR}/tests/v2/lsp/lsp_test.py $<TARGET_FILE:lglsp>)
endif()
