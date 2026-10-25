#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "go/lexer.hpp"
#include "go/lower.hpp"
#include "go/parser.hpp"
#include "go/sema.hpp"
#include "ir/ir.hpp"
#include "ir/optimize.hpp"
#include "x86/codegen.hpp"

namespace {

thoth::ir::Program build(std::string_view source, bool optimize) {
    auto lexed = thoth::go::lex(source);
    auto parsed = thoth::go::parse(lexed.tokens);
    auto checked = thoth::go::analyze(*parsed.program);
    EXPECT_TRUE(lexed.diagnostics.empty());
    EXPECT_TRUE(parsed.diagnostics.empty());
    EXPECT_TRUE(checked.diagnostics.empty());
    thoth::ir::Program program = thoth::go::lower(*parsed.program);
    if (optimize) {
        thoth::ir::optimize(program);
    }
    return program;
}

std::string tac(std::string_view source) {
    return thoth::ir::dump(build(source, false));
}

std::string optimized(std::string_view source) {
    return thoth::ir::dump(build(source, true));
}

std::string function_of(const std::string &dump, const std::string &name) {
    std::size_t start = dump.find("func " + name + "(");
    if (start == std::string::npos) {
        return "missing";
    }
    std::size_t end = dump.find("func ", start + 1);
    return dump.substr(start, end == std::string::npos ? std::string::npos
                                                       : end - start);
}

const char *SUM_PROGRAM = "package main\nimport \"fmt\"\nfunc main() {\n"
                          "\tn := 5\n\tsum := 0\n\tfor n > 0 {\n"
                          "\t\tsum = sum + n\n\t\tn = n - 1\n\t}\n"
                          "\tfmt.Println(sum)\n}\n";

} // namespace

TEST(Lowering, WhileStyleLoop) {
    EXPECT_EQ(tac(SUM_PROGRAM), "func main()\n"
                                "  (0) n = 5\n"
                                "  (1) sum = 0\n"
                                "  (2) t1 = n > 0\n"
                                "  (3) ifFalse t1 goto 9\n"
                                "  (4) t2 = sum + n\n"
                                "  (5) sum = t2\n"
                                "  (6) t3 = n - 1\n"
                                "  (7) n = t3\n"
                                "  (8) goto 2\n"
                                "  (9) print_int sum\n"
                                "  (10) print_newline\n"
                                "  (11) return\n");
}

TEST(Lowering, IfElse) {
    std::string dump = tac("package main\nfunc f(x int) int {\n\tif x > 0 {\n"
                           "\t\treturn 1\n\t} else {\n\t\treturn 2\n\t}\n}\n"
                           "func main() {}\n");
    EXPECT_EQ(function_of(dump, "f"), "func f(x)\n"
                                      "  (0) t1 = x > 0\n"
                                      "  (1) ifFalse t1 goto 4\n"
                                      "  (2) return 1\n"
                                      "  (3) goto 5\n"
                                      "  (4) return 2\n"
                                      "  (5) return\n");
}

TEST(Lowering, ShortCircuitAnd) {
    std::string dump = tac("package main\nfunc f(x int, y int) bool {\n"
                           "\treturn x > 0 && y > 0\n}\nfunc main() {}\n");
    EXPECT_EQ(function_of(dump, "f"), "func f(x, y)\n"
                                      "  (0) t2 = x > 0\n"
                                      "  (1) t1 = t2\n"
                                      "  (2) ifFalse t1 goto 5\n"
                                      "  (3) t3 = y > 0\n"
                                      "  (4) t1 = t3\n"
                                      "  (5) return t1\n"
                                      "  (6) return\n");
}

TEST(Lowering, ShortCircuitOrUsesIfTrue) {
    std::string dump = tac("package main\nfunc f(a bool, b bool) bool {\n"
                           "\treturn a || b\n}\nfunc main() {}\n");
    EXPECT_NE(function_of(dump, "f").find("ifTrue t1 goto 3"), std::string::npos);
}

