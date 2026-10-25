#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "go/lexer.hpp"
#include "go/parser.hpp"
#include "go/sema.hpp"

namespace {

std::vector<std::string> errors(std::string_view source) {
    auto lexed = thoth::go::lex(source);
    auto parsed = thoth::go::parse(lexed.tokens);
    if (!lexed.diagnostics.empty() || !parsed.diagnostics.empty()) {
        return {"did not parse"};
    }
    auto checked = thoth::go::analyze(*parsed.program);
    auto diagnostics = checked.diagnostics;
    std::stable_sort(diagnostics.begin(), diagnostics.end(),
                     [](const thoth::Diagnostic &a, const thoth::Diagnostic &b) {
                         if (a.location.line != b.location.line) {
                             return a.location.line < b.location.line;
                         }
                         return a.location.column < b.location.column;
                     });
    std::vector<std::string> result;
    for (const auto &diagnostic : diagnostics) {
        result.push_back(std::to_string(diagnostic.location.line) + ":" +
                         std::to_string(diagnostic.location.column) + " " +
                         diagnostic.message);
    }
    return result;
}

std::vector<std::string> in_main(std::string_view statements) {
    std::string source = "package main\nfunc use(v int) {}\nfunc useb(v bool) {}\n"
                         "func main() {\n";
    source += statements;
    source += "\n}\n";
    return errors(source);
}

using Errors = std::vector<std::string>;

} // namespace

TEST(GoSema, ValidProgramHasNoErrors) {
    EXPECT_EQ(errors("package main\n"
                     "import \"fmt\"\n"
                     "func add(a, b int) int {\n\treturn a + b\n}\n"
                     "func sign(n int) int {\n"
                     "\tif n < 0 {\n\t\treturn -1\n\t} else if n == 0 {\n"
                     "\t\treturn 0\n\t} else {\n\t\treturn 1\n\t}\n}\n"
                     "func forever() int {\n\tfor {\n\t}\n}\n"
                     "func main() {\n"
                     "\tvar a int\n\tvar b = true\n\tvar c int = 3\n"
                     "\tx := add(a, c)\n\tx += 2\n\tx++\n"
                     "\tfor i := 0; i < x; i++ {\n\t\tif i == 2 {\n\t\t\tcontinue\n"
                     "\t\t}\n\t\tfmt.Println(i, b && !b, sign(i))\n\t}\n"
                     "\tif v := add(1, 2); v > 2 {\n\t\tfmt.Println(v)\n\t}\n"
                     "}\n"),
              Errors{});
}

TEST(GoSema, UnusedVariable) {
    EXPECT_EQ(in_main("x := 1"), Errors{"5:1 declared and not used: x"});
}

TEST(GoSema, AssignmentAloneIsNotAUse) {
    EXPECT_EQ(in_main("x := 1\nx = 2"), Errors{"5:1 declared and not used: x"});
}

TEST(GoSema, UnusedParametersAreFine) {
    EXPECT_EQ(errors("package main\nfunc f(a int) {}\nfunc main() {}\n"), Errors{});
}

TEST(GoSema, Undefined) {
    EXPECT_EQ(in_main("use(y)"), Errors{"5:5 undefined: y"});
}

TEST(GoSema, ShortDeclarationNeedsANewName) {
    EXPECT_EQ(in_main("x := 1\nx := 2\nuse(x)"),
              Errors{"6:1 no new variables on left side of :="});
}

TEST(GoSema, Redeclared) {
    EXPECT_EQ(in_main("var x int\nvar x int\nuse(x)"),
              Errors{"6:5 x redeclared in this block"});
}

TEST(GoSema, ShadowingInInnerBlockIsFine) {
    EXPECT_EQ(in_main("x := 1\n{\n\tx := true\n\tuseb(x)\n}\nuse(x)"), Errors{});
}

TEST(GoSema, DeclarationTypeMismatch) {
    EXPECT_EQ(in_main("var b bool = 3\nuseb(b)"),
              Errors{"5:14 cannot use 3 (value of type int) as bool value in "
                     "variable declaration"});
}

