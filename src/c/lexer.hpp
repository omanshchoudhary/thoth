#pragma once

#include <string_view>
#include <vector>

#include "c/token.hpp"
#include "diagnostic.hpp"

namespace thoth::c {

struct LexResult {
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

LexResult lex(std::string_view source);

} // namespace thoth::c
