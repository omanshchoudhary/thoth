#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

namespace thoth {

struct SourceLocation {
    std::size_t line = 1;
    std::size_t column = 1;

    // comparing two locations
    bool operator==(const SourceLocation &other) const {
        return line == other.line && column == other.column;
    }
};

std::optional<std::string> read_source(const std::filesystem::path &path);

} // namespace thoth