TEST(GoSema, AssignmentTypeMismatch) {
    EXPECT_EQ(in_main("x := 1\nx = true\nuse(x)"),
              Errors{"6:5 cannot use true (value of type bool) as int value in "
                     "assignment"});
}

TEST(GoSema, CompoundAssignmentMismatch) {
    EXPECT_EQ(in_main("b := true\nb += 1\nuseb(b)"),
              Errors{"6:1 invalid operation: b += 1 (mismatched types bool and "
                     "int)"});
}

TEST(GoSema, IncrementNeedsInt) {
    EXPECT_EQ(in_main("b := true\nb++\nuseb(b)"),
              Errors{"6:1 invalid operation: b++ (non-numeric type bool)"});
}

TEST(GoSema, BinaryMismatch) {
    EXPECT_EQ(in_main("use(1 + true)"),
              Errors{"5:5 invalid operation: 1 + true (mismatched types int and "
                     "bool)"});
}

TEST(GoSema, OrderingNeedsInts) {
    EXPECT_EQ(in_main("useb(true < false)"),
              Errors{"5:6 invalid operation: operator < not defined on true "
                     "(value of type bool)"});
}

TEST(GoSema, LogicalNeedsBools) {
    EXPECT_EQ(in_main("useb(1 && 2)"),
              Errors{"5:6 invalid operation: operator && not defined on 1 "
                     "(value of type int)"});
}

TEST(GoSema, UnaryOperators) {
    EXPECT_EQ(in_main("use(-true)"),
              Errors{"5:5 invalid operation: operator - not defined on true "
                     "(value of type bool)"});
    EXPECT_EQ(in_main("useb(!1)"),
              Errors{"5:6 invalid operation: operator ! not defined on 1 "
                     "(value of type int)"});
}

TEST(GoSema, ConditionsMustBeBool) {
    EXPECT_EQ(in_main("if 1 {\n}"),
              Errors{"5:4 non-boolean condition in if statement"});
    EXPECT_EQ(in_main("for 1 {\n}"),
              Errors{"5:5 non-boolean condition in for statement"});
}

TEST(GoSema, BreakAndContinueOutsideLoop) {
    EXPECT_EQ(in_main("break"), Errors{"5:1 break is not in a loop"});
    EXPECT_EQ(in_main("continue"), Errors{"5:1 continue is not in a loop"});
}

TEST(GoSema, DivisionByConstantZero) {
    EXPECT_EQ(in_main("use(1 / 0)"),
              Errors{"5:9 invalid operation: division by zero"});
    EXPECT_EQ(in_main("x := 1\nx %= 0\nuse(x)"),
              Errors{"6:6 invalid operation: division by zero"});
}

TEST(GoSema, UnusedExpression) {
    EXPECT_EQ(in_main("1 + 2"),
              Errors{"5:1 1 + 2 (value of type int) is not used"});
}

TEST(GoSema, NoValueUsedAsValue) {
    EXPECT_EQ(in_main("use(use(1))"),
              Errors{"5:5 use(1) (no value) used as value"});
}

TEST(GoSema, CallArity) {
    EXPECT_EQ(in_main("use(1, 2)"),
              Errors{"5:1 too many arguments in call to use"});
    EXPECT_EQ(in_main("use()"), Errors{"5:1 not enough arguments in call to use"});
}

TEST(GoSema, ArgumentType) {
    EXPECT_EQ(in_main("use(true)"),
              Errors{"5:5 cannot use true (value of type bool) as int value in "
                     "argument to use"});
}

TEST(GoSema, CallingAVariable) {
    EXPECT_EQ(in_main("x := 1\nx()"),
              Errors{"6:1 invalid operation: cannot call non-function x"});
}

TEST(GoSema, AssigningToNonVariables) {
    EXPECT_EQ(in_main("use = 1"), Errors{"5:1 cannot assign to use"});
    EXPECT_EQ(in_main("true = false"), Errors{"5:1 cannot assign to true"});
}

