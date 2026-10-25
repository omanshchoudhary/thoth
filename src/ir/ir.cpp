#include "ir/ir.hpp"

#include <cstddef>

namespace thoth::ir {

// constants start with a digit or a minus sign; names never do
bool is_constant(const std::string &operand) {
    return !operand.empty() &&
           ((operand[0] >= '0' && operand[0] <= '9') || operand[0] == '-');
}

// temporaries are t followed by digits
bool is_temp(const std::string &operand) {
    if (operand.size() < 2 || operand[0] != 't') {
        return false;
    }
    for (std::size_t i = 1; i < operand.size(); i++) {
        if (operand[i] < '0' || operand[i] > '9') {
            return false;
        }
    }
    return true;
}

// instructions with a target
bool is_jump(Op op) {
    return op == Op::Goto || op == Op::IfFalse || op == Op::IfTrue;
}

// instructions with two inputs
bool is_binary(Op op) {
    switch (op) {
    case Op::Add:
    case Op::Sub:
    case Op::Mul:
    case Op::Div:
    case Op::Mod:
    case Op::Equal:
    case Op::NotEqual:
    case Op::Less:
    case Op::LessEqual:
    case Op::Greater:
    case Op::GreaterEqual:
        return true;
    default:
        return false;
    }
}

// instructions that write a name
bool defines_result(const Instruction &instruction) {
    return !instruction.result.empty();
}

namespace {

// constants are not names, so they are skipped
void add_use(std::vector<std::string> &names, const std::string &operand) {
    if (!operand.empty() && !is_constant(operand)) {
        names.push_back(operand);
    }
}

} // namespace

// names an instruction reads; used by liveness and register allocation
std::vector<std::string> uses(const Instruction &instruction) {
    std::vector<std::string> names;
    switch (instruction.op) {
    case Op::Call:
    case Op::Goto:
    case Op::PrintSpace:
    case Op::PrintNewline:
    case Op::Nop:
        break;
    default:
        add_use(names, instruction.arg1);
        if (is_binary(instruction.op)) {
            add_use(names, instruction.arg2);
        }
        break;
    }
    return names;
}

namespace {

// operator text for printing
const char *symbol(Op op) {
    switch (op) {
    case Op::Add:
        return "+";
    case Op::Sub:
        return "-";
    case Op::Mul:
        return "*";
    case Op::Div:
        return "/";
    case Op::Mod:
        return "%";
    case Op::Equal:
        return "==";
    case Op::NotEqual:
        return "!=";
    case Op::Less:
        return "<";
    case Op::LessEqual:
        return "<=";
    case Op::Greater:
        return ">";
    case Op::GreaterEqual:
        return ">=";
    default:
        return "?";
    }
}

} // namespace

// one instruction in the --emit=tac format
std::string to_string(const Instruction &i) {
    std::string target = std::to_string(i.target);
    switch (i.op) {
    case Op::Copy:
        return i.result + " = " + i.arg1;
    case Op::Neg:
        return i.result + " = -" + i.arg1;
    case Op::Not:
        return i.result + " = !" + i.arg1;
    case Op::Goto:
        return "goto " + target;
    case Op::IfFalse:
        return "ifFalse " + i.arg1 + " goto " + target;
    case Op::IfTrue:
        return "ifTrue " + i.arg1 + " goto " + target;
    case Op::Param:
        return "param " + i.arg1;
    case Op::Call:
        if (i.result.empty()) {
            return "call " + i.arg1 + ", " + i.arg2;
        }
        return i.result + " = call " + i.arg1 + ", " + i.arg2;
    case Op::Return:
        return i.arg1.empty() ? "return" : "return " + i.arg1;
    case Op::PrintInt:
        return "print_int " + i.arg1;
    case Op::PrintBool:
        return "print_bool " + i.arg1;
    case Op::PrintSpace:
        return "print_space";
    case Op::PrintNewline:
        return "print_newline";
    case Op::Nop:
        return "nop";
    default:
        return i.result + " = " + i.arg1 + " " + symbol(i.op) + " " + i.arg2;
    }
}

// every function for --emit=tac
std::string dump(const Program &program) {
    std::string out;
    for (const Function &function : program.functions) {
        out += "func " + function.name + "(";
        for (std::size_t i = 0; i < function.params.size(); i++) {
            if (i > 0) {
                out += ", ";
            }
            out += function.params[i];
        }
        out += ")\n";
        for (std::size_t i = 0; i < function.code.size(); i++) {
            out += "  (" + std::to_string(i) + ") " +
                   to_string(function.code[i]) + "\n";
        }
    }
    return out;
}

} // namespace thoth::ir
