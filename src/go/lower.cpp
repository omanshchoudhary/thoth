#include "go/lower.hpp"

#include <map>
#include <string>
#include <vector>

#include "go/sema.hpp"

namespace thoth::go {

namespace {

ir::Op binary_op(TokenKind kind) {
    switch (kind) {
    case TokenKind::Plus:
    case TokenKind::PlusAssign:
    case TokenKind::Increment:
        return ir::Op::Add;
    case TokenKind::Minus:
    case TokenKind::MinusAssign:
    case TokenKind::Decrement:
        return ir::Op::Sub;
    case TokenKind::Star:
    case TokenKind::StarAssign:
        return ir::Op::Mul;
    case TokenKind::Slash:
    case TokenKind::SlashAssign:
        return ir::Op::Div;
    case TokenKind::Percent:
    case TokenKind::PercentAssign:
        return ir::Op::Mod;
    case TokenKind::Equal:
        return ir::Op::Equal;
    case TokenKind::NotEqual:
        return ir::Op::NotEqual;
    case TokenKind::Less:
        return ir::Op::Less;
    case TokenKind::LessEqual:
        return ir::Op::LessEqual;
    case TokenKind::Greater:
        return ir::Op::Greater;
    default:
        return ir::Op::GreaterEqual;
    }
}

class Lowerer {
  public:
    ir::Program run(const Program &program);

  private:
    struct Loop {
        std::vector<int> breaks;
        std::vector<int> continues;
    };

    int here() const { return function_->code.size(); }
    int emit(ir::Instruction instruction);
    void patch(int jump) { function_->code[jump].target = here(); }
    std::string new_temp() { return "t" + std::to_string(++temp_count_); }
    std::string name_for(const Symbol *symbol);

    void lower_function(const FuncDecl &func);
    void lower_block(const BlockStmt &block);
    void lower_stmt(const Stmt &stmt);
    void lower_if(const IfStmt &if_stmt);
    void lower_for(const ForStmt &for_stmt);
    std::string lower_expr(const Expr &expr);
    std::string lower_logical(const BinaryExpr &binary);
    std::string lower_call(const CallExpr &call);

