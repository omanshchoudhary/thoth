#include "ir/optimize.hpp"

#include <cstdint>
#include <map>
#include <set>
#include <string>

namespace thoth::ir {

namespace {

std::vector<bool> find_leaders(const Function &function) {
    int count = function.code.size();
    std::vector<bool> leader(count, false);
    if (count > 0) {
        leader[0] = true;
    }
    for (int i = 0; i < count; i++) {
        const Instruction &instruction = function.code[i];
        if (is_jump(instruction.op) && instruction.target >= 0 &&
            instruction.target < count) {
            leader[instruction.target] = true;
        }
        if ((is_jump(instruction.op) || instruction.op == Op::Return) &&
            i + 1 < count) {
            leader[i + 1] = true;
        }
    }
    return leader;
}

std::vector<int> block_of_each(const std::vector<Block> &blocks, int count) {
    std::vector<int> owner(count, 0);
    int block_count = blocks.size();
    for (int b = 0; b < block_count; b++) {
        for (int i = blocks[b].first; i <= blocks[b].last; i++) {
            owner[i] = b;
        }
    }
    return owner;
}

std::int64_t constant_value(const std::string &operand) {
    return std::stoll(operand);
}

bool fold_binary(Op op, std::int64_t a, std::int64_t b, std::int64_t &out) {
    std::uint64_t ua = static_cast<std::uint64_t>(a);
    std::uint64_t ub = static_cast<std::uint64_t>(b);
    switch (op) {
    case Op::Add:
        out = static_cast<std::int64_t>(ua + ub);
        return true;
    case Op::Sub:
        out = static_cast<std::int64_t>(ua - ub);
        return true;
    case Op::Mul:
        out = static_cast<std::int64_t>(ua * ub);
        return true;
    case Op::Div:
        if (b == 0) {
            return false;
        }
        out = b == -1 ? static_cast<std::int64_t>(0 - ua) : a / b;
        return true;
    case Op::Mod:
        if (b == 0) {
            return false;
        }
        out = b == -1 ? 0 : a % b;
        return true;
    case Op::Equal:
        out = a == b;
        return true;
    case Op::NotEqual:
        out = a != b;
        return true;
    case Op::Less:
        out = a < b;
        return true;
    case Op::LessEqual:
        out = a <= b;
        return true;
    case Op::Greater:
        out = a > b;
        return true;
    case Op::GreaterEqual:
        out = a >= b;
        return true;
    default:
        return false;
    }
}

void make_copy(Instruction &instruction, const std::string &value) {
    instruction.op = Op::Copy;
    instruction.arg1 = value;
    instruction.arg2.clear();
}

bool is_value(const std::string &operand, std::int64_t number) {
    return is_constant(operand) && constant_value(operand) == number;
}

std::string simplified(const Instruction &i) {
    if (i.op == Op::Add && is_value(i.arg2, 0))
        return i.arg1;
    if (i.op == Op::Add && is_value(i.arg1, 0))
        return i.arg2;
    if (i.op == Op::Sub && is_value(i.arg2, 0))
        return i.arg1;
    if ((i.op == Op::Mul || i.op == Op::Div) && is_value(i.arg2, 1))
        return i.arg1;
    if (i.op == Op::Mul && is_value(i.arg1, 1))
        return i.arg2;
    if (i.op == Op::Mul && (is_value(i.arg1, 0) || is_value(i.arg2, 0)))
        return "0";
    return "";
}

bool fold_constants(Function &function) {
    bool changed = false;
    for (Instruction &i : function.code) {
        bool c1 = is_constant(i.arg1);
        bool c2 = is_constant(i.arg2);
        std::int64_t value = 0;
        std::string simpler = simplified(i);
        if (is_binary(i.op) && c1 && c2 &&
            fold_binary(i.op, constant_value(i.arg1), constant_value(i.arg2),
                        value)) {
            make_copy(i, std::to_string(value));
        } else if (i.op == Op::Neg && c1 &&
                   fold_binary(Op::Sub, 0, constant_value(i.arg1), value)) {
            make_copy(i, std::to_string(value));
        } else if (i.op == Op::Not && c1) {
            make_copy(i, i.arg1 == "0" ? "1" : "0");
        } else if ((i.op == Op::IfFalse || i.op == Op::IfTrue) && c1) {
            bool jumps = (i.arg1 != "0") == (i.op == Op::IfTrue);
            i.op = jumps ? Op::Goto : Op::Nop;
            i.arg1.clear();
        } else if (!simpler.empty()) {
            make_copy(i, simpler);
        } else if (i.op == Op::Copy && i.result == i.arg1) {
            i.op = Op::Nop;
        } else {
            continue;
        }
        changed = true;
    }
    return changed;
}

bool reads_arg1(Op op) {
    return op != Op::Call && op != Op::Goto && op != Op::PrintSpace &&
           op != Op::PrintNewline && op != Op::Nop;
}

void forget(std::map<std::string, std::string> &known, const std::string &name) {
    known.erase(name);
    std::vector<std::string> stale;
    for (const auto &entry : known) {
        if (entry.second == name) {
            stale.push_back(entry.first);
        }
    }
    for (const std::string &key : stale) {
        known.erase(key);
    }
}

bool propagate(Function &function) {
    bool changed = false;
    std::vector<Block> blocks = build_blocks(function);
    for (const Block &block : blocks) {
        std::map<std::string, std::string> known;
        for (int i = block.first; i <= block.last; i++) {
            Instruction &instruction = function.code[i];
            if (reads_arg1(instruction.op) && known.count(instruction.arg1)) {
                instruction.arg1 = known[instruction.arg1];
                changed = true;
            }
            if (is_binary(instruction.op) && known.count(instruction.arg2)) {
                instruction.arg2 = known[instruction.arg2];
                changed = true;
            }
            if (defines_result(instruction)) {
                forget(known, instruction.result);
                if (instruction.op == Op::Copy &&
                    instruction.arg1 != instruction.result) {
                    known[instruction.result] = instruction.arg1;
                }
            }
        }
    }
    return changed;
}

bool merge_copies(Function &function) {
    std::map<std::string, int> use_count;
    std::map<std::string, int> def_count;
    for (const Instruction &instruction : function.code) {
        for (const std::string &name : uses(instruction)) {
            use_count[name]++;
        }
        if (defines_result(instruction)) {
            def_count[instruction.result]++;
        }
    }
    std::vector<bool> leader = find_leaders(function);
    bool changed = false;
    int count = function.code.size();
    for (int i = 0; i + 1 < count; i++) {
        Instruction &first = function.code[i];
        Instruction &second = function.code[i + 1];
        if (first.op == Op::Nop || !is_temp(first.result) || leader[i + 1]) {
            continue;
        }
        if (second.op == Op::Copy && second.arg1 == first.result &&
            use_count[first.result] == 1 && def_count[first.result] == 1) {
            first.result = second.result;
            second.op = Op::Nop;
            changed = true;
        }
    }
    return changed;
}

bool remove_dead_code(Function &function) {
    std::vector<Block> blocks = build_blocks(function);
    int block_count = blocks.size();
    std::vector<std::set<std::string>> used(block_count);
    std::vector<std::set<std::string>> defined(block_count);
    for (int b = 0; b < block_count; b++) {
        for (int i = blocks[b].first; i <= blocks[b].last; i++) {
            const Instruction &instruction = function.code[i];
            for (const std::string &name : uses(instruction)) {
                if (!defined[b].count(name)) {
                    used[b].insert(name);
                }
            }
            if (defines_result(instruction)) {
                defined[b].insert(instruction.result);
            }
        }
    }

    std::vector<std::set<std::string>> live_in(block_count);
    std::vector<std::set<std::string>> live_out(block_count);
    bool stable = false;
    while (!stable) {
        stable = true;
        for (int b = block_count - 1; b >= 0; b--) {
            std::set<std::string> out;
            for (int next : blocks[b].next) {
                for (const std::string &name : live_in[next]) {
                    out.insert(name);
                }
            }
            std::set<std::string> in = used[b];
            for (const std::string &name : out) {
                if (!defined[b].count(name)) {
                    in.insert(name);
                }
            }
            if (in != live_in[b] || out != live_out[b]) {
                live_in[b] = in;
                live_out[b] = out;
                stable = false;
            }
        }
    }

    bool changed = false;
    for (int b = 0; b < block_count; b++) {
        std::set<std::string> live = live_out[b];
        for (int i = blocks[b].last; i >= blocks[b].first; i--) {
            Instruction &instruction = function.code[i];
            if (defines_result(instruction) && !live.count(instruction.result)) {
                changed = true;
                if (instruction.op != Op::Call) {
                    instruction.op = Op::Nop;
                    continue;
                }
                instruction.result.clear();
            }
            if (defines_result(instruction)) {
                live.erase(instruction.result);
            }
            for (const std::string &name : uses(instruction)) {
                live.insert(name);
            }
        }
    }
    return changed;
}

bool remove_unreachable(Function &function) {
    std::vector<Block> blocks = build_blocks(function);
    if (blocks.empty()) {
        return false;
    }
    std::vector<bool> reached(blocks.size(), false);
    std::vector<int> work{0};
    reached[0] = true;
    while (!work.empty()) {
        int b = work.back();
        work.pop_back();
        for (int next : blocks[b].next) {
            if (!reached[next]) {
                reached[next] = true;
                work.push_back(next);
            }
        }
    }
    bool changed = false;
    for (std::size_t b = 0; b < blocks.size(); b++) {
        if (reached[b]) {
            continue;
        }
        for (int i = blocks[b].first; i <= blocks[b].last; i++) {
            if (function.code[i].op != Op::Nop) {
                function.code[i].op = Op::Nop;
                changed = true;
            }
        }
    }
    return changed;
}

bool simplify_jumps(Function &function) {
    bool changed = false;
    int count = function.code.size();
    for (int i = 0; i < count; i++) {
        Instruction &instruction = function.code[i];
        if (!is_jump(instruction.op)) {
            continue;
        }
        int hops = 0;
        while (instruction.target >= 0 && instruction.target < count &&
               function.code[instruction.target].op == Op::Goto &&
               function.code[instruction.target].target != instruction.target &&
               hops < count) {
            instruction.target = function.code[instruction.target].target;
            hops++;
            changed = true;
        }
        if (instruction.op == Op::Goto && instruction.target == i + 1) {
            instruction.op = Op::Nop;
            changed = true;
        }
    }
    return changed;
}

void compact(Function &function) {
    int count = function.code.size();
    std::vector<int> new_index(count + 1, 0);
    int live = 0;
    for (int i = 0; i < count; i++) {
        new_index[i] = live;
        if (function.code[i].op != Op::Nop) {
            live++;
        }
    }
    new_index[count] = live;
    if (live == count) {
        return;
    }
    std::vector<Instruction> kept;
    for (const Instruction &instruction : function.code) {
        if (instruction.op == Op::Nop) {
            continue;
        }
        Instruction copy = instruction;
        if (is_jump(copy.op)) {
            copy.target = new_index[copy.target];
        }
        kept.push_back(copy);
    }
    function.code = kept;
}

void optimize_function(Function &function) {
    for (int round = 0; round < 100; round++) {
        bool changed = false;
        if (fold_constants(function))
            changed = true;
        compact(function);
        if (propagate(function))
            changed = true;
        compact(function);
        if (merge_copies(function))
            changed = true;
        compact(function);
        if (remove_dead_code(function))
            changed = true;
        compact(function);
        if (remove_unreachable(function))
            changed = true;
        compact(function);
        if (simplify_jumps(function))
            changed = true;
        compact(function);
        if (!changed) {
            break;
        }
    }
}

} // namespace

std::vector<Block> build_blocks(const Function &function) {
    std::vector<Block> blocks;
    std::vector<bool> leader = find_leaders(function);
    int count = function.code.size();
    for (int i = 0; i < count; i++) {
        if (leader[i]) {
            blocks.push_back({i, i, {}});
        } else {
            blocks.back().last = i;
        }
    }
    std::vector<int> owner = block_of_each(blocks, count);
    int block_count = blocks.size();
    for (int b = 0; b < block_count; b++) {
        const Instruction &last = function.code[blocks[b].last];
        bool falls_through = last.op != Op::Goto && last.op != Op::Return;
        if (is_jump(last.op) && last.target >= 0 && last.target < count) {
            blocks[b].next.push_back(owner[last.target]);
        }
        if (falls_through && b + 1 < block_count) {
            bool already = false;
            for (int next : blocks[b].next) {
                if (next == b + 1) {
                    already = true;
                }
            }
            if (!already) {
                blocks[b].next.push_back(b + 1);
            }
        }
    }
    return blocks;
}

void optimize(Program &program) {
    for (Function &function : program.functions) {
        optimize_function(function);
    }
}

std::string dump_cfg(const Program &program) {
    std::string out;
    for (const Function &function : program.functions) {
        out += "func " + function.name + "\n";
        std::vector<Block> blocks = build_blocks(function);
        for (std::size_t b = 0; b < blocks.size(); b++) {
            out += "  B" + std::to_string(b) + " (" +
                   std::to_string(blocks[b].first) + "-" +
                   std::to_string(blocks[b].last) + ")";
            if (!blocks[b].next.empty()) {
                out += " ->";
                for (int next : blocks[b].next) {
                    out += " B" + std::to_string(next);
                }
            }
            out += "\n";
            for (int i = blocks[b].first; i <= blocks[b].last; i++) {
                out += "    (" + std::to_string(i) + ") " +
                       to_string(function.code[i]) + "\n";
            }
        }
    }
    return out;
}

} // namespace thoth::ir
