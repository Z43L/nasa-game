# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje
# Build directory: /Users/david/Desktop/nasa/lenguaje/build-ns
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("unit" "/Users/david/Desktop/nasa/lenguaje/build-ns/lg_unit_tests")
set_tests_properties("unit" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-ns/lg_unit_tests" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;107;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("e2e" "/Users/david/Desktop/nasa/lenguaje/tests/e2e/run_e2e.sh" "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc")
set_tests_properties("e2e" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;109;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("debug_lldb" "bash" "/Users/david/Desktop/nasa/lenguaje/tests/v2/debug/run_debug_tests.sh" "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc")
set_tests_properties("debug_lldb" PROPERTIES  SKIP_RETURN_CODE "77" WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;114;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("adversarial" "/opt/homebrew/bin/python3" "/Users/david/Desktop/nasa/lenguaje/tests/adversarial/run_adversarial.py" "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc")
set_tests_properties("adversarial" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;120;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
subdirs("src/tools/fmt")
subdirs("src/tools/lsp")
subdirs("src/tools/pkg")
