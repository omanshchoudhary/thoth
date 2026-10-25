#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "go/token.hpp"
#include "source.hpp"

namespace thoth::go {

enum class Type {
    Invalid,
    Int,
    Bool,
    Void,
};

std::string_view to_string(Type type);

struct Symbol;

enum class ExprKind {
    Integer,
    Name,
    Unary,
    Binary,
    Call,
};

struct Expr {
    ExprKind kind;
    SourceLocation location;
    Type type = Type::Invalid;

    virtual ~Expr() = default;
};

struct IntegerExpr : Expr {
    IntegerExpr() { kind = ExprKind::Integer; }
    std::int64_t value = 0;
};

struct NameExpr : Expr {
    NameExpr() { kind = ExprKind::Name; }
    std::string_view name;
    const Symbol *symbol = nullptr;
};

struct UnaryExpr : Expr {
    UnaryExpr() { kind = ExprKind::Unary; }
    Token op;
    std::unique_ptr<Expr> operand;
};

struct BinaryExpr : Expr {
    BinaryExpr() { kind = ExprKind::Binary; }
    Token op;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
};

struct CallExpr : Expr {
    CallExpr() { kind = ExprKind::Call; }
    std::string_view package;
    std::string_view callee;
    std::vector<std::unique_ptr<Expr>> arguments;
    const Symbol *symbol = nullptr;
};

enum class StmtKind {
    Block,
    Var,
    Assign,
    IncDec,
    Expression,
    If,
    For,
    Return,
    Break,
    Continue,
};

struct Stmt {
    StmtKind kind;
    SourceLocation location;

    virtual ~Stmt() = default;
};

struct BlockStmt : Stmt {
    BlockStmt() { kind = StmtKind::Block; }
    std::vector<std::unique_ptr<Stmt>> statements;
    SourceLocation closing;
};

struct VarStmt : Stmt {
    VarStmt() { kind = StmtKind::Var; }
    bool is_short = false;
    std::string_view name;
    std::string_view type_name;
    SourceLocation type_location;
    std::unique_ptr<Expr> init;
    const Symbol *symbol = nullptr;
};

struct AssignStmt : Stmt {
    AssignStmt() { kind = StmtKind::Assign; }
    std::unique_ptr<NameExpr> target;
    Token op;
    std::unique_ptr<Expr> value;
};

struct IncDecStmt : Stmt {
    IncDecStmt() { kind = StmtKind::IncDec; }
    std::unique_ptr<NameExpr> target;
    Token op;
};

struct ExpressionStmt : Stmt {
    ExpressionStmt() { kind = StmtKind::Expression; }
    std::unique_ptr<Expr> expr;
};

struct IfStmt : Stmt {
    IfStmt() { kind = StmtKind::If; }
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<BlockStmt> then_branch;
    std::unique_ptr<Stmt> else_branch;
};

struct ForStmt : Stmt {
    ForStmt() { kind = StmtKind::For; }
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Stmt> post;
    std::unique_ptr<BlockStmt> body;
    bool has_break = false;
};

struct ReturnStmt : Stmt {
    ReturnStmt() { kind = StmtKind::Return; }
    std::unique_ptr<Expr> value;
};

struct BranchStmt : Stmt {
    explicit BranchStmt(StmtKind branch_kind) { kind = branch_kind; }
};

struct Param {
    std::string_view name;
    SourceLocation location;
    std::string_view type_name;
    SourceLocation type_location;
    const Symbol *symbol = nullptr;
};

struct FuncDecl {
    std::string_view name;
    SourceLocation location;
    std::vector<Param> params;
    std::string_view result_name;
    SourceLocation result_location;
    std::unique_ptr<BlockStmt> body;
    const Symbol *symbol = nullptr;
};

struct Import {
    std::string_view path;
    SourceLocation location;
};

struct Program {
    std::string_view package_name;
    SourceLocation package_location;
    std::vector<Import> imports;
    std::vector<std::unique_ptr<FuncDecl>> functions;
};

std::string to_sexpr(const Expr &expr);
std::string to_text(const Expr &expr);
std::string dump(const Program &program);

} // namespace thoth::go
