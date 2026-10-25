#pragma once

#include <string_view>
#include <vector>

#include "diagnostic.hpp"
#include "go/token.hpp"

namespace thoth::go {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

LexResult lex(std::string_view source);

} // namespace thoth::go
