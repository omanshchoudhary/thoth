#include "x86/codegen.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace thoth::x86 {

namespace {

// system v order for the first six arguments
const std::string ARGUMENT_REGISTERS[] = {"rdi", "rsi", "rdx", "rcx", "r8", "r9"};
// registers that can hold variables; rax, rdx and r11 stay free as scratch
const std::string POOL[] = {"rcx", "rsi", "rdi", "r8", "r9", "r10"};
const int POOL_SIZE = 6;
const int NEVER = 1000000000;
const std::string DIVIDE_MESSAGE = "panic: runtime error: integer divide by zero\n";

// what a pool register holds and whether its slot in memory is out of date
struct Register {
    std::string holds;
    bool dirty = false;
};

// x86 instructions only take 32-bit immediates
bool fits_in_32_bits(const std::string &constant) {
    std::int64_t value = std::stoll(constant);
    return value >= -2147483648LL && value <= 2147483647LL;
}

// writes one function: stack frame, then one template per instruction
class FunctionWriter {
  public:
    FunctionWriter(const ir::Function &function, std::string &out)
        : function_(function), out_(out) {}

    void write();

  private:
    void line(const std::string &text) { out_ += "    " + text + "\n"; }
    std::string label(int index) const {
        return ".L" + function_.name + "." + std::to_string(index);
    }
    std::string slot(const std::string &name) const {
        return "QWORD PTR [rbp-" + std::to_string(8 * slots_.at(name)) + "]";
    }
    void add_slot(const std::string &name);

    int find(const std::string &name) const;
    int next_use(const std::string &name, int index) const;
    int allocate(int index, int pinned);
    int load(const std::string &operand, int index, int pinned);
    std::string source(const std::string &operand, int index, int pinned);
    void store_rax(const std::string &name, int index);
    void spill(int reg);
    void spill_all();
    void forget_all();
    std::string value_into(const std::string &reg, const std::string &operand);

    void write_instruction(int index);
    void write_binary(const ir::Instruction &instruction, int index);
    void write_division(const ir::Instruction &instruction, int index);
    void write_call(const ir::Instruction &instruction);
    void write_print(const ir::Instruction &instruction);

