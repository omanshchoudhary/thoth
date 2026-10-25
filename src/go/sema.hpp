#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "go/ast.hpp"

namespace thoth::go {

enum class SymbolKind {
    Variable,
    Parameter,
    Function,
    Constant,
    TypeName,
    Package,
};

struct Symbol {
    std::string name;
    SymbolKind kind;
    Type type = Type::Invalid;
    SourceLocation location;
    bool used = false;
    std::int64_t value = 0;
    std::vector<Type> params;
};

struct SemaResult {
    std::vector<std::unique_ptr<Symbol>> symbols;
    std::vector<Diagnostic> diagnostics;
    std::string symbol_table;
};

SemaResult analyze(Program &program);

} // namespace thoth::go
