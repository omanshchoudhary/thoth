#pragma once

#include <string>

#include "ir/ir.hpp"

namespace thoth::x86 {

std::string generate(const ir::Program &program);

} // namespace thoth::x86