TEST(Lowering, BreakAndContinueTargets) {
    EXPECT_EQ(tac("package main\nfunc main() {\n"
                  "\tfor i := 0; i < 10; i++ {\n"
                  "\t\tif i == 5 {\n\t\t\tbreak\n\t\t}\n"
                  "\t\tif i == 2 {\n\t\t\tcontinue\n\t\t}\n\t}\n}\n"),
              "func main()\n"
              "  (0) i = 0\n"
              "  (1) t1 = i < 10\n"
              "  (2) ifFalse t1 goto 11\n"
              "  (3) t2 = i == 5\n"
              "  (4) ifFalse t2 goto 6\n"
              "  (5) goto 11\n"
              "  (6) t3 = i == 2\n"
              "  (7) ifFalse t3 goto 9\n"
              "  (8) goto 9\n"
              "  (9) i = i + 1\n"
              "  (10) goto 1\n"
              "  (11) return\n");
}

TEST(Lowering, ShadowedVariablesGetDistinctNames) {
    std::string dump = tac("package main\nfunc use(v int) {}\nfunc main() {\n"
                           "\tx := 1\n\t{\n\t\tx := 2\n\t\tuse(x)\n\t}\n"
                           "\tuse(x)\n}\n");
    EXPECT_EQ(function_of(dump, "main"), "func main()\n"
                                         "  (0) x = 1\n"
                                         "  (1) x#2 = 2\n"
                                         "  (2) param x#2\n"
                                         "  (3) call use, 1\n"
                                         "  (4) param x\n"
                                         "  (5) call use, 1\n"
                                         "  (6) return\n");
}

TEST(Lowering, UserVariableThatLooksLikeATemp) {
    std::string dump = tac("package main\nfunc use(v int) {}\nfunc main() {\n"
                           "\tt1 := 4\n\tuse(t1 + 1)\n}\n");
    EXPECT_NE(function_of(dump, "main").find("t1#1 = 4"), std::string::npos);
}

TEST(Lowering, BooleansAreOneAndZero) {
    std::string dump = tac("package main\nfunc useb(v bool) {}\nfunc main() {\n"
                           "\tb := true\n\tuseb(b)\n\tuseb(false)\n}\n");
    EXPECT_EQ(function_of(dump, "main"), "func main()\n"
                                         "  (0) b = 1\n"
                                         "  (1) param b\n"
                                         "  (2) call useb, 1\n"
                                         "  (3) param 0\n"
                                         "  (4) call useb, 1\n"
                                         "  (5) return\n");
}

TEST(Lowering, CallsWithResults) {
    std::string dump = tac("package main\nfunc two() int {\n\treturn 2\n}\n"
                           "func use(v int) {}\nfunc main() {\n\tuse(two() * 3)\n}\n");
    EXPECT_EQ(function_of(dump, "main"), "func main()\n"
                                         "  (0) t1 = call two, 0\n"
                                         "  (1) t2 = t1 * 3\n"
                                         "  (2) param t2\n"
                                         "  (3) call use, 1\n"
                                         "  (4) return\n");
}

TEST(Optimizer, LoopLosesItsTemporaries) {
    EXPECT_EQ(optimized(SUM_PROGRAM), "func main()\n"
                                      "  (0) n = 5\n"
                                      "  (1) sum = 0\n"
                                      "  (2) t1 = n > 0\n"
                                      "  (3) ifFalse t1 goto 7\n"
                                      "  (4) sum = sum + n\n"
                                      "  (5) n = n - 1\n"
                                      "  (6) goto 2\n"
                                      "  (7) print_int sum\n"
                                      "  (8) print_newline\n"
                                      "  (9) return\n");
}

TEST(Optimizer, ConstantFoldingAndPropagation) {
    EXPECT_EQ(optimized("package main\nimport \"fmt\"\nfunc main() {\n"
                        "\tx := 2 * 3 + 4\n\ty := x * 1 + 0\n\tfmt.Println(y)\n}\n"),
              "func main()\n"
              "  (0) print_int 10\n"
              "  (1) print_newline\n"
              "  (2) return\n");
}

TEST(Optimizer, DeadBranchIsRemoved) {
    EXPECT_EQ(optimized("package main\nimport \"fmt\"\nfunc main() {\n"
                        "\tif false {\n\t\tfmt.Println(1)\n\t}\n"
                        "\tfmt.Println(2)\n}\n"),
              "func main()\n"
              "  (0) print_int 2\n"
              "  (1) print_newline\n"
              "  (2) return\n");
}

