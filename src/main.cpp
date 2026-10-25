#include "go/ast.hpp"
#include "go/lexer.hpp"
#include "go/lower.hpp"
#include "go/parser.hpp"
#include "go/sema.hpp"
#include "ir/ir.hpp"
#include "ir/optimize.hpp"
#include "source.hpp"
#include "x86/codegen.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct Options {
    std::string emit;
    bool optimize = true;
    std::string output;
    std::string input;
};

int usage() {
    std::cerr << "Usage: thoth [--emit=tokens|ast|symbols|tac|cfg|asm] [-O0] "
                 "[-o output] <file>\n";
    return 1;
}

bool known_emit(const std::string &emit) {
    return emit == "tokens" || emit == "ast" || emit == "symbols" ||
           emit == "tac" || emit == "cfg" || emit == "asm";
}

bool parse_options(int argc, char *argv[], Options &options) {
    for (int i = 1; i < argc; i++) {
        std::string_view arg = argv[i];
        if (arg.starts_with("--emit=")) {
            options.emit = std::string(arg.substr(7));
            if (!known_emit(options.emit)) {
                std::cerr << "thoth: error: unknown emit format '"
                          << options.emit << "'\n";
                return false;
            }
        } else if (arg == "-O0") {
            options.optimize = false;
        } else if (arg == "-O1") {
            options.optimize = true;
        } else if (arg == "-o") {
            if (i + 1 >= argc) {
                usage();
                return false;
            }
            options.output = argv[++i];
        } else if (arg.starts_with("-")) {
            std::cerr << "thoth: error: unknown option '" << arg << "'\n";
            return false;
        } else if (options.input.empty()) {
            options.input = std::string(arg);
        } else {
            usage();
            return false;
        }
    }
    if (options.input.empty()) {
        usage();
        return false;
    }
    return true;
}

bool comes_before(const thoth::Diagnostic &a, const thoth::Diagnostic &b) {
    if (a.location.line != b.location.line) {
        return a.location.line < b.location.line;
    }
    return a.location.column < b.location.column;
}

bool report(const std::string &path, std::vector<thoth::Diagnostic> diagnostics) {
    std::stable_sort(diagnostics.begin(), diagnostics.end(), comes_before);
    for (const auto &diagnostic : diagnostics) {
        std::cerr << path << ':' << diagnostic.location.line << ':'
                  << diagnostic.location.column << ": error: "
                  << diagnostic.message << '\n';
    }
    return !diagnostics.empty();
}

int assemble(const Options &options, const std::string &assembly) {
    std::string output = options.output;
    if (output.empty()) {
        output = std::filesystem::path(options.input).stem().string();
    }
    std::string assembly_path = output + ".s";
    {
        std::ofstream file(assembly_path);
        if (!file) {
            std::cerr << "thoth: error: cannot write '" << assembly_path
                      << "'\n";
            return 1;
        }
        file << assembly;
    }
    std::string command = "gcc -o '" + output + "' '" + assembly_path + "'";
    int status = std::system(command.c_str());
    std::filesystem::remove(assembly_path);
    if (status != 0) {
        std::cerr << "thoth: error: assembling with gcc failed\n";
        return 1;
    }
    return 0;
}

int run_go(const Options &options, const std::string &source) {
    auto lexed = thoth::go::lex(source);
    if (options.emit == "tokens") {
        for (const auto &token : lexed.tokens) {
            std::cout << token.location.line << ':' << token.location.column
                      << ' ' << thoth::go::to_string(token.kind) << ' ';
            if (token.kind == thoth::go::TokenKind::Semicolon &&
                token.lexeme.empty()) {
                std::cout << "(inserted)";
            } else {
                std::cout << token.lexeme;
            }
            std::cout << '\n';
        }
    }
    if (report(options.input, lexed.diagnostics)) {
        return 1;
    }
    if (options.emit == "tokens") {
        return 0;
    }

    auto parsed = thoth::go::parse(lexed.tokens);
    if (report(options.input, parsed.diagnostics)) {
        return 1;
    }
    if (options.emit == "ast") {
        std::cout << thoth::go::dump(*parsed.program);
        return 0;
    }

    auto checked = thoth::go::analyze(*parsed.program);
    if (report(options.input, checked.diagnostics)) {
        return 1;
    }
    if (options.emit == "symbols") {
        std::cout << checked.symbol_table;
        return 0;
    }

    thoth::ir::Program program = thoth::go::lower(*parsed.program);
    if (options.emit == "tac") {
        std::cout << thoth::ir::dump(program);
        return 0;
    }
    if (options.optimize) {
        thoth::ir::optimize(program);
    }
    if (options.emit == "cfg") {
        std::cout << thoth::ir::dump_cfg(program);
        return 0;
    }

    std::string assembly = thoth::x86::generate(program);
    if (options.emit == "asm") {
        std::cout << assembly;
        return 0;
    }
    return assemble(options, assembly);
}

} // namespace

int main(int argc, char *argv[]) {
    Options options;
    if (!parse_options(argc, argv, options)) {
        return 1;
    }
    auto source = thoth::read_source(options.input);
    if (!source) {
        std::cerr << "thoth: error: cannot open '" << options.input << "'\n";
        return 1;
    }
    return run_go(options, *source);
}
