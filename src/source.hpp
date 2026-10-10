#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace thoth {

std::optional<std::string> read_source(const std::filesystem::path &path);

}
