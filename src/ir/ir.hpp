#pragma once

#include <string>
#include <vector>

namespace thoth::ir {

enum class Op {
    Copy,
    Neg,
    Not,
    Add,
    Sub,
    Mul,
    Div,
    Mod,
    Equal,
    NotEqual,
    Less,
    LessEqual,
    Greater,
    GreaterEqual,
    Goto,
    IfFalse,
    IfTrue,
    Param,
    Call,
    Return,
    PrintInt,
    PrintBool,
    PrintSpace,
    PrintNewline,
    Nop,
};

struct Instruction {
    Op op;
    std::string result;
    std::string arg1;
    std::string arg2;
    int target = -1;
};

struct Function {
    std::string name;
    std::vector<std::string> params;
    bool returns_value = false;
    std::vector<Instruction> code;
};

struct Program {
    std::vector<Function> functions;
};

bool is_constant(const std::string &operand);
bool is_temp(const std::string &operand);
bool is_jump(Op op);
bool is_binary(Op op);
bool defines_result(const Instruction &instruction);
std::vector<std::string> uses(const Instruction &instruction);

std::string to_string(const Instruction &instruction);
std::string dump(const Program &program);

} // namespace thoth::ir
