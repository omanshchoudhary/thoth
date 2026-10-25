cmake_minimum_required(VERSION 3.28)
project(thoth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(thoth_lib
  src/source.cpp
  src/go/token.cpp
  src/go/lexer.cpp
  src/go/ast.cpp
  src/go/parser.cpp
  src/go/sema.cpp
  src/go/lower.cpp
  src/ir/ir.cpp
  src/ir/optimize.cpp
  src/x86/codegen.cpp
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
  tests/go_lexer_test.cpp
  tests/go_parser_test.cpp
  tests/go_sema_test.cpp
  tests/ir_test.cpp
)
target_link_libraries(thoth_tests PRIVATE thoth_lib GTest::gtest_main)
target_compile_options(thoth_tests PRIVATE -Wall -Wextra -Wpedantic)
gtest_discover_tests(thoth_tests)

set(GO_PROGRAM ${CMAKE_CURRENT_SOURCE_DIR}/tests/go/print.go)
add_test(NAME cli.no_args COMMAND thoth)
add_test(NAME cli.too_many_args COMMAND thoth a.go b.go)
add_test(NAME cli.missing_file COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/missing.go)
add_test(NAME cli.unknown_emit COMMAND thoth --emit=bogus ${GO_PROGRAM})
add_test(NAME cli.lex_error COMMAND thoth --emit=tokens ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_char.go)
set_tests_properties(cli.no_args cli.too_many_args cli.missing_file cli.unknown_emit cli.lex_error PROPERTIES WILL_FAIL TRUE)
add_test(NAME cli.lex_error_location COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_char.go)
set_tests_properties(cli.lex_error_location PROPERTIES PASS_REGULAR_EXPRESSION "invalid_char.go:4:9: error: unexpected character '@'")
add_test(NAME cli.go.emit_tokens COMMAND thoth --emit=tokens ${GO_PROGRAM})
set_tests_properties(cli.go.emit_tokens PROPERTIES PASS_REGULAR_EXPRESSION "1:1 Package package")
add_test(NAME cli.go.emit_ast COMMAND thoth --emit=ast ${GO_PROGRAM})
set_tests_properties(cli.go.emit_ast PROPERTIES PASS_REGULAR_EXPRESSION "expr \\(call fmt.Println 42\\)")
add_test(NAME cli.go.emit_symbols COMMAND thoth --emit=symbols ${GO_PROGRAM})
set_tests_properties(cli.go.emit_symbols PROPERTIES PASS_REGULAR_EXPRESSION "func main\\(\\)  \\(5:6\\)")
add_test(NAME cli.go.emit_tac COMMAND thoth --emit=tac ${GO_PROGRAM})
set_tests_properties(cli.go.emit_tac PROPERTIES PASS_REGULAR_EXPRESSION "print_int 42")
add_test(NAME cli.go.emit_cfg COMMAND thoth --emit=cfg ${GO_PROGRAM})
set_tests_properties(cli.go.emit_cfg PROPERTIES PASS_REGULAR_EXPRESSION "B0 \\(0-")
add_test(NAME cli.go.emit_asm COMMAND thoth --emit=asm ${GO_PROGRAM})
set_tests_properties(cli.go.emit_asm PROPERTIES PASS_REGULAR_EXPRESSION "go.main:")
add_test(NAME cli.go.error_message COMMAND thoth ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/unused.go)
set_tests_properties(cli.go.error_message PROPERTIES PASS_REGULAR_EXPRESSION "unused.go:4:2: error: declared and not used: x")
add_test(NAME cli.go.error_exit COMMAND thoth --emit=asm ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/unused.go)
add_test(NAME cli.unknown_option COMMAND thoth -x ${GO_PROGRAM})
set_tests_properties(cli.go.error_exit cli.unknown_option PROPERTIES WILL_FAIL TRUE)

foreach(name arith control funcs bools scopes pressure print)
  foreach(level O0 O1)
    add_test(NAME e2e.${name}.${level}
      COMMAND ${CMAKE_COMMAND}
        -DTHOTH=$<TARGET_FILE:thoth>
        -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/go/${name}.go
        -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/go/${name}.out
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e/${name}-${level}
        -DFLAGS=-${level}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.cmake)
  endforeach()
endforeach()
add_test(NAME e2e.divzero
  COMMAND ${CMAKE_COMMAND}
    -DTHOTH=$<TARGET_FILE:thoth>
    -DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/go/divzero.go
    -DEXPECTED=${CMAKE_CURRENT_SOURCE_DIR}/tests/go/divzero.out
    -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/e2e/divzero
    -DEXPECTED_EXIT=2
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/e2e.cmake)
