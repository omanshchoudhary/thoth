#include "go/token.hpp"

namespace thoth::go {

std::string_view to_string(TokenKind kind) {
    const char *names[] = {
        "Package",     "Import",        "Func",        "Var",
        "If",          "Else",          "For",         "Return",
        "Break",       "Continue",      "Identifier",  "Integer",
        "String",      "Plus",          "Minus",       "Star",
        "Slash",       "Percent",       "Assign",      "Define",
        "PlusAssign",  "MinusAssign",   "StarAssign",  "SlashAssign",
        "PercentAssign", "Increment",   "Decrement",   "Equal",
        "NotEqual",    "Less",          "LessEqual",   "Greater",
        "GreaterEqual", "AndAnd",       "OrOr",        "Not",
        "LeftParen",   "RightParen",    "LeftBrace",   "RightBrace",
        "Comma",       "Semicolon",     "Dot",         "EndOfFile",
    };
    return names[int(kind)];
}

} // namespace thoth::go
