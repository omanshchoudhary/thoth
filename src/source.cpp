#include "source.hpp"

#include <fstream>
#include <iterator>

namespace thoth {

std::optional<std::string> read_source(const std::filesystem::path &path) {
    // path isn't a regular file, return "nothing"
    if (!std::filesystem::is_regular_file(path)) {
        return std::nullopt;
    }
    // Open the file in binary mode
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    return std::string(std::istreambuf_iterator<char>(file),
                       std::istreambuf_iterator<char>());
}

} // namespace thoth
