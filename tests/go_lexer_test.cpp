#include <gtest/gtest.h>

#include <ostream>
#include <string_view>
#include <vector>

#include "go/lexer.hpp"

namespace thoth::go {

void PrintTo(TokenKind kind, std::ostream *os) { *os << to_string(kind); }

} // namespace thoth::go

using thoth::SourceLocation;
using thoth::go::lex;
using thoth::go::TokenKind;
using enum thoth::go::TokenKind;

namespace {

std::vector<TokenKind> kinds(std::string_view source) {
    std::vector<TokenKind> result;
    for (const auto &token : lex(source).tokens) {
        result.push_back(token.kind);
    }
    return result;
}

} // namespace

TEST(GoLexer, EmptySource) {
    EXPECT_EQ(kinds(""), std::vector<TokenKind>{EndOfFile});
}

TEST(GoLexer, Keywords) {
    EXPECT_EQ(kinds("package import func var if else for return break continue"),
              (std::vector<TokenKind>{Package, Import, Func, Var, If, Else, For,
                                      Return, Break, Continue, Semicolon,
                                      EndOfFile}));
}

TEST(GoLexer, TypesAndBooleansAreIdentifiers) {
    EXPECT_EQ(kinds("int bool true false"),
              (std::vector<TokenKind>{Identifier, Identifier, Identifier,
                                      Identifier, Semicolon, EndOfFile}));
}

TEST(GoLexer, Operators) {
    EXPECT_EQ(kinds("+ - * / % = := += -= *= /= %= == != < <= > >= && || ! ( { , ; ."),
              (std::vector<TokenKind>{Plus, Minus, Star, Slash, Percent, Assign,
                                      Define, PlusAssign, MinusAssign, StarAssign,
                                      SlashAssign, PercentAssign, Equal, NotEqual,
                                      Less, LessEqual, Greater, GreaterEqual,
                                      AndAnd, OrOr, Not, LeftParen, LeftBrace,
                                      Comma, Semicolon, Dot, EndOfFile}));
}

TEST(GoLexer, IncrementAndDecrement) {
    EXPECT_EQ(kinds("i++ j--"),
              (std::vector<TokenKind>{Identifier, Increment, Identifier,
                                      Decrement, Semicolon, EndOfFile}));
}

TEST(GoLexer, SemicolonInsertedAfterNameAtNewline) {
    EXPECT_EQ(kinds("x\ny\n"),
              (std::vector<TokenKind>{Identifier, Semicolon, Identifier,
                                      Semicolon, EndOfFile}));
}

TEST(GoLexer, SemicolonInsertedAfterEachQualifyingToken) {
    for (std::string_view line :
         {"x\n", "42\n", "\"s\"\n", "return\n", "break\n", "continue\n",
          "i++\n", "i--\n", ")\n", "}\n"}) {
        auto tokens = lex(line).tokens;
        ASSERT_GE(tokens.size(), 2u) << line;
        EXPECT_EQ(tokens[tokens.size() - 2].kind, Semicolon) << line;
    }
}

TEST(GoLexer, NoSemicolonAfterOperatorsOrOpeners) {
    EXPECT_EQ(kinds("x +\ny"),
              (std::vector<TokenKind>{Identifier, Plus, Identifier, Semicolon,
                                      EndOfFile}));
    EXPECT_EQ(kinds("f(a,\nb)"),
              (std::vector<TokenKind>{Identifier, LeftParen, Identifier, Comma,
                                      Identifier, RightParen, Semicolon,
                                      EndOfFile}));
    EXPECT_EQ(kinds("{\n}"),
              (std::vector<TokenKind>{LeftBrace, RightBrace, Semicolon,
                                      EndOfFile}));
}

TEST(GoLexer, InsertedSemicolonHasNoTextAndSitsAtTheNewline) {
    auto tokens = lex("ab\n").tokens;

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].kind, Semicolon);
    EXPECT_EQ(tokens[1].lexeme, "");
    EXPECT_EQ(tokens[1].location, (SourceLocation{1, 3}));
}

TEST(GoLexer, WrittenSemicolonKeepsItsText) {
    auto tokens = lex("x;").tokens;

    ASSERT_EQ(tokens.size(), 3u);
    EXPECT_EQ(tokens[1].lexeme, ";");
}

TEST(GoLexer, LineCommentEndsLikeANewline) {
    EXPECT_EQ(kinds("x // note\ny"),
              (std::vector<TokenKind>{Identifier, Semicolon, Identifier,
                                      Semicolon, EndOfFile}));
}

TEST(GoLexer, MultiLineBlockCommentActsAsNewline) {
    EXPECT_EQ(kinds("x /* a\n b */ y"),
              (std::vector<TokenKind>{Identifier, Semicolon, Identifier,
                                      Semicolon, EndOfFile}));
    EXPECT_EQ(kinds("x /* same line */ y"),
              (std::vector<TokenKind>{Identifier, Identifier, Semicolon,
                                      EndOfFile}));
}

TEST(GoLexer, StringLiteralKeepsQuotes) {
    auto tokens = lex("import \"fmt\"").tokens;

    ASSERT_EQ(tokens.size(), 4u);
    EXPECT_EQ(tokens[1].kind, String);
    EXPECT_EQ(tokens[1].lexeme, "\"fmt\"");
}

TEST(GoLexer, TracksLineAndColumn) {
    auto tokens = lex("func main() {\n\tx := 1\n}").tokens;

    ASSERT_GE(tokens.size(), 8u);
    EXPECT_EQ(tokens[0].location, (SourceLocation{1, 1}));
    EXPECT_EQ(tokens[1].location, (SourceLocation{1, 6}));
    EXPECT_EQ(tokens[5].location, (SourceLocation{2, 2}));
    EXPECT_EQ(tokens[6].location, (SourceLocation{2, 4}));
    EXPECT_EQ(tokens[7].location, (SourceLocation{2, 7}));
}

TEST(GoLexer, NumbersAreValid) {
    auto result = lex("0 7 1234567");

    EXPECT_TRUE(result.diagnostics.empty());
    EXPECT_EQ(result.tokens[2].lexeme, "1234567");
}

TEST(GoLexer, LeadingZeroIsRejected) {
    auto result = lex("x := 012");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 6}));
}

TEST(GoLexer, NumberFollowedByLettersIsRejected) {
    auto result = lex("12ab");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 1}));
}

TEST(GoLexer, InvalidCharactersAreReportedAndSkipped) {
    auto result = lex("a @ b & c | d : e");

    ASSERT_EQ(result.diagnostics.size(), 4u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 3}));
    EXPECT_EQ(result.diagnostics[1].location, (SourceLocation{1, 7}));
    EXPECT_EQ(result.diagnostics[2].location, (SourceLocation{1, 11}));
    EXPECT_EQ(result.diagnostics[3].location, (SourceLocation{1, 15}));
}

TEST(GoLexer, UnterminatedString) {
    auto result = lex("\"abc\nx");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 1}));
}

TEST(GoLexer, UnterminatedComment) {
    auto result = lex("x /* never closed");

    ASSERT_EQ(result.diagnostics.size(), 1u);
    EXPECT_EQ(result.diagnostics[0].location, (SourceLocation{1, 3}));
}

TEST(GoLexer, EveryKindHasAName) {
    for (auto kind : {Package, Identifier, Define, Increment, Dot, EndOfFile}) {
        EXPECT_NE(to_string(kind), "Unknown");
    }
}