TEST(Optimizer, CodeAfterReturnIsRemoved) {
    std::string dump =
        optimized("package main\nimport \"fmt\"\nfunc f() {\n\tfmt.Println(1)\n"
                  "\treturn\n\tfmt.Println(5)\n}\nfunc main() {\n\tf()\n}\n");
    EXPECT_EQ(function_of(dump, "f"), "func f()\n"
                                      "  (0) print_int 1\n"
                                      "  (1) print_newline\n"
                                      "  (2) return\n");
}

TEST(Optimizer, DivisionByZeroIsLeftForRuntime) {
    std::string dump = optimized("package main\nimport \"fmt\"\nfunc main() {\n"
                                 "\tx := 0\n\tfmt.Println(10 / x)\n}\n");
    EXPECT_NE(dump.find("10 / 0"), std::string::npos);
}

TEST(Optimizer, MinIntDividedByMinusOneWraps) {
    std::string dump = optimized("package main\nimport \"fmt\"\nfunc main() {\n"
                                 "\tx := -9223372036854775807 - 1\n\ty := -1\n"
                                 "\tfmt.Println(x / y, x % y)\n}\n");
    EXPECT_NE(dump.find("print_int -9223372036854775808"), std::string::npos);
    EXPECT_NE(dump.find("print_int 0"), std::string::npos);
}

TEST(Optimizer, UnusedCallResultKeepsTheCall) {
    std::string dump =
        optimized("package main\nimport \"fmt\"\nfunc f() int {\n"
                  "\tfmt.Println(1)\n\treturn 2\n}\nfunc main() {\n\tf()\n}\n");
    EXPECT_EQ(function_of(dump, "main"), "func main()\n"
                                         "  (0) call f, 0\n"
                                         "  (1) return\n");
}

TEST(ControlFlowGraph, BlocksAndEdges) {
    thoth::ir::Program program = build(SUM_PROGRAM, true);
    auto blocks = thoth::ir::build_blocks(program.functions[0]);

    ASSERT_EQ(blocks.size(), 4u);
    EXPECT_EQ(blocks[0].next, (std::vector<int>{1}));
    EXPECT_EQ(blocks[1].next, (std::vector<int>{3, 2}));
    EXPECT_EQ(blocks[2].next, (std::vector<int>{1}));
    EXPECT_TRUE(blocks[3].next.empty());
    EXPECT_EQ(blocks[1].first, 2);
    EXPECT_EQ(blocks[1].last, 3);
}

TEST(ControlFlowGraph, Dump) {
    std::string cfg = thoth::ir::dump_cfg(build(SUM_PROGRAM, true));
    EXPECT_NE(cfg.find("  B1 (2-3) -> B3 B2\n"), std::string::npos);
    EXPECT_NE(cfg.find("  B2 (4-6) -> B1\n"), std::string::npos);
}

TEST(CodeGen, ProducesIntelAssembly) {
    std::string assembly = thoth::x86::generate(build(SUM_PROGRAM, true));
    EXPECT_NE(assembly.find(".intel_syntax noprefix"), std::string::npos);
    EXPECT_NE(assembly.find("go.main:"), std::string::npos);
    EXPECT_NE(assembly.find("call go.main"), std::string::npos);
    EXPECT_NE(assembly.find("call printf@PLT"), std::string::npos);
    EXPECT_NE(assembly.find("push rbp"), std::string::npos);
    EXPECT_NE(assembly.find("leave"), std::string::npos);
}

TEST(CodeGen, ParametersArriveInRegisters) {
    std::string assembly = thoth::x86::generate(
        build("package main\nfunc add(a int, b int) int {\n\treturn a + b\n}\n"
              "func use(v int) {}\nfunc main() {\n\tuse(add(1, 2))\n}\n",
              false));
    EXPECT_NE(assembly.find("mov QWORD PTR [rbp-8], rdi"), std::string::npos);
    EXPECT_NE(assembly.find("mov QWORD PTR [rbp-16], rsi"), std::string::npos);
    EXPECT_NE(assembly.find("call go.add"), std::string::npos);
}
