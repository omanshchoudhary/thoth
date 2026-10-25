#include "go/parser.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

namespace thoth::go {

namespace {

// thrown to jump back to the nearest recovery point once the error is recorded
struct ParseError {};

// how strongly a binary operator binds; 0 means not an operator
int precedence(TokenKind kind) {
    switch (kind) {
    case TokenKind::OrOr:
        return 1;
    case TokenKind::AndAnd:
        return 2;
    case TokenKind::Equal:
    case TokenKind::NotEqual:
    case TokenKind::Less:
    case TokenKind::LessEqual:
    case TokenKind::Greater:
    case TokenKind::GreaterEqual:
        return 3;
    case TokenKind::Plus:
    case TokenKind::Minus:
        return 4;
    case TokenKind::Star:
    case TokenKind::Slash:
    case TokenKind::Percent:
        return 5;
    default:
        return 0;
    }
}

// = and the compound assignments
bool is_assign_op(TokenKind kind) {
    return kind == TokenKind::Assign || kind == TokenKind::PlusAssign ||
           kind == TokenKind::MinusAssign || kind == TokenKind::StarAssign ||
           kind == TokenKind::SlashAssign || kind == TokenKind::PercentAssign;
}

// keywords are the first ten TokenKind values
bool is_keyword(TokenKind kind) {
    return int(kind) <= int(TokenKind::Continue);
}

bool is_inc_dec(TokenKind kind) {
    return kind == TokenKind::Increment || kind == TokenKind::Decrement;
}

// how a token is named in go-style syntax errors
std::string describe(const Token &token) {
    if (token.kind == TokenKind::EndOfFile) {
        return "EOF";
    }
    if (token.kind == TokenKind::Semicolon && token.lexeme.empty()) {
        return "newline";
    }
    if (token.kind == TokenKind::Identifier) {
        return "name " + std::string(token.lexeme);
    }
    if (token.kind == TokenKind::Integer || token.kind == TokenKind::String) {
        return "literal " + std::string(token.lexeme);
    }
    if (is_keyword(token.kind)) {
        return "keyword " + std::string(token.lexeme);
    }
    return std::string(token.lexeme);
}

// recursive descent: one function per grammar rule, with a cursor over the tokens
class Parser {
  public:
    explicit Parser(const std::vector<Token> &tokens) : tokens_(tokens) {}

    ParseResult run();

  private:
    const Token &peek(std::size_t offset = 0) const;
    const Token &advance();
    bool check(TokenKind kind) const { return peek().kind == kind; }
    bool match(TokenKind kind);
    const Token &expect(TokenKind kind, const std::string &what);
    ParseError error(SourceLocation location, const std::string &message);
    ParseError unexpected(const std::string &wanted);
    void synchronize();
    void synchronize_top_level();

    void parse_package(Program &program);
    void parse_imports(Program &program);
    std::unique_ptr<FuncDecl> parse_function();
    void parse_params(FuncDecl &func);
    std::unique_ptr<BlockStmt> parse_block();
    std::unique_ptr<Stmt> parse_statement();
    std::unique_ptr<Stmt> parse_var();
    std::unique_ptr<Stmt> parse_if();
    std::unique_ptr<Stmt> parse_for();
    std::unique_ptr<Stmt> parse_return();
    std::unique_ptr<Stmt> parse_simple_statement();
    std::unique_ptr<Expr> take_condition(std::unique_ptr<Stmt> stmt);

    std::unique_ptr<Expr> parse_expression();
    std::unique_ptr<Expr> parse_binary(int min_precedence);
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<NameExpr> parse_name();
    std::unique_ptr<Expr> parse_call(const Token *package, const Token &name);

