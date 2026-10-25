#pragma once

#include <string_view>

#include "source.hpp"

namespace thoth::go {

enum class TokenKind {
    Package,
    Import,
    Func,
    Var,
    If,
    Else,
    For,
    Return,
    Break,
    Continue,

    Identifier,
    Integer,
    String,

    Plus,
    Minus,
    Star,
    Slash,
    Percent,

    Assign,
    Define,
    PlusAssign,
    MinusAssign,
    StarAssign,
    SlashAssign,
    PercentAssign,
    Increment,
    Decrement,

    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    AndAnd,
    OrOr,
    Not,

    LeftParen,
    RightParen,
    LeftBrace,
    RightBrace,
    Comma,
    Semicolon,
    Dot,

    EndOfFile,
};

struct Token {
    TokenKind kind;
    std::string_view lexeme;
    SourceLocation location;
};

std::string_view to_string(TokenKind kind);

} // namespace thoth::go
