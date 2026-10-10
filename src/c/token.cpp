#include "c/token.hpp"

namespace thoth::c {

std::string_view to_string(TokenKind kind) {
    switch (kind) {
    case TokenKind::Int:
        return "Int";
    case TokenKind::Return:
        return "Return";
    case TokenKind::If:
        return "If";
    case TokenKind::Else:
        return "Else";
    case TokenKind::While:
        return "While";
    case TokenKind::Identifier:
        return "Identifier";
    case TokenKind::Integer:
        return "Integer";
    case TokenKind::Plus:
        return "Plus";
    case TokenKind::Minus:
        return "Minus";
    case TokenKind::Multiply:
        return "Multiply";
    case TokenKind::Divide:
        return "Divide";
    case TokenKind::Modulo:
        return "Modulo";
    case TokenKind::Assign:
        return "Assign";
    case TokenKind::Equal:
        return "Equal";
    case TokenKind::NotEqual:
        return "NotEqual";
    case TokenKind::Less:
        return "Less";
    case TokenKind::LessEqual:
        return "LessEqual";
    case TokenKind::Greater:
        return "Greater";
    case TokenKind::GreaterEqual:
        return "GreaterEqual";
    case TokenKind::And:
        return "And";
    case TokenKind::Or:
        return "Or";
    case TokenKind::Not:
        return "Not";
    case TokenKind::LeftParen:
        return "LeftParen";
    case TokenKind::RightParen:
        return "RightParen";
    case TokenKind::LeftBrace:
        return "LeftBrace";
    case TokenKind::RightBrace:
        return "RightBrace";
    case TokenKind::Semicolon:
        return "Semicolon";
    case TokenKind::Comma:
        return "Comma";
    case TokenKind::EndOfFile:
        return "EndOfFile";
    }
    return "Unknown";
}

} // namespace thoth::c
