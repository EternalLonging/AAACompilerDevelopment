#include "minic/modules.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

void usage(std::ostream& output) {
    output << "用法：minic <tokens|parse|symbols|check|ir|run> <源文件>\n";
}

}

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string(argv[1]) == "--help") { usage(std::cout); return 0; }
    if (argc != 3) { usage(std::cerr); return 1; }
    const std::string command = argv[1];
    minic::CompileTarget target;
    if (command == "tokens") target = minic::CompileTarget::Tokens;
    else if (command == "parse") target = minic::CompileTarget::Parse;
    else if (command == "symbols" || command == "check") target = minic::CompileTarget::Check;
    else if (command == "ir" || command == "run") target = minic::CompileTarget::IR;
    else { usage(std::cerr); return 1; }
    std::ifstream file(argv[2], std::ios::binary);
    if (!file) { std::cerr << "无法打开源文件：" << argv[2] << '\n'; return 1; }
    const std::string source{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    if (file.bad()) { std::cerr << "读取源文件失败\n"; return 1; }
    try {
        auto compilation = minic::compile(source, argv[2], target);
        minic::print_diagnostics(compilation.diagnostics, std::cerr);
        // 检查失败时仍可查看已经生成的部分结果。
        if (command == "tokens" && compilation.lexical)
            minic::print_tokens(compilation.lexical->tokens, std::cout);
        else if (command == "parse" && compilation.syntax && compilation.syntax->root)
            minic::print_ast(*compilation.syntax->root, std::cout);
        else if (command == "symbols" && compilation.semantic)
            minic::print_symbols(compilation.semantic->symbols, std::cout);
        else if (command == "ir" && compilation.ir && compilation.semantic)
            minic::print_ir(compilation.ir->program, compilation.semantic->symbols, std::cout);
        if (!compilation.ok()) return 1;
        if (command == "check") std::cout << "语义检查通过\n";
        if (command == "run") {
            const auto execution = minic::run(compilation.ir->program, compilation.semantic->symbols, std::cin, std::cout);
            minic::print_diagnostics(execution.diagnostics, std::cerr);
            if (!execution.ok()) return 1;
            return static_cast<int>(*execution.exit_code);
        }
        return std::cout ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << "编译器执行失败：" << error.what() << '\n';
        return 1;
    }
}
