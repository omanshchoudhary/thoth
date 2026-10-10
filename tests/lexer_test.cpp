#include <gtest/gtest.h>

#include <ostream>
#include <string_view>
#include <vector>

#include "c/lexer.hpp"

namespace thoth::c {

void PrintTo(TokenKind kind, std::ostream *os) { *os << to_string(kind); }

} // namespace thoth::c

using thoth::SourceLocation;
using thoth::c::lex;
using thoth::c::TokenKind;
using enum thoth::c::TokenKind;

namespace {

std::vector<TokenKind> kinds(std::string_view source) {
    std::vector<TokenKind> result;
    for (const auto &token : lex(source).tokens) {
        result.push_back(token.kind);
    }
    return result;
}

std::vector<std::string_view> lexemes(std::string_view source) {
    std::vector<std::string_view> result;
    for (const auto &token : lex(source).tokens) {
        result.push_back(token.lexeme);
    }
    return result;
}

} // namespace

TEST(Lexer, EmptySourceYieldsOnlyEndOfFile) {
    EXPECT_EQ(kinds(""), std::vector<TokenKind>{EndOfFile});
}

TEST(Lexer, WhitespaceOnlyYieldsOnlyEndOfFile) {
    EXPECT_EQ(kinds(" \t\n\r\n  "), std::vector<TokenKind>{EndOfFile});
}

TEST(Lexer, Keywords) {
    EXPECT_EQ(kinds("int return if else while"),
              (std::vector<TokenKind>{Int, Return, If, Else, While, EndOfFile}));
}

TEST(Lexer, KeywordLookalikesAreIdentifiers) {
    EXPECT_EQ(kinds("integer returns iff _if while2 Int"),
              (std::vector<TokenKind>{Identifier, Identifier, Identifier, Identifier, Identifier,
                                      Identifier, EndOfFile}));
}

TEST(Lexer, IdentifierLexemes) {
    EXPECT_EQ(lexemes("_foo bar9 Baz x"),
              (std::vector<std::string_view>{"_foo", "bar9", "Baz", "x", ""}));
}

TEST(Lexer, IntegerLiterals) {
    EXPECT_EQ(kinds("0 42 123456"),
              (std::vector<TokenKind>{Integer, Integer, Integer, EndOfFile}));
    EXPECT_EQ(lexemes("0 42 123456"), (std::vector<std::string_view>{"0", "42", "123456", ""}));
}

TEST(Lexer, SingleCharacterOperators) {
    EXPECT_EQ(kinds("+ - * / % = < > !"),
              (std::vector<TokenKind>{Plus, Minus, Multiply, Divide, Modulo, Assign, Less, Greater,
                                      Not, EndOfFile}));
}

TEST(Lexer, TwoCharacterOperators) {
    EXPECT_EQ(kinds("== != <= >= && ||"),
              (std::vector<TokenKind>{Equal, NotEqual, LessEqual, GreaterEqual, And, Or,
                                      EndOfFile}));
    EXPECT_EQ(lexemes("== != <= >= && ||"),
              (std::vector<std::string_view>{"==", "!=", "<=", ">=", "&&", "||", ""}));
}

TEST(Lexer, Punctuation) {
    EXPECT_EQ(kinds("( ) { } ; ,"),
              (std::vector<TokenKind>{LeftParen, RightParen, LeftBrace, RightBrace, Semicolon, Comma,
                                      EndOfFile}));
}

TEST(Lexer, MaximalMunch) {
    EXPECT_EQ(kinds(">=="), (std::vector<TokenKind>{GreaterEqual, Assign, EndOfFile}));
    EXPECT_EQ(kinds("!=="), (std::vector<TokenKind>{NotEqual, Assign, EndOfFile}));
    EXPECT_EQ(kinds("==="), (std::vector<TokenKind>{Equal, Assign, EndOfFile}));
    EXPECT_EQ(kinds("&&&&"), (std::vector<TokenKind>{And, And, EndOfFile}));
}

TEST(Lexer, NoWhitespaceBetweenTokens) {
    EXPECT_EQ(kinds("x=y+1;"),
              (std::vector<TokenKind>{Identifier, Assign, Identifier, Plus, Integer,
                                      Semicolon, EndOfFile}));
    EXPECT_EQ(kinds("a<=b"), (std::vector<TokenKind>{Identifier, LessEqual, Identifier, EndOfFile}));
}

