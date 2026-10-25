#include "go/sema.hpp"

#include <cstddef>
#include <utility>

namespace thoth::go {

namespace {

// (line:col) suffix for the symbol table dump
std::string where(SourceLocation location) {
    return "(" + std::to_string(location.line) + ":" +
           std::to_string(location.column) + ")";
}

// go's terminating statement rule, used for "missing return"
bool is_terminating(const Stmt &stmt) {
    switch (stmt.kind) {
    case StmtKind::Return:
        return true;
    case StmtKind::Block: {
        const auto &block = static_cast<const BlockStmt &>(stmt);
        return !block.statements.empty() &&
               is_terminating(*block.statements.back());
    }
    case StmtKind::If: {
        const auto &if_stmt = static_cast<const IfStmt &>(stmt);
        return if_stmt.else_branch && is_terminating(*if_stmt.then_branch) &&
               is_terminating(*if_stmt.else_branch);
    }
    case StmtKind::For: {
        const auto &for_stmt = static_cast<const ForStmt &>(stmt);
        return !for_stmt.condition && !for_stmt.has_break;
    }
    default:
        return false;
    }
}

std::string type_name(Type type) { return std::string(to_string(type)); }

// e.g. "x (value of type int)"
std::string value_text(const Expr &expr, Type type) {
    return to_text(expr) + " (value of type " + type_name(type) +
           ")";
}


// go-style "cannot use ... as ... value in ..." message
std::string cannot_use(const Expr &expr, Type have, Type want,
                       const std::string &context) {
    return "cannot use " + value_text(expr, have) + " as " + type_name(want) +
           " value in " + context;
}

// dividing by a literal 0 is a compile-time error in go
bool is_zero_literal(const Expr &expr) {
    return expr.kind == ExprKind::Integer &&
           static_cast<const IntegerExpr &>(expr).value == 0;
}

// walks the tree once with a stack of scopes, recording types and errors
class Checker {
  public:
    explicit Checker(Program &program) : program_(program) {}

    SemaResult run();

  private:
    Symbol *new_symbol(std::string name, SymbolKind kind, Type type,
                       SourceLocation location);
    void declare(Symbol *symbol);
    Symbol *lookup(std::string_view name) const;
    Symbol *lookup_current(std::string_view name) const;
    void enter_scope() { scopes_.emplace_back(); }
    void exit_scope();
    void error(SourceLocation location, std::string message);
    void note(Symbol *symbol, const std::string &kind_text);

    Type resolve_type(std::string_view name, SourceLocation location);
    void declare_function(FuncDecl &func);
    void check_function(FuncDecl &func);
    void check_block(BlockStmt &block, bool new_scope);
    void check_stmt(Stmt &stmt);
    void check_var(VarStmt &var);
    Symbol *check_target(NameExpr &target);
    void check_assign(AssignStmt &assign);
    void check_if(IfStmt &if_stmt);
    void check_for(ForStmt &for_stmt);
    void check_return(ReturnStmt &ret);

    Type check_expr(Expr &expr);
    Type value_type(Expr &expr);
    Type check_name(NameExpr &name);
    Type check_unary(UnaryExpr &unary);
    Type check_binary(BinaryExpr &binary);
    Type check_call(CallExpr &call);

