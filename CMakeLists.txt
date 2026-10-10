cmake_minimum_required(VERSION 3.28)
project(thoth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(thoth_lib
  src/source.cpp
  src/c/token.cpp
  src/c/lexer.cpp
)
target_include_directories(thoth_lib PUBLIC src)
target_compile_options(thoth_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(thoth src/main.cpp)
target_link_libraries(thoth PRIVATE thoth_lib)
target_compile_options(thoth PRIVATE -Wall -Wextra -Wpedantic)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_executable(thoth_tests
  tests/source_test.cpp
  tests/lexer_test.cpp
)
target_link_libraries(thoth_tests PRIVATE thoth_lib GTest::gtest_main)
target_compile_options(thoth_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(thoth_tests)

add_test(NAME cli.no_args COMMAND thoth)
add_test(NAME cli.too_many_args COMMAND thoth a.c b.c)
add_test(NAME cli.missing_file COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/missing.c)
set_tests_properties(cli.no_args cli.too_many_args cli.missing_file PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.reads_file COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/hello.c)

add_test(NAME cli.emit_tokens COMMAND thoth --emit=tokens ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/hello.c)
set_tests_properties(cli.emit_tokens PROPERTIES PASS_REGULAR_EXPRESSION "1:1 Int int")
add_test(NAME cli.unknown_emit COMMAND thoth --emit=bogus ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/hello.c)
add_test(NAME cli.lex_error COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_char.c)
set_tests_properties(cli.unknown_emit cli.lex_error PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.lex_error_location COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_char.c)
set_tests_properties(cli.lex_error_location PROPERTIES PASS_REGULAR_EXPRESSION "invalid_char.c:1:11: error: ")
