# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje/src/tools/pkg
# Build directory: /Users/david/Desktop/nasa/lenguaje/build-f5/src/tools/pkg
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("pkg" "/Users/david/Desktop/nasa/lenguaje/tests/v2/pkg/run_pkg_test.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgpm")
set_tests_properties("pkg" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc;/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgpm" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;17;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;0;")
add_test("pkg_app" "bash" "/Users/david/Desktop/nasa/lenguaje/tests/v2/pkg/run_app_test.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgpm")
set_tests_properties("pkg_app" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc;/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgpm" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;24;add_test;/Users/david/Desktop/nasa/lenguaje/src/tools/pkg/CMakeLists.txt;0;")
