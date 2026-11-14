# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje/src/tools/fmt
# Build directory: /Users/david/Desktop/nasa/lenguaje/build/src/tools/fmt
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("lgfmt" "bash" "/Users/david/Desktop/nasa/lenguaje/tests/v2/fmt/run_fmt_tests.sh" "/Users/david/Desktop/nasa/lenguaje/build/bin/lgfmt" "/Users/david/Desktop/nasa/lenguaje/build/bin/lgc")
set_tests_properties("lgfmt" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build/bin/lgc;/Users/david/Desktop/nasa/lenguaje/build/bin/lgfmt" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/fmt/CMakeLists.txt;16;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/fmt/CMakeLists.txt;0;")
