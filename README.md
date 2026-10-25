# Thoth

A compiler for a subset of Go, written from scratch in C++20, in which every phase of compilation can be inspected.

Thoth takes a `.go` file through the full pipeline (lexing, parsing, semantic analysis, intermediate code, optimization and x86-64 code generation) and produces a native Linux executable. Pass `--emit` to stop after any phase and print exactly what that phase produced.

> Thoth is the Egyptian god of writing, language and reckoning: the scribe who records everything. This compiler writes down each step it takes.

## Highlights

- **Complete pipeline.** Source code goes in, a native x86-64 executable comes out. No LLVM, no parser generators: every phase is hand-written.
- **Transparent by design.** Six `--emit` options expose tokens, the syntax tree, the symbol table, three-address code, the control-flow graph and the generated assembly.
- **Go-style diagnostics.** Errors are reported as `file:line:column` with messages modelled on the Go compiler's, and each run reports every error it can find, not just the first.
- **Checked against Go.** The end-to-end tests compile real Go programs and compare their output byte for byte with the official Go toolchain, both with and without optimization.

## Language scope

Thoth compiles a deliberately small, well-defined subset of Go. Programs inside the subset are valid Go and behave exactly as they would with `go run`. Anything outside the subset is rejected with an error and is never silently miscompiled.

### Supported

| Area | What is supported |
|---|---|
| Program structure | `package main`, `import "fmt"` (single or grouped), top-level functions declared in any order |
| Types | `int` (64-bit; overflow wraps as in Go), `bool` (`true`, `false`) |
| Declarations | `var x int`, `var x = expr`, `var x int = expr`, `x := expr`; nested block scopes and shadowing |
| Functions | Parameters, including grouped `a, b int` (up to 6); at most one result; recursion |
| Statements | `=`, `+=` `-=` `*=` `/=` `%=`, `++` `--`, `if` / `else if` / `else`, `if` with an init statement, all three forms of `for`, `break`, `continue`, `return` |
| Expressions | `+ - * / %`, `== != < <= > >=`, `&& \|\| !`, unary `-`, parentheses and calls, with Go's precedence rules and short-circuit evaluation |
| Output | `fmt.Println` with any number of `int` and `bool` arguments |
| Compile-time checks | Undefined and redeclared names, type mismatches, non-boolean conditions, wrong argument count or types, missing `return`, unused variables and imports, `break`/`continue` outside a loop, division by a constant zero |
| Runtime | Integer division by zero panics with Go's message and exit status 2 |

### Not supported

- Strings, floating-point numbers, runes and sized integer types (`int32`, `uint8`, …)
- Arrays, slices, maps, structs, pointers, interfaces and methods
- Multiple return values, named results and multiple assignment (`a, b = b, a`)
- `const`, package-level variables, closures and function values
- `switch`, labels, `goto`, `defer`, `panic` / `recover`
- Goroutines and channels
- Imports other than `fmt`, `fmt` functions other than `Println`, and the built-in `println`

## Requirements

- Linux on x86-64
- CMake 3.28 or newer and a C++20 compiler (GCC 13 or newer)
- `gcc` at compile time, to assemble and link the generated assembly
- Optional: the Go toolchain, only to regenerate the expected outputs of the end-to-end tests

## Building

```sh
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build
```

GoogleTest is downloaded automatically during configuration.

## Usage

```
thoth [--emit=tokens|ast|symbols|tac|cfg|asm] [-O0] [-o output] <file.go>
```

| Option | Effect |
|---|---|
| *(none)* | Compile to a native executable named after the source file |
| `-o output` | Choose the executable's name |
| `-O0` | Turn the optimizer off (it is on by default) |
| `--emit=tokens` | Print the token stream with `line:column` positions |
| `--emit=ast` | Print the abstract syntax tree |
| `--emit=symbols` | Print the symbol table, scope by scope |
| `--emit=tac` | Print the three-address code before optimization |
| `--emit=cfg` | Print the basic blocks and control-flow graph after optimization |
| `--emit=asm` | Print the generated x86-64 assembly |

