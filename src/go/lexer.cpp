#include "go/lexer.hpp"

#include <cstddef>
#include <string>
#include <utility>

namespace thoth::go {

namespace {

// letters and _ can start a name
bool is_letter(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// walks the source once with a cursor, building tokens and errors
class Lexer {
  public:
    explicit Lexer(std::string_view source) : source_(source) {}

    LexResult run();

  private:
    bool at_end() const { return current_ >= source_.size(); }
    char peek(std::size_t offset = 0) const;
    char advance();
    bool match(char expected);

    bool needs_semicolon() const;
    void insert_semicolon(SourceLocation location);
    void skip_block_comment();

    void lex_token();
    void lex_word();
    void lex_number();
    void lex_string();

    void add_token(TokenKind kind);
    void error(SourceLocation location, std::string message);

    std::string_view source_;
    std::size_t start_ = 0;
    std::size_t current_ = 0;
    SourceLocation start_location_;
    SourceLocation current_location_;
    LexResult result_;
};

// look ahead without moving; '\0' past the end
char Lexer::peek(std::size_t offset) const {
    if (current_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[current_ + offset];
}

// take one character and keep line:column up to date
char Lexer::advance() {
    char c = source_[current_++];
    if (c == '\n') {
        current_location_.line++;
        current_location_.column = 1;
    } else {
        current_location_.column++;
    }
    return c;
}

// take the next character only if it is the expected one (for <=, ==, ++)
bool Lexer::match(char expected) {
    if (at_end() || peek() != expected) {
        return false;
    }
    advance();
    return true;
}

// go's rule: a line ending after a name, literal, return, ++, ) or } gets a ;
bool Lexer::needs_semicolon() const {
    if (result_.tokens.empty()) {
        return false;
    }
    switch (result_.tokens.back().kind) {
    case TokenKind::Identifier:
    case TokenKind::Integer:
    case TokenKind::String:
    case TokenKind::Return:
    case TokenKind::Break:
    case TokenKind::Continue:
    case TokenKind::Increment:
    case TokenKind::Decrement:
    case TokenKind::RightParen:
    case TokenKind::RightBrace:
        return true;
    default:
        return false;
    }
}

// inserted semicolons have empty text so --emit=tokens can mark them
void Lexer::insert_semicolon(SourceLocation location) {
    result_.tokens.push_back({TokenKind::Semicolon, "", location});
}

// a block comment spanning lines counts as a newline for semicolon insertion
void Lexer::skip_block_comment() {
    SourceLocation comment_location = current_location_;
    advance();
    advance();
    bool saw_newline = false;
    while (!at_end() && !(peek() == '*' && peek(1) == '/')) {
        if (advance() == '\n') {
            saw_newline = true;
        }
    }
    if (at_end()) {
        error(comment_location, "comment not terminated");
        return;
    }
    advance();
    advance();
    if (saw_newline && needs_semicolon()) {
        insert_semicolon(comment_location);
    }
}

// the lexeme is a view into the source, nothing is copied
void Lexer::add_token(TokenKind kind) {
    result_.tokens.push_back(
        {kind, source_.substr(start_, current_ - start_), start_location_});
}

void Lexer::error(SourceLocation location, std::string message) {
    result_.diagnostics.push_back({location, std::move(message)});
}

// read the whole word, then check the keyword table (same order as TokenKind)
void Lexer::lex_word() {
    while (is_letter(peek()) || is_digit(peek())) {
        advance();
    }
    std::string_view word = source_.substr(start_, current_ - start_);
    const std::string_view keywords[] = {"package", "import", "func",
                                         "var",     "if",     "else",
                                         "for",     "return", "break",
                                         "continue"};
    for (int i = 0; i < 10; i++) {
        if (word == keywords[i]) {
            add_token(TokenKind(i));
            return;
        }
    }
    add_token(TokenKind::Identifier);
}

// decimal only; 012 would be octal in go, so it is rejected
void Lexer::lex_number() {
    while (is_digit(peek())) {
        advance();
    }
    if (is_letter(peek())) {
        while (is_letter(peek()) || is_digit(peek())) {
            advance();
        }
        error(start_location_, "invalid number literal");
        return;
    }
    std::string_view text = source_.substr(start_, current_ - start_);
    if (text.size() > 1 && text[0] == '0') {
        error(start_location_,
              "octal literals are not supported, write the number without "
              "a leading 0");
        return;
    }
    add_token(TokenKind::Integer);
}

// only needed for import paths, so no escape sequences
void Lexer::lex_string() {
    while (!at_end() && peek() != '"' && peek() != '\n') {
        advance();
    }
    if (peek() != '"') {
        error(start_location_, "string literal not terminated");
        return;
    }
    advance();
    add_token(TokenKind::String);
}

// the first character decides the token; match() picks the longest operator
void Lexer::lex_token() {
    char c = advance();
    switch (c) {
    case '+':
        if (match('+'))
            add_token(TokenKind::Increment);
        else if (match('='))
            add_token(TokenKind::PlusAssign);
        else
            add_token(TokenKind::Plus);
        return;
    case '-':
        if (match('-'))
            add_token(TokenKind::Decrement);
        else if (match('='))
            add_token(TokenKind::MinusAssign);
        else
            add_token(TokenKind::Minus);
        return;
    case '*':
        add_token(match('=') ? TokenKind::StarAssign : TokenKind::Star);
        return;
    case '/':
        add_token(match('=') ? TokenKind::SlashAssign : TokenKind::Slash);
        return;
    case '%':
        add_token(match('=') ? TokenKind::PercentAssign : TokenKind::Percent);
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
    case ':':
        if (match('='))
            add_token(TokenKind::Define);
        else
            error(start_location_, "unexpected character ':'");
        return;
    case '&':
        if (match('&'))
            add_token(TokenKind::AndAnd);
        else
            error(start_location_, "unexpected character '&'");
        return;
    case '|':
        if (match('|'))
            add_token(TokenKind::OrOr);
        else
            error(start_location_, "unexpected character '|'");
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
    case ',':
        add_token(TokenKind::Comma);
        return;
    case ';':
        add_token(TokenKind::Semicolon);
        return;
    case '.':
        add_token(TokenKind::Dot);
        return;
    case '"':
        lex_string();
        return;
    default:
        break;
    }

    if (is_letter(c)) {
        lex_word();
    } else if (is_digit(c)) {
        lex_number();
    } else {
        error(start_location_, std::string("unexpected character '") + c + "'");
    }
}

// main loop: newlines, spaces and comments are handled here, anything else is a token
LexResult Lexer::run() {
    while (!at_end()) {
        char c = peek();
        if (c == '\n') {
            if (needs_semicolon()) {
                insert_semicolon(current_location_);
            }
            advance();
        } else if (c == ' ' || c == '\t' || c == '\r') {
            advance();
        } else if (c == '/' && peek(1) == '/') {
            while (!at_end() && peek() != '\n') {
                advance();
            }
        } else if (c == '/' && peek(1) == '*') {
            skip_block_comment();
        } else {
            start_ = current_;
            start_location_ = current_location_;
            lex_token();
        }
    }
    if (needs_semicolon()) {
        insert_semicolon(current_location_);
    }
    // the parser always finds EndOfFile at the end
    result_.tokens.push_back({TokenKind::EndOfFile, "", current_location_});
    return std::move(result_);
}

} // namespace

LexResult lex(std::string_view source) { return Lexer(source).run(); }

} // namespace thoth::go
