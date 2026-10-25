#pragma once

#include <string>
#include <vector>

#include "ir/ir.hpp"

namespace thoth::ir {

struct Block {
    int first = 0;
    int last = 0;
    std::vector<int> next;
};

std::vector<Block> build_blocks(const Function &function);
void optimize(Program &program);
std::string dump_cfg(const Program &program);

} // namespace thoth::ir