### Example

[`examples/sum.go`](examples/sum.go):

```go
package main

import "fmt"

func main() {
	n := 5
	sum := 0
	for n > 0 {
		sum += n
		n--
	}
	fmt.Println(sum)
}
```

Compile and run it:

```
$ ./build/thoth -o sum examples/sum.go
$ ./sum
15
```

Look inside the compiler:

```
$ ./build/thoth --emit=tac examples/sum.go
func main()
  (0) n = 5
  (1) sum = 0
  (2) t1 = n > 0
  (3) ifFalse t1 goto 7
  (4) sum = sum + n
  (5) n = n - 1
  (6) goto 2
  (7) print_int sum
  (8) print_newline
  (9) return

$ ./build/thoth --emit=cfg examples/sum.go
func main
  B0 (0-1) -> B1
    (0) n = 5
    (1) sum = 0
  B1 (2-3) -> B3 B2
    (2) t1 = n > 0
    (3) ifFalse t1 goto 7
  B2 (4-6) -> B1
    (4) sum = sum + n
    (5) n = n - 1
    (6) goto 2
  B3 (7-9)
    (7) print_int sum
    (8) print_newline
    (9) return
```

Errors point at the exact location, and every error is reported in one run:

```
$ ./build/thoth bad.go
bad.go:7:14: error: invalid operation: x + true (mismatched types int and bool)
bad.go:8:2: error: declared and not used: y
```

## How it works

```
source ─► lexer ─► parser ─► semantic analysis ─► IR generation ─► optimizer ─► code generator ─► gcc ─► executable
          tokens    AST       checked AST         three-address    CFG          x86-64 assembly
                                                  code
```

| Phase | Source | Technique |
|---|---|---|
| Lexical analysis | `src/go/lexer.cpp` | Hand-written DFA with maximal munch; implements Go's automatic semicolon insertion |
| Syntax analysis | `src/go/parser.cpp` | Recursive descent, with precedence climbing for expressions; panic-mode error recovery |
| Semantic analysis | `src/go/sema.cpp` | Symbol table as a stack of scopes; type checking; Go's unused-variable, unused-import and missing-return rules |
| IR generation | `src/go/lower.cpp`, `src/ir/ir.cpp` | Three-address code stored as quadruples; jump targets filled in by backpatching; short-circuit `&&` and `\|\|` |
| Optimization | `src/ir/optimize.cpp` | Basic blocks and CFG; constant folding and propagation, copy propagation, liveness-based dead-code elimination, unreachable-code removal and jump threading, repeated until nothing changes |
| Code generation | `src/x86/codegen.cpp` | System V AMD64 calling convention; one stack frame per function; values cached in registers within a basic block, with next-use deciding which register to free; Intel-syntax assembly |

Only `src/go/` knows about Go. The intermediate code, optimizer and code generator are language-independent, so another front end could reuse them unchanged.

## Project layout

```
src/
  main.cpp        driver: command-line options and the phase pipeline
  source.*        reading source files, source locations
  diagnostic.hpp  error representation shared by all phases
  go/             Go front end: tokens, lexer, AST, parser, semantic analysis, lowering
  ir/             three-address code and the optimizer
  x86/            x86-64 code generator
tests/
  *_test.cpp      unit tests for every phase (GoogleTest)
  go/             end-to-end programs with their expected output from the Go toolchain
examples/         small programs to try
```

## Testing

```sh
ctest --test-dir build
```

The suite has three layers:

- **Unit tests** for each phase, covering both valid input and error cases with their exact positions.
- **Command-line tests** for every `--emit` option and for error exit codes.
- **End-to-end tests** that compile each program in `tests/go/` at `-O0` and `-O1`, run it, and compare its output with output recorded from the official Go toolchain.
