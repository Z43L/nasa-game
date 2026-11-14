# CMake generated Testfile for 
# Source directory: /Users/david/Desktop/nasa/lenguaje
# Build directory: /Users/david/Desktop/nasa/lenguaje/build-f5
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test("unit" "/Users/david/Desktop/nasa/lenguaje/build-f5/lg_unit_tests")
set_tests_properties("unit" PROPERTIES  _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/lg_unit_tests" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;161;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("e2e" "/Users/david/Desktop/nasa/lenguaje/tests/e2e/run_e2e.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("e2e" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;163;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("debug_lldb" "bash" "/Users/david/Desktop/nasa/lenguaje/tests/v2/debug/run_debug_tests.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("debug_lldb" PROPERTIES  SKIP_RETURN_CODE "77" WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;168;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("wasm" "/Users/david/Desktop/nasa/lenguaje/tests/wasm/run_wasm.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("wasm" PROPERTIES  SKIP_RETURN_CODE "77" WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;175;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("wasm_ui" "/opt/homebrew/bin/node" "--no-warnings" "/Users/david/Desktop/nasa/lenguaje/tests/wasm_ui/run_ui.js" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("wasm_ui" PROPERTIES  SKIP_RETURN_CODE "77" WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;180;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("qa" "/Users/david/Desktop/nasa/lenguaje/tests/qa/run_qa.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("qa" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;186;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("app" "bash" "/Users/david/Desktop/nasa/lenguaje/tests/app/run_app.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("app" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;189;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("qa_web" "/Users/david/Desktop/nasa/lenguaje/tests/qa/web/run_web_qa.sh" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("qa_web" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;192;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
add_test("adversarial" "/opt/homebrew/bin/python3" "/Users/david/Desktop/nasa/lenguaje/tests/adversarial/run_adversarial.py" "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc")
set_tests_properties("adversarial" PROPERTIES  WORKING_DIRECTORY "/Users/david/Desktop/nasa/lenguaje" _CMAKE_TEST_BUILD_DEPENDS "/Users/david/Desktop/nasa/lenguaje/build-f5/bin/lgc" _BACKTRACE_TRIPLES "/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;197;add_test;/Users/david/Desktop/nasa/lenguaje/CMakeLists.txt;0;")
subdirs("src/tools/fmt")
subdirs("src/tools/lsp")
subdirs("src/tools/pkg")
