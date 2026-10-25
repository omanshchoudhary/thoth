#pragma once

#include <memory>
#include <vector>

#include "diagnostic.hpp"
#include "go/ast.hpp"
#include "go/token.hpp"

namespace thoth::go {

struct ParseResult {
    std::unique_ptr<Program> program;
    std::vector<Diagnostic> diagnostics;
};

ParseResult parse(const std::vector<Token> &tokens);

} // namespace thoth::go
