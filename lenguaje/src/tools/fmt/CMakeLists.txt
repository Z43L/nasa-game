# lgfmt: formateador canónico de código .lg (B6). Usa el parser de lg_core.
add_executable(lgfmt
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/printer.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/ast_dump.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/comments.cpp)
target_link_libraries(lgfmt PRIVATE lg_core)
target_compile_options(lgfmt PRIVATE -Wall -Wextra)
set_target_properties(lgfmt PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Pruebas: casos golden de tests/v2/fmt, idempotencia y preservación del AST sobre los .lg del repo.
if(LG_BUILD_TESTS)
  # El enable_testing() del raíz va después de add_subdirectory: se activa aquí también.
  enable_testing()
  add_test(NAME lgfmt
    COMMAND bash ${CMAKE_SOURCE_DIR}/tests/v2/fmt/run_fmt_tests.sh $<TARGET_FILE:lgfmt> $<TARGET_FILE:lgc>
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()
