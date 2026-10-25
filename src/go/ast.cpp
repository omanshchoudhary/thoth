#include "go/ast.hpp"

namespace thoth::go {

// type names as go prints them in error messages
std::string_view to_string(Type type) {
    switch (type) {
    case Type::Int:
        return "int";
    case Type::Bool:
        return "bool";
    case Type::Void:
        return "no value";
    case Type::Invalid:
        return "invalid";
    }
    return "invalid";
}

// prefix form for --emit=ast and the tests, e.g. (+ 1 (* 2 3))
std::string to_sexpr(const Expr &expr) {
    switch (expr.kind) {
    case ExprKind::Integer:
        return std::to_string(static_cast<const IntegerExpr &>(expr).value);
    case ExprKind::Name:
        return std::string(static_cast<const NameExpr &>(expr).name);
    case ExprKind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        return "(" + std::string(unary.op.lexeme) + " " +
               to_sexpr(*unary.operand) + ")";
    }
    case ExprKind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        return "(" + std::string(binary.op.lexeme) + " " +
               to_sexpr(*binary.left) + " " + to_sexpr(*binary.right) + ")";
    }
    case ExprKind::Call: {
        const auto &call = static_cast<const CallExpr &>(expr);
        std::string text = "(call ";
        if (!call.package.empty()) {
            text += std::string(call.package) + ".";
        }
        text += call.callee;
        for (const auto &argument : call.arguments) {
            text += " " + to_sexpr(*argument);
        }
        return text + ")";
    }
    }
    return "?";
}

// go-like source text for error messages, e.g. x + true
std::string to_text(const Expr &expr) {
    switch (expr.kind) {
    case ExprKind::Integer:
    case ExprKind::Name:
        return to_sexpr(expr);
    case ExprKind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        std::string operand = to_text(*unary.operand);
        if (unary.operand->kind == ExprKind::Binary) {
            operand = "(" + operand + ")";
        }
        return std::string(unary.op.lexeme) + operand;
    }
    case ExprKind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        std::string left = to_text(*binary.left);
        std::string right = to_text(*binary.right);
        if (binary.left->kind == ExprKind::Binary) {
            left = "(" + left + ")";
        }
        if (binary.right->kind == ExprKind::Binary) {
            right = "(" + right + ")";
        }
        return left + " " + std::string(binary.op.lexeme) + " " + right;
    }
    case ExprKind::Call: {
        const auto &call = static_cast<const CallExpr &>(expr);
        std::string text;
        if (!call.package.empty()) {
            text += std::string(call.package) + ".";
        }
        text += std::string(call.callee) + "(";
        for (std::size_t i = 0; i < call.arguments.size(); i++) {
            if (i > 0) {
                text += ", ";
            }
            text += to_text(*call.arguments[i]);
        }
        return text + ")";
    }
    }
    return "?";
}

namespace {

// two spaces per level of nesting
void write_line(std::string &out, int depth, const std::string &text) {
    for (int i = 0; i < depth; i++) {
        out += "  ";
    }
    out += text;
    out += '\n';
}

std::string simple_text(const Stmt &stmt);

// statements with children (block, if, for) print them indented below
void write_stmt(std::string &out, const Stmt &stmt, int depth) {
    switch (stmt.kind) {
    case StmtKind::Block: {
        const auto &block = static_cast<const BlockStmt &>(stmt);
        write_line(out, depth, "block");
        for (const auto &inner : block.statements) {
            write_stmt(out, *inner, depth + 1);
        }
        return;
    }
    case StmtKind::If: {
        const auto &if_stmt = static_cast<const IfStmt &>(stmt);
        write_line(out, depth, "if " + to_sexpr(*if_stmt.condition));
        if (if_stmt.init) {
            write_line(out, depth + 1, "init: " + simple_text(*if_stmt.init));
        }
        write_stmt(out, *if_stmt.then_branch, depth + 1);
        if (if_stmt.else_branch) {
            write_line(out, depth, "else");
            write_stmt(out, *if_stmt.else_branch, depth + 1);
        }
        return;
    }
    case StmtKind::For: {
        const auto &for_stmt = static_cast<const ForStmt &>(stmt);
        write_line(out, depth, "for");
        if (for_stmt.init) {
            write_line(out, depth + 1, "init: " + simple_text(*for_stmt.init));
        }
        if (for_stmt.condition) {
            write_line(out, depth + 1, "cond: " + to_sexpr(*for_stmt.condition));
        }
        if (for_stmt.post) {
            write_line(out, depth + 1, "post: " + simple_text(*for_stmt.post));
        }
        write_stmt(out, *for_stmt.body, depth + 1);
        return;
    }
    default:
        write_line(out, depth, simple_text(stmt));
        return;
    }
}

// one line of text for statements without children
std::string simple_text(const Stmt &stmt) {
    switch (stmt.kind) {
    case StmtKind::Var: {
        const auto &var = static_cast<const VarStmt &>(stmt);
        if (var.is_short) {
            return "define " + std::string(var.name) + " := " +
                   to_sexpr(*var.init);
        }
        std::string text = "var " + std::string(var.name);
        if (!var.type_name.empty()) {
            text += " " + std::string(var.type_name);
        }
        if (var.init) {
            text += " = " + to_sexpr(*var.init);
        }
        return text;
    }
    case StmtKind::Assign: {
        const auto &assign = static_cast<const AssignStmt &>(stmt);
        return "assign " + std::string(assign.target->name) + " " +
               std::string(assign.op.lexeme) + " " + to_sexpr(*assign.value);
    }
    case StmtKind::IncDec: {
        const auto &incdec = static_cast<const IncDecStmt &>(stmt);
        return "incdec " + std::string(incdec.target->name) +
               std::string(incdec.op.lexeme);
    }
    case StmtKind::Expression:
        return "expr " +
               to_sexpr(*static_cast<const ExpressionStmt &>(stmt).expr);
    case StmtKind::Return: {
        const auto &ret = static_cast<const ReturnStmt &>(stmt);
        if (ret.value) {
            return "return " + to_sexpr(*ret.value);
        }
        return "return";
    }
    case StmtKind::Break:
        return "break";
    case StmtKind::Continue:
        return "continue";
    default:
        return "?";
    }
}

} // namespace

// the whole tree for --emit=ast
std::string dump(const Program &program) {
    std::string out;
    write_line(out, 0, "package " + std::string(program.package_name));
    for (const auto &import : program.imports) {
        write_line(out, 0, "import " + std::string(import.path));
    }
    for (const auto &func : program.functions) {
        std::string header = "func " + std::string(func->name) + "(";
        for (std::size_t i = 0; i < func->params.size(); i++) {
            if (i > 0) {
                header += ", ";
            }
            header += std::string(func->params[i].name) + " " +
                      std::string(func->params[i].type_name);
        }
        header += ")";
        if (!func->result_name.empty()) {
            header += " " + std::string(func->result_name);
        }
        write_line(out, 0, header);
        write_stmt(out, *func->body, 1);
    }
    return out;
}

} // namespace thoth::go