    Program &program_;
    SemaResult result_;
    std::vector<std::vector<Symbol *>> scopes_;
    Type result_type_ = Type::Void;
    std::vector<ForStmt *> loops_;
    std::size_t function_depth_ = 0;
};

// symbols are owned by the result so the tree can point at them
Symbol *Checker::new_symbol(std::string name, SymbolKind kind, Type type,
                            SourceLocation location) {
    auto symbol = std::make_unique<Symbol>();
    symbol->name = std::move(name);
    symbol->kind = kind;
    symbol->type = type;
    symbol->location = location;
    result_.symbols.push_back(std::move(symbol));
    return result_.symbols.back().get();
}

// add to the innermost scope; a second declaration there is an error
void Checker::declare(Symbol *symbol) {
    if (lookup_current(symbol->name)) {
        error(symbol->location, symbol->name + " redeclared in this block");
        return;
    }
    scopes_.back().push_back(symbol);
}

// search only the innermost scope
Symbol *Checker::lookup_current(std::string_view name) const {
    for (Symbol *symbol : scopes_.back()) {
        if (symbol->name == name) {
            return symbol;
        }
    }
    return nullptr;
}

// search from the innermost scope outwards, so inner names hide outer ones
Symbol *Checker::lookup(std::string_view name) const {
    for (int i = scopes_.size() - 1; i >= 0; i--) {
        for (Symbol *symbol : scopes_[i]) {
            if (symbol->name == name) {
                return symbol;
            }
        }
    }
    return nullptr;
}

// leaving a scope is when go reports variables that were never used
void Checker::exit_scope() {
    for (Symbol *symbol : scopes_.back()) {
        if (symbol->kind == SymbolKind::Variable && !symbol->used) {
            error(symbol->location, "declared and not used: " + symbol->name);
        }
    }
    scopes_.pop_back();
}

void Checker::error(SourceLocation location, std::string message) {
    result_.diagnostics.push_back({location, std::move(message)});
}

// one line of --emit=symbols, indented by scope depth
void Checker::note(Symbol *symbol, const std::string &kind_text) {
    std::size_t depth = scopes_.size() - function_depth_;
    for (std::size_t i = 0; i < depth; i++) {
        result_.symbol_table += "  ";
    }
    result_.symbol_table += kind_text + " " + symbol->name + " " +
                            type_name(symbol->type) + "  " +
                            where(symbol->location) + "\n";
}

// a type name has to be int or bool
Type Checker::resolve_type(std::string_view name, SourceLocation location) {
    Symbol *symbol = lookup(name);
    if (!symbol) {
        error(location, "undefined: " + std::string(name));
        return Type::Invalid;
    }
    if (symbol->kind != SymbolKind::TypeName) {
        error(location, std::string(name) + " is not a type");
        return Type::Invalid;
    }
    return symbol->type;
}

// universe scope (int, bool, true, false), package scope, then every function
SemaResult Checker::run() {
    enter_scope();
    declare(new_symbol("int", SymbolKind::TypeName, Type::Int, {}));
    declare(new_symbol("bool", SymbolKind::TypeName, Type::Bool, {}));
    Symbol *true_symbol = new_symbol("true", SymbolKind::Constant, Type::Bool, {});
    true_symbol->value = 1;
    declare(true_symbol);
    declare(new_symbol("false", SymbolKind::Constant, Type::Bool, {}));

    enter_scope();
    if (program_.package_name != "main") {
        error(program_.package_location,
              "package " + std::string(program_.package_name) +
                  " is not main: only package main can be compiled");
    }
    std::vector<Symbol *> imports;
    for (const auto &import : program_.imports) {
        if (import.path != "\"fmt\"") {
            error(import.location, "unsupported import " +
                                       std::string(import.path) +
                                       ": only \"fmt\" is available");
            continue;
        }
        Symbol *symbol =
            new_symbol("fmt", SymbolKind::Package, Type::Invalid, import.location);
        declare(symbol);
        imports.push_back(symbol);
        result_.symbol_table += "import fmt  " + where(import.location) + "\n";
    }

    // declare every function first so a call can come before the definition
    for (auto &func : program_.functions) {
        declare_function(*func);
    }
    Symbol *main = lookup_current("main");
    if (!main || main->kind != SymbolKind::Function) {
        error(program_.package_location,
              "function main is undeclared in the main package");
    } else if (!main->params.empty() || main->type != Type::Void) {
        error(main->location,
              "func main must have no arguments and no return values");
    }

    function_depth_ = scopes_.size();
    for (auto &func : program_.functions) {
        check_function(*func);
    }

    for (Symbol *symbol : imports) {
        if (!symbol->used) {
            error(symbol->location, "\"fmt\" imported and not used");
        }
    }
    scopes_.pop_back();
    scopes_.pop_back();
    return std::move(result_);
}

// parameter and result types from the signature
void Checker::declare_function(FuncDecl &func) {
    Type result = Type::Void;
    if (!func.result_name.empty()) {
        result = resolve_type(func.result_name, func.result_location);
    }
    Symbol *symbol = new_symbol(std::string(func.name), SymbolKind::Function,
                                result, func.location);
    for (auto &param : func.params) {
        symbol->params.push_back(
            resolve_type(param.type_name, param.type_location));
    }
    if (func.params.size() > 6) {
        error(func.location,
              "functions with more than 6 parameters are not supported");
    }
    declare(symbol);
    func.symbol = symbol;
}

// params share the body's scope, so redeclaring one at the top level is an error
void Checker::check_function(FuncDecl &func) {
    const Symbol *symbol = func.symbol;
    std::string header = "func " + std::string(func.name) + "(";
    for (std::size_t i = 0; i < func.params.size(); i++) {
        if (i > 0) {
            header += ", ";
        }
        header += std::string(func.params[i].name) + " " +
                  type_name(symbol->params[i]);
    }
    header += ")";
    if (symbol->type != Type::Void) {
        header += " " + type_name(symbol->type);
    }
    result_.symbol_table += header + "  " + where(func.location) + "\n";

    result_type_ = symbol->type;
    enter_scope();
    for (std::size_t i = 0; i < func.params.size(); i++) {
        Param &param = func.params[i];
        Symbol *param_symbol =
            new_symbol(std::string(param.name), SymbolKind::Parameter,
                       symbol->params[i], param.location);
        declare(param_symbol);
        param.symbol = param_symbol;
        note(param_symbol, "param");
    }
    check_block(*func.body, false);
    if (result_type_ != Type::Void && !is_terminating(*func.body)) {
        error(func.body->closing, "missing return");
    }
    exit_scope();
}

// a block opens its own scope unless it is a function body
void Checker::check_block(BlockStmt &block, bool new_scope) {
    if (new_scope) {
        enter_scope();
    }
    for (auto &stmt : block.statements) {
        check_stmt(*stmt);
    }
    if (new_scope) {
        exit_scope();
    }
}

// one case per statement kind
void Checker::check_stmt(Stmt &stmt) {
    switch (stmt.kind) {
    case StmtKind::Block:
        check_block(static_cast<BlockStmt &>(stmt), true);
        return;
    case StmtKind::Var:
        check_var(static_cast<VarStmt &>(stmt));
        return;
    case StmtKind::Assign:
        check_assign(static_cast<AssignStmt &>(stmt));
        return;
    case StmtKind::IncDec: {
        auto &incdec = static_cast<IncDecStmt &>(stmt);
        Symbol *target = check_target(*incdec.target);
        if (target && target->type == Type::Bool) {
            error(incdec.location, "invalid operation: " +
                                       std::string(incdec.target->name) +
                                       std::string(incdec.op.lexeme) +
                                       " (non-numeric type bool)");
        }
        return;
    }
    case StmtKind::Expression: {
        auto &expr_stmt = static_cast<ExpressionStmt &>(stmt);
        if (expr_stmt.expr->kind != ExprKind::Call) {
            Type type = check_expr(*expr_stmt.expr);
            if (type != Type::Invalid) {
                error(expr_stmt.location,
                      value_text(*expr_stmt.expr, type) + " is not used");
            }
            return;
        }
        check_expr(*expr_stmt.expr);
        return;
    }
    case StmtKind::If:
        check_if(static_cast<IfStmt &>(stmt));
        return;
    case StmtKind::For:
        check_for(static_cast<ForStmt &>(stmt));
        return;
    case StmtKind::Return:
        check_return(static_cast<ReturnStmt &>(stmt));
        return;
    case StmtKind::Break:
        if (loops_.empty()) {
            error(stmt.location, "break is not in a loop");
        } else {
            loops_.back()->has_break = true;
        }
        return;
    case StmtKind::Continue:
        if (loops_.empty()) {
            error(stmt.location, "continue is not in a loop");
        }
        return;
    }
}

// the initializer is checked before the name exists, so var x = x sees the outer x
void Checker::check_var(VarStmt &var) {
    Type declared = Type::Invalid;
    if (!var.type_name.empty()) {
        declared = resolve_type(var.type_name, var.type_location);
    }
    Type init_type = Type::Invalid;
    if (var.init) {
        init_type = value_type(*var.init);
    }
    Type type = var.type_name.empty() ? init_type : declared;
    if (!var.type_name.empty() && var.init && declared != Type::Invalid &&
        init_type != Type::Invalid && declared != init_type) {
        error(var.init->location, cannot_use(*var.init, init_type, declared,
                                             "variable declaration"));
    }
    if (var.is_short && lookup_current(var.name)) {
        error(var.location, "no new variables on left side of :=");
        return;
    }
    Symbol *symbol = new_symbol(std::string(var.name), SymbolKind::Variable,
                                type, var.location);
    declare(symbol);
    var.symbol = symbol;
    note(symbol, "var");
}

// assignments need a variable; assigning does not count as using it
Symbol *Checker::check_target(NameExpr &target) {
    Symbol *symbol = lookup(target.name);
    if (!symbol) {
        error(target.location, "undefined: " + std::string(target.name));
        return nullptr;
    }
    if (symbol->kind != SymbolKind::Variable &&
        symbol->kind != SymbolKind::Parameter) {
        error(target.location,
              "cannot assign to " + std::string(target.name));
        return nullptr;
    }
    target.symbol = symbol;
    target.type = symbol->type;
    return symbol;
}

// = needs matching types; compound operators need int on both sides
void Checker::check_assign(AssignStmt &assign) {
    Symbol *target = check_target(*assign.target);
    Type value = value_type(*assign.value);
    if (!target || target->type == Type::Invalid || value == Type::Invalid) {
        return;
    }
    if (assign.op.kind == TokenKind::Assign) {
        if (target->type != value) {
            error(assign.value->location,
                  cannot_use(*assign.value, value, target->type, "assignment"));
        }
        return;
    }
    std::string text = std::string(assign.target->name) + " " +
                       std::string(assign.op.lexeme) + " " +
                       to_text(*assign.value);
    if (target->type != value) {
        error(assign.location, "invalid operation: " + text +
                                   " (mismatched types " +
                                   type_name(target->type) +
                                   " and " + type_name(value) +
                                   ")");
    } else if (target->type != Type::Int) {
        error(assign.location, "invalid operation: operator " +
                                   std::string(assign.op.lexeme) +
                                   " not defined on " +
                                   std::string(assign.target->name) +
                                   " (value of type bool)");
    } else if ((assign.op.kind == TokenKind::SlashAssign ||
                assign.op.kind == TokenKind::PercentAssign) &&
               is_zero_literal(*assign.value)) {
        error(assign.value->location, "invalid operation: division by zero");
    }
}

// if gets its own scope so a variable from the init part ends with the if
void Checker::check_if(IfStmt &if_stmt) {
    enter_scope();
    if (if_stmt.init) {
        check_stmt(*if_stmt.init);
    }
    Type condition = value_type(*if_stmt.condition);
    if (condition != Type::Invalid && condition != Type::Bool) {
        error(if_stmt.condition->location,
              "non-boolean condition in if statement");
    }
    check_block(*if_stmt.then_branch, true);
    if (if_stmt.else_branch) {
        check_stmt(*if_stmt.else_branch);
    }
    exit_scope();
}

// for gets a scope for its init variable; the loop stack is for break and continue
void Checker::check_for(ForStmt &for_stmt) {
    enter_scope();
    if (for_stmt.init) {
        check_stmt(*for_stmt.init);
    }
    if (for_stmt.condition) {
        Type condition = value_type(*for_stmt.condition);
        if (condition != Type::Invalid && condition != Type::Bool) {
            error(for_stmt.condition->location,
                  "non-boolean condition in for statement");
        }
    }
    if (for_stmt.post) {
        check_stmt(*for_stmt.post);
    }
    loops_.push_back(&for_stmt);
    check_block(*for_stmt.body, true);
    loops_.pop_back();
    exit_scope();
}

// the value has to match the function's result type
void Checker::check_return(ReturnStmt &ret) {
    if (!ret.value) {
        if (result_type_ != Type::Void) {
            error(ret.location, "not enough return values (want " +
                                    type_name(result_type_) +
                                    ")");
        }
        return;
    }
    if (result_type_ == Type::Void) {
        check_expr(*ret.value);
        error(ret.value->location, "too many return values");
        return;
    }
    Type value = value_type(*ret.value);
    if (value != Type::Invalid && result_type_ != Type::Invalid &&
        value != result_type_) {
        error(ret.value->location, cannot_use(*ret.value, value, result_type_,
                                              "return statement"));
    }
}

// like check_expr, but a call with no result can't be used as a value
Type Checker::value_type(Expr &expr) {
    Type type = check_expr(expr);
    if (type == Type::Void) {
        error(expr.location, to_text(expr) + " (no value) used as value");
        return Type::Invalid;
    }
    return type;
}

// stores the type on the node for the later phases
Type Checker::check_expr(Expr &expr) {
    Type type = Type::Invalid;
    switch (expr.kind) {
    case ExprKind::Integer:
        type = Type::Int;
        break;
    case ExprKind::Name:
        type = check_name(static_cast<NameExpr &>(expr));
        break;
    case ExprKind::Unary:
        type = check_unary(static_cast<UnaryExpr &>(expr));
        break;
    case ExprKind::Binary:
        type = check_binary(static_cast<BinaryExpr &>(expr));
        break;
    case ExprKind::Call:
        type = check_call(static_cast<CallExpr &>(expr));
        break;
    }
    expr.type = type;
    return type;
}

// using a variable in an expression marks it as used
Type Checker::check_name(NameExpr &name) {
    Symbol *symbol = lookup(name.name);
    if (!symbol) {
        error(name.location, "undefined: " + std::string(name.name));
        return Type::Invalid;
    }
    name.symbol = symbol;
    switch (symbol->kind) {
    case SymbolKind::Variable:
    case SymbolKind::Parameter:
        symbol->used = true;
        return symbol->type;
    case SymbolKind::Constant:
        return symbol->type;
    case SymbolKind::TypeName:
        error(name.location,
              std::string(name.name) + " (type) is not an expression");
        return Type::Invalid;
    case SymbolKind::Function:
        error(name.location, "function " + std::string(name.name) +
                                 " must be called, it cannot be used as a "
                                 "value");
        return Type::Invalid;
    case SymbolKind::Package:
        symbol->used = true;
        error(name.location, "use of package " + std::string(name.name) +
                                 " without selector");
        return Type::Invalid;
    }
    return Type::Invalid;
}

// - needs an int, ! needs a bool
Type Checker::check_unary(UnaryExpr &unary) {
    Type operand = value_type(*unary.operand);
    if (operand == Type::Invalid) {
        return Type::Invalid;
    }
    Type wanted = unary.op.kind == TokenKind::Minus ? Type::Int : Type::Bool;
    if (operand != wanted) {
        error(unary.location, "invalid operation: operator " +
                                  std::string(unary.op.lexeme) +
                                  " not defined on " +
                                  value_text(*unary.operand, operand));
        return Type::Invalid;
    }
    return wanted;
}

// both sides must match; arithmetic and < need int, && and || need bool
Type Checker::check_binary(BinaryExpr &binary) {
    Type left = value_type(*binary.left);
    Type right = value_type(*binary.right);
    if (left == Type::Invalid || right == Type::Invalid) {
        return Type::Invalid;
    }
    if (left != right) {
        error(binary.location, "invalid operation: " + to_text(binary) +
                                   " (mismatched types " + type_name(left) +
                                   " and " + type_name(right) + ")");
        return Type::Invalid;
    }
    TokenKind op = binary.op.kind;
    bool arithmetic = op == TokenKind::Plus || op == TokenKind::Minus ||
                      op == TokenKind::Star || op == TokenKind::Slash ||
                      op == TokenKind::Percent;
    bool logical = op == TokenKind::AndAnd || op == TokenKind::OrOr;
    bool equality = op == TokenKind::Equal || op == TokenKind::NotEqual;
    Type needed = logical ? Type::Bool : Type::Int;
    if (!equality && left != needed) {
        error(binary.location, "invalid operation: operator " +
                                   std::string(binary.op.lexeme) +
                                   " not defined on " +
                                   value_text(*binary.left, left));
        return Type::Invalid;
    }
    if ((op == TokenKind::Slash || op == TokenKind::Percent) &&
        is_zero_literal(*binary.right)) {
        error(binary.right->location, "invalid operation: division by zero");
    }
    return arithmetic ? Type::Int : Type::Bool;
}

// fmt.Println takes any int and bool values; other calls need the exact count and types
Type Checker::check_call(CallExpr &call) {
    if (!call.package.empty()) {
        Symbol *package = lookup(call.package);
        for (auto &argument : call.arguments) {
            value_type(*argument);
        }
        if (!package) {
            error(call.location, "undefined: " + std::string(call.package));
            return Type::Invalid;
        }
        if (package->kind != SymbolKind::Package) {
            error(call.location,
                  std::string(call.package) + " is not a package");
            return Type::Invalid;
        }
        package->used = true;
        if (call.callee != "Println") {
            error(call.location, "undefined: fmt." + std::string(call.callee) +
                                     " (only fmt.Println is supported)");
            return Type::Invalid;
        }
        return Type::Void;
    }

    Symbol *symbol = lookup(call.callee);
    std::vector<Type> arguments;
    for (auto &argument : call.arguments) {
        arguments.push_back(value_type(*argument));
    }
    if (!symbol) {
        error(call.location, "undefined: " + std::string(call.callee));
        return Type::Invalid;
    }
    if (symbol->kind != SymbolKind::Function) {
        if (symbol->kind == SymbolKind::Variable ||
            symbol->kind == SymbolKind::Parameter) {
            symbol->used = true;
        }
        error(call.location, "invalid operation: cannot call non-function " +
                                 std::string(call.callee));
        return Type::Invalid;
    }
    call.symbol = symbol;
    std::string name = std::string(call.callee);
    if (arguments.size() < symbol->params.size()) {
        error(call.location, "not enough arguments in call to " + name);
    } else if (arguments.size() > symbol->params.size()) {
        error(call.location, "too many arguments in call to " + name);
    } else {
        for (std::size_t i = 0; i < arguments.size(); i++) {
            Type wanted = symbol->params[i];
            if (arguments[i] != Type::Invalid && wanted != Type::Invalid &&
                arguments[i] != wanted) {
                error(call.arguments[i]->location,
                      cannot_use(*call.arguments[i], arguments[i], wanted,
                                 "argument to " + name));
            }
        }
    }
    return symbol->type;
}

} // namespace

SemaResult analyze(Program &program) { return Checker(program).run(); }

} // namespace thoth::go