    const ir::Function &function_;
    std::string &out_;
    std::map<std::string, int> slots_;
    std::vector<bool> leader_;
    std::vector<bool> target_;
    std::vector<int> block_end_;
    Register registers_[POOL_SIZE];
    std::vector<std::string> params_;
};

// every name gets an 8-byte slot at [rbp - 8 * n]
void FunctionWriter::add_slot(const std::string &name) {
    if (name.empty() || ir::is_constant(name) || slots_.count(name)) {
        return;
    }
    int next = slots_.size() + 1;
    slots_[name] = next;
}

// which register holds this name, -1 if none
int FunctionWriter::find(const std::string &name) const {
    for (int r = 0; r < POOL_SIZE; r++) {
        if (registers_[r].holds == name) {
            return r;
        }
    }
    return -1;
}

// next-use information: the next step in this block that reads the name
int FunctionWriter::next_use(const std::string &name, int index) const {
    for (int i = index + 1; i <= block_end_[index]; i++) {
        const ir::Instruction &instruction = function_.code[i];
        for (const std::string &used : ir::uses(instruction)) {
            if (used == name) {
                return i;
            }
        }
        if (instruction.result == name) {
            return NEVER;
        }
    }
    return NEVER;
}

// a free register, or evict the one whose value is needed furthest away
int FunctionWriter::allocate(int index, int pinned) {
    for (int r = 0; r < POOL_SIZE; r++) {
        if (r != pinned && registers_[r].holds.empty()) {
            return r;
        }
    }
    int victim = -1;
    int farthest = -1;
    for (int r = 0; r < POOL_SIZE; r++) {
        if (r == pinned) {
            continue;
        }
        int use = next_use(registers_[r].holds, index);
        if (use > farthest) {
            farthest = use;
            victim = r;
        }
    }
    spill(victim);
    registers_[victim].holds.clear();
    return victim;
}

// make sure a value is in a register, reading it from its slot if needed
int FunctionWriter::load(const std::string &operand, int index, int pinned) {
    int found = find(operand);
    if (found >= 0) {
        return found;
    }
    int reg = allocate(index, pinned);
    if (ir::is_constant(operand)) {
        line("mov " + POOL[reg] + ", " + operand);
    } else {
        line("mov " + POOL[reg] + ", " + slot(operand));
    }
    registers_[reg].holds = operand;
    registers_[reg].dirty = false;
    return reg;
}

// small constants can be used directly as an immediate
std::string FunctionWriter::source(const std::string &operand, int index,
                                   int pinned) {
    if (ir::is_constant(operand) && fits_in_32_bits(operand)) {
        return operand;
    }
    return POOL[load(operand, index, pinned)];
}

// the result is in rax; keep it in a register and mark it dirty
void FunctionWriter::store_rax(const std::string &name, int index) {
    int reg = find(name);
    if (reg < 0) {
        reg = allocate(index, -1);
        registers_[reg].holds = name;
    }
    registers_[reg].dirty = true;
    line("mov " + POOL[reg] + ", rax");
}

// write a changed register back to its slot
void FunctionWriter::spill(int reg) {
    Register &r = registers_[reg];
    if (r.dirty && !r.holds.empty()) {
        line("mov " + slot(r.holds) + ", " + POOL[reg]);
    }
    r.dirty = false;
}

// before jumps, calls and block ends memory has to be up to date
void FunctionWriter::spill_all() {
    for (int r = 0; r < POOL_SIZE; r++) {
        spill(r);
    }
}

// after a call or at a new block, register contents can't be trusted
void FunctionWriter::forget_all() {
    for (int r = 0; r < POOL_SIZE; r++) {
        registers_[r].holds.clear();
        registers_[r].dirty = false;
    }
}

// mov from a constant, a register or a slot, whichever has the value
std::string FunctionWriter::value_into(const std::string &reg,
                                       const std::string &operand) {
    if (ir::is_constant(operand)) {
        return "mov " + reg + ", " + operand;
    }
    int cached = find(operand);
    if (cached >= 0) {
        return "mov " + reg + ", " + POOL[cached];
    }
    return "mov " + reg + ", " + slot(operand);
}

// frame layout, block boundaries and labels, then every instruction
void FunctionWriter::write() {
    int count = function_.code.size();
    for (const std::string &param : function_.params) {
        add_slot(param);
    }
    for (const ir::Instruction &instruction : function_.code) {
        add_slot(instruction.result);
        for (const std::string &name : ir::uses(instruction)) {
            add_slot(name);
        }
    }

    leader_.assign(count, false);
    target_.assign(count, false);
    if (count > 0) {
        leader_[0] = true;
    }
    for (int i = 0; i < count; i++) {
        const ir::Instruction &instruction = function_.code[i];
        if (ir::is_jump(instruction.op) && instruction.target < count) {
            target_[instruction.target] = true;
            leader_[instruction.target] = true;
        }
        if ((ir::is_jump(instruction.op) || instruction.op == ir::Op::Return ||
             instruction.op == ir::Op::Call) &&
            i + 1 < count) {
            leader_[i + 1] = true;
        }
    }
    block_end_.assign(count, 0);
    for (int i = count - 1; i >= 0; i--) {
        bool last = i + 1 >= count || leader_[i + 1];
        block_end_[i] = last ? i : block_end_[i + 1];
    }

    int frame = 8 * slots_.size();
    // the stack has to stay 16-byte aligned for calls
    frame = (frame + 15) / 16 * 16;

    out_ += "\n.globl go." + function_.name + "\n";
    out_ += "go." + function_.name + ":\n";
    line("push rbp");
    line("mov rbp, rsp");
    if (frame > 0) {
        line("sub rsp, " + std::to_string(frame));
    }
    for (std::size_t i = 0; i < function_.params.size(); i++) {
        line("mov " + slot(function_.params[i]) + ", " + ARGUMENT_REGISTERS[i]);
    }
    for (int i = 0; i < count; i++) {
        if (leader_[i] && i > 0) {
            spill_all();
            forget_all();
        }
        if (target_[i]) {
            out_ += label(i) + ":\n";
        }
        write_instruction(i);
    }
    out_ += label(count) + ":\n";
    line("leave");
    line("ret");
}

// one template per instruction kind
void FunctionWriter::write_instruction(int index) {
    const ir::Instruction &instruction = function_.code[index];
    switch (instruction.op) {
    case ir::Op::Copy: {
        if (instruction.arg1 == instruction.result) {
            return;
        }
        line(value_into("rax", instruction.arg1));
        store_rax(instruction.result, index);
        return;
    }
    case ir::Op::Neg:
        line(value_into("rax", instruction.arg1));
        line("neg rax");
        store_rax(instruction.result, index);
        return;
    case ir::Op::Not:
        line(value_into("rax", instruction.arg1));
        line("xor rax, 1");
        store_rax(instruction.result, index);
        return;
    case ir::Op::Div:
    case ir::Op::Mod:
        write_division(instruction, index);
        return;
    case ir::Op::Goto:
        spill_all();
        line("jmp " + label(instruction.target));
        return;
    case ir::Op::IfFalse:
    case ir::Op::IfTrue: {
        line(value_into("rax", instruction.arg1));
        spill_all();
        line("test rax, rax");
        line((instruction.op == ir::Op::IfFalse ? "je " : "jne ") +
             label(instruction.target));
        return;
    }
    case ir::Op::Param:
        params_.push_back(instruction.arg1);
        return;
    case ir::Op::Call:
        write_call(instruction);
        return;
    case ir::Op::Return:
        if (!instruction.arg1.empty()) {
            line(value_into("rax", instruction.arg1));
        }
        line("leave");
        line("ret");
        return;
    case ir::Op::PrintInt:
    case ir::Op::PrintBool:
    case ir::Op::PrintSpace:
    case ir::Op::PrintNewline:
        write_print(instruction);
        return;
    case ir::Op::Nop:
        return;
    default:
        write_binary(instruction, index);
        return;
    }
}

// left operand in rax, combine with the right one, store the result
void FunctionWriter::write_binary(const ir::Instruction &instruction,
                                  int index) {
    line(value_into("rax", instruction.arg1));
    std::string right = source(instruction.arg2, index, -1);
    switch (instruction.op) {
    case ir::Op::Add:
        line("add rax, " + right);
        break;
    case ir::Op::Sub:
        line("sub rax, " + right);
        break;
    case ir::Op::Mul:
        line("imul rax, " + right);
        break;
    default: {
        std::string set = "sete";
        if (instruction.op == ir::Op::NotEqual)
            set = "setne";
        else if (instruction.op == ir::Op::Less)
            set = "setl";
        else if (instruction.op == ir::Op::LessEqual)
            set = "setle";
        else if (instruction.op == ir::Op::Greater)
            set = "setg";
        else if (instruction.op == ir::Op::GreaterEqual)
            set = "setge";
        line("cmp rax, " + right);
        line(set + " al");
        line("movzx eax, al");
        break;
    }
    }
    store_rax(instruction.result, index);
}

// idiv faults on 0 and on MinInt / -1, so both are handled before it
void FunctionWriter::write_division(const ir::Instruction &instruction,
                                    int index) {
    std::string tag = function_.name + "." + std::to_string(index);
    line(value_into("r11", instruction.arg2));
    line(value_into("rax", instruction.arg1));
    line("test r11, r11");
    line("je .Ldivide_by_zero");
    line("cmp r11, -1");
    line("je .Lminus_one." + tag);
    line("cqo");
    line("idiv r11");
    if (instruction.op == ir::Op::Mod) {
        line("mov rax, rdx");
    }
    line("jmp .Ldivided." + tag);
    out_ += ".Lminus_one." + tag + ":\n";
    if (instruction.op == ir::Op::Div) {
        line("neg rax");
    } else {
        line("xor eax, eax");
    }
    out_ += ".Ldivided." + tag + ":\n";
    store_rax(instruction.result, index);
}

// arguments go in registers; the result comes back in rax
void FunctionWriter::write_call(const ir::Instruction &instruction) {
    spill_all();
    forget_all();
    for (std::size_t i = 0; i < params_.size(); i++) {
        line(value_into(ARGUMENT_REGISTERS[i], params_[i]));
    }
    params_.clear();
    line("call go." + instruction.arg1);
    if (!instruction.result.empty()) {
        line("mov " + slot(instruction.result) + ", rax");
    }
}

// fmt.Println is done with printf and putchar from the c library
void FunctionWriter::write_print(const ir::Instruction &instruction) {
    spill_all();
    forget_all();
    switch (instruction.op) {
    case ir::Op::PrintInt:
        line(value_into("rsi", instruction.arg1));
        line("lea rdi, [rip + .Lformat_int]");
        line("xor eax, eax");
        line("call printf@PLT");
        return;
    case ir::Op::PrintBool:
        line(value_into("rax", instruction.arg1));
        line("test rax, rax");
        line("lea rdi, [rip + .Lfalse]");
        line("lea rdx, [rip + .Ltrue]");
        line("cmovne rdi, rdx");
        line("xor eax, eax");
        line("call printf@PLT");
        return;
    case ir::Op::PrintSpace:
        line("mov edi, 32");
        line("call putchar@PLT");
        return;
    default:
        line("mov edi, 10");
        line("call putchar@PLT");
        return;
    }
}

} // namespace