    ir::Function *function_ = nullptr;
    int temp_count_ = 0;
    std::map<const Symbol *, std::string> names_;
    std::map<std::string, int> taken_;
    std::vector<Loop> loops_;
};

int Lowerer::emit(ir::Instruction instruction) {
    function_->code.push_back(instruction);
    return here() - 1;
}

std::string Lowerer::name_for(const Symbol *symbol) {
    auto found = names_.find(symbol);
    if (found != names_.end()) {
        return found->second;
    }
    std::string name = symbol->name;
    int count = taken_[name]++;
    if (count > 0 || ir::is_temp(name)) {
        name += "#" + std::to_string(count + 1);
    }
    names_[symbol] = name;
    return name;
}

ir::Program Lowerer::run(const Program &program) {
    ir::Program result;
    for (const auto &func : program.functions) {
        result.functions.emplace_back();
        function_ = &result.functions.back();
        lower_function(*func);
    }
    return result;
}

void Lowerer::lower_function(const FuncDecl &func) {
    function_->name = std::string(func.name);
    function_->returns_value = func.symbol->type != Type::Void;
    temp_count_ = 0;
    names_.clear();
    taken_.clear();
    for (const Param &param : func.params) {
        function_->params.push_back(name_for(param.symbol));
    }
    lower_block(*func.body);
    emit({ir::Op::Return, "", "", ""});
}

void Lowerer::lower_block(const BlockStmt &block) {
    for (const auto &stmt : block.statements) {
        lower_stmt(*stmt);
    }
}

void Lowerer::lower_stmt(const Stmt &stmt) {
    switch (stmt.kind) {
    case StmtKind::Block:
        lower_block(static_cast<const BlockStmt &>(stmt));
        return;
    case StmtKind::Var: {
        const auto &var = static_cast<const VarStmt &>(stmt);
        std::string value = var.init ? lower_expr(*var.init) : "0";
        emit({ir::Op::Copy, name_for(var.symbol), value, ""});
        return;
    }
    case StmtKind::Assign: {
        const auto &assign = static_cast<const AssignStmt &>(stmt);
        std::string value = lower_expr(*assign.value);
        std::string target = name_for(assign.target->symbol);
        if (assign.op.kind == TokenKind::Assign) {
            emit({ir::Op::Copy, target, value, ""});
        } else {
            emit({binary_op(assign.op.kind), target, target, value});
        }
        return;
    }
    case StmtKind::IncDec: {
        const auto &incdec = static_cast<const IncDecStmt &>(stmt);
        std::string target = name_for(incdec.target->symbol);
        emit({binary_op(incdec.op.kind), target, target, "1"});
        return;
    }
    case StmtKind::Expression:
        lower_expr(*static_cast<const ExpressionStmt &>(stmt).expr);
        return;
    case StmtKind::If:
        lower_if(static_cast<const IfStmt &>(stmt));
        return;
    case StmtKind::For:
        lower_for(static_cast<const ForStmt &>(stmt));
        return;
    case StmtKind::Return: {
        const auto &ret = static_cast<const ReturnStmt &>(stmt);
        std::string value = ret.value ? lower_expr(*ret.value) : "";
        emit({ir::Op::Return, "", value, ""});
        return;
    }
    case StmtKind::Break:
        loops_.back().breaks.push_back(emit({ir::Op::Goto, "", "", ""}));
        return;
    case StmtKind::Continue:
        loops_.back().continues.push_back(emit({ir::Op::Goto, "", "", ""}));
        return;
    }
}

void Lowerer::lower_if(const IfStmt &if_stmt) {
    if (if_stmt.init) {
        lower_stmt(*if_stmt.init);
    }
    std::string condition = lower_expr(*if_stmt.condition);
    int skip_then = emit({ir::Op::IfFalse, "", condition, ""});
    lower_block(*if_stmt.then_branch);
    if (if_stmt.else_branch) {
        int skip_else = emit({ir::Op::Goto, "", "", ""});
        patch(skip_then);
        lower_stmt(*if_stmt.else_branch);
        patch(skip_else);
    } else {
        patch(skip_then);
    }
}

void Lowerer::lower_for(const ForStmt &for_stmt) {
    if (for_stmt.init) {
        lower_stmt(*for_stmt.init);
    }
    int start = here();
    int exit_jump = -1;
    if (for_stmt.condition) {
        std::string condition = lower_expr(*for_stmt.condition);
        exit_jump = emit({ir::Op::IfFalse, "", condition, ""});
    }
    loops_.emplace_back();
    lower_block(*for_stmt.body);
    int continue_target = here();
    if (for_stmt.post) {
        lower_stmt(*for_stmt.post);
    }
    ir::Instruction back{ir::Op::Goto, "", "", ""};
    back.target = start;
    emit(back);
    int end = here();
    if (exit_jump >= 0) {
        function_->code[exit_jump].target = end;
    }
    for (int jump : loops_.back().breaks) {
        function_->code[jump].target = end;
    }
    for (int jump : loops_.back().continues) {
        function_->code[jump].target = continue_target;
    }
    loops_.pop_back();
}

std::string Lowerer::lower_expr(const Expr &expr) {
    switch (expr.kind) {
    case ExprKind::Integer:
        return std::to_string(static_cast<const IntegerExpr &>(expr).value);
    case ExprKind::Name: {
        const auto &name = static_cast<const NameExpr &>(expr);
        if (name.symbol->kind == SymbolKind::Constant) {
            return std::to_string(name.symbol->value);
        }
        return name_for(name.symbol);
    }
    case ExprKind::Unary: {
        const auto &unary = static_cast<const UnaryExpr &>(expr);
        std::string operand = lower_expr(*unary.operand);
        std::string temp = new_temp();
        ir::Op op = unary.op.kind == TokenKind::Minus ? ir::Op::Neg : ir::Op::Not;
        emit({op, temp, operand, ""});
        return temp;
    }
    case ExprKind::Binary: {
        const auto &binary = static_cast<const BinaryExpr &>(expr);
        if (binary.op.kind == TokenKind::AndAnd ||
            binary.op.kind == TokenKind::OrOr) {
            return lower_logical(binary);
        }
        std::string left = lower_expr(*binary.left);
        std::string right = lower_expr(*binary.right);
        std::string temp = new_temp();
        emit({binary_op(binary.op.kind), temp, left, right});
        return temp;
    }
    case ExprKind::Call:
        return lower_call(static_cast<const CallExpr &>(expr));
    }
    return "";
}

std::string Lowerer::lower_logical(const BinaryExpr &binary) {
    std::string temp = new_temp();
    std::string left = lower_expr(*binary.left);
    emit({ir::Op::Copy, temp, left, ""});
    ir::Op skip = binary.op.kind == TokenKind::AndAnd ? ir::Op::IfFalse
                                                      : ir::Op::IfTrue;
    int jump = emit({skip, "", temp, ""});
    std::string right = lower_expr(*binary.right);
    emit({ir::Op::Copy, temp, right, ""});
    patch(jump);
    return temp;
}

std::string Lowerer::lower_call(const CallExpr &call) {
    std::vector<std::string> values;
    for (const auto &argument : call.arguments) {
        values.push_back(lower_expr(*argument));
    }
    if (!call.package.empty()) {
        for (std::size_t i = 0; i < values.size(); i++) {
            if (i > 0) {
                emit({ir::Op::PrintSpace, "", "", ""});
            }
            ir::Op print = call.arguments[i]->type == Type::Bool
                               ? ir::Op::PrintBool
                               : ir::Op::PrintInt;
            emit({print, "", values[i], ""});
        }
        emit({ir::Op::PrintNewline, "", "", ""});
        return "";
    }
    for (const std::string &value : values) {
        emit({ir::Op::Param, "", value, ""});
    }
    std::string count = std::to_string(values.size());
    std::string callee = std::string(call.callee);
    if (call.type == Type::Void) {
        emit({ir::Op::Call, "", callee, count});
        return "";
    }
    std::string temp = new_temp();
    emit({ir::Op::Call, temp, callee, count});
    return temp;
}

} // namespace

ir::Program lower(const Program &program) { return Lowerer().run(program); }

} // namespace thoth::go
