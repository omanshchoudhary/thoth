#pragma once

#include <string>

#include "source.hpp"

namespace thoth {

// One compiler error: where it happened, and what to tell the user.
struct Diagnostic {
    SourceLocation location;
    std::string message;
};

} // namespace thoth