TEST(Lexer, SmallProgram) {
    EXPECT_EQ(kinds("int main() {\n    return 0;\n}\n"),
              (std::vector<TokenKind>{Int, Identifier, LeftParen, RightParen, LeftBrace, Return,
                                      Integer, Semicolon, RightBrace, EndOfFile}));
}

TEST(Lexer, LineCommentIsSkipped) {
    EXPECT_EQ(kinds("a // b c\nd"), (std::vector<TokenKind>{Identifier, Identifier, EndOfFile}));
    EXPECT_EQ(lexemes("a // b c\nd"), (std::vector<std::string_view>{"a", "d", ""}));
}

TEST(Lexer, LineCommentAtEndOfFile) {
    EXPECT_EQ(kinds("a // no newline"), (std::vector<TokenKind>{Identifier, EndOfFile}));
}

TEST(Lexer, BlockCommentIsSkipped) {
    EXPECT_EQ(lexemes("a /* b \n c */ d"), (std::vector<std::string_view>{"a", "d", ""}));
}

TEST(Lexer, BlockCommentsDoNotNest) {
    EXPECT_EQ(lexemes("/* a /* b */ c"), (std::vector<std::string_view>{"c", ""}));
}

TEST(Lexer, SlashAloneIsDivision) {
    EXPECT_EQ(kinds("a / b"), (std::vector<TokenKind>{Identifier, Divide, Identifier, EndOfFile}));
}

TEST(Lexer, TracksLineAndColumn) {
    auto tokens = lex("int x;\n  return 0;").tokens;

    ASSERT_EQ(tokens.size(), 7u);
    EXPECT_EQ(tokens[0].location, (SourceLocation{1, 1}));
    EXPECT_EQ(tokens[1].location, (SourceLocation{1, 5}));
    EXPECT_EQ(tokens[2].location, (SourceLocation{1, 6}));
    EXPECT_EQ(tokens[3].location, (SourceLocation{2, 3}));
    EXPECT_EQ(tokens[4].location, (SourceLocation{2, 10}));
    EXPECT_EQ(tokens[5].location, (SourceLocation{2, 11}));
}

TEST(Lexer, LocationAfterMultiLineBlockComment) {
    auto tokens = lex("/* a\nb */ x").tokens;

    ASSERT_EQ(tokens.size(), 2u);
    EXPECT_EQ(tokens[0].location, (SourceLocation{2, 6}));
}

TEST(Lexer, ValidSourceHasNoDiagnostics) {
    EXPECT_TRUE(lex("int main() {\n    return 0;\n}\n").diagnostics.empty());
}

TEST(Lexer, InvalidCharacterIsReportedAndSkipped) {
    auto result = lex("a @ b");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 3}));
    EXPECT_FALSE(result.diagnostics[0].message.empty());
    EXPECT_EQ(kinds("a @ b"), (std::vector<TokenKind>{Identifier, Identifier, EndOfFile}));
}

TEST(Lexer, ReportsEveryInvalidCharacter) {
    auto result = lex("@\n$ `");

    ASSERT_EQ(result.diagnostics.size(), 3u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 1}));
    EXPECT_EQ(result.diagnostics[1].location, (SourceLocation{2, 1}));
    EXPECT_EQ(result.diagnostics[2].location, (SourceLocation{2, 3}));
}

TEST(Lexer, PreprocessorDirectiveIsInvalid) {
    auto result = lex("#include");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 1}));
}

TEST(Lexer, LoneAmpersandAndPipeAreInvalid) {
    auto result = lex("a & b | c");

    ASSERT_EQ(result.diagnostics.size(), 2u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 3}));
    EXPECT_EQ(result.diagnostics[1].location, (SourceLocation{1, 7}));
}

TEST(Lexer, UnterminatedBlockComment) {
    auto result = lex("a /* never closed");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 3}));
    EXPECT_EQ(kinds("a /* never closed"), (std::vector<TokenKind>{Identifier, EndOfFile}));
}

TEST(Lexer, EveryKindHasAName) {
    for (auto kind : {Int, Identifier, Integer, Or, Comma, EndOfFile}) {
        EXPECT_FALSE(to_string(kind).empty());
    }
}
