#include <gtest/gtest.h>

#include <string>
#include <string_view>

#include "go/ast.hpp"
#include "go/lexer.hpp"
#include "go/parser.hpp"

namespace {

std::string parse(std::string_view source) {
    auto lexed = thoth::go::lex(source);
    if (!lexed.diagnostics.empty()) {
        return "lex error";
    }
    auto parsed = thoth::go::parse(lexed.tokens);
    if (!parsed.diagnostics.empty()) {
        const auto &location = parsed.diagnostics[0].location;
        return "error " + std::to_string(location.line) + ":" +
               std::to_string(location.column);
    }
    return thoth::go::dump(*parsed.program);
}

std::string body(std::string_view statements) {
    std::string source = "package main\nfunc main() {\n";
    source += statements;
    source += "\n}\n";
    std::string dumped = parse(source);
    std::string prefix = "package main\nfunc main()\n  block\n";
    if (dumped.rfind(prefix, 0) != 0) {
        return dumped;
    }
    return dumped.substr(prefix.size());
}

int error_count(std::string_view source) {
    auto lexed = thoth::go::lex(source);
    return static_cast<int>(thoth::go::parse(lexed.tokens).diagnostics.size());
}

} // namespace

TEST(GoParser, MinimalProgram) {
    EXPECT_EQ(parse("package main\nfunc main() {\n}\n"),
              "package main\nfunc main()\n  block\n");
}

TEST(GoParser, Imports) {
    EXPECT_EQ(parse("package main\nimport \"fmt\"\nfunc main() {}\n"),
              "package main\nimport \"fmt\"\nfunc main()\n  block\n");
    EXPECT_EQ(parse("package main\nimport (\n\t\"fmt\"\n)\nfunc main() {}\n"),
              "package main\nimport \"fmt\"\nfunc main()\n  block\n");
}

TEST(GoParser, FunctionSignatures) {
    EXPECT_EQ(parse("package main\nfunc f(a, b int, c bool) int {\n\treturn a\n}\n"),
              "package main\nfunc f(a int, b int, c bool) int\n  block\n"
              "    return a\n");
}

TEST(GoParser, Precedence) {
    EXPECT_EQ(body("x = 1 + 2 * 3 == 7 && !y || z"),
              "    assign x = (|| (&& (== (+ 1 (* 2 3)) 7) (! y)) z)\n");
    EXPECT_EQ(body("x = a - b - c"), "    assign x = (- (- a b) c)\n");
    EXPECT_EQ(body("x = a < b == c"), "    assign x = (== (< a b) c)\n");
    EXPECT_EQ(body("x = -(a + b) % 3"), "    assign x = (% (- (+ a b)) 3)\n");
}

TEST(GoParser, Declarations) {
    EXPECT_EQ(body("var a int\nvar b = 2\nvar c int = 3\nd := a + b"),
              "    var a int\n    var b = 2\n    var c int = 3\n"
              "    define d := (+ a b)\n");
}

TEST(GoParser, AssignmentsAndIncDec) {
    EXPECT_EQ(body("x += 2\nx %= 3\nx++\nx--"),
              "    assign x += 2\n    assign x %= 3\n    incdec x++\n"
              "    incdec x--\n");
}

TEST(GoParser, Calls) {
    EXPECT_EQ(body("fmt.Println(1, f(x), g())"),
              "    expr (call fmt.Println 1 (call f x) (call g))\n");
    EXPECT_EQ(body("f(1,\n2,\n)"), "    expr (call f 1 2)\n");
}

TEST(GoParser, IfElseChain) {
    EXPECT_EQ(body("if a {\n} else if b {\n} else {\n}"),
              "    if a\n      block\n    else\n      if b\n        block\n"
              "      else\n        block\n");
}

TEST(GoParser, IfWithInit) {
    EXPECT_EQ(body("if v := f(); v > 0 {\n}"),
              "    if (> v 0)\n      init: define v := (call f)\n"
              "      block\n");
}