TEST(GoSema, TypesAndNames) {
    EXPECT_EQ(in_main("x := int\nuse(x)"),
              Errors{"5:6 int (type) is not an expression"});
    EXPECT_EQ(in_main("var x foo\nuse(x)"), Errors{"5:7 undefined: foo"});
    EXPECT_EQ(in_main("var x use\nuse(x)"), Errors{"5:7 use is not a type"});
}

TEST(GoSema, PackageMustBeMain) {
    EXPECT_EQ(errors("package other\nfunc main() {}\n"),
              Errors{"1:9 package other is not main: only package main can be "
                     "compiled"});
}

TEST(GoSema, MainIsRequired) {
    EXPECT_EQ(errors("package main\nfunc f() {}\n"),
              Errors{"1:9 function main is undeclared in the main package"});
    EXPECT_EQ(errors("package main\nfunc main() int {\n\treturn 1\n}\n"),
              Errors{"2:6 func main must have no arguments and no return values"});
}

TEST(GoSema, Imports) {
    EXPECT_EQ(errors("package main\nimport \"fmt\"\nfunc main() {}\n"),
              Errors{"2:8 \"fmt\" imported and not used"});
    EXPECT_EQ(errors("package main\nimport \"os\"\nfunc main() {}\n"),
              Errors{"2:8 unsupported import \"os\": only \"fmt\" is available"});
    EXPECT_EQ(errors("package main\nimport \"fmt\"\nfunc main() {\n"
                     "\tfmt.Printf(1)\n}\n"),
              Errors{"4:2 undefined: fmt.Printf (only fmt.Println is supported)"});
}

TEST(GoSema, DuplicateFunction) {
    EXPECT_EQ(errors("package main\nfunc f() {}\nfunc f() {}\nfunc main() {}\n"),
              Errors{"3:6 f redeclared in this block"});
}

TEST(GoSema, MissingReturn) {
    EXPECT_EQ(errors("package main\nfunc f(a int) int {\n\tif a > 0 {\n"
                     "\t\treturn 1\n\t}\n}\nfunc main() {}\n"),
              Errors{"6:1 missing return"});
    EXPECT_EQ(errors("package main\nfunc f() int {\n\tfor {\n\t\tbreak\n\t}\n}\n"
                     "func main() {}\n"),
              Errors{"6:1 missing return"});
}

TEST(GoSema, ReturnChecks) {
    EXPECT_EQ(errors("package main\nfunc f() int {\n\treturn true\n}\n"
                     "func main() {}\n"),
              Errors{"3:9 cannot use true (value of type bool) as int value in "
                     "return statement"});
    EXPECT_EQ(errors("package main\nfunc g() {\n\treturn 1\n}\nfunc main() {}\n"),
              Errors{"3:9 too many return values"});
    EXPECT_EQ(errors("package main\nfunc h() int {\n\treturn\n}\nfunc main() {}\n"),
              Errors{"3:2 not enough return values (want int)"});
}

TEST(GoSema, TooManyParameters) {
    EXPECT_EQ(errors("package main\nfunc f(a, b, c, d, e, f, g int) {}\n"
                     "func main() {}\n"),
              Errors{"2:6 functions with more than 6 parameters are not "
                     "supported"});
}

TEST(GoSema, SymbolTable) {
    auto lexed = thoth::go::lex("package main\nimport \"fmt\"\n"
                                "func add(a int, b int) int {\n\treturn a + b\n}\n"
                                "func main() {\n\tx := add(1, 2)\n"
                                "\tfor i := 0; i < x; i++ {\n\t\tfmt.Println(i)\n"
                                "\t}\n}\n");
    auto parsed = thoth::go::parse(lexed.tokens);
    auto checked = thoth::go::analyze(*parsed.program);

    EXPECT_TRUE(checked.diagnostics.empty());
    EXPECT_EQ(checked.symbol_table, "import fmt  (2:8)\n"
                                    "func add(a int, b int) int  (3:6)\n"
                                    "  param a int  (3:10)\n"
                                    "  param b int  (3:17)\n"
                                    "func main()  (6:6)\n"
                                    "  var x int  (7:2)\n"
                                    "    var i int  (8:6)\n");
}
