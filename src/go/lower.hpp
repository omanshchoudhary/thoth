#pragma once

#include "go/ast.hpp"
#include "ir/ir.hpp"

namespace thoth::go {

ir::Program lower(const Program &program);

} // namespace thoth::go
