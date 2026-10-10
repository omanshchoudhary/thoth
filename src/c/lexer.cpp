#include "c/lexer.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace thoth::c {

namespace {

class Lexer {
  public:
    explicit Lexer(std::string_view source);

    LexResult run();

  private:
    bool at_end() const;
    char peek(std::size_t offset = 0) const;
    char advance();
    bool match(char expected);

    void skip_whitespace_and_comments();
    void lex_token();
    void lex_identifier_or_keyword();
    void lex_number();

    void add_token(TokenKind kind);
    void error(SourceLocation location, std::string message);

    std::string_view source_;
    std::size_t start_ = 0;
    std::size_t current_ = 0;
    SourceLocation start_location_;
    SourceLocation current_location_;
    LexResult result_;
};

} // namespace

LexResult lex(std::string_view source) { return Lexer(source).run(); }

Lexer::Lexer(std::string_view source) : source_(source) {}

bool Lexer::at_end() const { return current_ >= source_.size(); }

char Lexer::peek(std::size_t offset) const {
    if (current_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[current_ + offset];
}

char Lexer::advance() {
    const char character = source_[current_++];
    if (character == '\n') {
        ++current_location_.line;
        current_location_.column = 1;
    } else {
        ++current_location_.column;
    }
    return character;
}

bool Lexer::match(char expected) {
    if (at_end() || peek() != expected) {
        return false;
    }
    advance();
    return true;
}

void Lexer::skip_whitespace_and_comments() {
    while (!at_end()) {
        const char character = peek();
        if (character == ' ' || character == '\t' || character == '\r' ||
            character == '\n' || character == '\f' || character == '\v') {
            advance();
        } else if (character == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') {
                advance();
            }
        } else if (character == '/' && peek(1) == '*') {
            const SourceLocation comment_location = current_location_;
            advance();
            advance();
            while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
                advance();
            }
            if (at_end()) {
                error(comment_location, "unterminated block comment");
                return;
            }
            advance();
            advance();
        } else {
            break;
        }
    }
}

void Lexer::lex_token() {
    const char character = advance();
    switch (character) {
    case '+':
        add_token(TokenKind::Plus);
        return;
    case '-':
        add_token(TokenKind::Minus);
        return;
    case '*':
        add_token(TokenKind::Multiply);
        return;
    case '/':
        add_token(TokenKind::Divide);
        return;
    case '%':
        add_token(TokenKind::Modulo);
        return;
    case '(':
        add_token(TokenKind::LeftParen);
        return;
    case ')':
        add_token(TokenKind::RightParen);
        return;
    case '{':
        add_token(TokenKind::LeftBrace);
        return;
    case '}':
        add_token(TokenKind::RightBrace);
        return;
    case ';':
        add_token(TokenKind::Semicolon);
        return;
    case ',':
        add_token(TokenKind::Comma);
        return;
    case '=':
        add_token(match('=') ? TokenKind::Equal : TokenKind::Assign);
        return;
    case '!':
        add_token(match('=') ? TokenKind::NotEqual : TokenKind::Not);
        return;
    case '<':
        add_token(match('=') ? TokenKind::LessEqual : TokenKind::Less);
        return;
    case '>':
        add_token(match('=') ? TokenKind::GreaterEqual : TokenKind::Greater);
        return;
    case '&':
        if (match('&'))
            add_token(TokenKind::And);
        else
            error(start_location_, "unexpected character '&'");
        return;
    case '|':
        if (match('|'))
            add_token(TokenKind::Or);
        else
            error(start_location_, "unexpected character '|'");
        return;
    default:
        break;
    }

    if ((character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') || character == '_') {
        lex_identifier_or_keyword();
    } else if (character >= '0' && character <= '9') {
        lex_number();
    } else {
        error(start_location_,
              std::string("unexpected character '") + character + "'");
    }
}

void Lexer::lex_identifier_or_keyword() {
    while ((peek() >= 'a' && peek() <= 'z') ||
           (peek() >= 'A' && peek() <= 'Z') ||
           (peek() >= '0' && peek() <= '9') || peek() == '_') {
        advance();
    }
    const auto text = source_.substr(start_, current_ - start_);
    TokenKind kind = TokenKind::Identifier;
    if (text == "int")
        kind = TokenKind::Int;
    else if (text == "return")
        kind = TokenKind::Return;
    else if (text == "if")
        kind = TokenKind::If;
    else if (text == "else")
        kind = TokenKind::Else;
    else if (text == "while")
        kind = TokenKind::While;
    add_token(kind);
}

void Lexer::lex_number() {
    while (peek() >= '0' && peek() <= '9') {
        advance();
    }
    add_token(TokenKind::Integer);
}

void Lexer::add_token(TokenKind kind) {
    result_.tokens.push_back(
        {kind, source_.substr(start_, current_ - start_), start_location_});
}

void Lexer::error(SourceLocation location, std::string message) {
    result_.diagnostics.push_back({location, std::move(message)});
}

LexResult Lexer::run() {
    while (!at_end()) {
        skip_whitespace_and_comments();
        if (at_end())
            break;
        start_ = current_;
        start_location_ = current_location_;
        lex_token();
    }
    result_.tokens.push_back({TokenKind::EndOfFile, "", current_location_});
    return std::move(result_);
}

} // namespace thoth::c