// strings, a c main that calls go.main, the panic handler, then every function
std::string generate(const ir::Program &program) {
    std::string out;
    out += ".intel_syntax noprefix\n";
    out += ".section .rodata\n";
    out += ".Lformat_int:\n    .string \"%ld\"\n";
    out += ".Ltrue:\n    .string \"true\"\n";
    out += ".Lfalse:\n    .string \"false\"\n";
    out += ".Ldivide_message:\n    .ascii \"panic: runtime error: integer "
           "divide by zero\\n\"\n";
    out += ".text\n";
    out += "\n.globl main\nmain:\n";
    out += "    push rbp\n    mov rbp, rsp\n    call go.main\n";
    out += "    xor eax, eax\n    pop rbp\n    ret\n";
    out += "\n.Ldivide_by_zero:\n";
    out += "    xor edi, edi\n    call fflush@PLT\n";
    out += "    mov eax, 1\n    mov edi, 2\n";
    out += "    lea rsi, [rip + .Ldivide_message]\n";
    out += "    mov edx, " + std::to_string(DIVIDE_MESSAGE.size()) + "\n";
    out += "    syscall\n    mov edi, 2\n    call exit@PLT\n";
    for (const ir::Function &function : program.functions) {
        FunctionWriter(function, out).write();
    }
    out += "\n.section .note.GNU-stack,\"\",@progbits\n";
    return out;
}

} // namespace thoth::x86
