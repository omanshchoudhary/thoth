#include "c/lexer.hpp"
#include "source.hpp"

#include <iostream>
#include <string_view>

int main(int argc, char *argv[]) {
    bool emit_tokens = false;
    int file_argument = 1;
    if (argc > 1 && std::string_view(argv[1]) == "--emit=tokens") {
        emit_tokens = true;
        file_argument = 2;
    } else if (argc > 1 && std::string_view(argv[1]).starts_with("--emit=")) {
        std::cerr << "thoth: error: unknown emit format\n";
        return 1;
    }

    if (argc != file_argument + 1) {
        std::cerr << "Usage: thoth [--emit=tokens] <file>\n";
        return 1;
    }

    const char *file_path = argv[file_argument];
    auto file_content = thoth::read_source(file_path);
    if (!file_content) {
        std::cerr << "thoth: error: cannot open '" << file_path << "'\n";
        return 1;
    }

    auto result = thoth::c::lex(*file_content);

    if (emit_tokens) {
        for (const auto &token : result.tokens) {
            std::cout << token.location.line << ':' << token.location.column
                      << ' ' << thoth::c::to_string(token.kind) << ' '
                      << token.lexeme << '\n';
        }
    }

    for (const auto &diagnostic : result.diagnostics) {
        std::cerr << file_path << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column
                  << ": error: " << diagnostic.message << '\n';
    }

    if (!result.diagnostics.empty())
        return 1;
    return 0;
}
