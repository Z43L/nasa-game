# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje/src/tools/lsp
# Build directory: /Users/david/Desktop/nasa/lenguaje/build/src/tools/lsp
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("lsp_json" "/Users/david/Desktop/nasa/lenguaje/build/tests/lglsp_json_test")
set_tests_properties("lsp_json" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build/tests/lglsp_json_test" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/lsp/CMakeLists.txt;22;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/lsp/CMakeLists.txt;0;")
add_test("lsp_protocol" "python3" "/Users/david/Desktop/nasa/lenguaje/tests/v2/lsp/lsp_test.py" "/Users/david/Desktop/nasa/lenguaje/build/bin/lglsp")
set_tests_properties("lsp_protocol" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build/bin/lglsp" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/lsp/CMakeLists.txt;23;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/lsp/CMakeLists.txt;0;")
