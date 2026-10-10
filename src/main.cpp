#include "source.hpp"

#include <iostream>

int main(int argc, char *argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: thoth <file>\n";
        return 1;
    }

    const char *file_path = argv[1];
    auto file_content = thoth::read_source(file_path);
    if (!file_content) {
        std::cerr << "thoth: error: cannot open '" << file_path << "'\n";
        return 1;
    }

    return 0;
}