    const std::vector<Token> &tokens_;
    std::size_t current_ = 0;
    std::vector<Diagnostic> diagnostics_;
};

// look at a token without moving; stays on EndOfFile
const Token &Parser::peek(std::size_t offset) const {
    std::size_t index = current_ + offset;
    if (index >= tokens_.size()) {
        return tokens_.back();
    }
    return tokens_[index];
}

// move to the next token, never past EndOfFile
const Token &Parser::advance() {
    const Token &token = peek();
    if (token.kind != TokenKind::EndOfFile) {
        current_++;
    }
    return token;
}

// take the token only if it has this kind
bool Parser::match(TokenKind kind) {
    if (!check(kind)) {
        return false;
    }
    advance();
    return true;
}

// the token must have this kind, otherwise it is a syntax error
const Token &Parser::expect(TokenKind kind, const std::string &what) {
    if (!check(kind)) {
        throw unexpected(what);
    }
    return advance();
}

// record the message and hand back an exception to throw
ParseError Parser::error(SourceLocation location, const std::string &message) {
    diagnostics_.push_back({location, message});
    return ParseError{};
}

// the usual "unexpected X, expected Y" message
ParseError Parser::unexpected(const std::string &wanted) {
    return error(peek().location, "syntax error: unexpected " +
                                      describe(peek()) + ", expected " + wanted);
}

// panic mode: skip to the end of the statement, stepping over whole { } blocks
void Parser::synchronize() {
    int depth = 0;
    while (!check(TokenKind::EndOfFile)) {
        if (check(TokenKind::LeftBrace)) {
            depth++;
        } else if (check(TokenKind::RightBrace)) {
            if (depth == 0) {
                return;
            }
            depth--;
        } else if (check(TokenKind::Semicolon) && depth == 0) {
            advance();
            return;
        }
        advance();
    }
}

// after a bad declaration, skip to the next func
void Parser::synchronize_top_level() {
    advance();
    while (!check(TokenKind::EndOfFile) && !check(TokenKind::Func)) {
        advance();
    }
}

// package, imports, then functions; an error in one function doesn't stop the rest
ParseResult Parser::run() {
    auto program = std::make_unique<Program>();
    try {
        parse_package(*program);
        parse_imports(*program);
    } catch (const ParseError &) {
        synchronize_top_level();
    }
    while (!check(TokenKind::EndOfFile)) {
        try {
            if (!check(TokenKind::Func)) {
                throw error(peek().location, "syntax error: non-declaration "
                                             "statement outside function body");
            }
            program->functions.push_back(parse_function());
            if (!check(TokenKind::EndOfFile)) {
                expect(TokenKind::Semicolon, "newline after declaration");
            }
        } catch (const ParseError &) {
            synchronize_top_level();
        }
    }
    return {std::move(program), std::move(diagnostics_)};
}

// package main followed by a newline
void Parser::parse_package(Program &program) {
    expect(TokenKind::Package, "package clause");
    const Token &name = expect(TokenKind::Identifier, "package name");
    program.package_name = name.lexeme;
    program.package_location = name.location;
    expect(TokenKind::Semicolon, "newline after package clause");
}

// import "fmt" or a grouped import ( ... )
void Parser::parse_imports(Program &program) {
    while (match(TokenKind::Import)) {
        if (match(TokenKind::LeftParen)) {
            while (!check(TokenKind::RightParen) &&
                   !check(TokenKind::EndOfFile)) {
                if (match(TokenKind::Semicolon)) {
                    continue;
                }
                const Token &path =
                    expect(TokenKind::String, "import path string");
                program.imports.push_back({path.lexeme, path.location});
                if (!check(TokenKind::RightParen)) {
                    expect(TokenKind::Semicolon, "newline after import");
                }
            }
            expect(TokenKind::RightParen, ")");
        } else {
            const Token &path = expect(TokenKind::String, "import path string");
            program.imports.push_back({path.lexeme, path.location});
        }
        expect(TokenKind::Semicolon, "newline after import");
    }
}

// func name(params) result { body }
std::unique_ptr<FuncDecl> Parser::parse_function() {
    expect(TokenKind::Func, "func");
    auto func = std::make_unique<FuncDecl>();
    const Token &name = expect(TokenKind::Identifier, "function name");
    func->name = name.lexeme;
    func->location = name.location;
    expect(TokenKind::LeftParen, "(");
    parse_params(*func);
    if (check(TokenKind::Identifier)) {
        const Token &result = advance();
        func->result_name = result.lexeme;
        func->result_location = result.location;
    } else if (check(TokenKind::LeftParen)) {
        throw error(peek().location,
                    "multiple or named results are not supported");
    }
    func->body = parse_block();
    return func;
}

// a, b int, c bool: names are collected until their type appears
void Parser::parse_params(FuncDecl &func) {
    if (match(TokenKind::RightParen)) {
        return;
    }
    while (true) {
        std::vector<Token> names;
        names.push_back(expect(TokenKind::Identifier, "parameter name"));
        while (match(TokenKind::Comma)) {
            names.push_back(expect(TokenKind::Identifier, "parameter name"));
        }
        if (!check(TokenKind::Identifier)) {
            throw error(peek().location, "syntax error: missing parameter type");
        }
        const Token &type = advance();
        for (const Token &name : names) {
            func.params.push_back(
                {name.lexeme, name.location, type.lexeme, type.location});
        }
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    expect(TokenKind::RightParen, ")");
}

// { statements } with recovery after each bad statement
std::unique_ptr<BlockStmt> Parser::parse_block() {
    const Token &open = expect(TokenKind::LeftBrace, "{");
    auto block = std::make_unique<BlockStmt>();
    block->location = open.location;
    while (!check(TokenKind::RightBrace) && !check(TokenKind::EndOfFile)) {
        if (match(TokenKind::Semicolon)) {
            continue;
        }
        try {
            block->statements.push_back(parse_statement());
            if (!check(TokenKind::RightBrace) && !match(TokenKind::Semicolon)) {
                throw error(peek().location, "syntax error: unexpected " +
                                                 describe(peek()) +
                                                 " at end of statement");
            }
        } catch (const ParseError &) {
            synchronize();
        }
    }
    block->closing = peek().location;
    expect(TokenKind::RightBrace, "}");
    return block;
}

// the first token decides which kind of statement this is
std::unique_ptr<Stmt> Parser::parse_statement() {
    switch (peek().kind) {
    case TokenKind::Var:
        return parse_var();
    case TokenKind::If:
        return parse_if();
    case TokenKind::For:
        return parse_for();
    case TokenKind::Return:
        return parse_return();
    case TokenKind::Break: {
        auto stmt = std::make_unique<BranchStmt>(StmtKind::Break);
        stmt->location = advance().location;
        return stmt;
    }
    case TokenKind::Continue: {
        auto stmt = std::make_unique<BranchStmt>(StmtKind::Continue);
        stmt->location = advance().location;
        return stmt;
    }
    case TokenKind::LeftBrace:
        return parse_block();
    default:
        return parse_simple_statement();
    }
}

// var x int, var x = expr or var x int = expr
std::unique_ptr<Stmt> Parser::parse_var() {
    advance();
    auto var = std::make_unique<VarStmt>();
    const Token &name = expect(TokenKind::Identifier, "variable name");
    var->name = name.lexeme;
    var->location = name.location;
    if (check(TokenKind::Identifier)) {
        const Token &type = advance();
        var->type_name = type.lexeme;
        var->type_location = type.location;
    }
    if (match(TokenKind::Assign)) {
        var->init = parse_expression();
    }
    if (var->type_name.empty() && !var->init) {
        throw unexpected("type");
    }
    return var;
}

// x := e, x = e, x += e, x++ or an expression, decided by the token after the name
std::unique_ptr<Stmt> Parser::parse_simple_statement() {
    TokenKind next = peek(1).kind;
    if (check(TokenKind::Identifier) && next == TokenKind::Comma) {
        throw error(peek(1).location,
                    "assigning to several variables at once is not supported");
    }
    if (check(TokenKind::Identifier) && next == TokenKind::Define) {
        auto var = std::make_unique<VarStmt>();
        var->is_short = true;
        var->location = peek().location;
        var->name = advance().lexeme;
        advance();
        var->init = parse_expression();
        return var;
    }
    if (check(TokenKind::Identifier) && is_assign_op(next)) {
        auto assign = std::make_unique<AssignStmt>();
        assign->location = peek().location;
        assign->target = parse_name();
        assign->op = advance();
        assign->value = parse_expression();
        return assign;
    }
    if (check(TokenKind::Identifier) && is_inc_dec(next)) {
        auto incdec = std::make_unique<IncDecStmt>();
        incdec->location = peek().location;
        incdec->target = parse_name();
        incdec->op = advance();
        return incdec;
    }
    auto stmt = std::make_unique<ExpressionStmt>();
    stmt->location = peek().location;
    stmt->expr = parse_expression();
    if (is_assign_op(peek().kind) || is_inc_dec(peek().kind)) {
        throw error(peek().location,
                    "cannot assign to " + to_text(*stmt->expr));
    }
    return stmt;
}

// in if and for the simple statement must turn out to be an expression
std::unique_ptr<Expr> Parser::take_condition(std::unique_ptr<Stmt> stmt) {
    if (stmt->kind != StmtKind::Expression) {
        throw error(stmt->location,
                    "syntax error: expected a condition, not a statement");
    }
    auto &condition = static_cast<ExpressionStmt &>(*stmt);
    return std::move(condition.expr);
}

// if [init;] cond { } [else if ... | else { }]
std::unique_ptr<Stmt> Parser::parse_if() {
    const Token &keyword = advance();
    auto stmt = std::make_unique<IfStmt>();
    stmt->location = keyword.location;
    if (check(TokenKind::LeftBrace)) {
        throw error(peek().location, "missing condition in if statement");
    }
    auto first = parse_simple_statement();
    if (match(TokenKind::Semicolon)) {
        stmt->init = std::move(first);
        if (check(TokenKind::LeftBrace)) {
            throw error(peek().location, "missing condition in if statement");
        }
        stmt->condition = parse_expression();
    } else {
        stmt->condition = take_condition(std::move(first));
    }
    stmt->then_branch = parse_block();
    if (match(TokenKind::Else)) {
        if (check(TokenKind::If)) {
            stmt->else_branch = parse_if();
        } else if (check(TokenKind::LeftBrace)) {
            stmt->else_branch = parse_block();
        } else {
            throw error(peek().location,
                        "else must be followed by if or statement block");
        }
    }
    return stmt;
}

// for { }, for cond { } or for init; cond; post { }
std::unique_ptr<Stmt> Parser::parse_for() {
    const Token &keyword = advance();
    auto stmt = std::make_unique<ForStmt>();
    stmt->location = keyword.location;
    if (!check(TokenKind::LeftBrace)) {
        std::unique_ptr<Stmt> first;
        if (!check(TokenKind::Semicolon)) {
            first = parse_simple_statement();
        }
        if (match(TokenKind::Semicolon)) {
            stmt->init = std::move(first);
            if (!check(TokenKind::Semicolon)) {
                stmt->condition = parse_expression();
            }
            expect(TokenKind::Semicolon, "; after for loop condition");
            if (!check(TokenKind::LeftBrace)) {
                stmt->post = parse_simple_statement();
                if (stmt->post->kind == StmtKind::Var) {
                    throw error(stmt->post->location,
                                "cannot declare in post statement of for loop");
                }
            }
        } else {
            stmt->condition = take_condition(std::move(first));
        }
    }
    stmt->body = parse_block();
    return stmt;
}

// return with an optional value
std::unique_ptr<Stmt> Parser::parse_return() {
    const Token &keyword = advance();
    auto stmt = std::make_unique<ReturnStmt>();
    stmt->location = keyword.location;
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RightBrace)) {
        stmt->value = parse_expression();
    }
    return stmt;
}

// the weakest level is 1 (||)
std::unique_ptr<Expr> Parser::parse_expression() { return parse_binary(1); }

// precedence climbing: keep taking operators at least as strong as min_precedence
std::unique_ptr<Expr> Parser::parse_binary(int min_precedence) {
    auto left = parse_unary();
    while (true) {
        int p = precedence(peek().kind);
        if (p == 0 || p < min_precedence) {
            break;
        }
        const Token &op = advance();
        // p + 1 makes operators of the same level group from the left
        auto right = parse_binary(p + 1);
        auto binary = std::make_unique<BinaryExpr>();
        binary->location = left->location;
        binary->op = op;
        binary->left = std::move(left);
        binary->right = std::move(right);
        left = std::move(binary);
    }
    return left;
}

// prefix - and !, applied right to left
std::unique_ptr<Expr> Parser::parse_unary() {
    if (check(TokenKind::Minus) || check(TokenKind::Not)) {
        const Token &op = advance();
        auto unary = std::make_unique<UnaryExpr>();
        unary->location = op.location;
        unary->op = op;
        unary->operand = parse_unary();
        return unary;
    }
    return parse_primary();
}

// numbers, names, calls and ( expr )
std::unique_ptr<Expr> Parser::parse_primary() {
    if (check(TokenKind::Integer)) {
        const Token &token = advance();
        std::uint64_t value = 0;
        std::uint64_t max = 9223372036854775807ULL;
        for (char c : token.lexeme) {
            std::uint64_t digit = c - '0';
            // check before multiplying so the value itself never overflows
            if (value > (max - digit) / 10) {
                throw error(token.location, "integer constant " +
                                                std::string(token.lexeme) +
                                                " overflows int");
            }
            value = value * 10 + digit;
        }
        auto integer = std::make_unique<IntegerExpr>();
        integer->location = token.location;
        integer->value = value;
        return integer;
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::Dot) {
        const Token &package = advance();
        advance();
        const Token &name = expect(TokenKind::Identifier, "name after '.'");
        if (!check(TokenKind::LeftParen)) {
            throw error(peek().location, "only calls like " +
                                             std::string(package.lexeme) + "." +
                                             std::string(name.lexeme) +
                                             "(...) are supported");
        }
        return parse_call(&package, name);
    }
    if (check(TokenKind::Identifier) && peek(1).kind == TokenKind::LeftParen) {
        const Token &name = advance();
        return parse_call(nullptr, name);
    }
    if (check(TokenKind::Identifier)) {
        return parse_name();
    }
    if (match(TokenKind::LeftParen)) {
        auto inner = parse_expression();
        expect(TokenKind::RightParen, ")");
        return inner;
    }
    throw unexpected("expression");
}

// a plain variable reference
std::unique_ptr<NameExpr> Parser::parse_name() {
    const Token &name = advance();
    auto expr = std::make_unique<NameExpr>();
    expr->location = name.location;
    expr->name = name.lexeme;
    return expr;
}

// arguments separated by commas; a trailing comma is allowed
std::unique_ptr<Expr> Parser::parse_call(const Token *package,
                                         const Token &name) {
    expect(TokenKind::LeftParen, "(");
    auto call = std::make_unique<CallExpr>();
    call->location = package ? package->location : name.location;
    if (package) {
        call->package = package->lexeme;
    }
    call->callee = name.lexeme;
    if (!check(TokenKind::RightParen)) {
        while (true) {
            call->arguments.push_back(parse_expression());
            if (!match(TokenKind::Comma) || check(TokenKind::RightParen)) {
                break;
            }
        }
    }
    expect(TokenKind::RightParen, ")");
    return call;
}

} // namespace

ParseResult parse(const std::vector<Token> &tokens) {
    return Parser(tokens).run();
}

} // namespace thoth::go
