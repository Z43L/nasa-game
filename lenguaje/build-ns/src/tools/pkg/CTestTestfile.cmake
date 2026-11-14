# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje/src/tools/pkg
# Build directory: /Users/david/Desktop/nasa/lenguaje/build-ns/src/tools/pkg
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("pkg" "/Users/david/Desktop/nasa/lenguaje/tests/v2/pkg/run_pkg_test.sh" "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc" "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgpm")
set_tests_properties("pkg" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgc;/Users/david/Desktop/nasa/lenguaje/build-ns/bin/lgpm" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;15;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;0;")