TEST(GoParser, ForForms) {
    EXPECT_EQ(body("for {\n\tbreak\n}"),
              "    for\n      block\n        break\n");
    EXPECT_EQ(body("for n > 0 {\n\tcontinue\n}"),
              "    for\n      cond: (> n 0)\n      block\n        continue\n");
    EXPECT_EQ(body("for i := 0; i < 3; i++ {\n}"),
              "    for\n      init: define i := 0\n      cond: (< i 3)\n"
              "      post: incdec i++\n      block\n");
    EXPECT_EQ(body("for ;; {\n}"), "    for\n      block\n");
}

TEST(GoParser, Return) {
    EXPECT_EQ(body("return"), "    return\n");
    EXPECT_EQ(parse("package main\nfunc f() int { return 1 }\n"),
              "package main\nfunc f() int\n  block\n    return 1\n");
}

TEST(GoParser, NestedBlocksAndEmptyStatements) {
    EXPECT_EQ(body(";\n{\n\t;\n}"), "    block\n");
}

TEST(GoParser, MissingPackage) {
    EXPECT_EQ(parse("func main() {}\n"), "error 1:1");
}

TEST(GoParser, MissingClosingParenthesis) {
    EXPECT_EQ(body("x := (1 + 2\ny := 3"), "error 3:12");
}

TEST(GoParser, BraceMustBeOnTheSameLine) {
    EXPECT_EQ(parse("package main\nfunc main()\n{\n}\n"), "error 2:12");
}

TEST(GoParser, ElseOnItsOwnLine) {
    EXPECT_EQ(body("if a {\n}\nelse {\n}"), "error 5:1");
}

TEST(GoParser, MissingCondition) {
    EXPECT_EQ(body("if {\n}"), "error 3:4");
}

TEST(GoParser, StatementAsConditionIsRejected) {
    EXPECT_EQ(body("if x := 1 {\n}"), "error 3:4");
}

TEST(GoParser, CannotDeclareInForPost) {
    EXPECT_EQ(body("for i := 0; i < 3; j := 1 {\n}"), "error 3:20");
}

TEST(GoParser, CannotAssignToExpression) {
    EXPECT_EQ(body("1 = 2"), "error 3:3");
    EXPECT_EQ(body("a + b++"), "error 3:6");
}

TEST(GoParser, MultipleAssignmentNotSupported) {
    EXPECT_EQ(body("a, b := 1, 2"), "error 3:2");
}

TEST(GoParser, MultipleResultsNotSupported) {
    EXPECT_EQ(parse("package main\nfunc f() (int, int) {\n}\n"), "error 2:10");
}

TEST(GoParser, MissingParameterType) {
    EXPECT_EQ(parse("package main\nfunc f(a, b) {\n}\n"), "error 2:12");
}

TEST(GoParser, VarNeedsTypeOrValue) {
    EXPECT_EQ(body("var x"), "error 3:6");
}

TEST(GoParser, IntegerTooLarge) {
    EXPECT_EQ(body("x := 9223372036854775807"), "    define x := 9223372036854775807\n");
    EXPECT_EQ(body("x := 9223372036854775808"), "error 3:6");
}

TEST(GoParser, StatementOutsideFunction) {
    EXPECT_EQ(parse("package main\nx := 1\nfunc main() {}\n"), "error 2:1");
}

TEST(GoParser, RecoversAndReportsEveryStatementError) {
    EXPECT_EQ(error_count("package main\nfunc main() {\n\tx := (1\n\ty := 2 +\n"
                          "\tfor i := 0; i < 3; j := 1 {\n\t}\n\tz := 3\n}\n"),
              3);
}

TEST(GoParser, RecoveryDoesNotLeakIntoNextFunction) {
    EXPECT_EQ(error_count("package main\nfunc a() {\n\t)\n}\nfunc b() {\n\t)\n}\n"),
              2);
}
